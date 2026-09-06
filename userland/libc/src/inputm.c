/* Text-input service client. The public text_input_* / leonos_inputm_* entry
 * points are unchanged; transport moved from /dev/input-method ioctls to the
 * imd daemon over /run/leonos/input-method.sock. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <leonos/inputm.h>
#include <leonos/inputmd.h>
#include <leonos/text_input.h>
#include <leonos/unix_ipc.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INPUTM_PENDING_COMMIT_CAP 8U
#define INPUTM_RESULT_QUEUE 32U
#define INPUTM_KEY_QUEUE 32U
#define INPUTM_FRAME_CAP 4096U
#define INPUTM_CONNECT_RETRY_MS 5000u

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
static int imd_fd = -1;
static struct leonos_inputm_result inputm_results[INPUTM_RESULT_QUEUE];
static uint32_t inputm_result_head;
static uint32_t inputm_result_tail;
static struct leonos_inputm_key_event inputm_keys[INPUTM_KEY_QUEUE];
static uint32_t inputm_key_head;
static uint32_t inputm_key_tail;
static uint32_t imd_role = LEONOS_IMD_ROLE_APP;

static uint32_t inputm_now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

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

static int inputm_wait_frame(uint32_t expected, void *payload, uint32_t capacity,
                             uint32_t *length)
{
    uint32_t deadline = inputm_now_ms() + 3000u;
    for (;;) {
        uint8_t buffer[INPUTM_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (leonos_ipc_recv(imd_fd, &type, buffer, sizeof(buffer), &got) == 0) {
            if (type == expected) {
                if (got > capacity) got = capacity;
                if (got) memcpy(payload, buffer, got);
                if (length) *length = got;
                return 0;
            }
            if (type == LEONOS_IMD_MSG_RESULT && got >= sizeof(struct leonos_inputm_result)) {
                struct leonos_inputm_result result;
                memcpy(&result, buffer, sizeof(result));
                inputm_results[inputm_result_head] = result;
                inputm_result_head = (inputm_result_head + 1u) % INPUTM_RESULT_QUEUE;
            } else if (type == LEONOS_IMD_MSG_KEY_EVENT && got >= sizeof(struct leonos_inputm_key_event)) {
                struct leonos_inputm_key_event event;
                memcpy(&event, buffer, sizeof(event));
                inputm_keys[inputm_key_head] = event;
                inputm_key_head = (inputm_key_head + 1u) % INPUTM_KEY_QUEUE;
            }
        }
        if (inputm_now_ms() >= deadline) return -1;
        (void)poll(0, 0, 2);
    }
}

static int inputm_pump(void)
{
    int progress = 0;
    if (imd_fd < 0) return 0;
    for (;;) {
        struct pollfd descriptor = {.fd = imd_fd, .events = POLLIN, .revents = 0};
        uint8_t buffer[INPUTM_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (poll(&descriptor, 1, 0) <= 0) break;
        if (leonos_ipc_recv(imd_fd, &type, buffer, sizeof(buffer), &got) < 0) break;
        progress = 1;
        if (type == LEONOS_IMD_MSG_RESULT && got >= sizeof(struct leonos_inputm_result)) {
            struct leonos_inputm_result result;
            memcpy(&result, buffer, sizeof(result));
            inputm_results[inputm_result_head] = result;
            inputm_result_head = (inputm_result_head + 1u) % INPUTM_RESULT_QUEUE;
        } else if (type == LEONOS_IMD_MSG_KEY_EVENT && got >= sizeof(struct leonos_inputm_key_event)) {
            struct leonos_inputm_key_event event;
            memcpy(&event, buffer, sizeof(event));
            inputm_keys[inputm_key_head] = event;
            inputm_key_head = (inputm_key_head + 1u) % INPUTM_KEY_QUEUE;
        }
    }
    return progress;
}

static int inputm_open_connection(void)
{
    uint32_t deadline = inputm_now_ms() + INPUTM_CONNECT_RETRY_MS;
    struct leonos_imd_hello hello;
    struct leonos_imd_ack ack;
    if (imd_fd >= 0) return imd_fd;
    while (imd_fd < 0 && inputm_now_ms() < deadline) {
        imd_fd = leonos_ipc_connect(LEONOS_IPC_SOCK_INPUT_METHOD);
        if (imd_fd < 0) (void)poll(0, 0, 10);
    }
    if (imd_fd < 0) return -1;
    (void)leonos_ipc_set_nonblock(imd_fd, 1);
    hello.pid = (uint32_t)getpid();
    hello.role = imd_role;
    if (leonos_ipc_send(imd_fd, LEONOS_IMD_MSG_HELLO, &hello, sizeof(hello)) < 0 ||
        inputm_wait_frame(LEONOS_IMD_MSG_ACK, &ack, sizeof(ack), 0) < 0 ||
        ack.code < 0) {
        leonos_ipc_close(imd_fd);
        imd_fd = -1;
        return -1;
    }
    return imd_fd;
}

static int inputm_request_ack(uint32_t type, const void *payload, uint32_t length,
                              int32_t *code)
{
    struct leonos_imd_ack ack;
    if (inputm_open_connection() < 0) return -1;
    if (leonos_ipc_send(imd_fd, type, payload, length) < 0) return -1;
    if (inputm_wait_frame(LEONOS_IMD_MSG_ACK, &ack, sizeof(ack), 0) < 0) return -1;
    if (code) *code = ack.code;
    return 0;
}

int text_input_register(const struct leonos_inputm_provider *provider)
{
    int32_t code = -1;
    if (!provider) return -1;
    imd_role = LEONOS_IMD_ROLE_PROVIDER;
    if (imd_fd >= 0) leonos_ipc_close(imd_fd), imd_fd = -1;
    if (inputm_request_ack(LEONOS_IMD_MSG_REGISTER, provider, sizeof(*provider), &code) < 0) return -1;
    return code > 0 ? 1 : code;
}

int text_input_unregister(void)
{
    int32_t code = -1;
    if (inputm_request_ack(LEONOS_IMD_MSG_UNREGISTER, 0, 0, &code) < 0) return -1;
    return code;
}

int text_input_provider_next(struct leonos_inputm_key_event *event)
{
    uint32_t deadline;
    if (!event) return -1;
    if (inputm_open_connection() < 0) return -1;
    deadline = inputm_now_ms() + 50u;
    for (;;) {
        (void)inputm_pump();
        if (inputm_key_head != inputm_key_tail) {
            *event = inputm_keys[inputm_key_tail];
            inputm_key_tail = (inputm_key_tail + 1u) % INPUTM_KEY_QUEUE;
            return 1;
        }
        if (inputm_now_ms() >= deadline) return 0;
        (void)poll(0, 0, 2);
    }
}

int text_input_provider_result(const struct leonos_inputm_result *result)
{
    int32_t code = -1;
    if (!result) return -1;
    if (inputm_request_ack(LEONOS_IMD_MSG_RESULT, result, sizeof(*result), &code) < 0) return -1;
    return code;
}

int text_input_submit_key(uint32_t window_id, uint8_t keycode, uint8_t pressed)
{
    struct leonos_inputm_key_event event = {0};
    int32_t code = -1;
    if (!window_id) return -1;
    event.window_id = window_id;
    event.keycode = keycode;
    event.pressed = pressed ? 1u : 0u;
    if (inputm_request_ack(LEONOS_IMD_MSG_SUBMIT_KEY, &event, sizeof(event), &code) < 0) return -1;
    return code;
}

int text_input_poll_result(struct leonos_inputm_result *result)
{
    if (!result) return -1;
    if (inputm_open_connection() < 0) return 0;
    (void)inputm_pump();
    if (inputm_result_head == inputm_result_tail) return 0;
    *result = inputm_results[inputm_result_tail];
    inputm_result_tail = (inputm_result_tail + 1u) % INPUTM_RESULT_QUEUE;
    return 1;
}

int text_input_set_active(uint32_t uid, const char *id)
{
    struct leonos_inputm_active_request request = {0};
    int32_t code = -1;
    if (!uid || !inputm_copy_id(request.id, sizeof(request.id), id)) return -1;
    request.uid = uid;
    if (inputm_request_ack(LEONOS_IMD_MSG_SET_ACTIVE, &request, sizeof(request), &code) < 0) return -1;
    return code;
}

int text_input_list(uint32_t uid, struct leonos_inputm_provider *providers,
                    uint32_t capacity, uint32_t *out_count)
{
    struct leonos_imd_list request = {.uid = uid, .capacity = capacity};
    struct leonos_imd_list_ack ack;
    uint32_t length = 0;
    uint8_t buffer[INPUTM_FRAME_CAP];
    if (out_count) *out_count = 0;
    if (!uid) return -1;
    if (inputm_open_connection() < 0) return -1;
    if (leonos_ipc_send(imd_fd, LEONOS_IMD_MSG_LIST, &request, sizeof(request)) < 0) return -1;
    if (inputm_wait_frame(LEONOS_IMD_MSG_LIST_ACK, buffer, sizeof(buffer), &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (providers && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        uint32_t payload = length - sizeof(ack);
        if (payload < count * sizeof(*providers)) return -1;
        memcpy(providers, buffer + sizeof(ack), count * sizeof(*providers));
    }
    return (int)ack.count;
}

int text_input_set_context(const struct leonos_inputm_context *context)
{
    int32_t code = -1;
    if (!context) return -1;
    if (inputm_request_ack(LEONOS_IMD_MSG_SET_CONTEXT, context, sizeof(*context), &code) < 0) return -1;
    return code;
}

void text_input_note_gui_window(uint32_t window_id)
{
    if (window_id) inputm_current_window_id = window_id;
}

int text_input_set_current_context(uint32_t flags, int32_t caret_x,
                                   int32_t caret_y, uint32_t caret_w,
                                   uint32_t caret_h)
{
    struct leonos_inputm_context context = {0};
    if (!inputm_current_window_id) return 0;
    context.window_id = inputm_current_window_id;
    context.flags = flags & (LEONOS_INPUTM_CONTEXT_FOCUSED |
                             LEONOS_INPUTM_CONTEXT_SECURE);
    context.caret_x = caret_x;
    context.caret_y = caret_y;
    context.caret_w = caret_w;
    context.caret_h = caret_h;
    return text_input_set_context(&context);
}

int text_input_get_state(uint32_t uid, struct leonos_inputm_state *state)
{
    struct leonos_imd_get_state request = {.uid = uid};
    if (!uid || !state) return -1;
    if (inputm_open_connection() < 0) return -1;
    if (leonos_ipc_send(imd_fd, LEONOS_IMD_MSG_GET_STATE, &request, sizeof(request)) < 0) return -1;
    if (inputm_wait_frame(LEONOS_IMD_MSG_STATE_ACK, state, sizeof(*state), 0) < 0) return -1;
    return 1;
}

int text_input_notify_config(uint32_t uid)
{
    struct leonos_inputm_config_request request = {.uid = uid};
    int32_t code = -1;
    if (!uid) return -1;
    if (inputm_request_ack(LEONOS_IMD_MSG_NOTIFY_CONFIG, &request, sizeof(request), &code) < 0) return -1;
    return code;
}

int text_input_observe_gui_key(uint32_t window_id, uint8_t *keycode, uint8_t pressed)
{
    int ret;
    if (!window_id || !keycode || *keycode == 0) return 0;
    ret = text_input_submit_key(window_id, *keycode, pressed);
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
    while (text_input_poll_result(&result) > 0) {
        if ((result.type == LEONOS_INPUTM_RESULT_COMMIT && result.text[0]) ||
            result.type == LEONOS_INPUTM_RESULT_PASSTHROUGH) {
            inputm_queue_commit(&result);
        }
        result = (struct leonos_inputm_result){0};
    }
}

int text_input_poll_gui_commit(uint32_t window_id)
{
    if (!window_id) return 0;
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

int text_input_take_text(char *buffer, uint32_t capacity)
{
    if (!buffer || capacity == 0 || !inputm_taken_text[0]) return 0;
    inputm_copy_text(buffer, capacity, inputm_taken_text);
    inputm_taken_text[0] = 0;
    return 1;
}

int text_input_take_key(uint8_t *keycode, uint8_t *pressed)
{
    if (!inputm_taken_keycode) return 0;
    if (keycode) *keycode = inputm_taken_keycode;
    if (pressed) *pressed = inputm_taken_key_pressed;
    inputm_taken_keycode = 0;
    inputm_taken_key_pressed = 0;
    return 1;
}

int leonos_inputm_register(const struct leonos_inputm_provider *provider) { return text_input_register(provider); }
int leonos_inputm_unregister(void) { return text_input_unregister(); }
int leonos_inputm_provider_next(struct leonos_inputm_key_event *event) { return text_input_provider_next(event); }
int leonos_inputm_provider_result(const struct leonos_inputm_result *result) { return text_input_provider_result(result); }
int leonos_inputm_submit_key(uint32_t window_id, uint8_t keycode, uint8_t pressed) { return text_input_submit_key(window_id, keycode, pressed); }
int leonos_inputm_poll_result(struct leonos_inputm_result *result) { return text_input_poll_result(result); }
int leonos_inputm_set_active(uint32_t uid, const char *id) { return text_input_set_active(uid, id); }
int leonos_inputm_list(uint32_t uid, struct leonos_inputm_provider *providers, uint32_t capacity, uint32_t *out_count) { return text_input_list(uid, providers, capacity, out_count); }
int leonos_inputm_set_context(const struct leonos_inputm_context *context) { return text_input_set_context(context); }
void leonos_inputm_note_gui_window(uint32_t window_id) { text_input_note_gui_window(window_id); }
int leonos_inputm_set_current_context(uint32_t flags, int32_t caret_x, int32_t caret_y, uint32_t caret_w, uint32_t caret_h) { return text_input_set_current_context(flags, caret_x, caret_y, caret_w, caret_h); }
int leonos_inputm_get_state(uint32_t uid, struct leonos_inputm_state *state) { return text_input_get_state(uid, state); }
int leonos_inputm_notify_config(uint32_t uid) { return text_input_notify_config(uid); }
int leonos_inputm_observe_gui_key(uint32_t window_id, uint8_t *keycode, uint8_t pressed) { return text_input_observe_gui_key(window_id, keycode, pressed); }
int leonos_inputm_poll_gui_commit(uint32_t window_id) { return text_input_poll_gui_commit(window_id); }
int leonos_inputm_take_text(char *buffer, uint32_t capacity) { return text_input_take_text(buffer, capacity); }
int leonos_inputm_take_key(uint8_t *keycode, uint8_t *pressed) { return text_input_take_key(keycode, pressed); }
