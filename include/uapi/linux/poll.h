#ifndef LEONOS_UAPI_LINUX_POLL_H
#define LEONOS_UAPI_LINUX_POLL_H

#include <stdint.h>

typedef unsigned long nfds_t;

struct pollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

#endif
