/* Host regressions for the sudo/su authorization policy.
 *
 * The policy is deliberately free of daemon state so the real gates can run
 * under ASan/UBSan on the host instead of only inside QEMU: target
 * resolution, the UID 0 passwordless switch, the cached-credential window and
 * its invalidation paths, and the privileged file-operation validation.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../userland/apps/authd/sudo_policy.h"

/* Keep the test independent of the authd wire header; these values are
 * asserted against LEONOS_FILEOP_* in the authd unit test instead. */
enum { OP_LIST = 1, OP_MKDIR = 2, OP_RENAME = 3, OP_UNLINK = 4 };

static struct leonos_auth_record records[3];

static void records_init(void)
{
    memset(records, 0, sizeof(records));
    records[0].user.uid = 0;
    records[0].user.role = LEONOS_AUTH_ROLE_ADMIN;
    strcpy(records[0].user.username, "root");
    strcpy(records[0].user.home, "/root");
    records[1].user.uid = 1000;
    records[1].user.role = LEONOS_AUTH_ROLE_USER;
    strcpy(records[1].user.username, "alice");
    strcpy(records[1].user.home, "/home/alice");
    records[2].user.uid = 1001;
    records[2].user.role = LEONOS_AUTH_ROLE_USER;
    strcpy(records[2].user.username, "xiaobai");
    strcpy(records[2].user.home, "/home/xiaobai");
}

static const struct leonos_auth_record *lookup(const char *name, void *context)
{
    (void)context;
    if (!name) {
        return NULL;
    }
    for (unsigned i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        if (strcmp(records[i].user.username, name) == 0) {
            return &records[i];
        }
    }
    return NULL;
}

