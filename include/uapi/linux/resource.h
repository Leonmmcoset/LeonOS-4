#ifndef LEONOS_UAPI_LINUX_RESOURCE_H
#define LEONOS_UAPI_LINUX_RESOURCE_H

#include <stdint.h>
#include <linux/time.h>

struct linux_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

#define LINUX_RLIM_INFINITY UINT64_MAX
#define LINUX_RLIMIT_CPU 0
#define LINUX_RLIMIT_FSIZE 1
#define LINUX_RLIMIT_DATA 2
#define LINUX_RLIMIT_STACK 3
#define LINUX_RLIMIT_CORE 4
#define LINUX_RLIMIT_RSS 5
#define LINUX_RLIMIT_NPROC 6
#define LINUX_RLIMIT_NOFILE 7
#define LINUX_RLIMIT_MEMLOCK 8
#define LINUX_RLIMIT_AS 9
#define LINUX_RLIMIT_LOCKS 10
#define LINUX_RLIMIT_SIGPENDING 11
#define LINUX_RLIMIT_MSGQUEUE 12
#define LINUX_RLIMIT_NICE 13
#define LINUX_RLIMIT_RTPRIO 14
#define LINUX_RLIMIT_RTTIME 15
#define LINUX_RLIM_NLIMITS 16

/* Linux x86-64 struct rusage: two timevals followed by fourteen longs. */
struct linux_rusage {
    struct linux_timeval ru_utime;
    struct linux_timeval ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};

#endif
