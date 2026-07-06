#include <ntclks/console.h>
#include <ntclks/e1000.h>
#include <ntclks/net.h>
#include <ntclks/time.h>

#include <leonos/device.h>

#define ETH_TYPE_IPV4 0x0800u
#define ETH_TYPE_ARP 0x0806u
#define ARP_HTYPE_ETHERNET 0x0001u
#define ARP_OPER_REQUEST 0x0001u
#define ARP_OPER_REPLY 0x0002u
#define IPV4_PROTO_ICMP 1u
#define IPV4_PROTO_TCP 6u
#define IPV4_PROTO_UDP 17u
#define ICMP_ECHO_REPLY 0u
#define ICMP_ECHO_REQUEST 8u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define NET_FRAME_MAX 1536u
#define NET_ICMP_PAYLOAD_LEN 16u
#define NET_DHCP_PACKET_MAX 548u
#define NET_DHCP_PACKET_LEN 300u
#define NET_DNS_PACKET_MAX 512u
#define NET_HTTP_REQUEST_MAX 640u
#define NET_DHCP_CLIENT_PORT 68u
#define NET_DHCP_SERVER_PORT 67u
#define NET_DNS_PORT 53u
#define NET_HTTP_PORT 80u
#define NET_DHCP_MAGIC 0x63825363u
#define NET_DHCP_DISCOVER 1u
#define NET_DHCP_OFFER 2u
#define NET_DHCP_REQUEST 3u
#define NET_DHCP_ACK 5u
#define NET_DHCP_NAK 6u
#define NET_BOOT_DHCP_ATTEMPTS 3u
#define NET_BOOT_DHCP_TIMEOUT_MS 4000u
#define NET_TCP_MSS 1460u
#define NET_TCP_SYN_RETRANSMIT_MS 500u
#define NET_TCP_DATA_RETRANSMIT_MS 750u

struct net_arp_wait {
    uint32_t ip;
    uint8_t mac[6];
    uint32_t done;
};

struct net_ping_wait {
    uint32_t target_ip;
    uint16_t ident;
    uint16_t sequence;
    uint32_t done;
};

struct net_udp_wait {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t *payload;
    uint32_t capacity;
    uint32_t length;
    uint32_t done;
};

struct net_tcp_wait {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t remote_seq;
    uint32_t acked_seq;
    uint8_t *payload;
    uint32_t capacity;
    uint32_t length;
    uint32_t flags;
    uint32_t reset;
    uint32_t fin;
    uint32_t overflow;
    uint32_t changed;
};

struct net_dhcp_offer {
    uint32_t msg_type;
    uint32_t yiaddr;
    uint32_t subnet_mask;
    uint32_t router_ip;
    uint32_t dns_ip;
    uint32_t server_ip;
    uint32_t lease_seconds;
};

static const uint8_t net_broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint32_t net_sequence = 1;
static uint16_t net_ipv4_id = 1;
static struct leonos_net_config net_config;

static void net_cpu_relax(void)
{
    __asm__ volatile("pause");
}

static void net_memcpy(void *dst, const void *src, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (len--) {
        *d++ = *s++;
    }
}

static void net_memzero(void *dst, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    while (len--) {
        *d++ = 0;
    }
}

static uint32_t net_strlen(const char *text, uint32_t cap)
{
    uint32_t len = 0;
    while (text && len < cap && text[len]) {
        ++len;
    }
    return len;
}

static int net_memeq(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t net_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t net_get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void net_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void net_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int net_tcp_seq_after_or_equal(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static uint16_t net_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += net_get_u16(data);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t)data[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint32_t net_checksum_partial(uint32_t sum, const uint8_t *data,
                                     uint32_t len)
{
    while (len > 1) {
        sum += net_get_u16(data);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t)data[0] << 8;
    }
    return sum;
}

static uint16_t net_checksum_finish(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t net_tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                                 const uint8_t *tcp, uint16_t tcp_len)
{
    uint8_t pseudo[12];
    uint32_t sum = 0;
    net_put_u32(pseudo, src_ip);
    net_put_u32(pseudo + 4, dst_ip);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_TCP;
    net_put_u16(pseudo + 10, tcp_len);
    sum = net_checksum_partial(sum, pseudo, sizeof(pseudo));
    sum = net_checksum_partial(sum, tcp, tcp_len);
    return net_checksum_finish(sum);
}

static void net_append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1u < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void net_append_text(char *dst, uint32_t *pos, uint32_t cap,
                            const char *text)
{
    while (text && *text) {
        net_append_char(dst, pos, cap, *text++);
    }
}

static void net_append_u32(char *dst, uint32_t *pos, uint32_t cap,
                           uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        net_append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) {
        net_append_char(dst, pos, cap, tmp[--n]);
    }
}

static int net_timeout_expired(uint64_t start_ms, uint32_t timeout_ms,
                               uint32_t spins)
{
    uint64_t now = time_uptime_ms();
    uint32_t spin_limit = timeout_ms * 2000u + 100000u;
    if (now != start_ms) {
        return now - start_ms >= timeout_ms;
    }
    return spins >= spin_limit;
}

static void net_update_config_flags(void)
{
    uint32_t flags = 0;
    if (e1000_is_ready()) {
        flags |= LEONOS_NET_CONFIG_FLAG_PRESENT | LEONOS_NET_CONFIG_FLAG_ACTIVE;
    }
    if (net_config.source == LEONOS_NET_CONFIG_SOURCE_DHCP) {
        flags |= LEONOS_NET_CONFIG_FLAG_DHCP;
    }
    net_config.flags = flags;
    net_memzero(net_config.mac, sizeof(net_config.mac));
    if (e1000_is_ready()) {
        net_memcpy(net_config.mac, e1000_mac(), 6);
    }
}

static void net_set_static_fallback(void)
{
    net_config = (struct leonos_net_config){
        .flags = 0,
        .source = LEONOS_NET_CONFIG_SOURCE_STATIC,
        .local_ip = LEONOS_NET_DEFAULT_LOCAL_IP,
        .subnet_mask = LEONOS_NET_DEFAULT_SUBNET_MASK,
        .gateway_ip = LEONOS_NET_DEFAULT_GATEWAY_IP,
        .dns_ip = LEONOS_NET_DEFAULT_DNS_IP,
        .dhcp_server_ip = 0,
        .lease_seconds = 0,
    };
    net_update_config_flags();
}

static void net_apply_dhcp_offer(const struct net_dhcp_offer *offer)
{
    net_config.local_ip = offer->yiaddr;
    net_config.subnet_mask = offer->subnet_mask ? offer->subnet_mask : LEONOS_NET_DEFAULT_SUBNET_MASK;
    net_config.gateway_ip = offer->router_ip ? offer->router_ip : LEONOS_NET_DEFAULT_GATEWAY_IP;
    net_config.dns_ip = offer->dns_ip ? offer->dns_ip : LEONOS_NET_DEFAULT_DNS_IP;
    net_config.dhcp_server_ip = offer->server_ip;
    net_config.lease_seconds = offer->lease_seconds;
    net_config.source = LEONOS_NET_CONFIG_SOURCE_DHCP;
    net_update_config_flags();
}

