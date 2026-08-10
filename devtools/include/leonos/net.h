#ifndef LEONOS_NET_H
#define LEONOS_NET_H

#include <stdint.h>

#define LEONOS_IOCTL_NET_PING 0x4c4e5047UL
#define LEONOS_IOCTL_NET_CONFIG 0x4c4e4346UL
#define LEONOS_IOCTL_NET_DHCP 0x4c4e4448UL
#define LEONOS_IOCTL_NET_DNS 0x4c4e444eUL
#define LEONOS_IOCTL_NET_HTTP_GET 0x4c4e4854UL
#define LEONOS_IOCTL_NET_SOCKET_OPEN 0x4c4e534fUL
#define LEONOS_IOCTL_NET_SOCKET_CONNECT 0x4c4e5343UL
#define LEONOS_IOCTL_NET_SOCKET_SEND 0x4c4e5353UL
#define LEONOS_IOCTL_NET_SOCKET_RECV 0x4c4e5352UL
#define LEONOS_IOCTL_NET_SOCKET_CLOSE 0x4c4e5358UL
#define LEONOS_IOCTL_NET_CONNECTIONS 0x4c4e434eUL
#define LEONOS_IOCTL_NET_DNS_POLICY 0x4c4e4450UL

#define LEONOS_NET_STATUS_OK 0U
#define LEONOS_NET_STATUS_NO_DEVICE 1U
#define LEONOS_NET_STATUS_ARP_TIMEOUT 2U
#define LEONOS_NET_STATUS_ECHO_TIMEOUT 3U
#define LEONOS_NET_STATUS_BAD_ARGUMENT 4U
#define LEONOS_NET_STATUS_TX_FAILED 5U
#define LEONOS_NET_STATUS_DHCP_TIMEOUT 6U
#define LEONOS_NET_STATUS_DHCP_FAILED 7U
#define LEONOS_NET_STATUS_DNS_TIMEOUT 8U
#define LEONOS_NET_STATUS_DNS_FAILED 9U
#define LEONOS_NET_STATUS_DNS_NO_ANSWER 10U
#define LEONOS_NET_STATUS_TCP_TIMEOUT 11U
#define LEONOS_NET_STATUS_TCP_RESET 12U
#define LEONOS_NET_STATUS_TCP_FAILED 13U
#define LEONOS_NET_STATUS_HTTP_FAILED 14U
#define LEONOS_NET_STATUS_HTTP_TOO_LARGE 15U
#define LEONOS_NET_STATUS_SOCKET_LIMIT 16U
#define LEONOS_NET_STATUS_SOCKET_BAD_HANDLE 17U
#define LEONOS_NET_STATUS_SOCKET_NOT_CONNECTED 18U
#define LEONOS_NET_STATUS_SOCKET_CLOSED 19U
#define LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED 20U
#define LEONOS_NET_STATUS_TLS_FAILED 21U

#define LEONOS_NET_DEFAULT_TIMEOUT_MS 1000U
#define LEONOS_NET_MAX_TIMEOUT_MS 10000U
#define LEONOS_NET_DEFAULT_LOCAL_IP 0x0a00020fU
#define LEONOS_NET_DEFAULT_GATEWAY_IP 0x0a000202U
#define LEONOS_NET_DEFAULT_SUBNET_MASK 0xffffff00U
#define LEONOS_NET_CLOUDFLARE_DNS_IP 0x01010101U
#define LEONOS_NET_DEFAULT_DNS_IP LEONOS_NET_CLOUDFLARE_DNS_IP

#define LEONOS_NET_DNS_MODE_CLOUDFLARE 0U
#define LEONOS_NET_DNS_MODE_DHCP 1U
#define LEONOS_NET_DNS_MODE_CUSTOM 2U
#define LEONOS_NET_DNS_MODE_QUERY 0xffffffffU

#define LEONOS_NET_CONFIG_SOURCE_NONE 0U
#define LEONOS_NET_CONFIG_SOURCE_STATIC 1U
#define LEONOS_NET_CONFIG_SOURCE_DHCP 2U

#define LEONOS_NET_CONFIG_FLAG_PRESENT 0x00000001U
#define LEONOS_NET_CONFIG_FLAG_ACTIVE 0x00000002U
#define LEONOS_NET_CONFIG_FLAG_DHCP 0x00000004U

