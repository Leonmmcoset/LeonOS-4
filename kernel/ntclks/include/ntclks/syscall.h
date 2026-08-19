/*
 * LeonOS syscall interface: declares syscall dispatch and user ABI helpers.
 * Defines syscall frames, numbers, and kernel entry points for Ring 3.
 */
#ifndef NTCLKS_SYSCALL_H
#define NTCLKS_SYSCALL_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

#define LINUX_SYS_READ 0
#define LINUX_SYS_WRITE 1
#define LINUX_SYS_OPEN 2
#define LINUX_SYS_CLOSE 3
#define LINUX_SYS_PIPE 22
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
#define LINUX_SYS_FORK 57
#define LINUX_SYS_VFORK 58
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_SETPGID 109
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
#define LINUX_SYS_KILL 62
#define LINUX_SYS_GETPPID 110
#define LINUX_SYS_GETPGRP 111
#define LINUX_SYS_SETSID 112
#define LINUX_SYS_GETPGID 121
#define LINUX_SYS_NICE 34
#define LINUX_SYS_GETPRIORITY 140
#define LINUX_SYS_SETPRIORITY 141
#define LINUX_SYS_GETRLIMIT 97
#define LINUX_SYS_SETRLIMIT 160

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
#define LEONOS_EBUSY 16
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11
#define LEONOS_EPIPE 32
#define LEONOS_ENOTTY 25

struct task;

struct syscall_frame {
    uint64_t number;
    uint64_t args[6];
};

/**
 * @brief Coordinates the syscall init operation.
 */
void syscall_init(void);
/**
 * @brief Dispatches process identity, groups, signals, priority, and limits.
 */
int64_t syscall_process_control(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3);
/**
 * @brief Coordinates the syscall dispatch operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
 */
int64_t syscall_dispatch(const struct syscall_frame *frame);
/**
 * @brief Coordinates the syscall dispatch frame operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 */
void syscall_dispatch_frame(struct trap_frame *frame);
/**
 * @brief Coordinates the syscall handle user page fault operation.
 * @param fault_addr Address used by this operation; its address-space interpretation follows the API.
 * @param error Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int syscall_handle_user_page_fault(uint64_t fault_addr, uint64_t error);
int64_t syscall_mm_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset);
int64_t syscall_mm_mprotect(uint64_t addr, uint64_t len, uint64_t prot);
int64_t syscall_mm_munmap(uint64_t addr, uint64_t len);
/**
 * @brief Coordinates the syscall release task files operation.
 * @param task Task whose state or authority is inspected or updated.
 */
void syscall_release_task_files(struct task *task);
/**
 * @brief Retains shared descriptor backing objects after a task-table fork copy.
 * @param parent Source task whose descriptor entries were copied.
 * @param child Fork child containing the copied descriptor entries.
 * @return Zero on success or a negative errno-style failure.
 */
int syscall_clone_task_files(const struct task *parent, struct task *child);
/**
 * @brief Closes all explicitly marked close-on-exec descriptors.
 * @param task Process replacing its image through execve.
 */
void syscall_close_cloexec_files(struct task *task);
int syscall_inherit_task_fds(struct task *parent, struct task *child,
                             int stdin_fd, int stdout_fd, int stderr_fd);

#endif
