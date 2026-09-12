/* Historical regression fixture only. Never build or stage into LeonOS. */
/* authd privileged execution: sudo, su and the Fileman file-operation broker.
 *
 * The daemon is the only long-lived uid 0 process other than init, and the
 * kernel lets only uid 0 change a task's identity, so all elevation funnels
 * through here. Every entry point takes its caller identity from SO_PEERCRED
 * via the client slot, never from the request payload.
 *
 * Three things are injected rather than called directly: the reply channel,
 * the privileged spawn step and the password check. That keeps the decision
 * "who may elevate to what" in host-testable code instead of only reachable
 * behind a booted kernel.
 */
#include <errno.h>
#include <fcntl.h>
#include <leonos/authd.h>
#include <leonos/fs.h>
#include <leonos/layout.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <signal.h>

#include "accounts.h"
#include "authd_sudo.h"
#include "sudo_policy.h"

#define AUTHD_SUDO_SLOTS 16u
#define AUTHD_SUDO_WAIT_MS 100u
#define AUTHD_SUDO_LINGER_MS 60000u
#define AUTHD_FILEOP_DIR LEONOS_LAYOUT_RUN_LEONOS "/fileop"
#define AUTHD_SUDOD_PATH LEONOS_LAYOUT_LEONOS_APPS "/sudod/sudod.elf"
#define AUTHD_SUDO_PATH "/bin:/usr/bin:/usr/sbin:/sbin"

struct authd_sudo_slot {
    uint32_t used;
    uint32_t child_pid;
    uint32_t owner_uid;
    uint32_t owner_pid;
    int32_t status;
    uint32_t reaped;
    uint32_t started_ms;
    uint32_t result_path_used;
    char result_path[LEONOS_FS_PATH_LEN];
};

static struct authd_sudo_slot authd_sudo_slots[AUTHD_SUDO_SLOTS];
static struct sudo_cache authd_sudo_cache;
static uint32_t authd_fileop_seq;

static uint32_t authd_sudo_now_ms(void)
{
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

/* Small bounded string append used to build the child environment without
 * pulling in formatted output for fixed-size fields. */
static void authd_sudo_env(char *dst, uint32_t capacity, const char *name,
                           const char *value)
{
    uint32_t pos = 0;
    if (!dst || !capacity || !name) {
        return;
    }
    for (uint32_t i = 0; name[i] && pos + 1 < capacity; ++i) {
        dst[pos++] = name[i];
    }
    if (pos + 1 < capacity) {
        dst[pos++] = '=';
    }
    for (uint32_t i = 0; value && value[i] && pos + 1 < capacity; ++i) {
        dst[pos++] = value[i];
    }
    dst[pos] = 0;
}

/* Find an account record for the policy unit. Returns NULL for unknown names,
 * which the policy reports as SUDO_RESOLVE_NO_USER.
 *
 * The record count travels with the pointer: scanning a fixed maximum would
 * read past a shorter table, and the caller (not this module) owns the table's
 * real length. */
static const struct leonos_auth_record *authd_sudo_lookup_record(const char *name,
                                                                 void *context)
{
    const struct authd_sudo_records *table =
        (const struct authd_sudo_records *)context;
    if (!table || !table->records || !name) {
        return 0;
    }
    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->records[i].user.username[0] &&
            strcmp(table->records[i].user.username, name) == 0) {
            return &table->records[i];
        }
    }
    return 0;
}

/* The production spawn step: fork, wire the caller's terminal to all three
 * standard descriptors, assume the target identity, then exec.
 *
 * The child never calls setuid() through musl: __setxid broadcasts to every
 * thread and SIGKILLs the whole process when any thread fails, so a refused
 * identity change would masquerade as a crash. Every step here is explicit and
 * every failure exits with a distinct status. */
static void authd_exec_error(int error)
{
    (void)write(3, &error, sizeof(error));
    _exit(error == ENOENT ? 127 : 126);
}

