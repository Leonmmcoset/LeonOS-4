#include "litehtml_core.h"

#include <leonos/i18n.h>
#include <leonos/stdio.h>

#define T(en, zh) leonos_i18n((en), (zh))
#define CORE_CSS_RULE_MAX 48U
#define CORE_CSS_SELECTOR_MAX 32U
#define CORE_CSS_BLOCK_MAX 2048U
#define CORE_STYLE_STACK_MAX 32U

struct core_http_url {
    char host[96];
    char path[LEONOS_FS_PATH_LEN];
    uint32_t port;
    uint8_t secure;
};

struct core_css_style {
    uint8_t add_style;
    uint8_t clear_style;
    uint8_t has_fg;
    uint8_t has_bg;
    uint8_t has_border;
    uint8_t has_align;
    uint8_t has_indent;
    uint32_t fg;
    uint32_t bg;
    uint32_t border;
    uint8_t align;
    uint8_t indent;
};

struct core_css_rule {
    char tag[16];
    char class_name[32];
    char id[32];
    struct core_css_style style;
};

struct core_style_state {
    uint8_t text_style;
    uint8_t css_indent;
    uint8_t css_align;
    uint8_t table_cell_align;
    uint32_t text_fg;
    uint32_t text_bg;
    uint32_t border_color;
};

struct core_render_ctx {
    struct litehtml_core_view *view;
    uint8_t kind;
    uint8_t indent;
    uint8_t pending_space;
    uint8_t in_link;
    uint8_t link_id;
    uint8_t in_title;
    uint8_t skip_content;
    uint8_t text_style;
    uint8_t blockquote_depth;
    uint8_t list_depth;
    uint8_t table_depth;
    uint8_t in_table_row;
    uint8_t table_cell_count;
    uint8_t table_cell_align;
    uint8_t css_indent;
    uint8_t css_align;
    uint8_t style_depth;
    uint8_t capture_style;
    uint32_t text_fg;
    uint32_t text_bg;
    uint32_t border_color;
    uint32_t style_text_pos;
    char style_text[CORE_CSS_BLOCK_MAX];
    uint32_t css_rule_count;
    struct core_css_rule css_rules[CORE_CSS_RULE_MAX];
    struct core_style_state style_stack[CORE_STYLE_STACK_MAX];
};

static char core_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int core_text_eq(const char *a, const char *b)
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