#define LEONOS_NET_HOSTNAME_LEN 128U
#define LEONOS_NET_DNS_MAX_ADDRESSES 4U
#define LEONOS_NET_HTTP_PATH_LEN 256U
#define LEONOS_NET_HTTP_RESPONSE_MAX 4096U
#define LEONOS_NET_SOCKET_MAX 16U

#define LEONOS_NET_AF_INET 2U
#define LEONOS_NET_SOCK_STREAM 1U
#define LEONOS_NET_IPPROTO_TCP 6U

#define LEONOS_NET_TCP_CLOSED 0U
#define LEONOS_NET_TCP_SYN_SENT 1U
#define LEONOS_NET_TCP_ESTABLISHED 2U
#define LEONOS_NET_TCP_TIME_WAIT 3U

struct leonos_net_config {
    uint32_t flags;
    uint32_t source;
    uint32_t local_ip;
    uint32_t subnet_mask;
    uint32_t gateway_ip;
    uint32_t dns_ip;
    uint32_t dhcp_server_ip;
    uint32_t lease_seconds;
    uint8_t mac[6];
    uint8_t reserved_mac[2];
};

struct leonos_net_dhcp {
    uint32_t timeout_ms;
    uint32_t status;
    struct leonos_net_config config;
};

struct leonos_net_dns_policy {
    uint32_t mode;
    uint32_t custom_dns_ip;
    uint32_t status;
    uint32_t reserved;
    struct leonos_net_config config;
};

struct leonos_net_ping {
    uint32_t target_ip;
    uint32_t timeout_ms;
    uint32_t sequence;
    uint32_t status;
    uint32_t rtt_ms;
    uint32_t sent;
    uint32_t received;
    uint32_t reserved;
};

struct leonos_net_dns {
    char name[LEONOS_NET_HOSTNAME_LEN];
    uint32_t timeout_ms;
    uint32_t status;
    uint32_t address_count;
    uint32_t addresses[LEONOS_NET_DNS_MAX_ADDRESSES];
};

struct leonos_net_http_get {
    char host[LEONOS_NET_HOSTNAME_LEN];
    char path[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t port;
    uint32_t timeout_ms;
    uint32_t status;
    uint32_t remote_ip;
    uint32_t http_status;
    uint32_t response_len;
    char response[LEONOS_NET_HTTP_RESPONSE_MAX];
};

struct leonos_net_socket_open {
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    uint32_t timeout_ms;
    uint32_t status;
    int32_t socket;
};

struct leonos_net_socket_connect {
    int32_t socket;
    char host[LEONOS_NET_HOSTNAME_LEN];
    uint32_t port;
    uint32_t timeout_ms;
    uint32_t status;
    uint32_t remote_ip;
    uint32_t local_ip;
    uint32_t local_port;
};

struct leonos_net_socket_io {
    int32_t socket;
    void *buffer;
    uint32_t length;
    uint32_t timeout_ms;
    uint32_t status;
    uint32_t transferred;
};

struct leonos_net_socket_close {
    int32_t socket;
    uint32_t status;
};

struct leonos_net_connection_info {
    int32_t socket;
    uint32_t owner_pid;
    uint32_t state;
    uint32_t status;
    uint32_t local_ip;
    uint32_t remote_ip;
    uint32_t local_port;
    uint32_t remote_port;
    uint32_t age_ms;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
};

struct leonos_net_connection_list {
    uint32_t capacity;
    uint32_t count;
    struct leonos_net_connection_info *entries;
};

int leonos_net_config(struct leonos_net_config *config);
int leonos_net_get_dns_policy(struct leonos_net_dns_policy *result);
int leonos_net_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                              struct leonos_net_dns_policy *result);
int leonos_net_dhcp_renew(uint32_t timeout_ms, struct leonos_net_dhcp *result);
int leonos_net_ping(uint32_t target_ip, uint32_t timeout_ms,
                    struct leonos_net_ping *result);
int leonos_net_dns_resolve(const char *name, uint32_t timeout_ms,
                           struct leonos_net_dns *result);
int leonos_net_http_get(const char *host, const char *path,
                        uint32_t port, uint32_t timeout_ms,
                        struct leonos_net_http_get *result);
int leonos_socket_tcp(void);
int leonos_socket_connect(int socket, const char *host,
                          uint32_t port, uint32_t timeout_ms,
                          struct leonos_net_socket_connect *result);
long leonos_socket_send(int socket, const void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status);
long leonos_socket_recv(int socket, void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status);
int leonos_socket_close(int socket);
int leonos_net_connections(struct leonos_net_connection_info *entries,
                           uint32_t capacity, uint32_t *out_count);

#endif
