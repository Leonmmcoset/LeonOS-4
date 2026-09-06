/* libnet: netmand management client plus the standard AF_INET data plane.
 * Exports the historical leonos_net_* and leonos_socket_* entry points. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <leonos/http.h>
#include <leonos/net.h>
#include <leonos/netmand.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NET_FRAME_CAP 4096u
#define ntohs(value) ((uint16_t)((((uint16_t)(value) & 0xffu) << 8) | ((uint16_t)(value) >> 8)))
#define htons(value) ntohs(value)
#define ntohl(value) ((((uint32_t)(value) & 0xffu) << 24) | (((uint32_t)(value) & 0xff00u) << 8) | (((uint32_t)(value) & 0xff0000u) >> 8) | ((uint32_t)(value) >> 24))
#define htonl(value) ntohl(value)
#define NET_CONNECT_RETRY_MS 5000u

static int netmand_fd = -1;

static uint32_t net_now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void net_copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int net_wait_response(uint32_t expected, void *payload, uint32_t capacity,
                             uint32_t *length)
{
    uint32_t deadline = net_now_ms() + 3000u;
    for (;;) {
        uint8_t buffer[NET_FRAME_CAP];
        uint32_t type = 0;
        uint32_t got = 0;
        if (leonos_ipc_recv(netmand_fd, &type, buffer, sizeof(buffer), &got) == 0) {
            if (type == expected) {
                if (got > capacity) got = capacity;
                if (got) memcpy(payload, buffer, got);
                if (length) *length = got;
                return 0;
            }
        }
        if (net_now_ms() >= deadline) return -1;
        (void)poll(0, 0, 2);
    }
}

static int net_open(void)
{
    struct leonos_netmand_hello hello;
    struct leonos_netmand_ack ack;
    uint32_t deadline;
    if (netmand_fd >= 0) return netmand_fd;
    deadline = net_now_ms() + NET_CONNECT_RETRY_MS;
    while (netmand_fd < 0 && net_now_ms() < deadline) {
        netmand_fd = leonos_ipc_connect(LEONOS_IPC_SOCK_NET);
        if (netmand_fd < 0) (void)poll(0, 0, 10);
    }
    if (netmand_fd < 0) return -1;
    (void)leonos_ipc_set_nonblock(netmand_fd, 1);
    hello.pid = (uint32_t)getpid();
    if (leonos_ipc_send(netmand_fd, LEONOS_NET_MSG_HELLO, &hello,
                        sizeof(hello)) < 0 ||
        net_wait_response(LEONOS_NET_MSG_ACK, &ack, sizeof(ack), 0) < 0) {
        leonos_ipc_close(netmand_fd);
        netmand_fd = -1;
        return -1;
    }
    return netmand_fd;
}

static int net_request(uint32_t type, const void *request, uint32_t request_len,
                       void *response, uint32_t response_capacity)
{
    if (net_open() < 0) return -1;
    if (leonos_ipc_send(netmand_fd, type, request, request_len) < 0) return -1;
    return net_wait_response(type, response, response_capacity, 0);
}

int leonos_net_config(struct leonos_net_config *config)
{
    if (!config) { errno = EINVAL; return -1; }
    if (net_request(LEONOS_NET_MSG_CONFIG, 0, 0, config, sizeof(*config)) < 0) return -1;
    return 0;
}

int leonos_net_get_dns_policy(struct leonos_net_dns_policy *result)
{
    struct leonos_net_dns_policy query = {
        .mode = LEONOS_NET_DNS_MODE_QUERY,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    if (!result) { errno = EINVAL; return -1; }
    if (net_request(LEONOS_NET_MSG_DNS_POLICY, &query, sizeof(query),
                    result, sizeof(*result)) < 0) return -1;
    return 0;
}

int leonos_net_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                              struct leonos_net_dns_policy *result)
{
    struct leonos_net_dns_policy query = {
        .mode = mode,
        .custom_dns_ip = custom_dns_ip,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    if (!result) { errno = EINVAL; return -1; }
    if (net_request(LEONOS_NET_MSG_DNS_POLICY, &query, sizeof(query),
                    result, sizeof(*result)) < 0) return -1;
    return 0;
}

int leonos_net_dhcp_renew(uint32_t timeout_ms, struct leonos_net_dhcp *result)
{
    struct leonos_net_dhcp query = {
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_DHCP_FAILED,
    };
    if (!result) { errno = EINVAL; return -1; }
    if (net_request(LEONOS_NET_MSG_DHCP, &query, sizeof(query),
                    result, sizeof(*result)) < 0) return -1;
    return 0;
}

int leonos_net_ping(uint32_t target_ip, uint32_t timeout_ms,
                    struct leonos_net_ping *result)
{
    struct leonos_net_ping query = {
        .target_ip = target_ip,
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    if (!result) { errno = EINVAL; return -1; }
    if (net_request(LEONOS_NET_MSG_PING, &query, sizeof(query),
                    result, sizeof(*result)) < 0) return -1;
    return 0;
}

int leonos_net_dns_resolve(const char *name, uint32_t timeout_ms,
                           struct leonos_net_dns *result)
{
    struct leonos_net_dns query;
    if (!name || !result) { errno = EINVAL; return -1; }
    memset(&query, 0, sizeof(query));
    query.timeout_ms = timeout_ms;
    query.status = LEONOS_NET_STATUS_DNS_FAILED;
    net_copy_text(query.name, sizeof(query.name), name);
    if (net_request(LEONOS_NET_MSG_DNS, &query, sizeof(query),
                    result, sizeof(*result)) < 0) return -1;
    return 0;
}

int leonos_net_connections(struct leonos_net_connection_info *entries,
                           uint32_t capacity, uint32_t *out_count)
{
    struct leonos_netmand_connections_ack ack;
    uint8_t buffer[NET_FRAME_CAP];
    uint32_t length = 0;
    if (out_count) *out_count = 0;
    if (net_open() < 0) return -1;
    if (leonos_ipc_send(netmand_fd, LEONOS_NET_MSG_CONNECTIONS, 0, 0) < 0) return -1;
    if (net_wait_response(LEONOS_NET_MSG_CONNECTIONS, buffer, sizeof(buffer),
                          &length) < 0) return -1;
    if (length < sizeof(ack)) return -1;
    memcpy(&ack, buffer, sizeof(ack));
    if (out_count) *out_count = ack.count;
    if (entries && capacity) {
        uint32_t count = ack.count < capacity ? ack.count : capacity;
        if (length - sizeof(ack) >= count * sizeof(*entries)) {
            memcpy(entries, buffer + sizeof(ack), count * sizeof(*entries));
        }
    }
    return 0;
}

static int parse_ipv4_literal(const char *text, uint32_t *network_value)
{
    uint32_t host = 0;
    if (!text || !network_value) return 0;
    for (uint32_t octet = 0; octet < 4u; ++octet) {
        uint32_t value = 0;
        uint32_t digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) return 0;
            ++text;
            ++digits;
        }
        if (!digits) return 0;
        host = (host << 8) | value;
        if (octet != 3u) {
            if (*text != '.') return 0;
            ++text;
        }
    }
    if (*text) return 0;
    *network_value = ((host & 0xffu) << 24) | ((host & 0xff00u) << 8) |
                     ((host & 0xff0000u) >> 8) | ((host & 0xff000000u) >> 24);
    return 1;
}

int leonos_socket_tcp(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    return fd;
}

int leonos_socket_connect(int socket_fd, const char *host, uint32_t port,
                          uint32_t timeout_ms,
                          struct leonos_net_socket_connect *result)
{
    struct sockaddr_in address;
    uint32_t network_ip = 0;
    if (!host || !result || socket_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    result->socket = socket_fd;
    result->port = port;
    result->timeout_ms = timeout_ms;
    result->status = LEONOS_NET_STATUS_TCP_FAILED;
    net_copy_text(result->host, sizeof(result->host), host);
    if (!parse_ipv4_literal(host, &network_ip)) {
        struct leonos_net_dns dns;
        uint32_t i = 0;
        if (leonos_net_dns_resolve(host, timeout_ms, &dns) < 0 ||
            dns.status != LEONOS_NET_STATUS_OK || dns.address_count == 0) {
            result->status = dns.status ? dns.status : LEONOS_NET_STATUS_DNS_FAILED;
            return -1;
        }
        network_ip = dns.addresses[0];
        (void)i;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = network_ip;
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        result->status = LEONOS_NET_STATUS_TCP_FAILED;
        return -1;
    }
    result->status = LEONOS_NET_STATUS_OK;
    result->remote_ip = ntohl(network_ip);
    return 0;
}

long leonos_socket_send(int socket_fd, const void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status)
{
    ssize_t sent;
    (void)timeout_ms;
    if (status) *status = LEONOS_NET_STATUS_TCP_FAILED;
    if (length && !buffer) {
        if (status) *status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return -1;
    }
    sent = send(socket_fd, buffer, length, 0);
    if (sent < 0) {
        if (status) *status = LEONOS_NET_STATUS_TCP_FAILED;
        return -1;
    }
    if (status) *status = LEONOS_NET_STATUS_OK;
    return (long)sent;
}

long leonos_socket_recv(int socket_fd, void *buffer, uint32_t length,
                        uint32_t timeout_ms, uint32_t *status)
{
    ssize_t got;
    (void)timeout_ms;
    if (status) *status = LEONOS_NET_STATUS_TCP_FAILED;
    if (length && !buffer) {
        if (status) *status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return -1;
    }
    got = recv(socket_fd, buffer, length, 0);
    if (got < 0) {
        if (status) *status = LEONOS_NET_STATUS_TCP_FAILED;
        return -1;
    }
    if (status) *status = LEONOS_NET_STATUS_OK;
    return (long)got;
}

int leonos_socket_close(int socket_fd)
{
    return close(socket_fd);
}

int leonos_net_http_get(const char *host, const char *path, uint32_t port,
                        uint32_t timeout_ms, struct leonos_net_http_get *result)
{
    char url[LEONOS_NET_HOSTNAME_LEN + LEONOS_NET_HTTP_PATH_LEN + 16];
    struct leonos_http_response response;
    memset(url, 0, sizeof(url));
    (void)snprintf(url, sizeof(url), "http://%s:%u%s", host, port ? port : 80,
                   path && path[0] ? path : "/");
    memset(&response, 0, sizeof(response));
    if (leonos_http_get(url, timeout_ms, result->response,
                        sizeof(result->response), 0, 0, &response) < 0) {
        return -1;
    }
    result->status = response.net_status;
    result->http_status = response.http_status;
    result->response_len = response.body_len < sizeof(result->response)
                               ? response.body_len
                               : sizeof(result->response) - 1u;
    result->response[result->response_len] = 0;
    return response.net_status == LEONOS_NET_STATUS_OK ? 0 : -1;
}
