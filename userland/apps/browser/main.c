#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

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
#define BROWSER_PAGE_X 8U
#define BROWSER_STATUS_H 28U
#define BROWSER_LINE_H (LEONOS_FONT_H + 2U)
#define BROWSER_SCROLL_W 18U
#define BROWSER_LINK_BLUE 0x000000ccU
#define BROWSER_TEXT_DARK 0x00202020U
#define BROWSER_IE_NAVY 0x00000080U
#define BROWSER_IE_SKY 0x00d8e8f8U
#define T(en, zh) leonos_i18n((en), (zh))

enum browser_line_kind {
    BROWSER_LINE_NORMAL = 0,
    BROWSER_LINE_HEADING = 1,
    BROWSER_LINE_MUTED = 2,
};

struct browser_line {
    char text[BROWSER_LINE_CHARS];
    uint8_t link[BROWSER_LINE_CHARS];
    uint32_t len;
    uint8_t kind;
};

struct browser_link {
    char href[BROWSER_URL_CAP];
};

struct parsed_http_url {
    char host[LEONOS_NET_HOSTNAME_LEN];
    char path[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t port;
};

struct render_ctx {
    uint32_t cols;
    uint8_t kind;
    uint8_t pending_space;
    uint8_t in_link;
    uint8_t link_id;
    uint8_t in_title;
    uint8_t skip_content;
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

static void clear_line(uint32_t index)
{
    if (index >= BROWSER_MAX_LINES) {
        return;
    }
    lines[index].text[0] = 0;
    lines[index].len = 0;
    lines[index].kind = BROWSER_LINE_NORMAL;
    for (uint32_t i = 0; i < BROWSER_LINE_CHARS; ++i) {
        lines[index].link[i] = 0;
    }
}

static void document_reset(void)
{
    link_count = 0;
    line_count = 1;
    scroll_line = 0;
    for (uint32_t i = 0; i < BROWSER_MAX_LINES; ++i) {
        clear_line(i);
    }
}

static void render_newline(struct render_ctx *ctx, uint8_t force)
{
    if (line_count == 0) {
        line_count = 1;
        clear_line(0);
    }
    if (!force && lines[line_count - 1U].len == 0) {
        return;
    }
    if (line_count >= BROWSER_MAX_LINES) {
        source_truncated = 1;
        return;
    }
    clear_line(line_count);
    lines[line_count].kind = ctx ? ctx->kind : BROWSER_LINE_NORMAL;
    ++line_count;
}

static void render_raw_char(struct render_ctx *ctx, char ch)
{
    struct browser_line *line;
    uint8_t link_id = 0;
    if (!ctx) {
        return;
    }
    if (ch == '\r') {
        return;
    }
    if (ch == '\n') {
        render_newline(ctx, 1);
        return;
    }
    if (ch == '\t') {
        render_raw_char(ctx, ' ');
        render_raw_char(ctx, ' ');
        return;
    }
    if (line_count == 0) {
        line_count = 1;
        clear_line(0);
    }
    line = &lines[line_count - 1U];
    if (line->len >= ctx->cols || line->len + 1U >= BROWSER_LINE_CHARS) {
        render_newline(ctx, 1);
        line = &lines[line_count - 1U];
    }
    if (line->len >= ctx->cols) {
        source_truncated = 1;
        return;
    }
    if (line_count > BROWSER_MAX_LINES || line->len + 1U >= BROWSER_LINE_CHARS) {
        source_truncated = 1;
        return;
    }
    if (ctx->in_link) {
        link_id = (uint8_t)(ctx->link_id + 1U);
    }
    line->kind = ctx->kind;
    line->text[line->len] = ch >= 32 ? ch : ' ';
    line->link[line->len] = link_id;
    ++line->len;
    line->text[line->len] = 0;
}

static void render_html_char(struct render_ctx *ctx, char ch)
{
    if (!ctx) {
        return;
    }
    if (is_space_char(ch)) {
        ctx->pending_space = 1;
        return;
    }
    if (ctx->pending_space && line_count && lines[line_count - 1U].len > 0) {
        render_raw_char(ctx, ' ');
    }
    ctx->pending_space = 0;
    render_raw_char(ctx, ch);
}

static int tag_name_eq(const char *tag, const char *name)
{
    uint32_t i = 0;
    while (tag[i] && !is_space_char(tag[i]) && tag[i] != '/' && tag[i] != '>') {
        if (ascii_tolower(tag[i]) != name[i]) {
            return 0;
        }
        ++i;
    }
    return name[i] == 0;
}

static void extract_attr(const char *tag, const char *attr, char *dst, uint32_t cap)
{
    uint32_t i = 0;
    uint32_t attr_len = (uint32_t)strlen(attr);
    dst[0] = 0;
    while (tag && tag[i]) {
        while (tag[i] && is_space_char(tag[i])) {
            ++i;
        }
        if (!tag[i]) {
            return;
        }
        if (starts_with_ignore_case(tag + i, attr)) {
            uint32_t j = i + attr_len;
            while (tag[j] && is_space_char(tag[j])) {
                ++j;
            }
            if (tag[j] == '=') {
                char quote = 0;
                uint32_t pos = 0;
                ++j;
                while (tag[j] && is_space_char(tag[j])) {
                    ++j;
                }
                if (tag[j] == '\'' || tag[j] == '"') {
                    quote = tag[j++];
                }
                while (tag[j] && pos + 1U < cap) {
                    if (quote) {
                        if (tag[j] == quote) {
                            break;
                        }
                    } else if (is_space_char(tag[j]) || tag[j] == '>') {
                        break;
                    }
                    dst[pos++] = tag[j++];
                }
                dst[pos] = 0;
                return;
            }
        }
        while (tag[i] && !is_space_char(tag[i])) {
            ++i;
        }
    }
}

static uint8_t add_link(const char *href)
{
    if (!href || !href[0] || link_count >= BROWSER_MAX_LINKS) {
        return 0xffU;
    }
    copy_text(links[link_count].href, sizeof(links[link_count].href), href);
    return (uint8_t)link_count++;
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

static void parent_url_dir(const char *path, char *dst, uint32_t cap)
{
    uint32_t last_slash = 0;
    uint32_t i = 0;
    uint32_t pos = 0;
    if (!path || !path[0]) {
        copy_text(dst, cap, "/");
        return;
    }
    while (path[i]) {
        if (path[i] == '/') {
            last_slash = i;
        }
        ++i;
    }
    if (last_slash == 0) {
        copy_text(dst, cap, "/");
        return;
    }
    while (pos <= last_slash && pos + 1U < cap) {
        dst[pos] = path[pos];
        ++pos;
    }
    dst[pos] = 0;
}

static void resolve_href(const char *base, const char *href, char *out, uint32_t cap)
{
    struct parsed_http_url parsed;
    char dir[BROWSER_URL_CAP];
    uint32_t pos = 0;
    if (!href || !href[0]) {
        copy_text(out, cap, base);
        return;
    }
    if (starts_with_ignore_case(href, "http://") ||
        starts_with_ignore_case(href, "https://") ||
        starts_with_ignore_case(href, "about:") ||
        is_drive_path(href)) {
        copy_text(out, cap, href);
        return;
    }
    if (href[0] == '#') {
        copy_text(out, cap, base);
        return;
    }
    if (parse_http_url(base, &parsed)) {
        if (href[0] == '/') {
            build_http_url(out, cap, parsed.host, parsed.port, href);
            return;
        }
        parent_url_dir(parsed.path, dir, sizeof(dir));
        out[0] = 0;
        append_text(out, &pos, cap, "http://");
        append_text(out, &pos, cap, parsed.host);
        if (parsed.port != 80U) {
            append_char(out, &pos, cap, ':');
            append_u32(out, &pos, cap, parsed.port);
        }
        append_text(out, &pos, cap, dir);
        append_text(out, &pos, cap, href);
        return;
    }
    if (is_drive_path(base)) {
        parent_url_dir(base, dir, sizeof(dir));
        out[0] = 0;
        append_text(out, &pos, cap, dir);
        append_text(out, &pos, cap, href);
        return;
    }
    copy_text(out, cap, href);
}

static char entity_to_char(const char *entity)
{
    if (text_eq(entity, "amp")) {
        return '&';
    }
    if (text_eq(entity, "lt")) {
        return '<';
    }
    if (text_eq(entity, "gt")) {
        return '>';
    }
    if (text_eq(entity, "quot")) {
        return '"';
    }
    if (text_eq(entity, "apos")) {
        return '\'';
    }
    if (text_eq(entity, "nbsp")) {
        return ' ';
    }
    if (entity[0] == '#') {
        uint32_t value = 0;
        uint32_t i = 1;
        while (entity[i] && is_digit(entity[i])) {
            value = value * 10U + (uint32_t)(entity[i] - '0');
            ++i;
        }
        if (entity[i] == 0 && value >= 32U && value <= 126U) {
            return (char)value;
        }
    }
    return 0;
}

static void parse_tag(struct render_ctx *ctx, const char *tag,
                      const char *base_url)
{
    char href[BROWSER_URL_CAP];
    char resolved[BROWSER_URL_CAP];
    uint32_t i = 0;
    uint8_t closing = 0;
    if (!ctx || !tag) {
        return;
    }
    while (is_space_char(tag[i])) {
        ++i;
    }
    if (tag[i] == '!') {
        return;
    }
    if (tag[i] == '/') {
        closing = 1;
        ++i;
        while (is_space_char(tag[i])) {
            ++i;
        }
    }
    if (ctx->skip_content) {
        if (closing && (tag_name_eq(tag + i, "script") ||
                        tag_name_eq(tag + i, "style"))) {
            ctx->skip_content = 0;
        }
        return;
    }
    if (closing) {
        if (tag_name_eq(tag + i, "a")) {
            ctx->in_link = 0;
            ctx->link_id = 0xffU;
            return;
        }
        if (tag_name_eq(tag + i, "title")) {
            ctx->in_title = 0;
            return;
        }
        if (tag_name_eq(tag + i, "h1") || tag_name_eq(tag + i, "h2") ||
            tag_name_eq(tag + i, "h3")) {
            ctx->kind = BROWSER_LINE_NORMAL;
            render_newline(ctx, 0);
            return;
        }
        if (tag_name_eq(tag + i, "p") || tag_name_eq(tag + i, "div") ||
            tag_name_eq(tag + i, "li") || tag_name_eq(tag + i, "tr") ||
            tag_name_eq(tag + i, "section") || tag_name_eq(tag + i, "article")) {
            render_newline(ctx, 0);
        }
        return;
    }
    if (tag_name_eq(tag + i, "script") || tag_name_eq(tag + i, "style")) {
        ctx->skip_content = 1;
        return;
    }
    if (tag_name_eq(tag + i, "title")) {
        ctx->in_title = 1;
        page_title[0] = 0;
        return;
    }
    if (tag_name_eq(tag + i, "br")) {
        render_newline(ctx, 1);
        return;
    }
    if (tag_name_eq(tag + i, "p") || tag_name_eq(tag + i, "div") ||
        tag_name_eq(tag + i, "section") || tag_name_eq(tag + i, "article") ||
        tag_name_eq(tag + i, "table") || tag_name_eq(tag + i, "tr")) {
        render_newline(ctx, 0);
        return;
    }
    if (tag_name_eq(tag + i, "h1") || tag_name_eq(tag + i, "h2") ||
        tag_name_eq(tag + i, "h3")) {
        render_newline(ctx, 0);
        ctx->kind = BROWSER_LINE_HEADING;
        return;
    }
    if (tag_name_eq(tag + i, "li")) {
        render_newline(ctx, 0);
        render_html_char(ctx, '*');
        render_html_char(ctx, ' ');
        return;
    }
    if (tag_name_eq(tag + i, "a")) {
        extract_attr(tag + i, "href", href, sizeof(href));
        if (href[0]) {
            resolve_href(base_url, href, resolved, sizeof(resolved));
            ctx->link_id = add_link(resolved);
            ctx->in_link = ctx->link_id != 0xffU;
        }
    }
}

static void render_html_source(const char *source, const char *base_url)
{
    struct render_ctx ctx;
    uint32_t i = 0;
    char tag[192];
    document_reset();
    ctx.cols = text_cols();
    ctx.kind = BROWSER_LINE_NORMAL;
    ctx.pending_space = 0;
    ctx.in_link = 0;
    ctx.link_id = 0xffU;
    ctx.in_title = 0;
    ctx.skip_content = 0;
    if (!page_title[0]) {
        copy_text(page_title, sizeof(page_title), T("Untitled", "无标题"));
    }
    while (source && source[i]) {
        if (source[i] == '<') {
            uint32_t tag_pos = 0;
            ++i;
            while (source[i] && source[i] != '>' && tag_pos + 1U < sizeof(tag)) {
                tag[tag_pos++] = source[i++];
            }
            tag[tag_pos] = 0;
            if (source[i] == '>') {
                ++i;
            }
            parse_tag(&ctx, tag, base_url);
            continue;
        }
        if (source[i] == '&') {
            char entity[16];
            uint32_t entity_pos = 0;
            uint32_t j = i + 1U;
            char decoded;
            while (source[j] && source[j] != ';' && entity_pos + 1U < sizeof(entity)) {
                entity[entity_pos++] = source[j++];
            }
            entity[entity_pos] = 0;
            decoded = source[j] == ';' ? entity_to_char(entity) : 0;
            if (decoded) {
                if (ctx.in_title) {
                    uint32_t pos = (uint32_t)strlen(page_title);
                    append_char(page_title, &pos, sizeof(page_title), decoded);
                } else if (!ctx.skip_content) {
                    render_html_char(&ctx, decoded);
                }
                i = source[j] == ';' ? j + 1U : i + 1U;
                continue;
            }
        }
        if (ctx.in_title) {
            if (!is_space_char(source[i])) {
                uint32_t pos = (uint32_t)strlen(page_title);
                append_char(page_title, &pos, sizeof(page_title), source[i]);
            } else if (page_title[0] && page_title[strlen(page_title) - 1U] != ' ') {
                uint32_t pos = (uint32_t)strlen(page_title);
                append_char(page_title, &pos, sizeof(page_title), ' ');
            }
        } else if (!ctx.skip_content) {
            render_html_char(&ctx, source[i]);
        }
        ++i;
    }
    clamp_scroll();
}

static void render_plain_source(const char *source)
{
    struct render_ctx ctx;
    uint32_t i = 0;
    document_reset();
    ctx.cols = text_cols();
    ctx.kind = BROWSER_LINE_NORMAL;
    ctx.pending_space = 0;
    ctx.in_link = 0;
    ctx.link_id = 0xffU;
    ctx.in_title = 0;
    ctx.skip_content = 0;
    while (source && source[i]) {
        render_raw_char(&ctx, source[i++]);
    }
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
        "<p>This build supports HTTP, local .html files, text flow, links, "
        "history, refresh, and scrolling.</p>"
        "<p>HTTPS, JavaScript, images, CSS layout, and full litehtml are not "
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
    uint32_t go_w = 54U;
    if (view_w <= x + go_w + 20U) {
        return 120U;
    }
    return view_w - x - go_w - 20U;
}

static uint32_t go_x(void)
{
    return 74U + address_w() + 8U;
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
                          uint8_t underline, uint8_t bold)
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
    leonos_ui_text(&ui, x, y, tmp, fg, bg);
    if (bold) {
        leonos_ui_text(&ui, x + 1U, y, tmp, fg, bg);
    }
    if (underline && copy_len) {
        leonos_ui_rect(&ui, x, y + LEONOS_FONT_H - 1U,
                       copy_len * LEONOS_FONT_W, 1U, fg);
    }
}

static void draw_document_lines(void)
{
    uint32_t px = text_x();
    uint32_t py = text_y();
    uint32_t rows = visible_rows();
    uint32_t max_line = scroll_line + rows;
    uint32_t text_bg = LEONOS_UI_WHITE;
    if (max_line > line_count) {
        max_line = line_count;
    }
    for (uint32_t row = 0; scroll_line + row < max_line; ++row) {
        struct browser_line *line = &lines[scroll_line + row];
        uint32_t y = py + row * BROWSER_LINE_H;
        uint32_t start = 0;
        while (start < line->len) {
            uint8_t link = line->link[start];
            uint32_t end = start + 1U;
            uint32_t fg = line->kind == BROWSER_LINE_HEADING ? BROWSER_IE_NAVY : BROWSER_TEXT_DARK;
            uint8_t underline = 0;
            uint8_t bold = line->kind == BROWSER_LINE_HEADING;
            while (end < line->len && line->link[end] == link) {
                ++end;
            }
            if (link) {
                fg = BROWSER_LINK_BLUE;
                underline = 1;
                bold = 0;
            } else if (line->kind == BROWSER_LINE_MUTED) {
                fg = LEONOS_UI_DARK;
            }
            draw_line_run(px + start * LEONOS_FONT_W, y,
                          line->text + start, end - start, fg, text_bg,
                          underline, bold);
            start = end;
        }
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
    leonos_ui_menubar_item(&ui, 8, 0, 42, T("File", "文件"), 0);
    leonos_ui_menubar_item(&ui, 54, 0, 42, T("Edit", "编辑"), 0);
    leonos_ui_menubar_item(&ui, 100, 0, 44, T("View", "查看"), 0);
    leonos_ui_menubar_item(&ui, 150, 0, 74, T("Favorites", "收藏夹"), 0);
    leonos_ui_menubar_item(&ui, 228, 0, 42, T("Help", "帮助"), 0);
    leonos_ui_toolbar(&ui, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H);
    draw_toolbar_button(8, 58, T("Back", "后退"), !can_back);
    draw_toolbar_button(70, 70, T("Forward", "前进"), !can_forward);
    draw_toolbar_button(144, 64, T("Refresh", "刷新"), 0);
    draw_toolbar_button(212, 54, T("Home", "主页"), 0);
    draw_toolbar_button(270, 50, T("Stop", "停止"), 1);
    title_line[0] = 0;
    append_text(title_line, &pos, sizeof(title_line), "LeonOS Browser - ");
    append_text(title_line, &pos, sizeof(title_line), page_title);
    leonos_ui_text_clipped(&ui, 332, button_y() + 5U,
                           view_w > 344U ? view_w - 344U : 80U,
                           title_line, BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
    leonos_ui_rect(&ui, 0, BROWSER_MENU_H + BROWSER_TOOLBAR_H, view_w,
                   BROWSER_ADDR_H, BROWSER_IE_SKY);
    leonos_ui_text(&ui, 12, address_y() + 5U, T("Address", "地址"),
                   BROWSER_TEXT_DARK, BROWSER_IE_SKY);
    leonos_ui_edit_state_draw(&ui, 74, address_y(), address_w(), &address_edit, 0);
    leonos_ui_button(&ui, go_x(), address_y(), 54, LEONOS_UI_BUTTON_H,
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
    uint8_t link;
    if (!hit_rect_i(mx, my, text_x(), text_y(),
                    page_w() > BROWSER_SCROLL_W + 24U ? page_w() - BROWSER_SCROLL_W - 24U : 80U,
                    page_h() > 16U ? page_h() - 16U : page_h())) {
        return;
    }
    row = scroll_line + ((uint32_t)my - text_y()) / BROWSER_LINE_H;
    col = ((uint32_t)mx - text_x()) / LEONOS_FONT_W;
    if (row >= line_count || col >= lines[row].len) {
        return;
    }
    link = lines[row].link[col];
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
    if (hit_rect_i(x, y, 8, button_y(), 58, LEONOS_UI_BUTTON_H)) {
        go_back();
        return 1;
    }
    if (hit_rect_i(x, y, 70, button_y(), 70, LEONOS_UI_BUTTON_H)) {
        go_forward();
        return 1;
    }
    if (hit_rect_i(x, y, 144, button_y(), 64, LEONOS_UI_BUTTON_H)) {
        navigate_to(current_location, 0);
        return 1;
    }
    if (hit_rect_i(x, y, 212, button_y(), 54, LEONOS_UI_BUTTON_H)) {
        navigate_to("about:leonos", 1);
        return 1;
    }
    if (hit_rect_i(x, y, go_x(), address_y(), 54, LEONOS_UI_BUTTON_H)) {
        navigate_to(address_input, 1);
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
    if (!(event->buttons & 1U)) {
        return;
    }
    if (leonos_ui_edit_state_handle_mouse(&address_edit, event->x, event->y,
                                          74, address_y(), address_w(),
                                          event->buttons)) {
        present_browser();
        return;
    }
    if (handle_toolbar_click(event->x, event->y)) {
        present_browser();
        return;
    }
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
