#ifndef LEONOS_UAPI_LINUX_RESOURCE_H
#define LEONOS_UAPI_LINUX_RESOURCE_H

#include <stdint.h>

/* Linux v6.12 native x86-64: asm-generic/resource.h and linux/resource.h. */
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
#define LINUX_RLIM_INFINITY UINT64_MAX

struct linux_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

#endif
