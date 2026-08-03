#include <leonos/inputm.h>
#include <leonos/syscall.h>

#define INPUTM_PENDING_COMMIT_CAP 8U

struct inputm_pending_commit {
    uint8_t used;
    uint8_t keycode;
    uint8_t pressed;
    uint32_t window_id;
    char text[LEONOS_INPUTM_TEXT_LEN];
};

static struct inputm_pending_commit inputm_pending_commits[INPUTM_PENDING_COMMIT_CAP];
static char inputm_taken_text[LEONOS_INPUTM_TEXT_LEN];
static uint8_t inputm_taken_keycode;
static uint8_t inputm_taken_key_pressed;
static uint32_t inputm_current_window_id;

static void inputm_copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int inputm_copy_id(char *dst, uint32_t capacity, const char *id)
{
    uint32_t i = 0;
    if (!dst || capacity == 0 || !id || !id[0]) {
        return 0;
    }
    while (id[i] && i + 1U < capacity) {
        dst[i] = id[i];
        ++i;
    }
    if (id[i]) {
        return 0;
    }
    dst[i] = 0;
    return 1;
}

int leonos_inputm_register(const struct leonos_inputm_provider *provider)
{
    return provider ? ioctl(3, LEONOS_INPUTM_IOCTL_REGISTER, (void *)provider) : -1;
}

int leonos_inputm_unregister(void)
{
    return ioctl(3, LEONOS_INPUTM_IOCTL_UNREGISTER, 0);
}

int leonos_inputm_provider_next(struct leonos_inputm_key_event *event)
{
    return event ? ioctl(3, LEONOS_INPUTM_IOCTL_PROVIDER_NEXT, event) : -1;
}

int leonos_inputm_provider_result(const struct leonos_inputm_result *result)
{
    return result ? ioctl(3, LEONOS_INPUTM_IOCTL_PROVIDER_RESULT, (void *)result) : -1;
}

int leonos_inputm_submit_key(uint32_t window_id, uint8_t keycode, uint8_t pressed)
{
    struct leonos_inputm_key_event event = {0};
    if (!window_id) {
        return -1;
    }
    event.window_id = window_id;
    event.keycode = keycode;
    event.pressed = pressed ? 1U : 0U;
    return ioctl(3, LEONOS_INPUTM_IOCTL_SUBMIT_KEY, &event);
}

int leonos_inputm_poll_result(struct leonos_inputm_result *result)
{
    return result ? ioctl(3, LEONOS_INPUTM_IOCTL_POLL_RESULT, result) : -1;
}

int leonos_inputm_set_active(uint32_t uid, const char *id)
{
    struct leonos_inputm_active_request request = {0};
    if (!uid || !inputm_copy_id(request.id, sizeof(request.id), id)) {
        return -1;
    }
    request.uid = uid;
    return ioctl(3, LEONOS_INPUTM_IOCTL_SET_ACTIVE, &request);
}

int leonos_inputm_list(uint32_t uid, struct leonos_inputm_provider *providers,
                       uint32_t capacity, uint32_t *out_count)
{
    struct leonos_inputm_provider_list list = {uid, capacity, 0, 0, providers};
    int ret = ioctl(3, LEONOS_INPUTM_IOCTL_LIST, &list);
    if (out_count) {
        *out_count = list.count;
    }
    return ret;
}

int leonos_inputm_set_context(const struct leonos_inputm_context *context)
{
    return context ? ioctl(3, LEONOS_INPUTM_IOCTL_CONTEXT, (void *)context) : -1;
}

void leonos_inputm_note_gui_window(uint32_t window_id)
{
    if (window_id) {
        inputm_current_window_id = window_id;
    }
}

int leonos_inputm_set_current_context(uint32_t flags, int32_t caret_x,
                                      int32_t caret_y, uint32_t caret_w,
                                      uint32_t caret_h)
{
    struct leonos_inputm_context context = {0};
    if (!inputm_current_window_id) {
        return 0;
    }
    context.window_id = inputm_current_window_id;
    context.flags = flags & (LEONOS_INPUTM_CONTEXT_FOCUSED |
                             LEONOS_INPUTM_CONTEXT_SECURE);
    context.caret_x = caret_x;
    context.caret_y = caret_y;
    context.caret_w = caret_w;
    context.caret_h = caret_h;
    return leonos_inputm_set_context(&context);
}

