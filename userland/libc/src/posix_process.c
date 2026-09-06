/* Shared POSIX process, descriptor, pipe, and job-control wrappers. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <leonos/gui.h>
#include <leonos/signal.h>
#include <leonos/syscall.h>
#include <linux/utsname.h>
#include <linux/tty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Picolibc's minimal <sched.h> does not define cpu_set_t on this target. */
typedef uint64_t cpu_set_t;

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
    long result = syscall3(SYS_ioctl, fd, TIOCGPGRP,
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
    result = syscall3(SYS_ioctl, fd, TIOCSPGRP,
                      (long)&process_group);
    return syscall_error(result);
}

int sigprocmask(int how, const __sigset_t *set, __sigset_t *old_set)
{
    long result = syscall6(SYS_rt_sigprocmask, how, (long)set, (long)old_set,
                           (long)sizeof(__sigset_t), 0, 0);
    return syscall_error(result);
}

int sigsuspend(const __sigset_t *mask)
{
    long result = syscall6(SYS_rt_sigsuspend, (long)mask,
                           (long)sizeof(__sigset_t), 0, 0, 0, 0);
    return syscall_error(result);
}

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    struct leonos_linux_sigaction request;
    struct leonos_linux_sigaction previous_request;
    long result;
    if (signal_number <= 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    request = (struct leonos_linux_sigaction){0};
    if (action) {
        request.handler = (uintptr_t)action->sa_handler;
        request.mask = action->sa_mask;
        request.flags = (uint32_t)action->sa_flags;
        if (request.handler != 0 && request.handler != (uintptr_t)SIG_IGN) {
            request.restorer = (uintptr_t)&leonos_rt_sigreturn_trampoline;
        }
    }
    result = syscall6(SYS_rt_sigaction, signal_number, action ? (long)&request : 0,
                      previous ? (long)&previous_request : 0,
                      (long)sizeof(__sigset_t), 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (previous) {
        *previous = (struct sigaction){
            .sa_handler = previous_request.handler == 1 ? SIG_IGN
                          : previous_request.handler == 0 ? SIG_DFL
                          : (void (*)(int))(uintptr_t)previous_request.handler,
            .sa_mask = previous_request.mask,
            .sa_flags = (int)previous_request.flags,
        };
    }
    return 0;
}

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
    return kill(getpid(), signal_number);
}

uid_t getuid(void) __attribute__((weak))
{
    return (uid_t)syscall0(SYS_getuid);
}

uid_t geteuid(void) __attribute__((weak))
{
    return (uid_t)syscall0(SYS_geteuid);
}

gid_t getgid(void) __attribute__((weak))
{
    return (gid_t)syscall0(SYS_getgid);
}

gid_t getegid(void) __attribute__((weak))
{
    return (gid_t)syscall0(SYS_getegid);
}

int setuid(uid_t uid) __attribute__((weak))
{
    return syscall_error(syscall1(SYS_setuid, (long)uid));
}

int setgid(gid_t gid) __attribute__((weak))
{
    return syscall_error(syscall1(SYS_setgid, (long)gid));
}

int pipe2(int filedes[2], int flags)
{
    long result;
    if (!filedes) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(SYS_pipe2, (long)filedes, flags);
    return syscall_error(result);
}

int dup3(int old_fd, int new_fd, int flags)
{
    return syscall_error(syscall3(SYS_dup3, old_fd, new_fd, flags));
}

int uname(struct utsname *name) __attribute__((weak))
{
    long result = syscall1(SYS_uname, (long)name);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int reboot(int command) __attribute__((weak))
{
    return syscall_error(syscall1(SYS_reboot, command));
}

int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *set) __attribute__((weak))
{
    long result;
    if (!set || cpusetsize > sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    result = syscall3(SYS_sched_setaffinity, (long)pid, (long)sizeof(uint64_t),
                      (long)set);
    return syscall_error(result);
}

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *set) __attribute__((weak))
{
    long result;
    if (!set || cpusetsize > sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    result = syscall3(SYS_sched_getaffinity, (long)pid, (long)sizeof(uint64_t),
                      (long)set);
    return syscall_error(result);
}