static void net_write_eth(uint8_t *frame, const uint8_t *dst_mac,
                          uint16_t type)
{
    const uint8_t *src_mac = e1000_mac();
    net_memcpy(frame, dst_mac, 6);
    net_memcpy(frame + 6, src_mac, 6);
    net_put_u16(frame + 12, type);
}

static void net_write_arp_ipv4(uint8_t *frame, uint16_t op,
                               const uint8_t *dst_eth_mac,
                               const uint8_t *target_mac,
                               uint32_t target_ip)
{
    const uint8_t *src_mac = e1000_mac();
    net_write_eth(frame, dst_eth_mac, ETH_TYPE_ARP);
    net_put_u16(frame + 14, ARP_HTYPE_ETHERNET);
    net_put_u16(frame + 16, ETH_TYPE_IPV4);
    frame[18] = 6;
    frame[19] = 4;
    net_put_u16(frame + 20, op);
    net_memcpy(frame + 22, src_mac, 6);
    net_put_u32(frame + 28, net_config.local_ip);
    net_memcpy(frame + 32, target_mac, 6);
    net_put_u32(frame + 38, target_ip);
}

static int net_send_arp_request(uint32_t target_ip)
{
    uint8_t frame[64];
    uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    net_memzero(frame, sizeof(frame));
    net_write_arp_ipv4(frame, ARP_OPER_REQUEST, net_broadcast_mac,
                       zero_mac, target_ip);
    return e1000_send(frame, 42);
}

static int net_send_arp_reply(const uint8_t *target_mac, uint32_t target_ip)
{
    uint8_t frame[64];
    net_memzero(frame, sizeof(frame));
    net_write_arp_ipv4(frame, ARP_OPER_REPLY, target_mac, target_mac, target_ip);
    return e1000_send(frame, 42);
}

static uint32_t net_route_arp_ip(uint32_t target_ip)
{
    if ((target_ip & net_config.subnet_mask) ==
        (net_config.local_ip & net_config.subnet_mask)) {
        return target_ip;
    }
    return net_config.gateway_ip ? net_config.gateway_ip : target_ip;
}

static int net_frame_for_us(const uint8_t *frame)
{
    return net_memeq(frame, e1000_mac(), 6) ||
           net_memeq(frame, net_broadcast_mac, 6);
}

static int net_ip_for_us(uint32_t dst_ip)
{
    return dst_ip == net_config.local_ip ||
           dst_ip == 0xffffffffu ||
           dst_ip == 0;
}

static int net_send_udp_to_mac(const uint8_t *dst_mac, uint32_t src_ip,
                               uint32_t dst_ip, uint16_t src_port,
                               uint16_t dst_port, const uint8_t *payload,
                               uint32_t payload_len)
{
    uint8_t frame[NET_FRAME_MAX];
    uint8_t *ip = frame + 14;
    uint8_t *udp = frame + 34;
    uint16_t udp_len;
    uint16_t total_len;
    if (!payload || payload_len > NET_FRAME_MAX - 42u) {
        return -1;
    }
    udp_len = (uint16_t)(8u + payload_len);
    total_len = (uint16_t)(20u + udp_len);
    net_memzero(frame, 42u + payload_len);
    net_write_eth(frame, dst_mac, ETH_TYPE_IPV4);
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_u16(ip + 2, total_len);
    net_put_u16(ip + 4, net_ipv4_id++);
    net_put_u16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = IPV4_PROTO_UDP;
    net_put_u32(ip + 12, src_ip);
    net_put_u32(ip + 16, dst_ip);
    net_put_u16(ip + 10, net_checksum(ip, 20));
    net_put_u16(udp, src_port);
    net_put_u16(udp + 2, dst_port);
    net_put_u16(udp + 4, udp_len);
    net_put_u16(udp + 6, 0);
    net_memcpy(udp + 8, payload, payload_len);
    return e1000_send(frame, 42u + payload_len);
}

static int net_send_tcp_to_mac(const uint8_t *dst_mac, uint32_t src_ip,
                               uint32_t dst_ip, uint16_t src_port,
                               uint16_t dst_port, uint32_t seq,
                               uint32_t ack, uint8_t flags,
                               const uint8_t *payload,
                               uint32_t payload_len)
{
    uint8_t frame[NET_FRAME_MAX];
    uint8_t *ip = frame + 14;
    uint8_t *tcp = frame + 34;
    uint32_t tcp_header_len = (flags & TCP_FLAG_SYN) ? 24u : 20u;
    uint16_t tcp_len;
    uint16_t total_len;
    if (payload_len > NET_FRAME_MAX - 34u - tcp_header_len ||
        (payload_len && !payload)) {
        return -1;
    }
    tcp_len = (uint16_t)(tcp_header_len + payload_len);
    total_len = (uint16_t)(20u + tcp_len);
    net_memzero(frame, 34u + tcp_header_len + payload_len);
    net_write_eth(frame, dst_mac, ETH_TYPE_IPV4);
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_u16(ip + 2, total_len);
    net_put_u16(ip + 4, net_ipv4_id++);
    net_put_u16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = IPV4_PROTO_TCP;
    net_put_u32(ip + 12, src_ip);
    net_put_u32(ip + 16, dst_ip);
    net_put_u16(ip + 10, net_checksum(ip, 20));

    net_put_u16(tcp, src_port);
    net_put_u16(tcp + 2, dst_port);
    net_put_u32(tcp + 4, seq);
    net_put_u32(tcp + 8, ack);
    tcp[12] = (uint8_t)((tcp_header_len / 4u) << 4);
    tcp[13] = flags;
    net_put_u16(tcp + 14, 4096);
    net_put_u16(tcp + 16, 0);
    net_put_u16(tcp + 18, 0);
    if (flags & TCP_FLAG_SYN) {
        tcp[20] = 2;
        tcp[21] = 4;
        net_put_u16(tcp + 22, NET_TCP_MSS);
    }
    if (payload_len) {
        net_memcpy(tcp + tcp_header_len, payload, payload_len);
    }
    net_put_u16(tcp + 16, net_tcp_checksum(src_ip, dst_ip, tcp, tcp_len));
    return e1000_send(frame, 14u + total_len);
}

static void net_handle_arp(const uint8_t *frame, uint32_t len,
                           struct net_arp_wait *arp_wait)
{
    uint16_t op;
    uint32_t sender_ip;
    uint32_t target_ip;
    const uint8_t *sender_mac;
    if (len < 42 ||
        net_get_u16(frame + 14) != ARP_HTYPE_ETHERNET ||
        net_get_u16(frame + 16) != ETH_TYPE_IPV4 ||
        frame[18] != 6 || frame[19] != 4) {
        return;
    }
    op = net_get_u16(frame + 20);
    sender_mac = frame + 22;
    sender_ip = net_get_u32(frame + 28);
    target_ip = net_get_u32(frame + 38);
    if (op == ARP_OPER_REQUEST && target_ip == net_config.local_ip) {
        (void)net_send_arp_reply(sender_mac, sender_ip);
        return;
    }
    if (op == ARP_OPER_REPLY && arp_wait && sender_ip == arp_wait->ip) {
        net_memcpy(arp_wait->mac, sender_mac, 6);
        arp_wait->done = 1;
    }
}

