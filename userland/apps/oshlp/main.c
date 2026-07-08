#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#include <stddef.h>
#include <stdint.h>

#define OSHLP_INITIAL_W 900U
#define OSHLP_INITIAL_H 620U
#define OSHLP_MIN_W 560U
#define OSHLP_MIN_H 360U
#define OSHLP_MAX_W 1180U
#define OSHLP_MAX_H 760U
#define OSHLP_TOOL_H 46U
#define OSHLP_STATUS_H 24U
#define OSHLP_TREE_W 246U
#define OSHLP_TREE_ROW_H 22U
#define OSHLP_SCROLL_W 18U
#define OSHLP_SOURCE_MAX (64U * 1024U)
#define OSHLP_DOC_MAX 64U
#define OSHLP_TREE_MAX 128U
#define OSHLP_RENDER_MAX 512U
#define OSHLP_LINE_TEXT_MAX 224U
#define OSHLP_HISTORY_MAX 32U
#define OSHLP_DOC_ID_MAX 48U
#define OSHLP_TITLE_MAX 96U
#define OSHLP_PATH_MAX 128U
#define OSHLP_META_MAX 80U
#define OSHLP_DEFAULT_PATH "0:/docs/leonos.hlp"
#define OSHLP_KEY_ESCAPE 1U
#define OSHLP_KEY_PAGE_UP 73U
#define OSHLP_KEY_PAGE_DOWN 81U
#define OSHLP_KEY_UP 72U
#define OSHLP_KEY_DOWN 80U

#define T(en, zh) leonos_i18n((en), (zh))

enum oshlp_lang {
    OSHLP_LANG_EN = 0,
    OSHLP_LANG_ZH = 1,
    OSHLP_LANG_COUNT = 2,
};

enum oshlp_line_kind {
    OSHLP_LINE_NORMAL = 0,
    OSHLP_LINE_H1,
    OSHLP_LINE_H2,
    OSHLP_LINE_H3,
    OSHLP_LINE_MUTED,
    OSHLP_LINE_CODE,
    OSHLP_LINE_QUOTE,
    OSHLP_LINE_TABLE,
    OSHLP_LINE_HR,
};

struct hlp_body {
    uint32_t start;
    uint32_t len;
};

struct hlp_doc {
    char id[OSHLP_DOC_ID_MAX];
    char path[OSHLP_LANG_COUNT][OSHLP_PATH_MAX];
    char title[OSHLP_LANG_COUNT][OSHLP_TITLE_MAX];
    char author[OSHLP_META_MAX];
    char version[32];
    struct hlp_body body[OSHLP_LANG_COUNT];
};

struct tree_node {
    char label[OSHLP_TITLE_MAX];
    char key[OSHLP_PATH_MAX];
    uint32_t id;
    uint32_t depth;
    uint8_t leaf;
};

struct render_line {
    char text[OSHLP_LINE_TEXT_MAX];
    uint32_t cells;
    uint8_t kind;
    uint8_t indent;
};

static uint32_t pixels[OSHLP_MAX_W * OSHLP_MAX_H];
static struct leonos_ui_surface ui;
static int window_id;
static uint32_t view_w = OSHLP_INITIAL_W;
static uint32_t view_h = OSHLP_INITIAL_H;
static char source[OSHLP_SOURCE_MAX + 1U];
static uint32_t source_len;
static char hlp_path[LEONOS_FS_PATH_LEN];
static char file_title[OSHLP_LANG_COUNT][OSHLP_TITLE_MAX];
static char file_author[OSHLP_META_MAX];
static char file_version[32];
static struct hlp_doc docs[OSHLP_DOC_MAX];
static uint32_t doc_count;
static uint32_t active_doc;
static uint8_t current_lang;
static uint8_t lang_dropdown_open;
static char status_text[160];
static struct tree_node tree_nodes[OSHLP_TREE_MAX];
static struct leonos_ui_tree_item tree_items[OSHLP_TREE_MAX];
static uint32_t tree_count;
static uint32_t tree_scroll;
static struct render_line render_lines[OSHLP_RENDER_MAX];
static uint32_t render_count;
static uint32_t scroll_line;
static uint32_t history[OSHLP_HISTORY_MAX];
static uint32_t history_count;
static uint32_t history_index;

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
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

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (!dst || !pos || *pos + 1U >= cap) {
        return;
    }
    dst[*pos] = ch;
    ++(*pos);
    dst[*pos] = 0;
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    while (src && src[i]) {
        append_char(dst, pos, cap, src[i]);
        ++i;
    }
}

static int is_space_char(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int is_hlp_id_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '.' || ch == '_' || ch == '-';
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path) {
        return "";
    }
    for (uint32_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1U;
        }
    }
    return base ? base : "";
}

static void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text ? text : "");
}

static uint32_t lang_index(void)
{
    return current_lang == OSHLP_LANG_ZH ? OSHLP_LANG_ZH : OSHLP_LANG_EN;
}

static const char *localized_pair(char pair[OSHLP_LANG_COUNT][OSHLP_TITLE_MAX])
{
    uint32_t lang = lang_index();
    uint32_t other = lang == OSHLP_LANG_ZH ? OSHLP_LANG_EN : OSHLP_LANG_ZH;
    if (pair[lang][0]) {
        return pair[lang];
    }
    if (pair[other][0]) {
        return pair[other];
    }
    return "";
}

static const char *doc_title(const struct hlp_doc *doc)
{
    if (!doc) {
        return T("Document", "文档");
    }
    {
        uint32_t lang = lang_index();
        uint32_t other = lang == OSHLP_LANG_ZH ? OSHLP_LANG_EN : OSHLP_LANG_ZH;
        if (doc->title[lang][0]) {
            return doc->title[lang];
        }
        if (doc->title[other][0]) {
            return doc->title[other];
        }
    }
    return doc->id[0] ? doc->id : T("Document", "文档");
}

static const char *doc_path_label(const struct hlp_doc *doc)
{
    uint32_t lang = lang_index();
    uint32_t other = lang == OSHLP_LANG_ZH ? OSHLP_LANG_EN : OSHLP_LANG_ZH;
    if (!doc) {
        return "";
    }
    if (doc->path[lang][0]) {
        return doc->path[lang];
    }
    if (doc->path[other][0]) {
        return doc->path[other];
    }
    return doc_title(doc);
}

