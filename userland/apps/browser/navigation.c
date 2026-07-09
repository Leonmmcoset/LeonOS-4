#include "browser.h"

#define BROWSER_EXTERNAL_CSS_MAX 3072U
#define BROWSER_CSS_FETCH_MAX 2048U
#define BROWSER_CSS_LINK_MAX 4U
#define BROWSER_TAG_TEXT_MAX 512U

static char browser_http_headers[LEONOS_HTTP_HEADER_MAX + 1U];
static char browser_css_headers[LEONOS_HTTP_HEADER_MAX + 1U];
static char browser_css_body[BROWSER_CSS_FETCH_MAX + 1U];
static char browser_external_css[BROWSER_EXTERNAL_CSS_MAX + 1U];
static char browser_combined_source[BROWSER_SOURCE_CAP];

void render_html_source(const char *source, const char *base_url)
{
    const char *inline_source;
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
    inline_source = browser_forms_render_inline_source(source, base_url);
    scroll_x = 0;
    litehtml_core_render_html(&view, inline_source, base_url);
    browser_form_rebind_focus();
    clamp_scroll();
}

void render_plain_source(const char *source)
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
    scroll_x = 0;
    litehtml_core_render_plain(&view, source);
    clamp_scroll();
}

void rerender_page(void)
{
    if (page_is_html) {
        render_html_source(page_source, current_location);
    } else {
        browser_forms_clear();
        browser_form_clear_focus();
        render_plain_source(page_source);
    }
}

void set_page_source(const char *title, const char *source,
                            uint8_t is_html, const char *status)
{
    browser_form_clear_focus();
    copy_text(page_title, sizeof(page_title), title && title[0] ? title : T("Untitled", "无标题"));
    copy_text(page_source, sizeof(page_source), source ? source : "");
    page_is_html = is_html;
    source_truncated = 0;
    rerender_page();
    set_status(status);
}

void render_message_page(const char *title, const char *message,
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

static const char *browser_safe_detail(const char *detail)
{
    return browser_embedded ? "" : detail;
}

void push_history(const char *url)
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

void format_ret_status(char *dst, uint32_t cap, const char *prefix, int32_t ret)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, prefix);
    append_text(dst, &pos, cap, " ret=");
    append_i32(dst, &pos, cap, ret);
}

void load_about(void)
{
    static const char about_html[] =
        "<html><head><title>LeonOS Browser</title></head>"
        "<style>"
        "body{color:#202020;background:#ffffff;}"
        ".hero{background:#eaf3ff;border-left:4px solid #2f65c8;padding-left:2em;}"
        ".muted{color:#606870;}"
        ".ok{color:#106020;font-weight:bold;}"
        ".warn{color:#8a4a00;font-weight:bold;}"
        "h1{color:#1d3f7a;}h2{color:#244f90;}h3{color:#305f9f;}"
        "table{border-color:#a8b8c8;background:#f7fbff;}"
        "</style>"
        "<body>"
        "<h1>LeonOS Browser</h1>"
        "<p class=\"hero\"><strong>New start page for LeonOS 4.</strong><br>"
        "LeonOS 4 新版浏览器主页。</p>"
        "<h2>Quick Start / 快速开始</h2>"
        "<p>Open HTTP pages, local HTML documents, and files from LeonOS paths.</p>"
        "<p>可以打开 HTTP 网页、本地 HTML 文档，以及 LeonOS 文件路径。</p>"
        "<p><a href=\"http://example.com/\">Open example.com / 打开 example.com</a></p>"
        "<h2>What Works / 当前支持</h2>"
        "<ul>"
        "<li>HTTP GET, DNS, TCP, browser history, refresh, home, and scrolling.</li>"
        "<li>HTTP GET、DNS、TCP、历史记录、刷新、主页和滚动。</li>"
        "<li>Headings, links, lists, blockquotes, tables, inline styles, and basic CSS.</li>"
        "<li>标题、链接、列表、引用、表格、行内样式和基础 CSS。</li>"
        "</ul>"
        "<h2>Status / 状态</h2>"
        "<table>"
        "<tr><td><strong>Network</strong></td><td class=\"ok\">HTTP enabled / HTTP 已启用</td></tr>"
        "<tr><td><strong>Files</strong></td><td class=\"ok\">0:/ paths and .html files / 支持 0:/ 路径和 .html 文件</td></tr>"
        "<tr><td><strong>Limit</strong></td><td class=\"warn\">No HTTPS or JavaScript yet / 暂不支持 HTTPS 和 JavaScript</td></tr>"
        "</table>"
        "<h3>Tip / 提示</h3>"
        "<blockquote>Type a URL in the address bar, or open a local .html file from File Manager.<br>"
        "可以在地址栏输入网址，也可以从文件资源管理器打开本地 .html 文件。</blockquote>"
        "<p class=\"muted\">about:leonos</p>"
        "</body></html>";
    copy_text(current_location, sizeof(current_location), "about:leonos");
    copy_text(address_input, sizeof(address_input), current_location);
    leonos_ui_edit_state_sync(&address_edit);
    set_page_source("LeonOS Browser", about_html, 1, T("Ready", "就绪"));
}

