#ifndef LEONOS_UAPI_LINUX_TIMES_H
#define LEONOS_UAPI_LINUX_TIMES_H

#include <stdint.h>

struct linux_tms {
    int64_t tms_utime;
    int64_t tms_stime;
    int64_t tms_cutime;
    int64_t tms_cstime;
};

#endif
