#ifndef LEONOS_BUSYBOX_ARPA_INET_H
#define LEONOS_BUSYBOX_ARPA_INET_H

#include <stdint.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct in6_addr {
    uint8_t s6_addr[16];
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t sin6_family;
    in_port_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

#define INADDR_ANY ((in_addr_t)0)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
in_addr_t inet_addr(const char *text);
char *inet_ntoa(struct in_addr address);
int inet_pton(int family, const char *source, void *destination);
const char *inet_ntop(int family, const void *source, char *destination,
                      socklen_t size);

#endif
