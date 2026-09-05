/* GUI syscall category boundary.  GUI command handlers are kept behind the
 * stable legacy backend until the protocol-specific code is moved here. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/gpu.h>

int syscall_gui_owns(uint64_t number, uint64_t a1)
{
    uint32_t command = (uint32_t)a1;
    if (number != LINUX_SYS_IOCTL) {
        return 0;
    }
    return (command & 0xffff0000u) == 0x4c470000u ||
           (command & 0xffff0000u) == 0x4c460000u ||
           (command & 0xffff0000u) == 0x4c440000u ||
           (command & 0xffff0000u) == 0x4c410000u ||
           (command & 0xffff0000u) == 0x4c540000u;
}

int64_t syscall_gui_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (number == LINUX_SYS_IOCTL && syscall_gpu_owns(a1)) {
        return syscall_gpu_dispatch(a1, a2);
    }
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
