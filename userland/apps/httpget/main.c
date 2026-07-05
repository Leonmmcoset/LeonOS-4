#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define HTTPGET_W 720
#define HTTPGET_H 520
#define HOST_X 76
#define HOST_Y 62
#define HOST_W 248
#define PATH_X 396
#define PATH_Y 62
#define PATH_W 238
#define PORT_X 76
#define PORT_Y 96
#define PORT_W 80
#define GET_X 174
#define GET_Y 96
#define GET_W 82
#define RESPONSE_X 24
#define RESPONSE_Y 134
#define RESPONSE_W (HTTPGET_W - 48)
#define RESPONSE_H 332
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[HTTPGET_W * HTTPGET_H];
static char host_input[LEONOS_NET_HOSTNAME_LEN] = "example.com";
static char path_input[LEONOS_NET_HTTP_PATH_LEN] = "/";
static char port_input[8] = "80";
static char status_text[160] = "Ready";
static char summary_text[192] = "Enter a host and path, then send a HTTP/1.0 GET request.";
static char response_text[LEONOS_NET_HTTP_RESPONSE_MAX + 1];
static struct leonos_ui_edit_state host_edit;
static struct leonos_ui_edit_state path_edit;
static struct leonos_ui_edit_state port_edit;
static struct leonos_ui_text_area_state response_area;

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1u < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1u < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
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

static uint32_t parse_port(const char *text)
{
    uint32_t value = 0;
    uint32_t i = 0;
    if (!text || !text[0]) {
        return 80;
    }
    while (text[i]) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
        if (value > 65535u) {
            return 0;
        }
        ++i;
    }
    return value;
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
    case LEONOS_NET_STATUS_DNS_TIMEOUT:
        return T("DNS timeout", "DNS 超时");
    case LEONOS_NET_STATUS_DNS_FAILED:
        return T("DNS failed", "DNS 失败");
    case LEONOS_NET_STATUS_DNS_NO_ANSWER:
        return T("No A record", "没有 A 记录");
    case LEONOS_NET_STATUS_TCP_TIMEOUT:
        return T("TCP timeout", "TCP 超时");
    case LEONOS_NET_STATUS_TCP_RESET:
        return T("TCP reset", "TCP 复位");
    case LEONOS_NET_STATUS_TCP_FAILED:
        return T("TCP failed", "TCP 失败");
    case LEONOS_NET_STATUS_HTTP_FAILED:
        return T("HTTP failed", "HTTP 失败");
    case LEONOS_NET_STATUS_HTTP_TOO_LARGE:
        return T("Response too large", "响应过大");
    default:
        return T("Unknown status", "未知状态");
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

static void run_http_get(void)
{
    struct leonos_net_http_get result;
    char ip[32];
    uint32_t port = parse_port(port_input);
    uint32_t pos = 0;
    int ret;
    response_text[0] = 0;
    response_area.cursor = 0;
    response_area.scroll_line = 0;
    if (!port) {
        copy_text(status_text, sizeof(status_text), T("Port must be 1-65535", "端口必须是 1-65535"));
        copy_text(summary_text, sizeof(summary_text), status_text);
        return;
    }
    copy_text(status_text, sizeof(status_text), T("Sending HTTP GET...", "正在发送 HTTP GET..."));
    ret = leonos_net_http_get(host_input, path_input, port, 5000, &result);
    if (ret < 0) {
        set_status_ret(T("HTTP ioctl failed", "HTTP ioctl 失败"), ret);
        copy_text(summary_text, sizeof(summary_text), status_text);
        return;
    }
    if (result.response_len) {
        uint32_t copy_len = result.response_len;
        if (copy_len >= sizeof(response_text)) {
            copy_len = sizeof(response_text) - 1u;
        }
        for (uint32_t i = 0; i < copy_len; ++i) {
            response_text[i] = result.response[i] ? result.response[i] : ' ';
        }
        response_text[copy_len] = 0;
    }
    format_ipv4(ip, sizeof(ip), result.remote_ip);
    summary_text[0] = 0;
    append_text(summary_text, &pos, sizeof(summary_text), status_name(result.status));
    append_text(summary_text, &pos, sizeof(summary_text), "  IP ");
    append_text(summary_text, &pos, sizeof(summary_text), ip);
    append_text(summary_text, &pos, sizeof(summary_text), "  HTTP ");
    append_u32(summary_text, &pos, sizeof(summary_text), result.http_status);
    append_text(summary_text, &pos, sizeof(summary_text), "  bytes ");
    append_u32(summary_text, &pos, sizeof(summary_text), result.response_len);
    copy_text(status_text, sizeof(status_text), summary_text);
    leonos_ui_text_area_state_sync(&response_area, RESPONSE_W);
}

