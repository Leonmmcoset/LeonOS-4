/*
 * LeonOS kernel input manager: translates device input into session events.
 * Routes keyboard, mouse, and text-input activity to the active user task.
 */
#include <ntclks/inputm.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/usercopy.h>

#include <leonos/inputm.h>

#define INPUTM_EVENT_CAP 32U
#define INPUTM_RESULT_CAP 32U
#define INPUTM_CONTEXT_CAP 64U
#define INPUTM_UID_CAP LEONOS_AUTH_MAX_USERS

struct inputm_provider_slot {
    uint8_t used;
    uint32_t pid;
    uint32_t uid;
    struct leonos_inputm_provider provider;
    struct leonos_inputm_key_event events[INPUTM_EVENT_CAP];
    uint32_t event_head;
    uint32_t event_tail;
    struct leonos_inputm_key_event issued[INPUTM_EVENT_CAP];
    uint8_t issued_used[INPUTM_EVENT_CAP];
};

struct inputm_result_slot {
    uint8_t used;
    uint32_t pid;
    struct leonos_inputm_result result;
};

struct inputm_context_slot {
    uint8_t used;
    uint32_t pid;
    struct leonos_inputm_context context;
};

struct inputm_user_slot {
    uint8_t used;
    uint32_t uid;
    uint32_t next_sequence;
    struct leonos_inputm_state state;
};

static struct inputm_provider_slot providers[LEONOS_INPUTM_MAX_PROVIDERS];
static struct inputm_result_slot results[INPUTM_RESULT_CAP];
static struct inputm_context_slot contexts[INPUTM_CONTEXT_CAP];
static struct inputm_user_slot users[INPUTM_UID_CAP];

/**
 * @brief Coordinates the inputm copy text operation.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
 */
static void inputm_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

