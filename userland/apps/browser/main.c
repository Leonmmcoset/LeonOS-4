#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#include "litehtml_core.h"

#define BROWSER_INITIAL_W 860U
#define BROWSER_INITIAL_H 600U
#define BROWSER_MIN_W 560U
#define BROWSER_MIN_H 360U
#define BROWSER_MAX_W 1180U
#define BROWSER_MAX_H 760U
#define BROWSER_URL_CAP LEONOS_FS_PATH_LEN
#define BROWSER_SOURCE_CAP 8192U
#define BROWSER_STATUS_CAP 192U
#define BROWSER_TITLE_CAP 72U
#define BROWSER_LINE_CHARS 176U
#define BROWSER_MAX_LINES 512U
#define BROWSER_MAX_LINKS 64U
#define BROWSER_HISTORY_MAX 16U
#define BROWSER_MENU_H 26U
#define BROWSER_TOOLBAR_H 30U
#define BROWSER_ADDR_H 34U
#define BROWSER_MENU_ITEM_H (LEONOS_FONT_H + 8U)
#define BROWSER_MENU_ROW_STEP 26U
#define BROWSER_MENU_FILE_X 8U
#define BROWSER_MENU_FILE_W 52U
#define BROWSER_MENU_EDIT_X 64U
#define BROWSER_MENU_EDIT_W 52U
#define BROWSER_MENU_VIEW_X 120U
#define BROWSER_MENU_VIEW_W 56U
#define BROWSER_MENU_FAVORITES_X 182U
#define BROWSER_MENU_FAVORITES_W 92U
#define BROWSER_MENU_HELP_X 280U
#define BROWSER_MENU_HELP_W 52U
#define BROWSER_PAGE_X 8U
#define BROWSER_STATUS_H 28U
#define BROWSER_LINE_H (LEONOS_FONT_H + 2U)
#define BROWSER_SCROLL_W 18U
#define BROWSER_NAV_GAP 4U
#define BROWSER_BACK_X 8U
#define BROWSER_BACK_W 64U
#define BROWSER_FORWARD_W 82U
#define BROWSER_REFRESH_W 76U
#define BROWSER_HOME_W 62U
#define BROWSER_STOP_W 58U
#define BROWSER_GO_W 54U
#define BROWSER_LINK_BLUE 0x000000ccU
#define BROWSER_TEXT_DARK 0x00202020U
#define BROWSER_IE_NAVY 0x00000080U
#define BROWSER_IE_SKY 0x00d8e8f8U
#define BROWSER_QUOTE_BG 0x00f5f5f5U
#define BROWSER_TABLE_BG 0x00f7fbffU
#define BROWSER_TABLE_BORDER 0x00a8b8c8U
#define BROWSER_CODE_BG 0x00eeeeeeU
#define BROWSER_IMAGE_BG 0x00f0f4f8U
#define T(en, zh) leonos_i18n((en), (zh))

enum browser_menu {
    BROWSER_MENU_NONE = 0,
    BROWSER_MENU_FILE = 1,
    BROWSER_MENU_EDIT = 2,
    BROWSER_MENU_VIEW = 3,
    BROWSER_MENU_FAVORITES = 4,
    BROWSER_MENU_HELP = 5,
};

struct parsed_http_url {
    char host[LEONOS_NET_HOSTNAME_LEN];
    char path[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t port;
};

static uint32_t pixels[BROWSER_MAX_W * BROWSER_MAX_H];
static struct leonos_ui_surface ui;
static int window_id;
static uint32_t view_w = BROWSER_INITIAL_W;
static uint32_t view_h = BROWSER_INITIAL_H;
static char address_input[BROWSER_URL_CAP] = "about:leonos";
static struct leonos_ui_edit_state address_edit;
static char status_text[BROWSER_STATUS_CAP] = "Ready";
static char page_title[BROWSER_TITLE_CAP] = "LeonOS Browser";
static char current_location[BROWSER_URL_CAP] = "about:leonos";
static char page_source[BROWSER_SOURCE_CAP];
static uint8_t page_is_html;
static uint8_t source_truncated;
static struct browser_line lines[BROWSER_MAX_LINES];
static uint32_t line_count = 1;
static uint32_t scroll_line;
static struct browser_link links[BROWSER_MAX_LINKS];
static uint32_t link_count;
static char history[BROWSER_HISTORY_MAX][BROWSER_URL_CAP];
static uint32_t history_count;
static int32_t history_index = -1;
static uint8_t menu_open;
static uint8_t browser_should_exit;
static struct leonos_net_http_get http_result;

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int text_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int text_eq_ignore_case(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && ascii_tolower(a[i]) == ascii_tolower(b[i])) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int starts_with_ignore_case(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (ascii_tolower(text[i]) != ascii_tolower(prefix[i])) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int ends_with_ignore_case(const char *text, const char *suffix)
{
    uint32_t text_len = (uint32_t)strlen(text);
    uint32_t suffix_len = (uint32_t)strlen(suffix);
    if (!text || !suffix || suffix_len > text_len) {
        return 0;
    }
    return text_eq_ignore_case(text + text_len - suffix_len, suffix);
}

static int is_space_char(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
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
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static void append_i32(char *dst, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        append_char(dst, pos, cap, '-');
        value = -value;
    }
    append_u32(dst, pos, cap, (uint32_t)value);
}

static void trim_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t start = 0;
    uint32_t end;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && is_space_char(src[start])) {
        ++start;
    }
    end = start;
    while (src && src[end]) {
        ++end;
    }
    while (end > start && is_space_char(src[end - 1U])) {
        --end;
    }
    while (start < end && pos + 1U < cap) {
        dst[pos++] = src[start++];
    }
    dst[pos] = 0;
}

static uint32_t page_y(void)
{
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + BROWSER_ADDR_H + 4U;
}

static uint32_t page_w(void)
{
    return view_w > BROWSER_PAGE_X * 2U ? view_w - BROWSER_PAGE_X * 2U : 80U;
}

static uint32_t page_h(void)
{
    uint32_t y = page_y();
    if (view_h <= y + BROWSER_STATUS_H + 4U) {
        return BROWSER_LINE_H;
    }
    return view_h - y - BROWSER_STATUS_H - 4U;
}