static int core_starts_with_ignore_case(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (core_tolower(text[i]) != core_tolower(prefix[i])) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int core_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int core_utf8_cont(uint8_t byte)
{
    return (byte & 0xc0u) == 0x80u;
}

static int core_is_wide_codepoint(uint32_t cp)
{
    return (cp >= 0x1100u && cp <= 0x115fu) ||
           cp == 0x2329u || cp == 0x232au ||
           (cp >= 0x2e80u && cp <= 0xa4cfu) ||
           (cp >= 0xac00u && cp <= 0xd7a3u) ||
           (cp >= 0xf900u && cp <= 0xfaffu) ||
           (cp >= 0x20000u && cp <= 0x3fffdu) ||
           (cp >= 0xfe10u && cp <= 0xfe19u) ||
           (cp >= 0xfe30u && cp <= 0xfe6fu) ||
           (cp >= 0xff00u && cp <= 0xff60u) ||
           (cp >= 0xffe0u && cp <= 0xffe6u);
}

static uint32_t core_codepoint_cells(uint32_t cp)
{
    if (cp == 0 || cp == '\n' || cp == '\r') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    return core_is_wide_codepoint(cp) ? 2U : 1U;
}

static int core_codepoint_is_space(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

static uint32_t core_decode_utf8_at(const char *text, uint32_t pos,
                                    uint32_t *byte_len, uint32_t *cells)
{
    const uint8_t *s = (const uint8_t *)text;
    uint8_t b0;
    uint32_t cp;
    uint32_t len = 1;
    if (!text || !text[pos]) {
        if (byte_len) {
            *byte_len = 0;
        }
        if (cells) {
            *cells = 0;
        }
        return 0;
    }
    b0 = s[pos];
    if (b0 < 0x80u) {
        cp = b0;
    } else if ((b0 & 0xe0u) == 0xc0u &&
               s[pos + 1U] && core_utf8_cont(s[pos + 1U])) {
        cp = ((uint32_t)(b0 & 0x1fu) << 6) |
             (uint32_t)(s[pos + 1U] & 0x3fu);
        len = 2;
    } else if ((b0 & 0xf0u) == 0xe0u &&
               s[pos + 1U] && s[pos + 2U] &&
               core_utf8_cont(s[pos + 1U]) &&
               core_utf8_cont(s[pos + 2U])) {
        cp = ((uint32_t)(b0 & 0x0fu) << 12) |
             ((uint32_t)(s[pos + 1U] & 0x3fu) << 6) |
             (uint32_t)(s[pos + 2U] & 0x3fu);
        len = 3;
    } else if ((b0 & 0xf8u) == 0xf0u &&
               s[pos + 1U] && s[pos + 2U] && s[pos + 3U] &&
               core_utf8_cont(s[pos + 1U]) &&
               core_utf8_cont(s[pos + 2U]) &&
               core_utf8_cont(s[pos + 3U])) {
        cp = ((uint32_t)(b0 & 0x07u) << 18) |
             ((uint32_t)(s[pos + 1U] & 0x3fu) << 12) |
             ((uint32_t)(s[pos + 2U] & 0x3fu) << 6) |
             (uint32_t)(s[pos + 3U] & 0x3fu);
        len = 4;
    } else {
        cp = 0xfffdu;
    }
    if (byte_len) {
        *byte_len = len;
    }
    if (cells) {
        *cells = core_codepoint_cells(cp);
    }
    return cp;
}

static int core_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int core_is_hex_digit(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static uint32_t core_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (uint32_t)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (uint32_t)(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (uint32_t)(ch - 'A' + 10);
    }
    return 0;
}

static int core_is_drive_path(const char *text)
{
    return text && text[0] && text[1] == ':' && text[2] == '/';
}

static void core_copy_text(char *dst, uint32_t cap, const char *src)
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

static void core_append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void core_append_bytes(char *dst, uint32_t *pos, uint32_t cap,
                              const char *src, uint32_t len)
{
    for (uint32_t i = 0; src && i < len; ++i) {
        core_append_char(dst, pos, cap, src[i]);
    }
}

static void core_append_text(char *dst, uint32_t *pos, uint32_t cap,
                             const char *src)
{
    while (src && *src) {
        core_append_char(dst, pos, cap, *src++);
    }
}

static void core_append_u32(char *dst, uint32_t *pos, uint32_t cap,
                            uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        core_append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        core_append_char(dst, pos, cap, tmp[--n]);
    }
}

static void core_set_truncated(struct litehtml_core_view *view)
{
    if (view && view->source_truncated) {
        *view->source_truncated = 1;
    }
}

static uint32_t core_line_count(const struct litehtml_core_view *view)
{
    return view && view->line_count ? *view->line_count : 0;
}

static void core_set_line_count(struct litehtml_core_view *view, uint32_t count)
{
    if (view && view->line_count) {
        *view->line_count = count;
    }
}

static void core_clear_line(struct litehtml_core_view *view, uint32_t index)
{
    uint32_t cap;
    if (!view || !view->lines || index >= view->max_lines) {
        return;
    }
    cap = view->line_chars;
    if (cap == 0 || cap > sizeof(view->lines[index].text)) {
        cap = (uint32_t)sizeof(view->lines[index].text);
    }
    view->lines[index].text[0] = 0;
    view->lines[index].len = 0;
    view->lines[index].cells = 0;
    view->lines[index].kind = BROWSER_LINE_NORMAL;
    view->lines[index].indent = 0;
    view->lines[index].align = BROWSER_ALIGN_LEFT;
    view->lines[index].line_bg = BROWSER_COLOR_UNSET;
    view->lines[index].border_color = BROWSER_COLOR_UNSET;
    for (uint32_t i = 0; i < cap; ++i) {
        view->lines[index].link[i] = 0;
        view->lines[index].style[i] = 0;
        view->lines[index].fg[i] = BROWSER_COLOR_UNSET;
        view->lines[index].bg[i] = BROWSER_COLOR_UNSET;
        view->lines[index].cell_width[i] = 0;
        view->lines[index].cell_align[i] = BROWSER_ALIGN_LEFT;
    }
}

static void core_reset_document(struct litehtml_core_view *view)
{
    if (!view) {
        return;
    }
    if (view->link_count) {
        *view->link_count = 0;
    }
    core_set_line_count(view, 1);
    if (view->scroll_line) {
        *view->scroll_line = 0;
    }
    if (view->source_truncated) {
        *view->source_truncated = 0;
    }
    for (uint32_t i = 0; i < view->max_lines; ++i) {
        core_clear_line(view, i);
    }
}

static void core_render_newline(struct core_render_ctx *ctx, uint8_t force)
{
    struct litehtml_core_view *view;
    uint32_t count;
    if (!ctx || !ctx->view) {
        return;
    }
    ctx->pending_space = 0;
    view = ctx->view;
    count = core_line_count(view);
    if (count == 0) {
        core_set_line_count(view, 1);
        core_clear_line(view, 0);
        count = 1;
    }
    if (!force && view->lines[count - 1U].len == 0) {
        return;
    }
    if (count >= view->max_lines) {
        core_set_truncated(view);
        return;
    }
    core_clear_line(view, count);
    view->lines[count].kind = ctx->kind;
    view->lines[count].indent = ctx->indent;
    view->lines[count].align = ctx->css_align;
    view->lines[count].line_bg = ctx->text_bg;
    view->lines[count].border_color = ctx->border_color;
    core_set_line_count(view, count + 1U);
}

static uint32_t core_current_cols(const struct core_render_ctx *ctx,
                                  const struct browser_line *line)
{
    uint32_t cols;
    uint32_t indent;
    if (!ctx || !ctx->view) {
        return 1;
    }
    cols = ctx->view->cols;
    indent = line ? line->indent : ctx->indent;
    if (cols <= indent + 4U) {
        return cols > 4U ? cols - 4U : 1U;
    }
    return cols - indent;
}

static void core_apply_current_style(struct core_render_ctx *ctx,
                                     struct browser_line *line)
{
    if (!ctx || !line) {
        return;
    }
    if (line->len == 0 && line->kind != BROWSER_LINE_HR) {
        line->kind = ctx->kind;
        line->indent = ctx->indent;
        line->align = ctx->css_align;
        line->line_bg = ctx->text_bg;
        line->border_color = ctx->border_color;
    }
}

static void core_emit_empty_line(struct core_render_ctx *ctx, uint8_t kind)
{
    struct litehtml_core_view *view;
    struct browser_line *line;
    uint32_t count;
    if (!ctx || !ctx->view) {
        return;
    }
    view = ctx->view;
    count = core_line_count(view);
    if (count == 0) {
        core_set_line_count(view, 1);
        core_clear_line(view, 0);
        count = 1;
    }
    if (view->lines[count - 1U].len != 0 ||
        view->lines[count - 1U].kind == BROWSER_LINE_HR) {
        core_render_newline(ctx, 1);
        count = core_line_count(view);
    }
    if (count == 0 || count > view->max_lines) {
        core_set_truncated(view);
        return;
    }
    line = &view->lines[count - 1U];
    line->kind = kind;
    line->indent = ctx->indent;
    line->align = ctx->css_align;
    line->line_bg = ctx->text_bg;
    line->border_color = ctx->border_color;
    line->len = 0;
    line->cells = 0;
    line->text[0] = 0;
    core_render_newline(ctx, 1);
}

static uint8_t core_add_link(struct litehtml_core_view *view, const char *href)
{
    uint32_t index;
    if (!view || !view->links || !view->link_count || !href || !href[0] ||
        *view->link_count >= view->max_links) {
        return 0xffU;
    }
    index = *view->link_count;
    core_copy_text(view->links[index].href, sizeof(view->links[index].href),
                   href);
    *view->link_count = index + 1U;
    return (uint8_t)index;
}

static void core_render_raw_codepoint(struct core_render_ctx *ctx,
                                      const char *bytes,
                                      uint32_t byte_len,
                                      uint32_t cells,
                                      uint32_t codepoint)
{
    struct litehtml_core_view *view;
    struct browser_line *line;
    uint32_t count;
    uint32_t cap;
    uint8_t link_id = 0;
    if (!ctx || !ctx->view || !bytes || byte_len == 0) {
        return;
    }
    view = ctx->view;
    cap = view->line_chars;
    if (cap == 0 || cap > sizeof(view->lines[0].text)) {
        cap = (uint32_t)sizeof(view->lines[0].text);
    }
    if (codepoint == '\r') {
        return;
    }
    if (codepoint == '\n') {
        core_render_newline(ctx, 1);
        return;
    }
    if (codepoint == '\t') {
        core_render_raw_codepoint(ctx, " ", 1, 1, ' ');
        core_render_raw_codepoint(ctx, " ", 1, 1, ' ');
        return;
    }
    count = core_line_count(view);
    if (count == 0) {
        core_set_line_count(view, 1);
        core_clear_line(view, 0);
        count = 1;
    }
    line = &view->lines[count - 1U];
    core_apply_current_style(ctx, line);
    if (line->cells + cells > core_current_cols(ctx, line) ||
        line->len + byte_len >= cap) {
        core_render_newline(ctx, 1);
        count = core_line_count(view);
        line = &view->lines[count - 1U];
        core_apply_current_style(ctx, line);
    }
    if (line->cells + cells > core_current_cols(ctx, line) ||
        line->len + byte_len >= cap) {
        core_set_truncated(view);
        return;
    }
    if (ctx->in_link) {
        link_id = (uint8_t)(ctx->link_id + 1U);
    }
    line->kind = ctx->kind;
    for (uint32_t i = 0; i < byte_len; ++i) {
        uint32_t dst = line->len + i;
        line->text[dst] = codepoint >= 32 || byte_len > 1U
                              ? bytes[i]
                              : ' ';
        line->link[dst] = link_id;
        line->style[dst] = ctx->text_style;
        line->fg[dst] = ctx->text_fg;
        line->bg[dst] = ctx->text_bg;
        line->cell_width[dst] = i == 0 ? (uint8_t)cells : 0;
        line->cell_align[dst] = ctx->table_depth
                                    ? ctx->table_cell_align
                                    : ctx->css_align;
    }
    line->len += byte_len;
    line->cells += cells;
    line->text[line->len] = 0;
}

static void core_render_html_codepoint(struct core_render_ctx *ctx,
                                       const char *bytes,
                                       uint32_t byte_len,
                                       uint32_t cells,
                                       uint32_t codepoint)
{
    uint32_t count;
    if (!ctx || !ctx->view) {
        return;
    }
    if (core_codepoint_is_space(codepoint)) {
        ctx->pending_space = 1;
        return;
    }
    count = core_line_count(ctx->view);
    if (ctx->pending_space && count && ctx->view->lines[count - 1U].len > 0) {
        core_render_raw_codepoint(ctx, " ", 1, 1, ' ');
    }
    ctx->pending_space = 0;
    core_render_raw_codepoint(ctx, bytes, byte_len, cells, codepoint);
}

static void core_render_html_char(struct core_render_ctx *ctx, char ch)
{
    char bytes[1];
    bytes[0] = ch;
    core_render_html_codepoint(ctx, bytes, 1,
                               core_codepoint_cells((uint8_t)ch),
                               (uint8_t)ch);
}

static int core_tag_name_eq(const char *tag, const char *name);
static void core_extract_attr(const char *tag, const char *attr,
                              char *dst, uint32_t cap);

static uint8_t core_base_indent(const struct core_render_ctx *ctx)
{
    uint32_t indent = 0;
    if (!ctx) {
        return 0;
    }
    indent += (uint32_t)ctx->blockquote_depth * 3U;
    if (ctx->table_depth) {
        indent += 1U;
    }
    indent += ctx->css_indent;
    return indent > 28U ? 28U : (uint8_t)indent;
}

static void core_restore_flow_style(struct core_render_ctx *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->indent = core_base_indent(ctx);
    if (ctx->table_depth) {
        ctx->kind = BROWSER_LINE_TABLE;
    } else if (ctx->blockquote_depth) {
        ctx->kind = BROWSER_LINE_BLOCKQUOTE;
    } else {
        ctx->kind = BROWSER_LINE_NORMAL;
    }
}

static void core_render_literal(struct core_render_ctx *ctx, const char *text)
{
    uint32_t pos = 0;
    if (ctx) {
        ctx->pending_space = 0;
    }
    while (text && text[pos]) {
        uint32_t byte_len = 1;
        uint32_t cells = 1;
        uint32_t cp = core_decode_utf8_at(text, pos, &byte_len, &cells);
        core_render_raw_codepoint(ctx, text + pos, byte_len, cells, cp);
        pos += byte_len;
    }
    if (ctx) {
        ctx->pending_space = 0;
    }
}

static uint8_t core_tag_heading_kind(const char *tag)
{
    if (core_tag_name_eq(tag, "h1")) {
        return BROWSER_LINE_HEADING1;
    }
    if (core_tag_name_eq(tag, "h2")) {
        return BROWSER_LINE_HEADING2;
    }
    if (core_tag_name_eq(tag, "h3") ||
        core_tag_name_eq(tag, "h4") ||
        core_tag_name_eq(tag, "h5") ||
        core_tag_name_eq(tag, "h6")) {
        return BROWSER_LINE_HEADING3;
    }
    return 0;
}

static int core_tag_is_block(const char *tag)
{
    return core_tag_name_eq(tag, "p") ||
           core_tag_name_eq(tag, "div") ||
           core_tag_name_eq(tag, "section") ||
           core_tag_name_eq(tag, "article") ||
           core_tag_name_eq(tag, "header") ||
           core_tag_name_eq(tag, "footer") ||
           core_tag_name_eq(tag, "main") ||
           core_tag_name_eq(tag, "nav");
}

static int core_text_eq_ignore_case(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && core_tolower(a[i]) == core_tolower(b[i])) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static void core_trim_copy(char *dst, uint32_t cap, const char *src,
                           uint32_t len)
{
    uint32_t start = 0;
    uint32_t end = len;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (start < len && core_is_space(src[start])) {
        ++start;
    }
    while (end > start && core_is_space(src[end - 1U])) {
        --end;
    }
    while (start < end && pos + 1U < cap) {
        dst[pos++] = src[start++];
    }
    dst[pos] = 0;
}

static void core_lower_ascii(char *text)
{
    uint32_t i = 0;
    while (text && text[i]) {
        text[i] = core_tolower(text[i]);
        ++i;
    }
}

static uint32_t core_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    if (r > 255U) {
        r = 255U;
    }
    if (g > 255U) {
        g = 255U;
    }
    if (b > 255U) {
        b = 255U;
    }
    return (r << 16) | (g << 8) | b;
}