/**
 * @brief Coordinates the inputm text valid operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
static int inputm_text_valid(const char *text, uint32_t cap)
{
    uint32_t i = 0;
    if (!text || !text[0]) {
        return 0;
    }
    while (i < cap && text[i]) {
        uint8_t ch = (uint8_t)text[i];
        if (ch < 0x20U || ch == '/' || ch == '\\' || ch == ':') {
            return 0;
        }
        ++i;
    }
    return i < cap;
}

/**
 * @brief Coordinates the inputm text equal operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int inputm_text_equal(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

/**
 * @brief Coordinates the inputm utf8 valid operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param require_text Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int inputm_utf8_valid(const char *text, uint32_t cap, uint8_t require_text)
{
    uint32_t i = 0;
    if (!text || (require_text && !text[0])) {
        return 0;
    }
    while (i < cap && text[i]) {
        uint8_t first = (uint8_t)text[i++];
        uint32_t continuation = 0;
        uint32_t codepoint;
        uint32_t minimum;
        if (first < 0x20U || first == 0x7fU) {
            return 0;
        }
        if (first < 0x80U) {
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1;
            codepoint = first & 0x1fU;
            minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2;
            codepoint = first & 0x0fU;
            minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return 0;
        }
        if (i + continuation >= cap) {
            return 0;
        }
        while (continuation) {
            uint8_t next = (uint8_t)text[i++];
            if ((next & 0xc0U) != 0x80U) {
                return 0;
            }
            codepoint = (codepoint << 6U) | (uint32_t)(next & 0x3fU);
            --continuation;
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return 0;
        }
    }
    return i < cap;
}

/**
 * @brief Coordinates the inputm task alive operation.
 * @param pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int inputm_task_alive(uint32_t pid)
{
    struct task *task = sched_find(pid);
    return task && task->state != TASK_EXITED;
}

/**
 * @brief Coordinates the inputm find user operation.
 * @param uid Input or output value used by this operation.
 * @param create Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct inputm_user_slot *inputm_find_user(uint32_t uid, uint8_t create)
{
    struct inputm_user_slot *free_slot = 0;
    if (!uid) {
        return 0;
    }
    for (uint32_t i = 0; i < INPUTM_UID_CAP; ++i) {
        if (users[i].used && users[i].uid == uid) {
            return &users[i];
        }
        if (!users[i].used && !free_slot) {
            free_slot = &users[i];
        }
    }
    if (!create || !free_slot) {
        return 0;
    }
    *free_slot = (struct inputm_user_slot){0};
    free_slot->used = 1;
    free_slot->uid = uid;
    free_slot->next_sequence = 1;
    free_slot->state.uid = uid;
    inputm_copy_text(free_slot->state.active_id, sizeof(free_slot->state.active_id), "en");
    return free_slot;
}

/**
 * @brief Coordinates the inputm find provider operation.
 * @param pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct inputm_provider_slot *inputm_find_provider(uint32_t pid)
{
    for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].pid == pid) {
            return &providers[i];
        }
    }
    return 0;
}

/**
 * @brief Coordinates the inputm find provider for user operation.
 * @param uid Input or output value used by this operation.
 * @param id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct inputm_provider_slot *inputm_find_provider_for_user(uint32_t uid, const char *id)
{
    for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].uid == uid &&
            inputm_text_equal(providers[i].provider.id, id) &&
            inputm_task_alive(providers[i].pid)) {
            return &providers[i];
        }
    }
    return 0;
}

/**
 * @brief Coordinates the inputm find issued event operation.
 * @param provider Input or output value used by this operation.
 * @param result Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int inputm_find_issued_event(const struct inputm_provider_slot *provider,
                                    const struct leonos_inputm_result *result)
{
    if (!provider || !result) {
        return -1;
    }
    for (uint32_t i = 0; i < INPUTM_EVENT_CAP; ++i) {
        const struct leonos_inputm_key_event *event = &provider->issued[i];
        if (provider->issued_used[i] && event->sequence == result->sequence &&
            event->client_pid == result->client_pid &&
            event->window_id == result->window_id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Coordinates the inputm find context operation.
 * @param pid Input or output value used by this operation.
 * @param window_id Input or output value used by this operation.
 * @param create Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static struct inputm_context_slot *inputm_find_context(uint32_t pid, uint32_t window_id,
                                                        uint8_t create)
{
    struct inputm_context_slot *free_slot = 0;
    for (uint32_t i = 0; i < INPUTM_CONTEXT_CAP; ++i) {
        if (contexts[i].used && contexts[i].pid == pid &&
            contexts[i].context.window_id == window_id) {
            return &contexts[i];
        }
        if (!contexts[i].used && !free_slot) {
            free_slot = &contexts[i];
        }
    }
    if (!create || !free_slot) {
        return 0;
    }
    *free_slot = (struct inputm_context_slot){0};
    free_slot->used = 1;
    free_slot->pid = pid;
    free_slot->context.window_id = window_id;
    return free_slot;
}

/**
 * @brief Coordinates the inputm queue event operation.
 * @param provider Input or output value used by this operation.
 * @param event Input or output value used by this operation.
 */
static void inputm_queue_event(struct inputm_provider_slot *provider,
                               const struct leonos_inputm_key_event *event)
{
    uint32_t next;
    if (!provider || !event) {
        return;
    }
    next = (provider->event_head + 1U) % INPUTM_EVENT_CAP;
    if (next == provider->event_tail) {
        provider->event_tail = (provider->event_tail + 1U) % INPUTM_EVENT_CAP;
    }
    provider->events[provider->event_head] = *event;
    provider->event_head = next;
}

