#include "browser.h"

#define BROWSER_COOKIE_FLAG_HOST_ONLY 0x00000001U
#define BROWSER_COOKIE_FLAG_SECURE 0x00000002U
#define BROWSER_COOKIE_FLAG_HTTP_ONLY 0x00000004U
#define BROWSER_COOKIE_STORE_DIR "browser"
#define BROWSER_COOKIE_STORE_FILE "cookies.txt"

static struct browser_cookie browser_cookies[BROWSER_MAX_COOKIES];
static uint32_t browser_cookie_count;
static uint8_t browser_cookies_loaded;
static char browser_cookie_store_path[LEONOS_FS_PATH_LEN];
static char browser_cookie_file_buffer[BROWSER_COOKIE_FILE_CAP];

static uint32_t cookie_text_len(const char *text)
{
    return text ? (uint32_t)strlen(text) : 0U;
}

static int cookie_char_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int cookie_text_eq_ignore_case_len(const char *a, uint32_t a_len,
                                          const char *b)
{
    uint32_t b_len = cookie_text_len(b);
    if (!a || !b || a_len != b_len) {
        return 0;
    }
    for (uint32_t i = 0; i < a_len; ++i) {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) {
            return 0;
        }
    }
    return 1;
}

static void cookie_trim_bounds(const char *text, uint32_t len,
                               uint32_t *start, uint32_t *end)
{
    uint32_t s = 0;
    uint32_t e = len;
    while (s < e && cookie_char_is_space(text[s])) {
        ++s;
    }
    while (e > s && cookie_char_is_space(text[e - 1U])) {
        --e;
    }
    if (start) {
        *start = s;
    }
    if (end) {
        *end = e;
    }
}

static void cookie_copy_clean(char *dst, uint32_t cap,
                              const char *src, uint32_t len,
                              uint8_t lowercase)
{
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    cookie_trim_bounds(src, len, &start, &end);
    while (start < end && pos + 1U < cap) {
        char ch = src[start++];
        if (ch == '\t' || ch == '\r' || ch == '\n' || ch == ';') {
            continue;
        }
        dst[pos++] = lowercase ? ascii_tolower(ch) : ch;
    }
    dst[pos] = 0;
}

static void cookie_copy_trimmed(char *dst, uint32_t cap,
                                const char *src, uint32_t len)
{
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    cookie_trim_bounds(src, len, &start, &end);
    while (start < end && pos + 1U < cap) {
        char ch = src[start++];
        if (ch == '\r' || ch == '\n') {
            continue;
        }
        dst[pos++] = ch;
    }
    dst[pos] = 0;
}

static void cookie_append_path(char *dst, uint32_t cap,
                               const char *dir, const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, dir);
    if (dir && dir[0] && dir[cookie_text_len(dir) - 1U] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, name);
}

static void cookie_build_store_path(char *dst, uint32_t cap)
{
    struct leonos_user_info user;
    char dir[LEONOS_FS_PATH_LEN];
    if (leonos_auth_current(&user) == 0 && user.uid && user.home[0]) {
        cookie_append_path(dir, sizeof(dir), user.home,
                           BROWSER_COOKIE_STORE_DIR);
        (void)mkdir(dir, 0);
    } else {
        (void)mkdir("0:/var", 0);
        cookie_append_path(dir, sizeof(dir), "0:/var",
                           BROWSER_COOKIE_STORE_DIR);
        (void)mkdir(dir, 0);
    }
    cookie_append_path(dst, cap, dir, BROWSER_COOKIE_STORE_FILE);
}

static uint64_t cookie_now_unix(void)
{
    struct leonos_time_info info;
    if (leonos_time_info(&info) == 0 && info.valid) {
        return info.unix_seconds;
    }
    return 0;
}

static uint64_t cookie_parse_u64(const char *text)
{
    uint64_t value = 0;
    uint32_t i = 0;
    while (text && text[i] >= '0' && text[i] <= '9') {
        value = value * 10ULL + (uint64_t)(text[i] - '0');
        ++i;
    }
    return value;
}

