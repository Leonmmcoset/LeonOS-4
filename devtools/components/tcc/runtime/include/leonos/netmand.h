#ifndef LEONOS_NETMAND_H
#define LEONOS_NETMAND_H

#include <leonos/net.h>
#include <stdint.h>

enum leonos_netmand_msg {
    LEONOS_NET_MSG_HELLO = 10,
    LEONOS_NET_MSG_ACK = 11,
    LEONOS_NET_MSG_CONFIG = 20,
    LEONOS_NET_MSG_DNS_POLICY = 21,
    LEONOS_NET_MSG_DHCP = 22,
    LEONOS_NET_MSG_PING = 23,
    LEONOS_NET_MSG_DNS = 24,
    LEONOS_NET_MSG_CONNECTIONS = 25,
};

struct leonos_netmand_hello {
    uint32_t pid;
    uint32_t reserved;
};

struct leonos_netmand_ack {
    int32_t code;
    uint32_t reserved;
};

struct leonos_netmand_connections_ack {
    uint32_t count;
    uint32_t reserved;
    /* followed by count * struct leonos_net_connection_info */
};

#endif
