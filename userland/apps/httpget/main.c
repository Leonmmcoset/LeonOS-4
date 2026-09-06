#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/net_service.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define HTTPGET_W 720
#define HTTPGET_H 520
#define HOST_X 76
#define HOST_Y 38
#define HOST_W 248
#define PATH_X 396
#define PATH_Y 38
#define PATH_W 238
#define PORT_X 76
#define PORT_Y 72
#define PORT_W 80
#define GET_X 174
#define GET_Y 72
#define GET_W 82
#define HTTPS_X 270
#define HTTPS_Y 76
#define HTTPS_W 94
#define RESPONSE_X 24
#define RESPONSE_Y 110
#define RESPONSE_W (HTTPGET_W - 48)
#define RESPONSE_H 356
#define HTTPGET_RESPONSE_MAX (LEONOS_HTTP_HEADER_MAX + LEONOS_HTTP_BODY_MAX + 4U)
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[HTTPGET_W * HTTPGET_H];
static char host_input[NET_SERVICE_HOSTNAME_LEN] = "example.com";
static char path_input[NET_SERVICE_HTTP_PATH_LEN] = "/";
static char port_input[8] = "80";
static char status_text[160] = "Ready";
static char summary_text[192] = "Enter a host and path, then send an HTTP or HTTPS GET request.";
static char response_body[LEONOS_HTTP_BODY_MAX + 1];
static char response_headers[LEONOS_HTTP_HEADER_MAX + 1];
static char response_text[HTTPGET_RESPONSE_MAX + 1];
static struct leonos_ui_edit_state host_edit;
static struct leonos_ui_edit_state path_edit;
static struct leonos_ui_edit_state port_edit;
static struct leonos_ui_text_area_state response_area;
static uint8_t secure_request;

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

