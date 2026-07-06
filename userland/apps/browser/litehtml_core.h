#ifndef LEONOS_BROWSER_LITEHTML_CORE_H
#define LEONOS_BROWSER_LITEHTML_CORE_H

#include <leonos/fs.h>
#include <stdint.h>

#define BROWSER_COLOR_UNSET 0xffffffffu

enum browser_line_kind {
    BROWSER_LINE_NORMAL = 0,
    BROWSER_LINE_HEADING1 = 1,
    BROWSER_LINE_HEADING2 = 2,
    BROWSER_LINE_HEADING3 = 3,
    BROWSER_LINE_MUTED = 4,
    BROWSER_LINE_BLOCKQUOTE = 5,
    BROWSER_LINE_TABLE = 6,
    BROWSER_LINE_HR = 7,
    BROWSER_LINE_IMAGE = 8,
};

enum browser_text_style {
    BROWSER_TEXT_BOLD = 0x01,
    BROWSER_TEXT_ITALIC = 0x02,
    BROWSER_TEXT_CODE = 0x04,
    BROWSER_TEXT_UNDERLINE = 0x08,
};

enum browser_text_align {
    BROWSER_ALIGN_LEFT = 0,
    BROWSER_ALIGN_CENTER = 1,
    BROWSER_ALIGN_RIGHT = 2,
};

struct browser_line {
    char text[176];
    uint8_t link[176];
    uint8_t style[176];
    uint32_t fg[176];
    uint32_t bg[176];
    uint32_t len;
    uint8_t kind;
    uint8_t indent;
    uint8_t align;
    uint32_t line_bg;
    uint32_t border_color;
};

struct browser_link {
    char href[LEONOS_FS_PATH_LEN];
};

struct litehtml_core_view {
    struct browser_line *lines;
    uint32_t max_lines;
    uint32_t line_chars;
    struct browser_link *links;
    uint32_t max_links;
    uint32_t *line_count;
    uint32_t *link_count;
    uint32_t *scroll_line;
    char *page_title;
    uint32_t page_title_cap;
    uint8_t *source_truncated;
    uint32_t cols;
};

void litehtml_core_render_html(struct litehtml_core_view *view,
                               const char *source,
                               const char *base_url);
void litehtml_core_render_plain(struct litehtml_core_view *view,
                                const char *source);

#endif
