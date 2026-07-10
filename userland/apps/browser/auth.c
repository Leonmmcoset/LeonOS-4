#include "browser.h"

static struct browser_basic_auth browser_auth_entries[BROWSER_MAX_AUTH];
static uint32_t browser_auth_count;

static int auth_header_is_basic(const char *headers)
{
    const char *needle = "www-authenticate:";
    uint32_t pos = 0;
    while (headers && headers[pos]) {
        uint32_t line_start = pos;
        uint32_t name_pos = 0;
        while (headers[pos] && headers[pos] != '\r' && headers[pos] != '\n') {
            ++pos;
        }
        while (name_pos < sizeof("www-authenticate:") - 1U &&
               line_start + name_pos < pos &&
               ascii_tolower(headers[line_start + name_pos]) == needle[name_pos]) {
            ++name_pos;
        }
        if (name_pos == sizeof("www-authenticate:") - 1U) {
            uint32_t value = line_start + name_pos;
            while (value < pos && (headers[value] == ' ' || headers[value] == '\t')) {
                ++value;
            }
            if (value + 5U <= pos &&
                ascii_tolower(headers[value]) == 'b' &&
                ascii_tolower(headers[value + 1U]) == 'a' &&
                ascii_tolower(headers[value + 2U]) == 's' &&
                ascii_tolower(headers[value + 3U]) == 'i' &&
                ascii_tolower(headers[value + 4U]) == 'c') {
                return 1;
            }
        }
        while (headers[pos] == '\r' || headers[pos] == '\n') {
            ++pos;
        }
    }
    return 0;
}

static void auth_base64(char *dst, uint32_t cap, const char *text)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t pos = 0;
    uint32_t len = (uint32_t)strlen(text);
    for (uint32_t i = 0; i < len && pos + 4U < cap; i += 3U) {
        uint32_t a = (uint8_t)text[i];
        uint32_t b = i + 1U < len ? (uint8_t)text[i + 1U] : 0;
        uint32_t c = i + 2U < len ? (uint8_t)text[i + 2U] : 0;
        dst[pos++] = alphabet[a >> 2U];
        dst[pos++] = alphabet[((a & 3U) << 4U) | (b >> 4U)];
        dst[pos++] = i + 1U < len ? alphabet[((b & 15U) << 2U) | (c >> 6U)] : '=';
        dst[pos++] = i + 2U < len ? alphabet[c & 63U] : '=';
    }
    dst[pos] = 0;
}

static int auth_entry_for(const char *host, uint32_t port)
{
    for (uint32_t i = 0; i < browser_auth_count; ++i) {
        if (browser_auth_entries[i].port == port &&
            text_eq_ignore_case(browser_auth_entries[i].host, host)) {
            return (int)i;
        }
    }
    return -1;
}

void browser_auth_append_header(const char *url, char *dst, uint32_t *pos,
                                uint32_t cap)
{
    struct parsed_http_url parsed;
    int index;
    if (!url || !dst || !pos || !parse_http_url(url, &parsed)) {
        return;
    }
    index = auth_entry_for(parsed.host, parsed.port);
    if (index >= 0 && browser_auth_entries[index].header[0]) {
        append_text(dst, pos, cap, browser_auth_entries[index].header);
        append_text(dst, pos, cap, "\r\n");
    }
}

int browser_auth_retry_from_challenge(const char *url, const char *headers)
{
    struct parsed_http_url parsed;
    char username[64];
    char password[96];
    char joined[164];
    char encoded[224];
    uint32_t pos = 0;
    int index;
    if (!url || !parse_http_url(url, &parsed) || !auth_header_is_basic(headers)) {
        return 0;
    }
    username[0] = 0;
    password[0] = 0;
    if (!leonos_ui_show_input_dialog(T("HTTP Authentication", "HTTP 身份验证"),
                                     T("Username for this HTTP host:",
                                       "此 HTTP 主机的用户名:"),
                                     username, sizeof(username)) ||
        !username[0] ||
        !leonos_ui_show_password_dialog(T("HTTP Authentication", "HTTP 身份验证"),
                                        T("Password (kept only for this browser session):",
                                          "密码（只保存在本次浏览器会话中）:"),
                                        password, sizeof(password))) {
        return 0;
    }
    joined[0] = 0;
    append_text(joined, &pos, sizeof(joined), username);
    append_char(joined, &pos, sizeof(joined), ':');
    append_text(joined, &pos, sizeof(joined), password);
    auth_base64(encoded, sizeof(encoded), joined);
    index = auth_entry_for(parsed.host, parsed.port);
    if (index < 0) {
        if (browser_auth_count >= BROWSER_MAX_AUTH) {
            for (uint32_t i = 1; i < BROWSER_MAX_AUTH; ++i) {
                browser_auth_entries[i - 1U] = browser_auth_entries[i];
            }
            browser_auth_count = BROWSER_MAX_AUTH - 1U;
        }
        index = (int)browser_auth_count++;
    }
    browser_auth_entries[index] = (struct browser_basic_auth){0};
    copy_text(browser_auth_entries[index].host,
              sizeof(browser_auth_entries[index].host), parsed.host);
    browser_auth_entries[index].port = parsed.port;
    pos = 0;
    append_text(browser_auth_entries[index].header,
                &pos, sizeof(browser_auth_entries[index].header),
                "Authorization: Basic ");
    append_text(browser_auth_entries[index].header,
                &pos, sizeof(browser_auth_entries[index].header), encoded);
    for (uint32_t i = 0; i < sizeof(password); ++i) {
        password[i] = 0;
    }
    for (uint32_t i = 0; i < sizeof(joined); ++i) {
        joined[i] = 0;
    }
    return 1;
}
