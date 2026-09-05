#ifndef LEONOS_PTY_POSIX_H
#define LEONOS_PTY_POSIX_H

#include <sys/types.h>
#include <termios.h>

int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);
int openpty(int *master, int *slave, char *name,
            const struct termios *termios, const struct winsize *winsize);
pid_t forkpty(int *master, const char *name,
              const struct termios *termios, const struct winsize *winsize);

#endif