int authd_sudo_spawn(void *context, const char *path,
                     char *const argv[], const char *home,
                     const char *username, uint32_t uid,
                     int stdio_fd, uint32_t *out_pid)
{
    char home_env[LEONOS_AUTH_HOME_LEN + 8];
    char user_env[LEONOS_AUTH_USERNAME_LEN + 8];
    char logname_env[LEONOS_AUTH_USERNAME_LEN + 8];
    char term_env[72];
    char *envp[7];
    char *exec_argv[LEONOS_AUTHD_RUN_MAX_ARGS + 2];
    struct authd_run_context *run = context;
    int errors[2];
    pid_t pid;

    if (!path || !out_pid) {
        errno = EINVAL;
        return -1;
    }
    unsigned argc = 0;
    while (argv[argc] && argc < LEONOS_AUTHD_RUN_MAX_ARGS + 1) {
        exec_argv[argc] = argv[argc];
        ++argc;
    }
    if (argv[argc]) { errno = E2BIG; return -1; }
    exec_argv[argc] = NULL;
    if (pipe2(errors, O_CLOEXEC) < 0) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        if (errors[i] >= 4) continue;
        int copy = fcntl(errors[i], F_DUPFD_CLOEXEC, 4);
        if (copy < 0) {
            int saved = errno;
            close(errors[0]); close(errors[1]);
            errno = saved;
            return -1;
        }
        close(errors[i]);
        errors[i] = copy;
    }
    pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(errors[0]);
        close(errors[1]);
        errno = saved;
        return -1;
    }
    if (pid == 0) {
        close(errors[0]);
        for (int i = 0; i < 3; ++i) {
            int source = run ? run->stdio[i] : stdio_fd;
            if (run && source < 0) { close(i); continue; }
            if (source >= 0 && dup2(source, i) < 0) {
                int error = errno;
                (void)write(errors[1], &error, sizeof(error));
                _exit(126);
            }
        }
        if (errors[1] != 3 && dup3(errors[1], 3, O_CLOEXEC) < 0) _exit(126);
        if (syscall(SYS_close_range, 4u, ~0u, 0u) < 0) authd_exec_error(errno);
        if (uid != (uint32_t)getuid()) {
            struct passwd *pw = getpwnam(username);
            if (!pw || pw->pw_uid != uid) authd_exec_error(ENOENT);
            if (initgroups(username, pw->pw_gid) < 0 || setgid(pw->pw_gid) < 0 ||
                setuid(uid) < 0) authd_exec_error(errno);
        }
        const char *cwd = run && run->request &&
                          !(run->request->flags & LEONOS_AUTHD_RUN_LOGIN)
                          ? run->request->cwd : home;
        if (cwd && cwd[0] && chdir(cwd) < 0) authd_exec_error(errno);
        authd_sudo_env(home_env, sizeof(home_env), "HOME", home);
        authd_sudo_env(user_env, sizeof(user_env), "USER", username);
        authd_sudo_env(logname_env, sizeof(logname_env), "LOGNAME", username);
        envp[0] = home_env;
        envp[1] = user_env;
        envp[2] = logname_env;
        envp[3] = (char *)"PATH=" AUTHD_SUDO_PATH;
        authd_sudo_env(term_env, sizeof(term_env), "TERM",
                      run && run->request ? run->request->term : "");
        envp[4] = term_env;
        envp[5] = (char *)"SHELL=/bin/sh";
        envp[6] = 0;
        if (run && run->request && (run->request->flags & LEONOS_AUTHD_RUN_LOGIN)) {
            if (strcmp(path, "/bin/sh")) authd_exec_error(EINVAL);
            exec_argv[0] = (char *)"-sh";
        }
        if (strchr(path, '/')) {
            execve(path, exec_argv, envp);
            authd_exec_error(errno);
        }
        const char *dirs[] = {"/bin", "/usr/bin", "/usr/sbin", "/sbin"};
        int error = ENOENT;
        for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
            char candidate[LEONOS_FS_PATH_LEN];
            int n = snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], path);
            if (n < 0 || (size_t)n >= sizeof(candidate)) authd_exec_error(ENAMETOOLONG);
            execve(candidate, exec_argv, envp);
            if (errno == EACCES) error = EACCES;
            else if (errno != ENOENT && errno != ENOTDIR) authd_exec_error(errno);
        }
        authd_exec_error(error);
    }
    close(errors[1]);
    int error = 0;
    ssize_t n;
    do { n = read(errors[0], &error, sizeof(error)); } while (n < 0 && errno == EINTR);
    close(errors[0]);
    if (n != 0) {
        if (n < 0) error = errno;
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        errno = error ? error : EIO;
        return -1;
    }
    *out_pid = (uint32_t)pid;
    return 0;
}

