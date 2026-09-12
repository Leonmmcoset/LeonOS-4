/* Host regressions for the authd privileged-execution handlers.
 *
 * This links the real authd_sudo.c and drives it through its injected channel
 * and spawn hooks, so the decisions that matter — who may elevate to which
 * account, that a caller cannot authenticate as somebody else, that the cached
 * window is per requester and revocable, and that a child's status is only
 * collectable by its requester — are asserted on the host instead of only
 * behind a booted kernel.
 */
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../userland/apps/authd/authd_sudo.h"
#include "../../userland/apps/authd/sudo_policy.h"

#define CAP 4096u

struct record_log {
    uint32_t count;
    uint32_t type[16];
    uint8_t payload[16][CAP];
    uint32_t length[16];
};

struct spawn_log {
    uint32_t calls;
    char path[256];
    char argv0[192];
    char home[96];
    char username[32];
    uint32_t uid;
    int stdio_fd;
    int fail;
    int last_pid;
};

static struct leonos_auth_record record_storage[3];
static struct authd_sudo_records records;
static struct record_log replies;
static struct spawn_log spawned;
static uint32_t verified_seen;

/* The production channel and password wrappers are thin adapters over the
 * daemon's socket and Mbed TLS. This suite injects its own channel and verify
 * hooks, so the adapters are unreachable here; they still need definitions to
 * link. Keeping them as failing stubs means a test that accidentally starts
 * depending on the real ones fails loudly instead of silently passing. */
int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    (void)fd; (void)type; (void)payload; (void)length;
    assert(0 && "the injected channel must be used instead of the daemon socket");
    return -1;
}

int leonos_ipc_send_fd(int fd, uint32_t type, const void *payload, uint32_t length, int send_fd)
{
    (void)send_fd;
    return leonos_ipc_send(fd, type, payload, length);
}

int authd_check_password(const struct leonos_auth_record *record,
                         const char *password)
{
    (void)record; (void)password;
    assert(0 && "the injected verify hook must be used instead of PBKDF2");
    return 0;
}

/* The most recent reply, for the places that deliberately run several
 * requests before inspecting the transcript. */
static const uint8_t *last_reply(uint32_t *out_type, uint32_t *out_length)
{
    assert(replies.count > 0);
    if (out_type) {
        *out_type = replies.type[replies.count - 1];
    }
    if (out_length) {
        *out_length = replies.length[replies.count - 1];
    }
    return replies.payload[replies.count - 1];
}

static void reset_logs(void)
{
    memset(&replies, 0, sizeof(replies));
    memset(&spawned, 0, sizeof(spawned));
    verified_seen = 0;
}

static int channel_send(void *context, uint32_t type, const void *payload,
                        uint32_t length)
{
    (void)context;
    assert(replies.count < 16);
    if (replies.count < 16) {
        replies.type[replies.count] = type;
        if (length <= CAP) {
            memcpy(replies.payload[replies.count], payload, length);
        }
        replies.length[replies.count] = length;
        ++replies.count;
    }
    return 0;
}

/* Records what the handler asked to run. The uid passed here is the identity
 * authd decided on, which is the whole point of the test. */
static int spawn_record(void *context, const char *path, char *const argv[],
                        const char *home, const char *username, uint32_t uid,
                        int stdio_fd, uint32_t *out_pid)
{
    (void)context;
    ++spawned.calls;
    snprintf(spawned.path, sizeof(spawned.path), "%s", path ? path : "");
    snprintf(spawned.argv0, sizeof(spawned.argv0), "%s",
             (argv && argv[0]) ? argv[0] : "");
    snprintf(spawned.home, sizeof(spawned.home), "%s", home ? home : "");
    snprintf(spawned.username, sizeof(spawned.username), "%s",
             username ? username : "");
    spawned.uid = uid;
    spawned.stdio_fd = stdio_fd;
    if (spawned.fail) {
        errno = EAGAIN;
        return -1;
    }
    /* Hand back a really-exited child so authd's reap path and slot recycling
     * run for real; returning a fabricated pid would leave slots stuck and
     * would not exercise the code the daemon depends on. */
    spawned.last_pid = fork();
    if (spawned.last_pid == 0) {
        _exit(0);
    }
    assert(spawned.last_pid > 0);
    *out_pid = (uint32_t)spawned.last_pid;
    return 0;
}

static void drain(void)
{
    /* Reap the fake children and release their slots, the way the daemon loop
     * does between messages. */
    for (int i = 0; i < 200; ++i) {
        authd_sudo_maintenance();
        poll(NULL, 0, 1);
    }
    authd_sudo_maintenance();
}

/* Deterministic stand-in for the PBKDF2 check: the password is the account
 * name followed by "-pw". Records which account was actually compared. */
