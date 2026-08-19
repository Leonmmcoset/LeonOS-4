/* Device syscall category boundary for non-GUI ioctl operations. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>

int syscall_device_owns(uint64_t number, uint64_t a1)
{
    (void)a1;
    return number == LINUX_SYS_IOCTL;
}

int64_t syscall_device_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
