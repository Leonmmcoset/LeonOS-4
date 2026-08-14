#ifndef LEONOS_UNISTD_H
#define LEONOS_UNISTD_H

#include <sys/types.h>

int isatty(int fd);
int mkdir(const char *path, int mode);
pid_t fork(void);
pid_t vfork(void);

#endif
