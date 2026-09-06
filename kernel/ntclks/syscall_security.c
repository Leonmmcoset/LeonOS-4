/* Security syscall category boundary: only filesystem ACL compatibility
 * remains here; auth and startup policies moved to userspace daemons. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <leonos/auth.h>
#include <leonos/fs.h>

static int security_ioctl(uint32_t command)
{
    switch (command) {
    case LEONOS_FS_IOCTL_ACL_GET:
    case LEONOS_FS_IOCTL_ACL_SET:
    case LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP:
    case LEONOS_FS_IOCTL_ACL_REPAIR:
        return 1;
    default:
        return 0;
    }
}

int syscall_security_owns(uint64_t number, uint64_t a1)
{
    return number == LINUX_SYS_IOCTL && security_ioctl((uint32_t)a1);
}

int64_t syscall_security_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                  uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
