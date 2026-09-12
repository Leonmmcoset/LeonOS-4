/* Historical regression fixture only. Never build or stage into LeonOS. */
/* sudo/su authorization policy.
 *
 * Every privileged request authd serves is gated by the functions in this
 * header. They are pure (the user lookup and the file check are injected) so
 * the real gates run in the host test under ASan/UBSan, and authd cannot grow
 * a second, weaker path by accident.
 *
 * The requester's uid always comes from SO_PEERCRED, never from a request
 * field. The only passwordless path in the whole policy is a uid 0 requester
 * switching to an ordinary account, which mirrors real Unix `su`.
 */
#ifndef AUTHD_SUDO_POLICY_H
#define AUTHD_SUDO_POLICY_H

#include <leonos/auth.h>
#include <leonos/auth_db.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

/* Privileged file-operation verbs. These values are the authd wire constants
 * (LEONOS_FILEOP_*); authd asserts they stay equal instead of sharing a header,
 * so the policy unit stays free of the daemon protocol. */
#define SUDO_FILEOP_LIST 1u
#define SUDO_FILEOP_MKDIR 2u
#define SUDO_FILEOP_RENAME 3u
#define SUDO_FILEOP_UNLINK 4u

/* How long a verified password authorizes further privileged requests from the
 * same requester uid. Matches the sudo timestamp convention. */
#define SUDO_CACHE_WINDOW_MS 300000u
#define SUDO_CACHE_SLOTS 32u

/* Recursive delete confirmation word. Removing a directory is unrecoverable,
 * so it needs an explicit second signal beyond the password dialog. */
#define SUDO_DELETE_CONFIRM "DELETE"

enum sudo_resolve_result {
    SUDO_RESOLVE_OK = 0,
    SUDO_RESOLVE_BAD_ARG = -1,
    SUDO_RESOLVE_NO_USER = -2,
    SUDO_RESOLVE_DISABLED = -3,
};

enum sudo_auth_result {
    SUDO_AUTH_ALLOW = 0,
    SUDO_AUTH_NEED_PASSWORD = 1,
    SUDO_AUTH_DENIED = -1,
};

struct sudo_target {
    struct leonos_user_info user;
    uint32_t admin;
};

struct sudo_cache_entry {
    uint32_t used;
    uint32_t uid;
    uint32_t verified_at_ms;
    uint32_t session_id;
};

struct sudo_cache {
    struct sudo_cache_entry entries[SUDO_CACHE_SLOTS];
};

typedef const struct leonos_auth_record *(*sudo_user_lookup_fn)(const char *name,
                                                                void *context);

/* Resolve a requested account name. An empty name means root so that a bare
 * `sudo cmd` and a bare `su` need no extra argument. */
static inline int sudo_target_resolve(const char *name,
                                      sudo_user_lookup_fn lookup,
                                      void *context,
                                      struct sudo_target *out)
{
    const struct leonos_auth_record *record;
    if (!lookup || !out) {
        return SUDO_RESOLVE_BAD_ARG;
    }
    record = lookup(name && name[0] ? name : "root", context);
    if (!record) {
        return SUDO_RESOLVE_NO_USER;
    }
    if (record->user.flags & LEONOS_AUTH_USER_DISABLED) {
        return SUDO_RESOLVE_DISABLED;
    }
    out->user = record->user;
    out->admin = record->user.role == LEONOS_AUTH_ROLE_ADMIN ? 1u : 0u;
    return SUDO_RESOLVE_OK;
}

/* Decide whether a request may proceed.
 *
 * `password_verified` is set only after authd has actually compared a supplied
 * password against the target account; a client cannot claim it. Requiring the
 * target account's own password is what stops a caller from authenticating as
 * somebody else: an ordinary user switching to another ordinary account must
 * prove that account, not their own.
 *
 * A live cache entry authorizes an administrator target — that is the sudo
 * timestamp window — but it must never authorize assuming a *different*
 * ordinary account's identity. Otherwise a user who once ran `sudo` could then
 * switch into any other ordinary account without knowing its password.
 *
 * NEED_PASSWORD means "ask the user", DENIED means "no password will help". */
