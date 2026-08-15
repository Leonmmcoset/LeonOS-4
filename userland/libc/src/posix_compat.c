/* Compatibility entry points needed by the Picolibc shared runtime.
 * LeonOS provides fork/exec, process groups and default signal actions;
 * application-defined signal handlers and TLS remain unsupported. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <fcntl.h>
#include <leonos/gui.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
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

int usleep(useconds_t microseconds)
{
    unsigned long milliseconds =
        ((unsigned long)microseconds + 999UL) / 1000UL;
    int result = sleep_ms(milliseconds);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}