static void browser_copy_bytes(char *dst, uint32_t cap,
                               const char *src, uint32_t len)
{
    uint32_t n = len;
    if (!dst || cap == 0) {
        return;
    }
    if (n + 1U > cap) {
        n = cap - 1U;
    }
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src ? src[i] : 0;
    }
    dst[n] = 0;
}

static int browser_contains_ignore_case(const char *text, const char *needle)
{
    uint32_t text_len;
    uint32_t needle_len;
    if (!text || !needle) {
        return 0;
    }
    text_len = (uint32_t)strlen(text);
    needle_len = (uint32_t)strlen(needle);
    if (needle_len == 0 || needle_len > text_len) {
        return 0;
    }
    for (uint32_t i = 0; i + needle_len <= text_len; ++i) {
        uint32_t j = 0;
        while (j < needle_len &&
               ascii_tolower(text[i + j]) == ascii_tolower(needle[j])) {
            ++j;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int browser_attr_name_eq(const char *text, uint32_t len,
                                const char *name)
{
    uint32_t name_len = (uint32_t)strlen(name);
    if (len != name_len) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (ascii_tolower(text[i]) != ascii_tolower(name[i])) {
            return 0;
        }
    }
    return 1;
}

static int browser_extract_attr(const char *tag, const char *name,
                                char *out, uint32_t cap)
{
    uint32_t i = 0;
    if (out && cap) {
        out[0] = 0;
    }
    if (!tag || !name || !out || cap == 0) {
        return 0;
    }
    while (tag[i]) {
        uint32_t name_start;
        uint32_t name_end;
        uint32_t value_start;
        uint32_t value_end;
        char quote = 0;
        while (tag[i] && (is_space_char(tag[i]) || tag[i] == '<' ||
                          tag[i] == '/' || tag[i] == '>')) {
            ++i;
        }
        name_start = i;
        while (tag[i] && !is_space_char(tag[i]) && tag[i] != '=' &&
               tag[i] != '/' && tag[i] != '>') {
            ++i;
        }
        name_end = i;
        while (tag[i] && is_space_char(tag[i])) {
            ++i;
        }
        if (tag[i] != '=') {
            continue;
        }
        ++i;
        while (tag[i] && is_space_char(tag[i])) {
            ++i;
        }
        if (tag[i] == '"' || tag[i] == '\'') {
            quote = tag[i++];
        }
        value_start = i;
        if (quote) {
            while (tag[i] && tag[i] != quote) {
                ++i;
            }
        } else {
            while (tag[i] && !is_space_char(tag[i]) && tag[i] != '>') {
                ++i;
            }
        }
        value_end = i;
        if (quote && tag[i] == quote) {
            ++i;
        }
        if (browser_attr_name_eq(tag + name_start, name_end - name_start,
                                 name)) {
            browser_copy_bytes(out, cap, tag + value_start,
                               value_end - value_start);
            return 1;
        }
    }
    return 0;
}

static int browser_tag_name_at(const char *source, const char *name)
{
    uint32_t i = 0;
    uint32_t name_len = (uint32_t)strlen(name);
    if (!source || source[0] != '<') {
        return 0;
    }
    i = 1;
    while (source[i] && is_space_char(source[i])) {
        ++i;
    }
    if (source[i] == '/') {
        return 0;
    }
    for (uint32_t n = 0; n < name_len; ++n) {
        if (ascii_tolower(source[i + n]) != ascii_tolower(name[n])) {
            return 0;
        }
    }
    i += name_len;
    return source[i] == 0 || source[i] == '>' || source[i] == '/' ||
           is_space_char(source[i]);
}

static uint32_t browser_copy_tag_at(const char *source, char *tag,
                                    uint32_t tag_cap)
{
    uint32_t i = 0;
    if (!source || !tag || tag_cap == 0) {
        return 0;
    }
    while (source[i] && source[i] != '>' && i + 1U < tag_cap) {
        tag[i] = source[i];
        ++i;
    }
    if (source[i] == '>' && i + 1U < tag_cap) {
        tag[i++] = '>';
    }
    tag[i] = 0;
    return i;
}

static uint32_t browser_fetch_external_css(const char *base_url)
{
    char tag[BROWSER_TAG_TEXT_MAX];
    char rel[80];
    char href[BROWSER_URL_CAP];
    char resolved[BROWSER_URL_CAP];
    uint32_t css_pos = 0;
    uint32_t scan = 0;
    uint32_t count = 0;
    browser_external_css[0] = 0;
    while (page_source[scan] && count < BROWSER_CSS_LINK_MAX) {
        uint32_t tag_len;
        struct leonos_http_response css_response;
        if (page_source[scan] != '<' ||
            !browser_tag_name_at(page_source + scan, "link")) {
            ++scan;
            continue;
        }
        tag_len = browser_copy_tag_at(page_source + scan, tag, sizeof(tag));
        if (!tag_len) {
            ++scan;
            continue;
        }
        scan += tag_len;
        rel[0] = 0;
        href[0] = 0;
        if (!browser_extract_attr(tag, "rel", rel, sizeof(rel)) ||
            !browser_contains_ignore_case(rel, "stylesheet") ||
            !browser_extract_attr(tag, "href", href, sizeof(href))) {
            continue;
        }
        if (leonos_http_resolve_url(base_url, href, resolved,
                                    sizeof(resolved)) < 0 ||
            !starts_with_ignore_case(resolved, "http://")) {
            continue;
        }
        browser_css_body[0] = 0;
        browser_css_headers[0] = 0;
        if (browser_http_get_with_cookies(resolved, 5000,
                                          browser_css_body,
                                          sizeof(browser_css_body),
                                          browser_css_headers,
                                          sizeof(browser_css_headers),
                                          &css_response) < 0 ||
            css_response.net_status != LEONOS_NET_STATUS_OK ||
            css_response.http_status < 200U ||
            css_response.http_status >= 400U) {
            continue;
        }
        for (uint32_t i = 0; i < css_response.body_len; ++i) {
            if (!browser_css_body[i]) {
                browser_css_body[i] = ' ';
            }
        }
        if (css_response.flags & LEONOS_HTTP_FLAG_TRUNCATED) {
            source_truncated = 1;
        }
        append_text(browser_external_css, &css_pos,
                    sizeof(browser_external_css), browser_css_body);
        append_text(browser_external_css, &css_pos,
                    sizeof(browser_external_css), "\n");
        ++count;
    }
    return count;
}

static uint32_t browser_inject_external_css(const char *base_url)
{
    uint32_t css_count = browser_fetch_external_css(base_url);
    uint32_t pos = 0;
    uint32_t wrapper_len;
    if (!css_count || !browser_external_css[0]) {
        return 0;
    }
    browser_combined_source[0] = 0;
    append_text(browser_combined_source, &pos,
                sizeof(browser_combined_source), "<style>\n");
    append_text(browser_combined_source, &pos,
                sizeof(browser_combined_source), browser_external_css);
    append_text(browser_combined_source, &pos,
                sizeof(browser_combined_source), "</style>\n");
    wrapper_len = pos;
    append_text(browser_combined_source, &pos,
                sizeof(browser_combined_source), page_source);
    if (wrapper_len + (uint32_t)strlen(page_source) + 1U >
        sizeof(browser_combined_source)) {
        source_truncated = 1;
    }
    copy_text(page_source, sizeof(page_source), browser_combined_source);
    return css_count;
}

static int browser_response_is_html(const struct leonos_http_response *response,
                                    const char *url)
{
    if (response && response->content_type[0]) {
        if (browser_contains_ignore_case(response->content_type, "text/plain") ||
            browser_contains_ignore_case(response->content_type, "text/css") ||
            browser_contains_ignore_case(response->content_type, "json")) {
            return 0;
        }
        if (browser_contains_ignore_case(response->content_type, "html") ||
            browser_contains_ignore_case(response->content_type, "xml")) {
            return 1;
        }
    }
    if (ends_with_ignore_case(url, ".txt") ||
        ends_with_ignore_case(url, ".css") ||
        ends_with_ignore_case(url, ".json")) {
        return 0;
    }
    return 1;
}

void load_http_form_post(const char *url, const char *body)
{
    struct parsed_http_url parsed;
    struct leonos_http_response response;
    uint32_t pos = 0;
    uint32_t css_count = 0;
    int ret;
    char normalized[BROWSER_URL_CAP];
    char status[BROWSER_STATUS_CAP];
    browser_form_clear_focus();
    if (!parse_http_url(url, &parsed)) {
        render_message_page(T("Invalid URL", "无效地址"),
                            T("The form action could not be parsed as HTTP.",
                              "无法把表单提交地址解析为 HTTP。"),
                            browser_safe_detail(url));
        return;
    }
    build_http_url(normalized, sizeof(normalized), parsed.host, parsed.port, parsed.path);
    copy_text(current_location, sizeof(current_location), normalized);
    copy_text(address_input, sizeof(address_input), normalized);
    leonos_ui_edit_state_sync(&address_edit);
    set_status(T("Submitting form...", "正在提交表单..."));
    present_browser();
    page_source[0] = 0;
    browser_http_headers[0] = 0;
    source_truncated = 0;
    ret = browser_http_post_with_cookies(normalized, body,
                                         page_source, sizeof(page_source),
                                         browser_http_headers,
                                         sizeof(browser_http_headers),
                                         &response);
    if (ret < 0) {
        format_ret_status(status, sizeof(status),
                          T("HTTP client failed", "HTTP 客户端失败"), ret);
        render_message_page(T("Network Error", "网络错误"), status,
                            browser_safe_detail(normalized));
        return;
    }
    if (response.final_url[0]) {
        copy_text(current_location, sizeof(current_location), response.final_url);
        copy_text(address_input, sizeof(address_input), current_location);
        leonos_ui_edit_state_sync(&address_edit);
    }
    for (uint32_t i = 0; i < response.body_len; ++i) {
        if (!page_source[i]) {
            page_source[i] = ' ';
        }
    }
    if (response.flags & LEONOS_HTTP_FLAG_TRUNCATED) {
        source_truncated = 1;
    }
    status[0] = 0;
    append_text(status, &pos, sizeof(status), net_status_name(response.net_status));
    append_text(status, &pos, sizeof(status), "  HTTP ");
    append_u32(status, &pos, sizeof(status), response.http_status);
    append_text(status, &pos, sizeof(status), "  ");
    append_u32(status, &pos, sizeof(status), response.body_len);
    append_text(status, &pos, sizeof(status), " bytes");
    if (response.redirect_count) {
        append_text(status, &pos, sizeof(status), "  redirects ");
        append_u32(status, &pos, sizeof(status), response.redirect_count);
    }
    if (source_truncated) {
        append_text(status, &pos, sizeof(status), "  truncated");
    }
    if (response.net_status != LEONOS_NET_STATUS_OK) {
        render_message_page(T("Network Error", "网络错误"), status,
                            browser_safe_detail(normalized));
        return;
    }
    page_is_html = (uint8_t)browser_response_is_html(&response, current_location);
    if (page_is_html) {
        css_count = browser_inject_external_css(current_location);
        if (css_count) {
            append_text(status, &pos, sizeof(status), "  css ");
            append_u32(status, &pos, sizeof(status), css_count);
        }
    }
    if (parse_http_url(current_location, &parsed)) {
        copy_text(page_title, sizeof(page_title), parsed.host);
    } else {
        copy_text(page_title, sizeof(page_title), T("HTTP Page", "HTTP 页面"));
    }
    rerender_page();
    set_status(status);
}

void load_http_url(const char *url)
{
    struct parsed_http_url parsed;
    struct leonos_http_response response;
    uint32_t pos = 0;
    uint32_t css_count = 0;
    int ret;
    char normalized[BROWSER_URL_CAP];
    char status[BROWSER_STATUS_CAP];
    browser_form_clear_focus();
    if (!parse_http_url(url, &parsed)) {
        render_message_page(T("Invalid URL", "无效地址"),
                            T("The address could not be parsed as HTTP.", "无法把该地址解析为 HTTP。"),
                            browser_safe_detail(url));
        return;
    }
    build_http_url(normalized, sizeof(normalized), parsed.host, parsed.port, parsed.path);
    copy_text(current_location, sizeof(current_location), normalized);
    copy_text(address_input, sizeof(address_input), normalized);
    leonos_ui_edit_state_sync(&address_edit);
    set_status(T("Opening page...", "正在打开页面..."));
    present_browser();
    page_source[0] = 0;
    browser_http_headers[0] = 0;
    source_truncated = 0;
    ret = browser_http_get_with_cookies(normalized,
                                        LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                                        page_source, sizeof(page_source),
                                        browser_http_headers,
                                        sizeof(browser_http_headers),
                                        &response);
    if (ret < 0) {
        format_ret_status(status, sizeof(status),
                          T("HTTP client failed", "HTTP 客户端失败"), ret);
        render_message_page(T("Network Error", "网络错误"), status,
                            browser_safe_detail(normalized));
        return;
    }
    if (response.final_url[0]) {
        copy_text(current_location, sizeof(current_location),
                  response.final_url);
        copy_text(address_input, sizeof(address_input), current_location);
        leonos_ui_edit_state_sync(&address_edit);
    }
    for (uint32_t i = 0; i < response.body_len; ++i) {
        if (!page_source[i]) {
            page_source[i] = ' ';
        }
    }
    if (response.flags & LEONOS_HTTP_FLAG_TRUNCATED) {
        source_truncated = 1;
    }
    status[0] = 0;
    append_text(status, &pos, sizeof(status), net_status_name(response.net_status));
    append_text(status, &pos, sizeof(status), "  HTTP ");
    append_u32(status, &pos, sizeof(status), response.http_status);
    append_text(status, &pos, sizeof(status), "  ");
    append_u32(status, &pos, sizeof(status), response.body_len);
    append_text(status, &pos, sizeof(status), " bytes");
    if (response.redirect_count) {
        append_text(status, &pos, sizeof(status), "  redirects ");
        append_u32(status, &pos, sizeof(status), response.redirect_count);
    }
    if (response.flags & LEONOS_HTTP_FLAG_CHUNKED) {
        append_text(status, &pos, sizeof(status), "  chunked");
    }
    if (source_truncated) {
        append_text(status, &pos, sizeof(status), "  truncated");
    }
    if (response.net_status != LEONOS_NET_STATUS_OK) {
        render_message_page(T("Network Error", "网络错误"), status,
                            browser_safe_detail(normalized));
        return;
    }
    page_is_html = (uint8_t)browser_response_is_html(&response,
                                                     current_location);
    if (page_is_html) {
        css_count = browser_inject_external_css(current_location);
        if (css_count) {
            append_text(status, &pos, sizeof(status), "  css ");
            append_u32(status, &pos, sizeof(status), css_count);
        }
    }
    if (parse_http_url(current_location, &parsed)) {
        copy_text(page_title, sizeof(page_title), parsed.host);
    } else {
        copy_text(page_title, sizeof(page_title), T("HTTP Page", "HTTP 页面"));
    }
    rerender_page();
    set_status(status);
}

void load_local_file(const char *path)
{
    int fd;
    uint32_t len = 0;
    char status[BROWSER_STATUS_CAP];
    source_truncated = 0;
    browser_form_clear_focus();
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

static int browser_should_download_http_url(const char *url)
{
    if (!url || !starts_with_ignore_case(url, "http://")) {
        return 0;
    }
    return ends_with_ignore_case(url, ".bmp") ||
           ends_with_ignore_case(url, ".png") ||
           ends_with_ignore_case(url, ".jpg") ||
           ends_with_ignore_case(url, ".jpeg") ||
           ends_with_ignore_case(url, ".gif") ||
           ends_with_ignore_case(url, ".zip") ||
           ends_with_ignore_case(url, ".bin") ||
           ends_with_ignore_case(url, ".elf") ||
           ends_with_ignore_case(url, ".iso") ||
           ends_with_ignore_case(url, ".vmdk") ||
           ends_with_ignore_case(url, ".pdf") ||
           ends_with_ignore_case(url, ".7z") ||
           ends_with_ignore_case(url, ".tar") ||
           ends_with_ignore_case(url, ".gz");
}

static void launch_download_for_url(const char *url)
{
    char target[LEONOS_HTTP_URL_LEN];
    char *argv[3];
    copy_text(target, sizeof(target), url);
    argv[0] = "0:/userland/downloadmgr.elf";
    argv[1] = target;
    argv[2] = 0;
    if (leonos_launch_argv(argv) < 0) {
        set_status(T("Could not start Download Manager", "无法启动下载管理器"));
    } else {
        set_status(T("Download started", "下载已开始"));
    }
}

void navigate_to(const char *input, uint8_t add_to_history)
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
                            browser_safe_detail(url));
    } else if (starts_with_ignore_case(url, "http://")) {
        if (browser_should_download_http_url(url)) {
            launch_download_for_url(url);
            return;
        } else {
            load_http_url(url);
        }
    } else if (is_drive_path(url)) {
        load_local_file(url);
    } else {
        render_message_page(T("Unsupported Address", "不支持的地址"),
                            T("Use http://, about:, or a LeonOS file path such as 0:/file.html.",
                              "请使用 http://、about:，或类似 0:/file.html 的 LeonOS 文件路径。"),
                            browser_safe_detail(url));
    }
    if (add_to_history) {
        push_history(current_location);
    }
}

void go_back(void)
{
    if (history_index > 0) {
        --history_index;
        navigate_to(history[(uint32_t)history_index], 0);
    }
}

void go_forward(void)
{
    if (history_index >= 0 && (uint32_t)history_index + 1U < history_count) {
        ++history_index;
        navigate_to(history[(uint32_t)history_index], 0);
    }
}
