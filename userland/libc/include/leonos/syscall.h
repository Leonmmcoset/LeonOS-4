#ifndef LEONOS_SYSCALL_H
#define LEONOS_SYSCALL_H

#include <leonos/fs.h>
#include <leonos/auth.h>
#include <leonos/startup.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_pipe 22
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lseek 8
#define SYS_poll 7
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_ioctl 16
#define SYS_rt_sigaction 13
#define SYS_rt_sigprocmask 14
#define SYS_rt_sigreturn 15
#define SYS_rt_sigsuspend 130
#define SYS_sched_yield 24
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_socket 41
#define SYS_connect 42
#define SYS_accept 43
#define SYS_sendto 44
#define SYS_recvfrom 45
#define SYS_sendmsg 46
#define SYS_recvmsg 47
#define SYS_shutdown 48
#define SYS_bind 49
#define SYS_listen 50
#define SYS_getsockname 51
#define SYS_setsockopt 54
#define SYS_getsockopt 55
#define SYS_socketpair 53
#define SYS_setpgid 109
#define SYS_fork 57
#define SYS_vfork 58
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_kill 62
#define SYS_nice 34
#define SYS_getppid 110
#define SYS_getpgrp 111
#define SYS_setsid 112
#define SYS_getpgid 121
#define SYS_getpriority 140
#define SYS_setpriority 141
#define SYS_getrlimit 97
#define SYS_setrlimit 160
#define SYS_getcwd 79
#define SYS_chdir 80
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_unlink 87
#define SYS_fcntl 72

#define LEONOS_PROT_READ 0x1
#define LEONOS_PROT_WRITE 0x2
#define LEONOS_PROT_EXEC 0x4

#define LEONOS_MAP_PRIVATE 0x02
#define LEONOS_MAP_FIXED 0x10
#define LEONOS_MAP_ANONYMOUS 0x20
#define LEONOS_MAP_FAILED ((void *)-1)

#define LEONOS_EPERM 1
#define LEONOS_EINTR 4
#define LEONOS_EACCES 13
#define LEONOS_EBUSY 16
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11
#define LEONOS_EEXIST 17
#define LEONOS_EPIPE 32
#define LEONOS_ENOTSOCK 88
#define LEONOS_ECONNREFUSED 111
#define LEONOS_ECONNRESET 104
#define LEONOS_EADDRINUSE 98
#define LEONOS_ENOTCONN 107

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

int open(const char *path, int flags, ...);
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
int stat(const char *path, struct leonos_stat *st);
int fstat(int fd, struct leonos_stat *st);
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
