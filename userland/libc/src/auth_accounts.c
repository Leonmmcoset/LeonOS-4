#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <leonos/auth.h>
#include <leonos/sudo.h>
#include <pwd.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../auth/standard_accounts.h"

int leonos_auth_current(struct leonos_user_info *user)
{
    return leonos_account_info(getpwuid(geteuid()), user);
}

int leonos_auth_list_users(struct leonos_user_info *users, uint32_t capacity,
                           uint32_t include_disabled, uint32_t *out_count)
{
    if (!out_count || (capacity && !users)) { errno = EINVAL; return -1; }
    uint32_t count = 0;
    int error = 0;
    setpwent();
    for (;;) {
        errno = 0;
        struct passwd *account = getpwent();
        if (!account) { error = errno; break; }
        if ((account->pw_uid && account->pw_uid < 1000) || account->pw_uid == 65534) continue;
        struct leonos_user_info item;
        if (leonos_account_info(account, &item) < 0) { error = errno; break; }
        if (!include_disabled && (item.flags & LEONOS_AUTH_USER_DISABLED)) continue;
        if (count == UINT32_MAX) { error = EOVERFLOW; break; }
        if (count < capacity) users[count] = item;
        ++count;
    }
    endpwent();
    *out_count = count;
    errno = error;
    return error ? -1 : 0;
}

int leonos_auth_status(struct leonos_auth_status *status)
{
    if (!status) { errno = EINVAL; return -1; }
    *status = (struct leonos_auth_status){0};
    struct leonos_user_info root;
    if (leonos_auth_list_users(NULL, 0, 1, &status->user_count) < 0) return -1;
    if (!leonos_account_info(getpwuid(0), &root))
        status->has_admin = !(root.flags & LEONOS_AUTH_USER_DISABLED);
    return 0;
}

int leonos_auth_users_alloc(struct leonos_user_info **users, uint32_t include_disabled,
                            uint32_t *out_count)
{
    if (!users || !out_count) { errno = EINVAL; return -1; }
    for (unsigned retry = 0; retry < 8; ++retry) {
        uint32_t count, actual;
        if (leonos_auth_list_users(NULL, 0, include_disabled, &count) < 0) return -1;
        struct leonos_user_info *next = calloc(count ? count : 1, sizeof(*next));
        if (!next) return -1;
        if (leonos_auth_list_users(next, count, include_disabled, &actual) < 0) { free(next); return -1; }
        if (actual > count) { free(next); continue; }
        free(*users); *users = next; *out_count = actual;
        return 0;
    }
    errno = EAGAIN;
    return -1;
}

static int account_command(char *const args[], const char *input)
{
    if (geteuid() != 0) { errno = EPERM; return -1; }
    int pair[2] = {-1, -1};
    if (input && pipe2(pair, O_CLOEXEC) < 0) return -1;
    pid_t pid = fork();
    if (!pid) {
        if (input && (dup2(pair[0], STDIN_FILENO) < 0 ||
                      fcntl(STDIN_FILENO, F_SETFD, 0) < 0)) _exit(126);
        if (syscall(SYS_close_range, 3u, ~0u, 0u) < 0 || clearenv() < 0 ||
            setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) < 0) _exit(126);
        execv(args[0], args);
        _exit(127);
    }
    int error = pid < 0 ? errno : 0;
    if (input) {
        close(pair[0]);
        /* A single bounded write cannot fill this pipe. Block SIGPIPE locally. */
        sigset_t block, before;
        sigemptyset(&block); sigaddset(&block, SIGPIPE);
        if (sigprocmask(SIG_BLOCK, &block, &before) < 0) error = errno;
        else {
            size_t sent = 0, length = strlen(input);
            while (!error && sent < length) {
                ssize_t n = write(pair[1], input + sent, length - sent);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { error = n < 0 ? errno : EIO; break; }
                sent += (size_t)n;
            }
            if (error == EPIPE) {
                struct timespec zero = {0};
                (void)sigtimedwait(&block, NULL, &zero);
            }
            sigprocmask(SIG_SETMASK, &before, NULL);
        }
        close(pair[1]);
    }
    if (pid > 0) {
        int status;
        pid_t waited;
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0) error = errno;
        else if (!WIFEXITED(status) || WEXITSTATUS(status)) error = EIO;
    }
    errno = error;
    return error ? -1 : 0;
}

int leonos_auth_create_user(const char *name, const char *password,
                            uint32_t role, struct leonos_user_info *user)
{
    if (geteuid() != 0) { errno = EPERM; return -1; }
    if (!user || role != LEONOS_AUTH_ROLE_USER ||
        !leonos_account_name_valid(name, LEONOS_AUTH_USERNAME_LEN) ||
        !leonos_auth_password_valid(password, LEONOS_AUTH_PASSWORD_LEN)) { errno = EINVAL; return -1; }
    char *args[] = {"/usr/sbin/useradd", "-m", "-U", "-s", "/bin/sh",
                    "-e", "1970-01-02", "--", (char *)name, NULL};
    if (account_command(args, NULL) < 0) return -1;
    char *record = NULL;
    if (asprintf(&record, "%s:%s\n", name, password) < 0) return -1;
    char *change[] = {"/usr/sbin/chpasswd", NULL};
    int result = account_command(change, record), error = errno;
    explicit_bzero(record, strlen(record)); free(record);
    /* Keep the account expired until its password has been installed. */
    if (result < 0) { errno = error; return -1; }
    char *enable[] = {"/usr/sbin/usermod", "-e", "", "--", (char *)name, NULL};
    if (account_command(enable, NULL) < 0) return -1;
    return leonos_account_info(getpwnam(name), user);
}

int leonos_auth_update_user(uint32_t uid, uint32_t mask, uint32_t role, uint32_t flags)
{
    (void)role;
    if (geteuid() != 0) { errno = EPERM; return -1; }
    if (!uid || mask != LEONOS_AUTH_UPDATE_FLAGS || flags & ~LEONOS_AUTH_USER_DISABLED) {
        errno = EINVAL; return -1;
    }
    struct passwd *account = getpwuid(uid);
    if (!account) { errno = ENOENT; return -1; }
    char *args[] = {"/usr/sbin/usermod", flags ? "-L" : "-U", "-e",
                    flags ? "1970-01-02" : "", "--", account->pw_name, NULL};
    return account_command(args, NULL);
}

int leonos_auth_request_power(uint32_t command)
{
    if (command != RB_AUTOBOOT && command != RB_POWER_OFF) { errno = EINVAL; return -1; }
    if (geteuid() == 0) return reboot(command);
    char *args[] = {command == RB_AUTOBOOT ? "/sbin/reboot" : "/sbin/poweroff", NULL};
    uint32_t pid;
    int status;
    if (leonos_sudo_run(NULL, NULL, args, &pid) < 0 || leonos_sudo_wait_command(pid, &status) < 0) return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status)) { errno = EACCES; return -1; }
    return 0;
}