static inline int sudo_authorize(uint32_t requester_uid,
                                 const struct sudo_target *target,
                                 uint32_t password_verified,
                                 uint32_t cache_valid)
{
    if (!target) {
        return SUDO_AUTH_DENIED;
    }
    if (requester_uid == 0) return SUDO_AUTH_ALLOW;
    if (target->admin) {
        /* Elevation: the password, or a window that one already opened. */
        return (password_verified || cache_valid) ? SUDO_AUTH_ALLOW
                                                 : SUDO_AUTH_NEED_PASSWORD;
    }
    /* Switching to an ordinary account: only a uid 0 caller is exempt, which
     * is real Unix `su`. Everyone else proves the target account itself. */
    if (requester_uid == 0) {
        return SUDO_AUTH_ALLOW;
    }
    return password_verified ? SUDO_AUTH_ALLOW : SUDO_AUTH_NEED_PASSWORD;
}

static inline void sudo_cache_grant_session(struct sudo_cache *cache, uint32_t uid,
                                            uint32_t session_id, uint32_t now_ms)
{
    struct sudo_cache_entry *free_slot = NULL;
    struct sudo_cache_entry *expired_slot = NULL;
    if (!cache) {
        return;
    }
    for (uint32_t i = 0; i < SUDO_CACHE_SLOTS; ++i) {
        struct sudo_cache_entry *entry = &cache->entries[i];
        if (entry->used && entry->uid == uid && entry->session_id == session_id) {
            entry->verified_at_ms = now_ms;
            return;
        }
        if (entry->used) {
            /* An expired window is free real estate; recycling it keeps a
             * stale uid from crowding out a live one. */
            if (expired_slot == NULL &&
                now_ms - entry->verified_at_ms >= SUDO_CACHE_WINDOW_MS) {
                expired_slot = entry;
            }
            continue;
        }
        if (!free_slot) {
            free_slot = entry;
        }
    }
    if (!free_slot) {
        free_slot = expired_slot;
    }
    if (!free_slot) {
        /* The table is sized for LEONOS_AUTH_MAX_USERS. If every slot holds a
         * live window, refuse the new grant: dropping a live entry would
         * silently revoke an authorization the user already earned. */
        return;
    }
    free_slot->used = 1;
    free_slot->uid = uid;
    free_slot->verified_at_ms = now_ms;
    free_slot->session_id = session_id;
}

static inline int sudo_cache_valid_session(const struct sudo_cache *cache, uint32_t uid,
                                           uint32_t session_id, uint32_t now_ms)
{
    if (!cache) {
        return 0;
    }
    for (uint32_t i = 0; i < SUDO_CACHE_SLOTS; ++i) {
        if (!cache->entries[i].used || cache->entries[i].uid != uid ||
            cache->entries[i].session_id != session_id) {
            continue;
        }
        uint32_t elapsed = now_ms - cache->entries[i].verified_at_ms;
        /* A window is open for strictly less than SUDO_CACHE_WINDOW_MS: one
         * millisecond past the verified time it is already closed. A clock
         * that appears to move backwards must not extend it either. */
        if (elapsed >= SUDO_CACHE_WINDOW_MS) {
            return 0;
        }
        return 1;
    }
    return 0;
}

static inline void sudo_cache_grant(struct sudo_cache *cache, uint32_t uid, uint32_t now_ms)
{
    sudo_cache_grant_session(cache, uid, 0, now_ms);
}

static inline int sudo_cache_valid(const struct sudo_cache *cache, uint32_t uid, uint32_t now_ms)
{
    return sudo_cache_valid_session(cache, uid, 0, now_ms);
}

static inline void sudo_cache_revoke(struct sudo_cache *cache, uint32_t uid)
{
    if (!cache) {
        return;
    }
    for (uint32_t i = 0; i < SUDO_CACHE_SLOTS; ++i) {
        if (cache->entries[i].used && cache->entries[i].uid == uid) {
            cache->entries[i].used = 0;
            cache->entries[i].uid = 0;
            cache->entries[i].verified_at_ms = 0;
        }
    }
}

