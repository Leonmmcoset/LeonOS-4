#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/net_service.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define PING_W 500
#define PING_H 238
#define PING_INPUT_LEN 32
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[PING_W * PING_H];
static char input_ip[PING_INPUT_LEN] = "10.0.2.2";
static char status_text[128] = "Ready";
static char result_text[160] = "Press Ping to send one ICMP Echo request.";
static char detail_text[160] = "Network configuration not loaded.";
static struct leonos_ui_edit_state input_edit;

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

static int parse_ipv4(const char *text, uint32_t *out)
{
    uint32_t octets[4] = {0, 0, 0, 0};
    uint32_t part = 0;
    uint32_t value = 0;
    uint32_t digits = 0;
    if (!text || !text[0]) {
        return -1;
    }
    for (uint32_t i = 0;; ++i) {
        char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            value = value * 10u + (uint32_t)(ch - '0');
            if (value > 255u) {
                return -1;
            }
            ++digits;
            continue;
        }
        if (ch == '.' || ch == 0) {
            if (digits == 0 || part >= 4u) {
                return -1;
            }
            octets[part++] = value;
            value = 0;
            digits = 0;
            if (ch == 0) {
                break;
            }
            continue;
        }
        return -1;
    }
    if (part != 4u) {
        return -1;
    }
    *out = (octets[0] << 24) | (octets[1] << 16) |
           (octets[2] << 8) | octets[3];
    return 0;
}

