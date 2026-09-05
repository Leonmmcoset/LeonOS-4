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

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#endif