static int slice_starts_with(uint32_t start, uint32_t len, const char *prefix)
{
    uint32_t i = 0;
    while (prefix && prefix[i]) {
        if (i >= len || source[start + i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return i > 0;
}

static void trim_slice(uint32_t *start, uint32_t *len)
{
    while (*len && is_space_char(source[*start])) {
        ++(*start);
        --(*len);
    }
    while (*len && is_space_char(source[*start + *len - 1U])) {
        --(*len);
    }
}

static void copy_source_slice(char *dst, uint32_t cap, uint32_t start, uint32_t len)
{
    uint32_t i = 0;
    trim_slice(&start, &len);
    if (!dst || cap == 0) {
        return;
    }
    while (i + 1U < cap && i < len) {
        dst[i] = source[start + i];
        ++i;
    }
    dst[i] = 0;
}

static int key_eq(uint32_t start, uint32_t len, const char *key)
{
    uint32_t i = 0;
    trim_slice(&start, &len);
    while (key && key[i]) {
        if (i >= len || source[start + i] != key[i]) {
            return 0;
        }
        ++i;
    }
    return i == len;
}

static void next_line(uint32_t pos, uint32_t *line_start,
                      uint32_t *line_len, uint32_t *next_pos)
{
    uint32_t end = pos;
    while (end < source_len && source[end] != '\n' && source[end] != '\r') {
        ++end;
    }
    *line_start = pos;
    *line_len = end - pos;
    while (end < source_len && (source[end] == '\n' || source[end] == '\r')) {
        ++end;
    }
    *next_pos = end;
}

static int valid_doc_id_slice(uint32_t start, uint32_t len)
{
    trim_slice(&start, &len);
    if (!len || len >= OSHLP_DOC_ID_MAX) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (!is_hlp_id_char(source[start + i])) {
            return 0;
        }
    }
    return 1;
}

static int parse_lang_slice(uint32_t start, uint32_t len, uint32_t *out_lang)
{
    trim_slice(&start, &len);
    if (len == 2 && source[start] == 'e' && source[start + 1U] == 'n') {
        *out_lang = OSHLP_LANG_EN;
        return 1;
    }
    if (len == 2 && source[start] == 'z' && source[start + 1U] == 'h') {
        *out_lang = OSHLP_LANG_ZH;
        return 1;
    }
    return 0;
}

static void parse_meta_line(uint32_t start, uint32_t len, int32_t current_doc)
{
    uint32_t colon = start;
    uint32_t key_start = start;
    uint32_t key_len;
    uint32_t value_start;
    uint32_t value_len;
    while (colon < start + len && source[colon] != ':') {
        ++colon;
    }
    if (colon >= start + len) {
        return;
    }
    key_len = colon - start;
    value_start = colon + 1U;
    value_len = start + len - value_start;
    if (current_doc >= 0) {
        struct hlp_doc *doc = &docs[(uint32_t)current_doc];
        if (key_eq(key_start, key_len, "title.en")) {
            copy_source_slice(doc->title[OSHLP_LANG_EN], sizeof(doc->title[0]),
                              value_start, value_len);
        } else if (key_eq(key_start, key_len, "title.zh")) {
            copy_source_slice(doc->title[OSHLP_LANG_ZH], sizeof(doc->title[0]),
                              value_start, value_len);
        } else if (key_eq(key_start, key_len, "path.en")) {
            copy_source_slice(doc->path[OSHLP_LANG_EN], sizeof(doc->path[0]),
                              value_start, value_len);
        } else if (key_eq(key_start, key_len, "path.zh")) {
            copy_source_slice(doc->path[OSHLP_LANG_ZH], sizeof(doc->path[0]),
                              value_start, value_len);
        } else if (key_eq(key_start, key_len, "author")) {
            copy_source_slice(doc->author, sizeof(doc->author), value_start, value_len);
        } else if (key_eq(key_start, key_len, "version")) {
            copy_source_slice(doc->version, sizeof(doc->version), value_start, value_len);
        }
        return;
    }
    if (key_eq(key_start, key_len, "title.en")) {
        copy_source_slice(file_title[OSHLP_LANG_EN], sizeof(file_title[0]),
                          value_start, value_len);
    } else if (key_eq(key_start, key_len, "title.zh")) {
        copy_source_slice(file_title[OSHLP_LANG_ZH], sizeof(file_title[0]),
                          value_start, value_len);
    } else if (key_eq(key_start, key_len, "author")) {
        copy_source_slice(file_author, sizeof(file_author), value_start, value_len);
    } else if (key_eq(key_start, key_len, "version")) {
        copy_source_slice(file_version, sizeof(file_version), value_start, value_len);
    }
}

static void close_body(int32_t current_doc, int32_t current_lang,
                       uint32_t marker_start)
{
    if (current_doc < 0 || current_lang < 0) {
        return;
    }
    {
        struct hlp_body *body = &docs[(uint32_t)current_doc].body[(uint32_t)current_lang];
        if (body->start <= marker_start) {
            body->len = marker_start - body->start;
            while (body->len && is_space_char(source[body->start + body->len - 1U])) {
                --body->len;
            }
        }
    }
}

static int parse_hlp(void)
{
    uint32_t pos = 0;
    int32_t current_doc = -1;
    int32_t current_lang = -1;
    memset(docs, 0, sizeof(docs));
    memset(file_title, 0, sizeof(file_title));
    file_author[0] = 0;
    file_version[0] = 0;
    doc_count = 0;
    while (pos < source_len) {
        uint32_t line_start;
        uint32_t line_len;
        uint32_t next_pos;
        next_line(pos, &line_start, &line_len, &next_pos);
        if (slice_starts_with(line_start, line_len, "%%")) {
            close_body(current_doc, current_lang, line_start);
            current_lang = -1;
            if (slice_starts_with(line_start, line_len, "%%HLP")) {
                current_doc = -1;
            } else if (slice_starts_with(line_start, line_len, "%%DOC")) {
                uint32_t id_start = line_start + 5U;
                uint32_t id_len = line_len > 5U ? line_len - 5U : 0;
                if (doc_count >= OSHLP_DOC_MAX || !valid_doc_id_slice(id_start, id_len)) {
                    return -1;
                }
                current_doc = (int32_t)doc_count;
                copy_source_slice(docs[doc_count].id, sizeof(docs[doc_count].id),
                                  id_start, id_len);
                ++doc_count;
            } else if (slice_starts_with(line_start, line_len, "%%LANG")) {
                uint32_t lang_start = line_start + 7U;
                uint32_t lang_len = line_len > 7U ? line_len - 7U : 0;
                uint32_t lang = 0;
                if (current_doc < 0 || !parse_lang_slice(lang_start, lang_len, &lang)) {
                    return -1;
                }
                current_lang = (int32_t)lang;
                docs[(uint32_t)current_doc].body[lang].start = next_pos;
                docs[(uint32_t)current_doc].body[lang].len = 0;
            } else if (slice_starts_with(line_start, line_len, "%%ENDDOC")) {
                current_doc = -1;
            }
        } else if (current_lang < 0) {
            parse_meta_line(line_start, line_len, current_doc);
        }
        pos = next_pos;
    }
    close_body(current_doc, current_lang, source_len);
    for (uint32_t i = 0; i < doc_count; ++i) {
        if (!docs[i].title[OSHLP_LANG_EN][0] && !docs[i].title[OSHLP_LANG_ZH][0]) {
            copy_text(docs[i].title[OSHLP_LANG_EN], sizeof(docs[i].title[0]), docs[i].id);
        }
    }
    return doc_count ? 0 : -1;
}

static uint32_t utf8_decode(const char *text, uint32_t pos,
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
    if (b0 < 0x80U) {
        cp = b0;
    } else if ((b0 & 0xe0U) == 0xc0U && (s[pos + 1U] & 0xc0U) == 0x80U) {
        cp = ((uint32_t)(b0 & 0x1fU) << 6) | (uint32_t)(s[pos + 1U] & 0x3fU);
        len = 2;
    } else if ((b0 & 0xf0U) == 0xe0U &&
               (s[pos + 1U] & 0xc0U) == 0x80U &&
               (s[pos + 2U] & 0xc0U) == 0x80U) {
        cp = ((uint32_t)(b0 & 0x0fU) << 12) |
             ((uint32_t)(s[pos + 1U] & 0x3fU) << 6) |
             (uint32_t)(s[pos + 2U] & 0x3fU);
        len = 3;
    } else if ((b0 & 0xf8U) == 0xf0U &&
               (s[pos + 1U] & 0xc0U) == 0x80U &&
               (s[pos + 2U] & 0xc0U) == 0x80U &&
               (s[pos + 3U] & 0xc0U) == 0x80U) {
        cp = ((uint32_t)(b0 & 0x07U) << 18) |
             ((uint32_t)(s[pos + 1U] & 0x3fU) << 12) |
             ((uint32_t)(s[pos + 2U] & 0x3fU) << 6) |
             (uint32_t)(s[pos + 3U] & 0x3fU);
        len = 4;
    } else {
        cp = 0xfffdU;
    }
    if (byte_len) {
        *byte_len = len;
    }
    if (cells) {
        int wide = (cp >= 0x1100U && cp <= 0x115fU) ||
                   (cp >= 0x2e80U && cp <= 0xa4cfU) ||
                   (cp >= 0xac00U && cp <= 0xd7a3U) ||
                   (cp >= 0xf900U && cp <= 0xfaffU) ||
                   (cp >= 0xfe10U && cp <= 0xfe6fU) ||
                   (cp >= 0xff00U && cp <= 0xffe6U);
        *cells = wide ? 2U : (cp == '\t' ? 4U : 1U);
    }
    return cp;
}

static uint32_t text_cells(const char *text)
{
    uint32_t pos = 0;
    uint32_t cells = 0;
    while (text && text[pos]) {
        uint32_t len;
        uint32_t c;
        (void)utf8_decode(text, pos, &len, &c);
        if (!len) {
            break;
        }
        cells += c;
        pos += len;
    }
    return cells;
}

static void render_add_line(uint8_t kind, uint8_t indent, const char *text)
{
    if (render_count >= OSHLP_RENDER_MAX) {
        return;
    }
    render_lines[render_count].kind = kind;
    render_lines[render_count].indent = indent;
    copy_text(render_lines[render_count].text,
              sizeof(render_lines[render_count].text), text ? text : "");
    render_lines[render_count].cells = text_cells(render_lines[render_count].text);
    ++render_count;
}

static uint32_t doc_text_cols(void)
{
    uint32_t w = view_w > OSHLP_TREE_W + OSHLP_SCROLL_W + 54U
                     ? view_w - OSHLP_TREE_W - OSHLP_SCROLL_W - 54U
                     : 80U;
    return w / LEONOS_FONT_W;
}

static void clean_markdown_inline(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    uint32_t o = 0;
    dst[0] = 0;
    while (src && src[i]) {
        if (src[i] == '*' && src[i + 1U] == '*') {
            i += 2U;
            continue;
        }
        if (src[i] == '*' || src[i] == '_' || src[i] == '`') {
            ++i;
            continue;
        }
        if (src[i] == '[') {
            uint32_t label_start = i + 1U;
            uint32_t label_end = label_start;
            while (src[label_end] && src[label_end] != ']') {
                ++label_end;
            }
            if (src[label_end] == ']' && src[label_end + 1U] == '(') {
                uint32_t url_start = label_end + 2U;
                uint32_t url_end = url_start;
                while (src[url_end] && src[url_end] != ')') {
                    ++url_end;
                }
                if (src[url_end] == ')') {
                    while (label_start < label_end && o + 1U < cap) {
                        dst[o++] = src[label_start++];
                    }
                    if (o + 2U < cap) {
                        dst[o++] = ' ';
                        dst[o++] = '<';
                    }
                    while (url_start < url_end && o + 1U < cap) {
                        dst[o++] = src[url_start++];
                    }
                    if (o + 1U < cap) {
                        dst[o++] = '>';
                    }
                    dst[o] = 0;
                    i = url_end + 1U;
                    continue;
                }
            }
        }
        if (o + 1U < cap) {
            dst[o++] = src[i];
            dst[o] = 0;
        }
        ++i;
    }
}

static void emit_wrapped(uint8_t kind, uint8_t indent,
                         const char *prefix, const char *text)
{
    char line[OSHLP_LINE_TEXT_MAX];
    uint32_t out = 0;
    uint32_t pos = 0;
    uint32_t cells = 0;
    uint32_t max_cells = doc_text_cols();
    uint32_t prefix_cells = text_cells(prefix);
    if (max_cells > (uint32_t)indent * 2U + 4U) {
        max_cells -= (uint32_t)indent * 2U;
    }
    if (max_cells < 16U) {
        max_cells = 16U;
    }
    line[0] = 0;
    append_text(line, &out, sizeof(line), prefix);
    cells = prefix_cells;
    while (text && text[pos]) {
        uint32_t byte_len;
        uint32_t char_cells;
        (void)utf8_decode(text, pos, &byte_len, &char_cells);
        if (!byte_len) {
            break;
        }
        if (cells + char_cells > max_cells || out + byte_len + 1U >= sizeof(line)) {
            render_add_line(kind, indent, line);
            line[0] = 0;
            out = 0;
            append_text(line, &out, sizeof(line), prefix_cells ? "  " : "");
            cells = prefix_cells ? 2U : 0U;
        }
        for (uint32_t i = 0; i < byte_len && out + 1U < sizeof(line); ++i) {
            line[out++] = text[pos + i];
        }
        line[out] = 0;
        cells += char_cells;
        pos += byte_len;
    }
    render_add_line(kind, indent, line);
}

static int line_is_rule(const char *line)
{
    uint32_t count = 0;
    char ch = 0;
    for (uint32_t i = 0; line && line[i]; ++i) {
        if (line[i] == ' ' || line[i] == '\t') {
            continue;
        }
        if (line[i] != '-' && line[i] != '*') {
            return 0;
        }
        if (!ch) {
            ch = line[i];
        } else if (line[i] != ch) {
            return 0;
        }
        ++count;
    }
    return count >= 3U;
}

static int line_is_ordered_list(const char *line, uint32_t *text_start)
{
    uint32_t pos = 0;
    if (!line || !is_digit(line[0])) {
        return 0;
    }
    while (is_digit(line[pos])) {
        ++pos;
    }
    if (line[pos] == '.' && line[pos + 1U] == ' ') {
        if (text_start) {
            *text_start = pos + 2U;
        }
        return 1;
    }
    return 0;
}

static int line_has_table_bar(const char *line)
{
    uint32_t bars = 0;
    for (uint32_t i = 0; line && line[i]; ++i) {
        if (line[i] == '|') {
            ++bars;
        }
    }
    return bars >= 2U;
}

static void render_markdown_body(uint32_t start, uint32_t len)
{
    uint32_t pos = start;
    uint32_t end = start + len;
    uint8_t in_code = 0;
    char raw[512];
    char clean[512];
    while (pos < end && render_count < OSHLP_RENDER_MAX) {
        uint32_t line_start = pos;
        uint32_t line_end;
        uint32_t line_len;
        uint32_t copy_len;
        while (pos < end && source[pos] != '\n' && source[pos] != '\r') {
            ++pos;
        }
        line_end = pos;
        while (pos < end && (source[pos] == '\n' || source[pos] == '\r')) {
            ++pos;
        }
        line_len = line_end - line_start;
        copy_len = line_len < sizeof(raw) - 1U ? line_len : sizeof(raw) - 1U;
        for (uint32_t i = 0; i < copy_len; ++i) {
            raw[i] = source[line_start + i];
        }
        raw[copy_len] = 0;
        if (raw[0] == '`' && raw[1] == '`' && raw[2] == '`') {
            in_code = in_code ? 0U : 1U;
            continue;
        }
        if (in_code) {
            emit_wrapped(OSHLP_LINE_CODE, 0, "", raw);
            continue;
        }
        {
            char *line = raw;
            while (*line == ' ' || *line == '\t') {
                ++line;
            }
            if (!line[0]) {
                render_add_line(OSHLP_LINE_NORMAL, 0, "");
            } else if (line[0] == '#' && line[1] == ' ') {
                clean_markdown_inline(clean, sizeof(clean), line + 2);
                emit_wrapped(OSHLP_LINE_H1, 0, "", clean);
            } else if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
                clean_markdown_inline(clean, sizeof(clean), line + 3);
                emit_wrapped(OSHLP_LINE_H2, 0, "", clean);
            } else if (line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
                clean_markdown_inline(clean, sizeof(clean), line + 4);
                emit_wrapped(OSHLP_LINE_H3, 0, "", clean);
            } else if (line_is_rule(line)) {
                render_add_line(OSHLP_LINE_HR, 0, "");
            } else if (line[0] == '>' && (line[1] == ' ' || line[1] == '\t')) {
                clean_markdown_inline(clean, sizeof(clean), line + 2);
                emit_wrapped(OSHLP_LINE_QUOTE, 1, "", clean);
            } else if ((line[0] == '-' || line[0] == '*') &&
                       (line[1] == ' ' || line[1] == '\t')) {
                clean_markdown_inline(clean, sizeof(clean), line + 2);
                emit_wrapped(OSHLP_LINE_NORMAL, 1, "* ", clean);
            } else {
                uint32_t item_start = 0;
                if (line_is_ordered_list(line, &item_start)) {
                    char prefix[16];
                    uint32_t p = 0;
                    for (uint32_t i = 0; i < item_start && p + 1U < sizeof(prefix); ++i) {
                        append_char(prefix, &p, sizeof(prefix), line[i]);
                    }
                    clean_markdown_inline(clean, sizeof(clean), line + item_start);
                    emit_wrapped(OSHLP_LINE_NORMAL, 1, prefix, clean);
                } else if (line_has_table_bar(line)) {
                    clean_markdown_inline(clean, sizeof(clean), line);
                    emit_wrapped(OSHLP_LINE_TABLE, 0, "", clean);
                } else {
                    clean_markdown_inline(clean, sizeof(clean), line);
                    emit_wrapped(OSHLP_LINE_NORMAL, 0, "", clean);
                }
            }
        }
    }
}

static void render_error_page(const char *title, const char *detail)
{
    render_count = 0;
    render_add_line(OSHLP_LINE_H1, 0, title ? title : T("Help error", "帮助错误"));
    render_add_line(OSHLP_LINE_NORMAL, 0, detail ? detail : T("Could not open help file.", "无法打开帮助文件。"));
    scroll_line = 0;
}

static void render_current_doc(void)
{
    struct hlp_doc *doc;
    uint32_t lang;
    uint32_t other;
    struct hlp_body *body;
    char meta[192];
    uint32_t pos = 0;
    render_count = 0;
    scroll_line = 0;
    if (!doc_count || active_doc >= doc_count) {
        render_error_page(T("No document", "没有文档"), T("This help file has no pages.", "此帮助文件没有页面。"));
        return;
    }
    doc = &docs[active_doc];
    lang = lang_index();
    other = lang == OSHLP_LANG_ZH ? OSHLP_LANG_EN : OSHLP_LANG_ZH;
    body = doc->body[lang].len ? &doc->body[lang] : &doc->body[other];
    render_add_line(OSHLP_LINE_H1, 0, doc_title(doc));
    if (doc->author[0] || doc->version[0]) {
        meta[0] = 0;
        if (doc->author[0]) {
            append_text(meta, &pos, sizeof(meta), lang == OSHLP_LANG_ZH ? "作者: " : "Author: ");
            append_text(meta, &pos, sizeof(meta), doc->author);
        }
        if (doc->version[0]) {
            if (pos) {
                append_text(meta, &pos, sizeof(meta), "  ");
            }
            append_text(meta, &pos, sizeof(meta), lang == OSHLP_LANG_ZH ? "版本: " : "Version: ");
            append_text(meta, &pos, sizeof(meta), doc->version);
        }
        render_add_line(OSHLP_LINE_MUTED, 0, meta);
    }
    render_add_line(OSHLP_LINE_NORMAL, 0, "");
    if (!body->len) {
        render_add_line(OSHLP_LINE_NORMAL, 0,
                        lang == OSHLP_LANG_ZH ? "当前语言没有正文内容。" : "No body is available for this language.");
    } else {
        render_markdown_body(body->start, body->len);
    }
}

static int ensure_folder_node(const char *key, const char *label, uint32_t depth)
{
    for (uint32_t i = 0; i < tree_count; ++i) {
        if (!tree_nodes[i].leaf && text_eq(tree_nodes[i].key, key)) {
            return 0;
        }
    }
    if (tree_count >= OSHLP_TREE_MAX) {
        return -1;
    }
    copy_text(tree_nodes[tree_count].key, sizeof(tree_nodes[tree_count].key), key);
    copy_text(tree_nodes[tree_count].label, sizeof(tree_nodes[tree_count].label), label);
    tree_nodes[tree_count].depth = depth;
    tree_nodes[tree_count].leaf = 0;
    tree_nodes[tree_count].id = 0;
    ++tree_count;
    return 0;
}

static void add_leaf_node(const char *label, uint32_t doc_index, uint32_t depth)
{
    if (tree_count >= OSHLP_TREE_MAX) {
        return;
    }
    tree_nodes[tree_count].key[0] = 0;
    copy_text(tree_nodes[tree_count].label, sizeof(tree_nodes[tree_count].label), label);
    tree_nodes[tree_count].depth = depth;
    tree_nodes[tree_count].leaf = 1;
    tree_nodes[tree_count].id = doc_index + 1U;
    ++tree_count;
}

static void add_doc_to_tree(uint32_t doc_index)
{
    char path[OSHLP_PATH_MAX];
    char segment[OSHLP_TITLE_MAX];
    char key[OSHLP_PATH_MAX];
    uint32_t seg = 0;
    uint32_t key_pos = 0;
    uint32_t depth = 0;
    const char *src = doc_path_label(&docs[doc_index]);
    copy_text(path, sizeof(path), src && src[0] ? src : doc_title(&docs[doc_index]));
    key[0] = 0;
    for (uint32_t i = 0;; ++i) {
        char ch = path[i];
        if (ch == '/' || ch == 0) {
            segment[seg] = 0;
            if (segment[0]) {
                if (ch == 0) {
                    add_leaf_node(segment, doc_index, depth);
                } else {
                    if (key_pos) {
                        append_char(key, &key_pos, sizeof(key), '/');
                    }
                    append_text(key, &key_pos, sizeof(key), segment);
                    (void)ensure_folder_node(key, segment, depth);
                    ++depth;
                }
            }
            seg = 0;
            if (ch == 0) {
                break;
            }
        } else if (seg + 1U < sizeof(segment)) {
            segment[seg++] = ch;
        }
    }
}

static void rebuild_tree(void)
{
    uint32_t visible;
    tree_count = 0;
    for (uint32_t i = 0; i < doc_count; ++i) {
        add_doc_to_tree(i);
    }
    for (uint32_t i = 0; i < tree_count; ++i) {
        tree_items[i].label = tree_nodes[i].label;
        tree_items[i].id = tree_nodes[i].id;
        tree_items[i].depth = tree_nodes[i].depth;
        tree_items[i].flags = tree_nodes[i].leaf ? LEONOS_UI_TREE_LEAF : LEONOS_UI_TREE_EXPANDED;
        if (tree_nodes[i].leaf && tree_nodes[i].id == active_doc + 1U) {
            tree_items[i].flags |= LEONOS_UI_TREE_SELECTED;
        }
    }
    visible = view_h > OSHLP_TOOL_H + OSHLP_STATUS_H + 28U
                  ? (view_h - OSHLP_TOOL_H - OSHLP_STATUS_H - 28U) / OSHLP_TREE_ROW_H
                  : 1U;
    if (tree_scroll + visible > tree_count) {
        tree_scroll = tree_count > visible ? tree_count - visible : 0;
    }
}

static void history_seed(uint32_t doc)
{
    history_count = 1;
    history_index = 0;
    history[0] = doc;
}

static void navigate_doc(uint32_t doc, uint8_t push_history)
{
    if (doc >= doc_count) {
        return;
    }
    active_doc = doc;
    if (push_history) {
        if (history_index + 1U < history_count) {
            history_count = history_index + 1U;
        }
        if (history_count < OSHLP_HISTORY_MAX) {
            history[history_count++] = doc;
            history_index = history_count - 1U;
        } else {
            for (uint32_t i = 1; i < OSHLP_HISTORY_MAX; ++i) {
                history[i - 1U] = history[i];
            }
            history[OSHLP_HISTORY_MAX - 1U] = doc;
            history_index = OSHLP_HISTORY_MAX - 1U;
        }
    }
    rebuild_tree();
    render_current_doc();
}

static void history_back(void)
{
    if (history_index == 0 || !history_count) {
        return;
    }
    --history_index;
    active_doc = history[history_index];
    rebuild_tree();
    render_current_doc();
}

static void history_forward(void)
{
    if (history_index + 1U >= history_count) {
        return;
    }
    ++history_index;
    active_doc = history[history_index];
    rebuild_tree();
    render_current_doc();
}

static int read_hlp_file(const char *path)
{
    int fd;
    long got;
    source_len = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    while (source_len < OSHLP_SOURCE_MAX) {
        got = read(fd, source + source_len, OSHLP_SOURCE_MAX - source_len);
        if (got < 0) {
            close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        source_len += (uint32_t)got;
    }
    if (source_len >= OSHLP_SOURCE_MAX) {
        char extra;
        got = read(fd, &extra, 1);
        close(fd);
        if (got < 0) {
            return (int)got;
        }
        if (got > 0) {
            return -1;
        }
    } else {
        close(fd);
    }
    source[source_len] = 0;
    return 0;
}

static uint32_t find_doc_by_id(const char *id)
{
    if (!id || !id[0]) {
        return 0;
    }
    for (uint32_t i = 0; i < doc_count; ++i) {
        if (text_eq(docs[i].id, id)) {
            return i;
        }
    }
    return 0;
}

static int load_help_file(const char *path, const char *doc_id)
{
    int ret;
    copy_text(hlp_path, sizeof(hlp_path), path && path[0] ? path : OSHLP_DEFAULT_PATH);
    ret = read_hlp_file(hlp_path);
    if (ret < 0) {
        render_error_page(T("Could not open help", "无法打开帮助"),
                          T("The .hlp file could not be read.", "无法读取 .hlp 文件。"));
        set_status(T("Open failed", "打开失败"));
        return ret;
    }
    if (parse_hlp() < 0) {
        render_error_page(T("Invalid help file", "无效的帮助文件"),
                          T("The .hlp file format is invalid or too large.", "此 .hlp 文件格式无效或过大。"));
        set_status(T("Parse failed", "解析失败"));
        return -1;
    }
    active_doc = find_doc_by_id(doc_id);
    tree_scroll = 0;
    history_seed(active_doc);
    rebuild_tree();
    render_current_doc();
    set_status(T("Ready", "就绪"));
    return 0;
}

static uint32_t content_top(void)
{
    return OSHLP_TOOL_H + 10U;
}

static uint32_t content_bottom(void)
{
    return view_h > OSHLP_STATUS_H + 8U ? view_h - OSHLP_STATUS_H - 8U : view_h;
}

static uint32_t doc_x(void)
{
    return OSHLP_TREE_W + 18U;
}

static uint32_t doc_y(void)
{
    return content_top() + 10U;
}

static uint32_t doc_w(void)
{
    return view_w > doc_x() + OSHLP_SCROLL_W + 14U
               ? view_w - doc_x() - OSHLP_SCROLL_W - 14U
               : 120U;
}

static uint32_t doc_h(void)
{
    return content_bottom() > doc_y() ? content_bottom() - doc_y() : 80U;
}

static uint32_t doc_page_lines(void)
{
    uint32_t rows = doc_h() / (LEONOS_FONT_H + 4U);
    return rows ? rows : 1U;
}

static uint32_t tree_visible_rows(void)
{
    uint32_t h = content_bottom() > content_top() + 8U ? content_bottom() - content_top() - 8U : 40U;
    uint32_t rows = h / OSHLP_TREE_ROW_H;
    return rows ? rows : 1U;
}

static void clamp_scroll(void)
{
    uint32_t page = doc_page_lines();
    uint32_t tree_page = tree_visible_rows();
    if (scroll_line + page > render_count) {
        scroll_line = render_count > page ? render_count - page : 0;
    }
    if (tree_scroll + tree_page > tree_count) {
        tree_scroll = tree_count > tree_page ? tree_count - tree_page : 0;
    }
}

static uint32_t render_line_height(uint8_t kind)
{
    if (kind == OSHLP_LINE_H1) {
        return 36U;
    }
    if (kind == OSHLP_LINE_H2) {
        return 28U;
    }
    if (kind == OSHLP_LINE_H3) {
        return 24U;
    }
    if (kind == OSHLP_LINE_HR) {
        return 14U;
    }
    return LEONOS_FONT_H + 4U;
}

static void draw_render_line(const struct render_line *line,
                             uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t cell_w = LEONOS_FONT_W;
    uint32_t cell_h = LEONOS_FONT_H;
    uint32_t fg = LEONOS_UI_BLACK;
    uint32_t bg = LEONOS_UI_WHITE;
    uint32_t tx;
    if (!line) {
        return;
    }
    if (line->kind == OSHLP_LINE_HR) {
        leonos_ui_rect(&ui, x, y + 6U, w, 1U, LEONOS_UI_DARK);
        leonos_ui_rect(&ui, x, y + 7U, w, 1U, LEONOS_UI_LIGHT);
        return;
    }
    if (line->kind == OSHLP_LINE_H1) {
        cell_w = 16U;
        cell_h = 32U;
        fg = 0x00000080U;
    } else if (line->kind == OSHLP_LINE_H2) {
        cell_w = 12U;
        cell_h = 24U;
        fg = 0x00003090U;
    } else if (line->kind == OSHLP_LINE_H3) {
        cell_w = 10U;
        cell_h = 20U;
        fg = 0x00004098U;
    } else if (line->kind == OSHLP_LINE_MUTED) {
        fg = LEONOS_UI_DARK;
    } else if (line->kind == OSHLP_LINE_CODE) {
        bg = 0x00eeeeeeU;
        leonos_ui_rect(&ui, x, y, w, render_line_height(line->kind) - 1U, bg);
    } else if (line->kind == OSHLP_LINE_QUOTE) {
        bg = 0x00f5f5f5U;
        leonos_ui_rect(&ui, x, y, w, render_line_height(line->kind) - 1U, bg);
        leonos_ui_rect(&ui, x + 4U, y, 3U, render_line_height(line->kind) - 1U, 0x00888888U);
        fg = 0x00484848U;
    } else if (line->kind == OSHLP_LINE_TABLE) {
        bg = 0x00f7fbffU;
        leonos_ui_rect(&ui, x, y, w, render_line_height(line->kind) - 1U, bg);
        leonos_ui_rect(&ui, x, y, w, 1U, 0x00a8b8c8U);
        leonos_ui_rect(&ui, x, y + render_line_height(line->kind) - 2U, w, 1U, 0x00a8b8c8U);
    }
    tx = x + (uint32_t)line->indent * 16U + 8U;
    if (tx < x + w) {
        leonos_ui_text_resized_clipped(&ui, tx, y + 2U, x + w - tx,
                                       line->text, fg, bg, cell_w, cell_h);
        if (line->kind == OSHLP_LINE_H1 || line->kind == OSHLP_LINE_H2) {
            leonos_ui_text_resized_clipped(&ui, tx + 1U, y + 2U, x + w - tx,
                                           line->text, fg, bg, cell_w, cell_h);
        }
    }
}

static void present_help(void)
{
    uint32_t lang_x = view_w > 156U ? view_w - 156U : 360U;
    const char *title = localized_pair(file_title);
    const char *display_title = title[0] ? title : path_basename(hlp_path);
    struct leonos_ui_dropdown_item lang_items[2] = {
        {"English", OSHLP_LANG_EN, 0},
        {"中文", OSHLP_LANG_ZH, 0},
    };
    uint32_t tree_rows = tree_visible_rows();
    uint32_t tree_draw = tree_count > tree_scroll ? min_u32(tree_count - tree_scroll, tree_rows) : 0;
    uint32_t y = doc_y();
    clamp_scroll();
    leonos_ui_bind(&ui, pixels, view_w, view_h, OSHLP_MAX_W);
    leonos_ui_rect(&ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_rect(&ui, 0, 0, view_w, OSHLP_TOOL_H, LEONOS_UI_GRAY);
    leonos_ui_button(&ui, 8, 9, 64, LEONOS_UI_BUTTON_H, T("Back", "后退"),
                     history_index > 0 ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(&ui, 78, 9, 76, LEONOS_UI_BUTTON_H, T("Forward", "前进"),
                     history_index + 1U < history_count ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_text_clipped(&ui, 166, 14, lang_x > 174U ? lang_x - 174U : 100U,
                           display_title, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_combobox(&ui, lang_x, 8, 140,
                       current_lang == OSHLP_LANG_ZH ? "中文" : "English",
                       lang_dropdown_open, 0);
    leonos_ui_panel(&ui, 8, content_top(), OSHLP_TREE_W - 16U,
                    content_bottom() > content_top() ? content_bottom() - content_top() : 40U,
                    LEONOS_UI_WHITE);
    if (tree_draw) {
        leonos_ui_tree(&ui, 12, content_top() + 4U, OSHLP_TREE_W - 24U,
                       &tree_items[tree_scroll], tree_draw, OSHLP_TREE_ROW_H);
    } else {
        leonos_ui_text_clipped(&ui, 18, content_top() + 14U, OSHLP_TREE_W - 36U,
                               T("No pages", "没有页面"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    leonos_ui_vscrollbar(&ui, OSHLP_TREE_W - 18U, content_top() + 4U, 12,
                         content_bottom() > content_top() + 8U
                             ? content_bottom() - content_top() - 8U
                             : 40U,
                         tree_scroll, tree_count > tree_rows ? tree_count : tree_rows,
                         tree_rows,
                         tree_count <= tree_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_panel(&ui, doc_x() - 8U, content_top(),
                    view_w > doc_x() ? view_w - doc_x() - 6U : 120U,
                    content_bottom() > content_top() ? content_bottom() - content_top() : 40U,
                    LEONOS_UI_WHITE);
    for (uint32_t i = scroll_line; i < render_count && y + 4U < content_bottom(); ++i) {
        uint32_t h = render_line_height(render_lines[i].kind);
        if (y + h > content_bottom()) {
            break;
        }
        draw_render_line(&render_lines[i], doc_x(), y, doc_w());
        y += h;
    }
    leonos_ui_vscrollbar(&ui, view_w > OSHLP_SCROLL_W + 8U ? view_w - OSHLP_SCROLL_W - 8U : 0,
                         doc_y(), OSHLP_SCROLL_W - 4U, doc_h(),
                         scroll_line, render_count > doc_page_lines() ? render_count : doc_page_lines(),
                         doc_page_lines(),
                         render_count <= doc_page_lines() ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_statusbar(&ui, view_h - OSHLP_STATUS_H, OSHLP_STATUS_H, status_text);
    if (lang_dropdown_open) {
        leonos_ui_dropdown(&ui, lang_x, 34, 140, lang_items, 2, current_lang,
                           26U, 1000U);
    }
    (void)leonos_gui_present_window((uint32_t)window_id, view_w, view_h, OSHLP_MAX_W, pixels);
}

static void handle_language_change(uint32_t lang)
{
    if (lang != OSHLP_LANG_EN && lang != OSHLP_LANG_ZH) {
        return;
    }
    current_lang = (uint8_t)lang;
    lang_dropdown_open = 0;
    rebuild_tree();
    render_current_doc();
    set_status(T("Language changed", "语言已更改"));
}

static int hit_rect_i(int32_t px, int32_t py, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static void handle_mouse_button(const struct leonos_gui_app_event *event)
{
    uint32_t lang_x = view_w > 156U ? view_w - 156U : 360U;
    struct leonos_ui_dropdown_item lang_items[2] = {
        {"English", OSHLP_LANG_EN, 0},
        {"中文", OSHLP_LANG_ZH, 0},
    };
    uint32_t id = 0;
    if (!event || !event->pressed || !(event->buttons & 1U)) {
        return;
    }
    if (lang_dropdown_open &&
        leonos_ui_dropdown_hit(event->x, event->y, lang_x, 34, 140,
                               lang_items, 2, 26U, 1000U, &id)) {
        handle_language_change(id);
        return;
    }
    if (hit_rect_i(event->x, event->y, 8, 9, 64, LEONOS_UI_BUTTON_H)) {
        history_back();
        return;
    }
    if (hit_rect_i(event->x, event->y, 78, 9, 76, LEONOS_UI_BUTTON_H)) {
        history_forward();
        return;
    }
    if (hit_rect_i(event->x, event->y, lang_x, 8, 140, LEONOS_UI_BUTTON_H)) {
        lang_dropdown_open = lang_dropdown_open ? 0U : 1U;
        return;
    }
    lang_dropdown_open = 0;
    if (event->x >= 12 && event->x < (int32_t)(OSHLP_TREE_W - 12U) &&
        event->y >= (int32_t)(content_top() + 4U) &&
        event->y < (int32_t)content_bottom()) {
        uint32_t tree_rows = tree_visible_rows();
        uint32_t tree_draw = tree_count > tree_scroll ? min_u32(tree_count - tree_scroll, tree_rows) : 0;
        if (leonos_ui_tree_hit(event->x, event->y, 12, content_top() + 4U,
                               OSHLP_TREE_W - 24U, &tree_items[tree_scroll],
                               tree_draw, OSHLP_TREE_ROW_H, &id)) {
            if (id > 0 && id <= doc_count) {
                navigate_doc(id - 1U, 1);
            }
        }
    }
}

static void handle_wheel(const struct leonos_gui_app_event *event)
{
    uint32_t steps;
    if (!event || event->dy == 0) {
        return;
    }
    steps = event->dy < 0 ? (uint32_t)(-event->dy) : (uint32_t)event->dy;
    if (!steps) {
        steps = 1;
    }
    if (event->x < (int32_t)OSHLP_TREE_W) {
        uint32_t rows = tree_visible_rows();
        uint32_t max_scroll = tree_count > rows ? tree_count - rows : 0;
        if (event->dy > 0) {
            tree_scroll = tree_scroll > steps ? tree_scroll - steps : 0;
        } else {
            tree_scroll = tree_scroll + steps < max_scroll ? tree_scroll + steps : max_scroll;
        }
    } else {
        uint32_t page = doc_page_lines();
        uint32_t max_scroll = render_count > page ? render_count - page : 0;
        if (event->dy > 0) {
            scroll_line = scroll_line > steps ? scroll_line - steps : 0;
        } else {
            scroll_line = scroll_line + steps < max_scroll ? scroll_line + steps : max_scroll;
        }
    }
}

static void handle_key(const struct leonos_gui_app_event *event)
{
    uint32_t page;
    if (!event || !event->pressed) {
        return;
    }
    page = doc_page_lines();
    if (event->keycode == OSHLP_KEY_ESCAPE) {
        leonos_gui_destroy_app_window((uint32_t)window_id);
        exit(0);
    } else if (event->keycode == OSHLP_KEY_UP) {
        scroll_line = scroll_line ? scroll_line - 1U : 0;
    } else if (event->keycode == OSHLP_KEY_DOWN) {
        if (scroll_line + page < render_count) {
            ++scroll_line;
        }
    } else if (event->keycode == OSHLP_KEY_PAGE_UP) {
        scroll_line = scroll_line > page ? scroll_line - page : 0;
    } else if (event->keycode == OSHLP_KEY_PAGE_DOWN) {
        uint32_t max_scroll = render_count > page ? render_count - page : 0;
        scroll_line = scroll_line + page < max_scroll ? scroll_line + page : max_scroll;
    } else if (event->keycode == LEONOS_KEY_BACKSPACE) {
        history_back();
    }
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_gui_app_event event;
    const char *path = OSHLP_DEFAULT_PATH;
    const char *doc_id = 0;
    (void)envp;
    puts("[oshlp.elf] help viewer starting");
    current_lang = leonos_i18n_language() == LEONOS_LANG_ZH ? OSHLP_LANG_ZH : OSHLP_LANG_EN;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        path = argv[1];
    }
    if (argc > 2 && argv && argv[2] && argv[2][0]) {
        doc_id = argv[2];
    }
    copy_text(status_text, sizeof(status_text), T("Loading", "正在加载"));
    window_id = leonos_gui_create_app_window_ex(T("LeonOS Help", "LeonOS 帮助"),
                                                T("Help Viewer", "帮助查看器"),
                                                view_w, view_h, 0);
    if (window_id <= 0) {
        printf("[oshlp.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, OSHLP_MAX_W);
    (void)load_help_file(path, doc_id);
    present_help();
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                handle_mouse_button(&event);
                present_help();
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                handle_wheel(&event);
                present_help();
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                handle_key(&event);
                present_help();
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width) {
                    view_w = event.width > OSHLP_MAX_W ? OSHLP_MAX_W : event.width;
                    if (view_w < OSHLP_MIN_W) {
                        view_w = OSHLP_MIN_W;
                    }
                }
                if (event.height) {
                    view_h = event.height > OSHLP_MAX_H ? OSHLP_MAX_H : event.height;
                    if (view_h < OSHLP_MIN_H) {
                        view_h = OSHLP_MIN_H;
                    }
                }
                rebuild_tree();
                render_current_doc();
                present_help();
                continue;
            }
        }
        sleep_ms(10);
    }
}
