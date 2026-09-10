#ifndef LEONOS_UAPI_LINUX_SOCKET_H
#define LEONOS_UAPI_LINUX_SOCKET_H

#include <linux/types.h>
#include <linux/uio.h>

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
#define SO_PASSCRED 16
#define SO_PEERCRED 17
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_RCVTIMEO_NEW 66
#define SO_SNDTIMEO_NEW 67
#define SO_ACCEPTCONN 30
#define SO_PROTOCOL 38
#define SO_DOMAIN 39

#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2

#define MSG_CTRUNC 0x08
#define MSG_OOB 0x01
#define MSG_TRUNC 0x20
#define MSG_PEEK 0x02
#define MSG_DONTWAIT 0x40
#define MSG_EOR 0x80
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000
#define MSG_MORE 0x8000
#define MSG_ERRQUEUE 0x2000
#define MSG_WAITFORONE 0x10000
#define MSG_BATCH 0x40000
#define MSG_CMSG_COMPAT 0x80000000u
#define MSG_CMSG_CLOEXEC 0x40000000

#define SOCK_NONBLOCK 0x0800
#define SOCK_CLOEXEC 0x80000

struct ucred {
    int32_t pid;
    uint32_t uid;
    uint32_t gid;
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

struct mmsghdr {
    struct msghdr msg_hdr;
    uint32_t msg_len;
};

_Static_assert(sizeof(struct msghdr) == 56, "native x86-64 msghdr");
_Static_assert(sizeof(struct mmsghdr) == 64, "native x86-64 mmsghdr stride");
_Static_assert(__builtin_offsetof(struct mmsghdr, msg_len) == 56, "native msg_len offset");

#define CMSG_ALIGN(len) (((len) + sizeof(uint64_t) - 1u) & ~(sizeof(uint64_t) - 1u))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((void *)((uint8_t *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr))))
#define CMSG_FIRSTHDR(msg) ((msg)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(msg)->msg_control : 0)
static inline struct cmsghdr *leonos_cmsg_nxthdr(const struct msghdr *message,
                                                const struct cmsghdr *header)
{
    uintptr_t offset = (uintptr_t)header - (uintptr_t)message->msg_control;
    if (header->cmsg_len < sizeof(*header) || offset > message->msg_controllen ||
        header->cmsg_len > message->msg_controllen - offset) return 0;
    offset += CMSG_ALIGN(header->cmsg_len);
    if (offset > message->msg_controllen || sizeof(*header) > message->msg_controllen - offset) return 0;
    return (struct cmsghdr *)((uint8_t *)message->msg_control + offset);
}
#define CMSG_NXTHDR(msg, cmsg) leonos_cmsg_nxthdr((msg), (cmsg))

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#endif