static int core_parse_css_color(const char *value, uint32_t *out)
{
    char tmp[64];
    uint32_t len;
    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    uint32_t i = 0;
    if (!value || !out) {
        return 0;
    }
    len = (uint32_t)strlen(value);
    core_trim_copy(tmp, sizeof(tmp), value, len);
    core_lower_ascii(tmp);
    if (tmp[0] == '#') {
        if (core_is_hex_digit(tmp[1]) && core_is_hex_digit(tmp[2]) &&
            core_is_hex_digit(tmp[3]) && tmp[4] == 0) {
            r = core_hex_value(tmp[1]) * 17U;
            g = core_hex_value(tmp[2]) * 17U;
            b = core_hex_value(tmp[3]) * 17U;
            *out = core_rgb(r, g, b);
            return 1;
        }
        if (core_is_hex_digit(tmp[1]) && core_is_hex_digit(tmp[2]) &&
            core_is_hex_digit(tmp[3]) && core_is_hex_digit(tmp[4]) &&
            core_is_hex_digit(tmp[5]) && core_is_hex_digit(tmp[6]) &&
            tmp[7] == 0) {
            r = core_hex_value(tmp[1]) * 16U + core_hex_value(tmp[2]);
            g = core_hex_value(tmp[3]) * 16U + core_hex_value(tmp[4]);
            b = core_hex_value(tmp[5]) * 16U + core_hex_value(tmp[6]);
            *out = core_rgb(r, g, b);
            return 1;
        }
        return 0;
    }
    if (core_starts_with_ignore_case(tmp, "rgb(")) {
        uint32_t values[3] = {0, 0, 0};
        uint32_t index = 0;
        i = 4;
        while (tmp[i] && tmp[i] != ')' && index < 3U) {
            while (core_is_space(tmp[i]) || tmp[i] == ',') {
                ++i;
            }
            values[index] = 0;
            while (core_is_digit(tmp[i])) {
                values[index] = values[index] * 10U + (uint32_t)(tmp[i] - '0');
                ++i;
            }
            ++index;
            while (core_is_space(tmp[i])) {
                ++i;
            }
            if (tmp[i] == ',') {
                ++i;
            }
        }
        if (index == 3U) {
            *out = core_rgb(values[0], values[1], values[2]);
            return 1;
        }
        return 0;
    }
    if (core_text_eq_ignore_case(tmp, "black")) {
        *out = 0x00000000U;
    } else if (core_text_eq_ignore_case(tmp, "white")) {
        *out = 0x00ffffffU;
    } else if (core_text_eq_ignore_case(tmp, "red")) {
        *out = 0x00cc0000U;
    } else if (core_text_eq_ignore_case(tmp, "green")) {
        *out = 0x00008000U;
    } else if (core_text_eq_ignore_case(tmp, "blue")) {
        *out = 0x000000ccU;
    } else if (core_text_eq_ignore_case(tmp, "navy")) {
        *out = 0x00000080U;
    } else if (core_text_eq_ignore_case(tmp, "gray") ||
               core_text_eq_ignore_case(tmp, "grey")) {
        *out = 0x00808080U;
    } else if (core_text_eq_ignore_case(tmp, "silver")) {
        *out = 0x00c0c0c0U;
    } else if (core_text_eq_ignore_case(tmp, "yellow")) {
        *out = 0x00ffff00U;
    } else if (core_text_eq_ignore_case(tmp, "cyan")) {
        *out = 0x0000ffffU;
    } else if (core_text_eq_ignore_case(tmp, "magenta")) {
        *out = 0x00ff00ffU;
    } else if (core_text_eq_ignore_case(tmp, "purple")) {
        *out = 0x00800080U;
    } else if (core_text_eq_ignore_case(tmp, "teal")) {
        *out = 0x00008080U;
    } else if (core_text_eq_ignore_case(tmp, "orange")) {
        *out = 0x00ff9900U;
    } else if (core_text_eq_ignore_case(tmp, "maroon")) {
        *out = 0x00800000U;
    } else {
        return 0;
    }
    return 1;
}