static int64_t cookie_parse_i64_len(const char *text, uint32_t len,
                                    int *ok)
{
    uint32_t i = 0;
    int negative = 0;
    int seen = 0;
    int64_t value = 0;
    if (ok) {
        *ok = 0;
    }
    while (i < len && cookie_char_is_space(text[i])) {
        ++i;
    }
    if (i < len && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    while (i < len && text[i] >= '0' && text[i] <= '9') {
        seen = 1;
        value = value * 10 + (int64_t)(text[i] - '0');
        ++i;
    }
    if (!seen) {
        return 0;
    }
    if (ok) {
        *ok = 1;
    }
    return negative ? -value : value;
}

static void cookie_append_u64(char *dst, uint32_t *pos, uint32_t cap,
                              uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static int cookie_is_expired(const struct browser_cookie *cookie,
                             uint64_t now)
{
    return cookie && now && cookie->expires_unix &&
           cookie->expires_unix <= now;
}

static void cookie_remove_index(uint32_t index)
{
    if (index >= browser_cookie_count) {
        return;
    }
    for (uint32_t i = index + 1U; i < browser_cookie_count; ++i) {
        browser_cookies[i - 1U] = browser_cookies[i];
    }
    --browser_cookie_count;
}

static void cookie_cleanup_expired(void)
{
    uint64_t now = cookie_now_unix();
    if (!now) {
        return;
    }
    for (uint32_t i = 0; i < browser_cookie_count;) {
        if (cookie_is_expired(&browser_cookies[i], now)) {
            cookie_remove_index(i);
        } else {
            ++i;
        }
    }
}

static void cookie_save(void)
{
    int fd;
    uint32_t pos = 0;
    if (!browser_cookie_store_path[0]) {
        return;
    }
    browser_cookie_file_buffer[0] = 0;
    append_text(browser_cookie_file_buffer, &pos,
                sizeof(browser_cookie_file_buffer),
                "# LeonOS Browser cookies v1\n");
    for (uint32_t i = 0; i < browser_cookie_count; ++i) {
        const struct browser_cookie *cookie = &browser_cookies[i];
        append_text(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), cookie->domain);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\t');
        append_text(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), cookie->path);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\t');
        append_text(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), cookie->name);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\t');
        append_text(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), cookie->value);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\t');
        append_u32(browser_cookie_file_buffer, &pos,
                   sizeof(browser_cookie_file_buffer), cookie->flags);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\t');
        cookie_append_u64(browser_cookie_file_buffer, &pos,
                          sizeof(browser_cookie_file_buffer),
                          cookie->expires_unix);
        append_char(browser_cookie_file_buffer, &pos,
                    sizeof(browser_cookie_file_buffer), '\n');
    }
    fd = open(browser_cookie_store_path,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd >= 0) {
        (void)write(fd, browser_cookie_file_buffer,
                    cookie_text_len(browser_cookie_file_buffer));
        close(fd);
    }
}

static int cookie_parse_saved_line(char *line)
{
    char *fields[6];
    uint32_t field = 0;
    struct browser_cookie cookie;
    if (!line || !line[0] || line[0] == '#') {
        return 0;
    }
    fields[field++] = line;
    for (uint32_t i = 0; line[i] && field < 6U; ++i) {
        if (line[i] == '\t') {
            line[i] = 0;
            fields[field++] = line + i + 1U;
        }
    }
    if (field != 6U) {
        return 0;
    }
    cookie = (struct browser_cookie){0};
    copy_text(cookie.domain, sizeof(cookie.domain), fields[0]);
    copy_text(cookie.path, sizeof(cookie.path), fields[1]);
    copy_text(cookie.name, sizeof(cookie.name), fields[2]);
    copy_text(cookie.value, sizeof(cookie.value), fields[3]);
    cookie.flags = (uint32_t)cookie_parse_u64(fields[4]);
    cookie.expires_unix = cookie_parse_u64(fields[5]);
    if (!cookie.domain[0] || !cookie.path[0] || !cookie.name[0]) {
        return 0;
    }
    if (browser_cookie_count < BROWSER_MAX_COOKIES) {
        browser_cookies[browser_cookie_count++] = cookie;
    }
    return 1;
}

