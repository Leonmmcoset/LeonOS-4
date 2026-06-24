#ifndef NTCLKS_SYSCALL_H
#define NTCLKS_SYSCALL_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

#define LINUX_SYS_READ 0
#define LINUX_SYS_WRITE 1
#define LINUX_SYS_OPEN 2
#define LINUX_SYS_CLOSE 3
#define LINUX_SYS_STAT 4
#define LINUX_SYS_FSTAT 5
#define LINUX_SYS_LSEEK 8
#define LINUX_SYS_MMAP 9
#define LINUX_SYS_MUNMAP 11
#define LINUX_SYS_IOCTL 16
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_GETCWD 79
#define LINUX_SYS_CHDIR 80
#define LINUX_SYS_NANOSLEEP 35
#define LINUX_SYS_EXECVE 59
#define LINUX_SYS_EXIT 60
#define LINUX_SYS_WAIT4 61

#define LEONOS_ENOSYS 38
#define LEONOS_EFAULT 14
#define LEONOS_EINVAL 22
#define LEONOS_ECHILD 10
#define LEONOS_ENOENT 2
#define LEONOS_ENOMEM 12

struct syscall_frame {
    uint64_t number;
    uint64_t args[6];
};

void syscall_init(void);
int64_t syscall_dispatch(const struct syscall_frame *frame);
void syscall_dispatch_frame(struct trap_frame *frame);

#endif