static int core_parse_css_color_token(const char *value, uint32_t *out)
{
    uint32_t i = 0;
    if (!value || !out) {
        return 0;
    }
    while (value[i]) {
        char token[64];
        uint32_t pos = 0;
        while (value[i] && (core_is_space(value[i]) || value[i] == ',')) {
            ++i;
        }
        if (!value[i]) {
            break;
        }
        while (value[i] && !core_is_space(value[i]) && value[i] != ',' &&
               value[i] != ')' && pos + 1U < sizeof(token)) {
            token[pos++] = value[i++];
        }
        if (value[i] == ')' && pos + 1U < sizeof(token)) {
            token[pos++] = value[i++];
        }
        token[pos] = 0;
        if (core_parse_css_color(token, out)) {
            return 1;
        }
        while (value[i] && !core_is_space(value[i]) && value[i] != ',') {
            ++i;
        }
    }
    return 0;
}

static uint8_t core_parse_css_length_cells(const char *value)
{
    uint32_t i = 0;
    uint32_t number = 0;
    char tmp[32];
    if (!value) {
        return 0;
    }
    core_trim_copy(tmp, sizeof(tmp), value, (uint32_t)strlen(value));
    while (core_is_space(tmp[i])) {
        ++i;
    }
    while (core_is_digit(tmp[i])) {
        number = number * 10U + (uint32_t)(tmp[i] - '0');
        ++i;
    }
    if (number == 0) {
        return 0;
    }
    if (core_starts_with_ignore_case(tmp + i, "em")) {
        number *= 2U;
    } else if (core_starts_with_ignore_case(tmp + i, "px")) {
        number = (number + 7U) / 8U;
    }
    if (number > 28U) {
        number = 28U;
    }
    return (uint8_t)number;
}

static void core_css_style_init(struct core_css_style *style)
{
    if (!style) {
        return;
    }
    style->add_style = 0;
    style->clear_style = 0;
    style->has_fg = 0;
    style->has_bg = 0;
    style->has_border = 0;
    style->has_align = 0;
    style->has_indent = 0;
    style->fg = BROWSER_COLOR_UNSET;
    style->bg = BROWSER_COLOR_UNSET;
    style->border = BROWSER_COLOR_UNSET;
    style->align = BROWSER_ALIGN_LEFT;
    style->indent = 0;
}

static void core_parse_css_declarations(const char *decl,
                                        struct core_css_style *style)
{
    uint32_t i = 0;
    if (!decl || !style) {
        return;
    }
    while (decl[i]) {
        char prop[48];
        char value[96];
        uint32_t prop_start;
        uint32_t value_start;
        uint32_t color;
        while (decl[i] && (core_is_space(decl[i]) || decl[i] == ';')) {
            ++i;
        }
        prop_start = i;
        while (decl[i] && decl[i] != ':' && decl[i] != ';') {
            ++i;
        }
        if (decl[i] != ':') {
            while (decl[i] && decl[i] != ';') {
                ++i;
            }
            continue;
        }
        core_trim_copy(prop, sizeof(prop), decl + prop_start, i - prop_start);
        core_lower_ascii(prop);
        ++i;
        value_start = i;
        while (decl[i] && decl[i] != ';') {
            ++i;
        }
        core_trim_copy(value, sizeof(value), decl + value_start, i - value_start);
        if (core_text_eq_ignore_case(prop, "color")) {
            if (core_parse_css_color(value, &color)) {
                style->has_fg = 1;
                style->fg = color;
            }
        } else if (core_text_eq_ignore_case(prop, "background") ||
                   core_text_eq_ignore_case(prop, "background-color")) {
            if (core_parse_css_color(value, &color)) {
                style->has_bg = 1;
                style->bg = color;
            }
        } else if (core_text_eq_ignore_case(prop, "border-color") ||
                   core_text_eq_ignore_case(prop, "border-left-color")) {
            if (core_parse_css_color(value, &color)) {
                style->has_border = 1;
                style->border = color;
            }
        } else if (core_text_eq_ignore_case(prop, "border") ||
                   core_text_eq_ignore_case(prop, "border-left")) {
            if (core_parse_css_color_token(value, &color)) {
                style->has_border = 1;
                style->border = color;
            } else {
                style->has_border = 1;
                style->border = 0x00808080U;
            }
        } else if (core_text_eq_ignore_case(prop, "font-weight")) {
            if (core_starts_with_ignore_case(value, "bold") ||
                core_starts_with_ignore_case(value, "bolder") ||
                value[0] == '6' || value[0] == '7' ||
                value[0] == '8' || value[0] == '9') {
                style->add_style |= BROWSER_TEXT_BOLD;
            } else if (core_starts_with_ignore_case(value, "normal") ||
                       value[0] == '4') {
                style->clear_style |= BROWSER_TEXT_BOLD;
            }
        } else if (core_text_eq_ignore_case(prop, "font-style")) {
            if (core_starts_with_ignore_case(value, "italic") ||
                core_starts_with_ignore_case(value, "oblique")) {
                style->add_style |= BROWSER_TEXT_ITALIC;
            } else if (core_starts_with_ignore_case(value, "normal")) {
                style->clear_style |= BROWSER_TEXT_ITALIC;
            }
        } else if (core_text_eq_ignore_case(prop, "text-decoration")) {
            if (core_starts_with_ignore_case(value, "underline")) {
                style->add_style |= BROWSER_TEXT_UNDERLINE;
            } else if (core_starts_with_ignore_case(value, "none")) {
                style->clear_style |= BROWSER_TEXT_UNDERLINE;
            }
        } else if (core_text_eq_ignore_case(prop, "text-align")) {
            style->has_align = 1;
            if (core_starts_with_ignore_case(value, "center")) {
                style->align = BROWSER_ALIGN_CENTER;
            } else if (core_starts_with_ignore_case(value, "right")) {
                style->align = BROWSER_ALIGN_RIGHT;
            } else {
                style->align = BROWSER_ALIGN_LEFT;
            }
        } else if (core_text_eq_ignore_case(prop, "margin-left") ||
                   core_text_eq_ignore_case(prop, "padding-left") ||
                   core_text_eq_ignore_case(prop, "text-indent")) {
            style->has_indent = 1;
            style->indent = core_parse_css_length_cells(value);
        }
        if (decl[i] == ';') {
            ++i;
        }
    }
}

