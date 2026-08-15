/* Shared POSIX process, descriptor, pipe, and job-control wrappers. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/syscall.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int syscall_error(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

pid_t fork(void)
{
    long result = syscall0(SYS_fork);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
}

pid_t vfork(void)
{
    /* LeonOS deliberately gives vfork fork/COW semantics until a parent
     * suspension ABI exists.  This is safe for callers that expect vfork's
     * child to exec immediately, and never exposes a shared address space. */
    long result = syscall0(SYS_vfork);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
}

int dup(int fd)
{
    return syscall_error(syscall1(SYS_dup, fd));
}

int dup2(int old_fd, int new_fd)
{
    return syscall_error(syscall2(SYS_dup2, old_fd, new_fd));
}

int fcntl(int fd, int command, ...)
{
    va_list args;
    long argument = 0;
    long result;

    va_start(args, command);
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC ||
        command == F_SETFD || command == F_SETFL) {
        argument = va_arg(args, int);
    }
    va_end(args);
    result = syscall3(SYS_fcntl, fd, command, argument);
    return syscall_error(result);
}

int pipe(int filedes[2])
{
    long result;
    if (!filedes) {
        errno = EINVAL;
        return -1;
    }
    result = syscall1(SYS_pipe, (long)filedes);
    return syscall_error(result);
}

void _exit(int code)
{
    (void)syscall1(SYS_exit, code);
    for (;;) {
    }
}

int sched_yield(void)
{
    return syscall_error(syscall0(SYS_sched_yield));
}

pid_t getpid(void)
{
    return (pid_t)syscall0(SYS_getpid);
}

pid_t getppid(void)
{
    return (pid_t)syscall0(SYS_getppid);
}

int kill(pid_t pid, int signal_number)
{
    return syscall_error(syscall2(SYS_kill, (long)pid, signal_number));
}

int nice(int increment)
{
    int result = syscall_error(syscall1(SYS_nice, increment));
    return result < 0 ? -1 : result - 20;
}

int getpriority(int which, id_t who)
{
    int result = syscall_error(syscall2(SYS_getpriority, which, who));
    return result < 0 ? -1 : result - 20;
}

int setpriority(int which, id_t who, int priority)
{
    return syscall_error(syscall3(SYS_setpriority, which, who, priority));
}

int getrlimit(int resource, struct rlimit *limit)
{
    if (!limit) {
        errno = EINVAL;
        return -1;
    }
    return syscall_error(syscall2(SYS_getrlimit, resource, (long)limit));
}

int setrlimit(int resource, const struct rlimit *limit)
{
    if (!limit) {
        errno = EINVAL;
        return -1;
    }
    return syscall_error(syscall2(SYS_setrlimit, resource, (long)limit));
}

int wait4(pid_t pid, int *status, int options, void *rusage)
{
    long result = syscall6(SYS_wait4, (long)pid, (long)status, options,
                           (long)rusage, 0, 0);
    return syscall_error(result);
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    for (;;) {
        int result = wait4(pid, status, options, 0);
        if (result == -1 && errno == EAGAIN && !(options & WNOHANG)) {
            (void)sched_yield();
            continue;
        }
        if (result < 0) {
            return (pid_t)-1;
        }
        return (pid_t)result;
    }
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    long result = syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
    return syscall_error(result);
}

pid_t getpgrp(void)
{
    long result = syscall0(SYS_getpgrp);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
}

pid_t getpgid(pid_t pid)
{
    long result = syscall1(SYS_getpgid, (long)pid);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
}

int setpgid(pid_t pid, pid_t process_group)
{
    return syscall_error(syscall2(SYS_setpgid, (long)pid, (long)process_group));
}

int setpgrp(void)
{
    return setpgid(0, 0);
}

int killpg(pid_t process_group, int signal_number)
{
    if (process_group <= 0) {
        errno = EINVAL;
        return -1;
    }
    return kill(-process_group, signal_number);
}

pid_t setsid(void)
{
    long result = syscall0(SYS_setsid);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
}

pid_t tcgetpgrp(int fd)
{
    int process_group = 0;
    long result = syscall3(SYS_ioctl, fd, LEONOS_PTY_IOCTL_GET_PGRP,
                           (long)&process_group);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)process_group;
}

int tcsetpgrp(int fd, pid_t process_group)
{
    long result;
    if (process_group <= 0) {
        errno = EINVAL;
        return -1;
    }
    result = syscall3(SYS_ioctl, fd, LEONOS_PTY_IOCTL_SET_PGRP,
                      (long)&process_group);
    return syscall_error(result);
}

int sigprocmask(int how, const __sigset_t *set, __sigset_t *old_set)
{
    (void)how;
    (void)set;
    if (old_set) {
        *old_set = 0;
    }
    errno = ENOSYS;
    return -1;
}

int sigsuspend(const __sigset_t *mask)
{
    (void)mask;
    (void)sched_yield();
    errno = EINTR;
    return -1;
}

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    (void)signal_number;
    (void)action;
    if (previous) {
        *previous = (struct sigaction){.sa_handler = SIG_DFL};
    }
    errno = ENOSYS;
    return -1;
}

/* The kernel currently supports signal delivery and default/stop actions,
 * but not per-process user handlers.  Keep the standard entry point in the
 * shared POSIX runtime so applications do not need private signal stubs. */
_sig_func_ptr signal(int signal_number, _sig_func_ptr handler)
{
    struct sigaction action;
    struct sigaction previous;

    action.sa_handler = handler;
    action.sa_flags = 0;
    action.sa_mask = 0;
    if (sigaction(signal_number, &action, &previous) < 0)
        return SIG_ERR;
    return previous.sa_handler;
}

int raise(int signal_number)
{
    (void)signal_number;
    errno = ENOSYS;
    return -1;
}

uid_t getuid(void)
{
    return 0;
}