/**
 * @brief Coordinates the inputm queue result operation.
 * @param pid Input or output value used by this operation.
 * @param result Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int inputm_queue_result(uint32_t pid, const struct leonos_inputm_result *result)
{
    for (uint32_t i = 0; i < INPUTM_RESULT_CAP; ++i) {
        if (!results[i].used) {
            results[i].used = 1;
            results[i].pid = pid;
            results[i].result = *result;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Coordinates the inputm handles ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int inputm_handles_ioctl(uint64_t request)
{
    switch (request) {
    case LEONOS_INPUTM_IOCTL_REGISTER:
    case LEONOS_INPUTM_IOCTL_UNREGISTER:
    case LEONOS_INPUTM_IOCTL_PROVIDER_NEXT:
    case LEONOS_INPUTM_IOCTL_PROVIDER_RESULT:
    case LEONOS_INPUTM_IOCTL_SUBMIT_KEY:
    case LEONOS_INPUTM_IOCTL_POLL_RESULT:
    case LEONOS_INPUTM_IOCTL_SET_ACTIVE:
    case LEONOS_INPUTM_IOCTL_LIST:
    case LEONOS_INPUTM_IOCTL_CONTEXT:
    case LEONOS_INPUTM_IOCTL_GET_STATE:
    case LEONOS_INPUTM_IOCTL_NOTIFY_CONFIG:
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief Coordinates the inputm handle ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param user_arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t inputm_handle_ioctl(uint64_t request, uint64_t user_arg)
{
    struct task *task = sched_current_task();
    if (!task) {
        return -LEONOS_EACCES;
    }
    if (request == LEONOS_INPUTM_IOCTL_REGISTER) {
        const struct leonos_inputm_provider *input;
        struct inputm_provider_slot *slot;
        if (!task->uid || !user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_provider *)(uintptr_t)user_arg;
        if (!inputm_text_valid(input->id, LEONOS_INPUTM_ID_LEN) ||
            !inputm_text_valid(input->name, LEONOS_INPUTM_NAME_LEN) ||
            !inputm_text_valid(input->abbreviation, LEONOS_INPUTM_ABBREV_LEN) ||
            input->startup_mode > LEONOS_INPUTM_START_ON_DEMAND ||
            (input->render_flags & ~(LEONOS_INPUTM_RENDER_CONTROLS | LEONOS_INPUTM_RENDER_PIXELS))) {
            return -LEONOS_EINVAL;
        }
        slot = inputm_find_provider(task->pid);
        if (!slot) {
            for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
                if (!providers[i].used) {
                    slot = &providers[i];
                    *slot = (struct inputm_provider_slot){0};
                    slot->used = 1;
                    slot->pid = task->pid;
                    slot->uid = task->uid;
                    break;
                }
            }
        }
        if (!slot) {
            return -LEONOS_E2BIG;
        }
        for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
            if (providers[i].used && providers[i].pid != task->pid &&
                providers[i].uid == task->uid &&
                inputm_text_equal(providers[i].provider.id, input->id)) {
                return -LEONOS_EEXIST;
            }
        }
        slot->provider = *input;
        slot->provider.enabled = input->enabled ? 1U : 0U;
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_UNREGISTER) {
        struct inputm_provider_slot *slot = inputm_find_provider(task->pid);
        if (!slot) {
            return -LEONOS_ENOENT;
        }
        *slot = (struct inputm_provider_slot){0};
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_PROVIDER_NEXT) {
        struct inputm_provider_slot *slot = inputm_find_provider(task->pid);
        struct leonos_inputm_key_event *out;
        uint32_t issued = INPUTM_EVENT_CAP;
        if (!slot || !user_range_ok(user_arg, sizeof(*out))) {
            return -LEONOS_EFAULT;
        }
        out = (struct leonos_inputm_key_event *)(uintptr_t)user_arg;
        if (slot->event_head == slot->event_tail) {
            return 0;
        }
        for (uint32_t i = 0; i < INPUTM_EVENT_CAP; ++i) {
            if (!slot->issued_used[i]) {
                issued = i;
                break;
            }
        }
        if (issued == INPUTM_EVENT_CAP) {
            return -LEONOS_E2BIG;
        }
        *out = slot->events[slot->event_tail];
        slot->issued[issued] = *out;
        slot->issued_used[issued] = 1;
        slot->event_tail = (slot->event_tail + 1U) % INPUTM_EVENT_CAP;
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_PROVIDER_RESULT) {
        const struct leonos_inputm_result *input;
        struct inputm_provider_slot *slot = inputm_find_provider(task->pid);
        struct inputm_user_slot *user;
        struct inputm_context_slot *context;
        int issued;
        if (!slot || !user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_result *)(uintptr_t)user_arg;
        if (!input->client_pid || !input->window_id ||
            (input->type < LEONOS_INPUTM_RESULT_COMPOSITION ||
             input->type > LEONOS_INPUTM_RESULT_PASSTHROUGH) ||
            input->candidate_count > LEONOS_INPUTM_MAX_CANDIDATES ||
            !inputm_task_alive(input->client_pid) ||
            !inputm_utf8_valid(input->text, sizeof(input->text),
                               input->type == LEONOS_INPUTM_RESULT_COMMIT)) {
            return -LEONOS_EINVAL;
        }
        for (uint32_t i = 0; i < input->candidate_count; ++i) {
            if (!inputm_utf8_valid(input->candidates[i], sizeof(input->candidates[i]), 1)) {
                return -LEONOS_EINVAL;
            }
        }
        issued = inputm_find_issued_event(slot, input);
        if (issued < 0) {
            return -LEONOS_EACCES;
        }
        slot->issued_used[(uint32_t)issued] = 0;
        if (input->type == LEONOS_INPUTM_RESULT_PASSTHROUGH) {
            struct leonos_inputm_result forwarded = *input;
            forwarded.keycode = slot->issued[(uint32_t)issued].keycode;
            forwarded.pressed = slot->issued[(uint32_t)issued].pressed;
            return inputm_queue_result(input->client_pid, &forwarded) ?
                       1 : -LEONOS_E2BIG;
        }
        user = inputm_find_user(slot->uid, 1);
        if (!user) {
            return -LEONOS_E2BIG;
        }
        if (input->type == LEONOS_INPUTM_RESULT_COMPOSITION) {
            context = inputm_find_context(input->client_pid, input->window_id, 0);
            user->state.candidate_count = input->candidate_count;
            user->state.selected_candidate = input->selected_candidate;
            inputm_copy_text(user->state.composition, sizeof(user->state.composition), input->text);
            for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_CANDIDATES; ++i) {
                inputm_copy_text(user->state.candidates[i], sizeof(user->state.candidates[i]), input->candidates[i]);
            }
            user->state.render_flags = slot->provider.render_flags;
            user->state.window_id = input->window_id;
            if (context) {
                user->state.caret_x = context->context.caret_x;
                user->state.caret_y = context->context.caret_y;
                user->state.caret_w = context->context.caret_w;
                user->state.caret_h = context->context.caret_h;
            }
        } else if (input->type == LEONOS_INPUTM_RESULT_CANCEL) {
            user->state.composition[0] = 0;
            user->state.candidate_count = 0;
        }
        if (input->type == LEONOS_INPUTM_RESULT_COMMIT) {
            return inputm_queue_result(input->client_pid, input) ? 1 : -LEONOS_E2BIG;
        }
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_SUBMIT_KEY) {
        const struct leonos_inputm_key_event *input;
        struct inputm_user_slot *user;
        struct inputm_provider_slot *provider;
        struct inputm_context_slot *context;
        struct leonos_inputm_key_event event;
        if (!task->uid || !user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_key_event *)(uintptr_t)user_arg;
        if (!input->window_id) {
            return -LEONOS_EINVAL;
        }
        context = inputm_find_context(task->pid, input->window_id, 0);
        /* An application must explicitly opt an editable control into IME input. */
        if (!context || !(context->context.flags & LEONOS_INPUTM_CONTEXT_FOCUSED) ||
            (context->context.flags & LEONOS_INPUTM_CONTEXT_SECURE)) {
            return 0;
        }
        user = inputm_find_user(task->uid, 1);
        if (!user || inputm_text_equal(user->state.active_id, "en")) {
            return 0;
        }
        provider = inputm_find_provider_for_user(task->uid, user->state.active_id);
        if (!provider || !provider->provider.enabled) {
            inputm_copy_text(user->state.active_id, sizeof(user->state.active_id), "en");
            user->state.composition[0] = 0;
            user->state.candidate_count = 0;
            return 0;
        }
        event = *input;
        event.client_pid = task->pid;
        event.context_flags = context->context.flags;
        event.caret_x = context->context.caret_x;
        event.caret_y = context->context.caret_y;
        event.caret_w = context->context.caret_w;
        event.caret_h = context->context.caret_h;
        event.sequence = user->next_sequence++;
        if (!user->next_sequence) {
            user->next_sequence = 1;
        }
        inputm_queue_event(provider, &event);
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_POLL_RESULT) {
        struct leonos_inputm_result *out;
        if (!user_range_ok(user_arg, sizeof(*out))) {
            return -LEONOS_EFAULT;
        }
        out = (struct leonos_inputm_result *)(uintptr_t)user_arg;
        for (uint32_t i = 0; i < INPUTM_RESULT_CAP; ++i) {
            if (results[i].used && results[i].pid == task->pid) {
                *out = results[i].result;
                results[i] = (struct inputm_result_slot){0};
                return 1;
            }
        }
        return 0;
    }
    if (request == LEONOS_INPUTM_IOCTL_SET_ACTIVE) {
        const struct leonos_inputm_active_request *input;
        struct inputm_user_slot *user;
        if (!user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_active_request *)(uintptr_t)user_arg;
        if (!input->uid || (task->uid != input->uid &&
                            !(task->flags & TASK_FLAG_WINDOW_SERVER) &&
                            !(task->flags & TASK_FLAG_ELEVATED_ADMIN)) ||
            !inputm_text_valid(input->id, LEONOS_INPUTM_ID_LEN)) {
            return -LEONOS_EACCES;
        }
        user = inputm_find_user(input->uid, 1);
        if (!user) {
            return -LEONOS_E2BIG;
        }
        if (!inputm_text_equal(input->id, "en") &&
            !inputm_find_provider_for_user(input->uid, input->id)) {
            return -LEONOS_ENOENT;
        }
        inputm_copy_text(user->state.active_id, sizeof(user->state.active_id), input->id);
        user->state.composition[0] = 0;
        user->state.candidate_count = 0;
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_LIST) {
        struct leonos_inputm_provider_list *list;
        uint32_t count = 1;
        if (!user_range_ok(user_arg, sizeof(*list))) {
            return -LEONOS_EFAULT;
        }
        list = (struct leonos_inputm_provider_list *)(uintptr_t)user_arg;
        if (!list->uid || (task->uid != list->uid &&
                           !(task->flags & TASK_FLAG_WINDOW_SERVER) &&
                           !(task->flags & TASK_FLAG_ELEVATED_ADMIN)) ||
            list->capacity > LEONOS_INPUTM_MAX_PROVIDERS + 1U ||
            (list->capacity && (!list->providers ||
             !user_range_ok((uint64_t)(uintptr_t)list->providers,
                            (uint64_t)list->capacity * sizeof(*list->providers))))) {
            return -LEONOS_EACCES;
        }
        if (list->providers && list->capacity) {
            list->providers[0] = (struct leonos_inputm_provider){0};
            inputm_copy_text(list->providers[0].id, sizeof(list->providers[0].id), "en");
            inputm_copy_text(list->providers[0].name, sizeof(list->providers[0].name), "English");
            inputm_copy_text(list->providers[0].abbreviation, sizeof(list->providers[0].abbreviation), "EN");
            list->providers[0].enabled = 1;
        }
        for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
            if (!providers[i].used || providers[i].uid != list->uid ||
                !inputm_task_alive(providers[i].pid)) {
                continue;
            }
            if (list->providers && count < list->capacity) {
                list->providers[count] = providers[i].provider;
            }
            ++count;
        }
        list->count = count;
        return (int64_t)count;
    }
    if (request == LEONOS_INPUTM_IOCTL_CONTEXT) {
        const struct leonos_inputm_context *input;
        struct inputm_context_slot *slot;
        if (!user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_context *)(uintptr_t)user_arg;
        if (!input->window_id || (input->flags &
            ~(LEONOS_INPUTM_CONTEXT_FOCUSED | LEONOS_INPUTM_CONTEXT_SECURE))) {
            return -LEONOS_EINVAL;
        }
        slot = inputm_find_context(task->pid, input->window_id, 1);
        if (!slot) {
            return -LEONOS_E2BIG;
        }
        slot->context = *input;
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_GET_STATE) {
        struct leonos_inputm_state *out;
        struct inputm_user_slot *user;
        if (!user_range_ok(user_arg, sizeof(*out))) {
            return -LEONOS_EFAULT;
        }
        out = (struct leonos_inputm_state *)(uintptr_t)user_arg;
        if (!out->uid || (task->uid != out->uid &&
                          !(task->flags & TASK_FLAG_WINDOW_SERVER) &&
                          !(task->flags & TASK_FLAG_ELEVATED_ADMIN))) {
            return -LEONOS_EACCES;
        }
        user = inputm_find_user(out->uid, 1);
        if (!user) {
            return -LEONOS_E2BIG;
        }
        *out = user->state;
        return 1;
    }
    if (request == LEONOS_INPUTM_IOCTL_NOTIFY_CONFIG) {
        const struct leonos_inputm_config_request *input;
        struct inputm_user_slot *user;
        if (!user_range_ok(user_arg, sizeof(*input))) {
            return -LEONOS_EFAULT;
        }
        input = (const struct leonos_inputm_config_request *)(uintptr_t)user_arg;
        if (!input->uid || (task->uid != input->uid &&
                            !(task->flags & TASK_FLAG_WINDOW_SERVER) &&
                            !(task->flags & TASK_FLAG_ELEVATED_ADMIN))) {
            return -LEONOS_EACCES;
        }
        user = inputm_find_user(input->uid, 1);
        if (!user) {
            return -LEONOS_E2BIG;
        }
        ++user->state.config_generation;
        if (!user->state.config_generation) {
            user->state.config_generation = 1;
        }
        return 1;
    }
    return -LEONOS_EINVAL;
}

