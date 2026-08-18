#include "browser.h"

void copy_text(char *dst, uint32_t cap, const char *src)
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

char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

int text_eq(const char *a, const char *b)
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

int text_eq_ignore_case(const char *a, const char *b)
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

int starts_with_ignore_case(const char *text, const char *prefix)
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

int ends_with_ignore_case(const char *text, const char *suffix)
{
    uint32_t text_len;
    uint32_t suffix_len;
    if (!text || !suffix) {
        return 0;
    }
    text_len = (uint32_t)strlen(text);
    suffix_len = (uint32_t)strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }
    return text_eq_ignore_case(text + text_len - suffix_len, suffix);
}

int is_space_char(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
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

void append_i32(char *dst, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        append_char(dst, pos, cap, '-');
        value = -value;
    }
    append_u32(dst, pos, cap, (uint32_t)value);
}

void trim_copy(char *dst, uint32_t cap, const char *src)
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


const char *net_status_name(uint32_t status)
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
    case LEONOS_NET_STATUS_SOCKET_LIMIT:
        return T("Socket limit reached", "Socket 数量已满");
    case LEONOS_NET_STATUS_SOCKET_BAD_HANDLE:
        return T("Bad socket", "Socket 无效");
    case LEONOS_NET_STATUS_SOCKET_NOT_CONNECTED:
        return T("Socket not connected", "Socket 未连接");
    case LEONOS_NET_STATUS_SOCKET_CLOSED:
        return T("Socket closed", "Socket 已关闭");
    case LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED:
        return T("Protocol unsupported", "协议不支持");
    case LEONOS_NET_STATUS_TLS_FAILED:
        return T("TLS verification failed", "TLS 验证失败");
    default:
        return T("Unknown network status", "未知网络状态");
    }
}

int parse_http_url(const char *url, struct parsed_http_url *out)
{
    const char *p;
    uint32_t host_pos = 0;
    uint32_t path_pos = 0;
    uint32_t port;
    if (!url || !out) {
        return 0;
    }
    if (starts_with_ignore_case(url, "https://")) {
        out->secure = 1;
        port = 443;
        p = url + 8;
    } else if (starts_with_ignore_case(url, "http://")) {
        out->secure = 0;
        port = 80;
        p = url + 7;
    } else {
        return 0;
    }
    while (*p && *p != '/' && *p != ':' && *p != '#' && *p != '?' &&
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
    } else if (*p == '?') {
        out->path[path_pos++] = '/';
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

void build_http_url(char *dst, uint32_t cap, const char *host,
                    uint32_t port, uint8_t secure, const char *path)
{
    uint32_t pos = 0;
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
}

int is_drive_path(const char *text)
{
    return text && text[0] && text[1] == ':' && text[2] == '/';
}

void normalize_location(const char *input, char *out, uint32_t cap)
{
    char tmp[BROWSER_URL_CAP];
    uint32_t pos = 0;
    trim_copy(tmp, sizeof(tmp), input);
    if (!tmp[0]) {
        copy_text(out, cap, "about:leonos");
        return;
    }
    if (tmp[0] == '/' &&
        (starts_with_ignore_case(current_location, "http://") ||
         starts_with_ignore_case(current_location, "https://"))) {
        char resolved[BROWSER_URL_CAP];
        if (leonos_http_resolve_url(current_location, tmp, resolved,
                                    sizeof(resolved)) == 0) {
            copy_text(out, cap, resolved);
            return;
        }
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