static uint8_t core_parse_align_value(const char *value, uint8_t fallback)
{
    if (core_starts_with_ignore_case(value, "center") ||
        core_starts_with_ignore_case(value, "middle")) {
        return BROWSER_ALIGN_CENTER;
    }
    if (core_starts_with_ignore_case(value, "right") ||
        core_starts_with_ignore_case(value, "end")) {
        return BROWSER_ALIGN_RIGHT;
    }
    if (core_starts_with_ignore_case(value, "left") ||
        core_starts_with_ignore_case(value, "start")) {
        return BROWSER_ALIGN_LEFT;
    }
    return fallback;
}

static void core_parse_css_selector(struct core_css_rule *rule,
                                    const char *selector)
{
    uint32_t i = 0;
    uint32_t pos = 0;
    char sel[CORE_CSS_SELECTOR_MAX];
    if (!rule || !selector) {
        return;
    }
    rule->tag[0] = 0;
    rule->class_name[0] = 0;
    rule->id[0] = 0;
    core_trim_copy(sel, sizeof(sel), selector, (uint32_t)strlen(selector));
    core_lower_ascii(sel);
    while (sel[i] && sel[i] != '.' && sel[i] != '#' &&
           !core_is_space(sel[i]) && pos + 1U < sizeof(rule->tag)) {
        rule->tag[pos++] = sel[i++];
    }
    rule->tag[pos] = 0;
    while (sel[i]) {
        char marker = sel[i];
        char *out = 0;
        uint32_t cap = 0;
        if (marker != '.' && marker != '#') {
            ++i;
            continue;
        }
        ++i;
        out = marker == '.' ? rule->class_name : rule->id;
        cap = marker == '.' ? (uint32_t)sizeof(rule->class_name)
                            : (uint32_t)sizeof(rule->id);
        pos = 0;
        while (sel[i] && sel[i] != '.' && sel[i] != '#' &&
               !core_is_space(sel[i]) && pos + 1U < cap) {
            out[pos++] = sel[i++];
        }
        out[pos] = 0;
    }
}

static void core_add_css_rule(struct core_render_ctx *ctx,
                              const char *selector,
                              const char *decl)
{
    struct core_css_rule *rule;
    if (!ctx || !selector || !decl || ctx->css_rule_count >= CORE_CSS_RULE_MAX) {
        return;
    }
    rule = &ctx->css_rules[ctx->css_rule_count];
    core_css_style_init(&rule->style);
    core_parse_css_selector(rule, selector);
    if (!rule->tag[0] && !rule->class_name[0] && !rule->id[0]) {
        return;
    }
    core_parse_css_declarations(decl, &rule->style);
    ++ctx->css_rule_count;
}

static void core_parse_css_rules(struct core_render_ctx *ctx, const char *css)
{
    uint32_t i = 0;
    if (!ctx || !css) {
        return;
    }
    while (css[i]) {
        char selectors[128];
        char decl[256];
        uint32_t selector_start;
        uint32_t decl_start;
        while (css[i] && core_is_space(css[i])) {
            ++i;
        }
        selector_start = i;
        while (css[i] && css[i] != '{') {
            ++i;
        }
        if (css[i] != '{') {
            break;
        }
        core_trim_copy(selectors, sizeof(selectors), css + selector_start, i - selector_start);
        ++i;
        decl_start = i;
        while (css[i] && css[i] != '}') {
            ++i;
        }
        core_trim_copy(decl, sizeof(decl), css + decl_start, i - decl_start);
        if (css[i] == '}') {
            ++i;
        }
        {
            uint32_t s = 0;
            while (selectors[s]) {
                char selector[CORE_CSS_SELECTOR_MAX];
                uint32_t start = s;
                while (selectors[s] && selectors[s] != ',') {
                    ++s;
                }
                core_trim_copy(selector, sizeof(selector), selectors + start, s - start);
                core_add_css_rule(ctx, selector, decl);
                if (selectors[s] == ',') {
                    ++s;
                }
            }
        }
    }
}

