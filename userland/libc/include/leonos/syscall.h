#ifndef LEONOS_SYSCALL_H
#define LEONOS_SYSCALL_H

#include <leonos/fs.h>
#include <leonos/auth.h>
#include <leonos/startup.h>
#include <leonos/syscall_abi.h>
#include <linux/mman.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define SYS_send SYS_sendto
#define SYS_recv SYS_recvfrom
#define SYS_nice LEONOS_SYS_NICE

#define LEONOS_PROT_READ LINUX_PROT_READ
#define LEONOS_PROT_WRITE LINUX_PROT_WRITE
#define LEONOS_PROT_EXEC LINUX_PROT_EXEC

#define LEONOS_MAP_PRIVATE LINUX_MAP_PRIVATE
#define LEONOS_MAP_FIXED LINUX_MAP_FIXED
#define LEONOS_MAP_ANONYMOUS LINUX_MAP_ANONYMOUS
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


int sleep_ms(unsigned long ms);
int leonos_stat_legacy(const char *path, struct leonos_stat *st);
int leonos_fstat_legacy(int fd, struct leonos_stat *st);

#endif
