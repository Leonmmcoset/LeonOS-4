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
#include <linux/fcntl.h>
#include <linux/eventfd.h>
#include <linux/timerfd.h>
#include <linux/epoll.h>
#include <linux/prctl.h>
#include <linux/membarrier.h>
#include <linux/openat2.h>
#include <leonos/syscall_abi.h>

#define LINUX_SYS_READ __NR_read
#define LINUX_SYS_WRITE __NR_write
#define LINUX_SYS_OPEN __NR_open
#define LINUX_SYS_CLOSE __NR_close
#define LINUX_SYS_PIPE __NR_pipe
#define LINUX_SYS_MSGGET __NR_msgget
#define LINUX_SYS_SEMGET __NR_semget
#define LINUX_SYS_SEMOP __NR_semop
#define LINUX_SYS_SEMCTL __NR_semctl
#define LINUX_SYS_SEMTIMEDOP __NR_semtimedop
#define LINUX_SYS_MSGSND __NR_msgsnd
#define LINUX_SYS_MSGRCV __NR_msgrcv
#define LINUX_SYS_MSGCTL __NR_msgctl
#define LINUX_SYS_SEND __NR_sendto
#define LINUX_SYS_RECV __NR_recvfrom
#define LINUX_SYS_SOCKET __NR_socket
#define LINUX_SYS_CONNECT __NR_connect
#define LINUX_SYS_ACCEPT __NR_accept
#define LINUX_SYS_BIND __NR_bind
#define LINUX_SYS_LISTEN __NR_listen
#define LINUX_SYS_GETSOCKNAME __NR_getsockname
#define LINUX_SYS_GETPEERNAME __NR_getpeername
#define LINUX_SYS_GETSOCKOPT __NR_getsockopt
#define LINUX_SYS_SETSOCKOPT __NR_setsockopt
#define LINUX_SYS_SHUTDOWN __NR_shutdown
#define LINUX_SYS_SENDTO __NR_sendto
#define LINUX_SYS_RECVFROM __NR_recvfrom
#define LINUX_SYS_STAT __NR_stat
#define LINUX_SYS_STATFS __NR_statfs
#define LINUX_SYS_FSTATFS __NR_fstatfs
#define LINUX_SYS_FSTAT __NR_fstat
#define LINUX_SYS_LSEEK __NR_lseek
#define LINUX_SYS_FTRUNCATE __NR_ftruncate
#define LINUX_SYS_MMAP __NR_mmap
#define LINUX_SYS_MREMAP __NR_mremap
#define LINUX_SYS_MLOCK __NR_mlock
#define LINUX_SYS_MUNLOCK __NR_munlock
#define LINUX_SYS_MLOCKALL __NR_mlockall
#define LINUX_SYS_MUNLOCKALL __NR_munlockall
#define LINUX_SYS_MLOCK2 __NR_mlock2
#define LINUX_SYS_BRK __NR_brk
#define LINUX_SYS_MSYNC __NR_msync
#define LINUX_SYS_MINCORE __NR_mincore
#define LINUX_SYS_MADVISE __NR_madvise
#define LINUX_SYS_FADVISE64 __NR_fadvise64
#define LINUX_SYS_READAHEAD __NR_readahead
#define LINUX_SYS_FALLOCATE __NR_fallocate
#define LINUX_SYS_UTIMENSAT __NR_utimensat
#define LINUX_SYS_UTIME __NR_utime
#define LINUX_SYS_UTIMES __NR_utimes
#define LINUX_SYS_FUTIMESAT __NR_futimesat
#define LINUX_SYS_SYNC_FILE_RANGE __NR_sync_file_range
#define LINUX_SYS_SYNC __NR_sync
#define LINUX_SYS_SYNCFS __NR_syncfs
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
#define LINUX_SYS_PAUSE __NR_pause
#define LINUX_SYS_SETPGID __NR_setpgid
#define LINUX_SYS_GETCWD __NR_getcwd
#define LINUX_SYS_CHDIR __NR_chdir
#define LINUX_SYS_RENAME __NR_rename
#define LINUX_SYS_MKDIR __NR_mkdir
#define LINUX_SYS_RMDIR __NR_rmdir
#define LINUX_SYS_UNLINK __NR_unlink
#define LINUX_SYS_FCNTL __NR_fcntl
#define LINUX_SYS_NANOSLEEP __NR_nanosleep
#define LINUX_SYS_ALARM __NR_alarm
#define LINUX_SYS_GETITIMER __NR_getitimer
#define LINUX_SYS_SETITIMER __NR_setitimer
#define LINUX_SYS_TIMER_CREATE __NR_timer_create
#define LINUX_SYS_TIMER_SETTIME __NR_timer_settime
#define LINUX_SYS_TIMER_GETTIME __NR_timer_gettime
#define LINUX_SYS_TIMER_GETOVERRUN __NR_timer_getoverrun
#define LINUX_SYS_TIMER_DELETE __NR_timer_delete
#define LINUX_SYS_RT_SIGTIMEDWAIT __NR_rt_sigtimedwait
#define LINUX_SYS_EVENTFD __NR_eventfd
#define LINUX_SYS_SIGNALFD __NR_signalfd
#define LINUX_SYS_SIGNALFD4 __NR_signalfd4
#define LINUX_SYS_PROCESS_VM_READV __NR_process_vm_readv
#define LINUX_SYS_PROCESS_VM_WRITEV __NR_process_vm_writev
#define LINUX_SYS_EVENTFD2 __NR_eventfd2
#define LINUX_SYS_TIMERFD_CREATE __NR_timerfd_create
#define LINUX_SYS_TIMERFD_SETTIME __NR_timerfd_settime
#define LINUX_SYS_TIMERFD_GETTIME __NR_timerfd_gettime
#define LINUX_SYS_EPOLL_CREATE __NR_epoll_create
#define LINUX_SYS_EPOLL_WAIT __NR_epoll_wait
#define LINUX_SYS_EPOLL_CTL __NR_epoll_ctl
#define LINUX_SYS_EPOLL_PWAIT __NR_epoll_pwait
#define LINUX_SYS_EPOLL_CREATE1 __NR_epoll_create1
#define LINUX_SYS_EPOLL_PWAIT2 __NR_epoll_pwait2
#define LINUX_SYS_EXECVE __NR_execve
#define LINUX_SYS_EXIT __NR_exit
#define LINUX_SYS_WAIT4 __NR_wait4
#define LINUX_SYS_WAITID __NR_waitid
#define LINUX_SYS_KILL __NR_kill
#define LINUX_SYS_TKILL __NR_tkill
#define LINUX_SYS_TGKILL __NR_tgkill
#define LINUX_SYS_SENDMMSG __NR_sendmmsg
#define LINUX_SYS_RECVMMSG __NR_recvmmsg
#define LINUX_SYS_RT_SIGQUEUEINFO __NR_rt_sigqueueinfo
#define LINUX_SYS_RT_TGSIGQUEUEINFO __NR_rt_tgsigqueueinfo
#define LINUX_SYS_RT_SIGPENDING __NR_rt_sigpending
#define LINUX_SYS_SIGALTSTACK __NR_sigaltstack
#define LINUX_SYS_GETPPID __NR_getppid
#define LINUX_SYS_GETPGRP __NR_getpgrp
#define LINUX_SYS_SETSID __NR_setsid
#define LINUX_SYS_GETPGID __NR_getpgid
#define LINUX_SYS_GETSID __NR_getsid
#define LINUX_SYS_CAPGET __NR_capget
#define LINUX_SYS_CAPSET __NR_capset
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
#define LINUX_SYS_SOCKETPAIR __NR_socketpair
#define LINUX_SYS_SENDMSG __NR_sendmsg
#define LINUX_SYS_RECVMSG __NR_recvmsg
#define LINUX_SYS_ACCEPT4 __NR_accept4
#define LINUX_SYS_GETUID __NR_getuid
#define LINUX_SYS_GETGID __NR_getgid
#define LINUX_SYS_GETEUID __NR_geteuid
#define LINUX_SYS_GETEGID __NR_getegid
#define LINUX_SYS_SETUID __NR_setuid
#define LINUX_SYS_SETGID __NR_setgid
#define LINUX_SYS_SETREUID __NR_setreuid
#define LINUX_SYS_SETREGID __NR_setregid
#define LINUX_SYS_SETRESUID __NR_setresuid
#define LINUX_SYS_GETRESUID __NR_getresuid
#define LINUX_SYS_SETRESGID __NR_setresgid
#define LINUX_SYS_GETRESGID __NR_getresgid
#define LINUX_SYS_SETFSUID __NR_setfsuid
#define LINUX_SYS_SETFSGID __NR_setfsgid
#define LINUX_SYS_SCHED_GET_PRIORITY_MAX __NR_sched_get_priority_max
#define LINUX_SYS_SCHED_GET_PRIORITY_MIN __NR_sched_get_priority_min
#define LINUX_SYS_SCHED_GETPARAM __NR_sched_getparam
#define LINUX_SYS_SCHED_GETSCHEDULER __NR_sched_getscheduler
#define LINUX_SYS_SCHED_SETPARAM __NR_sched_setparam
#define LINUX_SYS_SCHED_SETSCHEDULER __NR_sched_setscheduler
#define LINUX_SYS_SCHED_RR_GET_INTERVAL __NR_sched_rr_get_interval
#define LINUX_SYS_SCHED_SETATTR __NR_sched_setattr
#define LINUX_SYS_SCHED_GETATTR __NR_sched_getattr
#define LINUX_SYS_PERSONALITY __NR_personality
#define LINUX_SYS_PRCTL __NR_prctl
#define LINUX_SYS_UNAME __NR_uname
#define LINUX_SYS_SETHOSTNAME __NR_sethostname
#define LINUX_SYS_SETDOMAINNAME __NR_setdomainname
#define LINUX_SYS_MEMBARRIER __NR_membarrier
#define LINUX_SYS_RSEQ __NR_rseq
#define LINUX_SYS_OPENAT2 __NR_openat2
#define LINUX_SYS_MEMFD_CREATE __NR_memfd_create
#define LINUX_SYS_GETTIMEOFDAY __NR_gettimeofday
#define LINUX_SYS_SETTIMEOFDAY __NR_settimeofday
#define LINUX_SYS_CLOCK_SETTIME __NR_clock_settime
#define LINUX_SYS_CHMOD __NR_chmod
#define LINUX_SYS_FCHMOD __NR_fchmod
#define LINUX_SYS_CHOWN __NR_chown
#define LINUX_SYS_FCHOWN __NR_fchown
#define LINUX_SYS_LCHOWN __NR_lchown
#define LINUX_SYS_GETGROUPS __NR_getgroups
#define LINUX_SYS_SETGROUPS __NR_setgroups
#define LINUX_SYS_FCHMODAT2 __NR_fchmodat2
#define LINUX_SYS_FACCESSAT __NR_faccessat
#define LINUX_SYS_FACCESSAT2 __NR_faccessat2
#define LINUX_SYS_SCHED_SETAFFINITY __NR_sched_setaffinity
#define LINUX_SYS_SCHED_GETAFFINITY __NR_sched_getaffinity
#define LINUX_SYS_REBOOT __NR_reboot
#define LINUX_SYS_PIPE2 __NR_pipe2
#define LINUX_SYS_DUP3 __NR_dup3
#define LINUX_SYS_LSTAT __NR_lstat
#define LINUX_SYS_PREAD64 __NR_pread64
#define LINUX_SYS_PWRITE64 __NR_pwrite64
#define LINUX_SYS_READV __NR_readv
#define LINUX_SYS_WRITEV __NR_writev
#define LINUX_SYS_PREADV __NR_preadv
#define LINUX_SYS_PWRITEV __NR_pwritev
#define LINUX_SYS_PREADV2 __NR_preadv2
#define LINUX_SYS_PWRITEV2 __NR_pwritev2
#define LINUX_SYS_ACCESS __NR_access
#define LINUX_SYS_GETDENTS64 __NR_getdents64
#define LINUX_SYS_FCHDIR __NR_fchdir
#define LINUX_SYS_TRUNCATE __NR_truncate
#define LINUX_SYS_FSYNC __NR_fsync
#define LINUX_SYS_FDATASYNC __NR_fdatasync
#define LINUX_SYS_UMASK __NR_umask
#define LINUX_SYS_GETTID __NR_gettid
#define LINUX_SYS_SET_ROBUST_LIST __NR_set_robust_list
#define LINUX_SYS_GET_ROBUST_LIST __NR_get_robust_list
#define LINUX_SYS_ARCH_PRCTL __NR_arch_prctl
#define LINUX_SYS_SET_TID_ADDRESS __NR_set_tid_address
#define LINUX_SYS_EXIT_GROUP __NR_exit_group
#define LINUX_SYS_FUTEX __NR_futex
#define LINUX_SYS_FUTEX_WAKE __NR_futex_wake
#define LINUX_SYS_FUTEX_WAIT __NR_futex_wait
#define LINUX_SYS_FUTEX_REQUEUE __NR_futex_requeue
#define LINUX_SYS_FUTEX_WAITV __NR_futex_waitv
#define LINUX_SYS_CLOCK_GETRES __NR_clock_getres
#define LINUX_SYS_CLOCK_NANOSLEEP __NR_clock_nanosleep
#define LINUX_SYS_GETRANDOM __NR_getrandom
#define LINUX_SYS_TIME __NR_time
#define LINUX_SYS_GETCPU __NR_getcpu
#define LINUX_SYS_CLOSE_RANGE __NR_close_range
#define LINUX_SYS_PRLIMIT64 __NR_prlimit64
#define LINUX_SYS_NEWFSTATAT __NR_newfstatat
#define LINUX_SYS_STATX __NR_statx
#define LINUX_SYS_SENDFILE __NR_sendfile
#define LINUX_SYS_COPY_FILE_RANGE __NR_copy_file_range
#define LINUX_SYS_UNLINKAT __NR_unlinkat
#define LINUX_SYS_RENAMEAT __NR_renameat
#define LINUX_SYS_RENAMEAT2 __NR_renameat2
#define LINUX_SYS_MKDIRAT __NR_mkdirat
#define LINUX_SYS_CREAT __NR_creat
#define LINUX_SYS_GETDENTS __NR_getdents
#define LINUX_SYS_LINK __NR_link
#define LINUX_SYS_SYMLINK __NR_symlink
#define LINUX_SYS_READLINK __NR_readlink
#define LINUX_SYS_LINKAT __NR_linkat
#define LINUX_SYS_SYMLINKAT __NR_symlinkat
#define LINUX_SYS_FCHMODAT __NR_fchmodat
#define LINUX_SYS_FLOCK __NR_flock
#define LINUX_SYS_FCHOWNAT __NR_fchownat
#define LINUX_SYS_READLINKAT __NR_readlinkat
#define LINUX_SYS_PPOLL __NR_ppoll
#define LINUX_SYS_PSELECT6 __NR_pselect6
#define LINUX_SYS_SELECT __NR_select
#define LINUX_SYS_GETRUSAGE __NR_getrusage
#define LINUX_SYS_SYSINFO __NR_sysinfo
#define LINUX_SYS_TIMES __NR_times
#define LINUX_SYS_CLONE __NR_clone
#define LINUX_SYS_CLONE3 __NR_clone3