static struct authd_sudo_slot *authd_sudo_alloc_slot(uint32_t child_pid,
                                                     uint32_t owner_uid)
{
    for (uint32_t i = 0; i < AUTHD_SUDO_SLOTS; ++i) {
        if (!authd_sudo_slots[i].used) {
            memset(&authd_sudo_slots[i], 0, sizeof(authd_sudo_slots[i]));
            authd_sudo_slots[i].used = 1;
            authd_sudo_slots[i].child_pid = child_pid;
            authd_sudo_slots[i].owner_uid = owner_uid;
            authd_sudo_slots[i].started_ms = authd_sudo_now_ms();
            return &authd_sudo_slots[i];
        }
    }
    return 0;
}

static struct authd_sudo_slot *authd_sudo_find_slot(uint32_t child_pid)
{
    for (uint32_t i = 0; i < AUTHD_SUDO_SLOTS; ++i) {
        if (authd_sudo_slots[i].used &&
            authd_sudo_slots[i].child_pid == child_pid) {
            return &authd_sudo_slots[i];
        }
    }
    return 0;
}

/* Reap finished children so a long session cannot accumulate zombies, and
 * record the status for the client that is waiting on it. Never blocks. */
static void authd_sudo_reap(void)
{
    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            return;
        }
        struct authd_sudo_slot *slot = authd_sudo_find_slot((uint32_t)pid);
        if (slot && !slot->reaped) {
            slot->status = status;
            slot->reaped = 1;
            slot->started_ms = authd_sudo_now_ms();
        }
    }
}

/* Drop slots nobody collected. A client that died mid-command must not leak a
 * slot forever, and its result file has to go with it. */
static void authd_sudo_expire(void)
{
    uint32_t now = authd_sudo_now_ms();
    for (uint32_t i = 0; i < AUTHD_SUDO_SLOTS; ++i) {
        struct authd_sudo_slot *slot = &authd_sudo_slots[i];
        if (!slot->used || !slot->reaped) {
            continue;
        }
        if (now - slot->started_ms < AUTHD_SUDO_LINGER_MS) {
            continue;
        }
        if (slot->result_path_used) {
            (void)unlink(slot->result_path);
        }
        memset(slot, 0, sizeof(*slot));
    }
}

int authd_sudo_maintenance(void)
{
    authd_sudo_reap();
    authd_sudo_expire();
    return 0;
}

static void authd_sudo_send_ack(const struct authd_sudo_channel *channel,
                                int32_t code)
{
    struct leonos_authd_ack ack = {.code = code, .reserved = 0};
    if (channel && channel->send) {
        (void)channel->send(channel->context, LEONOS_AUTHD_MSG_ACK, &ack,
                            sizeof(ack));
    }
}

/* A rejected RUN/FILEOP is answered on the same message type the request used,
 * with the errno in its code field, so a client reads exactly one reply and
 * cannot mistake a status for a different operation. */
static void authd_sudo_send_error(const struct authd_sudo_channel *channel,
                                  uint32_t type, int32_t code)
{
    if (!channel || !channel->send) {
        return;
    }
    if (type == LEONOS_AUTHD_MSG_RUN) {
        struct leonos_authd_run_ack ack = {.code = code, .child_pid = 0};
        (void)channel->send(channel->context, type, &ack, sizeof(ack));
    } else {
        struct leonos_authd_fileop_ack ack = {.code = code, .child_pid = 0};
        (void)channel->send(channel->context, type, &ack, sizeof(ack));
    }
}

/* Resolve the target account and decide whether this request is authorized.
 * An empty password means the cached window is the only thing that can
 * authorize it; a non-empty password is always verified against the target
 * account, never against the caller's own account. */