static int core_class_attr_has(const char *class_attr, const char *class_name)
{
    uint32_t i = 0;
    uint32_t wanted;
    if (!class_attr || !class_name || !class_name[0]) {
        return 0;
    }
    wanted = (uint32_t)strlen(class_name);
    while (class_attr[i]) {
        while (core_is_space(class_attr[i])) {
            ++i;
        }
        if (!class_attr[i]) {
            break;
        }
        {
            uint32_t start = i;
            uint32_t len;
            while (class_attr[i] && !core_is_space(class_attr[i])) {
                ++i;
            }
            len = i - start;
            if (len == wanted) {
                uint32_t n = 0;
                while (n < len &&
                       core_tolower(class_attr[start + n]) == class_name[n]) {
                    ++n;
                }
                if (n == len) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int core_css_rule_matches(const struct core_css_rule *rule,
                                 const char *tag, const char *class_attr,
                                 const char *id_attr)
{
    if (!rule || !tag) {
        return 0;
    }
    if (rule->tag[0] && !core_text_eq_ignore_case(rule->tag, tag)) {
        return 0;
    }
    if (rule->id[0] && !core_text_eq_ignore_case(rule->id, id_attr)) {
        return 0;
    }
    if (rule->class_name[0] &&
        !core_class_attr_has(class_attr, rule->class_name)) {
        return 0;
    }
    return 1;
}

static void core_apply_css_style(struct core_render_ctx *ctx,
                                 const struct core_css_style *style)
{
    if (!ctx || !style) {
        return;
    }
    ctx->text_style &= (uint8_t)~style->clear_style;
    ctx->text_style |= style->add_style;
    if (style->has_fg) {
        ctx->text_fg = style->fg;
    }
    if (style->has_bg) {
        ctx->text_bg = style->bg;
    }
    if (style->has_border) {
        ctx->border_color = style->border;
    }
    if (style->has_align) {
        ctx->css_align = style->align;
    }
    if (style->has_indent) {
        ctx->css_indent = style->indent;
        ctx->indent = core_base_indent(ctx);
    }
}

static void core_push_style(struct core_render_ctx *ctx)
{
    struct core_style_state *state;
    if (!ctx || ctx->style_depth >= CORE_STYLE_STACK_MAX) {
        return;
    }
    state = &ctx->style_stack[ctx->style_depth++];
    state->text_style = ctx->text_style;
    state->css_indent = ctx->css_indent;
    state->css_align = ctx->css_align;
    state->table_cell_align = ctx->table_cell_align;
    state->text_fg = ctx->text_fg;
    state->text_bg = ctx->text_bg;
    state->border_color = ctx->border_color;
}

static void core_pop_style(struct core_render_ctx *ctx)
{
    struct core_style_state *state;
    if (!ctx || ctx->style_depth == 0) {
        return;
    }
    state = &ctx->style_stack[--ctx->style_depth];
    ctx->text_style = state->text_style;
    ctx->css_indent = state->css_indent;
    ctx->css_align = state->css_align;
    ctx->table_cell_align = state->table_cell_align;
    ctx->text_fg = state->text_fg;
    ctx->text_bg = state->text_bg;
    ctx->border_color = state->border_color;
    ctx->indent = core_base_indent(ctx);
}

static void core_apply_element_css(struct core_render_ctx *ctx,
                                   const char *tag, const char *tag_text)
{
    char class_attr[64];
    char id_attr[64];
    char style_attr[256];
    struct core_css_style inline_style;
    if (!ctx || !tag_text) {
        return;
    }
    core_extract_attr(tag_text, "class", class_attr, sizeof(class_attr));
    core_extract_attr(tag_text, "id", id_attr, sizeof(id_attr));
    for (uint32_t i = 0; i < ctx->css_rule_count; ++i) {
        if (core_css_rule_matches(&ctx->css_rules[i], tag,
                                  class_attr, id_attr)) {
            core_apply_css_style(ctx, &ctx->css_rules[i].style);
        }
    }
    core_extract_attr(tag_text, "style", style_attr, sizeof(style_attr));
    if (style_attr[0]) {
        core_css_style_init(&inline_style);
        core_parse_css_declarations(style_attr, &inline_style);
        core_apply_css_style(ctx, &inline_style);
    }
    ctx->indent = core_base_indent(ctx);
}

static int core_tag_name_eq(const char *tag, const char *name)
{
    uint32_t i = 0;
    while (tag && tag[i] && !core_is_space(tag[i]) &&
           tag[i] != '/' && tag[i] != '>') {
        if (core_tolower(tag[i]) != name[i]) {
            return 0;
        }
        ++i;
    }
    return name[i] == 0;
}

static void core_extract_attr(const char *tag, const char *attr,
                              char *dst, uint32_t cap)
{
    uint32_t i = 0;
    uint32_t attr_len = (uint32_t)strlen(attr);
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    while (tag && tag[i]) {
        while (tag[i] && core_is_space(tag[i])) {
            ++i;
        }
        if (!tag[i]) {
            return;
        }
        if (core_starts_with_ignore_case(tag + i, attr)) {
            uint32_t j = i + attr_len;
            while (tag[j] && core_is_space(tag[j])) {
                ++j;
            }
            if (tag[j] == '=') {
                char quote = 0;
                uint32_t pos = 0;
                ++j;
                while (tag[j] && core_is_space(tag[j])) {
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
                    } else if (core_is_space(tag[j]) || tag[j] == '>') {
                        break;
                    }
                    dst[pos++] = tag[j++];
                }
                dst[pos] = 0;
                return;
            }
        }
        while (tag[i] && !core_is_space(tag[i])) {
            ++i;
        }
    }
}

static int core_parse_http_url(const char *url, struct core_http_url *out)
{
    const char *p;
    uint32_t host_pos = 0;
    uint32_t path_pos = 0;
    uint32_t port;
    if (!url || !out) {
        return 0;
    }
    if (core_starts_with_ignore_case(url, "https://")) {
        out->secure = 1;
        port = 443;
        p = url + 8;
    } else if (core_starts_with_ignore_case(url, "http://")) {
        out->secure = 0;
        port = 80;
        p = url + 7;
    } else {
        return 0;
    }
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
        while (core_is_digit(*p)) {
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

static void core_build_http_url(char *dst, uint32_t cap, const char *host,
                                uint32_t port, uint8_t secure,
                                const char *path)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    core_append_text(dst, &pos, cap, secure ? "https://" : "http://");
    core_append_text(dst, &pos, cap, host);
    if (port != (secure ? 443U : 80U)) {
        core_append_char(dst, &pos, cap, ':');
        core_append_u32(dst, &pos, cap, port);
    }
    if (!path || path[0] != '/') {
        core_append_char(dst, &pos, cap, '/');
    }
    core_append_text(dst, &pos, cap, path && path[0] ? path : "/");
}

static void core_parent_url_dir(const char *path, char *dst, uint32_t cap)
{
    uint32_t last_slash = 0;
    uint32_t i = 0;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!path || !path[0]) {
        core_copy_text(dst, cap, "/");
        return;
    }
    while (path[i]) {
        if (path[i] == '/') {
            last_slash = i;
        }
        ++i;
    }
    if (last_slash == 0) {
        core_copy_text(dst, cap, "/");
        return;
    }
    while (pos <= last_slash && pos + 1U < cap) {
        dst[pos] = path[pos];
        ++pos;
    }
    dst[pos] = 0;
}

static void core_resolve_href(const char *base, const char *href,
                              char *out, uint32_t cap)
{
    struct core_http_url parsed;
    char dir[LEONOS_FS_PATH_LEN];
    uint32_t pos = 0;
    if (!out || cap == 0) {
        return;
    }
    if (!href || !href[0]) {
        core_copy_text(out, cap, base);
        return;
    }
    if (core_starts_with_ignore_case(href, "http://") ||
        core_starts_with_ignore_case(href, "https://") ||
        core_starts_with_ignore_case(href, "about:") ||
        core_starts_with_ignore_case(href, "form:") ||
        core_is_drive_path(href)) {
        core_copy_text(out, cap, href);
        return;
    }
    if (href[0] == '#') {
        core_copy_text(out, cap, base);
        return;
    }
    if (core_parse_http_url(base, &parsed)) {
        if (href[0] == '/') {
            core_build_http_url(out, cap, parsed.host, parsed.port,
                                parsed.secure, href);
            return;
        }
        core_parent_url_dir(parsed.path, dir, sizeof(dir));
        out[0] = 0;
        core_append_text(out, &pos, cap,
                         parsed.secure ? "https://" : "http://");
        core_append_text(out, &pos, cap, parsed.host);
        if (parsed.port != (parsed.secure ? 443U : 80U)) {
            core_append_char(out, &pos, cap, ':');
            core_append_u32(out, &pos, cap, parsed.port);
        }
        core_append_text(out, &pos, cap, dir);
        core_append_text(out, &pos, cap, href);
        return;
    }
    if (core_is_drive_path(base)) {
        core_parent_url_dir(base, dir, sizeof(dir));
        out[0] = 0;
        core_append_text(out, &pos, cap, dir);
        core_append_text(out, &pos, cap, href);
        return;
    }
    core_copy_text(out, cap, href);
}

static char core_entity_to_char(const char *entity)
{
    if (core_text_eq(entity, "amp")) {
        return '&';
    }
    if (core_text_eq(entity, "lt")) {
        return '<';
    }
    if (core_text_eq(entity, "gt")) {
        return '>';
    }
    if (core_text_eq(entity, "quot")) {
        return '"';
    }
    if (core_text_eq(entity, "apos")) {
        return '\'';
    }
    if (core_text_eq(entity, "nbsp")) {
        return ' ';
    }
    if (entity && entity[0] == '#') {
        uint32_t value = 0;
        uint32_t i = 1;
        if (entity[i] == 'x' || entity[i] == 'X') {
            ++i;
            while (entity[i] && core_is_hex_digit(entity[i])) {
                value = value * 16U + core_hex_value(entity[i]);
                ++i;
            }
            if (entity[i] == 0 && value >= 32U && value <= 126U) {
                return (char)value;
            }
            return 0;
        }
        while (entity[i] && core_is_digit(entity[i])) {
            value = value * 10U + (uint32_t)(entity[i] - '0');
            ++i;
        }
        if (entity[i] == 0 && value >= 32U && value <= 126U) {
            return (char)value;
        }
    }
    return 0;
}

static void core_parse_tag(struct core_render_ctx *ctx, const char *tag,
                           const char *base_url)
{
    char href[LEONOS_FS_PATH_LEN];
    char resolved[LEONOS_FS_PATH_LEN];
    char alt[128];
    char align_attr[24];
    char tag_name[16];
    uint32_t i = 0;
    uint32_t name_pos = 0;
    uint8_t heading_kind;
    uint8_t closing = 0;
    if (!ctx || !ctx->view || !tag) {
        return;
    }
    while (core_is_space(tag[i])) {
        ++i;
    }
    if (tag[i] == '!') {
        return;
    }
    if (tag[i] == '/') {
        closing = 1;
        ++i;
        while (core_is_space(tag[i])) {
            ++i;
        }
    }
    while (tag[i + name_pos] && !core_is_space(tag[i + name_pos]) &&
           tag[i + name_pos] != '/' && tag[i + name_pos] != '>' &&
           name_pos + 1U < sizeof(tag_name)) {
        tag_name[name_pos] = core_tolower(tag[i + name_pos]);
        ++name_pos;
    }
    tag_name[name_pos] = 0;
    if (ctx->capture_style) {
        if (closing && core_text_eq_ignore_case(tag_name, "style")) {
            ctx->style_text[ctx->style_text_pos] = 0;
            core_parse_css_rules(ctx, ctx->style_text);
            ctx->capture_style = 0;
            ctx->style_text_pos = 0;
        }
        return;
    }
    if (ctx->skip_content) {
        if (closing && core_text_eq_ignore_case(tag_name, "script")) {
            ctx->skip_content = 0;
        }
        return;
    }
    if (closing) {
        if (core_text_eq_ignore_case(tag_name, "a")) {
            ctx->in_link = 0;
            ctx->link_id = 0xffU;
            core_pop_style(ctx);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "title")) {
            ctx->in_title = 0;
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "style") ||
            core_text_eq_ignore_case(tag_name, "script")) {
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "b") ||
            core_text_eq_ignore_case(tag_name, "strong") ||
            core_text_eq_ignore_case(tag_name, "i") ||
            core_text_eq_ignore_case(tag_name, "em") ||
            core_text_eq_ignore_case(tag_name, "code") ||
            core_text_eq_ignore_case(tag_name, "span")) {
            core_pop_style(ctx);
            return;
        }
        if (core_tag_heading_kind(tag_name)) {
            core_render_newline(ctx, 0);
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "td") ||
            core_text_eq_ignore_case(tag_name, "th")) {
            core_render_literal(ctx, " ");
            core_pop_style(ctx);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "tr")) {
            if (ctx->in_table_row && ctx->table_cell_count) {
                core_render_literal(ctx, " |");
            }
            core_render_newline(ctx, 0);
            ctx->in_table_row = 0;
            ctx->table_cell_count = 0;
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "table")) {
            core_render_newline(ctx, 0);
            if (ctx->table_depth) {
                --ctx->table_depth;
            }
            ctx->in_table_row = 0;
            ctx->table_cell_count = 0;
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            core_render_newline(ctx, 0);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "blockquote")) {
            core_render_newline(ctx, 0);
            if (ctx->blockquote_depth) {
                --ctx->blockquote_depth;
            }
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            core_render_newline(ctx, 0);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "ul") ||
            core_text_eq_ignore_case(tag_name, "ol")) {
            core_render_newline(ctx, 0);
            if (ctx->list_depth) {
                --ctx->list_depth;
            }
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            return;
        }
        if (core_text_eq_ignore_case(tag_name, "li") ||
            core_tag_is_block(tag_name)) {
            core_render_newline(ctx, 0);
            core_pop_style(ctx);
            core_restore_flow_style(ctx);
            return;
        }
        core_pop_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "script")) {
        ctx->skip_content = 1;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "style")) {
        ctx->capture_style = 1;
        ctx->style_text_pos = 0;
        ctx->style_text[0] = 0;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "title")) {
        ctx->in_title = 1;
        if (ctx->view->page_title && ctx->view->page_title_cap) {
            ctx->view->page_title[0] = 0;
        }
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "br")) {
        core_render_newline(ctx, 1);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "hr")) {
        core_push_style(ctx);
        core_apply_element_css(ctx, tag_name, tag + i);
        core_emit_empty_line(ctx, BROWSER_LINE_HR);
        core_pop_style(ctx);
        return;
    }
    core_push_style(ctx);
    core_apply_element_css(ctx, tag_name, tag + i);
    if (core_tag_is_block(tag_name)) {
        core_render_newline(ctx, 0);
        core_restore_flow_style(ctx);
        return;
    }
    heading_kind = core_tag_heading_kind(tag_name);
    if (heading_kind) {
        core_render_newline(ctx, 0);
        ctx->kind = heading_kind;
        ctx->indent = core_base_indent(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "blockquote")) {
        core_render_newline(ctx, 0);
        if (ctx->blockquote_depth < 8U) {
            ++ctx->blockquote_depth;
        }
        core_restore_flow_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "table")) {
        core_render_newline(ctx, 0);
        if (ctx->table_depth < 4U) {
            ++ctx->table_depth;
        }
        ctx->in_table_row = 0;
        ctx->table_cell_count = 0;
        core_restore_flow_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "tr")) {
        core_render_newline(ctx, 0);
        if (!ctx->table_depth) {
            ctx->table_depth = 1;
        }
        ctx->in_table_row = 1;
        ctx->table_cell_count = 0;
        core_restore_flow_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "td") ||
        core_text_eq_ignore_case(tag_name, "th")) {
        uint8_t inherited_align = ctx->style_depth
                                      ? ctx->style_stack[ctx->style_depth - 1U].css_align
                                      : BROWSER_ALIGN_LEFT;
        uint8_t cell_align = ctx->css_align;
        core_extract_attr(tag + i, "align", align_attr, sizeof(align_attr));
        if (align_attr[0]) {
            cell_align = core_parse_align_value(align_attr, cell_align);
        } else if (core_text_eq_ignore_case(tag_name, "th") &&
                   ctx->css_align == inherited_align) {
            cell_align = BROWSER_ALIGN_CENTER;
        }
        ctx->table_cell_align = cell_align;
        if (!ctx->in_table_row) {
            ctx->in_table_row = 1;
            ctx->table_cell_count = 0;
            if (!ctx->table_depth) {
                ctx->table_depth = 1;
            }
            core_restore_flow_style(ctx);
        }
        if (ctx->table_cell_count == 0) {
            core_render_literal(ctx, "| ");
        } else {
            core_render_literal(ctx, " | ");
        }
        if (ctx->table_cell_count < 250U) {
            ++ctx->table_cell_count;
        }
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "ul") ||
        core_text_eq_ignore_case(tag_name, "ol")) {
        core_render_newline(ctx, 0);
        if (ctx->list_depth < 8U) {
            ++ctx->list_depth;
        }
        core_restore_flow_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "li")) {
        uint8_t bullet_indent;
        uint8_t continue_indent;
        core_render_newline(ctx, 0);
        core_restore_flow_style(ctx);
        bullet_indent = (uint8_t)(core_base_indent(ctx) +
                        (ctx->list_depth ? (ctx->list_depth - 1U) * 2U : 0U));
        if (bullet_indent > 30U) {
            bullet_indent = 30U;
        }
        continue_indent = bullet_indent > 29U ? bullet_indent : (uint8_t)(bullet_indent + 2U);
        ctx->indent = bullet_indent;
        core_render_literal(ctx, "* ");
        ctx->indent = continue_indent;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "img")) {
        uint8_t saved_kind = ctx->kind;
        uint8_t saved_indent = ctx->indent;
        core_extract_attr(tag + i, "alt", alt, sizeof(alt));
        core_render_newline(ctx, 0);
        ctx->kind = BROWSER_LINE_IMAGE;
        ctx->indent = core_base_indent(ctx);
        core_render_literal(ctx, "[image] ");
        core_render_literal(ctx, alt[0] ? alt : "image");
        core_render_newline(ctx, 1);
        ctx->kind = saved_kind;
        ctx->indent = saved_indent;
        core_pop_style(ctx);
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "b") ||
        core_text_eq_ignore_case(tag_name, "strong")) {
        ctx->text_style |= BROWSER_TEXT_BOLD;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "i") ||
        core_text_eq_ignore_case(tag_name, "em")) {
        ctx->text_style |= BROWSER_TEXT_ITALIC;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "code")) {
        ctx->text_style |= BROWSER_TEXT_CODE;
        return;
    }
    if (core_text_eq_ignore_case(tag_name, "a")) {
        core_extract_attr(tag + i, "href", href, sizeof(href));
        if (href[0]) {
            core_resolve_href(base_url, href, resolved, sizeof(resolved));
            ctx->link_id = core_add_link(ctx->view, resolved);
            ctx->in_link = ctx->link_id != 0xffU;
        }
    }
}

