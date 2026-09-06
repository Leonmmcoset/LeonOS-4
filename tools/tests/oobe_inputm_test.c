#include <assert.h>
#include <sys/un.h>

#define main imd_program_main
#include "../../userland/apps/imd/main.c"
#undef main
#include "../../userland/libc/src/inputm.c"

static uint32_t test_now;
static uint32_t reply_type;
static uint32_t reply_length;
static uint8_t reply_buffer[INPUTM_FRAME_CAP];
static uint32_t request_type;
static uint32_t request_length;
static uint8_t request_buffer[INPUTM_FRAME_CAP];
static int request_pending;
static int reply_pending;

int clock_gettime(clockid_t clock, struct timespec *ts)
{
    (void)clock;
    ts->tv_sec = test_now / 1000u;
    ts->tv_nsec = (test_now % 1000u) * 1000000u;
    return 0;
}

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    if (!count) {
        if (timeout > 0) test_now += (uint32_t)timeout;
        return 0;
    }
    assert(count == 1 && fds[0].fd == 5);
    fds[0].revents = request_pending ? POLLIN : 0;
    return request_pending;
}

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    if (fd == 4) {
        assert(!request_pending && length <= sizeof(request_buffer));
        request_type = type;
        request_length = length;
        if (length) memcpy(request_buffer, payload, length);
        request_pending = 1;
        imd_handle_client(0);
    } else {
        assert(fd == 5 && !reply_pending && length <= sizeof(reply_buffer));
        reply_type = type;
        reply_length = length;
        memcpy(reply_buffer, payload, length);
        reply_pending = 1;
    }
    return 0;
}

int leonos_ipc_recv(int fd, uint32_t *type, void *payload, uint32_t capacity,
                    uint32_t *length)
{
    if (fd == 5) {
        assert(request_pending && capacity >= request_length);
        *type = request_type;
        *length = request_length;
        memcpy(payload, request_buffer, request_length);
        request_pending = 0;
        return 0;
    }
    assert(fd == 4);
    if (!reply_pending) { errno = EAGAIN; return -1; }
    assert(capacity >= reply_length);
    *type = reply_type;
    *length = reply_length;
    memcpy(payload, reply_buffer, reply_length);
    reply_pending = 0;
    return 0;
}

int leonos_ipc_connect(const char *path) { (void)path; assert(0); return -1; }
int leonos_ipc_set_nonblock(int fd, int enabled) { (void)fd; (void)enabled; return 0; }
int leonos_ipc_close(int fd) { (void)fd; return 0; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    clients[0] = (struct imd_client){.used = 1, .fd = 5, .pid = 10,
                                    .uid = 0, .role = LEONOS_IMD_ROLE_APP};
    imd_fd = 4;
    if (!strcmp(argv[1], "list")) {
        struct leonos_inputm_provider entries[2];
        uint32_t count = 0;
        assert(text_input_list(1, entries, 2, &count) == 1);
        assert(count == 1 && !strcmp(entries[0].id, "en"));
    } else if (!strcmp(argv[1], "state")) {
        struct leonos_inputm_state state = {0};
        assert(text_input_get_state(1, &state) == 1);
        assert(state.uid == 1 && !strcmp(state.active_id, "en"));
    } else if (!strcmp(argv[1], "active")) {
        assert(text_input_set_active(1, "en") == 1);
        assert(!strcmp(imd_find_user(1)->state.active_id, "en"));
    } else if (!strcmp(argv[1], "context")) {
        struct leonos_inputm_context context = {.window_id = 3,
            .flags = LEONOS_INPUTM_CONTEXT_FOCUSED | LEONOS_INPUTM_CONTEXT_SECURE};
        assert(text_input_set_context(&context) == 1);
        struct imd_context *stored = imd_find_context(10, 3, 0);
        assert(stored && stored->context.flags == context.flags);
    } else if (!strcmp(argv[1], "notify")) {
        assert(text_input_notify_config(1) == 1);
        assert(imd_find_user(1)->state.config_generation == 1);
    } else if (!strcmp(argv[1], "denied")) {
        clients[0].uid = 2;
        struct leonos_inputm_state state = {0};
        assert(text_input_get_state(1, &state) < 0);
        assert(test_now < 50); /* A rejection is a reply, not a timeout. */
        assert(text_input_set_active(1, "en") < 0);
        assert(text_input_notify_config(1) < 0);
        assert(test_now < 50);
    } else {
        assert(0);
    }
    assert(test_now < 50);
    printf("OOBE input method scenario passed: %s\n", argv[1]);
    return 0;
}