static int authd_sudo_authorize(const char *username, const char *password,
                                uint32_t requester_uid, uint32_t session_id, uint32_t require_admin,
                                const struct authd_sudo_records *records,
                                authd_verify_fn verify, void *verify_context,
                                struct leonos_user_info *out_user)
{
    struct sudo_target target;
    uint32_t password_verified = 0;
    uint32_t cache_valid;
    int resolved = sudo_target_resolve(username,
                                       authd_sudo_lookup_record,
                                       (void *)records, &target);
    if (resolved == SUDO_RESOLVE_NO_USER || resolved == SUDO_RESOLVE_DISABLED) {
        return -EACCES;
    }
    if (resolved != SUDO_RESOLVE_OK) {
        return -EINVAL;
    }
    if (require_admin && !target.admin) {
        /* sudo always elevates, so a non-administrator target there is a
         * caller error rather than a privilege path. */
        return -EACCES;
    }
    if (requester_uid != 0 && password && password[0]) {
        if (!verify || verify(verify_context, &target.user, password) != 0) {
            return -EACCES;
        }
        password_verified = 1;
        if (target.admin)
            sudo_cache_grant_session(&authd_sudo_cache, requester_uid, session_id, authd_sudo_now_ms());
    }
    cache_valid = sudo_cache_valid_session(&authd_sudo_cache, requester_uid, session_id,
                                   authd_sudo_now_ms());
    if (sudo_authorize(requester_uid, &target, password_verified, cache_valid) !=
        SUDO_AUTH_ALLOW) {
        return -EACCES;
    }
    if (out_user) {
        *out_user = target.user;
    }
    return 0;
}

int authd_sudo_cache_revoke_all(void)
{
    sudo_cache_revoke_all(&authd_sudo_cache);
    return 0;
}

/* Production reply channel: the daemon's Unix-IPC wrapper over the accepted
 * socket. The host test substitutes a recorder here. */
int authd_sudo_channel_send(void *context, uint32_t type, const void *payload,
                            uint32_t length)
{
    int fd = (int)(intptr_t)context;
    return leonos_ipc_send(fd, type, payload, length);
}

int authd_sudo_channel_send_fd(void *context, uint32_t type, const void *payload,
                             uint32_t length, int fd)
{
    return leonos_ipc_send_fd((int)(intptr_t)context, type, payload, length, fd);
}

/* Production password check: the account database record whose hash is
 * compared. authd supplies its own record table as the context. */
int authd_sudo_verify(void *context, const struct leonos_user_info *user,
                      const char *password)
{
    const struct authd_sudo_records *table =
        (const struct authd_sudo_records *)context;
    if (!table || !table->records || !user || !password) {
        return -1;
    }
    for (uint32_t i = 0; i < table->count; ++i) {
        if (table->records[i].user.uid == user->uid &&
            strcmp(table->records[i].user.username, user->username) == 0) {
            return authd_check_password(&table->records[i], password) == 1 ? 0 : -1;
        }
    }
    return -1;
}