void litehtml_core_render_html(struct litehtml_core_view *view,
                               const char *source,
                               const char *base_url)
{
    struct core_render_ctx ctx;
    uint32_t i = 0;
    char tag[192];
    if (!view) {
        return;
    }
    core_reset_document(view);
    ctx.view = view;
    ctx.kind = BROWSER_LINE_NORMAL;
    ctx.indent = 0;
    ctx.pending_space = 0;
    ctx.in_link = 0;
    ctx.link_id = 0xffU;
    ctx.in_title = 0;
    ctx.skip_content = 0;
    ctx.text_style = 0;
    ctx.blockquote_depth = 0;
    ctx.list_depth = 0;
    ctx.table_depth = 0;
    ctx.in_table_row = 0;
    ctx.table_cell_count = 0;
    ctx.table_cell_align = BROWSER_ALIGN_LEFT;
    ctx.css_indent = 0;
    ctx.css_align = BROWSER_ALIGN_LEFT;
    ctx.style_depth = 0;
    ctx.capture_style = 0;
    ctx.text_fg = BROWSER_COLOR_UNSET;
    ctx.text_bg = BROWSER_COLOR_UNSET;
    ctx.border_color = BROWSER_COLOR_UNSET;
    ctx.style_text_pos = 0;
    ctx.style_text[0] = 0;
    ctx.css_rule_count = 0;
    if (view->page_title && view->page_title_cap && !view->page_title[0]) {
        core_copy_text(view->page_title, view->page_title_cap,
                       T("Untitled", "无标题"));
    }
    while (source && source[i]) {
        if (source[i] == '<') {
            uint32_t tag_pos = 0;
            ++i;
            while (source[i] && source[i] != '>' &&
                   tag_pos + 1U < sizeof(tag)) {
                tag[tag_pos++] = source[i++];
            }
            tag[tag_pos] = 0;
            if (source[i] == '>') {
                ++i;
            }
            core_parse_tag(&ctx, tag, base_url);
            continue;
        }
        if (ctx.capture_style) {
            if (ctx.style_text_pos + 1U < sizeof(ctx.style_text)) {
                ctx.style_text[ctx.style_text_pos++] = source[i];
                ctx.style_text[ctx.style_text_pos] = 0;
            }
            ++i;
            continue;
        }
        if (source[i] == '&') {
            char entity[16];
            uint32_t entity_pos = 0;
            uint32_t j = i + 1U;
            char decoded;
            while (source[j] && source[j] != ';' &&
                   entity_pos + 1U < sizeof(entity)) {
                entity[entity_pos++] = source[j++];
            }
            entity[entity_pos] = 0;
            decoded = source[j] == ';' ? core_entity_to_char(entity) : 0;
            if (decoded) {
                if (ctx.in_title && view->page_title && view->page_title_cap) {
                    uint32_t pos = (uint32_t)strlen(view->page_title);
                    core_append_char(view->page_title, &pos,
                                     view->page_title_cap, decoded);
                } else if (!ctx.skip_content) {
                    core_render_html_char(&ctx, decoded);
                }
                i = source[j] == ';' ? j + 1U : i + 1U;
                continue;
            }
        }
        {
            uint32_t byte_len = 1;
            uint32_t cells = 1;
            uint32_t cp = core_decode_utf8_at(source, i, &byte_len, &cells);
            if (ctx.in_title && view->page_title && view->page_title_cap) {
                if (!core_codepoint_is_space(cp)) {
                    uint32_t pos = (uint32_t)strlen(view->page_title);
                    core_append_bytes(view->page_title, &pos,
                                      view->page_title_cap,
                                      source + i, byte_len);
                } else if (view->page_title[0] &&
                           view->page_title[strlen(view->page_title) - 1U] != ' ') {
                    uint32_t pos = (uint32_t)strlen(view->page_title);
                    core_append_char(view->page_title, &pos,
                                     view->page_title_cap, ' ');
                }
            } else if (!ctx.skip_content) {
                core_render_html_codepoint(&ctx, source + i, byte_len,
                                           cells, cp);
            }
            i += byte_len;
        }
    }
}

