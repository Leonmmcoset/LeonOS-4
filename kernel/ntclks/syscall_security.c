/* Security syscall category boundary. Auth/startup/ACL ioctl policies have
 * moved to userspace daemons and chmod/chown; nothing is owned here. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>

int syscall_security_owns(uint64_t number, uint64_t a1)
{
    (void)number;
    (void)a1;
    return 0;
}

int64_t syscall_security_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                  uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -LEONOS_ENOSYS;
}