int authd_sudo_run_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                             uint32_t length, int stdio_fd,
                             const struct authd_sudo_channel *channel,
                             const struct authd_sudo_records *records,
                             authd_spawn_fn spawn, authd_verify_fn verify,
                             void *spawn_context, void *verify_context)
{
    struct leonos_authd_run request;
    struct leonos_authd_run_ack ack = {.code = -EINVAL, .child_pid = 0};
    struct leonos_user_info user;
    char *argv[LEONOS_AUTHD_RUN_MAX_ARGS + 1];
    uint32_t pid = 0;
    int result;
    int failed = 0;

    if (!channel || !channel->send) {
        if (stdio_fd >= 0) close(stdio_fd);
        return -1;
    }
    if (length != sizeof(request)) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -EINVAL);
        goto done;
    }
    memcpy(&request, buffer, sizeof(request));
    request.password[LEONOS_AUTH_PASSWORD_LEN - 1] = 0;
    request.username[LEONOS_AUTH_USERNAME_LEN - 1] = 0;
    for (uint32_t i = 0; i < LEONOS_AUTHD_RUN_MAX_ARGS; ++i) {
        request.argv[i][LEONOS_AUTHD_RUN_ARG_LEN - 1] = 0;
    }
    if (request.argc == 0 || request.argc > LEONOS_AUTHD_RUN_MAX_ARGS ||
        !request.argv[0][0]) {
        memset(request.password, 0, sizeof(request.password));
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -EINVAL);
        goto done;
    }
    if (request.flags & ~(LEONOS_AUTHD_RUN_REQUIRE_ADMIN | LEONOS_AUTHD_RUN_LOGIN)) {
        /* Unknown operation flags are refused rather than ignored. */
        memset(request.password, 0, sizeof(request.password));
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -EINVAL);
        goto done;
    }
    result = authd_sudo_authorize(request.username, request.password,
                                  requester_uid,
                                  channel->session_id,
                                  (request.flags & LEONOS_AUTHD_RUN_REQUIRE_ADMIN) ? 1u : 0u,
                                  records, verify, verify_context, &user);
    memset(request.password, 0, sizeof(request.password));
    if (result < 0) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, result);
        goto done;
    }
    for (uint32_t i = 0; i < request.argc; ++i) {
        argv[i] = request.argv[i];
    }
    argv[request.argc] = 0;
    if (!spawn) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -ENOSYS);
        goto done;
    }
    struct authd_sudo_slot *slot = authd_sudo_alloc_slot(0, requester_uid);
    if (!slot) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -ENOSPC);
        goto done;
    }
    request.cwd[sizeof(request.cwd) - 1] = 0;
    request.term[sizeof(request.term) - 1] = 0;
    if (spawn == authd_sudo_spawn && spawn_context)
        ((struct authd_run_context *)spawn_context)->request = &request;
    if (spawn(spawn_context, argv[0], argv, user.home, user.username,
              user.uid, stdio_fd, &pid) < 0) {
        memset(slot, 0, sizeof(*slot));
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_RUN, -(errno ? errno : EIO));
        /* A daemon that cannot spawn is broken, not merely unhappy with this
         * request, so the peer is dropped rather than left waiting. */
        failed = 1;
        goto done;
    }
    slot->child_pid = pid;
    slot->owner_pid = channel->owner_pid;
    ack.code = 0;
    ack.child_pid = pid;
    (void)channel->send(channel->context, LEONOS_AUTHD_MSG_RUN, &ack, sizeof(ack));
done:
    /* The child duplicated this descriptor into its own stdio, so the daemon
     * never keeps a stray copy of a caller's terminal. */
    if (stdio_fd >= 0) close(stdio_fd);
    return failed ? -1 : 0;
}

