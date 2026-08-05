#ifndef LEONOS_BUSYBOX_SYS_SOCKET_H
#define LEONOS_BUSYBOX_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned short sa_family_t;
typedef unsigned socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __padding[126];
};

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_RDM 4
#define SOCK_SEQPACKET 5
#define SOL_SOCKET 1
#define SO_ERROR 4

int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
int setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int shutdown(int fd, int how);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *destination, socklen_t destination_length);

#endif
