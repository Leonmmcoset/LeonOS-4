/* authd client: keeps the leonos_auth_* ABI stable while replacing the
 * private auth ioctl channel with /run/leonos/authd.sock. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <leonos/auth.h>
#include <leonos/authd.h>
#include <leonos/fs.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AUTHD_FRAME_CAP 4096u
#define AUTHD_RETRY_MS 5000u
#define AUTHD_SESSION_FILE "/run/leonos/session-user"

static int authd_fd = -1;

static uint32_t authd_now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void authd_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int authd_wait(uint32_t expected, void *payload, uint32_t capacity,
                      uint32_t *length)
{
    uint32_t deadline = authd_now_ms() + 3000u;
    for (;;) {
        uint8_t buffer[AUTHD_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (leonos_ipc_recv(authd_fd, &type, buffer, sizeof(buffer), &got) == 0) {
            if (type == expected) {
                if (got > capacity) got = capacity;
                if (got) memcpy(payload, buffer, got);
                if (length) *length = got;
                return 0;
            }
            if (type == LEONOS_AUTHD_MSG_ACK && got >= sizeof(struct leonos_authd_ack)) {
                struct leonos_authd_ack ack;
                memcpy(&ack, buffer, sizeof(ack));
                if (expected == LEONOS_AUTHD_MSG_ACK) {
                    if (length) *length = got;
                    return 0;
                }
                errno = EACCES;
                return -1;
            }
        }
        if (authd_now_ms() >= deadline) return -1;
        (void)poll(0, 0, 2);
    }
}

static int authd_open(void)
{
    struct leonos_authd_hello hello;
    struct leonos_authd_ack ack;
    uint32_t deadline = authd_now_ms() + AUTHD_RETRY_MS;
    if (authd_fd >= 0) return authd_fd;
    while (authd_fd < 0 && authd_now_ms() < deadline) {
        authd_fd = leonos_ipc_connect(LEONOS_IPC_SOCK_AUTH);
        if (authd_fd < 0) (void)poll(0, 0, 10);
    }
    if (authd_fd < 0) return -1;
    (void)leonos_ipc_set_nonblock(authd_fd, 1);
    hello.pid = (uint32_t)getpid();
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_HELLO, &hello,
                        sizeof(hello)) < 0 ||
        authd_wait(LEONOS_AUTHD_MSG_ACK, &ack, sizeof(ack), 0) < 0) {
        leonos_ipc_close(authd_fd);
        authd_fd = -1;
        return -1;
    }
    return authd_fd;
}

static int authd_write_session_user(uint32_t uid)
{
    char text[16];
    uint32_t len = 0;
    int fd;
    (void)mkdir("/run", 0);
    (void)mkdir("/run/leonos", 0);
    if (!uid) {
        (void)unlink(AUTHD_SESSION_FILE);
        return 0;
    }
    {
        uint32_t value = uid;
        char tmp[12];
        uint32_t n = 0;
        if (!value) tmp[n++] = '0';
        while (value && n + 1u < sizeof(tmp)) {
            tmp[n++] = (char)('0' + value % 10u);
            value /= 10u;
        }
        while (n) text[len++] = tmp[--n];
        text[len++] = '\n';
        text[len] = 0;
    }
    fd = open(AUTHD_SESSION_FILE, LEONOS_O_WRONLY | LEONOS_O_CREAT |
              LEONOS_O_TRUNC, 0);
    if (fd < 0) return -1;
    {
        long wrote = write(fd, text, len);
        close(fd);
        return wrote == (long)len ? 0 : -1;
    }
}

int leonos_auth_status(struct leonos_auth_status *status)
{
    if (!status) { errno = EINVAL; return -1; }
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_STATUS, 0, 0) < 0) return -1;
    return authd_wait(LEONOS_AUTHD_MSG_STATUS, status, sizeof(*status), 0);
}

int leonos_auth_current(struct leonos_user_info *user)
{
    if (!user) { errno = EINVAL; return -1; }
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_CURRENT, 0, 0) < 0) return -1;
    return authd_wait(LEONOS_AUTHD_MSG_CURRENT, user, sizeof(*user), 0);
}

int leonos_auth_list_users(struct leonos_user_info *users, uint32_t capacity,
                           uint32_t include_disabled, uint32_t *out_count)
{
    struct leonos_authd_list request = {
        .include_disabled = include_disabled ? 1u : 0u,
        .capacity = capacity,
    };
    uint8_t buffer[AUTHD_FRAME_CAP];
    struct leonos_authd_list_ack ack;
    uint32_t length = 0;
    if (out_count) *out_count = 0;
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_LIST, &request,
                        sizeof(request)) < 0) return -1;
    if (authd_wait(LEONOS_AUTHD_MSG_LIST, buffer, sizeof(buffer), &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (users && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        if (length - sizeof(ack) >= count * sizeof(*users)) {
            memcpy(users, buffer + sizeof(ack), count * sizeof(*users));
        }
    }
    return 0;
}

int leonos_auth_login(const char *username, const char *password,
                      struct leonos_user_info *user)
{
    struct leonos_auth_login login;
    if (!username || !password || !user) { errno = EINVAL; return -1; }
    memset(&login, 0, sizeof(login));
    authd_copy(login.username, sizeof(login.username), username);
    authd_copy(login.password, sizeof(login.password), password);
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_LOGIN, &login,
                        sizeof(login)) < 0) return -1;
    memset(login.password, 0, sizeof(login.password));
    if (authd_wait(LEONOS_AUTHD_MSG_LOGIN, user, sizeof(*user), 0) < 0) return -1;
    (void)authd_write_session_user(user->uid);
    /* The login process itself becomes the authenticated session bootstrap.
     * Processes launched later inherit the session uid through libc spawn. */
    if (getuid() == 0) (void)setuid(user->uid);
    return 0;
}

