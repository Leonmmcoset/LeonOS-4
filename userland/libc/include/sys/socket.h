#ifndef LEONOS_SYS_SOCKET_H
#define LEONOS_SYS_SOCKET_H

#include <leonos/socket.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

typedef leonos_socklen_t socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[126];
};

struct in_addr { uint32_t s_addr; };
struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};
struct sockaddr_in6 {
    sa_family_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

#define AF_INET 2
#define AF_INET6 10

#define AF_UNIX LEONOS_AF_UNIX
#define AF_LOCAL LEONOS_AF_UNIX
#define AF_UNSPEC 0
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define SOCK_STREAM LEONOS_SOCK_STREAM
#define SOCK_DGRAM LEONOS_SOCK_DGRAM
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY 0u
#define INADDR_LOOPBACK 0x7f000001u
#define SOL_SOCKET LEONOS_SOL_SOCKET
#define SO_ERROR LEONOS_SO_ERROR
#define SO_TYPE LEONOS_SO_TYPE
#define SO_KEEPALIVE 9
#define SCM_RIGHTS LEONOS_SCM_RIGHTS
#define MSG_CTRUNC LEONOS_MSG_CTRUNC
#define SHUT_RD LEONOS_SHUT_RD
#define SHUT_WR LEONOS_SHUT_WR
#define SHUT_RDWR LEONOS_SHUT_RDWR

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(length) (((length) + sizeof(size_t) - 1u) & ~(sizeof(size_t) - 1u))
#define CMSG_SPACE(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(length))
#define CMSG_LEN(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (length))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(message) \
    ((message)->msg_controllen >= sizeof(struct cmsghdr) ? \
     (struct cmsghdr *)(message)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(message, cmsg) \
    (((unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) + \
      sizeof(struct cmsghdr) <= (unsigned char *)(message)->msg_control + \
      (message)->msg_controllen) ? \
     (struct cmsghdr *)((unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len)) : \
     (struct cmsghdr *)0)

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int filedes[2]);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int connect(int fd, const struct sockaddr *address, socklen_t length);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length);
int shutdown(int fd, int how);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
int setsockopt(int fd, int level, int option, const void *value, socklen_t length);

static inline uint16_t htons(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }
static inline uint16_t ntohs(uint16_t value) { return htons(value); }
static inline uint32_t htonl(uint32_t value) {
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}
static inline uint32_t ntohl(uint32_t value) { return htonl(value); }

#endif