void litehtml_core_render_plain(struct litehtml_core_view *view,
                                const char *source)
{
    struct core_render_ctx ctx;
    uint32_t i = 0;
    if (!view) {
        return;
    }
    core_reset_document(view);
    ctx.view = view;
    ctx.kind = BROWSER_LINE_NORMAL;
    ctx.indent = 0;
    ctx.pending_space = 0;
    ctx.in_link = 0;
    ctx.link_id = 0xffU;
    ctx.in_title = 0;
    ctx.skip_content = 0;
    ctx.text_style = 0;
    ctx.blockquote_depth = 0;
    ctx.list_depth = 0;
    ctx.table_depth = 0;
    ctx.in_table_row = 0;
    ctx.table_cell_count = 0;
    ctx.table_cell_align = BROWSER_ALIGN_LEFT;
    ctx.css_indent = 0;
    ctx.css_align = BROWSER_ALIGN_LEFT;
    ctx.style_depth = 0;
    ctx.capture_style = 0;
    ctx.text_fg = BROWSER_COLOR_UNSET;
    ctx.text_bg = BROWSER_COLOR_UNSET;
    ctx.border_color = BROWSER_COLOR_UNSET;
    ctx.style_text_pos = 0;
    ctx.style_text[0] = 0;
    ctx.css_rule_count = 0;
    while (source && source[i]) {
        uint32_t byte_len = 1;
        uint32_t cells = 1;
        uint32_t cp = core_decode_utf8_at(source, i, &byte_len, &cells);
        core_render_raw_codepoint(&ctx, source + i, byte_len, cells, cp);
        i += byte_len;
    }
}
