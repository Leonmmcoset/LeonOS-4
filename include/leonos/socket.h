#ifndef LEONOS_SOCKET_H
#define LEONOS_SOCKET_H

#include <stdint.h>

/* Small, stable AF_UNIX ABI shared by the kernel and freestanding libc. */
#define LEONOS_AF_UNIX 1
#define LEONOS_SOCK_STREAM 1
#define LEONOS_SOCK_DGRAM 2
#define LEONOS_SOL_SOCKET 1
#define LEONOS_SO_ERROR 4
#define LEONOS_SO_TYPE 3
#define LEONOS_SHUT_RD 0
#define LEONOS_SHUT_WR 1
#define LEONOS_SHUT_RDWR 2
#define LEONOS_UNIX_PATH_MAX 108
#define LEONOS_IOV_MAX 16
#define LEONOS_SCM_RIGHTS 1
#define LEONOS_MSG_CTRUNC 0x08

typedef uint32_t leonos_socklen_t;

struct leonos_sockaddr_un {
    uint16_t sun_family;
    char sun_path[LEONOS_UNIX_PATH_MAX];
};

struct leonos_pollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

/* These use fixed-width members so kernel and freestanding userland retain
 * one stable x86_64 ABI without importing host socket headers. */
struct leonos_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};

struct leonos_msghdr {
    uint64_t msg_name;
    uint32_t msg_namelen;
    uint32_t reserved0;
    uint64_t msg_iov;
    uint64_t msg_iovlen;
    uint64_t msg_control;
    uint64_t msg_controllen;
    int32_t msg_flags;
    uint32_t reserved1;
};

struct leonos_cmsghdr {
    uint64_t cmsg_len;
    int32_t cmsg_level;
    int32_t cmsg_type;
};

#define LEONOS_POLLIN 0x0001
#define LEONOS_POLLPRI 0x0002
#define LEONOS_POLLOUT 0x0004
#define LEONOS_POLLERR 0x0008
#define LEONOS_POLLHUP 0x0010
#define LEONOS_POLLNVAL 0x0020

#endif