/* Open-status flags are declared by <leonos/fs.h>, which is included by the
 * storage and descriptor interfaces. Keep only the descriptor flag here. */
#ifndef LEONOS_FD_CLOEXEC
#define LEONOS_FD_CLOEXEC LINUX_FD_CLOEXEC
#endif

#define LEONOS_ENOSYS 38
#define LEONOS_EFAULT 14
#define LEONOS_EINVAL 22
#define LEONOS_ECHILD 10
#define LEONOS_ENOENT 2
#define LEONOS_ENOMEM 12
#define LEONOS_ESRCH 3
#define LEONOS_ENFILE 23
#define LEONOS_EWOULDBLOCK 11
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
#define LEONOS_ESPIPE 29
#define LEONOS_ERANGE 34
#include <linux/errno.h>
#define LEONOS_EROFS LINUX_EROFS
#define LEONOS_ENAMETOOLONG LINUX_ENAMETOOLONG
#define LEONOS_EOPNOTSUPP LINUX_EOPNOTSUPP

struct task;

struct syscall_frame {
    uint64_t number;
    uint64_t args[6];
};

/**
 * @brief Set up syscall dispatch tables and register the kernel entry handlers.
 */
void syscall_init(void);
/** @brief Enable opt-in numeric syscall diagnostics for an executable path prefix. */
void syscall_trace_configure(const char *cmdline);
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
 * @brief Validate an extensible Linux clone3 request and create its child.
 * @param frame Live parent trap frame, copied into the child with rax zero.
 * @param arguments User clone_args pointer; extensions must be zero.
 * @param size Native size_t length, at least 64 and at most one page.
 * @return Child TID or negative errno; unsupported backends return ENOSYS.
 */
