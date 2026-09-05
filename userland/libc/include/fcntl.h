#ifndef LEONOS_FCNTL_H
#define LEONOS_FCNTL_H

#include <leonos/fs.h>

#define O_RDONLY LEONOS_O_RDONLY
#define O_WRONLY LEONOS_O_WRONLY
#define O_RDWR LEONOS_O_RDWR
#define O_CREAT LEONOS_O_CREAT
#define O_TRUNC LEONOS_O_TRUNC
#define O_APPEND LEONOS_O_APPEND
#define O_CLOEXEC 0x80000
#define O_DIRECTORY 0x10000
#define O_NONBLOCK 0x800
#define O_NOFOLLOW 0x20000

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 14
#define FD_CLOEXEC 1

int fcntl(int fd, int command, ...);

#endif