static int verify_record(void *context, const struct leonos_user_info *user,
                         const char *password)
{
    (void)context;
    char expected[64];
    ++verified_seen;
    snprintf(expected, sizeof(expected), "%s-pw", user->username);
    return strcmp(expected, password) == 0 ? 0 : -1;
}

static void records_init(void)
{
    memset(record_storage, 0, sizeof(record_storage));
    records.records = record_storage;
    records.count = 3;
    record_storage[0].user.uid = 0;
    record_storage[0].user.role = LEONOS_AUTH_ROLE_ADMIN;
    strcpy(record_storage[0].user.username, "root");
    strcpy(record_storage[0].user.home, "/root");
    record_storage[1].user.uid = 1000;
    record_storage[1].user.role = LEONOS_AUTH_ROLE_USER;
    strcpy(record_storage[1].user.username, "alice");
    strcpy(record_storage[1].user.home, "/home/alice");
    record_storage[2].user.uid = 1001;
    record_storage[2].user.role = LEONOS_AUTH_ROLE_USER;
    strcpy(record_storage[2].user.username, "bob");
    strcpy(record_storage[2].user.home, "/home/bob");
}

/* `require_admin` mirrors what the client sends: sudo always sets it, su
 * clears it so a switch to an ordinary account is expressible. */
static void fill_run_ex(struct leonos_authd_run *run, const char *username,
                        const char *password, const char *command,
                        uint32_t require_admin)
{
    memset(run, 0, sizeof(*run));
    if (username) {
        snprintf(run->username, sizeof(run->username), "%s", username);
    }
    if (password) {
        snprintf(run->password, sizeof(run->password), "%s", password);
    }
    run->argc = 1;
    run->flags = require_admin ? LEONOS_AUTHD_RUN_REQUIRE_ADMIN : 0u;
    snprintf(run->argv[0], sizeof(run->argv[0]), "%s", command);
}

static void fill_run(struct leonos_authd_run *run, const char *username,
                     const char *password, const char *command)
{
    fill_run_ex(run, username, password, command,
                LEONOS_AUTHD_RUN_REQUIRE_ADMIN);
}

/* Send a RUN and, unless the test wants to inspect the slot itself, collect
 * the child's status the way the real client does. Collecting is what releases
 * the daemon slot, so skipping it would leak one slot per request. */
static int run_request_ex(uint32_t requester_uid, struct leonos_authd_run *run,
                          int stdio_fd, int collect)
{
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    struct leonos_authd_wait wait;
    int result = authd_sudo_run_from_peer(requester_uid, (const uint8_t *)run,
                                          sizeof(*run), stdio_fd, &channel,
                                          &records, spawn_record, verify_record,
                                          NULL, &records);
    drain();
    if (collect && spawned.calls) {
        wait.child_pid = (uint32_t)spawned.last_pid;
        wait.reserved = 0;
        (void)authd_sudo_wait_from_peer(requester_uid, (const uint8_t *)&wait,
                                        sizeof(wait), &channel);
    }
    return result;
}

static int run_request(uint32_t requester_uid, struct leonos_authd_run *run,
                       int stdio_fd)
{
    return run_request_ex(requester_uid, run, stdio_fd, 1);
}

static void test_run_requires_a_password(void)
{
    struct leonos_authd_run run;
    reset_logs();
    /* A normal user asking for root without a password and without a cached
     * window must be refused, and no child may be created. */
    fill_run(&run, "root", NULL, "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    assert(replies.count == 1);
    assert(replies.type[0] == LEONOS_AUTHD_MSG_RUN);
    struct leonos_authd_run_ack ack;
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES && ack.child_pid == 0);
}

static void test_run_rejects_a_wrong_password(void)
{
    struct leonos_authd_run run;
    reset_logs();
    fill_run(&run, "root", "not-the-password", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(verified_seen == 1);
    assert(spawned.calls == 0);
    struct leonos_authd_run_ack ack;
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);
}

static void test_run_elevates_with_the_target_password(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    /* The password was checked against root, not against the caller. */
    assert(verified_seen == 1);
    assert(spawned.calls == 1);
    assert(spawned.uid == 0);
    assert(strcmp(spawned.username, "root") == 0);
    assert(strcmp(spawned.home, "/root") == 0);
    assert(replies.type[0] == LEONOS_AUTHD_MSG_RUN);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == 0);
    assert(ack.child_pid == (uint32_t)spawned.last_pid);
}