int authd_sudo_wait_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                              uint32_t length,
                              const struct authd_sudo_channel *channel)
{
    struct leonos_authd_wait request;
    struct leonos_authd_wait_ack ack = {.code = -ESRCH, .status = 0};
    struct authd_sudo_slot *slot;
    uint32_t deadline;

    if (length < sizeof(request)) {
        authd_sudo_send_ack(channel, -EINVAL);
        return 0;
    }
    memcpy(&request, buffer, sizeof(request));
    slot = authd_sudo_find_slot(request.child_pid);
    /* Only the process that requested the command may collect its status. */
    if (!slot || slot->owner_uid != requester_uid || slot->owner_pid != channel->owner_pid) {
        (void)channel->send(channel->context, LEONOS_AUTHD_MSG_WAIT, &ack, sizeof(ack));
        return -ESRCH;
    }
    deadline = authd_sudo_now_ms() + AUTHD_SUDO_WAIT_MS;
    while (!slot->reaped &&
           (int32_t)(authd_sudo_now_ms() - deadline) < 0) {
        (void)poll(0, 0, 2);
        authd_sudo_reap();
        slot = authd_sudo_find_slot(request.child_pid);
        if (!slot) {
            break;
        }
    }
    if (!slot) {
        ack.code = -ESRCH;
    } else if (slot->reaped) {
        ack.code = 0;
        ack.status = slot->status;
    } else {
        ack.code = -EAGAIN;
    }
    int sent;
    int result_fd = -1;
    if (slot && ack.code == 0 && slot->result_path_used) {
        result_fd = open(slot->result_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (result_fd < 0) ack.code = -errno;
        else if (!channel->send_fd) ack.code = -ENOSYS;
    }
    if (result_fd >= 0 && ack.code == 0)
        sent = channel->send_fd(channel->context, LEONOS_AUTHD_MSG_WAIT, &ack, sizeof(ack), result_fd);
    else sent = channel->send(channel->context, LEONOS_AUTHD_MSG_WAIT, &ack, sizeof(ack));
    if (result_fd >= 0) close(result_fd);
    if (slot && slot->reaped && sent == 0) {
        /* The requester owns the status now, so the slot is finished: holding
         * it for the linger window would let 16 commands exhaust the table and
         * make the next elevation fail with ENOSPC. The linger timeout stays as
         * the safety net for a client that disappears without collecting. */
        if (slot->result_path_used) {
            (void)unlink(slot->result_path);
        }
        memset(slot, 0, sizeof(*slot));
    }
    return ack.code;
}

int authd_sudo_kill_from_peer(uint32_t requester_uid,
                              const struct authd_sudo_channel *channel)
{
    sudo_cache_revoke(&authd_sudo_cache, requester_uid);
    authd_sudo_send_ack(channel, 0);
    return 0;
}

int authd_sudo_signal_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                               uint32_t length, const struct authd_sudo_channel *channel)
{
    struct leonos_authd_run_signal request;
    if (length != sizeof(request)) { authd_sudo_send_ack(channel, -EINVAL); return 0; }
    memcpy(&request, buffer, sizeof(request));
    struct authd_sudo_slot *slot = authd_sudo_find_slot(request.child_pid);
    if (!slot || slot->reaped || slot->owner_uid != requester_uid ||
        slot->owner_pid != channel->owner_pid) {
        authd_sudo_send_ack(channel, -ESRCH);
        return 0;
    }
    if (request.signal_number != SIGINT && request.signal_number != SIGQUIT &&
        request.signal_number != SIGTERM && request.signal_number != SIGHUP &&
        request.signal_number != SIGKILL && request.signal_number != SIGCONT &&
        request.signal_number != SIGTSTP) {
        authd_sudo_send_ack(channel, -EINVAL);
        return 0;
    }
    int result = kill((pid_t)slot->child_pid, (int)request.signal_number);
    authd_sudo_send_ack(channel, result < 0 ? -errno : 0);
    return 0;
}

int authd_sudo_check_from_peer(uint32_t requester_uid,
                               const struct authd_sudo_channel *channel)
{
    int valid = requester_uid == 0 || sudo_cache_valid_session(&authd_sudo_cache, requester_uid,
                                 channel->session_id,
                                 authd_sudo_now_ms());
    authd_sudo_send_ack(channel, valid ? 1 : 0);
    return 0;
}

int authd_sudo_verify_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                                uint32_t length,
                                const struct authd_sudo_channel *channel,
                                const struct authd_sudo_records *records,
                                authd_verify_fn verify, void *verify_context)
{
    struct leonos_authd_verify request;
    struct leonos_user_info user;
    int result;

    if (length < sizeof(request)) {
        authd_sudo_send_ack(channel, -EINVAL);
        return 0;
    }
    memcpy(&request, buffer, sizeof(request));
    request.username[LEONOS_AUTH_USERNAME_LEN - 1] = 0;
    request.password[LEONOS_AUTH_PASSWORD_LEN - 1] = 0;
    /* Always an elevation, so the target must be an administrator and the
     * password must be given: there is nothing to verify otherwise. */
    if (!request.password[0]) {
        memset(request.password, 0, sizeof(request.password));
        authd_sudo_send_ack(channel, -EACCES);
        return 0;
    }
    result = authd_sudo_authorize(request.username, request.password,
                                  requester_uid, channel->session_id, 1, records, verify,
                                  verify_context, &user);
    memset(request.password, 0, sizeof(request.password));
    authd_sudo_send_ack(channel, result < 0 ? result : 1);
    return 0;
}

