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
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_ioctl 16
#define SYS_sched_yield 24
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
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
#define LEONOS_EACCES 13
#define LEONOS_EIO 5
#define LEONOS_EAGAIN 11
#define LEONOS_EEXIST 17

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

int open(const char *path, int flags, int mode);
long read(int fd, void *buf, size_t len);
long write(int fd, const void *buf, size_t len);
int close(int fd);
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
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);

#endif
