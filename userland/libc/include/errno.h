#ifndef LEONOS_ERRNO_H
#define LEONOS_ERRNO_H

#define EISDIR 21
#define EIO 5
#define EAGAIN 11
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define EBADF 9
#define EFAULT 14
#define EINVAL 22
#define ENOENT 2
#define ENOMEM 12
#define EMFILE 24
#define EPIPE 32
#define ENOSYS 38
#define ENOTSUP 95
#define EADDRINUSE 98
#define EISCONN 106
#define ENOTCONN 107
#define EINTR 4
#define EAFNOSUPPORT 97

extern int errno;

#endif
