#ifndef LEONOS_UAPI_LINUX_EPOLL_H
#define LEONOS_UAPI_LINUX_EPOLL_H

#include <linux/types.h>

struct epoll_event {
    __u32 events;
    __u64 data;
} __attribute__((packed));

_Static_assert(sizeof(struct epoll_event) == 12, "Linux x86-64 epoll_event ABI");

#define EPOLLIN 0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDNORM 0x040
#define EPOLLRDBAND 0x080
#define EPOLLWRNORM 0x100
#define EPOLLWRBAND 0x200
#define EPOLLMSG 0x400
#define EPOLLRDHUP 0x2000
#define EPOLLEXCLUSIVE (1u << 28)
#define EPOLLWAKEUP (1u << 29)
#define EPOLLONESHOT (1u << 30)
#define EPOLLET (1u << 31)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#endif