static inline void sudo_cache_revoke_all(struct sudo_cache *cache)
{
    if (!cache) {
        return;
    }
    for (uint32_t i = 0; i < SUDO_CACHE_SLOTS; ++i) {
        cache->entries[i].used = 0;
        cache->entries[i].uid = 0;
        cache->entries[i].verified_at_ms = 0;
    }
}

/* Reject any path the privileged worker must never touch. ".." is refused
 * outright so the worker never relies on the kernel folding it after the
 * check, and the kernel-managed trees are refused by path prefix (a plain
 * substring match would wrongly reject "/process"). */
static inline int sudo_path_allowed(const char *path)
{
    static const char *const denied[] = {"/proc", "/sys", "/dev"};
    if (!path || path[0] != '/') {
        return 0;
    }
    for (const char *scan = path; *scan; ++scan) {
        /* Require canonical spelling; do not let redundant components bypass
         * the protected-tree check below. The worker also resolves by dirfd. */
        if (scan[0] == '/' && (scan[1] == '/' ||
            (scan[1] == '.' && (scan[2] == '/' || scan[2] == 0)))) {
            return 0;
        }
        if (scan[0] == '.' && scan[1] == '.' &&
            (scan == path || scan[-1] == '/') &&
            (scan[2] == 0 || scan[2] == '/')) {
            return 0;
        }
    }
    for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i) {
        uint32_t length = 0;
        while (denied[i][length]) {
            ++length;
        }
        /* strncmp bounds the comparison at the prefix, so a short path can
         * never read past its own terminator. */
        if (strncmp(path, denied[i], length) != 0) {
            continue;
        }
        if (path[length] == 0 || path[length] == '/') {
            return 0;
        }
    }
    return 1;
}

/* Validate one privileged file operation without touching the filesystem.
 * The op values are the LEONOS_FILEOP_* wire constants. */
static inline int sudo_fileop_check(uint32_t op, const char *path1,
                                    const char *path2)
{
    switch (op) {
    case SUDO_FILEOP_LIST:
        return sudo_path_allowed(path1) ? 0 : -1;
    case SUDO_FILEOP_MKDIR:
        if (!sudo_path_allowed(path1) || !path2 || !path2[0]) {
            return -1;
        }
        if (path2[0] == '/' || strchr(path2, '/')) {
            return -1;
        }
        if (strcmp(path2, ".") == 0 || strcmp(path2, "..") == 0) {
            return -1;
        }
        return 0;
    case SUDO_FILEOP_RENAME:
        return sudo_path_allowed(path1) && sudo_path_allowed(path2) ? 0 : -1;
    case SUDO_FILEOP_UNLINK:
        if (!sudo_path_allowed(path1)) {
            return -1;
        }
        if (path2 && path2[0] && strcmp(path2, SUDO_DELETE_CONFIRM) != 0) {
            return -1;
        }
        return 0;
    default:
        return -1;
    }
}

typedef int (*sudo_stat_fn)(const char *path, struct stat *st, void *context);

/* Decide whether an UNLINK needs the confirmation word.
 *
 * The directory type comes from the worker's own stat, not from the client:
 * if the client could assert "this is a file", one click would silently become
 * a recursive delete. A target that cannot be inspected still demands
 * confirmation, so the answer never depends on the target being observable. */
static inline int sudo_delete_needs_confirm(uint32_t op, const char *path,
                                            sudo_stat_fn stat_fn, void *context)
{
    struct stat st;
    if (op != SUDO_FILEOP_UNLINK) {
        return 0;
    }
    if (!stat_fn || !path) {
        return 1;
    }
    if (stat_fn(path, &st, context) < 0) {
        return 1;
    }
    return (st.st_mode & S_IFMT) == S_IFDIR ? 1 : 0;
}

#endif