static void cookie_load_file(void)
{
    int fd;
    long got;
    browser_cookie_count = 0;
    if (!browser_cookie_store_path[0]) {
        return;
    }
    fd = open(browser_cookie_store_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, browser_cookie_file_buffer,
               sizeof(browser_cookie_file_buffer) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    browser_cookie_file_buffer[(uint32_t)got] = 0;
    for (uint32_t start = 0; start < (uint32_t)got;) {
        uint32_t end = start;
        while (browser_cookie_file_buffer[end] &&
               browser_cookie_file_buffer[end] != '\n' &&
               browser_cookie_file_buffer[end] != '\r') {
            ++end;
        }
        if (browser_cookie_file_buffer[end]) {
            browser_cookie_file_buffer[end++] = 0;
            while (browser_cookie_file_buffer[end] == '\n' ||
                   browser_cookie_file_buffer[end] == '\r') {
                browser_cookie_file_buffer[end++] = 0;
            }
        }
        (void)cookie_parse_saved_line(browser_cookie_file_buffer + start);
        start = end;
    }
    cookie_cleanup_expired();
}

static void cookie_ensure_loaded(void)
{
    char path[LEONOS_FS_PATH_LEN];
    cookie_build_store_path(path, sizeof(path));
    if (!browser_cookies_loaded ||
        !text_eq(path, browser_cookie_store_path)) {
        copy_text(browser_cookie_store_path,
                  sizeof(browser_cookie_store_path), path);
        browser_cookies_loaded = 1;
        cookie_load_file();
    }
}

static int cookie_domain_match(const char *host,
                               const struct browser_cookie *cookie)
{
    uint32_t host_len;
    uint32_t domain_len;
    if (!host || !cookie || !cookie->domain[0]) {
        return 0;
    }
    if (text_eq_ignore_case(host, cookie->domain)) {
        return 1;
    }
    if (cookie->flags & BROWSER_COOKIE_FLAG_HOST_ONLY) {
        return 0;
    }
    host_len = cookie_text_len(host);
    domain_len = cookie_text_len(cookie->domain);
    if (host_len <= domain_len + 1U) {
        return 0;
    }
    if (host[host_len - domain_len - 1U] != '.') {
        return 0;
    }
    return text_eq_ignore_case(host + host_len - domain_len,
                               cookie->domain);
}

static int cookie_path_match(const char *request_path,
                             const char *cookie_path)
{
    uint32_t req_len = cookie_text_len(request_path);
    uint32_t cookie_len = cookie_text_len(cookie_path);
    if (!request_path || !cookie_path || !cookie_path[0]) {
        return 0;
    }
    if (cookie_len == 1U && cookie_path[0] == '/') {
        return 1;
    }
    if (cookie_len > req_len) {
        return 0;
    }
    for (uint32_t i = 0; i < cookie_len; ++i) {
        if (request_path[i] != cookie_path[i]) {
            return 0;
        }
    }
    return req_len == cookie_len || cookie_path[cookie_len - 1U] == '/' ||
           request_path[cookie_len] == '/';
}

static void cookie_default_path(const char *request_path,
                                char *dst, uint32_t cap)
{
    uint32_t len = cookie_text_len(request_path);
    uint32_t slash = 0;
    uint32_t pos = 0;
    if (!request_path || request_path[0] != '/') {
        copy_text(dst, cap, "/");
        return;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (request_path[i] == '/') {
            slash = i;
        }
    }
    if (slash == 0) {
        copy_text(dst, cap, "/");
        return;
    }
    while (pos < slash && pos + 1U < cap) {
        dst[pos] = request_path[pos];
        ++pos;
    }
    dst[pos] = 0;
}

static int cookie_find(const char *domain, const char *path,
                       const char *name)
{
    for (uint32_t i = 0; i < browser_cookie_count; ++i) {
        if (text_eq_ignore_case(browser_cookies[i].domain, domain) &&
            text_eq(browser_cookies[i].path, path) &&
            text_eq(browser_cookies[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static void cookie_store_or_delete(struct browser_cookie *cookie,
                                   int delete_cookie)
{
    int index = cookie_find(cookie->domain, cookie->path, cookie->name);
    if (delete_cookie) {
        if (index >= 0) {
            cookie_remove_index((uint32_t)index);
            cookie_save();
        }
        return;
    }
    if (index >= 0) {
        browser_cookies[(uint32_t)index] = *cookie;
    } else if (browser_cookie_count < BROWSER_MAX_COOKIES) {
        browser_cookies[browser_cookie_count++] = *cookie;
    } else {
        browser_cookies[BROWSER_MAX_COOKIES - 1U] = *cookie;
    }
    cookie_cleanup_expired();
    cookie_save();
}

static void cookie_parse_set_cookie_value(const char *url,
                                          const char *value,
                                          uint32_t value_len)
{
    struct parsed_http_url parsed;
    struct browser_cookie cookie;
    uint32_t pair_start = 0;
    uint32_t pair_end = 0;
    uint32_t eq = 0xffffffffU;
    uint8_t have_domain = 0;
    int delete_cookie = 0;
    if (!parse_http_url(url, &parsed)) {
        return;
    }
    cookie = (struct browser_cookie){0};
    cookie.flags = BROWSER_COOKIE_FLAG_HOST_ONLY;
    cookie_copy_clean(cookie.domain, sizeof(cookie.domain), parsed.host,
                      cookie_text_len(parsed.host), 1);
    cookie_default_path(parsed.path, cookie.path, sizeof(cookie.path));
    while (pair_end < value_len && value[pair_end] != ';') {
        ++pair_end;
    }
    cookie_trim_bounds(value, pair_end, &pair_start, &pair_end);
    for (uint32_t i = pair_start; i < pair_end; ++i) {
        if (value[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq == 0xffffffffU || eq == pair_start) {
        return;
    }
    cookie_copy_clean(cookie.name, sizeof(cookie.name),
                      value + pair_start, eq - pair_start, 0);
    cookie_copy_clean(cookie.value, sizeof(cookie.value),
                      value + eq + 1U, pair_end - eq - 1U, 0);
    if (!cookie.name[0]) {
        return;
    }
    for (uint32_t pos = pair_end + 1U; pos < value_len;) {
        uint32_t attr_start = pos;
        uint32_t attr_end;
        uint32_t attr_eq = 0xffffffffU;
        uint32_t name_start;
        uint32_t name_end;
        uint32_t val_start;
        uint32_t val_end;
        while (attr_start < value_len && value[attr_start] == ';') {
            ++attr_start;
        }
        attr_end = attr_start;
        while (attr_end < value_len && value[attr_end] != ';') {
            ++attr_end;
        }
        cookie_trim_bounds(value + attr_start, attr_end - attr_start,
                           &name_start, &name_end);
        name_start += attr_start;
        name_end += attr_start;
        for (uint32_t i = name_start; i < name_end; ++i) {
            if (value[i] == '=') {
                attr_eq = i;
                break;
            }
        }
        if (attr_eq != 0xffffffffU) {
            val_start = attr_eq + 1U;
            val_end = name_end;
            name_end = attr_eq;
            while (name_end > name_start &&
                   cookie_char_is_space(value[name_end - 1U])) {
                --name_end;
            }
            cookie_trim_bounds(value + val_start, val_end - val_start,
                               &val_start, &val_end);
            val_start += attr_eq + 1U;
            val_end += attr_eq + 1U;
        } else {
            val_start = name_end;
            val_end = name_end;
        }
        if (cookie_text_eq_ignore_case_len(value + name_start,
                                           name_end - name_start,
                                           "Domain") &&
            attr_eq != 0xffffffffU) {
            uint32_t start = val_start;
            while (start < val_end && value[start] == '.') {
                ++start;
            }
            cookie_copy_clean(cookie.domain, sizeof(cookie.domain),
                              value + start, val_end - start, 1);
            cookie.flags &= ~BROWSER_COOKIE_FLAG_HOST_ONLY;
            have_domain = 1;
        } else if (cookie_text_eq_ignore_case_len(value + name_start,
                                                  name_end - name_start,
                                                  "Path") &&
                   attr_eq != 0xffffffffU) {
            cookie_copy_clean(cookie.path, sizeof(cookie.path),
                              value + val_start, val_end - val_start, 0);
            if (cookie.path[0] != '/') {
                cookie_default_path(parsed.path, cookie.path,
                                    sizeof(cookie.path));
            }
        } else if (cookie_text_eq_ignore_case_len(value + name_start,
                                                  name_end - name_start,
                                                  "Max-Age") &&
                   attr_eq != 0xffffffffU) {
            int ok = 0;
            int64_t seconds = cookie_parse_i64_len(value + val_start,
                                                   val_end - val_start, &ok);
            if (ok && seconds <= 0) {
                delete_cookie = 1;
            } else if (ok) {
                uint64_t now = cookie_now_unix();
                if (now) {
                    cookie.expires_unix = now + (uint64_t)seconds;
                }
            }
        } else if (cookie_text_eq_ignore_case_len(value + name_start,
                                                  name_end - name_start,
                                                  "Secure")) {
            cookie.flags |= BROWSER_COOKIE_FLAG_SECURE;
        } else if (cookie_text_eq_ignore_case_len(value + name_start,
                                                  name_end - name_start,
                                                  "HttpOnly")) {
            cookie.flags |= BROWSER_COOKIE_FLAG_HTTP_ONLY;
        }
        pos = attr_end + 1U;
    }
    if (have_domain && !cookie_domain_match(parsed.host, &cookie)) {
        return;
    }
    cookie_store_or_delete(&cookie, delete_cookie);
}

static void cookie_accept_response_headers(const char *url,
                                           const char *headers)
{
    uint32_t pos = 0;
    cookie_ensure_loaded();
    while (headers && headers[pos]) {
        uint32_t line_start = pos;
        uint32_t line_end;
        uint32_t colon = 0xffffffffU;
        while (headers[pos] && headers[pos] != '\n' && headers[pos] != '\r') {
            ++pos;
        }
        line_end = pos;
        while (headers[pos] == '\n' || headers[pos] == '\r') {
            ++pos;
        }
        for (uint32_t i = line_start; i < line_end; ++i) {
            if (headers[i] == ':') {
                colon = i;
                break;
            }
        }
        if (colon == 0xffffffffU) {
            continue;
        }
        if (cookie_text_eq_ignore_case_len(headers + line_start,
                                           colon - line_start,
                                           "Set-Cookie")) {
            cookie_parse_set_cookie_value(url, headers + colon + 1U,
                                          line_end - colon - 1U);
        }
    }
}

static void cookie_build_header_value(const char *url,
                                      char *dst, uint32_t cap)
{
    struct parsed_http_url parsed;
    uint64_t now;
    uint32_t pos = 0;
    dst[0] = 0;
    cookie_ensure_loaded();
    cookie_cleanup_expired();
    if (!parse_http_url(url, &parsed)) {
        return;
    }
    now = cookie_now_unix();
    for (uint32_t i = 0; i < browser_cookie_count; ++i) {
        const struct browser_cookie *cookie = &browser_cookies[i];
        if (cookie_is_expired(cookie, now) ||
            !cookie_domain_match(parsed.host, cookie) ||
            !cookie_path_match(parsed.path, cookie->path) ||
            (cookie->flags & BROWSER_COOKIE_FLAG_SECURE)) {
            continue;
        }
        if (pos) {
            append_text(dst, &pos, cap, "; ");
        }
        append_text(dst, &pos, cap, cookie->name);
        append_char(dst, &pos, cap, '=');
        append_text(dst, &pos, cap, cookie->value);
    }
}

static void browser_http_header_value(const char *headers, const char *name,
                                      char *dst, uint32_t cap)
{
    uint32_t pos = 0;
    uint32_t name_len = cookie_text_len(name);
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    while (headers && headers[pos]) {
        uint32_t line_start = pos;
        uint32_t line_end;
        uint32_t colon = 0xffffffffU;
        while (headers[pos] && headers[pos] != '\n' && headers[pos] != '\r') {
            ++pos;
        }
        line_end = pos;
        while (headers[pos] == '\n' || headers[pos] == '\r') {
            ++pos;
        }
        for (uint32_t i = line_start; i < line_end; ++i) {
            if (headers[i] == ':') {
                colon = i;
                break;
            }
        }
        if (colon == 0xffffffffU || colon - line_start != name_len) {
            continue;
        }
        if (cookie_text_eq_ignore_case_len(headers + line_start,
                                           colon - line_start, name)) {
            uint32_t start;
            uint32_t end;
            cookie_trim_bounds(headers + colon + 1U,
                               line_end - colon - 1U, &start, &end);
            cookie_copy_trimmed(dst, cap, headers + colon + 1U + start,
                                end - start);
            return;
        }
    }
}

static int browser_http_is_redirect(uint32_t status)
{
    return status == 301U || status == 302U || status == 303U ||
           status == 307U || status == 308U;
}

static void browser_http_prepare_headers(const char *url,
                                         const char *extra_headers,
                                         char *out, uint32_t cap)
{
    char cookie_header[BROWSER_COOKIE_HEADER_CAP];
    uint32_t pos = 0;
    out[0] = 0;
    if (extra_headers && extra_headers[0]) {
        append_text(out, &pos, cap, extra_headers);
        if (pos < 2U || out[pos - 1U] != '\n') {
            append_text(out, &pos, cap, "\r\n");
        }
    }
    cookie_build_header_value(url, cookie_header, sizeof(cookie_header));
    if (cookie_header[0]) {
        append_text(out, &pos, cap, "Cookie: ");
        append_text(out, &pos, cap, cookie_header);
        append_text(out, &pos, cap, "\r\n");
    }
    browser_auth_append_header(url, out, &pos, cap);
}

static int browser_http_request_with_cookies(
    const char *url, const char *method, const char *extra_headers,
    const char *body, uint32_t body_len, uint32_t timeout_ms,
    char *response_body, uint32_t response_body_capacity,
    char *response_headers, uint32_t response_headers_capacity,
    struct leonos_http_response *response)
{
    char current_url[BROWSER_URL_CAP];
    char next_url[BROWSER_URL_CAP];
    char location[BROWSER_URL_CAP];
    char merged_headers[1024];
    const char *active_method = method && method[0] ? method : "GET";
    const char *active_extra = extra_headers;
    const char *active_body = body;
    uint32_t active_body_len = body_len;
    uint32_t redirect_count = 0;
    uint32_t preserved_flags = 0;
    uint8_t auth_retry_done = 0;
    int ret;
    if (!url || !response || !response_body || response_body_capacity == 0) {
        return -1;
    }
    copy_text(current_url, sizeof(current_url), url);
    for (;;) {
        struct leonos_http_request request;
        browser_http_prepare_headers(current_url, active_extra,
                                     merged_headers,
                                     sizeof(merged_headers));
        request = (struct leonos_http_request){
            .url = current_url,
            .method = active_method,
            .extra_headers = merged_headers,
            .request_body = active_body,
            .request_body_len = active_body_len,
            .timeout_ms = timeout_ms ? timeout_ms : LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
            .max_redirects = LEONOS_HTTP_NO_REDIRECTS,
            .response_body = response_body,
            .response_body_capacity = response_body_capacity,
            .response_headers = response_headers,
            .response_headers_capacity = response_headers_capacity,
        };
        ret = leonos_http_request(&request, response);
        if (ret < 0) {
            return ret;
        }
        response->redirect_count = redirect_count;
        response->flags |= preserved_flags;
        copy_text(response->final_url, sizeof(response->final_url),
                  current_url);
        if (response_headers && response_headers_capacity) {
            cookie_accept_response_headers(current_url, response_headers);
        }
        if (response->net_status != LEONOS_NET_STATUS_OK ||
            !browser_http_is_redirect(response->http_status)) {
            if (response->net_status == LEONOS_NET_STATUS_OK &&
                response->http_status == 401U && !auth_retry_done &&
                browser_auth_retry_from_challenge(current_url,
                                                  response_headers)) {
                auth_retry_done = 1;
                continue;
            }
            return 0;
        }
        browser_http_header_value(response_headers, "Location",
                                  location, sizeof(location));
        if (!location[0]) {
            return 0;
        }
        if (redirect_count >= LEONOS_HTTP_DEFAULT_REDIRECTS ||
            leonos_http_resolve_url(current_url, location,
                                    next_url, sizeof(next_url)) < 0) {
            response->net_status = LEONOS_NET_STATUS_HTTP_FAILED;
            return 0;
        }
        copy_text(current_url, sizeof(current_url), next_url);
        ++redirect_count;
        preserved_flags |= LEONOS_HTTP_FLAG_REDIRECTED;
        if (response->http_status == 303U ||
            ((response->http_status == 301U || response->http_status == 302U) &&
             !text_eq_ignore_case(active_method, "GET"))) {
            active_method = "GET";
            active_extra = 0;
            active_body = 0;
            active_body_len = 0;
        }
    }
}

int browser_http_get_with_cookies(const char *url, uint32_t timeout_ms,
                                  char *response_body,
                                  uint32_t response_body_capacity,
                                  char *response_headers,
                                  uint32_t response_headers_capacity,
                                  struct leonos_http_response *response)
{
    return browser_http_request_with_cookies(
        url, "GET", 0, 0, 0, timeout_ms, response_body, response_body_capacity,
        response_headers, response_headers_capacity, response);
}

int browser_http_post_with_cookies(const char *url, const char *body,
                                   char *response_body,
                                   uint32_t response_body_capacity,
                                   char *response_headers,
                                   uint32_t response_headers_capacity,
                                   struct leonos_http_response *response)
{
    const char *request_body = body ? body : "";
    return browser_http_request_with_cookies(
        url, "POST", "Content-Type: application/x-www-form-urlencoded\r\n",
        request_body, (uint32_t)strlen(request_body),
        LEONOS_HTTP_DEFAULT_TIMEOUT_MS, response_body,
        response_body_capacity, response_headers,
        response_headers_capacity, response);
}