int64_t syscall_clone3(const struct trap_frame *frame, uint64_t arguments, uint64_t size);
/**
 * @brief Resolve a user page fault at fault_addr with the given error code; 0 if handled.
 */
int syscall_handle_user_page_fault(uint64_t fault_addr, uint64_t error);
int syscall_handle_task_page_fault(struct task *task, uint64_t fault_addr, uint64_t error);
int syscall_page_fault_signal_code(struct task *task, uint64_t address);
int64_t syscall_poll(uint64_t fds_ptr, uint64_t count, int64_t timeout_ms);
int64_t syscall_linux_signal(uint64_t number, uint64_t signal_number,
                             uint64_t action_ptr, uint64_t old_action_ptr,
                             uint64_t mask_ptr, uint64_t sigset_size);
int64_t syscall_mm_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset);
int64_t syscall_mm_mremap(uint64_t old_addr, uint64_t old_len, uint64_t new_len,
                          uint64_t flags, uint64_t new_addr);
int64_t syscall_mm_mlock(uint64_t addr, uint64_t len, uint64_t flags);
int64_t syscall_mm_munlock(uint64_t addr, uint64_t len);
int64_t syscall_mm_mlockall(uint64_t flags);
int64_t syscall_mm_munlockall(void);
int64_t syscall_mm_brk(uint64_t requested);
int64_t syscall_mm_mprotect(uint64_t addr, uint64_t len, uint64_t prot);
int64_t syscall_mm_munmap(uint64_t addr, uint64_t len);
int64_t syscall_mm_msync(uint64_t addr, uint64_t len, uint64_t flags);
int64_t syscall_mm_mincore(uint64_t addr, uint64_t len, uint64_t vec);
int64_t syscall_mm_madvise(uint64_t addr, uint64_t len, uint64_t advice);
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
int syscall_unshare_task_files(struct task *task);
uint64_t task_socket_cancel_receive(struct task *task);
void task_release_syscall_file(struct task *task);
/**
 * @brief Closes all explicitly marked close-on-exec descriptors.
 * @param task Process replacing its image through execve.
 */
void syscall_close_cloexec_files(struct task *task);
int syscall_inherit_task_fds(struct task *parent, struct task *child,
                             int stdin_fd, int stdout_fd, int stderr_fd);

#endif
