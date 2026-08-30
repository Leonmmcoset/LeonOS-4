/* Shared POSIX process, descriptor, pipe, and job-control wrappers. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/signal.h>
#include <leonos/syscall.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

static _sig_func_ptr leonos_signal_handlers[32];
static __sigset_t leonos_signal_masks[32];
static int leonos_dispatching_signal;

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

int socket(int domain, int type, int protocol)
{
    return syscall_error(syscall3(SYS_socket, domain, type, protocol));
}

int socketpair(int domain, int type, int protocol, int filedes[2])
{
    if (!filedes) {
        errno = EINVAL;
        return -1;
    }
    return syscall_error(syscall6(SYS_socketpair, domain, type, protocol,
                                  (long)filedes, 0, 0));
}

int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    return syscall_error(syscall3(SYS_bind, fd, (long)address, length));
}

int listen(int fd, int backlog)
{
    return syscall_error(syscall2(SYS_listen, fd, backlog));
}

int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    return syscall_error(syscall3(SYS_accept, fd, (long)address, (long)length));
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    return syscall_error(syscall3(SYS_connect, fd, (long)address, length));
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    long result = syscall6(SYS_sendto, fd, (long)buffer, (long)length, flags,
                           (long)address, address_length);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    long result = syscall6(SYS_recvfrom, fd, (long)buffer, (long)length, flags,
                           (long)address, (long)address_length);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    return sendto(fd, buffer, length, flags, 0, 0);
}

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    return recvfrom(fd, buffer, length, flags, 0, 0);
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    long result;
    if (!message) {
        errno = EINVAL;
        return -1;
    }
    result = syscall3(SYS_sendmsg, fd, (long)message, flags);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    long result;
    if (!message) {
        errno = EINVAL;
        return -1;
    }
    result = syscall3(SYS_recvmsg, fd, (long)message, flags);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    if (!iov || iovcnt < 0 || iovcnt > LEONOS_IOV_MAX) {
        errno = EINVAL;
        return -1;
    }
    for (int index = 0; index < iovcnt; ++index) {
        ssize_t result = read(fd, iov[index].iov_base, iov[index].iov_len);
        if (result < 0) return total ? total : -1;
        total += result;
        if ((size_t)result != iov[index].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    if (!iov || iovcnt < 0 || iovcnt > LEONOS_IOV_MAX) {
        errno = EINVAL;
        return -1;
    }
    for (int index = 0; index < iovcnt; ++index) {
        ssize_t result = write(fd, iov[index].iov_base, iov[index].iov_len);
        if (result < 0) return total ? total : -1;
        total += result;
        if ((size_t)result != iov[index].iov_len) break;
    }
    return total;
}

int shutdown(int fd, int how)
{
    return syscall_error(syscall2(SYS_shutdown, fd, how));
}

int getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    return syscall_error(syscall3(SYS_getsockname, fd, (long)address, (long)length));
}

int getpeername(int fd, struct sockaddr *address, socklen_t *length)
{
    (void)fd;
    (void)address;
    (void)length;
    errno = ENOTSUP;
    return -1;
}

int getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
    return syscall_error(syscall6(SYS_getsockopt, fd, level, option,
                                  (long)value, (long)length, 0));
}

int setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
    return syscall_error(syscall6(SYS_setsockopt, fd, level, option,
                                  (long)value, length, 0));
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
    long result = syscall6(SYS_rt_sigprocmask, how, (long)set, (long)old_set,
                           sizeof(__sigset_t), 0, 0);
    return syscall_error(result);
}

int sigsuspend(const __sigset_t *mask)
{
    long result;
    if (!mask) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(SYS_rt_sigsuspend, (long)mask, sizeof(*mask));
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    errno = EINTR;
    return -1;
}

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    struct leonos_rt_sigaction request;
    struct leonos_rt_sigaction old_request;
    long result;
    if (signal_number <= 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    request = (struct leonos_rt_sigaction){0};
    old_request = (struct leonos_rt_sigaction){0};
    if (action) {
        request.handler = (uint64_t)(uintptr_t)action->sa_handler;
        request.mask = (uint64_t)action->sa_mask;
        request.flags = (uint64_t)(uint32_t)action->sa_flags;
    }
    result = syscall6(SYS_rt_sigaction, signal_number,
                      action ? (long)&request : 0,
                      previous ? (long)&old_request : 0,
                      sizeof(__sigset_t), 0, 0);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (previous) {
        *previous = (struct sigaction){
            .sa_handler = (_sig_func_ptr)(uintptr_t)old_request.handler,
            .sa_mask = (__sigset_t)old_request.mask,
            .sa_flags = (int)old_request.flags,
        };
    }
    if (action) {
        leonos_signal_handlers[signal_number] = action->sa_handler;
        leonos_signal_masks[signal_number] = action->sa_mask;
    }
    return 0;
}

void leonos_dispatch_pending_signals(void)
{
    int signal_number;
    if (leonos_dispatching_signal) return;
    leonos_dispatching_signal = 1;
    while ((signal_number = (int)syscall0(SYS_rt_sigreturn)) > 0) {
        _sig_func_ptr handler = leonos_signal_handlers[signal_number];
        if (handler && handler != SIG_IGN && handler != SIG_DFL) {
            __sigset_t old_mask;
            (void)sigprocmask(SIG_BLOCK, &leonos_signal_masks[signal_number], &old_mask);
            handler(signal_number);
            (void)sigprocmask(SIG_SETMASK, &old_mask, 0);
        }
    }
    leonos_dispatching_signal = 0;
}

/* The kernel supports default and ignore dispositions. User callbacks remain
 * unavailable until LeonOS has a user signal-frame ABI. */
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
    int result = kill(getpid(), signal_number);
    leonos_dispatch_pending_signals();
    return result;
}

uid_t getuid(void)
{
    return 0;
}
