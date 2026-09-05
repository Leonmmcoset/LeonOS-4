/*
 * LeonOS syscall interface: declares syscall dispatch and user ABI helpers.
 * Defines syscall frames, numbers, and kernel entry points for Ring 3.
 */
#ifndef NTCLKS_SYSCALL_H
#define NTCLKS_SYSCALL_H

#include <ntclks/trap.h>
#include <ntclks/types.h>
#include <linux/syscall.h>
#include <linux/poll.h>

#define LINUX_SYS_READ __NR_read
#define LINUX_SYS_WRITE __NR_write
#define LINUX_SYS_OPEN __NR_open
#define LINUX_SYS_CLOSE __NR_close
#define LINUX_SYS_PIPE __NR_pipe
#define LINUX_SYS_SEND __NR_send
#define LINUX_SYS_RECV __NR_recv
#define LINUX_SYS_SOCKET __NR_socket
#define LINUX_SYS_CONNECT __NR_connect
#define LINUX_SYS_ACCEPT __NR_accept
#define LINUX_SYS_BIND __NR_bind
#define LINUX_SYS_LISTEN __NR_listen
#define LINUX_SYS_GETSOCKNAME __NR_getsockname
#define LINUX_SYS_GETSOCKOPT __NR_getsockopt
#define LINUX_SYS_SETSOCKOPT __NR_setsockopt
#define LINUX_SYS_SHUTDOWN __NR_shutdown
#define LINUX_SYS_SENDTO __NR_sendto
#define LINUX_SYS_RECVFROM __NR_recvfrom
#define LINUX_SYS_STAT __NR_stat
#define LINUX_SYS_FSTAT __NR_fstat
#define LINUX_SYS_LSEEK __NR_lseek
#define LINUX_SYS_FTRUNCATE __NR_ftruncate
#define LINUX_SYS_MMAP __NR_mmap
#define LINUX_SYS_MUNMAP __NR_munmap
#define LINUX_SYS_MPROTECT __NR_mprotect
#define LINUX_SYS_IOCTL __NR_ioctl
#define LINUX_SYS_POLL __NR_poll
#define LINUX_SYS_SCHED_YIELD __NR_sched_yield
#define LINUX_SYS_DUP __NR_dup
#define LINUX_SYS_DUP2 __NR_dup2
#define LINUX_SYS_FORK __NR_fork
#define LINUX_SYS_VFORK __NR_vfork
#define LINUX_SYS_GETPID __NR_getpid
#define LINUX_SYS_SETPGID __NR_setpgid
#define LINUX_SYS_GETCWD __NR_getcwd
#define LINUX_SYS_CHDIR __NR_chdir
#define LINUX_SYS_RENAME __NR_rename
#define LINUX_SYS_MKDIR __NR_mkdir
#define LINUX_SYS_RMDIR __NR_rmdir
#define LINUX_SYS_UNLINK __NR_unlink
#define LINUX_SYS_FCNTL __NR_fcntl
#define LINUX_SYS_NANOSLEEP __NR_nanosleep
#define LINUX_SYS_EXECVE __NR_execve
#define LINUX_SYS_EXIT __NR_exit
#define LINUX_SYS_WAIT4 __NR_wait4
#define LINUX_SYS_KILL __NR_kill
#define LINUX_SYS_GETPPID __NR_getppid
#define LINUX_SYS_GETPGRP __NR_getpgrp
#define LINUX_SYS_SETSID __NR_setsid
#define LINUX_SYS_GETPGID __NR_getpgid
#define LINUX_SYS_NICE 34
#define LINUX_SYS_GETPRIORITY __NR_getpriority
#define LINUX_SYS_SETPRIORITY __NR_setpriority
#define LINUX_SYS_GETRLIMIT __NR_getrlimit
#define LINUX_SYS_SETRLIMIT __NR_setrlimit
#define LINUX_SYS_OPENAT __NR_openat
#define LINUX_SYS_CLOCK_GETTIME __NR_clock_gettime
#define LINUX_SYS_RT_SIGACTION __NR_rt_sigaction
#define LINUX_SYS_RT_SIGPROCMASK __NR_rt_sigprocmask
#define LINUX_SYS_RT_SIGRETURN __NR_rt_sigreturn
#define LINUX_SYS_RT_SIGSUSPEND __NR_rt_sigsuspend
#define LINUX_SYS_MOUNT __NR_mount
#define LINUX_SYS_UMOUNT2 __NR_umount2

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
#define LEONOS_ENODEV 19
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11
#define LEONOS_EPIPE 32
#define LEONOS_ENOTTY 25
#define LEONOS_ENOSPC 28
#define LEONOS_ENOTSUP 95
#define LEONOS_EADDRINUSE 98
#define LEONOS_EISCONN 106
#define LEONOS_EINTR 4

struct task;

struct syscall_frame {
    uint64_t number;
    uint64_t args[6];
};

/**
 * @brief Set up syscall dispatch tables and register the kernel entry handlers.
 */
void syscall_init(void);
/**
 * @brief Dispatches process identity, groups, signals, priority, and limits.
 */
int64_t syscall_process_control(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3);
/**
 * @brief Execute the syscall described by frame and return its result.
 */
int64_t syscall_dispatch(const struct syscall_frame *frame);
/**
 * @brief Run the syscall encoded in the trap frame and update its return registers.
 */
void syscall_dispatch_frame(struct trap_frame *frame);
/**
 * @brief Resolve a user page fault at fault_addr with the given error code; 0 if handled.
 */
int syscall_handle_user_page_fault(uint64_t fault_addr, uint64_t error);
int64_t syscall_poll(uint64_t fds_ptr, uint64_t count, int64_t timeout_ms);
int64_t syscall_linux_signal(uint64_t number, uint64_t signal_number,
                             uint64_t action_ptr, uint64_t old_action_ptr,
                             uint64_t mask_ptr, uint64_t sigset_size);
int64_t syscall_mm_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset);
int64_t syscall_mm_mprotect(uint64_t addr, uint64_t len, uint64_t prot);
int64_t syscall_mm_munmap(uint64_t addr, uint64_t len);
/**
 * @brief Close and free every file descriptor still open in task.
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