static uint32_t text_x(void)
{
    return BROWSER_PAGE_X + 8U;
}

static uint32_t text_y(void)
{
    return page_y() + 8U;
}

static uint32_t text_cols(void)
{
    uint32_t w = page_w();
    uint32_t cols;
    if (w <= BROWSER_SCROLL_W + 24U) {
        return 16U;
    }
    cols = (w - BROWSER_SCROLL_W - 24U) / LEONOS_FONT_W;
    if (cols < 16U) {
        cols = 16U;
    }
    if (cols >= BROWSER_LINE_CHARS) {
        cols = BROWSER_LINE_CHARS - 1U;
    }
    return cols;
}

static uint32_t visible_rows(void)
{
    uint32_t h = page_h();
    uint32_t rows;
    if (h <= 16U) {
        return 1U;
    }
    rows = (h - 16U) / BROWSER_LINE_H;
    return rows ? rows : 1U;
}

static void clamp_scroll(void)
{
    uint32_t rows = visible_rows();
    if (line_count <= rows) {
        scroll_line = 0;
    } else if (scroll_line + rows > line_count) {
        scroll_line = line_count - rows;
    }
}

static void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
}

static const char *net_status_name(uint32_t status)
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
        return T("Unknown network status", "未知网络状态");
    }
}

static int parse_http_url(const char *url, struct parsed_http_url *out)
{
    const char *p;
    uint32_t host_pos = 0;
    uint32_t path_pos = 0;
    uint32_t port = 80;
    if (!url || !out || !starts_with_ignore_case(url, "http://")) {
        return 0;
    }
    p = url + 7;
    while (*p && *p != '/' && *p != ':' && *p != '#' &&
           host_pos + 1U < sizeof(out->host)) {
        out->host[host_pos++] = *p++;
    }
    out->host[host_pos] = 0;
    if (!out->host[0]) {
        return 0;
    }
    if (*p == ':') {
        port = 0;
        ++p;
        while (is_digit(*p)) {
            port = port * 10U + (uint32_t)(*p - '0');
            if (port > 65535U) {
                return 0;
            }
            ++p;
        }
        if (port == 0) {
            return 0;
        }
    }
    if (*p == '/') {
        while (*p && *p != '#' && path_pos + 1U < sizeof(out->path)) {
            out->path[path_pos++] = *p++;
        }
    }
    if (path_pos == 0) {
        out->path[path_pos++] = '/';
    }
    out->path[path_pos] = 0;
    out->port = port;
    return 1;
}

static void build_http_url(char *dst, uint32_t cap, const char *host,
                           uint32_t port, const char *path)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, "http://");
    append_text(dst, &pos, cap, host);
    if (port != 80U) {
        append_char(dst, &pos, cap, ':');
        append_u32(dst, &pos, cap, port);
    }
    if (!path || path[0] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, path && path[0] ? path : "/");
}

static int is_drive_path(const char *text)
{
    return text && text[0] && text[1] == ':' && text[2] == '/';
}

static void normalize_location(const char *input, char *out, uint32_t cap)
{
    char tmp[BROWSER_URL_CAP];
    uint32_t pos = 0;
    trim_copy(tmp, sizeof(tmp), input);
    if (!tmp[0]) {
        copy_text(out, cap, "about:leonos");
        return;
    }
    if (starts_with_ignore_case(tmp, "http://") ||
        starts_with_ignore_case(tmp, "https://") ||
        starts_with_ignore_case(tmp, "about:") ||
        is_drive_path(tmp)) {
        copy_text(out, cap, tmp);
        return;
    }
    out[0] = 0;
    append_text(out, &pos, cap, "http://");
    append_text(out, &pos, cap, tmp);
}

static void render_html_source(const char *source, const char *base_url)
{
    struct litehtml_core_view view = {
        .lines = lines,
        .max_lines = BROWSER_MAX_LINES,
        .line_chars = BROWSER_LINE_CHARS,
        .links = links,
        .max_links = BROWSER_MAX_LINKS,
        .line_count = &line_count,
        .link_count = &link_count,
        .scroll_line = &scroll_line,
        .page_title = page_title,
        .page_title_cap = sizeof(page_title),
        .source_truncated = &source_truncated,
        .cols = text_cols(),
    };
    litehtml_core_render_html(&view, source, base_url);
    clamp_scroll();
}

static void render_plain_source(const char *source)
{
    struct litehtml_core_view view = {
        .lines = lines,
        .max_lines = BROWSER_MAX_LINES,
        .line_chars = BROWSER_LINE_CHARS,
        .links = links,
        .max_links = BROWSER_MAX_LINKS,
        .line_count = &line_count,
        .link_count = &link_count,
        .scroll_line = &scroll_line,
        .page_title = page_title,
        .page_title_cap = sizeof(page_title),
        .source_truncated = &source_truncated,
        .cols = text_cols(),
    };
    litehtml_core_render_plain(&view, source);
    clamp_scroll();
}

static void rerender_page(void)
{
    if (page_is_html) {
        render_html_source(page_source, current_location);
    } else {
        render_plain_source(page_source);
    }
}

static void set_page_source(const char *title, const char *source,
                            uint8_t is_html, const char *status)
{
    copy_text(page_title, sizeof(page_title), title && title[0] ? title : T("Untitled", "无标题"));
    copy_text(page_source, sizeof(page_source), source ? source : "");
    page_is_html = is_html;
    source_truncated = 0;
    rerender_page();
    set_status(status);
}

static void render_message_page(const char *title, const char *message,
                                const char *detail)
{
    char text[BROWSER_SOURCE_CAP];
    uint32_t pos = 0;
    text[0] = 0;
    append_text(text, &pos, sizeof(text), title);
    append_text(text, &pos, sizeof(text), "\n\n");
    append_text(text, &pos, sizeof(text), message);
    if (detail && detail[0]) {
        append_text(text, &pos, sizeof(text), "\n\n");
        append_text(text, &pos, sizeof(text), detail);
    }
    set_page_source(title, text, 0, message);
}