static const char *status_name(uint32_t status)
{
    switch (status) {
    case NET_SERVICE_STATUS_OK:
        return T("OK", "成功");
    case NET_SERVICE_STATUS_NO_DEVICE:
        return T("No e1000 adapter is active", "没有活动的 e1000 网卡");
    case NET_SERVICE_STATUS_ARP_TIMEOUT:
        return T("ARP timeout", "ARP 超时");
    case NET_SERVICE_STATUS_ECHO_TIMEOUT:
        return T("Request timed out", "请求超时");
    case NET_SERVICE_STATUS_BAD_ARGUMENT:
        return T("Bad target address", "目标地址无效");
    case NET_SERVICE_STATUS_TX_FAILED:
        return T("Transmit failed", "发送失败");
    case NET_SERVICE_STATUS_DHCP_TIMEOUT:
        return T("DHCP timeout", "DHCP 超时");
    case NET_SERVICE_STATUS_DHCP_FAILED:
        return T("DHCP failed", "DHCP 失败");
    default:
        return T("Unknown network status", "未知网络状态");
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

static void refresh_detail(void)
{
    net_service_config_t cfg;
    char ip[24];
    char gateway[24];
    char dns[24];
    uint32_t pos = 0;
    if (net_service_config(&cfg) < 0) {
        copy_text(detail_text, sizeof(detail_text), T("Could not read network configuration.", "无法读取网络配置。"));
        return;
    }
    format_ipv4(ip, sizeof(ip), cfg.local_ip);
    format_ipv4(gateway, sizeof(gateway), cfg.gateway_ip);
    format_ipv4(dns, sizeof(dns), cfg.dns_ip);
    detail_text[0] = 0;
    append_text(detail_text, &pos, sizeof(detail_text),
                cfg.source == NET_SERVICE_CONFIG_SOURCE_DHCP ? "DHCP " : "Static ");
    append_text(detail_text, &pos, sizeof(detail_text), "IPv4 ");
    append_text(detail_text, &pos, sizeof(detail_text), ip);
    append_text(detail_text, &pos, sizeof(detail_text), ", gateway ");
    append_text(detail_text, &pos, sizeof(detail_text), gateway);
    append_text(detail_text, &pos, sizeof(detail_text), ", DNS ");
    append_text(detail_text, &pos, sizeof(detail_text), dns);
}

static void run_ping(void)
{
    uint32_t ip;
    net_service_ping_t result;
    char ip_text[24];
    uint32_t pos;
    int ret;
    if (parse_ipv4(input_ip, &ip) < 0) {
        copy_text(status_text, sizeof(status_text), T("Invalid IPv4 address", "IPv4 地址无效"));
        copy_text(result_text, sizeof(result_text), T("Use dotted decimal form, for example 10.0.2.2.", "请输入类似 10.0.2.2 的 IPv4 地址。"));
        return;
    }
    format_ipv4(ip_text, sizeof(ip_text), ip);
    copy_text(status_text, sizeof(status_text), T("Sending ICMP Echo request...", "正在发送 ICMP Echo 请求..."));
    result = (net_service_ping_t){0};
    ret = net_service_ping(ip, NET_SERVICE_DEFAULT_TIMEOUT_MS, &result);
    if (ret < 0) {
        set_status_ret(T("Network ioctl failed", "网络 ioctl 失败"), ret);
        copy_text(result_text, sizeof(result_text), T("The kernel rejected the ping request.", "内核拒绝了 ping 请求。"));
        return;
    }
    pos = 0;
    result_text[0] = 0;
    if (result.status == NET_SERVICE_STATUS_OK) {
        append_text(result_text, &pos, sizeof(result_text), T("Reply from ", "来自 "));
        append_text(result_text, &pos, sizeof(result_text), ip_text);
        append_text(result_text, &pos, sizeof(result_text), ": bytes=16 time=");
        append_u32(result_text, &pos, sizeof(result_text), result.rtt_ms);
        append_text(result_text, &pos, sizeof(result_text), "ms");
        copy_text(status_text, sizeof(status_text), T("Ping completed", "Ping 完成"));
    } else {
        append_text(result_text, &pos, sizeof(result_text), status_name(result.status));
        append_text(result_text, &pos, sizeof(result_text), T(" while pinging ", "，目标 "));
        append_text(result_text, &pos, sizeof(result_text), ip_text);
        copy_text(status_text, sizeof(status_text), status_name(result.status));
    }
}

static void draw_ping(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, PING_W, PING_H, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 20, 18, T("Target IPv4:", "目标 IPv4:"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit_state_draw(ui, 116, 14, 236, &input_edit, 0);
    leonos_ui_button(ui, 370, 14, 92, LEONOS_UI_BUTTON_H, T("Ping", "Ping"), 0);
    leonos_ui_text(ui, 20, 58, T("Network:", "网络:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 96, 58, PING_W - 120, detail_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 20, 94, T("Result:", "结果:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 96, 94, PING_W - 120, result_text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 20, 126, T("Mode:", "模式:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 96, 126, T("ARP + IPv4 + ICMP Echo over Intel e1000", "Intel e1000 上的 ARP + IPv4 + ICMP Echo"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_statusbar(ui, PING_H - 28, 28, status_text);
}

static int hit_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= x && py >= y &&
           px < x + (int32_t)w && py < y + (int32_t)h;
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[ping.elf] network ping app starting");
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        copy_text(input_ip, sizeof(input_ip), argv[1]);
    }
    window_id = leonos_gui_create_app_window_ex(T("Ping", "Ping"),
                                                T("ICMP Echo test", "ICMP Echo 测试"),
                                                PING_W, PING_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[ping.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, PING_W, PING_H, PING_W);
    leonos_ui_edit_state_init(&input_edit, input_ip, sizeof(input_ip));
    input_edit.focused = 1;
    refresh_detail();
    draw_ping(&ui);
    leonos_gui_present_window((uint32_t)window_id, PING_W, PING_H, PING_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                if (leonos_ui_edit_state_handle_mouse(&input_edit, event.x, event.y,
                                                      116, 14, 236, event.buttons)) {
                    draw_ping(&ui);
                    leonos_gui_present_window((uint32_t)window_id, PING_W, PING_H, PING_W, pixels);
                }
                if ((event.buttons & 1u) &&
                    hit_rect(event.x, event.y, 370, 14, 92, LEONOS_UI_BUTTON_H)) {
                    run_ping();
                    draw_ping(&ui);
                    leonos_gui_present_window((uint32_t)window_id, PING_W, PING_H, PING_W, pixels);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    return 0;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    run_ping();
                } else if (!leonos_ui_edit_state_handle_key(&input_edit, event.keycode, event.pressed)) {
                    continue;
                }
                draw_ping(&ui);
                leonos_gui_present_window((uint32_t)window_id, PING_W, PING_H, PING_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                draw_ping(&ui);
                leonos_gui_present_window((uint32_t)window_id, PING_W, PING_H, PING_W, pixels);
            }
        }
        sleep_ms(10);
    }
}
