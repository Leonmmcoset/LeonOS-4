#ifndef LEONOS_UAPI_LINUX_TIME_H
#define LEONOS_UAPI_LINUX_TIME_H
#include <stdint.h>

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_MONOTONIC_COARSE 6
#define LINUX_CLOCK_BOOTTIME 7
#define LINUX_TIMER_ABSTIME 1
#define LINUX_ITIMER_REAL 0
#define LINUX_ITIMER_VIRTUAL 1
#define LINUX_ITIMER_PROF 2

struct linux_timespec { int64_t tv_sec, tv_nsec; };
struct linux_timeval { int64_t tv_sec, tv_usec; };
struct linux_itimerval { struct linux_timeval it_interval, it_value; };
struct linux_itimerspec { struct linux_timespec it_interval, it_value; };
#endif