static void test_cached_window_is_per_requester_and_revocable(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    /* The window is module state shared across requests, so start from a clean
     * slate rather than relying on the order the suites run in. */
    authd_sudo_cache_revoke_all();
    reset_logs();
    /* An empty password with no window must be refused: the cache is the only
     * thing that can authorize a passwordless request. */
    fill_run(&run, "root", NULL, "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);

    /* A correct password verifies once and opens the window for uid 1000. */
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1);
    assert(verified_seen == 1);
    /* Same requester, empty password inside the window: allowed, and the
     * password is not re-checked. */
    reset_logs();
    fill_run(&run, "root", NULL, "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1);
    assert(verified_seen == 0);
    /* A different requester inherits nothing. */
    reset_logs();
    assert(run_request(1001, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);
    /* sudo -k drops it, and the next empty-password request is refused again. */
    authd_sudo_kill_from_peer(1000, &channel);
    reset_logs();
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);
    /* An account update revokes every window, so an elevation cannot outlive
     * the credentials that authorized it. */
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1);
    authd_sudo_cache_revoke_all();
    reset_logs();
    fill_run(&run, "root", NULL, "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
}

static void test_sudo_check_reports_the_window(void)
{
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    struct leonos_authd_run run;
    reset_logs();
    authd_sudo_check_from_peer(1000, &channel);
    struct leonos_authd_ack ack;
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == 0);
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    reset_logs();
    authd_sudo_check_from_peer(1000, &channel);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == 1);
}

static void test_su_switch_to_a_normal_user(void)
{
    struct leonos_authd_run run;
    reset_logs();
    /* A uid 0 requester switching to a normal account needs no password: this
     * is real Unix `su alice` and the only passwordless path. */
    fill_run_ex(&run, "alice", NULL, "/bin/sh", 0);
    assert(run_request(0, &run, -1) == 0);
    assert(spawned.calls == 1);
    assert(spawned.uid == 1000);
    assert(strcmp(spawned.username, "alice") == 0);
    assert(strcmp(spawned.home, "/home/alice") == 0);
    assert(verified_seen == 0);
}

static void test_su_cannot_impersonate_with_the_wrong_account(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    reset_logs();
    /* alice supplying her OWN password to become bob must fail: the password
     * is checked against bob, not against the caller. */
    fill_run_ex(&run, "bob", "alice-pw", "/bin/sh", 0);
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);

    /* A live sudo window in the SAME process must not become a way to switch
     * into bob: open the window first, then try with an empty password. */
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1);
    reset_logs();
    fill_run_ex(&run, "bob", NULL, "/bin/sh", 0);
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);

    /* bob's own password works from a non-root caller. */
    reset_logs();
    fill_run_ex(&run, "bob", "bob-pw", "/bin/sh", 0);
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 1);
    assert(spawned.uid == 1001);
}

static void test_sudo_never_targets_a_non_admin(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    reset_logs();
    /* sudo without -u always elevates, so a normal-account target is refused
     * even with that account's correct password: it must not become a way to
     * assume a different ordinary user's identity. */
    fill_run(&run, "bob", "bob-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);
}

static void test_unknown_and_disabled_targets(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    reset_logs();
    fill_run(&run, "nobody", "nobody-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);

    record_storage[0].user.flags = LEONOS_AUTH_USER_DISABLED;
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request(1000, &run, -1) == 0);
    assert(spawned.calls == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EACCES);
    record_storage[0].user.flags = 0;
}

static void test_malformed_requests_are_refused(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_run_ack ack;
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    reset_logs();
    /* Truncated frame. */
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(authd_sudo_run_from_peer(1000, (const uint8_t *)&run,
                                    sizeof(run) - 1, -1, &channel, &records,
                                    spawn_record, verify_record, NULL,
                                    &records) == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EINVAL);
    assert(spawned.calls == 0);
    /* Zero argc and an empty program path. */
    reset_logs();
    run.argc = 0;
    assert(run_request(1000, &run, -1) == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EINVAL);
    assert(spawned.calls == 0);
    reset_logs();
    fill_run(&run, "root", "root-pw", "");
    assert(run_request(1000, &run, -1) == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EINVAL);
    assert(spawned.calls == 0);
    /* argc above the wire limit. */
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    run.argc = LEONOS_AUTHD_RUN_MAX_ARGS + 1;
    assert(run_request(1000, &run, -1) == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EINVAL);
    assert(spawned.calls == 0);
    /* A spawn failure is reported as an error and drops the peer. */
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    spawned.fail = 1;
    assert(run_request(1000, &run, -1) == -1);
    assert(spawned.calls == 1);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EAGAIN);
    spawned.fail = 0;
}

