#include "browser.h"

void render_html_source(const char *source, const char *base_url)
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
    litehtml_core_render_plain(&view, source);
    clamp_scroll();
}

void rerender_page(void)
{
    if (page_is_html) {
        render_html_source(page_source, current_location);
    } else {
        render_plain_source(page_source);
    }
}

void set_page_source(const char *title, const char *source,
                            uint8_t is_html, const char *status)
{
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

uint32_t response_body_offset(const char *data, uint32_t len)
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

void load_http_url(const char *url)
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
    ret = leonos_net_http_get(parsed.host, parsed.path, parsed.port, 10000, &http_result);
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

void load_local_file(const char *path)
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
