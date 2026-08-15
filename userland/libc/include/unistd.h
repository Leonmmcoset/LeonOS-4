#ifndef LEONOS_UNISTD_H
#define LEONOS_UNISTD_H

#include <sys/types.h>

#ifndef _USECONDS_T_DECLARED
typedef unsigned long useconds_t;
#define _USECONDS_T_DECLARED
#endif

int isatty(int fd);
int mkdir(const char *path, int mode);
pid_t fork(void);
pid_t vfork(void);
pid_t getpid(void);
pid_t getppid(void);
int pipe(int filedes[2]);
int dup(int fd);
int dup2(int old_fd, int new_fd);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t waitpid(pid_t pid, int *status, int options);
int kill(pid_t pid, int signal_number);
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
int setpgid(pid_t pid, pid_t process_group);
int setpgrp(void);
pid_t setsid(void);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t process_group);
void _exit(int code) __attribute__((noreturn));

/* POSIX/XSI extension exported by libleonos; visible even with strict POSIX
 * feature macros used by the built-in applications. */
int usleep(useconds_t microseconds);

#endif