static void draw_httpget(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, HTTPGET_W, HTTPGET_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, HTTPGET_W, HTTPGET_H,
                     T("HTTP GET", "HTTP GET"));
    leonos_ui_text(ui, 24, 38, T("HTTP GET over TCP", "通过 TCP 发送 HTTP GET"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    leonos_ui_text(ui, 24, 66, T("Host:", "主机:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, HOST_X, HOST_Y, HOST_W, &host_edit, 0);
    leonos_ui_text(ui, 348, 66, T("Path:", "路径:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, PATH_X, PATH_Y, PATH_W, &path_edit, 0);
    leonos_ui_text(ui, 24, 100, T("Port:", "端口:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, PORT_X, PORT_Y, PORT_W, &port_edit, 0);
    leonos_ui_button(ui, GET_X, GET_Y, GET_W, LEONOS_UI_BUTTON_H,
                     T("GET", "GET"), 0);
    leonos_ui_text_clipped(ui, 276, 100, HTTPGET_W - 300, summary_text,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text_area_state_draw(ui, RESPONSE_X, RESPONSE_Y,
                                   RESPONSE_W, RESPONSE_H,
                                   &response_area, LEONOS_UI_EDIT_READONLY);
    leonos_ui_statusbar(ui, HTTPGET_H - 28, 28, status_text);
}

static int hit_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= x && py >= y &&
           px < x + (int32_t)w && py < y + (int32_t)h;
}

static int handle_edit_mouse(struct leonos_gui_app_event *event)
{
    int changed = 0;
    changed |= leonos_ui_edit_state_handle_mouse(&host_edit, event->x, event->y,
                                                 HOST_X, HOST_Y, HOST_W,
                                                 event->buttons);
    changed |= leonos_ui_edit_state_handle_mouse(&path_edit, event->x, event->y,
                                                 PATH_X, PATH_Y, PATH_W,
                                                 event->buttons);
    changed |= leonos_ui_edit_state_handle_mouse(&port_edit, event->x, event->y,
                                                 PORT_X, PORT_Y, PORT_W,
                                                 event->buttons);
    return changed;
}

static int handle_edit_key(uint8_t keycode, uint8_t pressed)
{
    if (host_edit.focused) {
        return leonos_ui_edit_state_handle_key(&host_edit, keycode, pressed);
    }
    if (path_edit.focused) {
        return leonos_ui_edit_state_handle_key(&path_edit, keycode, pressed);
    }
    if (port_edit.focused) {
        return leonos_ui_edit_state_handle_key(&port_edit, keycode, pressed);
    }
    return 0;
}

static void present(int window_id, struct leonos_ui_surface *ui)
{
    draw_httpget(ui);
    leonos_gui_present_window((uint32_t)window_id, HTTPGET_W, HTTPGET_H,
                              HTTPGET_W, pixels);
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[httpget.elf] HTTP GET app starting");
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        copy_text(host_input, sizeof(host_input), argv[1]);
    }
    if (argc > 2 && argv && argv[2] && argv[2][0]) {
        copy_text(path_input, sizeof(path_input), argv[2]);
    }
    window_id = leonos_gui_create_app_window_ex(T("HTTP GET", "HTTP GET"),
                                                T("TCP and HTTP test", "TCP 和 HTTP 测试"),
                                                HTTPGET_W, HTTPGET_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[httpget.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, HTTPGET_W, HTTPGET_H, HTTPGET_W);
    leonos_ui_edit_state_init(&host_edit, host_input, sizeof(host_input));
    leonos_ui_edit_state_init(&path_edit, path_input, sizeof(path_input));
    leonos_ui_edit_state_init(&port_edit, port_input, sizeof(port_input));
    leonos_ui_text_area_state_init(&response_area, response_text, sizeof(response_text));
    host_edit.focused = 1;
    response_area.readonly = 1;
    present(window_id, &ui);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                int changed = handle_edit_mouse(&event);
                changed |= leonos_ui_text_area_state_handle_mouse(&response_area,
                                                                  event.x, event.y,
                                                                  RESPONSE_X, RESPONSE_Y,
                                                                  RESPONSE_W, RESPONSE_H,
                                                                  event.buttons);
                if ((event.buttons & 1u) &&
                    hit_rect(event.x, event.y, GET_X, GET_Y,
                             GET_W, LEONOS_UI_BUTTON_H)) {
                    run_http_get();
                    changed = 1;
                }
                if (changed) {
                    present(window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (hit_rect(event.x, event.y, RESPONSE_X, RESPONSE_Y,
                             RESPONSE_W, RESPONSE_H)) {
                    if (event.dy < 0) {
                        ++response_area.scroll_line;
                    } else if (response_area.scroll_line) {
                        --response_area.scroll_line;
                    }
                    present(window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    return 0;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    run_http_get();
                    present(window_id, &ui);
                } else if (handle_edit_key(event.keycode, event.pressed) ||
                           leonos_ui_text_area_state_handle_key(&response_area,
                                                               event.keycode,
                                                               event.pressed,
                                                               RESPONSE_W,
                                                               RESPONSE_H)) {
                    present(window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                present(window_id, &ui);
            }
        }
        sleep_ms(10);
    }
}
