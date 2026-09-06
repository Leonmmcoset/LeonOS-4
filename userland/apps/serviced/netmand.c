/* netmand, hosted by serviced: network management plane over
 * /run/leonos/net.sock. SO_PEERCRED is the connection trust boundary.
 * Data traffic uses AF_INET sockets and never enters this control path. */
#include <errno.h>
#include <leonos/fs.h>
#include <leonos/net.h>
#include <leonos/netmand.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "netmand.h"

#define NETMAND_MAX_CLIENTS 16u
#define NETMAND_FRAME_CAP 4096u
#define NETMAND_RESOLV_PATH "/etc/resolv.conf"
#define NETMAND_NETCONF_PATH "/etc/net.conf"

struct netmand_client {
    uint32_t used;
    int fd;
    uint32_t pid;
    uint32_t uid;
};

static struct netmand_client clients[NETMAND_MAX_CLIENTS];
static int listen_fd = -1;
static uint32_t dns_mode = LEONOS_NET_DNS_MODE_CLOUDFLARE;
static uint32_t custom_dns_ip = LEONOS_NET_CLOUDFLARE_DNS_IP;

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

static uint32_t net_text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

static int net_text_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

static void net_append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1u < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void net_append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (!value) { net_append_char(dst, pos, cap, '0'); return; }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (n) net_append_char(dst, pos, cap, tmp[--n]);
}

static void net_append_ipv4(char *dst, uint32_t *pos, uint32_t cap, uint32_t ip)
{
    net_append_u32(dst, pos, cap, (ip >> 24) & 0xffu);
    net_append_char(dst, pos, cap, '.');
    net_append_u32(dst, pos, cap, (ip >> 16) & 0xffu);
    net_append_char(dst, pos, cap, '.');
    net_append_u32(dst, pos, cap, (ip >> 8) & 0xffu);
    net_append_char(dst, pos, cap, '.');
    net_append_u32(dst, pos, cap, ip & 0xffu);
}

static uint32_t net_parse_ipv4(const char *text)
{
    uint32_t host = 0;
    if (!text) return 0;
    for (uint32_t octet = 0; octet < 4u; ++octet) {
        uint32_t value = 0;
        uint32_t digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) return 0;
            ++text; ++digits;
        }
        if (!digits) return 0;
        host = (host << 8) | value;
        if (octet != 3u) {
            if (*text != '.') return 0;
            ++text;
        }
    }
    return *text ? 0 : host;
}

static int net_read_file(const char *path, char *buffer, uint32_t capacity)
{
    int fd;
    uint32_t len = 0;
    if (!buffer || !capacity) return -1;
    buffer[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) return fd;
    while (len + 1u < capacity) {
        long got = read(fd, buffer + len, capacity - len - 1u);
        if (got < 0) { close(fd); return (int)got; }
        if (!got) break;
        len += (uint32_t)got;
    }
    close(fd);
    buffer[len] = 0;
    return 0;
}

static int net_write_file(const char *path, const char *text)
{
    int fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    uint32_t len;
    if (fd < 0) return fd;
    len = net_text_len(text);
    {
        long wrote = write(fd, text, len);
        close(fd);
        return wrote == (long)len ? 0 : -1;
    }
}

static void net_load_dns_policy(void)
{
    char resolv[512];
    char line[64];
    uint32_t len = 0;
    uint32_t line_len = 0;
    if (net_read_file(NETMAND_RESOLV_PATH, resolv, sizeof(resolv)) < 0) return;
    for (uint32_t i = 0; i < sizeof(resolv) && resolv[i]; ++i) {
        if (resolv[i] != '\\n' && line_len + 1u < sizeof(line)) {
            line[line_len++] = resolv[i];
            continue;
        }
        line[line_len] = 0;
        if (line_len >= 10u && line[0] == 'n' && line[1] == 'a' &&
            line[2] == 'm' && line[3] == 'e') {
            char *addr = line + 10;
            while (*addr == ' ' || *addr == '\\t') ++addr;
            custom_dns_ip = net_parse_ipv4(addr);
            if (custom_dns_ip) dns_mode = LEONOS_NET_DNS_MODE_CUSTOM;
            break;
        }
        line_len = 0;
    }
    (void)len;
}

static void net_save_dns_policy(void)
{
    char text[128];
    uint32_t pos = 0;
    text[0] = 0;
    net_append_char(text, &pos, sizeof(text), 'n');
    net_append_char(text, &pos, sizeof(text), 'a');
    net_append_char(text, &pos, sizeof(text), 'm');
    net_append_char(text, &pos, sizeof(text), 'e');
    net_append_char(text, &pos, sizeof(text), 's');
    net_append_char(text, &pos, sizeof(text), 'e');
    net_append_char(text, &pos, sizeof(text), 'r');
    net_append_char(text, &pos, sizeof(text), 'v');
    net_append_char(text, &pos, sizeof(text), 'e');
    net_append_char(text, &pos, sizeof(text), 'r');
    net_append_char(text, &pos, sizeof(text), ' ');
    if (dns_mode == LEONOS_NET_DNS_MODE_DHCP) {
        net_append_ipv4(text, &pos, sizeof(text), LEONOS_NET_DEFAULT_DNS_IP);
    } else if (dns_mode == LEONOS_NET_DNS_MODE_CUSTOM && custom_dns_ip) {
        net_append_ipv4(text, &pos, sizeof(text), custom_dns_ip);
    } else {
        net_append_ipv4(text, &pos, sizeof(text), LEONOS_NET_CLOUDFLARE_DNS_IP);
    }
    net_append_char(text, &pos, sizeof(text), '\\n');
    (void)net_write_file(NETMAND_RESOLV_PATH, text);
}

