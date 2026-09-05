#ifndef LEONOS_SYSCALL_H
#define LEONOS_SYSCALL_H

#include <leonos/fs.h>
#include <leonos/auth.h>
#include <leonos/startup.h>
#include <linux/syscall.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_read __NR_read
#define SYS_write __NR_write
#define SYS_open __NR_open
#define SYS_close __NR_close
#define SYS_pipe __NR_pipe
#define SYS_socket __NR_socket
#define SYS_connect __NR_connect
#define SYS_accept __NR_accept
#define SYS_bind __NR_bind
#define SYS_listen __NR_listen
#define SYS_getsockname __NR_getsockname
#define SYS_getsockopt __NR_getsockopt
#define SYS_setsockopt __NR_setsockopt
#define SYS_shutdown __NR_shutdown
#define SYS_sendto __NR_sendto
#define SYS_recvfrom __NR_recvfrom
#define SYS_send __NR_send
#define SYS_recv __NR_recv
#define SYS_stat __NR_stat
#define SYS_fstat __NR_fstat
#define SYS_lseek __NR_lseek
#define SYS_ftruncate __NR_ftruncate
#define SYS_mmap __NR_mmap
#define SYS_mprotect __NR_mprotect
#define SYS_munmap __NR_munmap
#define SYS_ioctl __NR_ioctl
#define SYS_poll __NR_poll
#define SYS_sched_yield __NR_sched_yield
#define SYS_dup __NR_dup
#define SYS_dup2 __NR_dup2
#define SYS_nanosleep __NR_nanosleep
#define SYS_getpid __NR_getpid
#define SYS_setpgid __NR_setpgid
#define SYS_fork __NR_fork
#define SYS_vfork __NR_vfork
#define SYS_execve __NR_execve
#define SYS_exit __NR_exit
#define SYS_wait4 __NR_wait4
#define SYS_kill __NR_kill
#define SYS_nice 34
#define SYS_getppid __NR_getppid
#define SYS_getpgrp __NR_getpgrp
#define SYS_setsid __NR_setsid
#define SYS_getpgid __NR_getpgid
#define SYS_getpriority __NR_getpriority
#define SYS_setpriority __NR_setpriority
#define SYS_getrlimit __NR_getrlimit
#define SYS_setrlimit __NR_setrlimit
#define SYS_getcwd __NR_getcwd
#define SYS_chdir __NR_chdir
#define SYS_rename __NR_rename
#define SYS_mkdir __NR_mkdir
#define SYS_rmdir __NR_rmdir
#define SYS_unlink __NR_unlink
#define SYS_fcntl __NR_fcntl
#define SYS_openat __NR_openat
#define SYS_clock_gettime __NR_clock_gettime
#define SYS_rt_sigaction __NR_rt_sigaction
#define SYS_rt_sigprocmask __NR_rt_sigprocmask
#define SYS_rt_sigreturn __NR_rt_sigreturn
#define SYS_rt_sigsuspend __NR_rt_sigsuspend
#define SYS_mount __NR_mount
#define SYS_umount2 __NR_umount2

#define LEONOS_PROT_READ 0x1
#define LEONOS_PROT_WRITE 0x2
#define LEONOS_PROT_EXEC 0x4

#define LEONOS_MAP_PRIVATE 0x02
#define LEONOS_MAP_FIXED 0x10
#define LEONOS_MAP_ANONYMOUS 0x20
#define LEONOS_MAP_FAILED ((void *)-1)

#define LEONOS_EPERM 1
#define LEONOS_EACCES 13
#define LEONOS_EBUSY 16
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11
#define LEONOS_EEXIST 17
#define LEONOS_EPIPE 32

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, ...);
long read(int fd, void *buf, size_t len);
long write(int fd, const void *buf, size_t len);
int close(int fd);
int dup(int fd);
int pipe(int filedes[2]);
long lseek(int fd, long offset, int whence);
void exit(int code) __attribute__((noreturn));
int chdir(const char *path);
char *getcwd(char *buf, size_t len);
int ioctl(int fd, unsigned long request, void *arg);
int sched_yield(void);
int sleep_ms(unsigned long ms);
int getpid(void);
int leonos_stat_legacy(const char *path, struct leonos_stat *st);
int leonos_fstat_legacy(int fd, struct leonos_stat *st);
int wait4(int pid, int *status, int options, void *rusage);
int execve(const char *path, char *const argv[], char *const envp[]);
int mkdir(const char *path, int mode);
int unlink(const char *path);
int rmdir(const char *path);
int rename(const char *old_path, const char *new_path);
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long offset);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);
int ftruncate(int fd, long length);
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);

#endif