static void test_wait_is_owner_only(void)
{
    struct leonos_authd_run run;
    struct leonos_authd_wait wait;
    struct leonos_authd_wait_ack ack;
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    assert(run_request_ex(1000, &run, -1, 0) == 0);
    wait.child_pid = (uint32_t)spawned.last_pid;
    wait.reserved = 0;
    /* A different uid must not be able to collect this child's status. */
    authd_sudo_wait_from_peer(1001, (const uint8_t *)&wait, sizeof(wait),
                              &channel);
    uint32_t wait_type = 0;
    memcpy(&ack, last_reply(&wait_type, NULL), sizeof(ack));
    assert(wait_type == LEONOS_AUTHD_MSG_WAIT);
    assert(ack.code == -ESRCH);
    /* The owner collects it and gets the real exit status. */
    reset_logs();
    authd_sudo_wait_from_peer(1000, (const uint8_t *)&wait, sizeof(wait),
                              &channel);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == 0);
    /* An unknown child reports ESRCH rather than hanging. */
    reset_logs();
    wait.child_pid = 999999u;
    authd_sudo_wait_from_peer(1000, (const uint8_t *)&wait, sizeof(wait),
                              &channel);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -ESRCH);
}

/* A long session must be able to elevate far more times than the daemon has
 * slots: each collected command returns its slot to the pool. */
static void test_slot_table_recycles(void)
{
    struct leonos_authd_run run;
    reset_logs();
    fill_run(&run, "root", "root-pw", "/bin/id");
    for (uint32_t i = 0; i < 64; ++i) {
        reset_logs();
        assert(run_request(1000, &run, -1) == 0);
        assert(spawned.calls == 1);
    }
}

static void test_full_table_does_not_execute(void)
{
    struct leonos_authd_run run;
    uint32_t pids[16];
    fill_run(&run, "root", "root-pw", "/bin/id");
    for (unsigned i = 0; i < 16; ++i) {
        reset_logs();
        assert(run_request_ex(1000, &run, -1, 0) == 0);
        assert(spawned.calls == 1);
        pids[i] = (uint32_t)spawned.last_pid;
    }
    reset_logs();
    assert(run_request_ex(1000, &run, -1, 0) == 0);
    assert(spawned.calls == 0);
    struct leonos_authd_run_ack ack;
    memcpy(&ack, last_reply(NULL, NULL), sizeof(ack));
    assert(ack.code == -ENOSPC);
    struct authd_sudo_channel channel = {.send = channel_send};
    for (unsigned i = 0; i < 16; ++i) {
        reset_logs();
        struct leonos_authd_wait wait = {.child_pid = pids[i]};
        assert(authd_sudo_wait_from_peer(1000, (const uint8_t *)&wait, sizeof(wait), &channel) == 0);
    }
}

static void test_fileop_validates_before_authorizing(void)
{
    struct leonos_authd_fileop op;
    struct leonos_authd_fileop_ack ack;
    struct authd_sudo_channel channel = {.send = channel_send, .context = NULL};
    reset_logs();
    memset(&op, 0, sizeof(op));
    snprintf(op.username, sizeof(op.username), "root");
    snprintf(op.password, sizeof(op.password), "root-pw");
    op.op = LEONOS_FILEOP_LIST;
    snprintf(op.path1, sizeof(op.path1), "/proc/self");
    /* A denied path is rejected without consuming a password check. */
    assert(authd_sudo_fileop_from_peer(1000, (const uint8_t *)&op, sizeof(op),
                                       &channel, &records, spawn_record,
                                       verify_record, NULL, &records) == 0);
    assert(spawned.calls == 0);
    assert(verified_seen == 0);
    memcpy(&ack, replies.payload[0], sizeof(ack));
    assert(ack.code == -EINVAL);

    /* Unknown verbs are refused the same way, and a directory delete without
     * the confirmation word never reaches the worker. */
    reset_logs();
    op.op = 99;
    snprintf(op.path1, sizeof(op.path1), "/root");
    assert(authd_sudo_fileop_from_peer(1000, (const uint8_t *)&op, sizeof(op),
                                       &channel, &records, spawn_record,
                                       verify_record, NULL, &records) == 0);
    assert(spawned.calls == 0);
    reset_logs();
    op.op = LEONOS_FILEOP_RENAME;
    memset(op.path2, 0, sizeof(op.path2));
    assert(authd_sudo_fileop_from_peer(1000, (const uint8_t *)&op, sizeof(op),
                                       &channel, &records, spawn_record,
                                       verify_record, NULL, &records) == 0);
    assert(spawned.calls == 0);
    assert(verified_seen == 0);
}

int main(void)
{
    records_init();
    test_run_requires_a_password();
    test_run_rejects_a_wrong_password();
    test_run_elevates_with_the_target_password();
    test_cached_window_is_per_requester_and_revocable();
    test_sudo_check_reports_the_window();
    test_su_switch_to_a_normal_user();
    test_su_cannot_impersonate_with_the_wrong_account();
    test_sudo_never_targets_a_non_admin();
    test_unknown_and_disabled_targets();
    test_malformed_requests_are_refused();
    test_wait_is_owner_only();
    test_slot_table_recycles();
    test_full_table_does_not_execute();
    test_fileop_validates_before_authorizing();
    printf("authd sudo handlers ok\n");
    return 0;
}