/**
 * @brief Coordinates the inputm destroy owner operation.
 * @param pid Input or output value used by this operation.
 */
void inputm_destroy_owner(uint32_t pid)
{
    for (uint32_t i = 0; i < LEONOS_INPUTM_MAX_PROVIDERS; ++i) {
        if (providers[i].used && providers[i].pid == pid) {
            for (uint32_t j = 0; j < INPUTM_UID_CAP; ++j) {
                if (users[j].used && users[j].uid == providers[i].uid &&
                    inputm_text_equal(users[j].state.active_id, providers[i].provider.id)) {
                    inputm_copy_text(users[j].state.active_id,
                                     sizeof(users[j].state.active_id), "en");
                    users[j].state.composition[0] = 0;
                    users[j].state.candidate_count = 0;
                }
            }
            providers[i] = (struct inputm_provider_slot){0};
        }
    }
    for (uint32_t i = 0; i < INPUTM_CONTEXT_CAP; ++i) {
        if (contexts[i].used && contexts[i].pid == pid) {
            contexts[i] = (struct inputm_context_slot){0};
        }
    }
    for (uint32_t i = 0; i < INPUTM_RESULT_CAP; ++i) {
        if (results[i].used && results[i].pid == pid) {
            results[i] = (struct inputm_result_slot){0};
        }
    }
}