static int net_send_icmp_echo_request(const uint8_t *dst_mac, uint32_t target_ip,
                                      uint16_t ident, uint16_t sequence)
{
    uint8_t frame[98];
    uint8_t *ip = frame + 14;
    uint8_t *icmp = frame + 34;
    uint16_t icmp_len = 8u + NET_ICMP_PAYLOAD_LEN;
    uint16_t total_len = 20u + icmp_len;

    net_memzero(frame, sizeof(frame));
    net_write_eth(frame, dst_mac, ETH_TYPE_IPV4);
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_u16(ip + 2, total_len);
    net_put_u16(ip + 4, net_ipv4_id++);
    net_put_u16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = IPV4_PROTO_ICMP;
    net_put_u32(ip + 12, net_config.local_ip);
    net_put_u32(ip + 16, target_ip);
    net_put_u16(ip + 10, net_checksum(ip, 20));

    icmp[0] = ICMP_ECHO_REQUEST;
    icmp[1] = 0;
    net_put_u16(icmp + 4, ident);
    net_put_u16(icmp + 6, sequence);
    for (uint32_t i = 0; i < NET_ICMP_PAYLOAD_LEN; ++i) {
        icmp[8 + i] = (uint8_t)('A' + (i % 26));
    }
    net_put_u16(icmp + 2, net_checksum(icmp, icmp_len));
    return e1000_send(frame, 14u + total_len);
}

static void net_handle_udp(const uint8_t *ip, uint32_t total_len,
                           uint32_t ihl, uint32_t src_ip,
                           struct net_udp_wait *udp_wait)
{
    const uint8_t *udp;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t udp_len;
    uint32_t payload_len;
    if (!udp_wait || udp_wait->done || total_len < ihl + 8u) {
        return;
    }
    udp = ip + ihl;
    src_port = net_get_u16(udp);
    dst_port = net_get_u16(udp + 2);
    udp_len = net_get_u16(udp + 4);
    if (udp_len < 8u || ihl + udp_len > total_len) {
        return;
    }
    if (udp_wait->src_port && src_port != udp_wait->src_port) {
        return;
    }
    if (udp_wait->dst_port && dst_port != udp_wait->dst_port) {
        return;
    }
    if (udp_wait->src_ip && src_ip != udp_wait->src_ip) {
        return;
    }
    payload_len = udp_len - 8u;
    if (payload_len > udp_wait->capacity) {
        payload_len = udp_wait->capacity;
    }
    net_memcpy(udp_wait->payload, udp + 8, payload_len);
    udp_wait->length = payload_len;
    udp_wait->done = 1;
}

static void net_handle_tcp(const uint8_t *ip, uint32_t total_len,
                           uint32_t ihl, uint32_t src_ip,
                           struct net_tcp_wait *tcp_wait)
{
    const uint8_t *tcp;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint32_t tcp_len;
    uint32_t hdr_len;
    uint32_t payload_len;
    uint8_t flags;
    if (!tcp_wait || total_len < ihl + 20u) {
        return;
    }
    tcp = ip + ihl;
    tcp_len = total_len - ihl;
    src_port = net_get_u16(tcp);
    dst_port = net_get_u16(tcp + 2);
    if (tcp_wait->src_port && src_port != tcp_wait->src_port) {
        return;
    }
    if (tcp_wait->dst_port && dst_port != tcp_wait->dst_port) {
        return;
    }
    if (tcp_wait->src_ip && src_ip != tcp_wait->src_ip) {
        return;
    }
    hdr_len = (uint32_t)(tcp[12] >> 4) * 4u;
    if (hdr_len < 20u || hdr_len > tcp_len) {
        return;
    }
    flags = tcp[13];
    seq = net_get_u32(tcp + 4);
    ack = net_get_u32(tcp + 8);
    payload_len = tcp_len - hdr_len;
    tcp_wait->flags |= flags;
    if (flags & TCP_FLAG_ACK) {
        if (!tcp_wait->acked_seq ||
            net_tcp_seq_after_or_equal(ack, tcp_wait->acked_seq)) {
            tcp_wait->acked_seq = ack;
        }
    }
    if (flags & TCP_FLAG_RST) {
        tcp_wait->reset = 1;
        ++tcp_wait->changed;
        return;
    }
    if (flags & TCP_FLAG_SYN) {
        tcp_wait->remote_seq = seq + 1u;
        ++tcp_wait->changed;
    }
    if (payload_len) {
        if (tcp_wait->remote_seq == 0 || seq == tcp_wait->remote_seq) {
            uint32_t copy_len = payload_len;
            if (copy_len > tcp_wait->capacity - tcp_wait->length) {
                copy_len = tcp_wait->capacity - tcp_wait->length;
                tcp_wait->overflow = 1;
            }
            if (copy_len && tcp_wait->payload) {
                net_memcpy(tcp_wait->payload + tcp_wait->length,
                           tcp + hdr_len, copy_len);
                tcp_wait->length += copy_len;
            }
            tcp_wait->remote_seq = seq + payload_len;
            ++tcp_wait->changed;
        } else if (seq + payload_len > tcp_wait->remote_seq) {
            tcp_wait->overflow = 1;
        }
    }
    if (flags & TCP_FLAG_FIN) {
        if (tcp_wait->remote_seq == 0 ||
            seq + payload_len == tcp_wait->remote_seq) {
            ++tcp_wait->remote_seq;
            tcp_wait->fin = 1;
            ++tcp_wait->changed;
        }
    }
}

static void net_handle_ipv4(const uint8_t *frame, uint32_t len,
                            struct net_ping_wait *ping_wait,
                            struct net_udp_wait *udp_wait,
                            struct net_tcp_wait *tcp_wait)
{
    const uint8_t *ip;
    const uint8_t *icmp;
    uint32_t ihl;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t total_len;
    uint16_t icmp_len;
    if (len < 34) {
        return;
    }
    ip = frame + 14;
    if ((ip[0] >> 4) != 4) {
        return;
    }
    ihl = (uint32_t)(ip[0] & 0x0fu) * 4u;
    if (ihl < 20 || len < 14u + ihl) {
        return;
    }
    total_len = net_get_u16(ip + 2);
    if (total_len < ihl || 14u + total_len > len) {
        return;
    }
    dst_ip = net_get_u32(ip + 16);
    if (!net_ip_for_us(dst_ip)) {
        return;
    }
    src_ip = net_get_u32(ip + 12);
    if (ip[9] == IPV4_PROTO_UDP) {
        net_handle_udp(ip, total_len, ihl, src_ip, udp_wait);
        return;
    }
    if (ip[9] == IPV4_PROTO_TCP) {
        net_handle_tcp(ip, total_len, ihl, src_ip, tcp_wait);
        return;
    }
    if (ip[9] != IPV4_PROTO_ICMP || total_len < ihl + 8u) {
        return;
    }
    icmp = ip + ihl;
    icmp_len = (uint16_t)(total_len - ihl);
    if (icmp_len < 8) {
        return;
    }
    if (icmp[0] == ICMP_ECHO_REPLY && ping_wait &&
        src_ip == ping_wait->target_ip &&
        net_get_u16(icmp + 4) == ping_wait->ident &&
        net_get_u16(icmp + 6) == ping_wait->sequence) {
        ping_wait->done = 1;
    }
}