int leonos_inputm_get_state(uint32_t uid, struct leonos_inputm_state *state)
{
    if (!uid || !state) {
        return -1;
    }
    state->uid = uid;
    return ioctl(3, LEONOS_INPUTM_IOCTL_GET_STATE, state);
}

int leonos_inputm_notify_config(uint32_t uid)
{
    struct leonos_inputm_config_request request = {uid};
    return uid ? ioctl(3, LEONOS_INPUTM_IOCTL_NOTIFY_CONFIG, &request) : -1;
}

int leonos_inputm_observe_gui_key(uint32_t window_id, uint8_t *keycode, uint8_t pressed)
{
    int ret;
    if (!window_id || !keycode || *keycode == 0) {
        return 0;
    }
    ret = leonos_inputm_submit_key(window_id, *keycode, pressed);
    if (ret > 0) {
        *keycode = 0;
        return 1;
    }
    return ret;
}

static void inputm_queue_commit(const struct leonos_inputm_result *result)
{
    for (uint32_t i = 0; i < INPUTM_PENDING_COMMIT_CAP; ++i) {
        if (!inputm_pending_commits[i].used) {
            inputm_pending_commits[i].used = 1;
            inputm_pending_commits[i].window_id = result->window_id;
            inputm_pending_commits[i].keycode = result->keycode;
            inputm_pending_commits[i].pressed = result->pressed;
            inputm_copy_text(inputm_pending_commits[i].text,
                             sizeof(inputm_pending_commits[i].text), result->text);
            return;
        }
    }
    inputm_pending_commits[0].window_id = result->window_id;
    inputm_pending_commits[0].keycode = result->keycode;
    inputm_pending_commits[0].pressed = result->pressed;
    inputm_copy_text(inputm_pending_commits[0].text,
                     sizeof(inputm_pending_commits[0].text), result->text);
    inputm_pending_commits[0].used = 1;
}

static void inputm_drain_commits(void)
{
    struct leonos_inputm_result result = {0};
    while (leonos_inputm_poll_result(&result) > 0) {
        if ((result.type == LEONOS_INPUTM_RESULT_COMMIT && result.text[0]) ||
            result.type == LEONOS_INPUTM_RESULT_PASSTHROUGH) {
            inputm_queue_commit(&result);
        }
        result = (struct leonos_inputm_result){0};
    }
}

int leonos_inputm_poll_gui_commit(uint32_t window_id)
{
    if (!window_id) {
        return 0;
    }
    inputm_drain_commits();
    for (uint32_t i = 0; i < INPUTM_PENDING_COMMIT_CAP; ++i) {
        if (inputm_pending_commits[i].used &&
            inputm_pending_commits[i].window_id == window_id) {
            inputm_copy_text(inputm_taken_text, sizeof(inputm_taken_text),
                             inputm_pending_commits[i].text);
            inputm_taken_keycode = inputm_pending_commits[i].keycode;
            inputm_taken_key_pressed = inputm_pending_commits[i].pressed;
            inputm_pending_commits[i] = (struct inputm_pending_commit){0};
            return inputm_taken_text[0] || inputm_taken_keycode ? 1 : 0;
        }
    }
    return 0;
}

int leonos_inputm_take_text(char *buffer, uint32_t capacity)
{
    if (!buffer || capacity == 0 || !inputm_taken_text[0]) {
        return 0;
    }
    inputm_copy_text(buffer, capacity, inputm_taken_text);
    inputm_taken_text[0] = 0;
    return 1;
}

int leonos_inputm_take_key(uint8_t *keycode, uint8_t *pressed)
{
    if (!inputm_taken_keycode) {
        return 0;
    }
    if (keycode) {
        *keycode = inputm_taken_keycode;
    }
    if (pressed) {
        *pressed = inputm_taken_key_pressed;
    }
    inputm_taken_keycode = 0;
    inputm_taken_key_pressed = 0;
    return 1;
}