static const char *status_name(uint32_t status)
{
    switch (status) {
    case NET_SERVICE_STATUS_OK:
        return T("OK", "成功");
    case NET_SERVICE_STATUS_NO_DEVICE:
        return T("No e1000 adapter", "没有 e1000 网卡");
    case NET_SERVICE_STATUS_ARP_TIMEOUT:
        return T("ARP timeout", "ARP 超时");
    case NET_SERVICE_STATUS_BAD_ARGUMENT:
        return T("Bad argument", "参数无效");
    case NET_SERVICE_STATUS_TX_FAILED:
        return T("Transmit failed", "发送失败");
    case NET_SERVICE_STATUS_DHCP_TIMEOUT:
        return T("DHCP timeout", "DHCP 超时");
    case NET_SERVICE_STATUS_DHCP_FAILED:
        return T("DHCP failed", "DHCP 失败");
    case NET_SERVICE_STATUS_DNS_TIMEOUT:
        return T("DNS timeout", "DNS 超时");
    case NET_SERVICE_STATUS_DNS_FAILED:
        return T("DNS failed", "DNS 失败");
    case NET_SERVICE_STATUS_DNS_NO_ANSWER:
        return T("No A record", "没有 A 记录");
    case NET_SERVICE_STATUS_TCP_TIMEOUT:
        return T("TCP timeout", "TCP 超时");
    case NET_SERVICE_STATUS_TCP_RESET:
        return T("TCP reset", "TCP 复位");
    case NET_SERVICE_STATUS_TCP_FAILED:
        return T("TCP failed", "TCP 失败");
    case NET_SERVICE_STATUS_HTTP_FAILED:
        return T("HTTP failed", "HTTP 失败");
    case NET_SERVICE_STATUS_HTTP_TOO_LARGE:
        return T("Response too large", "响应过大");
    case NET_SERVICE_STATUS_SOCKET_LIMIT:
        return T("Socket limit reached", "Socket 数量已满");
    case NET_SERVICE_STATUS_SOCKET_BAD_HANDLE:
        return T("Bad socket", "Socket 无效");
    case NET_SERVICE_STATUS_SOCKET_NOT_CONNECTED:
        return T("Socket not connected", "Socket 未连接");
    case NET_SERVICE_STATUS_SOCKET_CLOSED:
        return T("Socket closed", "Socket 已关闭");
    case NET_SERVICE_STATUS_PROTOCOL_UNSUPPORTED:
        return T("Protocol unsupported", "协议不支持");
    case NET_SERVICE_STATUS_TLS_FAILED:
        return T("TLS verification failed", "TLS 验证失败");
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

static uint32_t build_http_url_text(char *dst, uint32_t cap,
                                    const char *host, const char *path,
                                    uint32_t port, uint8_t secure)
{
    uint32_t pos = 0;
    if (!dst || !cap || !host || !host[0]) {
        return 0;
    }
    dst[0] = 0;
    append_text(dst, &pos, cap, secure ? "https://" : "http://");
    append_text(dst, &pos, cap, host);
    if (port != (secure ? 443U : 80U)) {
        append_char(dst, &pos, cap, ':');
        append_u32(dst, &pos, cap, port);
    }
    if (!path || path[0] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, path && path[0] ? path : "/");
    return pos + 1U < cap ? pos : 0;
}

static void sanitize_nuls(char *text, uint32_t len)
{
    for (uint32_t i = 0; text && i < len; ++i) {
        if (!text[i]) {
            text[i] = ' ';
        }
    }
}

static void append_http_response_text(const char *headers, const char *body)
{
    uint32_t pos = 0;
    response_text[0] = 0;
    append_text(response_text, &pos, sizeof(response_text), headers);
    append_text(response_text, &pos, sizeof(response_text), "\n\n");
    append_text(response_text, &pos, sizeof(response_text), body);
}

static void run_http_get(void)
{
    struct leonos_http_response response;
    char url[LEONOS_HTTP_URL_LEN];
    uint32_t port = parse_port(port_input);
    uint32_t pos = 0;
    int ret;
    response_text[0] = 0;
    response_body[0] = 0;
    response_headers[0] = 0;
    response_area.cursor = 0;
    response_area.scroll_line = 0;
    if (!port) {
        copy_text(status_text, sizeof(status_text), T("Port must be 1-65535", "端口必须是 1-65535"));
        copy_text(summary_text, sizeof(summary_text), status_text);
        return;
    }
    if (!build_http_url_text(url, sizeof(url), host_input, path_input, port,
                             secure_request)) {
        copy_text(status_text, sizeof(status_text),
                  T("URL is too large", "URL 过大"));
        copy_text(summary_text, sizeof(summary_text), status_text);
        return;
    }
    copy_text(status_text, sizeof(status_text),
              secure_request ? T("Sending HTTPS GET...", "正在发送 HTTPS GET...")
                             : T("Sending HTTP GET...", "正在发送 HTTP GET..."));
    ret = leonos_http_get(url, LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                          response_body, sizeof(response_body),
                          response_headers, sizeof(response_headers),
                          &response);
    if (ret < 0) {
        set_status_ret(T("HTTP client failed", "HTTP 客户端失败"), ret);
        copy_text(summary_text, sizeof(summary_text), status_text);
        return;
    }
    sanitize_nuls(response_headers, response.headers_len);
    sanitize_nuls(response_body, response.body_len);
    append_http_response_text(response_headers, response_body);
    summary_text[0] = 0;
    append_text(summary_text, &pos, sizeof(summary_text), status_name(response.net_status));
    append_text(summary_text, &pos, sizeof(summary_text), "  HTTP ");
    append_u32(summary_text, &pos, sizeof(summary_text), response.http_status);
    append_text(summary_text, &pos, sizeof(summary_text), "  body ");
    append_u32(summary_text, &pos, sizeof(summary_text), response.body_len);
    append_text(summary_text, &pos, sizeof(summary_text), " bytes");
    if (response.redirect_count) {
        append_text(summary_text, &pos, sizeof(summary_text), "  redirects ");
        append_u32(summary_text, &pos, sizeof(summary_text), response.redirect_count);
    }
    if (response.flags & LEONOS_HTTP_FLAG_CHUNKED) {
        append_text(summary_text, &pos, sizeof(summary_text), "  chunked");
    }
    if (response.flags & LEONOS_HTTP_FLAG_TRUNCATED) {
        append_text(summary_text, &pos, sizeof(summary_text), "  truncated");
    }
    if (response.content_type[0]) {
        append_text(summary_text, &pos, sizeof(summary_text), "  ");
        append_text(summary_text, &pos, sizeof(summary_text), response.content_type);
    }
    copy_text(status_text, sizeof(status_text), summary_text);
    leonos_ui_text_area_state_sync(&response_area, RESPONSE_W);
}

static void draw_httpget(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, HTTPGET_W, HTTPGET_H, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, 14, T("HTTP/HTTPS GET over TCP", "通过 TCP 发送 HTTP/HTTPS GET"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    leonos_ui_text(ui, 24, 42, T("Host:", "主机:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, HOST_X, HOST_Y, HOST_W, &host_edit, 0);
    leonos_ui_text(ui, 348, 42, T("Path:", "路径:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, PATH_X, PATH_Y, PATH_W, &path_edit, 0);
    leonos_ui_text(ui, 24, 76, T("Port:", "端口:"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, PORT_X, PORT_Y, PORT_W, &port_edit, 0);
    leonos_ui_button(ui, GET_X, GET_Y, GET_W, LEONOS_UI_BUTTON_H,
                     T("GET", "GET"), 0);
    leonos_ui_checkbox(ui, HTTPS_X, HTTPS_Y, "HTTPS", secure_request, 0);
    leonos_ui_text_clipped(ui, 372, 76, HTTPGET_W - 396, summary_text,
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
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
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
                if ((event.buttons & 1u) &&
                    hit_rect(event.x, event.y, HTTPS_X, HTTPS_Y,
                             HTTPS_W, LEONOS_UI_BUTTON_H)) {
                    secure_request = !secure_request;
                    if ((secure_request && port_input[0] == '8' &&
                         port_input[1] == '0' && port_input[2] == 0) ||
                        (!secure_request && port_input[0] == '4' &&
                         port_input[1] == '4' && port_input[2] == '3' &&
                         port_input[3] == 0)) {
                        copy_text(port_input, sizeof(port_input),
                                  secure_request ? "443" : "80");
                        leonos_ui_edit_state_sync(&port_edit);
                    }
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