static void net_process_frame(const uint8_t *frame, uint32_t len,
                              struct net_arp_wait *arp_wait,
                              struct net_ping_wait *ping_wait,
                              struct net_udp_wait *udp_wait,
                              struct net_tcp_wait *tcp_wait)
{
    uint16_t type;
    if (len < 14 || !net_frame_for_us(frame)) {
        return;
    }
    type = net_get_u16(frame + 12);
    if (type == ETH_TYPE_ARP) {
        net_handle_arp(frame, len, arp_wait);
    } else if (type == ETH_TYPE_IPV4) {
        net_handle_ipv4(frame, len, ping_wait, udp_wait, tcp_wait);
    }
}

static void net_poll_once(struct net_arp_wait *arp_wait,
                          struct net_ping_wait *ping_wait,
                          struct net_udp_wait *udp_wait,
                          struct net_tcp_wait *tcp_wait)
{
    uint8_t frame[NET_FRAME_MAX];
    uint32_t len = 0;
    int ret = e1000_poll(frame, sizeof(frame), &len);
    if (ret > 0 && len) {
        net_process_frame(frame, len, arp_wait, ping_wait, udp_wait, tcp_wait);
    }
}

static int net_resolve_mac(uint32_t ip, uint32_t timeout_ms, uint8_t *out_mac)
{
    struct net_arp_wait wait;
    uint64_t start = time_uptime_ms();
    uint64_t next_request = start;
    uint32_t spins = 0;
    wait = (struct net_arp_wait){0};
    wait.ip = ip;
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        uint64_t now = time_uptime_ms();
        if (now >= next_request) {
            if (net_send_arp_request(ip) < 0) {
                return -1;
            }
            next_request = now + 250u;
        }
        net_poll_once(&wait, 0, 0, 0);
        if (wait.done) {
            net_memcpy(out_mac, wait.mac, 6);
            return 0;
        }
        net_cpu_relax();
    }
    return -2;
}

static uint32_t net_dhcp_add_option(uint8_t *payload, uint32_t pos,
                                    uint32_t cap, uint8_t code,
                                    const uint8_t *data, uint8_t len)
{
    if (pos + 2u + len > cap) {
        return pos;
    }
    payload[pos++] = code;
    payload[pos++] = len;
    net_memcpy(payload + pos, data, len);
    return pos + len;
}

static uint32_t net_dhcp_add_u8(uint8_t *payload, uint32_t pos,
                                uint32_t cap, uint8_t code, uint8_t value)
{
    return net_dhcp_add_option(payload, pos, cap, code, &value, 1);
}

static uint32_t net_dhcp_add_u32(uint8_t *payload, uint32_t pos,
                                 uint32_t cap, uint8_t code, uint32_t value)
{
    uint8_t data[4];
    net_put_u32(data, value);
    return net_dhcp_add_option(payload, pos, cap, code, data, 4);
}

static uint32_t net_build_dhcp_packet(uint8_t *payload, uint32_t cap,
                                      uint32_t xid, uint8_t msg_type,
                                      uint32_t requested_ip,
                                      uint32_t server_ip)
{
    uint32_t pos = 240;
    uint8_t params[] = {1, 3, 6, 51, 54};
    uint8_t client_id[7];
    if (cap < NET_DHCP_PACKET_LEN) {
        return 0;
    }
    net_memzero(payload, cap);
    payload[0] = 1;
    payload[1] = 1;
    payload[2] = 6;
    payload[3] = 0;
    net_put_u32(payload + 4, xid);
    net_put_u16(payload + 10, 0x8000u);
    net_memcpy(payload + 28, e1000_mac(), 6);
    net_put_u32(payload + 236, NET_DHCP_MAGIC);
    pos = net_dhcp_add_u8(payload, pos, cap, 53, msg_type);
    client_id[0] = 1;
    net_memcpy(client_id + 1, e1000_mac(), 6);
    pos = net_dhcp_add_option(payload, pos, cap, 61, client_id, sizeof(client_id));
    pos = net_dhcp_add_option(payload, pos, cap, 55, params, sizeof(params));
    if (requested_ip) {
        pos = net_dhcp_add_u32(payload, pos, cap, 50, requested_ip);
    }
    if (server_ip) {
        pos = net_dhcp_add_u32(payload, pos, cap, 54, server_ip);
    }
    if (pos < cap) {
        payload[pos++] = 255;
    }
    return pos < NET_DHCP_PACKET_LEN ? NET_DHCP_PACKET_LEN : pos;
}

static int net_dhcp_parse_packet(const uint8_t *payload, uint32_t len,
                                 uint32_t xid, struct net_dhcp_offer *offer)
{
    uint32_t pos;
    if (!payload || !offer || len < 240 ||
        payload[0] != 2 ||
        payload[1] != 1 ||
        payload[2] != 6 ||
        net_get_u32(payload + 4) != xid ||
        !net_memeq(payload + 28, e1000_mac(), 6) ||
        net_get_u32(payload + 236) != NET_DHCP_MAGIC) {
        return -1;
    }
    *offer = (struct net_dhcp_offer){
        .yiaddr = net_get_u32(payload + 16),
    };
    pos = 240;
    while (pos < len) {
        uint8_t code = payload[pos++];
        uint8_t opt_len;
        const uint8_t *opt;
        if (code == 0) {
            continue;
        }
        if (code == 255) {
            break;
        }
        if (pos >= len) {
            break;
        }
        opt_len = payload[pos++];
        if (pos + opt_len > len) {
            break;
        }
        opt = payload + pos;
        if (code == 53 && opt_len >= 1) {
            offer->msg_type = opt[0];
        } else if (code == 1 && opt_len >= 4) {
            offer->subnet_mask = net_get_u32(opt);
        } else if (code == 3 && opt_len >= 4) {
            offer->router_ip = net_get_u32(opt);
        } else if (code == 6 && opt_len >= 4) {
            offer->dns_ip = net_get_u32(opt);
        } else if (code == 51 && opt_len >= 4) {
            offer->lease_seconds = net_get_u32(opt);
        } else if (code == 54 && opt_len >= 4) {
            offer->server_ip = net_get_u32(opt);
        }
        pos += opt_len;
    }
    return offer->msg_type ? 0 : -1;
}

static int net_dhcp_send(uint32_t xid, uint8_t msg_type,
                         uint32_t requested_ip, uint32_t server_ip)
{
    uint8_t payload[NET_DHCP_PACKET_MAX];
    uint32_t len = net_build_dhcp_packet(payload, sizeof(payload), xid,
                                         msg_type, requested_ip, server_ip);
    if (!len) {
        return -1;
    }
    return net_send_udp_to_mac(net_broadcast_mac, 0, 0xffffffffu,
                               NET_DHCP_CLIENT_PORT, NET_DHCP_SERVER_PORT,
                               payload, len);
}

