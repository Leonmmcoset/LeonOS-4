#ifndef LEONOS_NETINET_IN_H
#define LEONOS_NETINET_IN_H

#include <sys/socket.h>
#include <stdint.h>

#ifndef _IN_PORT_T_DECLARED
typedef uint16_t in_port_t;
#define _IN_PORT_T_DECLARED
#endif
#ifndef _IN_ADDR_T_DECLARED
typedef uint32_t in_addr_t;
#define _IN_ADDR_T_DECLARED
#endif
struct in_addr { in_addr_t s_addr; };

#define INADDR_ANY ((in_addr_t)0)
#define INADDR_LOOPBACK ((in_addr_t)0x0100007fU)

#endif
