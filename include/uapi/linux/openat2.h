#ifndef LEONOS_UAPI_LINUX_OPENAT2_H
#define LEONOS_UAPI_LINUX_OPENAT2_H

#include <linux/types.h>

struct open_how {
    __u64 flags;
    __u64 mode;
    __u64 resolve;
};

#define RESOLVE_NO_XDEV       0x01u
#define RESOLVE_NO_MAGICLINKS 0x02u
#define RESOLVE_NO_SYMLINKS   0x04u
#define RESOLVE_BENEATH       0x08u
#define RESOLVE_IN_ROOT       0x10u
#define RESOLVE_CACHED        0x20u

#endif