static int net_dhcp_wait(uint32_t xid, uint32_t timeout_ms,
                         uint8_t expected_type, struct net_dhcp_offer *offer)
{
    uint8_t payload[NET_DHCP_PACKET_MAX];
    struct net_udp_wait udp_wait;
    uint64_t start = time_uptime_ms();
    uint32_t spins = 0;
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        udp_wait = (struct net_udp_wait){
            .src_port = NET_DHCP_SERVER_PORT,
            .dst_port = NET_DHCP_CLIENT_PORT,
            .payload = payload,
            .capacity = sizeof(payload),
        };
        net_poll_once(0, 0, &udp_wait, 0);
        if (udp_wait.done &&
            net_dhcp_parse_packet(payload, udp_wait.length, xid, offer) == 0) {
            if (offer->msg_type == expected_type) {
                return 0;
            }
            if (offer->msg_type == NET_DHCP_NAK) {
                return -3;
            }
        }
        net_cpu_relax();
    }
    return -2;
}

static uint32_t net_dhcp_request(uint32_t timeout_ms,
                                 struct leonos_net_config *out_config)
{
    struct net_dhcp_offer offer;
    uint32_t xid;
    int ret;
    if (!e1000_is_ready()) {
        if (out_config) {
            *out_config = net_config;
        }
        return LEONOS_NET_STATUS_NO_DEVICE;
    }
    timeout_ms = timeout_ms ? timeout_ms : 3000u;
    if (timeout_ms > 10000u) {
        timeout_ms = 10000u;
    }
    xid = 0x4c4e0000u | (net_sequence++ & 0xffffu);
    if (net_dhcp_send(xid, NET_DHCP_DISCOVER, 0, 0) < 0) {
        if (out_config) {
            *out_config = net_config;
        }
        return LEONOS_NET_STATUS_TX_FAILED;
    }
    ret = net_dhcp_wait(xid, timeout_ms, NET_DHCP_OFFER, &offer);
    if (ret < 0 || !offer.yiaddr) {
        if (out_config) {
            *out_config = net_config;
        }
        return ret == -2 ? LEONOS_NET_STATUS_DHCP_TIMEOUT
                         : LEONOS_NET_STATUS_DHCP_FAILED;
    }
    if (net_dhcp_send(xid, NET_DHCP_REQUEST, offer.yiaddr, offer.server_ip) < 0) {
        if (out_config) {
            *out_config = net_config;
        }
        return LEONOS_NET_STATUS_TX_FAILED;
    }
    ret = net_dhcp_wait(xid, timeout_ms, NET_DHCP_ACK, &offer);
    if (ret < 0 || !offer.yiaddr) {
        if (out_config) {
            *out_config = net_config;
        }
        return ret == -2 ? LEONOS_NET_STATUS_DHCP_TIMEOUT
                         : LEONOS_NET_STATUS_DHCP_FAILED;
    }
    net_apply_dhcp_offer(&offer);
    if (out_config) {
        *out_config = net_config;
    }
    return LEONOS_NET_STATUS_OK;
}

static uint32_t net_ensure_dhcp_lease(uint32_t timeout_ms)
{
    if (!e1000_is_ready()) {
        return LEONOS_NET_STATUS_NO_DEVICE;
    }
    if (net_config.source == LEONOS_NET_CONFIG_SOURCE_DHCP &&
        net_config.local_ip && net_config.gateway_ip) {
        return LEONOS_NET_STATUS_OK;
    }
    return net_dhcp_request(timeout_ms, 0);
}

static void net_console_ipv4(uint32_t ip)
{
    console_printf("%u.%u.%u.%u",
                   (ip >> 24) & 0xffu, (ip >> 16) & 0xffu,
                   (ip >> 8) & 0xffu, ip & 0xffu);
}

static void net_log_config(const char *prefix)
{
    console_printf("%s ip=", prefix);
    net_console_ipv4(net_config.local_ip);
    console_printf(" gateway=");
    net_console_ipv4(net_config.gateway_ip);
    console_printf(" dns=");
    net_console_ipv4(net_config.dns_ip);
    console_printf("\n");
}

void net_init(void)
{
    uint32_t status = LEONOS_NET_STATUS_DHCP_FAILED;
    e1000_init();
    net_set_static_fallback();
    if (!e1000_is_ready()) {
        console_printf("[ntclks] net unavailable: no active e1000\n");
        return;
    }
    net_log_config("[ntclks] net ready static fallback");
    for (uint32_t attempt = 1; attempt <= NET_BOOT_DHCP_ATTEMPTS; ++attempt) {
        console_printf("[ntclks] DHCP boot attempt %u/%u\n",
                       attempt, NET_BOOT_DHCP_ATTEMPTS);
        status = net_dhcp_request(NET_BOOT_DHCP_TIMEOUT_MS, 0);
        if (status == LEONOS_NET_STATUS_OK) {
            net_log_config("[ntclks] DHCP lease acquired");
            return;
        }
        console_printf("[ntclks] DHCP boot attempt %u failed status=%u\n",
                       attempt, status);
    }
    net_log_config("[ntclks] DHCP unavailable, keeping static fallback");
}

int net_is_ready(void)
{
    return e1000_is_ready();
}

int net_get_config(struct leonos_net_config *config)
{
    if (!config) {
        return -1;
    }
    net_update_config_flags();
    *config = net_config;
    return 0;
}

int net_dhcp_renew(struct leonos_net_dhcp *request)
{
    uint32_t timeout_ms;
    if (!request) {
        return -1;
    }
    request->status = LEONOS_NET_STATUS_DHCP_FAILED;
    request->config = net_config;
    timeout_ms = request->timeout_ms ? request->timeout_ms : 3000u;
    request->status = net_dhcp_request(timeout_ms, &request->config);
    return 0;
}

