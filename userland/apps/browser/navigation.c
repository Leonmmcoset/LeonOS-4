#include "browser.h"

#include <leonos/png.h>
#include <leonos/text.h>
#include <stdlib.h>

#define BROWSER_HTTP_MAX_RETRIES 3U

static char browser_http_headers[LEONOS_HTTP_HEADER_MAX + 1U];
static char browser_combined_source[BROWSER_SOURCE_CAP];

static int browser_url_has_query(const char *url)
{
    while (url && *url) {
        if (*url == '?') {
            return 1;
        }
        ++url;
    }
    return 0;
}

static void browser_decode_page_source(uint32_t length)
{
    uint32_t encoding = LEONOS_TEXT_ENCODING_UTF8;
    uint32_t decoded_len = 0;
    uint32_t replacements = 0;
    char *raw;
    int ret;
    if (length >= BROWSER_SOURCE_CAP) {
        length = BROWSER_SOURCE_CAP - 1U;
        source_truncated = 1;
    }
    page_source[length] = 0;
    if (length == 0 || leonos_text_detect_encoding(page_source, length,
                                                   &encoding) < 0) {
        return;
    }
    /* The decoder permits no overlapping input/output.  Keep the raw body
     * temporarily so GBK and UTF-16 can expand into UTF-8 safely. */
    raw = (char *)malloc((size_t)length + 1U);
    if (!raw) {
        return;
    }
    memcpy(raw, page_source, length);
    raw[length] = 0;
    ret = leonos_text_decode(raw, length, encoding, page_source,
                             BROWSER_SOURCE_CAP, &decoded_len, &replacements);
    free(raw);
    if (ret < 0 && ret != LEONOS_TEXT_ENCODING_NO_SPACE) {
        return;
    }
    if (ret == LEONOS_TEXT_ENCODING_NO_SPACE ||
        decoded_len + 1U >= BROWSER_SOURCE_CAP) {
        source_truncated = 1;
        decoded_len = BROWSER_SOURCE_CAP - 1U;
    }
    page_source[decoded_len] = 0;
    (void)replacements;
}

int browser_litehtml_fetch_resource(void *opaque, const char *url,
                                    uint8_t **data, uint32_t *size,
                                    char *content_type,
                                    uint32_t content_type_cap)
{
    struct leonos_http_response response;
    char *headers;
    uint32_t capacity = LEONOS_PNG_MAX_FILE_BYTES;
    int ret;
    (void)opaque;
    if (!url || !data || !size ||
        (!starts_with_ignore_case(url, "http://") &&
         !starts_with_ignore_case(url, "https://"))) {
        return -1;
    }
    *data = 0;
    *size = 0;
    if (content_type && content_type_cap) {
        content_type[0] = 0;
    }
    *data = (uint8_t *)malloc((size_t)capacity + 1U);
    headers = (char *)malloc(LEONOS_HTTP_HEADER_MAX + 1U);
    if (!*data || !headers) {
        free(*data);
        free(headers);
        *data = 0;
        return -1;
    }
    ret = browser_http_get_with_cookies(url, LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                                        (char *)*data, capacity + 1U,
                                        headers, LEONOS_HTTP_HEADER_MAX + 1U,
                                        &response);
    free(headers);
    if (ret < 0 || response.net_status != LEONOS_NET_STATUS_OK ||
        response.http_status < 200U || response.http_status >= 300U ||
        response.body_len == 0 || response.body_len > capacity) {
        free(*data);
        *data = 0;
        return -1;
    }
    if (content_type && content_type_cap) {
        copy_text(content_type, content_type_cap, response.content_type);
    }
    *size = response.body_len;
    return 0;
}

static void browser_litehtml_link(void *opaque, const char *url)
{
    (void)opaque;
    if (url && url[0]) {
        navigate_to(url, 1);
    }
}

static void browser_litehtml_title(void *opaque, const char *title)
{
    (void)opaque;
    if (title && title[0]) {
        copy_text(page_title, sizeof(page_title), title);
    }
}

