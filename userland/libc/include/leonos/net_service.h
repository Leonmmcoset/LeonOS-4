#ifndef LEONOS_NET_SERVICE_H
#define LEONOS_NET_SERVICE_H

/* Versioned LeonOS network service SDK.
 *
 * Consumers open the registered /dev/net0 service device through this
 * library; they never use fd 3 or the pre-migration leonos_net_* ABI.
 * IPv4 transport and control operations remain a versioned kernel service
 * until the AF_INET socket subset reaches parity. The wire structs are the
 * existing versioned network-service protocol records.
 */
#include <leonos/net.h>
#include <stdint.h>

#define NET_SERVICE_ABI_VERSION 1U

typedef struct leonos_net_config net_service_config_t;
typedef struct leonos_net_dns_policy net_service_dns_policy_t;
typedef struct leonos_net_dhcp net_service_dhcp_t;
typedef struct leonos_net_ping net_service_ping_t;
typedef struct leonos_net_dns net_service_dns_t;
typedef struct leonos_net_http_get net_service_http_get_t;
typedef struct leonos_net_socket_open net_service_socket_open_t;
typedef struct leonos_net_socket_connect net_service_socket_connect_t;
typedef struct leonos_net_socket_io net_service_socket_io_t;
typedef struct leonos_net_socket_close net_service_socket_close_t;
typedef struct leonos_net_connection_info net_service_connection_info_t;
typedef struct leonos_net_connection_list net_service_connection_list_t;

#define NET_SERVICE_AF_INET LEONOS_NET_AF_INET
#define NET_SERVICE_SOCK_STREAM LEONOS_NET_SOCK_STREAM
#define NET_SERVICE_IPPROTO_TCP LEONOS_NET_IPPROTO_TCP

#define NET_SERVICE_STATUS_OK LEONOS_NET_STATUS_OK
#define NET_SERVICE_STATUS_NO_DEVICE LEONOS_NET_STATUS_NO_DEVICE
#define NET_SERVICE_STATUS_ARP_TIMEOUT LEONOS_NET_STATUS_ARP_TIMEOUT
#define NET_SERVICE_STATUS_ECHO_TIMEOUT LEONOS_NET_STATUS_ECHO_TIMEOUT
#define NET_SERVICE_STATUS_BAD_ARGUMENT LEONOS_NET_STATUS_BAD_ARGUMENT
#define NET_SERVICE_STATUS_TX_FAILED LEONOS_NET_STATUS_TX_FAILED
#define NET_SERVICE_STATUS_DHCP_TIMEOUT LEONOS_NET_STATUS_DHCP_TIMEOUT
#define NET_SERVICE_STATUS_DHCP_FAILED LEONOS_NET_STATUS_DHCP_FAILED
#define NET_SERVICE_STATUS_DNS_TIMEOUT LEONOS_NET_STATUS_DNS_TIMEOUT
#define NET_SERVICE_STATUS_DNS_FAILED LEONOS_NET_STATUS_DNS_FAILED
#define NET_SERVICE_STATUS_DNS_NO_ANSWER LEONOS_NET_STATUS_DNS_NO_ANSWER
#define NET_SERVICE_STATUS_TCP_TIMEOUT LEONOS_NET_STATUS_TCP_TIMEOUT
#define NET_SERVICE_STATUS_TCP_RESET LEONOS_NET_STATUS_TCP_RESET
#define NET_SERVICE_STATUS_TCP_FAILED LEONOS_NET_STATUS_TCP_FAILED
#define NET_SERVICE_STATUS_HTTP_FAILED LEONOS_NET_STATUS_HTTP_FAILED
#define NET_SERVICE_STATUS_HTTP_TOO_LARGE LEONOS_NET_STATUS_HTTP_TOO_LARGE
#define NET_SERVICE_STATUS_SOCKET_LIMIT LEONOS_NET_STATUS_SOCKET_LIMIT
#define NET_SERVICE_STATUS_SOCKET_BAD_HANDLE LEONOS_NET_STATUS_SOCKET_BAD_HANDLE
#define NET_SERVICE_STATUS_SOCKET_NOT_CONNECTED LEONOS_NET_STATUS_SOCKET_NOT_CONNECTED
#define NET_SERVICE_STATUS_SOCKET_CLOSED LEONOS_NET_STATUS_SOCKET_CLOSED
#define NET_SERVICE_STATUS_PROTOCOL_UNSUPPORTED LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED
#define NET_SERVICE_STATUS_TLS_FAILED LEONOS_NET_STATUS_TLS_FAILED

#define NET_SERVICE_DNS_MODE_QUERY LEONOS_NET_DNS_MODE_QUERY
#define NET_SERVICE_DNS_MODE_CLOUDFLARE LEONOS_NET_DNS_MODE_CLOUDFLARE
#define NET_SERVICE_DNS_MODE_DHCP LEONOS_NET_DNS_MODE_DHCP
#define NET_SERVICE_DNS_MODE_CUSTOM LEONOS_NET_DNS_MODE_CUSTOM
#define NET_SERVICE_CLOUDFLARE_DNS_IP LEONOS_NET_CLOUDFLARE_DNS_IP

#define NET_SERVICE_TCP_SYN_SENT LEONOS_NET_TCP_SYN_SENT
#define NET_SERVICE_TCP_ESTABLISHED LEONOS_NET_TCP_ESTABLISHED
#define NET_SERVICE_TCP_TIME_WAIT LEONOS_NET_TCP_TIME_WAIT
#define NET_SERVICE_TCP_CLOSED LEONOS_NET_TCP_CLOSED

#define NET_SERVICE_CONFIG_SOURCE_DHCP LEONOS_NET_CONFIG_SOURCE_DHCP
#define NET_SERVICE_CONFIG_SOURCE_STATIC LEONOS_NET_CONFIG_SOURCE_STATIC
#define NET_SERVICE_CONFIG_FLAG_ACTIVE LEONOS_NET_CONFIG_FLAG_ACTIVE
#define NET_SERVICE_CONFIG_FLAG_DHCP LEONOS_NET_CONFIG_FLAG_DHCP

#define NET_SERVICE_DEFAULT_TIMEOUT_MS LEONOS_NET_DEFAULT_TIMEOUT_MS
#define NET_SERVICE_HOSTNAME_LEN LEONOS_NET_HOSTNAME_LEN
#define NET_SERVICE_HTTP_PATH_LEN LEONOS_NET_HTTP_PATH_LEN
#define NET_SERVICE_SOCKET_MAX LEONOS_NET_SOCKET_MAX

int net_service_config(net_service_config_t *config);
int net_service_get_dns_policy(net_service_dns_policy_t *result);
int net_service_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                               net_service_dns_policy_t *result);
int net_service_dhcp_renew(uint32_t timeout_ms, net_service_dhcp_t *result);
int net_service_ping(uint32_t target_ip, uint32_t timeout_ms,
                     net_service_ping_t *result);
int net_service_dns_resolve(const char *name, uint32_t timeout_ms,
                            net_service_dns_t *result);
int net_service_connections(net_service_connection_info_t *entries,
                            uint32_t capacity, uint32_t *out_count);

#endif
