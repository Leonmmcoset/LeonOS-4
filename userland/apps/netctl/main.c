#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define NETCTL_W 620
#define NETCTL_H 360
#define DOMAIN_LEN LEONOS_NET_HOSTNAME_LEN
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[NETCTL_W * NETCTL_H];
static struct leonos_net_config config;
static char domain_input[DOMAIN_LEN] = "example.com";
static char status_text[128] = "Ready";
static char dns_text[192] = "Enter a host name and resolve an A record.";
static struct leonos_ui_edit_state domain_edit;

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1 < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(dst, pos, cap, *text++);
    }
}

static void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static void append_i32(char *dst, uint32_t *pos, uint32_t cap, int value)
{
    if (value < 0) {
        append_char(dst, pos, cap, '-');
        value = -value;
    }
    append_u32(dst, pos, cap, (uint32_t)value);
}

static void append_hex_nibble(char *dst, uint32_t *pos, uint32_t cap, uint8_t value)
{
    value &= 0x0fu;
    append_char(dst, pos, cap, value < 10 ? (char)('0' + value)
                                          : (char)('a' + value - 10));
}

static void append_hex_byte(char *dst, uint32_t *pos, uint32_t cap, uint8_t value)
{
    append_hex_nibble(dst, pos, cap, (uint8_t)(value >> 4));
    append_hex_nibble(dst, pos, cap, value);
}

static void format_ipv4(char *dst, uint32_t cap, uint32_t ip)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_u32(dst, &pos, cap, (ip >> 24) & 0xffu);
    append_char(dst, &pos, cap, '.');
    append_u32(dst, &pos, cap, (ip >> 16) & 0xffu);
    append_char(dst, &pos, cap, '.');
    append_u32(dst, &pos, cap, (ip >> 8) & 0xffu);
    append_char(dst, &pos, cap, '.');
    append_u32(dst, &pos, cap, ip & 0xffu);
}

static void format_mac(char *dst, uint32_t cap, const uint8_t mac[6])
{
    uint32_t pos = 0;
    dst[0] = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        if (i) {
            append_char(dst, &pos, cap, ':');
        }
        append_hex_byte(dst, &pos, cap, mac[i]);
    }
}

static const char *status_name(uint32_t status)
{
    switch (status) {
    case LEONOS_NET_STATUS_OK:
        return T("OK", "成功");
    case LEONOS_NET_STATUS_NO_DEVICE:
        return T("No e1000 adapter", "没有 e1000 网卡");
    case LEONOS_NET_STATUS_ARP_TIMEOUT:
        return T("ARP timeout", "ARP 超时");
    case LEONOS_NET_STATUS_BAD_ARGUMENT:
        return T("Bad argument", "参数无效");
    case LEONOS_NET_STATUS_TX_FAILED:
        return T("Transmit failed", "发送失败");
    case LEONOS_NET_STATUS_DHCP_TIMEOUT:
        return T("DHCP timeout", "DHCP 超时");
    case LEONOS_NET_STATUS_DHCP_FAILED:
        return T("DHCP failed", "DHCP 失败");
    case LEONOS_NET_STATUS_DNS_TIMEOUT:
        return T("DNS timeout", "DNS 超时");
    case LEONOS_NET_STATUS_DNS_FAILED:
        return T("DNS failed", "DNS 失败");
    case LEONOS_NET_STATUS_DNS_NO_ANSWER:
        return T("No A record", "没有 A 记录");
    default:
        return T("Unknown status", "未知状态");
    }
}

static const char *source_name(uint32_t source)
{
    switch (source) {
    case LEONOS_NET_CONFIG_SOURCE_DHCP:
        return "DHCP";
    case LEONOS_NET_CONFIG_SOURCE_STATIC:
        return T("Static fallback", "静态回退");
    default:
        return T("None", "无");
    }
}

static void set_status_ret(const char *prefix, int ret)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), prefix);
    append_text(status_text, &pos, sizeof(status_text), " ret=");
    append_i32(status_text, &pos, sizeof(status_text), ret);
}

static void refresh_config(void)
{
    int ret = leonos_net_config(&config);
    if (ret < 0) {
        config = (struct leonos_net_config){0};
        set_status_ret(T("Config query failed", "读取配置失败"), ret);
        return;
    }
    copy_text(status_text, sizeof(status_text), T("Network configuration refreshed", "网络配置已刷新"));
}

static void renew_dhcp(void)
{
    struct leonos_net_dhcp dhcp;
    int ret = leonos_net_dhcp_renew(4000, &dhcp);
    if (ret < 0) {
        set_status_ret(T("DHCP ioctl failed", "DHCP ioctl 失败"), ret);
        return;
    }
    config = dhcp.config;
    copy_text(status_text, sizeof(status_text), status_name(dhcp.status));
}

static void resolve_domain(void)
{
    struct leonos_net_dns dns;
    int ret = leonos_net_dns_resolve(domain_input, 4000, &dns);
    uint32_t pos = 0;
    if (ret < 0) {
        set_status_ret(T("DNS ioctl failed", "DNS ioctl 失败"), ret);
        return;
    }
    dns_text[0] = 0;
    if (dns.status == LEONOS_NET_STATUS_OK && dns.address_count) {
        append_text(dns_text, &pos, sizeof(dns_text), domain_input);
        append_text(dns_text, &pos, sizeof(dns_text), " -> ");
        for (uint32_t i = 0; i < dns.address_count; ++i) {
            char ip[24];
            if (i) {
                append_text(dns_text, &pos, sizeof(dns_text), ", ");
            }
            format_ipv4(ip, sizeof(ip), dns.addresses[i]);
            append_text(dns_text, &pos, sizeof(dns_text), ip);
        }
    } else {
        append_text(dns_text, &pos, sizeof(dns_text), status_name(dns.status));
        append_text(dns_text, &pos, sizeof(dns_text), T(" resolving ", "，解析 "));
        append_text(dns_text, &pos, sizeof(dns_text), domain_input);
    }
    copy_text(status_text, sizeof(status_text), status_name(dns.status));
}