static void push_history(const char *url)
{
    if (!url || !url[0]) {
        return;
    }
    if (history_index >= 0 &&
        text_eq(history[(uint32_t)history_index], url)) {
        return;
    }
    if (history_index >= 0 && (uint32_t)history_index + 1U < history_count) {
        history_count = (uint32_t)history_index + 1U;
    }
    if (history_count >= BROWSER_HISTORY_MAX) {
        for (uint32_t i = 1; i < history_count; ++i) {
            copy_text(history[i - 1U], sizeof(history[0]), history[i]);
        }
        history_count = BROWSER_HISTORY_MAX - 1U;
        history_index = (int32_t)history_count - 1;
    }
    copy_text(history[history_count], sizeof(history[0]), url);
    history_index = (int32_t)history_count;
    ++history_count;
}

static void format_ret_status(char *dst, uint32_t cap, const char *prefix, int32_t ret)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, prefix);
    append_text(dst, &pos, cap, " ret=");
    append_i32(dst, &pos, cap, ret);
}

static void present_browser(void);

static void load_about(void)
{
    static const char about_html[] =
        "<html><head><title>LeonOS Browser</title></head>"
        "<body><h1>LeonOS Browser</h1>"
        "<p>Classic IE-style browser shell for LeonOS 4.</p>"
        "<p>This build supports HTTP, local .html files, headings, blocks, "
        "lists, tables, blockquotes, links, inline styles, image placeholders, "
        "basic CSS, history, refresh, and scrolling.</p>"
        "<p>HTTPS, JavaScript, images, full CSS layout, and full litehtml are not "
        "enabled yet.</p>"
        "<p><a href=\"http://example.com/\">Open example.com</a></p>"
        "</body></html>";
    copy_text(current_location, sizeof(current_location), "about:leonos");
    copy_text(address_input, sizeof(address_input), current_location);
    leonos_ui_edit_state_sync(&address_edit);
    set_page_source("LeonOS Browser", about_html, 1, T("Ready", "就绪"));
}

static uint32_t response_body_offset(const char *data, uint32_t len)
{
    for (uint32_t i = 0; i + 3U < len; ++i) {
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n') {
            return i + 4U;
        }
    }
    for (uint32_t i = 0; i + 1U < len; ++i) {
        if (data[i] == '\n' && data[i + 1U] == '\n') {
            return i + 2U;
        }
    }
    return 0;
}

static void load_http_url(const char *url)
{
    struct parsed_http_url parsed;
    uint32_t body_offset;
    uint32_t body_len;
    uint32_t copy_len;
    uint32_t pos = 0;
    int ret;
    char normalized[BROWSER_URL_CAP];
    char status[BROWSER_STATUS_CAP];
    if (!parse_http_url(url, &parsed)) {
        render_message_page(T("Invalid URL", "无效地址"),
                            T("The address could not be parsed as HTTP.", "无法把该地址解析为 HTTP。"),
                            url);
        return;
    }
    build_http_url(normalized, sizeof(normalized), parsed.host, parsed.port, parsed.path);
    copy_text(current_location, sizeof(current_location), normalized);
    copy_text(address_input, sizeof(address_input), normalized);
    leonos_ui_edit_state_sync(&address_edit);
    set_status(T("Opening page...", "正在打开页面..."));
    present_browser();
    ret = leonos_net_http_get(parsed.host, parsed.path, parsed.port, 5000, &http_result);
    if (ret < 0) {
        format_ret_status(status, sizeof(status), T("HTTP ioctl failed", "HTTP ioctl 失败"), ret);
        render_message_page(T("Network Error", "网络错误"), status, normalized);
        return;
    }
    status[0] = 0;
    append_text(status, &pos, sizeof(status), net_status_name(http_result.status));
    append_text(status, &pos, sizeof(status), "  HTTP ");
    append_u32(status, &pos, sizeof(status), http_result.http_status);
    append_text(status, &pos, sizeof(status), "  ");
    append_u32(status, &pos, sizeof(status), http_result.response_len);
    append_text(status, &pos, sizeof(status), " bytes");
    if (http_result.status != LEONOS_NET_STATUS_OK) {
        render_message_page(T("Network Error", "网络错误"), status, normalized);
        return;
    }
    body_offset = response_body_offset(http_result.response, http_result.response_len);
    body_len = body_offset < http_result.response_len
                   ? http_result.response_len - body_offset
                   : http_result.response_len;
    copy_len = body_len;
    if (copy_len >= sizeof(page_source)) {
        copy_len = sizeof(page_source) - 1U;
        source_truncated = 1;
    }
    for (uint32_t i = 0; i < copy_len; ++i) {
        char ch = http_result.response[body_offset + i];
        page_source[i] = ch ? ch : ' ';
    }
    page_source[copy_len] = 0;
    copy_text(page_title, sizeof(page_title), parsed.host);
    page_is_html = 1;
    rerender_page();
    set_status(status);
}

