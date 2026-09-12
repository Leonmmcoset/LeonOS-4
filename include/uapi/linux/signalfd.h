#ifndef LEONOS_UAPI_LINUX_SIGNALFD_H
#define LEONOS_UAPI_LINUX_SIGNALFD_H
#include <stdint.h>
#include <linux/fcntl.h>

#define LINUX_SFD_CLOEXEC LINUX_O_CLOEXEC
#define LINUX_SFD_NONBLOCK LINUX_O_NONBLOCK

struct linux_signalfd_siginfo {
    uint32_t signo;
    int32_t error, code;
    uint32_t pid, uid;
    int32_t fd;
    uint32_t tid, band, overrun, trapno;
    int32_t status, value_int;
    uint64_t value_ptr, utime, stime, address;
    uint16_t address_lsb, padding;
    int32_t syscall;
    uint64_t call_address;
    uint32_t arch;
    uint8_t padding_end[28];
};

_Static_assert(sizeof(struct linux_signalfd_siginfo) == 128, "signalfd record size");
_Static_assert(__builtin_offsetof(struct linux_signalfd_siginfo, value_ptr) == 48, "signalfd value offset");
_Static_assert(__builtin_offsetof(struct linux_signalfd_siginfo, call_address) == 88, "signalfd call address offset");
#endif