int net_ping(struct leonos_net_ping *request)
{
    uint8_t next_hop_mac[6];
    struct net_ping_wait wait;
    uint32_t timeout_ms;
    uint32_t target_ip;
    uint32_t arp_ip;
    uint16_t sequence;
    uint64_t start;
    uint32_t spins = 0;

    if (!request || request->target_ip == 0 || request->target_ip == 0xffffffffu) {
        if (request) {
            request->status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        }
        return 0;
    }
    request->sent = 0;
    request->received = 0;
    request->rtt_ms = 0;
    if (!e1000_is_ready()) {
        request->status = LEONOS_NET_STATUS_NO_DEVICE;
        return 0;
    }

    timeout_ms = request->timeout_ms ? request->timeout_ms : LEONOS_NET_DEFAULT_TIMEOUT_MS;
    if (timeout_ms > LEONOS_NET_MAX_TIMEOUT_MS) {
        timeout_ms = LEONOS_NET_MAX_TIMEOUT_MS;
    }
    {
        uint32_t lease_status = net_ensure_dhcp_lease(timeout_ms);
        if (lease_status != LEONOS_NET_STATUS_OK) {
            request->status = lease_status;
            return 0;
        }
    }
    target_ip = request->target_ip;
    arp_ip = net_route_arp_ip(target_ip);
    {
        int arp_ret = net_resolve_mac(arp_ip, timeout_ms, next_hop_mac);
        if (arp_ret < 0) {
            request->status = arp_ret == -1
                                  ? LEONOS_NET_STATUS_TX_FAILED
                                  : LEONOS_NET_STATUS_ARP_TIMEOUT;
            return 0;
        }
    }

    sequence = (uint16_t)(request->sequence ? request->sequence : net_sequence++);
    wait = (struct net_ping_wait){
        .target_ip = target_ip,
        .ident = 0x4c4e,
        .sequence = sequence,
        .done = 0,
    };
    start = time_uptime_ms();
    if (net_send_icmp_echo_request(next_hop_mac, target_ip, wait.ident, wait.sequence) < 0) {
        request->status = LEONOS_NET_STATUS_TX_FAILED;
        return 0;
    }
    request->sent = 1;
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        net_poll_once(0, &wait, 0, 0);
        if (wait.done) {
            uint64_t now = time_uptime_ms();
            request->received = 1;
            request->rtt_ms = (uint32_t)(now >= start ? now - start : 0);
            request->status = LEONOS_NET_STATUS_OK;
            return 0;
        }
        net_cpu_relax();
    }
    request->status = LEONOS_NET_STATUS_ECHO_TIMEOUT;
    return 0;
}

static int net_dns_encode_name(const char *name, uint8_t *out,
                               uint32_t cap, uint32_t *out_len)
{
    uint32_t label_start = 0;
    uint32_t len = net_strlen(name, LEONOS_NET_HOSTNAME_LEN);
    uint32_t pos = 0;
    if (!name || !out || !out_len || len == 0 || len >= LEONOS_NET_HOSTNAME_LEN) {
        return -1;
    }
    for (uint32_t i = 0; i <= len; ++i) {
        if (name[i] == '.' || name[i] == 0) {
            uint32_t label_len = i - label_start;
            if (label_len == 0 || label_len > 63 || pos + label_len + 1 >= cap) {
                return -1;
            }
            out[pos++] = (uint8_t)label_len;
            net_memcpy(out + pos, name + label_start, label_len);
            pos += label_len;
            label_start = i + 1;
        }
    }
    if (pos + 1 > cap) {
        return -1;
    }
    out[pos++] = 0;
    *out_len = pos;
    return 0;
}

static int net_dns_skip_name(const uint8_t *packet, uint32_t len, uint32_t *pos)
{
    uint32_t p;
    if (!packet || !pos) {
        return -1;
    }
    p = *pos;
    while (p < len) {
        uint8_t label = packet[p];
        if (label == 0) {
            *pos = p + 1;
            return 0;
        }
        if ((label & 0xc0u) == 0xc0u) {
            if (p + 2 > len) {
                return -1;
            }
            *pos = p + 2;
            return 0;
        }
        if ((label & 0xc0u) != 0 || p + 1u + label > len) {
            return -1;
        }
        p += 1u + label;
    }
    return -1;
}

static uint32_t net_dns_build_query(uint8_t *packet, uint32_t cap,
                                    const char *name, uint16_t ident)
{
    uint32_t qname_len = 0;
    uint32_t pos = 12;
    if (cap < 32) {
        return 0;
    }
    net_memzero(packet, cap);
    if (net_dns_encode_name(name, packet + pos, cap - pos, &qname_len) < 0) {
        return 0;
    }
    net_put_u16(packet, ident);
    net_put_u16(packet + 2, 0x0100u);
    net_put_u16(packet + 4, 1);
    pos = 12 + qname_len;
    net_put_u16(packet + pos, 1);
    net_put_u16(packet + pos + 2, 1);
    return pos + 4;
}

static int net_dns_parse_response(const uint8_t *packet, uint32_t len,
                                  uint16_t ident, struct leonos_net_dns *request)
{
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint32_t pos = 12;
    if (!packet || !request || len < 12 || net_get_u16(packet) != ident) {
        return -1;
    }
    flags = net_get_u16(packet + 2);
    if ((flags & 0x8000u) == 0 || (flags & 0x000fu) != 0) {
        return -1;
    }
    qdcount = net_get_u16(packet + 4);
    ancount = net_get_u16(packet + 6);
    for (uint32_t i = 0; i < qdcount; ++i) {
        if (net_dns_skip_name(packet, len, &pos) < 0 || pos + 4 > len) {
            return -1;
        }
        pos += 4;
    }
    request->address_count = 0;
    for (uint32_t i = 0; i < ancount && pos < len; ++i) {
        uint16_t type;
        uint16_t cls;
        uint16_t rdlen;
        if (net_dns_skip_name(packet, len, &pos) < 0 || pos + 10 > len) {
            return -1;
        }
        type = net_get_u16(packet + pos);
        cls = net_get_u16(packet + pos + 2);
        rdlen = net_get_u16(packet + pos + 8);
        pos += 10;
        if (pos + rdlen > len) {
            return -1;
        }
        if (type == 1 && cls == 1 && rdlen == 4 &&
            request->address_count < LEONOS_NET_DNS_MAX_ADDRESSES) {
            request->addresses[request->address_count++] = net_get_u32(packet + pos);
        }
        pos += rdlen;
    }
    return request->address_count ? 0 : -2;
}

int net_dns_resolve(struct leonos_net_dns *request)
{
    uint8_t query[NET_DNS_PACKET_MAX];
    uint8_t response[NET_DNS_PACKET_MAX];
    uint8_t dst_mac[6];
    struct net_udp_wait udp_wait;
    uint32_t timeout_ms;
    uint32_t query_len;
    uint32_t arp_ip;
    uint16_t ident;
    uint16_t local_port;
    uint64_t start;
    uint32_t spins = 0;
    int ret;

    if (!request || !request->name[0]) {
        if (request) {
            request->status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        }
        return 0;
    }
    request->status = LEONOS_NET_STATUS_DNS_FAILED;
    request->address_count = 0;
    for (uint32_t i = 0; i < LEONOS_NET_DNS_MAX_ADDRESSES; ++i) {
        request->addresses[i] = 0;
    }
    if (!e1000_is_ready()) {
        request->status = LEONOS_NET_STATUS_NO_DEVICE;
        return 0;
    }
    timeout_ms = request->timeout_ms ? request->timeout_ms : 3000u;
    if (timeout_ms > 10000u) {
        timeout_ms = 10000u;
    }
    {
        uint32_t lease_status = net_ensure_dhcp_lease(timeout_ms);
        if (lease_status != LEONOS_NET_STATUS_OK) {
            request->status = lease_status;
            return 0;
        }
    }
    if (!net_config.dns_ip) {
        request->status = LEONOS_NET_STATUS_DNS_FAILED;
        return 0;
    }
    ident = (uint16_t)(net_sequence++ & 0xffffu);
    local_port = (uint16_t)(49152u + (ident & 0x3fffu));
    query_len = net_dns_build_query(query, sizeof(query), request->name, ident);
    if (!query_len) {
        request->status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    arp_ip = net_route_arp_ip(net_config.dns_ip);
    ret = net_resolve_mac(arp_ip, timeout_ms, dst_mac);
    if (ret < 0) {
        request->status = ret == -1 ? LEONOS_NET_STATUS_TX_FAILED
                                    : LEONOS_NET_STATUS_ARP_TIMEOUT;
        return 0;
    }
    if (net_send_udp_to_mac(dst_mac, net_config.local_ip, net_config.dns_ip,
                            local_port, NET_DNS_PORT, query, query_len) < 0) {
        request->status = LEONOS_NET_STATUS_TX_FAILED;
        return 0;
    }
    start = time_uptime_ms();
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        udp_wait = (struct net_udp_wait){
            .src_ip = net_config.dns_ip,
            .src_port = NET_DNS_PORT,
            .dst_port = local_port,
            .payload = response,
            .capacity = sizeof(response),
        };
        net_poll_once(0, 0, &udp_wait, 0);
        if (udp_wait.done) {
            ret = net_dns_parse_response(response, udp_wait.length, ident, request);
            request->status = ret == 0 ? LEONOS_NET_STATUS_OK
                                       : LEONOS_NET_STATUS_DNS_NO_ANSWER;
            return 0;
        }
        net_cpu_relax();
    }
    request->status = LEONOS_NET_STATUS_DNS_TIMEOUT;
    return 0;
}