int leonos_auth_elevate_admin(const char *username, const char *password,
                               struct leonos_user_info *user)
{
    struct leonos_auth_login login;
    if (!username || !password || !user) { errno = EINVAL; return -1; }
    memset(&login, 0, sizeof(login));
    authd_copy(login.username, sizeof(login.username), username);
    authd_copy(login.password, sizeof(login.password), password);
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_ELEVATE, &login,
                        sizeof(login)) < 0) return -1;
    memset(login.password, 0, sizeof(login.password));
    return authd_wait(LEONOS_AUTHD_MSG_ELEVATE, user, sizeof(*user), 0);
}

int leonos_auth_delegate_elevation(uint32_t child_pid)
{
    (void)child_pid;
    /* The kernel TASK_FLAG_ELEVATED_ADMIN ioctl is gone. Authenticated admin
     * children now receive their identity through setuid at spawn time. */
    return 0;
}

int leonos_auth_logout(void)
{
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_LOGOUT, 0, 0) < 0) return -1;
    (void)authd_write_session_user(0);
    return 0;
}

int leonos_auth_create_user(const char *username, const char *password,
                            uint32_t role, struct leonos_user_info *user)
{
    struct leonos_authd_create create;
    if (!username || !password || !user) { errno = EINVAL; return -1; }
    memset(&create, 0, sizeof(create));
    create.role = role;
    authd_copy(create.username, sizeof(create.username), username);
    authd_copy(create.password, sizeof(create.password), password);
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_CREATE, &create,
                        sizeof(create)) < 0) return -1;
    memset(create.password, 0, sizeof(create.password));
    return authd_wait(LEONOS_AUTHD_MSG_CREATE, user, sizeof(*user), 0);
}

int leonos_auth_update_user(uint32_t uid, uint32_t mask, uint32_t role,
                            uint32_t flags)
{
    struct leonos_authd_update update = {
        .uid = uid, .mask = mask, .role = role, .flags = flags};
    struct leonos_authd_ack ack;
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_UPDATE, &update,
                        sizeof(update)) < 0) return -1;
    return authd_wait(LEONOS_AUTHD_MSG_ACK, &ack, sizeof(ack), 0);
}

int leonos_auth_change_password(uint32_t uid, const char *old_password,
                                const char *new_password)
{
    struct leonos_authd_password password;
    struct leonos_authd_ack ack;
    if (!old_password || !new_password) { errno = EINVAL; return -1; }
    memset(&password, 0, sizeof(password));
    password.uid = uid;
    authd_copy(password.old_password, sizeof(password.old_password), old_password);
    authd_copy(password.new_password, sizeof(password.new_password), new_password);
    if (authd_open() < 0) return -1;
    if (leonos_ipc_send(authd_fd, LEONOS_AUTHD_MSG_CHANGE_PASSWORD, &password,
                        sizeof(password)) < 0) return -1;
    memset(password.old_password, 0, sizeof(password.old_password));
    memset(password.new_password, 0, sizeof(password.new_password));
    return authd_wait(LEONOS_AUTHD_MSG_ACK, &ack, sizeof(ack), 0);
}