static void draw_row(struct leonos_ui_surface *ui, uint32_t y,
                     const char *label, const char *value)
{
    leonos_ui_text(ui, 26, y, label, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 146, y, NETCTL_W - 170, value, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
}

static void draw_netctl(struct leonos_ui_surface *ui)
{
    char mac[32];
    char ip[32];
    char mask[32];
    char gateway[32];
    char dns[32];
    char lease[48];
    uint32_t pos = 0;

    format_mac(mac, sizeof(mac), config.mac);
    format_ipv4(ip, sizeof(ip), config.local_ip);
    format_ipv4(mask, sizeof(mask), config.subnet_mask);
    format_ipv4(gateway, sizeof(gateway), config.gateway_ip);
    format_ipv4(dns, sizeof(dns), config.dns_ip);
    lease[0] = 0;
    append_u32(lease, &pos, sizeof(lease), config.lease_seconds);
    append_text(lease, &pos, sizeof(lease), "s");

    leonos_ui_rect(ui, 0, 0, NETCTL_W, NETCTL_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, NETCTL_W, NETCTL_H, T("Network Controller", "网络控制器"));
    leonos_ui_text(ui, 24, 40, T("Intel e1000 Network", "Intel e1000 网络"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    draw_row(ui, 74, T("State:", "状态:"), (config.flags & LEONOS_NET_CONFIG_FLAG_ACTIVE)
                                               ? T("Active", "活动")
                                               : T("Unavailable", "不可用"));
    draw_row(ui, 98, T("MAC:", "MAC:"), mac);
    draw_row(ui, 122, T("Config:", "配置:"), source_name(config.source));
    draw_row(ui, 146, T("IPv4:", "IPv4:"), ip);
    draw_row(ui, 170, T("Mask:", "掩码:"), mask);
    draw_row(ui, 194, T("Gateway:", "网关:"), gateway);
    draw_row(ui, 218, T("DNS:", "DNS:"), dns);
    draw_row(ui, 242, T("Lease:", "租约:"), lease);

    leonos_ui_button(ui, 24, 280, 88, LEONOS_UI_BUTTON_H, T("Refresh", "刷新"), 0);
    leonos_ui_button(ui, 124, 280, 118, LEONOS_UI_BUTTON_H, T("Renew DHCP", "更新 DHCP"), 0);
    leonos_ui_text(ui, 264, 284, T("Host:", "域名:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, 308, 280, 178, &domain_edit, 0);
    leonos_ui_button(ui, 498, 280, 86, LEONOS_UI_BUTTON_H, T("Resolve", "解析"), 0);
    leonos_ui_text_clipped(ui, 24, 316, NETCTL_W - 48, dns_text,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_statusbar(ui, NETCTL_H - 28, 28, status_text);
}

static int hit_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= x && py >= y &&
           px < x + (int32_t)w && py < y + (int32_t)h;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    puts("[netctl.elf] network controller starting");
    window_id = leonos_gui_create_app_window_ex(T("Network Controller", "网络控制器"),
                                                T("DHCP and DNS", "DHCP 和 DNS"),
                                                NETCTL_W, NETCTL_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[netctl.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, NETCTL_W, NETCTL_H, NETCTL_W);
    leonos_ui_edit_state_init(&domain_edit, domain_input, sizeof(domain_input));
    domain_edit.focused = 0;
    refresh_config();
    draw_netctl(&ui);
    leonos_gui_present_window((uint32_t)window_id, NETCTL_W, NETCTL_H, NETCTL_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                if (leonos_ui_edit_state_handle_mouse(&domain_edit, event.x, event.y,
                                                      308, 280, 178, event.buttons)) {
                    draw_netctl(&ui);
                    leonos_gui_present_window((uint32_t)window_id, NETCTL_W, NETCTL_H, NETCTL_W, pixels);
                }
                if (event.buttons & 1u) {
                    if (hit_rect(event.x, event.y, 24, 280, 88, LEONOS_UI_BUTTON_H)) {
                        refresh_config();
                    } else if (hit_rect(event.x, event.y, 124, 280, 118, LEONOS_UI_BUTTON_H)) {
                        renew_dhcp();
                    } else if (hit_rect(event.x, event.y, 498, 280, 86, LEONOS_UI_BUTTON_H)) {
                        resolve_domain();
                    }
                    draw_netctl(&ui);
                    leonos_gui_present_window((uint32_t)window_id, NETCTL_W, NETCTL_H, NETCTL_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    return 0;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    resolve_domain();
                } else if (!leonos_ui_edit_state_handle_key(&domain_edit, event.keycode, event.pressed)) {
                    continue;
                }
                draw_netctl(&ui);
                leonos_gui_present_window((uint32_t)window_id, NETCTL_W, NETCTL_H, NETCTL_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                draw_netctl(&ui);
                leonos_gui_present_window((uint32_t)window_id, NETCTL_W, NETCTL_H, NETCTL_W, pixels);
            }
        }
        sleep_ms(10);
    }
}