static int net_parse_ipv4_literal(const char *text, uint32_t len,
                                  uint32_t *out_ip)
{
    uint32_t parts[4] = {0, 0, 0, 0};
    uint32_t part = 0;
    uint32_t value = 0;
    uint32_t digits = 0;
    if (!text || !out_ip || len == 0) {
        return -1;
    }
    for (uint32_t i = 0; i <= len; ++i) {
        char ch = i < len ? text[i] : '.';
        if (ch >= '0' && ch <= '9') {
            value = value * 10u + (uint32_t)(ch - '0');
            if (value > 255u) {
                return -1;
            }
            ++digits;
            continue;
        }
        if (ch != '.' || digits == 0 || part >= 4) {
            return -1;
        }
        parts[part++] = value;
        value = 0;
        digits = 0;
    }
    if (part != 4) {
        return -1;
    }
    *out_ip = (parts[0] << 24) | (parts[1] << 16) |
              (parts[2] << 8) | parts[3];
    return 0;
}

static int net_http_resolve_host(const char *host, uint32_t host_len,
                                 uint32_t timeout_ms, uint32_t *out_ip,
                                 uint32_t *out_status)
{
    struct leonos_net_dns dns;
    if (net_parse_ipv4_literal(host, host_len, out_ip) == 0) {
        return 0;
    }
    dns = (struct leonos_net_dns){0};
    dns.timeout_ms = timeout_ms;
    dns.status = LEONOS_NET_STATUS_DNS_FAILED;
    for (uint32_t i = 0; i < host_len && i + 1u < sizeof(dns.name); ++i) {
        dns.name[i] = host[i];
    }
    if (net_dns_resolve(&dns) < 0) {
        if (out_status) {
            *out_status = LEONOS_NET_STATUS_DNS_FAILED;
        }
        return -1;
    }
    if (dns.status != LEONOS_NET_STATUS_OK || dns.address_count == 0) {
        if (out_status) {
            *out_status = dns.status;
        }
        return -1;
    }
    *out_ip = dns.addresses[0];
    return 0;
}

static uint32_t net_http_build_request(char *dst, uint32_t cap,
                                       const char *host, const char *path,
                                       uint32_t port)
{
    uint32_t pos = 0;
    if (!dst || cap == 0 || !host || !host[0]) {
        return 0;
    }
    dst[0] = 0;
    net_append_text(dst, &pos, cap, "GET ");
    if (!path || !path[0]) {
        net_append_char(dst, &pos, cap, '/');
    } else {
        if (path[0] != '/') {
            net_append_char(dst, &pos, cap, '/');
        }
        net_append_text(dst, &pos, cap, path);
    }
    net_append_text(dst, &pos, cap, " HTTP/1.0\r\nHost: ");
    net_append_text(dst, &pos, cap, host);
    if (port && port != NET_HTTP_PORT) {
        net_append_char(dst, &pos, cap, ':');
        net_append_u32(dst, &pos, cap, port);
    }
    net_append_text(dst, &pos, cap,
                    "\r\nConnection: close\r\nUser-Agent: LeonOS/4\r\n\r\n");
    if (pos + 1u >= cap) {
        return 0;
    }
    return pos;
}

static uint32_t net_http_parse_status(const char *response, uint32_t len)
{
    uint32_t pos = 0;
    uint32_t status = 0;
    if (!response || len < 12) {
        return 0;
    }
    if (response[0] != 'H' || response[1] != 'T' ||
        response[2] != 'T' || response[3] != 'P' ||
        response[4] != '/') {
        return 0;
    }
    while (pos < len && response[pos] != ' ' &&
           response[pos] != '\r' && response[pos] != '\n') {
        ++pos;
    }
    while (pos < len && response[pos] == ' ') {
        ++pos;
    }
    for (uint32_t i = 0; i < 3 && pos < len; ++i, ++pos) {
        if (response[pos] < '0' || response[pos] > '9') {
            return 0;
        }
        status = status * 10u + (uint32_t)(response[pos] - '0');
    }
    return status;
}

