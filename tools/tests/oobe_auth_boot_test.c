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
static int database_fixture;

int authd_export_accounts(const char *directory, const struct leonos_auth_record *records, unsigned count)
{
    assert(!strcmp(directory, "/etc") && count == 1 && records[0].user.uid == 0);
    assert(!strcmp(records[0].user.username, "root"));
    return 0;
}

int authd_username_valid(const char *name, unsigned capacity)
{
    (void)name; (void)capacity;
    assert(0); /* No requests are accepted in this startup fixture. */
    return 0;
}

int authd_account_valid(const struct leonos_user_info *user)
{
    assert(!user->uid && user->role == LEONOS_AUTH_ROLE_ADMIN && !strcmp(user->username, "root"));
    return 1;
}

int leonos_auth_password_valid(const char *password, uint32_t capacity)
{ (void)password; (void)capacity; assert(0); return 0; }
int authd_set_password(struct leonos_auth_record *record, const char *password)
{ (void)record; (void)password; assert(0); return -1; }
int authd_check_password(const struct leonos_auth_record *record, const char *password)
{ (void)record; (void)password; assert(0); return 0; }
int authd_store_database(const char *path, const struct leonos_auth_record *records, unsigned count)
{ (void)path; (void)records; (void)count; assert(0); return -1; }
int authd_publish_session(const char *path, const struct leonos_user_info *user)
{ (void)path; (void)user; assert(0); return -1; }

static int test_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, LEONOS_PATH_USERS_DB) && flags == LEONOS_O_RDONLY);
    return 10;
}

static long test_read(int fd, void *buffer, unsigned long count)
{
    assert(fd == 10);
    if (database_fixture == 1) return 0;
    if (database_fixture == 2) { memset(buffer, 0, count); return count; }
    if (read_index == 0) {
        assert(count == sizeof(uint32_t));
        *(uint32_t *)buffer = AUTHD_MAGIC;
    } else if (read_index == 1) {
        assert(count == sizeof(uint32_t));
        *(uint32_t *)buffer = 1;
    } else {
        struct leonos_auth_record record = {.user = {.uid = 0, .role = LEONOS_AUTH_ROLE_ADMIN}};
        assert(read_index == 2 && count == sizeof(record));
        strcpy(record.user.username, "root");
        strcpy(record.user.home, "/root");
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

int leonos_ipc_bind_listen_mode(const char *path, int backlog, uint32_t mode)
{
    assert(mode == 0666);
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
    if (!strcmp(argv[1], "database-formats")) {
        database_fixture = 1;
        assert(authd_load() == -1 && errno == EIO && user_count == 0);
        database_fixture = 2;
        assert(authd_load() == -1 && errno == EIO);
        puts("Account database: truncated and corrupt seeds rejected");
        return 0;
    }
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
    assert(listener_attempted && current_uid == 0 && !session_active);
    assert(user_count == 1 && users[0].user.uid == 0);
    assert(!strcmp(users[0].user.username, "root"));
    puts("OOBE reboot: stale session cleared before listening, existing account retained");
    return 0;
}
