/* sessiond client: startup requests and launch policy over
 * /run/leonos/session.sock. Exports the old leonos_startup_* API. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <leonos/sessiond.h>
#include <leonos/startup.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SESSION_FRAME_CAP 4096u
#define SESSION_RETRY_MS 5000u

static int session_fd = -1;

static uint32_t session_now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static int session_wait(uint32_t expected, void *payload, uint32_t capacity,
                        uint32_t *length)
{
    uint32_t deadline = session_now_ms() + 3000u;
    for (;;) {
        uint8_t buffer[SESSION_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (leonos_ipc_recv(session_fd, &type, buffer, sizeof(buffer), &got) == 0) {
            if (type == expected) {
                if (got > capacity) got = capacity;
                if (got) memcpy(payload, buffer, got);
                if (length) *length = got;
                return 0;
            }
            if (type == LEONOS_SESSIOND_MSG_ACK && got >= sizeof(struct leonos_sessiond_ack)) {
                struct leonos_sessiond_ack ack;
                memcpy(&ack, buffer, sizeof(ack));
                if (expected == LEONOS_SESSIOND_MSG_ACK) {
                    if (length) *length = got;
                    return 0;
                }
                errno = ack.code < 0 ? EACCES : 0;
                return -1;
            }
        }
        if (session_now_ms() >= deadline) return -1;
        (void)poll(0, 0, 2);
    }
}

static int session_open(void)
{
    struct leonos_sessiond_hello hello;
    struct leonos_sessiond_ack ack;
    uint32_t deadline = session_now_ms() + SESSION_RETRY_MS;
    if (session_fd >= 0) return session_fd;
    while (session_fd < 0 && session_now_ms() < deadline) {
        session_fd = leonos_ipc_connect(LEONOS_IPC_SOCK_SESSION);
        if (session_fd < 0) (void)poll(0, 0, 10);
    }
    if (session_fd < 0) return -1;
    (void)leonos_ipc_set_nonblock(session_fd, 1);
    hello.pid = (uint32_t)getpid();
    hello.uid = (uint32_t)getuid();
    if (leonos_ipc_send(session_fd, LEONOS_SESSIOND_MSG_HELLO, &hello,
                        sizeof(hello)) < 0 ||
        session_wait(LEONOS_SESSIOND_MSG_ACK, &ack, sizeof(ack), 0) < 0) {
        leonos_ipc_close(session_fd);
        session_fd = -1;
        return -1;
    }
    return session_fd;
}

static int session_ack_request(uint32_t type, const void *payload, uint32_t length,
                               struct leonos_sessiond_ack *ack)
{
    if (session_open() < 0) return -1;
    if (leonos_ipc_send(session_fd, type, payload, length) < 0) return -1;
    return session_wait(LEONOS_SESSIOND_MSG_ACK, ack, sizeof(*ack), 0);
}

int leonos_startup_request(const struct leonos_startup_command *command,
                           uint32_t *out_request_id)
{
    struct leonos_sessiond_ack ack;
    if (out_request_id) *out_request_id = 0;
    if (!command) { errno = EINVAL; return -1; }
    if (session_ack_request(LEONOS_SESSIOND_MSG_REQUEST, command,
                            sizeof(*command), &ack) < 0) return -1;
    if (out_request_id) *out_request_id = ack.value;
    return ack.code == LEONOS_STARTUP_STATUS_APPROVED ? 0 : -1;
}

int leonos_startup_request_status(uint32_t request_id, uint32_t *out_status)
{
    struct leonos_startup_request_status request = {.request_id = request_id};
    if (session_open() < 0) return -1;
    if (leonos_ipc_send(session_fd, LEONOS_SESSIOND_MSG_REQUEST_STATUS,
                        &request, sizeof(request)) < 0) return -1;
    if (session_wait(LEONOS_SESSIOND_MSG_REQUEST_STATUS, &request,
                     sizeof(request), 0) < 0) return -1;
    if (out_status) *out_status = request.status;
    return 0;
}

int leonos_startup_dialog_get(struct leonos_startup_dialog_request *request)
{
    struct leonos_sessiond_ack ack;
    if (!request) return -1;
    if (session_open() < 0) return -1;
    if (leonos_ipc_send(session_fd, LEONOS_SESSIOND_MSG_DIALOG_GET, 0, 0) < 0) return -1;
    if (session_wait(LEONOS_SESSIOND_MSG_ACK, &ack, sizeof(ack), 0) < 0) return -1;
    if (ack.code <= 0) return 0;
    return 1;
}

int leonos_startup_dialog_resolve(uint32_t request_id, uint32_t decision)
{
    struct leonos_startup_dialog_resolution resolution = {
        .request_id = request_id, .decision = decision};
    struct leonos_sessiond_ack ack;
    if (session_ack_request(LEONOS_SESSIOND_MSG_DIALOG_RESOLVE, &resolution,
                            sizeof(resolution), &ack) < 0) return -1;
    return 0;
}

int leonos_startup_list(uint32_t uid, struct leonos_startup_entry *entries,
                        uint32_t capacity, uint32_t *out_count)
{
    struct leonos_startup_list request = {.uid = uid, .capacity = capacity};
    uint8_t buffer[SESSION_FRAME_CAP];
    struct leonos_sessiond_list_ack ack;
    uint32_t length = 0;
    if (out_count) *out_count = 0;
    if (session_open() < 0) return -1;
    if (leonos_ipc_send(session_fd, LEONOS_SESSIOND_MSG_LIST, &request,
                        sizeof(request)) < 0) return -1;
    if (session_wait(LEONOS_SESSIOND_MSG_LIST, buffer, sizeof(buffer), &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (entries && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        if (length - sizeof(ack) >= count * sizeof(*entries)) {
            memcpy(entries, buffer + sizeof(ack), count * sizeof(*entries));
        }
    }
    return 0;
}

int leonos_startup_set_enabled(uint32_t uid, uint32_t entry_id, uint32_t enabled)
{
    struct leonos_startup_update update = {
        .uid = uid, .entry_id = entry_id, .enabled = enabled ? 1u : 0u};
    struct leonos_sessiond_ack ack;
    return session_ack_request(LEONOS_SESSIOND_MSG_SET_ENABLED, &update,
                               sizeof(update), &ack);
}

int leonos_startup_remove(uint32_t uid, uint32_t entry_id)
{
    struct leonos_startup_update update = {.uid = uid, .entry_id = entry_id};
    struct leonos_sessiond_ack ack;
    return session_ack_request(LEONOS_SESSIOND_MSG_REMOVE, &update,
                               sizeof(update), &ack);
}

int leonos_startup_launch_current_user(void)
{
    struct leonos_sessiond_ack ack;
    return session_ack_request(LEONOS_SESSIOND_MSG_LAUNCH_CURRENT, 0, 0, &ack);
}
