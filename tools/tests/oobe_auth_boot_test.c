#include <assert.h>
#include <sys/un.h>
#include <leonos/syscall.h>

static int test_open(const char *path, int flags, ...);
static long test_read(int fd, void *buffer, unsigned long count);
static int test_close(int fd);
static int test_unlink(const char *path);
#define open test_open
#define read test_read
#define close test_close
#define unlink test_unlink
#define main authd_program_main
#include "../../userland/apps/authd/main.c"
#undef main
#undef unlink
#undef close
#undef read
#undef open

static int stale_session_exists = 1;
static int cleanup_error;
static unsigned read_index;
static int listener_attempted;

static int test_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, "/system/config/users.db") && flags == LEONOS_O_RDONLY);
    return 10;
}

static long test_read(int fd, void *buffer, unsigned long count)
{
    assert(fd == 10);
    if (read_index == 0) {
        assert(count == sizeof(uint32_t));
        *(uint32_t *)buffer = AUTHD_MAGIC;
    } else if (read_index == 1) {
        assert(count == sizeof(uint32_t));
        *(uint32_t *)buffer = 1;
    } else {
        struct authd_record record = {.user = {.uid = 1, .role = LEONOS_AUTH_ROLE_ADMIN}};
        assert(read_index == 2 && count == sizeof(record));
        strcpy(record.user.username, "existing_admin");
        memcpy(buffer, &record, sizeof(record));
    }
    ++read_index;
    return (long)count;
}

static int test_close(int fd) { assert(fd == 10); return 0; }
static int test_unlink(const char *path)
{
    assert(!strcmp(path, "/run/leonos/session-user"));
    if (cleanup_error) {
        errno = cleanup_error;
        return -1;
    }
    if (!stale_session_exists) {
        errno = ENOENT;
        return -1;
    }
    stale_session_exists = 0;
    return 0;
}

int leonos_ipc_bind_listen(const char *path, int backlog)
{
    (void)path; (void)backlog;
    assert(!stale_session_exists);
    listener_attempted = 1;
    /* Stop after startup, before entering the daemon's request loop. */
    errno = EADDRINUSE;
    return -1;
}
int leonos_ipc_set_nonblock(int fd, int enabled) { (void)fd; (void)enabled; assert(0); return -1; }
int leonos_ipc_accept(int fd, struct ucred *peer) { (void)fd; (void)peer; assert(0); return -1; }
int leonos_ipc_peer_credentials(int fd, struct ucred *peer) { (void)fd; (void)peer; assert(0); return -1; }
int leonos_ipc_recv(int fd, uint32_t *type, void *payload, uint32_t capacity, uint32_t *length)
{
    (void)fd; (void)type; (void)payload; (void)capacity; (void)length;
    assert(0); return -1;
}
int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    (void)fd; (void)type; (void)payload; (void)length;
    assert(0); return -1;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "missing")) stale_session_exists = 0;
    else if (!strcmp(argv[1], "denied")) cleanup_error = EACCES;
    else assert(!strcmp(argv[1], "stale"));
    errno = ENOSYS;
    assert(authd_program_main() == 1);
    if (cleanup_error) {
        assert(!listener_attempted && stale_session_exists);
        puts("OOBE reboot: session cleanup failure prevents accepting logins");
        return 0;
    }
    assert(listener_attempted && current_uid == 0);
    assert(user_count == 1 && users[0].user.uid == 1);
    assert(!strcmp(users[0].user.username, "existing_admin"));
    puts("OOBE reboot: stale session cleared before listening, existing account retained");
    return 0;
}