static void net_fill_config(struct leonos_net_config *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->flags = LEONOS_NET_CONFIG_FLAG_PRESENT;
    config->source = LEONOS_NET_CONFIG_SOURCE_STATIC;
    config->local_ip = LEONOS_NET_DEFAULT_LOCAL_IP;
    config->subnet_mask = LEONOS_NET_DEFAULT_SUBNET_MASK;
    config->gateway_ip = LEONOS_NET_DEFAULT_GATEWAY_IP;
    config->dns_ip = dns_mode == LEONOS_NET_DNS_MODE_CUSTOM ? custom_dns_ip
                                                             : LEONOS_NET_CLOUDFLARE_DNS_IP;
    config->mac[0] = 0x02;
    config->mac[1] = 0x4c;
    config->mac[2] = 0x4e;
}

static void net_handle_client(int slot)
{
    struct netmand_client *client = &clients[slot];
    uint8_t buffer[NETMAND_FRAME_CAP];
    uint32_t type = 0;
    uint32_t length = 0;
    for (;;) {
        struct pollfd descriptor = {.fd = client->fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) <= 0) return;
        if (leonos_ipc_recv(client->fd, &type, buffer, sizeof(buffer), &length) < 0) {
            if (errno == EAGAIN) return;
            close(client->fd);
            memset(client, 0, sizeof(*client));
            client->fd = -1;
            return;
        }
        if (type == LEONOS_NET_MSG_HELLO) {
            struct leonos_netmand_hello hello;
            struct leonos_netmand_ack ack = {.code = 1};
            if (length < sizeof(hello)) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            memcpy(&hello, buffer, sizeof(hello));
            if (hello.pid != client->pid) { close(client->fd); memset(client,0,sizeof(*client)); client->fd=-1; return; }
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_ACK, &ack, sizeof(ack));
            continue;
        }
        if (type == LEONOS_NET_MSG_CONFIG) {
            struct leonos_net_config config;
            net_fill_config(&config);
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_CONFIG,
                                  &config, sizeof(config));
            continue;
        }
        if (type == LEONOS_NET_MSG_DNS_POLICY) {
            struct leonos_net_dns_policy request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            if (request.mode != LEONOS_NET_DNS_MODE_QUERY) {
                if (request.mode <= LEONOS_NET_DNS_MODE_CUSTOM) {
                    dns_mode = request.mode;
                    custom_dns_ip = request.custom_dns_ip;
                    net_save_dns_policy();
                }
                net_load_dns_policy();
            }
            request.mode = dns_mode;
            request.custom_dns_ip = custom_dns_ip;
            request.status = LEONOS_NET_STATUS_OK;
            net_fill_config(&request.config);
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_DNS_POLICY,
                                  &request, sizeof(request));
            continue;
        }
        if (type == LEONOS_NET_MSG_DHCP) {
            struct leonos_net_dhcp request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            request.status = LEONOS_NET_STATUS_NO_DEVICE;
            net_fill_config(&request.config);
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_DHCP,
                                  &request, sizeof(request));
            continue;
        }
        if (type == LEONOS_NET_MSG_PING) {
            struct leonos_net_ping request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            request.status = LEONOS_NET_STATUS_NO_DEVICE;
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_PING,
                                  &request, sizeof(request));
            continue;
        }
        if (type == LEONOS_NET_MSG_DNS) {
            struct leonos_net_dns request;
            if (length < sizeof(request)) continue;
            memcpy(&request, buffer, sizeof(request));
            request.status = LEONOS_NET_STATUS_DNS_NO_ANSWER;
            request.address_count = 0;
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_DNS,
                                  &request, sizeof(request));
            continue;
        }
        if (type == LEONOS_NET_MSG_CONNECTIONS) {
            struct leonos_netmand_connections_ack ack = {0};
            (void)leonos_ipc_send(client->fd, LEONOS_NET_MSG_CONNECTIONS,
                                  &ack, sizeof(ack));
            continue;
        }
    }
}

void netmand_poll(void)
{
    if (listen_fd < 0) {
        net_load_dns_policy();
        listen_fd = leonos_ipc_bind_listen(LEONOS_IPC_SOCK_NET, 8);
        if (listen_fd < 0) {
            printf("[netmand] bind failed errno=%d\n", errno);
            return;
        }
        (void)leonos_ipc_set_nonblock(listen_fd, 1);
        printf("[netmand] listening on %s\n", LEONOS_IPC_SOCK_NET);
    }
    {
        struct pollfd descriptor = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        if (poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN)) {
            int fd;
            while ((fd = leonos_ipc_accept(listen_fd, 0)) >= 0) {
                struct ucred credentials;
                int slot = -1;
                for (uint32_t i = 0; i < NETMAND_MAX_CLIENTS; ++i) {
                    if (!clients[i].used) { slot = (int)i; break; }
                }
                if (slot < 0 || leonos_ipc_peer_credentials(fd, &credentials) < 0) {
                    close(fd);
                    continue;
                }
                (void)leonos_ipc_set_nonblock(fd, 1);
                clients[slot].used = 1;
                clients[slot].fd = fd;
                clients[slot].pid = (uint32_t)credentials.pid;
                clients[slot].uid = credentials.uid;
            }
        }
    }
    for (uint32_t i = 0; i < NETMAND_MAX_CLIENTS; ++i) {
        if (clients[i].used) net_handle_client(i);
    }
}
