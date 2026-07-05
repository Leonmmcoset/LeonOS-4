#ifndef LEONOS_NET_H
#define LEONOS_NET_H

#include <stdint.h>

#define LEONOS_IOCTL_NET_PING 0x4c4e5047UL
#define LEONOS_IOCTL_NET_CONFIG 0x4c4e4346UL
#define LEONOS_IOCTL_NET_DHCP 0x4c4e4448UL
#define LEONOS_IOCTL_NET_DNS 0x4c4e444eUL
#define LEONOS_IOCTL_NET_HTTP_GET 0x4c4e4854UL

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

#define LEONOS_NET_DEFAULT_TIMEOUT_MS 1000U
#define LEONOS_NET_MAX_TIMEOUT_MS 5000U
#define LEONOS_NET_DEFAULT_LOCAL_IP 0x0a00020fU
#define LEONOS_NET_DEFAULT_GATEWAY_IP 0x0a000202U
#define LEONOS_NET_DEFAULT_SUBNET_MASK 0xffffff00U
#define LEONOS_NET_DEFAULT_DNS_IP 0x0a000203U

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

int leonos_net_config(struct leonos_net_config *config);
int leonos_net_dhcp_renew(uint32_t timeout_ms, struct leonos_net_dhcp *result);
int leonos_net_ping(uint32_t target_ip, uint32_t timeout_ms,
                    struct leonos_net_ping *result);
int leonos_net_dns_resolve(const char *name, uint32_t timeout_ms,
                           struct leonos_net_dns *result);
int leonos_net_http_get(const char *host, const char *path,
                        uint32_t port, uint32_t timeout_ms,
                        struct leonos_net_http_get *result);

#endif