static void test_target_resolution(void)
{
    struct sudo_target target;
    /* An empty username means root, so bare "sudo cmd" and "su" resolve. */
    assert(sudo_target_resolve("", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    assert(target.user.uid == 0);
    assert(strcmp(target.user.username, "root") == 0);
    assert(target.admin == 1);

    assert(sudo_target_resolve("root", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    assert(target.user.uid == 0 && target.admin == 1);

    /* Switching to an ordinary account is allowed and is not an elevation. */
    assert(sudo_target_resolve("xiaobai", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    assert(target.user.uid == 1001);
    assert(strcmp(target.user.home, "/home/xiaobai") == 0);
    assert(target.admin == 0);

    assert(sudo_target_resolve("nobody-here", lookup, NULL, &target) == SUDO_RESOLVE_NO_USER);
    assert(sudo_target_resolve("alice", NULL, NULL, &target) == SUDO_RESOLVE_BAD_ARG);
    assert(sudo_target_resolve("alice", lookup, NULL, NULL) == SUDO_RESOLVE_BAD_ARG);
    assert(sudo_target_resolve(NULL, lookup, NULL, &target) == SUDO_RESOLVE_OK);
}

static void test_disabled_account_rejected(void)
{
    struct sudo_target target;
    records[2].user.flags = LEONOS_AUTH_USER_DISABLED;
    /* A disabled account cannot be authenticated into, in either direction,
     * and the empty name must not smuggle it in as "root". */
    assert(sudo_target_resolve("xiaobai", lookup, NULL, &target) == SUDO_RESOLVE_DISABLED);
    assert(sudo_target_resolve("", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    assert(target.user.uid == 0);
    records[0].user.flags = LEONOS_AUTH_USER_DISABLED;
    assert(sudo_target_resolve("root", lookup, NULL, &target) == SUDO_RESOLVE_DISABLED);
    assert(sudo_target_resolve("", lookup, NULL, &target) == SUDO_RESOLVE_DISABLED);
    records[0].user.flags = 0;
    records[2].user.flags = 0;
}

static void test_admin_target_requires_password_or_a_window(void)
{
    struct sudo_target target;
    assert(sudo_target_resolve("root", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    /* Elevation needs either a verified password or a live window that only a
     * verified password could have opened. Without both it must ask, never
     * silently allow — and a uid 0 requester gets no exemption, so `sudo` can
     * never become passwordless. */
    assert(sudo_authorize(1000, &target, 0, 0) == SUDO_AUTH_NEED_PASSWORD);
    assert(sudo_authorize(0, &target, 0, 0) == SUDO_AUTH_ALLOW);
    assert(sudo_authorize(1000, &target, 1, 0) == SUDO_AUTH_ALLOW);
    assert(sudo_authorize(0, &target, 1, 1) == SUDO_AUTH_ALLOW);
    /* A live window authorizes without re-prompting: that is the 5 minute
     * sudo timestamp behaviour. */
    assert(sudo_authorize(1000, &target, 0, 1) == SUDO_AUTH_ALLOW);
    /* A missing target is never recoverable by asking for a password. */
    assert(sudo_authorize(0, NULL, 1, 1) == SUDO_AUTH_DENIED);
}

static void test_root_switches_users_without_password(void)
{
    struct sudo_target target;
    assert(sudo_target_resolve("xiaobai", lookup, NULL, &target) == SUDO_RESOLVE_OK);
    /* uid 0 switching to an ordinary account matches real Unix `su` and is the
     * only passwordless path in the whole policy. */
    assert(sudo_authorize(0, &target, 0, 0) == SUDO_AUTH_ALLOW);
    assert(sudo_authorize(0, &target, 1, 0) == SUDO_AUTH_ALLOW);
    /* A non-root caller must prove the target account itself. A cached sudo
     * window must NOT let them assume a different ordinary account. */
    assert(sudo_authorize(1001, &target, 0, 0) == SUDO_AUTH_NEED_PASSWORD);
    assert(sudo_authorize(1000, &target, 0, 1) == SUDO_AUTH_NEED_PASSWORD);
    assert(sudo_authorize(1000, &target, 1, 0) == SUDO_AUTH_ALLOW);
    assert(sudo_authorize(1000, &target, 1, 1) == SUDO_AUTH_ALLOW);
}

static void test_cached_window(void)
{
    struct sudo_cache cache;
    memset(&cache, 0, sizeof(cache));
    assert(sudo_cache_valid(&cache, 1000, 1000) == 0);
    sudo_cache_grant(&cache, 1000, 5000);
    assert(sudo_cache_valid(&cache, 1000, 5000) == 1);
    assert(sudo_cache_valid(&cache, 1000, 5000 + SUDO_CACHE_WINDOW_MS - 1) == 1);
    /* The window closes exactly one millisecond later. */
    assert(sudo_cache_valid(&cache, 1000, 5000 + SUDO_CACHE_WINDOW_MS) == 0);
    assert(sudo_cache_valid(&cache, 1000, 5000 + SUDO_CACHE_WINDOW_MS + 1) == 0);
    /* Grant is per requester: another uid never inherits the window. */
    sudo_cache_grant(&cache, 1000, 9000);
    assert(sudo_cache_valid(&cache, 1001, 9000) == 0);
    /* A clock that appears to move backwards must not extend the window. */
    assert(sudo_cache_valid(&cache, 1000, 8000) == 0);
}

static void test_cached_window_invalidation(void)
{
    struct sudo_cache cache;
    memset(&cache, 0, sizeof(cache));
    sudo_cache_grant(&cache, 1000, 1000);
    sudo_cache_grant(&cache, 1001, 1000);
    sudo_cache_revoke(&cache, 1000);
    assert(sudo_cache_valid(&cache, 1000, 1000) == 0);
    assert(sudo_cache_valid(&cache, 1001, 1000) == 1);

    /* An account update revokes every window that could have used it. */
    sudo_cache_grant(&cache, 1000, 1000);
    sudo_cache_revoke_all(&cache);
    assert(sudo_cache_valid(&cache, 1000, 1000) == 0);
    assert(sudo_cache_valid(&cache, 1001, 1000) == 0);

    /* An expired window is recycled instead of crowding out a live entry. */
    struct sudo_cache expired;
    memset(&expired, 0, sizeof(expired));
    for (uint32_t uid = 1; uid <= SUDO_CACHE_SLOTS; ++uid) {
        sudo_cache_grant(&expired, uid, 1000);
    }
    /* Everything granted at t=1000 has expired by t=1000+WINDOW, so each new
     * grant recycles a stale slot rather than failing. */
    const uint32_t later = 1000 + SUDO_CACHE_WINDOW_MS;
    sudo_cache_grant(&expired, 40, later);
    assert(sudo_cache_valid(&expired, 40, later) == 1);
    assert(sudo_cache_valid(&expired, 1, later) == 0);
    /* A grant that is still live must survive later grants that reuse the
     * other, already-expired slots. */
    sudo_cache_grant(&expired, 41, later + 1);
    assert(sudo_cache_valid(&expired, 40, later + 2) == 1);
    assert(sudo_cache_valid(&expired, 41, later + 2) == 1);

    /* A table with every window still live must not silently drop one: the
     * user already earned those authorizations. */
    struct sudo_cache saturated;
    memset(&saturated, 0, sizeof(saturated));
    for (uint32_t uid = 1; uid <= SUDO_CACHE_SLOTS; ++uid) {
        sudo_cache_grant(&saturated, uid, 500);
    }
    sudo_cache_grant(&saturated, 9999, 600);
    assert(sudo_cache_valid(&saturated, 9999, 600) == 0);
    for (uint32_t uid = 1; uid <= SUDO_CACHE_SLOTS; ++uid) {
        assert(sudo_cache_valid(&saturated, uid, 600) == 1);
    }
}

static void test_fileop_validation(void)
{
    /* Accepted verbs. */
    assert(sudo_fileop_check(OP_LIST, "/root", NULL) == 0);
    assert(sudo_fileop_check(OP_LIST, "/home/xiaobai", NULL) == 0);
    assert(sudo_fileop_check(OP_MKDIR, "/root", "sub") == 0);
    assert(sudo_fileop_check(OP_RENAME, "/root/a", "/root/b") == 0);
    assert(sudo_fileop_check(OP_UNLINK, "/root/a.txt", NULL) == 0);
    assert(sudo_fileop_check(OP_UNLINK, "/root/dir", "DELETE") == 0);

    /* Unknown verb and unusable paths. */
    assert(sudo_fileop_check(99, "/root", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, NULL, NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "root", NULL) < 0);
    /* ".." is rejected outright so the worker never has to trust the kernel
     * to fold it after the check. */
    assert(sudo_fileop_check(OP_LIST, "/root/../etc", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/root/..", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/root/sub/..", NULL) < 0);
    assert(sudo_fileop_check(OP_MKDIR, "/root", NULL) < 0);
    assert(sudo_fileop_check(OP_MKDIR, "/root", "sub/dir") < 0);
    assert(sudo_fileop_check(OP_MKDIR, "/root", "..") < 0);
    assert(sudo_fileop_check(OP_RENAME, "/root/a", NULL) < 0);
    assert(sudo_fileop_check(OP_RENAME, "/root/../a", "/root/b") < 0);

    /* Kernel-managed trees are never exposed through the privileged worker. */
    assert(sudo_fileop_check(OP_LIST, "/proc", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/proc/self", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/sys/kernel", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/dev", NULL) < 0);
    assert(sudo_fileop_check(OP_LIST, "/dev/disk0", NULL) < 0);
    assert(sudo_fileop_check(OP_RENAME, "/root/a", "/proc/x") < 0);
    /* A prefix match must not degrade into a substring match. */
    assert(sudo_fileop_check(OP_LIST, "/process", NULL) == 0);
    assert(sudo_fileop_check(OP_LIST, "/device", NULL) == 0);
}

static int fake_stat_dir(const char *path, struct stat *st, void *context)
{
    (void)path;
    (void)context;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0700;
    return 0;
}

static int fake_stat_file(const char *path, struct stat *st, void *context)
{
    (void)path;
    (void)context;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    return 0;
}

static int fake_stat_missing(const char *path, struct stat *st, void *context)
{
    (void)path;
    (void)st;
    (void)context;
    return -1;
}

static void test_recursive_delete_needs_confirmation(void)
{
    /* A directory needs the confirmation word; a regular file does not. This
     * is the gate that stops one dialog click from becoming an unrecoverable
     * recursive delete. The type comes from the worker's own stat, never from
     * the client. */
    assert(sudo_fileop_check(OP_UNLINK, "/root/dir", "DELETE") == 0);
    assert(sudo_delete_needs_confirm(OP_UNLINK, "/root/dir", fake_stat_dir, NULL) == 1);
    assert(sudo_delete_needs_confirm(OP_UNLINK, "/root/file", fake_stat_file, NULL) == 0);
    /* A missing target still demands confirmation: the answer must not depend
     * on the target being observable at check time. */
    assert(sudo_delete_needs_confirm(OP_UNLINK, "/root/gone", fake_stat_missing, NULL) == 1);
    /* Only UNLINK has a confirmation contract. */
    assert(sudo_delete_needs_confirm(OP_RENAME, "/root/dir", fake_stat_dir, NULL) == 0);
    assert(sudo_delete_needs_confirm(OP_LIST, "/root/dir", fake_stat_dir, NULL) == 0);
    /* Missing inputs fail closed. */
    assert(sudo_delete_needs_confirm(OP_UNLINK, NULL, fake_stat_file, NULL) == 1);
    assert(sudo_delete_needs_confirm(OP_UNLINK, "/root/file", NULL, NULL) == 1);
}

int main(void)
{
    records_init();
    test_target_resolution();
    test_disabled_account_rejected();
    test_admin_target_requires_password_or_a_window();
    test_root_switches_users_without_password();
    test_cached_window();
    test_cached_window_invalidation();
    test_fileop_validation();
    test_recursive_delete_needs_confirmation();
    printf("sudo policy ok\n");
    return 0;
}
