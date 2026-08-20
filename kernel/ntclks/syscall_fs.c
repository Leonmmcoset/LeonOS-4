/* File-system syscall category boundary.  The legacy backend remains shared
 * while individual handlers are moved here incrementally. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>

int syscall_fs_owns(uint64_t number)
{
    switch (number) {
    case LINUX_SYS_READ:
    case LINUX_SYS_WRITE:
    case LINUX_SYS_OPEN:
    case LINUX_SYS_CLOSE:
    case LINUX_SYS_STAT:
    case LINUX_SYS_FSTAT:
    case LINUX_SYS_LSEEK:
    case LINUX_SYS_FTRUNCATE:
    case LINUX_SYS_GETCWD:
    case LINUX_SYS_CHDIR:
    case LINUX_SYS_RENAME:
    case LINUX_SYS_MKDIR:
    case LINUX_SYS_RMDIR:
    case LINUX_SYS_UNLINK:
    case LINUX_SYS_FCNTL:
        return 1;
    default:
        return 0;
    }
}

int64_t syscall_fs_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                            uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
