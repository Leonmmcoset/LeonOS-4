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
#define LINUX_SYS_FTRUNCATE 77
#define LINUX_SYS_MMAP 9
#define LINUX_SYS_MUNMAP 11
#define LINUX_SYS_MPROTECT 10
#define LINUX_SYS_IOCTL 16
#define LINUX_SYS_SCHED_YIELD 24
#define LINUX_SYS_DUP 32
#define LINUX_SYS_DUP2 33
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_GETCWD 79
#define LINUX_SYS_CHDIR 80
#define LINUX_SYS_RENAME 82
#define LINUX_SYS_MKDIR 83
#define LINUX_SYS_RMDIR 84
#define LINUX_SYS_UNLINK 87
#define LINUX_SYS_FCNTL 72
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
#define LEONOS_EBADF 9
#define LEONOS_ENOTDIR 20
#define LEONOS_EISDIR 21
#define LEONOS_EMFILE 24
#define LEONOS_E2BIG 7
#define LEONOS_EEXIST 17
#define LEONOS_ENOTEMPTY 39
#define LEONOS_EPERM 1
#define LEONOS_EACCES 13
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11

struct task;

struct syscall_frame {
    uint64_t number;
    uint64_t args[6];
};

void syscall_init(void);
int64_t syscall_dispatch(const struct syscall_frame *frame);
void syscall_dispatch_frame(struct trap_frame *frame);
int syscall_handle_user_page_fault(uint64_t fault_addr, uint64_t error);
void syscall_release_task_files(struct task *task);

#endif
