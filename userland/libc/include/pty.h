#ifndef LEONOS_PTY_COMPAT_H
#define LEONOS_PTY_COMPAT_H

#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>

int openpty(int *master, int *slave, char *name,
            const struct termios *termios, const struct winsize *winsize);
pid_t forkpty(int *master, char *name, const struct termios *termios,
              const struct winsize *winsize);
int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);

#endif
