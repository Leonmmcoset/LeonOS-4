#include <assert.h>
#include <sys/un.h>

#define main windowd_program_main
#include "../../userland/apps/windowd/main.c"
#undef main
#include "../../userland/libc/src/wind.c"

static uint32_t test_now;
static int request_pending;
static struct leonos_win_fetch pending_fetch;
static uint32_t reply_type;
static uint32_t reply_length;
static uint8_t reply_buffer[WIND_FRAME_CAP];
static int reply_pending;
static int reply_descriptor = -1;

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

int leonos_ipc_send_fd(int fd, uint32_t type, const void *payload,
                       uint32_t length, int descriptor)
{
    if (fd == 4) {
        assert(type == LEONOS_WIN_MSG_FETCH && length == sizeof(pending_fetch));
        memcpy(&pending_fetch, payload, length);
        request_pending = 1;
        handle_client(0);
    } else {
        assert(fd == 5 && !reply_pending && length <= sizeof(reply_buffer));
        reply_type = type;
        reply_length = length;
        memcpy(reply_buffer, payload, length);
        reply_pending = 1;
        reply_descriptor = descriptor;
    }
    return 0;
}

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    return leonos_ipc_send_fd(fd, type, payload, length, -1);
}

int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd)
{
    if (received_fd) *received_fd = -1;
    if (fd == 5) {
        assert(request_pending && capacity >= sizeof(pending_fetch));
        *type = LEONOS_WIN_MSG_FETCH;
        *length = sizeof(pending_fetch);
        memcpy(payload, &pending_fetch, sizeof(pending_fetch));
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
    if (received_fd) *received_fd = reply_descriptor;
    return 0;
}

int leonos_ipc_connect(const char *path) { (void)path; assert(0); return -1; }
int leonos_ipc_set_nonblock(int fd, int enabled) { (void)fd; (void)enabled; return 0; }
int leonos_ipc_close(int fd) { (void)fd; return 0; }

int main(void)
{
    uint32_t pixels[4] = {0};
    wind_policy_fd = 4;
    clients[0] = (struct windowd_client){.used = 1, .fd = 5, .pid = 10,
                                        .uid = 0, .role = LEONOS_WIN_ROLE_POLICY};
    assert(leonos_gui_fetch_window(1, 2, 2, 2, pixels, 0, 0) < 0);
    assert(errno == ENOENT && test_now < 50);
    /* A failed fetch must leave the connection usable for the next request. */
    windows[0] = (struct windowd_window){.used = 1, .id = 2, .width = 2,
                                        .height = 2, .stride = 8, .shm_fd = 42};
    struct leonos_win_fetch request = {.window_id = 2};
    struct leonos_win_fetch_ack ack = {0};
    int descriptor = -1;
    assert(leonos_ipc_send(4, LEONOS_WIN_MSG_FETCH, &request, sizeof(request)) == 0);
    assert(wind_wait_type(4, LEONOS_WIN_MSG_FETCH_ACK, &ack, sizeof(ack),
                          0, &descriptor) == 0);
    assert(ack.window_id == 2 && descriptor == 42 && test_now < 50);
    puts("OOBE window teardown: missing surface returns promptly, next reply stays intact");
    return 0;
}
