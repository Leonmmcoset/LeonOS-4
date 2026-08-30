#ifndef LEONOS_SYS_UIO_H
#define LEONOS_SYS_UIO_H

#include <stddef.h>
#include <sys/types.h>

#ifndef IOV_MAX
#define IOV_MAX 1024
#endif

struct iovec {
    void *iov_base;
    size_t iov_len;
};

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#endif