static void load_local_file(const char *path)
{
    int fd;
    uint32_t len = 0;
    char status[BROWSER_STATUS_CAP];
    source_truncated = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        format_ret_status(status, sizeof(status), T("Open failed", "打开失败"), fd);
        render_message_page(T("File Error", "文件错误"), status, path);
        return;
    }
    for (;;) {
        long got;
        uint32_t free_bytes = sizeof(page_source) - len - 1U;
        if (free_bytes == 0) {
            source_truncated = 1;
            break;
        }
        got = read(fd, page_source + len, free_bytes);
        if (got < 0) {
            close(fd);
            format_ret_status(status, sizeof(status), T("Read failed", "读取失败"), (int32_t)got);
            render_message_page(T("File Error", "文件错误"), status, path);
            return;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    page_source[len] = 0;
    copy_text(current_location, sizeof(current_location), path);
    copy_text(address_input, sizeof(address_input), path);
    leonos_ui_edit_state_sync(&address_edit);
    copy_text(page_title, sizeof(page_title), path);
    page_is_html = ends_with_ignore_case(path, ".html") || ends_with_ignore_case(path, ".htm");
    rerender_page();
    set_status(source_truncated ? T("File loaded, truncated", "文件已打开，内容被截断")
                                : T("File loaded", "文件已打开"));
}

static void navigate_to(const char *input, uint8_t add_to_history)
{
    char url[BROWSER_URL_CAP];
    normalize_location(input, url, sizeof(url));
    if (starts_with_ignore_case(url, "about:")) {
        load_about();
    } else if (starts_with_ignore_case(url, "https://")) {
        copy_text(current_location, sizeof(current_location), url);
        copy_text(address_input, sizeof(address_input), url);
        leonos_ui_edit_state_sync(&address_edit);
        render_message_page(T("HTTPS Unsupported", "不支持 HTTPS"),
                            T("LeonOS Browser can only open http:// pages in this build.",
                              "这个版本的 LeonOS Browser 只能打开 http:// 页面。"),
                            url);
    } else if (starts_with_ignore_case(url, "http://")) {
        load_http_url(url);
    } else if (is_drive_path(url)) {
        load_local_file(url);
    } else {
        render_message_page(T("Unsupported Address", "不支持的地址"),
                            T("Use http://, about:, or a LeonOS file path such as 0:/file.html.",
                              "请使用 http://、about:，或类似 0:/file.html 的 LeonOS 文件路径。"),
                            url);
    }
    if (add_to_history) {
        push_history(current_location);
    }
}

static void go_back(void)
{
    if (history_index > 0) {
        --history_index;
        navigate_to(history[(uint32_t)history_index], 0);
    }
}

static void go_forward(void)
{
    if (history_index >= 0 && (uint32_t)history_index + 1U < history_count) {
        ++history_index;
        navigate_to(history[(uint32_t)history_index], 0);
    }
}

static uint32_t button_y(void)
{
    return BROWSER_MENU_H + 3U;
}

static uint32_t address_y(void)
{
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + 5U;
}

static uint32_t address_w(void)
{
    uint32_t x = 74U;
    uint32_t go_w = BROWSER_GO_W;
    if (view_w <= x + go_w + 20U) {
        return 120U;
    }
    return view_w - x - go_w - 20U;
}

static uint32_t go_x(void)
{
    return 74U + address_w() + 8U;
}

static uint32_t toolbar_forward_x(void)
{
    return BROWSER_BACK_X + BROWSER_BACK_W + BROWSER_NAV_GAP;
}

static uint32_t toolbar_refresh_x(void)
{
    return toolbar_forward_x() + BROWSER_FORWARD_W + BROWSER_NAV_GAP;
}

static uint32_t toolbar_home_x(void)
{
    return toolbar_refresh_x() + BROWSER_REFRESH_W + BROWSER_NAV_GAP;
}

static uint32_t toolbar_stop_x(void)
{
    return toolbar_home_x() + BROWSER_HOME_W + BROWSER_NAV_GAP;
}

static uint32_t toolbar_title_x(void)
{
    return toolbar_stop_x() + BROWSER_STOP_W + 12U;
}

static uint32_t menu_row_y(uint32_t row)
{
    return BROWSER_MENU_H + 8U + row * BROWSER_MENU_ROW_STEP;
}

static int hit_rect_i(int32_t px, int32_t py, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static void draw_toolbar_button(uint32_t x, uint32_t w, const char *label,
                                uint32_t disabled)
{
    leonos_ui_toolbar_button(&ui, x, button_y(), w, label,
                             disabled ? LEONOS_UI_TOOLBAR_BUTTON_DISABLED : 0);
}

static void draw_line_run(uint32_t x, uint32_t y, const char *text,
                          uint32_t len, uint32_t fg, uint32_t bg,
                          uint8_t underline, uint8_t bold,
                          uint8_t italic, uint8_t code)
{
    char tmp[BROWSER_LINE_CHARS];
    uint32_t copy_len = len;
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1U;
    }
    for (uint32_t i = 0; i < copy_len; ++i) {
        tmp[i] = text[i];
    }
    tmp[copy_len] = 0;
    if (code && copy_len) {
        if (bg == LEONOS_UI_WHITE) {
            bg = BROWSER_CODE_BG;
        }
        leonos_ui_rect(&ui, x > 1U ? x - 1U : x, y > 1U ? y - 1U : y,
                       copy_len * LEONOS_FONT_W + 2U, LEONOS_FONT_H + 2U,
                       bg);
    }
    leonos_ui_text(&ui, x, y, tmp, fg, bg);
    if (bold) {
        leonos_ui_text(&ui, x + 1U, y, tmp, fg, bg);
    }
    if (italic) {
        for (uint32_t n = 0; n < copy_len; ++n) {
            uint32_t sx = x + n * LEONOS_FONT_W + 1U;
            leonos_ui_rect(&ui, sx, y + LEONOS_FONT_H - 3U, 3U, 1U, fg);
        }
    }
    if (underline && copy_len) {
        leonos_ui_rect(&ui, x, y + LEONOS_FONT_H - 1U,
                       copy_len * LEONOS_FONT_W, 1U, fg);
    }
}

static uint8_t line_is_heading(uint8_t kind)
{
    return kind == BROWSER_LINE_HEADING1 ||
           kind == BROWSER_LINE_HEADING2 ||
           kind == BROWSER_LINE_HEADING3;
}

static uint32_t document_text_w(void)
{
    return page_w() > BROWSER_SCROLL_W + 24U
               ? page_w() - BROWSER_SCROLL_W - 24U
               : 80U;
}

static void draw_document_line_frame(const struct browser_line *line,
                                     uint32_t x, uint32_t y,
                                     uint32_t width)
{
    uint32_t content_x;
    uint32_t content_w;
    uint32_t bg;
    uint32_t border;
    if (!line) {
        return;
    }
    content_x = x + (uint32_t)line->indent * LEONOS_FONT_W;
    content_w = width > (content_x - x) ? width - (content_x - x) : width;
    bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : LEONOS_UI_WHITE;
    border = line->border_color != BROWSER_COLOR_UNSET
                 ? line->border_color
                 : BROWSER_TABLE_BORDER;
    if (line->kind == BROWSER_LINE_HR) {
        uint32_t hr = line->border_color != BROWSER_COLOR_UNSET
                          ? line->border_color
                          : LEONOS_UI_DARK;
        leonos_ui_rect(&ui, x, y + LEONOS_FONT_H / 2U, width, 1U, hr);
        leonos_ui_rect(&ui, x, y + LEONOS_FONT_H / 2U + 1U, width, 1U,
                       LEONOS_UI_LIGHT);
        return;
    }
    if (line->line_bg != BROWSER_COLOR_UNSET) {
        leonos_ui_rect(&ui, content_x, y - 1U,
                       content_w > 6U ? content_w - 6U : content_w,
                       LEONOS_FONT_H + 3U, bg);
    }
    if (line->border_color != BROWSER_COLOR_UNSET &&
        line->kind != BROWSER_LINE_TABLE &&
        line->kind != BROWSER_LINE_BLOCKQUOTE) {
        uint32_t bar_x = content_x >= 5U ? content_x - 5U : content_x;
        leonos_ui_rect(&ui, bar_x, y - 1U, 3U,
                       LEONOS_FONT_H + 3U, border);
    }
    if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
        uint32_t bar_x = content_x >= 8U ? content_x - 8U : x;
        leonos_ui_rect(&ui, bar_x, y - 1U, 3U, LEONOS_FONT_H + 3U,
                       border);
        leonos_ui_rect(&ui, bar_x + 3U, y - 1U,
                       content_w > 3U ? content_w - 3U : content_w,
                       LEONOS_FONT_H + 3U,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_QUOTE_BG);
        return;
    }
    if (line->kind == BROWSER_LINE_TABLE) {
        uint32_t row_w = content_w > 6U ? content_w - 6U : content_w;
        leonos_ui_rect(&ui, content_x, y - 1U, row_w,
                       LEONOS_FONT_H + 3U,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_TABLE_BG);
        leonos_ui_rect(&ui, content_x, y - 1U, row_w, 1U, border);
        leonos_ui_rect(&ui, content_x, y + LEONOS_FONT_H + 1U,
                       row_w, 1U, border);
        for (uint32_t i = 0; i < line->len; ++i) {
            if (line->text[i] == '|') {
                uint32_t vx = content_x + i * LEONOS_FONT_W + LEONOS_FONT_W / 2U;
                leonos_ui_rect(&ui, vx, y - 1U, 1U, LEONOS_FONT_H + 3U,
                               border);
            }
        }
        return;
    }
    if (line->kind == BROWSER_LINE_IMAGE) {
        leonos_ui_rect(&ui, content_x, y - 1U, content_w > 6U ? content_w - 6U : content_w,
                       LEONOS_FONT_H + 4U,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_IMAGE_BG);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 14U, 14U, LEONOS_UI_WHITE);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 14U, 1U, border);
        leonos_ui_rect(&ui, content_x + 2U, y + 14U, 14U, 1U, border);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 1U, 14U, border);
        leonos_ui_rect(&ui, content_x + 15U, y + 1U, 1U, 14U, border);
    }
}

