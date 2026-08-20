/* Security and policy syscall category boundary. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/startup.h>

static int security_ioctl(uint32_t command)
{
    switch (command) {
    case LEONOS_AUTH_IOCTL_STATUS:
    case LEONOS_AUTH_IOCTL_CURRENT:
    case LEONOS_AUTH_IOCTL_LIST_USERS:
    case LEONOS_AUTH_IOCTL_LOGIN:
    case LEONOS_AUTH_IOCTL_ELEVATE_ADMIN:
    case LEONOS_AUTH_IOCTL_DELEGATE_ELEVATION:
    case LEONOS_AUTH_IOCTL_LOGOUT:
    case LEONOS_AUTH_IOCTL_CREATE_USER:
    case LEONOS_AUTH_IOCTL_UPDATE_USER:
    case LEONOS_AUTH_IOCTL_CHANGE_PASSWORD:
    case LEONOS_STARTUP_IOCTL_REQUEST:
    case LEONOS_STARTUP_IOCTL_REQUEST_STATUS:
    case LEONOS_STARTUP_IOCTL_DIALOG_GET:
    case LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE:
    case LEONOS_STARTUP_IOCTL_LIST:
    case LEONOS_STARTUP_IOCTL_SET_ENABLED:
    case LEONOS_STARTUP_IOCTL_REMOVE:
    case LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT:
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