static void browser_litehtml_submit(void *opaque, const char *action,
                                    const char *method, const char *body)
{
    char resolved[BROWSER_URL_CAP];
    const char *target = action && action[0] ? action : current_location;
    (void)opaque;
    if (action && action[0] &&
        leonos_http_resolve_url(current_location, target, resolved,
                                sizeof(resolved)) == 0) {
        target = resolved;
    }
    copy_text(browser_pending_form_url, sizeof(browser_pending_form_url), target);
    copy_text(browser_pending_form_method, sizeof(browser_pending_form_method),
              method && method[0] ? method : "get");
    copy_text(browser_pending_form_body, sizeof(browser_pending_form_body),
              body ? body : "");
    browser_pending_form = 1;
}

void browser_process_pending_form(void)
{
    char url[BROWSER_URL_CAP];
    char method[12];
    char body[BROWSER_FORM_BODY_CAP];
    uint32_t pos;
    if (!browser_pending_form) {
        return;
    }
    copy_text(url, sizeof(url), browser_pending_form_url);
    copy_text(method, sizeof(method), browser_pending_form_method);
    copy_text(body, sizeof(body), browser_pending_form_body);
    browser_pending_form = 0;
    browser_pending_form_url[0] = 0;
    browser_pending_form_method[0] = 0;
    browser_pending_form_body[0] = 0;
    if (text_eq_ignore_case(method, "post")) {
        load_http_form_post(url, body);
        return;
    }
    if (body[0]) {
        pos = (uint32_t)strlen(url);
        append_char(url, &pos, sizeof(url),
                    browser_url_has_query(url) ? '&' : '?');
        append_text(url, &pos, sizeof(url), body);
    }
    navigate_to(url, 1);
}

static void browser_escape_pre(const char *source)
{
    uint32_t pos = 0;
    browser_combined_source[0] = 0;
    append_text(browser_combined_source, &pos, sizeof(browser_combined_source),
                "<pre style=\"white-space: pre-wrap; word-wrap: break-word\">\n");
    while (source && *source) {
        const char *replacement = 0;
        if (*source == '&') {
            replacement = "&amp;";
        } else if (*source == '<') {
            replacement = "&lt;";
        } else if (*source == '>') {
            replacement = "&gt;";
        }
        if (replacement) {
            append_text(browser_combined_source, &pos,
                        sizeof(browser_combined_source), replacement);
        } else {
            append_char(browser_combined_source, &pos,
                        sizeof(browser_combined_source), *source);
        }
        ++source;
    }
    append_text(browser_combined_source, &pos, sizeof(browser_combined_source),
                "\n</pre>");
}

void render_html_source(const char *source, const char *base_url)
{
    uint32_t width = document_text_w();
    if (width < 1U) {
        width = 1U;
    }
    if (browser_document) {
        browser_litehtml_destroy(browser_document);
        browser_document = 0;
    }
    browser_form_count = 0;
    browser_form_control_count = 0;
    scroll_x = 0;
    browser_document = browser_litehtml_create(source ? source : "",
                                               base_url ? base_url : "",
                                               browser_litehtml_link,
                                               browser_litehtml_title,
                                               browser_litehtml_submit,
                                               browser_litehtml_fetch_resource,
                                               0, width, document_view_h());
    browser_document_width = width;
    browser_document_height = browser_document
                                  ? browser_litehtml_content_height(browser_document)
                                  : 0U;
    browser_form_count = browser_litehtml_form_count(browser_document);
    browser_form_control_count =
        browser_litehtml_form_control_count(browser_document);
    browser_form_clear_focus();
    clamp_scroll();
}

void render_plain_source(const char *source)
{
    browser_escape_pre(source ? source : "");
    if (browser_document) {
        browser_litehtml_destroy(browser_document);
        browser_document = 0;
    }
    scroll_x = 0;
    browser_document = browser_litehtml_create(browser_combined_source,
                                               current_location,
                                               browser_litehtml_link,
                                               browser_litehtml_title,
                                               browser_litehtml_submit,
                                               browser_litehtml_fetch_resource,
                                               0, document_text_w(), document_view_h());
    browser_document_width = document_text_w();
    browser_document_height = browser_document
                                  ? browser_litehtml_content_height(browser_document)
                                  : 0U;
    clamp_scroll();
}

void rerender_page(void)
{
    if (page_is_html) {
        render_html_source(page_source, current_location);
    } else {
        browser_form_clear_focus();
        render_plain_source(page_source);
    }
}