static uint32_t line_align_shift_px(const struct browser_line *line,
                                    uint32_t doc_w)
{
    uint32_t total_cols = doc_w / LEONOS_FONT_W;
    uint32_t content_cols;
    uint32_t image_cols;
    if (!line || line->align == BROWSER_ALIGN_LEFT ||
        total_cols <= line->indent) {
        return 0;
    }
    content_cols = total_cols - line->indent;
    image_cols = line->kind == BROWSER_LINE_IMAGE ? 20U / LEONOS_FONT_W : 0U;
    if (content_cols <= image_cols + line->len) {
        return 0;
    }
    content_cols -= image_cols;
    if (line->align == BROWSER_ALIGN_RIGHT) {
        return (content_cols - line->len) * LEONOS_FONT_W;
    }
    return ((content_cols - line->len) / 2U) * LEONOS_FONT_W;
}

static void draw_document_lines(void)
{
    uint32_t px = text_x();
    uint32_t py = text_y();
    uint32_t rows = visible_rows();
    uint32_t max_line = scroll_line + rows;
    uint32_t doc_w = document_text_w();
    uint32_t text_bg = LEONOS_UI_WHITE;
    if (max_line > line_count) {
        max_line = line_count;
    }
    for (uint32_t row = 0; scroll_line + row < max_line; ++row) {
        struct browser_line *line = &lines[scroll_line + row];
        uint32_t y = py + row * BROWSER_LINE_H;
        uint32_t line_px = px + (uint32_t)line->indent * LEONOS_FONT_W;
        uint32_t image_text_offset = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
        uint32_t start = 0;
        if (line->len == 0 && line->kind != BROWSER_LINE_HR) {
            continue;
        }
        draw_document_line_frame(line, px, y, doc_w);
        if (line->kind == BROWSER_LINE_HR) {
            continue;
        }
        line_px += line_align_shift_px(line, doc_w);
        while (start < line->len) {
            uint8_t link = line->link[start];
            uint8_t style = line->style[start];
            uint32_t run_fg = line->fg[start];
            uint32_t run_bg = line->bg[start];
            uint32_t end = start + 1U;
            uint32_t fg = line_is_heading(line->kind) ? BROWSER_IE_NAVY : BROWSER_TEXT_DARK;
            uint32_t bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : text_bg;
            uint8_t underline = 0;
            uint8_t bold = line_is_heading(line->kind) ||
                           (style & BROWSER_TEXT_BOLD);
            uint8_t italic = (style & BROWSER_TEXT_ITALIC) != 0;
            uint8_t code = (style & BROWSER_TEXT_CODE) != 0;
            while (end < line->len && line->link[end] == link &&
                   line->style[end] == style &&
                   line->fg[end] == run_fg &&
                   line->bg[end] == run_bg) {
                ++end;
            }
            if (run_fg != BROWSER_COLOR_UNSET) {
                fg = run_fg;
            }
            if (run_bg != BROWSER_COLOR_UNSET) {
                bg = run_bg;
            }
            if (link) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = BROWSER_LINK_BLUE;
                }
                underline = 1;
                bold = (style & BROWSER_TEXT_BOLD) != 0;
            } else if (line->kind == BROWSER_LINE_MUTED) {
                fg = LEONOS_UI_DARK;
            } else if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00484848U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_QUOTE_BG;
                }
            } else if (line->kind == BROWSER_LINE_TABLE) {
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_TABLE_BG;
                }
            } else if (line->kind == BROWSER_LINE_IMAGE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00304050U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_IMAGE_BG;
                }
            }
            if (line->kind == BROWSER_LINE_HEADING1 ||
                (style & BROWSER_TEXT_UNDERLINE)) {
                underline = 1;
            }
            draw_line_run(line_px + image_text_offset + start * LEONOS_FONT_W, y,
                          line->text + start, end - start, fg, bg,
                          underline, bold, italic, code);
            start = end;
        }
    }
}