int authd_sudo_fileop_from_peer(uint32_t requester_uid, const uint8_t *buffer,
                               uint32_t length,
                               const struct authd_sudo_channel *channel,
                               const struct authd_sudo_records *records,
                               authd_spawn_fn spawn, authd_verify_fn verify,
                               void *spawn_context, void *verify_context)
{
    struct leonos_authd_fileop request;
    struct leonos_authd_fileop_ack ack = {.code = -EINVAL, .child_pid = 0};
    struct leonos_user_info user;
    struct authd_sudo_slot *slot;
    char result_path[LEONOS_FS_PATH_LEN];
    char op_text[16];
    char *argv[10];
    uint32_t pid = 0;
    int result;

    if (length < sizeof(request)) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -EINVAL);
        return 0;
    }
    memcpy(&request, buffer, sizeof(request));
    request.username[LEONOS_AUTH_USERNAME_LEN - 1] = 0;
    request.password[LEONOS_AUTH_PASSWORD_LEN - 1] = 0;
    request.path1[LEONOS_FS_PATH_LEN - 1] = 0;
    request.path2[LEONOS_FS_PATH_LEN - 1] = 0;
    /* Validate before spending a password check on a request that can never be
     * served, and refuse the whole thing rather than silently sanitizing it. */
    if (sudo_fileop_check(request.op, request.path1,
                          request.path2[0] ? request.path2 : 0) < 0) {
        memset(request.password, 0, sizeof(request.password));
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -EINVAL);
        return 0;
    }
    result = authd_sudo_authorize(request.username, request.password,
                                  requester_uid, channel->session_id, 1, records, verify,
                                  verify_context, &user);
    memset(request.password, 0, sizeof(request.password));
    if (result < 0) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, result);
        return result;
    }
    if (!spawn) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -ENOSYS);
        return -ENOSYS;
    }
    struct stat directory;
    if ((mkdir(AUTHD_FILEOP_DIR, 0700) < 0 && errno != EEXIST) ||
        lstat(AUTHD_FILEOP_DIR, &directory) < 0 || !S_ISDIR(directory.st_mode) ||
        directory.st_uid != 0 || (directory.st_mode & 0777) != 0700) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -EACCES);
        return 0;
    }
    uint32_t seq = authd_fileop_seq++;
    /* The path is authd-chosen, never client-supplied: a client cannot make
     * the worker write outside the private result directory. */
    if (snprintf(result_path, sizeof(result_path), "%s/%u-%u.bin",
                 AUTHD_FILEOP_DIR, (unsigned)requester_uid,
                 (unsigned)seq) <= 0 ||
        snprintf(op_text, sizeof(op_text), "%u", (unsigned)request.op) <= 0) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -EIO);
        return -EIO;
    }
    (void)unlink(result_path);
    argv[0] = (char *)AUTHD_SUDOD_PATH;
    argv[1] = (char *)"--op";
    argv[2] = op_text;
    argv[3] = (char *)"--result";
    argv[4] = result_path;
    argv[5] = (char *)"--path1";
    argv[6] = request.path1;
    argv[7] = (char *)"--path2";
    argv[8] = request.path2;
    argv[9] = NULL;
    slot = authd_sudo_alloc_slot(0, requester_uid);
    if (!slot) {
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -ENOSPC);
        return 0;
    }
    if (spawn(spawn_context, AUTHD_SUDOD_PATH, argv, user.home, user.username,
              user.uid, -1, &pid) < 0) {
        memset(slot, 0, sizeof(*slot));
        authd_sudo_send_error(channel, LEONOS_AUTHD_MSG_FILEOP, -(errno ? errno : EIO));
        return -1;
    }
    slot->child_pid = pid;
    slot->owner_pid = channel->owner_pid;
    slot->result_path_used = 1;
    memcpy(slot->result_path, result_path, sizeof(slot->result_path));
    ack.code = 0;
    ack.child_pid = pid;
    memcpy(ack.path, result_path, sizeof(ack.path));
    (void)channel->send(channel->context, LEONOS_AUTHD_MSG_FILEOP, &ack, sizeof(ack));
    return 0;
}
