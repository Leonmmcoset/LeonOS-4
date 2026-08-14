/* Compatibility entry points needed by the Picolibc shared runtime.
 * LeonOS provides fork/exec, process groups and default signal actions;
 * application-defined signal handlers and TLS remain unsupported. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int unsupported(void)
{
    errno = ENOSYS;
    return -1;
}

int getentropy(void *buffer, size_t length)
{
    (void)buffer;
    (void)length;
    return unsupported();
}

int sigprocmask(int how, const __sigset_t *set, __sigset_t *old_set)
{
    (void)how;
    (void)set;
    if (old_set) {
        *old_set = 0;
    }
    return unsupported();
}

int sigsuspend(const __sigset_t *mask)
{
    /* LeonOS currently applies terminal/default signal actions in the
     * scheduler rather than entering user-installed handlers.  Still yield
     * here so Hush's wait-for-job loop remains interruptible and does not
     * busy-spin while it waits for a child state transition. */
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
    return unsupported();
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
    /* The kernel deliberately provides fork-equivalent COW semantics until
     * a safe parent-suspending vfork ABI exists. */
    long result = syscall0(SYS_vfork);
    if (result < 0) {
        errno = (int)-result;
        return (pid_t)-1;
    }
    return (pid_t)result;
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
    long result = syscall2(SYS_setpgid, (long)pid, (long)process_group);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
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
    return kill(-(int)process_group, signal_number);
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
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int dup2(int old_fd, int new_fd)
{
    return (int)syscall2(SYS_dup2, old_fd, new_fd);
}

uid_t getuid(void)
{
    return 0;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    for (;;) {
        int result = wait4((int)pid, status, options, 0);
        if (result == -LEONOS_EAGAIN && (options & 1) == 0) {
            (void)sched_yield();
            continue;
        }
        if (result < 0) {
            errno = -result;
            return (pid_t)-1;
        }
        return (pid_t)result;
    }
}

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (!request || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    if (remaining) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return (int)syscall2(SYS_nanosleep, (long)request, 0);
}

int gettimeofday(struct timeval *value, void *timezone)
{
    struct leonos_time_info info;
    (void)timezone;
    if (!value || leonos_time_info(&info) < 0) {
        errno = EINVAL;
        return -1;
    }
    value->tv_sec = (time_t)info.unix_seconds;
    value->tv_usec = (suseconds_t)((info.uptime_ms % 1000ULL) * 1000ULL);
    return 0;
}

clock_t times(struct tms *buffer)
{
    clock_t ticks = (clock_t)leonos_uptime_ms();
    if (buffer) {
        buffer->tms_utime = ticks;
        buffer->tms_stime = 0;
        buffer->tms_cutime = 0;
        buffer->tms_cstime = 0;
    }
    return ticks;
}

#define LEONOS_CLOCK_REALTIME 1
#define LEONOS_CLOCK_MONOTONIC 4

int clock_gettime(clockid_t clock_id, struct timespec *value)
{
    struct leonos_time_info info;
    uint64_t milliseconds;
    if (!value || (clock_id != LEONOS_CLOCK_REALTIME &&
                   clock_id != LEONOS_CLOCK_MONOTONIC)) {
        errno = EINVAL;
        return -1;
    }
    if (clock_id == LEONOS_CLOCK_REALTIME && leonos_time_info(&info) == 0 && info.valid) {
        value->tv_sec = (time_t)info.unix_seconds;
        value->tv_nsec = (long)((info.uptime_ms % 1000ULL) * 1000000ULL);
        return 0;
    }
    milliseconds = leonos_uptime_ms();
    value->tv_sec = (time_t)(milliseconds / 1000ULL);
    value->tv_nsec = (long)((milliseconds % 1000ULL) * 1000000ULL);
    return 0;
}

int clock_getres(clockid_t clock_id, struct timespec *value)
{
    if (clock_id != LEONOS_CLOCK_REALTIME && clock_id != LEONOS_CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    if (value) {
        value->tv_sec = 0;
        value->tv_nsec = 1000000L;
    }
    return 0;
}