static void draw_browser_menu(void)
{
    if (menu_open == BROWSER_MENU_FILE) {
        leonos_ui_menu(&ui, BROWSER_MENU_FILE_X, BROWSER_MENU_H, 188U, 86U);
        leonos_ui_menu_item(&ui, BROWSER_MENU_FILE_X + 34U, menu_row_y(0), 146U,
                            T("Home", "主页"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_FILE_X + 34U, menu_row_y(1), 146U,
                            T("Refresh", "刷新"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_FILE_X + 34U, menu_row_y(2), 146U,
                            T("Close", "关闭"), 0);
    } else if (menu_open == BROWSER_MENU_EDIT) {
        leonos_ui_menu(&ui, BROWSER_MENU_EDIT_X, BROWSER_MENU_H, 192U, 60U);
        leonos_ui_menu_item(&ui, BROWSER_MENU_EDIT_X + 34U, menu_row_y(0), 150U,
                            T("Select Address", "选中地址"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_EDIT_X + 34U, menu_row_y(1), 150U,
                            T("Clear Address", "清空地址"), 0);
    } else if (menu_open == BROWSER_MENU_VIEW) {
        leonos_ui_menu(&ui, BROWSER_MENU_VIEW_X, BROWSER_MENU_H, 166U, 86U);
        leonos_ui_menu_item(&ui, BROWSER_MENU_VIEW_X + 34U, menu_row_y(0), 124U,
                            T("Refresh", "刷新"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_VIEW_X + 34U, menu_row_y(1), 124U,
                            T("Top", "顶部"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_VIEW_X + 34U, menu_row_y(2), 124U,
                            T("Bottom", "底部"), 0);
    } else if (menu_open == BROWSER_MENU_FAVORITES) {
        leonos_ui_menu(&ui, BROWSER_MENU_FAVORITES_X, BROWSER_MENU_H, 204U, 60U);
        leonos_ui_menu_item(&ui, BROWSER_MENU_FAVORITES_X + 34U, menu_row_y(0), 162U,
                            T("LeonOS Home", "LeonOS 主页"), 0);
        leonos_ui_menu_item(&ui, BROWSER_MENU_FAVORITES_X + 34U, menu_row_y(1), 162U,
                            "example.com", 0);
    } else if (menu_open == BROWSER_MENU_HELP) {
        leonos_ui_menu(&ui, BROWSER_MENU_HELP_X, BROWSER_MENU_H, 176U, 34U);
        leonos_ui_menu_item(&ui, BROWSER_MENU_HELP_X + 34U, menu_row_y(0), 134U,
                            T("About Browser", "关于浏览器"), 0);
    }
}

static void draw_browser(void)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t rows = visible_rows();
    uint32_t can_back = history_index > 0;
    uint32_t can_forward = history_index >= 0 && (uint32_t)history_index + 1U < history_count;
    char title_line[BROWSER_TITLE_CAP + 32U];
    uint32_t pos = 0;
    leonos_ui_rect(&ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_menubar(&ui, 0, 0, view_w);
    leonos_ui_menubar_item(&ui, BROWSER_MENU_FILE_X, 0, BROWSER_MENU_FILE_W,
                           T("File", "文件"), menu_open == BROWSER_MENU_FILE);
    leonos_ui_menubar_item(&ui, BROWSER_MENU_EDIT_X, 0, BROWSER_MENU_EDIT_W,
                           T("Edit", "编辑"), menu_open == BROWSER_MENU_EDIT);
    leonos_ui_menubar_item(&ui, BROWSER_MENU_VIEW_X, 0, BROWSER_MENU_VIEW_W,
                           T("View", "查看"), menu_open == BROWSER_MENU_VIEW);
    leonos_ui_menubar_item(&ui, BROWSER_MENU_FAVORITES_X, 0, BROWSER_MENU_FAVORITES_W,
                           T("Favorites", "收藏夹"), menu_open == BROWSER_MENU_FAVORITES);
    leonos_ui_menubar_item(&ui, BROWSER_MENU_HELP_X, 0, BROWSER_MENU_HELP_W,
                           T("Help", "帮助"), menu_open == BROWSER_MENU_HELP);
    leonos_ui_toolbar(&ui, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H);
    draw_toolbar_button(BROWSER_BACK_X, BROWSER_BACK_W, T("Back", "后退"), !can_back);
    draw_toolbar_button(toolbar_forward_x(), BROWSER_FORWARD_W, T("Forward", "前进"), !can_forward);
    draw_toolbar_button(toolbar_refresh_x(), BROWSER_REFRESH_W, T("Refresh", "刷新"), 0);
    draw_toolbar_button(toolbar_home_x(), BROWSER_HOME_W, T("Home", "主页"), 0);
    draw_toolbar_button(toolbar_stop_x(), BROWSER_STOP_W, T("Stop", "停止"), 1);
    title_line[0] = 0;
    append_text(title_line, &pos, sizeof(title_line), "LeonOS Browser - ");
    append_text(title_line, &pos, sizeof(title_line), page_title);
    leonos_ui_text_clipped(&ui, toolbar_title_x(), button_y() + 5U,
                           view_w > toolbar_title_x() + 12U ? view_w - toolbar_title_x() - 12U : 80U,
                           title_line, BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
    leonos_ui_rect(&ui, 0, BROWSER_MENU_H + BROWSER_TOOLBAR_H, view_w,
                   BROWSER_ADDR_H, BROWSER_IE_SKY);
    leonos_ui_text(&ui, 12, address_y() + 5U, T("Address", "地址"),
                   BROWSER_TEXT_DARK, BROWSER_IE_SKY);
    leonos_ui_edit_state_draw(&ui, 74, address_y(), address_w(), &address_edit, 0);
    leonos_ui_button(&ui, go_x(), address_y(), BROWSER_GO_W, LEONOS_UI_BUTTON_H,
                     T("Go", "转到"), 0);
    leonos_ui_inset(&ui, BROWSER_PAGE_X, p_y, p_w, p_h, LEONOS_UI_WHITE);
    draw_document_lines();
    leonos_ui_vscrollbar(&ui, BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U,
                         p_y + 2U, BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h,
                         scroll_line, line_count ? line_count : 1U, rows,
                         line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    if (source_truncated) {
        char truncated[BROWSER_STATUS_CAP];
        copy_text(truncated, sizeof(truncated), status_text);
        pos = (uint32_t)strlen(truncated);
        append_text(truncated, &pos, sizeof(truncated), T("  Truncated", "  已截断"));
        leonos_ui_statusbar(&ui, view_h - BROWSER_STATUS_H, BROWSER_STATUS_H, truncated);
    } else {
        leonos_ui_statusbar(&ui, view_h - BROWSER_STATUS_H, BROWSER_STATUS_H, status_text);
    }
    draw_browser_menu();
}

static void present_browser(void)
{
    if (window_id <= 0) {
        return;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, BROWSER_MAX_W);
    draw_browser();
    leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                              BROWSER_MAX_W, pixels);
}

static void activate_link_at(int32_t mx, int32_t my)
{
    uint32_t row;
    uint32_t col;
    uint32_t align_shift_cols;
    uint8_t link;
    struct browser_line *line;
    if (!hit_rect_i(mx, my, text_x(), text_y(),
                    document_text_w(),
                    page_h() > 16U ? page_h() - 16U : page_h())) {
        return;
    }
    row = scroll_line + ((uint32_t)my - text_y()) / BROWSER_LINE_H;
    if (row >= line_count) {
        return;
    }
    line = &lines[row];
    col = ((uint32_t)mx - text_x()) / LEONOS_FONT_W;
    if (col < line->indent) {
        return;
    }
    col -= line->indent;
    align_shift_cols = line_align_shift_px(line, document_text_w()) / LEONOS_FONT_W;
    if (col < align_shift_cols) {
        return;
    }
    col -= align_shift_cols;
    if (line->kind == BROWSER_LINE_IMAGE) {
        uint32_t image_cols = 20U / LEONOS_FONT_W;
        if (col < image_cols) {
            return;
        }
        col -= image_cols;
    }
    if (col >= line->len) {
        return;
    }
    link = line->link[col];
    if (!link || (uint32_t)(link - 1U) >= link_count) {
        return;
    }
    navigate_to(links[link - 1U].href, 1);
}

static int handle_toolbar_click(int32_t x, int32_t y)
{
    if (!hit_rect_i(x, y, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H + BROWSER_ADDR_H)) {
        return 0;
    }
    if (hit_rect_i(x, y, BROWSER_BACK_X, button_y(), BROWSER_BACK_W, LEONOS_UI_BUTTON_H)) {
        go_back();
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_forward_x(), button_y(), BROWSER_FORWARD_W, LEONOS_UI_BUTTON_H)) {
        go_forward();
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_refresh_x(), button_y(), BROWSER_REFRESH_W, LEONOS_UI_BUTTON_H)) {
        navigate_to(current_location, 0);
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_home_x(), button_y(), BROWSER_HOME_W, LEONOS_UI_BUTTON_H)) {
        navigate_to("about:leonos", 1);
        return 1;
    }
    if (hit_rect_i(x, y, go_x(), address_y(), BROWSER_GO_W, LEONOS_UI_BUTTON_H)) {
        navigate_to(address_input, 1);
        return 1;
    }
    return 0;
}

static int address_edit_hit(int32_t x, int32_t y)
{
    return hit_rect_i(x, y, 74, address_y(), address_w(), LEONOS_FONT_H + 8U);
}

static void select_address_text(void)
{
    leonos_ui_edit_state_sync(&address_edit);
    address_edit.focused = 1;
    address_edit.selection_anchor = 0;
    address_edit.cursor = address_edit.length;
    address_edit.scroll = 0;
    address_edit.selecting = 0;
    set_status(T("Address selected", "已选中地址"));
}

static int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)BROWSER_MENU_H) {
        if (hit_rect_i(x, y, BROWSER_MENU_FILE_X, 0, BROWSER_MENU_FILE_W, BROWSER_MENU_H)) {
            menu_open = menu_open == BROWSER_MENU_FILE ? BROWSER_MENU_NONE : BROWSER_MENU_FILE;
            address_edit.focused = 0;
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_EDIT_X, 0, BROWSER_MENU_EDIT_W, BROWSER_MENU_H)) {
            menu_open = menu_open == BROWSER_MENU_EDIT ? BROWSER_MENU_NONE : BROWSER_MENU_EDIT;
            address_edit.focused = 0;
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_VIEW_X, 0, BROWSER_MENU_VIEW_W, BROWSER_MENU_H)) {
            menu_open = menu_open == BROWSER_MENU_VIEW ? BROWSER_MENU_NONE : BROWSER_MENU_VIEW;
            address_edit.focused = 0;
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_FAVORITES_X, 0, BROWSER_MENU_FAVORITES_W, BROWSER_MENU_H)) {
            menu_open = menu_open == BROWSER_MENU_FAVORITES ? BROWSER_MENU_NONE : BROWSER_MENU_FAVORITES;
            address_edit.focused = 0;
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_HELP_X, 0, BROWSER_MENU_HELP_W, BROWSER_MENU_H)) {
            menu_open = menu_open == BROWSER_MENU_HELP ? BROWSER_MENU_NONE : BROWSER_MENU_HELP;
            address_edit.focused = 0;
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_FILE) {
        if (hit_rect_i(x, y, BROWSER_MENU_FILE_X + 4U, menu_row_y(0), 180U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            navigate_to("about:leonos", 1);
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_FILE_X + 4U, menu_row_y(1), 180U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            navigate_to(current_location, 0);
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_FILE_X + 4U, menu_row_y(2), 180U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            browser_should_exit = 1;
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_EDIT) {
        if (hit_rect_i(x, y, BROWSER_MENU_EDIT_X + 4U, menu_row_y(0), 184U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            select_address_text();
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_EDIT_X + 4U, menu_row_y(1), 184U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            address_input[0] = 0;
            leonos_ui_edit_state_sync(&address_edit);
            address_edit.focused = 1;
            set_status(T("Address cleared", "地址已清空"));
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_VIEW) {
        if (hit_rect_i(x, y, BROWSER_MENU_VIEW_X + 4U, menu_row_y(0), 158U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            navigate_to(current_location, 0);
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_VIEW_X + 4U, menu_row_y(1), 158U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            scroll_line = 0;
            set_status(T("Top of page", "页面顶部"));
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_VIEW_X + 4U, menu_row_y(2), 158U, BROWSER_MENU_ITEM_H)) {
            uint32_t rows = visible_rows();
            menu_open = BROWSER_MENU_NONE;
            scroll_line = line_count > rows ? line_count - rows : 0;
            set_status(T("Bottom of page", "页面底部"));
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_FAVORITES) {
        if (hit_rect_i(x, y, BROWSER_MENU_FAVORITES_X + 4U, menu_row_y(0), 196U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            navigate_to("about:leonos", 1);
            return 1;
        }
        if (hit_rect_i(x, y, BROWSER_MENU_FAVORITES_X + 4U, menu_row_y(1), 196U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            navigate_to("http://example.com/", 1);
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_HELP) {
        if (hit_rect_i(x, y, BROWSER_MENU_HELP_X + 4U, menu_row_y(0), 168U, BROWSER_MENU_ITEM_H)) {
            menu_open = BROWSER_MENU_NONE;
            leonos_ui_show_message_box(T("LeonOS Browser", "LeonOS 浏览器"),
                                       T("Classic HTTP browser for LeonOS 4.",
                                         "LeonOS 4 经典 HTTP 浏览器。"),
                                       T("OK", "确定"));
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    return 0;
}

static void handle_mouse_button(struct leonos_gui_app_event *event)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t scroll_x = BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U;
    uint32_t buttons = event->buttons;
    if (event->pressed) {
        buttons |= 1U;
    }
    if (!(buttons & 1U)) {
        return;
    }
    if (handle_menu_click(event->x, event->y)) {
        present_browser();
        return;
    }
    if (address_edit_hit(event->x, event->y) &&
        leonos_ui_edit_state_handle_mouse(&address_edit, event->x, event->y,
                                          74, address_y(), address_w(),
                                          buttons)) {
        present_browser();
        return;
    }
    if (handle_toolbar_click(event->x, event->y)) {
        menu_open = BROWSER_MENU_NONE;
        if (!address_edit_hit(event->x, event->y)) {
            address_edit.focused = 0;
        }
        present_browser();
        return;
    }
    if (address_edit.focused) {
        address_edit.focused = 0;
    }
    menu_open = BROWSER_MENU_NONE;
    if (hit_rect_i(event->x, event->y, scroll_x, p_y + 2U,
                   BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h)) {
        if (leonos_ui_vscrollbar_handle_mouse(&scroll_line,
                                              line_count ? line_count : 1U,
                                              visible_rows(),
                                              scroll_x, p_y + 2U,
                                              BROWSER_SCROLL_W,
                                              p_h > 4U ? p_h - 4U : p_h,
                                              event->x, event->y)) {
            present_browser();
        }
        return;
    }
    activate_link_at(event->x, event->y);
    present_browser();
}

static void handle_key(struct leonos_gui_app_event *event)
{
    if (!event->pressed) {
        leonos_ui_edit_state_handle_key(&address_edit, event->keycode, event->pressed);
        return;
    }
    if (event->keycode == 1) {
        return;
    }
    if (event->keycode == LEONOS_KEY_ENTER && address_edit.focused) {
        navigate_to(address_input, 1);
        present_browser();
        return;
    }
    if (leonos_ui_edit_state_handle_key(&address_edit, event->keycode, event->pressed)) {
        present_browser();
        return;
    }
    if (event->keycode == 73U) {
        uint32_t rows = visible_rows();
        scroll_line = scroll_line > rows ? scroll_line - rows : 0;
        present_browser();
    } else if (event->keycode == 81U) {
        scroll_line += visible_rows();
        clamp_scroll();
        present_browser();
    }
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_gui_app_event event;
    const char *initial = "about:leonos";
    (void)envp;
    puts("[browser.elf] browser starting");
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        initial = argv[1];
    }
    window_id = leonos_gui_create_app_window_ex(T("LeonOS Browser", "LeonOS 浏览器"),
                                                T("Classic Web Browser", "经典网页浏览器"),
                                                view_w, view_h, 0);
    if (window_id <= 0) {
        printf("[browser.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, BROWSER_MAX_W);
    leonos_ui_edit_state_init(&address_edit, address_input, sizeof(address_input));
    address_edit.focused = 1;
    load_about();
    if (!text_eq(initial, "about:leonos")) {
        navigate_to(initial, 1);
    } else {
        push_history(current_location);
    }
    present_browser();
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                handle_mouse_button(&event);
                if (browser_should_exit) {
                    return 0;
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_vscrollbar_handle_wheel(&scroll_line,
                                                      line_count ? line_count : 1U,
                                                      visible_rows(), event.dy)) {
                    present_browser();
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    return 0;
                }
                handle_key(&event);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width) {
                    view_w = event.width > BROWSER_MAX_W ? BROWSER_MAX_W : event.width;
                    if (view_w < BROWSER_MIN_W) {
                        view_w = BROWSER_MIN_W;
                    }
                }
                if (event.height) {
                    view_h = event.height > BROWSER_MAX_H ? BROWSER_MAX_H : event.height;
                    if (view_h < BROWSER_MIN_H) {
                        view_h = BROWSER_MIN_H;
                    }
                }
                rerender_page();
                present_browser();
                continue;
            }
        } else {
            sleep_ms(10);
        }
    }
}
