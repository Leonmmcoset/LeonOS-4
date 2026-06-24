#ifndef LEONOS_SYSCALL_H
#define LEONOS_SYSCALL_H

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
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_getcwd 79
#define SYS_chdir 80

long syscall0(long n);
long syscall1(long n, long a0);
long syscall2(long n, long a0, long a1);
long syscall3(long n, long a0, long a1, long a2);
long syscall6(long n, long a0, long a1, long a2, long a3, long a4, long a5);

int open(const char *path, int flags, int mode);
long read(int fd, void *buf, size_t len);
long write(int fd, const void *buf, size_t len);
int close(int fd);
void exit(int code) __attribute__((noreturn));
int chdir(const char *path);
char *getcwd(char *buf, size_t len);
int ioctl(int fd, unsigned long request, void *arg);
int sleep_ms(unsigned long ms);
int getpid(void);
int wait4(int pid, int *status, int options, void *rusage);
int execve(const char *path, char *const argv[], char *const envp[]);
void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int value, size_t len);

#endif