void set_page_source(const char *title, const char *source,
                            uint8_t is_html, const char *status)
{
    char window_title[47];
    uint32_t title_pos = 0;
    browser_form_clear_focus();
    /* A new document, including history navigation, starts at its origin.
     * Retaining the previous offset can make a shorter page look blank. */
    browser_scroll_y = 0;
    scroll_x = 0;
    copy_text(page_title, sizeof(page_title), title && title[0] ? title : T("Untitled", "无标题"));
    copy_text(page_source, sizeof(page_source), source ? source : "");
    page_is_html = is_html;
    source_truncated = 0;
    rerender_page();
    if (!browser_embedded && window_id > 0) {
        window_title[0] = 0;
        append_text(window_title, &title_pos, sizeof(window_title), "LeonOS Browser - ");
        append_text(window_title, &title_pos, sizeof(window_title), page_title);
        (void)leonos_gui_set_window_title((uint32_t)window_id, window_title);
    }
    set_status(status);
}

void render_message_page(const char *title, const char *message,
                                const char *detail)
{
    static char text[BROWSER_SOURCE_CAP];
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
        "<p>Open HTTP and HTTPS pages, local HTML documents, and files from LeonOS paths.</p>"
        "<p>可以打开 HTTP、HTTPS 网页、本地 HTML 文档，以及 LeonOS 文件路径。</p>"
        "<p><a href=\"https://example.com/\">Open example.com / 打开 example.com</a></p>"
        "<h2>What Works / 当前支持</h2>"
        "<ul>"
        "<li>HTTP/HTTPS GET, DNS, TCP, certificate validation, browser history, refresh, home, and scrolling.</li>"
        "<li>HTTP/HTTPS GET、DNS、TCP、证书验证、历史记录、刷新、主页和滚动。</li>"
        "<li>Headings, links, lists, blockquotes, tables, inline styles, and basic CSS.</li>"
        "<li>标题、链接、列表、引用、表格、行内样式和基础 CSS。</li>"
        "</ul>"
        "<h2>Status / 状态</h2>"
        "<table>"
        "<tr><td><strong>Network</strong></td><td class=\"ok\">HTTP and HTTPS enabled / HTTP 和 HTTPS 已启用</td></tr>"
        "<tr><td><strong>Files</strong></td><td class=\"ok\">0:/ paths and .html files / 支持 0:/ 路径和 .html 文件</td></tr>"
        "<tr><td><strong>Limit</strong></td><td class=\"warn\">No JavaScript yet / 暂不支持 JavaScript</td></tr>"
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

static int is_retriable_error(int ret, uint32_t net_status)
{
    if (ret < 0) {
        return 1;
    }
    if (net_status == LEONOS_NET_STATUS_DNS_TIMEOUT ||
        net_status == LEONOS_NET_STATUS_DNS_FAILED ||
        net_status == LEONOS_NET_STATUS_DNS_NO_ANSWER ||
        net_status == LEONOS_NET_STATUS_TCP_TIMEOUT ||
        net_status == LEONOS_NET_STATUS_TCP_RESET ||
        net_status == LEONOS_NET_STATUS_TCP_FAILED ||
        net_status == LEONOS_NET_STATUS_SOCKET_LIMIT ||
        net_status == LEONOS_NET_STATUS_TX_FAILED ||
        net_status == LEONOS_NET_STATUS_TLS_FAILED) {
        return 1;
    }
    return 0;
}

void load_http_form_post(const char *url, const char *body)
{
    struct parsed_http_url parsed;
    struct leonos_http_response response;
    uint32_t pos = 0;
    uint32_t retries = 0;
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
    build_http_url(normalized, sizeof(normalized), parsed.host, parsed.port,
                   parsed.secure, parsed.path);
    copy_text(current_location, sizeof(current_location), normalized);
    copy_text(address_input, sizeof(address_input), normalized);
    leonos_ui_edit_state_sync(&address_edit);
    set_status(T("Submitting form...", "正在提交表单..."));
    present_browser();
    for (;;) {
        page_source[0] = 0;
        browser_http_headers[0] = 0;
        source_truncated = 0;
        ret = browser_http_post_with_cookies(normalized, body,
                                             page_source, sizeof(page_source),
                                             browser_http_headers,
                                             sizeof(browser_http_headers),
                                             &response);
        if (ret >= 0 && response.net_status == LEONOS_NET_STATUS_OK) {
            break;
        }
        if (!is_retriable_error(ret, response.net_status)) {
            break;
        }
        ++retries;
        if (retries > BROWSER_HTTP_MAX_RETRIES) {
            break;
        }
        set_status(T("Retrying form submission...", "正在重新提交表单..."));
    }
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
    browser_decode_page_source(response.body_len);
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
    uint32_t retries = 0;
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
    build_http_url(normalized, sizeof(normalized), parsed.host, parsed.port,
                   parsed.secure, parsed.path);
    copy_text(current_location, sizeof(current_location), normalized);
    copy_text(address_input, sizeof(address_input), normalized);
    leonos_ui_edit_state_sync(&address_edit);
    set_status(T("Opening page...", "正在打开页面..."));
    present_browser();
    for (;;) {
        page_source[0] = 0;
        browser_http_headers[0] = 0;
        source_truncated = 0;
        ret = browser_http_get_with_cookies(normalized,
                                            LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                                            page_source, sizeof(page_source),
                                            browser_http_headers,
                                            sizeof(browser_http_headers),
                                            &response);
        if (ret >= 0 && response.net_status == LEONOS_NET_STATUS_OK) {
            break;
        }
        if (!is_retriable_error(ret, response.net_status)) {
            break;
        }
        ++retries;
        if (retries > BROWSER_HTTP_MAX_RETRIES) {
            break;
        }
        set_status(T("Retrying...", "正在重试..."));
    }
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
    browser_decode_page_source(response.body_len);
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
    browser_decode_page_source(len);
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
    if (!url || (!starts_with_ignore_case(url, "http://") &&
                 !starts_with_ignore_case(url, "https://"))) {
        return 0;
    }
    return ends_with_ignore_case(url, ".bmp") ||
           ends_with_ignore_case(url, ".png") ||
           ends_with_ignore_case(url, ".jpg") ||
           ends_with_ignore_case(url, ".jpeg") ||
           ends_with_ignore_case(url, ".gif") ||
            ends_with_ignore_case(url, ".zip") ||
            ends_with_ignore_case(url, ".bin") ||
            ends_with_ignore_case(url, ".api") ||
            ends_with_ignore_case(url, ".elf") ||
           ends_with_ignore_case(url, ".iso") ||
           ends_with_ignore_case(url, ".vmdk") ||
           ends_with_ignore_case(url, ".pdf") ||
           ends_with_ignore_case(url, ".7z") ||
           ends_with_ignore_case(url, ".tar") ||
           ends_with_ignore_case(url, ".gz");
}

void browser_start_download(const char *url)
{
    char target[LEONOS_HTTP_URL_LEN];
    char *argv[3];
    copy_text(target, sizeof(target), url);
    argv[0] = "0:/programs/downloadmgr/downloadmgr.elf";
    argv[1] = target;
    argv[2] = 0;
    if (leonos_launch_argv(argv) < 0) {
        set_status(T("Could not start Download Manager", "无法启动下载管理器"));
    } else {
        set_status(T("Download started", "下载已开始"));
    }
}

static void browser_start_api_install(const char *url)
{
    char target[LEONOS_HTTP_URL_LEN];
    char *argv[3];
    copy_text(target, sizeof(target), url);
    argv[0] = "0:/system/apps/apiapp/apiapp.elf";
    argv[1] = target;
    argv[2] = 0;
    if (leonos_launch_argv(argv) < 0) {
        set_status(T("Could not start API Installer", "无法启动 API 安装程序"));
    } else {
        set_status(T("Application download started", "应用下载已开始"));
    }
}

void navigate_to(const char *input, uint8_t add_to_history)
{
    char url[BROWSER_URL_CAP];
    normalize_location(input, url, sizeof(url));
    if (starts_with_ignore_case(url, "about:")) {
        load_about();
    } else if (starts_with_ignore_case(url, "http://") ||
               starts_with_ignore_case(url, "https://")) {
        if (browser_should_download_http_url(url)) {
            if (ends_with_ignore_case(url, ".api")) {
                browser_start_api_install(url);
            } else {
                browser_start_download(url);
            }
            return;
        } else {
            load_http_url(url);
        }
    } else if (is_drive_path(url)) {
        load_local_file(url);
    } else {
        render_message_page(T("Unsupported Address", "不支持的地址"),
                            T("Use http://, https://, about:, or a LeonOS file path such as 0:/file.html.",
                              "请使用 http://、https://、about:，或类似 0:/file.html 的 LeonOS 文件路径。"),
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