int net_http_get(struct leonos_net_http_get *request)
{
    uint8_t dst_mac[6];
    char http_request[NET_HTTP_REQUEST_MAX];
    struct net_tcp_wait wait;
    uint32_t timeout_ms;
    uint32_t host_len;
    uint32_t path_len;
    uint32_t port;
    uint32_t remote_ip = 0;
    uint32_t arp_ip;
    uint32_t http_len;
    uint32_t syn_seq;
    uint32_t local_seq;
    uint32_t acked_remote_seq;
    uint16_t local_port;
    uint64_t start;
    uint64_t next_retransmit;
    uint32_t spins;
    int ret;

    if (!request) {
        return -1;
    }
    host_len = net_strlen(request->host, LEONOS_NET_HOSTNAME_LEN);
    path_len = net_strlen(request->path, LEONOS_NET_HTTP_PATH_LEN);
    request->status = LEONOS_NET_STATUS_HTTP_FAILED;
    request->remote_ip = 0;
    request->http_status = 0;
    request->response_len = 0;
    request->response[0] = 0;
    if (host_len == 0 || host_len >= LEONOS_NET_HOSTNAME_LEN ||
        path_len >= LEONOS_NET_HTTP_PATH_LEN ||
        request->port > 65535u) {
        request->status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    if (!e1000_is_ready()) {
        request->status = LEONOS_NET_STATUS_NO_DEVICE;
        return 0;
    }
    timeout_ms = request->timeout_ms ? request->timeout_ms : 4000u;
    if (timeout_ms > 10000u) {
        timeout_ms = 10000u;
    }
    {
        uint32_t lease_status = net_ensure_dhcp_lease(timeout_ms);
        if (lease_status != LEONOS_NET_STATUS_OK) {
            request->status = lease_status;
            return 0;
        }
    }
    port = request->port ? request->port : NET_HTTP_PORT;
    http_len = net_http_build_request(http_request, sizeof(http_request),
                                      request->host, request->path, port);
    if (!http_len) {
        request->status = LEONOS_NET_STATUS_BAD_ARGUMENT;
        return 0;
    }
    if (net_http_resolve_host(request->host, host_len, timeout_ms,
                              &remote_ip, &request->status) < 0) {
        return 0;
    }
    request->remote_ip = remote_ip;
    arp_ip = net_route_arp_ip(remote_ip);
    ret = net_resolve_mac(arp_ip, timeout_ms, dst_mac);
    if (ret < 0) {
        request->status = ret == -1 ? LEONOS_NET_STATUS_TX_FAILED
                                    : LEONOS_NET_STATUS_ARP_TIMEOUT;
        return 0;
    }

    local_seq = 0x4c4e0000u ^ (net_sequence++ << 8) ^
                (uint32_t)time_uptime_ms();
    local_port = (uint16_t)(49152u + (local_seq & 0x3fffu));
    syn_seq = local_seq;
    wait = (struct net_tcp_wait){
        .src_ip = remote_ip,
        .src_port = (uint16_t)port,
        .dst_port = local_port,
        .payload = (uint8_t *)request->response,
        .capacity = LEONOS_NET_HTTP_RESPONSE_MAX - 1u,
    };

    if (net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                            local_port, (uint16_t)port, local_seq, 0,
                            TCP_FLAG_SYN, 0, 0) < 0) {
        request->status = LEONOS_NET_STATUS_TX_FAILED;
        return 0;
    }
    ++local_seq;
    start = time_uptime_ms();
    next_retransmit = start + NET_TCP_SYN_RETRANSMIT_MS;
    spins = 0;
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        uint64_t now;
        net_poll_once(0, 0, 0, &wait);
        if (wait.reset) {
            request->status = LEONOS_NET_STATUS_TCP_RESET;
            return 0;
        }
        if ((wait.flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            wait.acked_seq == local_seq && wait.remote_seq) {
            break;
        }
        now = time_uptime_ms();
        if (now >= next_retransmit) {
            if (net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                                    local_port, (uint16_t)port, syn_seq, 0,
                                    TCP_FLAG_SYN, 0, 0) < 0) {
                request->status = LEONOS_NET_STATUS_TX_FAILED;
                return 0;
            }
            next_retransmit = now + NET_TCP_SYN_RETRANSMIT_MS;
        }
        net_cpu_relax();
    }
    if (!wait.remote_seq || wait.acked_seq != local_seq) {
        request->status = LEONOS_NET_STATUS_TCP_TIMEOUT;
        return 0;
    }
    if (net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                            local_port, (uint16_t)port, local_seq,
                            wait.remote_seq, TCP_FLAG_ACK, 0, 0) < 0) {
        request->status = LEONOS_NET_STATUS_TX_FAILED;
        return 0;
    }

    if (net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                            local_port, (uint16_t)port, local_seq,
                            wait.remote_seq, TCP_FLAG_PSH | TCP_FLAG_ACK,
                            (const uint8_t *)http_request, http_len) < 0) {
        request->status = LEONOS_NET_STATUS_TX_FAILED;
        return 0;
    }
    local_seq += http_len;
    acked_remote_seq = wait.remote_seq;
    start = time_uptime_ms();
    next_retransmit = start + NET_TCP_DATA_RETRANSMIT_MS;
    spins = 0;
    while (!net_timeout_expired(start, timeout_ms, spins++)) {
        uint32_t before = wait.changed;
        uint64_t now;
        net_poll_once(0, 0, 0, &wait);
        if (wait.reset) {
            request->status = LEONOS_NET_STATUS_TCP_RESET;
            return 0;
        }
        if (wait.changed != before && wait.remote_seq != acked_remote_seq) {
            (void)net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                                      local_port, (uint16_t)port, local_seq,
                                      wait.remote_seq, TCP_FLAG_ACK, 0, 0);
            acked_remote_seq = wait.remote_seq;
        }
        if (wait.overflow) {
            request->status = LEONOS_NET_STATUS_HTTP_TOO_LARGE;
            break;
        }
        if (wait.fin) {
            request->status = LEONOS_NET_STATUS_OK;
            break;
        }
        now = time_uptime_ms();
        if (wait.length == 0 && !wait.fin && wait.acked_seq != local_seq &&
            now >= next_retransmit) {
            if (net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                                    local_port, (uint16_t)port,
                                    local_seq - http_len, wait.remote_seq,
                                    TCP_FLAG_PSH | TCP_FLAG_ACK,
                                    (const uint8_t *)http_request,
                                    http_len) < 0) {
                request->status = LEONOS_NET_STATUS_TX_FAILED;
                break;
            }
            next_retransmit = now + NET_TCP_DATA_RETRANSMIT_MS;
        }
        net_cpu_relax();
    }

    request->response_len = wait.length;
    if (request->response_len >= LEONOS_NET_HTTP_RESPONSE_MAX) {
        request->response_len = LEONOS_NET_HTTP_RESPONSE_MAX - 1u;
    }
    request->response[request->response_len] = 0;
    if (request->status != LEONOS_NET_STATUS_HTTP_TOO_LARGE &&
        request->status != LEONOS_NET_STATUS_OK) {
        request->status = wait.length ? LEONOS_NET_STATUS_OK
                                      : LEONOS_NET_STATUS_TCP_TIMEOUT;
    }
    if (request->status == LEONOS_NET_STATUS_OK ||
        request->status == LEONOS_NET_STATUS_HTTP_TOO_LARGE) {
        request->http_status =
            net_http_parse_status(request->response, request->response_len);
        if (request->status == LEONOS_NET_STATUS_OK && !request->http_status) {
            request->status = LEONOS_NET_STATUS_HTTP_FAILED;
        }
    }
    (void)net_send_tcp_to_mac(dst_mac, net_config.local_ip, remote_ip,
                              local_port, (uint16_t)port, local_seq,
                              wait.remote_seq, TCP_FLAG_FIN | TCP_FLAG_ACK,
                              0, 0);
    return 0;
}

void net_device_info(uint32_t *flags, uint64_t *mac_value, uint32_t *local_ip)
{
    struct e1000_info info;
    uint64_t mac = 0;
    e1000_get_info(&info);
    for (uint32_t i = 0; i < 6; ++i) {
        mac |= (uint64_t)info.mac[i] << (i * 8u);
    }
    if (flags) {
        *flags = info.present ? LEONOS_DEVICE_FLAG_PRESENT : 0;
        if (info.active) {
            *flags |= LEONOS_DEVICE_FLAG_ACTIVE;
        }
    }
    if (mac_value) {
        *mac_value = mac;
    }
    if (local_ip) {
        *local_ip = net_config.local_ip;
    }
}
