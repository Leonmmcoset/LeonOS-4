#ifndef LEONOS_UAPI_LINUX_SOCKET_H
#define LEONOS_UAPI_LINUX_SOCKET_H

#include <linux/types.h>

typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __data[126];
};

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct { uint32_t s_addr; } sin_addr;
    uint8_t sin_zero[8];
};

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_SEQPACKET 5

#define SOL_SOCKET 1
#define SO_ERROR 4
#define SO_TYPE 3
#define SO_REUSEADDR 2
#define SO_PEERCRED 17

#define SCM_RIGHTS 1

#define MSG_CTRUNC 0x08
#define MSG_PEEK 0x02

#define SOCK_NONBLOCK 0x0800
#define SOCK_CLOEXEC 0x80000

struct ucred {
    int32_t pid;
    uint32_t uid;
    uint32_t gid;
};

struct iovec {
    void *iov_base;
    uint64_t iov_len;
};

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    uint64_t msg_iovlen;
    void *msg_control;
    uint64_t msg_controllen;
    int32_t msg_flags;
};

struct cmsghdr {
    uint64_t cmsg_len;
    int32_t cmsg_level;
    int32_t cmsg_type;
};

#define CMSG_ALIGN(len) (((len) + sizeof(uint64_t) - 1u) & ~(sizeof(uint64_t) - 1u))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((void *)((uint8_t *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr))))
#define CMSG_FIRSTHDR(msg) ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(msg)->msg_control : 0)

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#endif
