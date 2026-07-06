#include <leonos/psf_font.h>
#include <leonos/syscall.h>
#include <stdlib.h>

#include "ui_internal.h"

#define UI_SYSTEM_FONT_MAX 8192U
#define UI_CJK_FONT_MAX (2U * 1024U * 1024U)
#define UI_CJK_FONT_PATH "0:/system/fonts/cjk16.lbf"
static uint8_t ui_system_font[UI_SYSTEM_FONT_MAX];
static uint8_t *ui_cjk_font;
static uint8_t ui_system_font_checked;
static uint8_t ui_cjk_font_checked;
static uint32_t ui_cjk_font_len;
static uint32_t ui_cjk_font_count;
static uint32_t ui_cjk_index_offset;
static uint32_t ui_cjk_bitmap_offset;
static uint32_t ui_cjk_glyph_bytes;
static struct leonos_psf_view ui_font_view;

uint32_t ui_strlen(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static const uint8_t *ui_font_glyph(char ch)
{
    if (!ui_system_font_checked) {
        struct leonos_stat st;
        ui_system_font_checked = 1;
        if (stat(LEONOS_SYSTEM_FONT_PATH, &st) == 0 &&
            st.type == LEONOS_FS_TYPE_FILE &&
            st.size > 0 && st.size <= sizeof(ui_system_font)) {
            int fd = open(LEONOS_SYSTEM_FONT_PATH, LEONOS_O_RDONLY, 0);
            if (fd >= 0) {
                uint32_t len = 0;
                while (len < st.size) {
                    long got = read(fd, ui_system_font + len, (uint32_t)st.size - len);
                    if (got <= 0) {
                        break;
                    }
                    len += (uint32_t)got;
                }
                close(fd);
                leonos_psf_view_from_memory(ui_system_font, len, &ui_font_view);
            }
        }
    }
    if (ui_font_view.glyphs) {
        return leonos_psf_view_glyph(&ui_font_view, ch);
    }
    return leonos_psf_glyph(ch);
}

static uint16_t ui_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ui_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ui_load_cjk_font(void)
{
    if (ui_cjk_font_checked) {
        return;
    }
    ui_cjk_font_checked = 1;
    struct leonos_stat st;
    if (stat(UI_CJK_FONT_PATH, &st) != 0 ||
        st.type != LEONOS_FS_TYPE_FILE ||
        st.size < 24 || st.size > UI_CJK_FONT_MAX) {
        return;
    }
    ui_cjk_font = malloc((size_t)st.size);
    if (!ui_cjk_font) {
        return;
    }
    int fd = open(UI_CJK_FONT_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        free(ui_cjk_font);
        ui_cjk_font = 0;
        return;
    }
    uint32_t len = 0;
    while (len < st.size) {
        long got = read(fd, ui_cjk_font + len, (uint32_t)st.size - len);
        if (got <= 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    if (len < 24 ||
        ui_cjk_font[0] != 'L' || ui_cjk_font[1] != 'B' ||
        ui_cjk_font[2] != 'F' || ui_cjk_font[3] != '1' ||
        ui_read_le16(ui_cjk_font + 4) != 16 ||
        ui_read_le16(ui_cjk_font + 6) != 16) {
        free(ui_cjk_font);
        ui_cjk_font = 0;
        return;
    }
    ui_cjk_glyph_bytes = ui_read_le16(ui_cjk_font + 8);
    ui_cjk_font_count = ui_read_le32(ui_cjk_font + 12);
    ui_cjk_index_offset = ui_read_le32(ui_cjk_font + 16);
    ui_cjk_bitmap_offset = ui_read_le32(ui_cjk_font + 20);
    if (ui_cjk_glyph_bytes != 32 ||
        ui_cjk_index_offset + ui_cjk_font_count * 8u > len ||
        ui_cjk_bitmap_offset > len) {
        ui_cjk_font_count = 0;
        free(ui_cjk_font);
        ui_cjk_font = 0;
        return;
    }
    ui_cjk_font_len = len;
}

static const uint8_t *ui_cjk_glyph(uint32_t codepoint)
{
    ui_load_cjk_font();
    if (!ui_cjk_font_count) {
        return 0;
    }
    uint32_t lo = 0;
    uint32_t hi = ui_cjk_font_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const uint8_t *entry = ui_cjk_font + ui_cjk_index_offset + mid * 8u;
        uint32_t cp = ui_read_le32(entry);
        uint32_t off = ui_read_le32(entry + 4);
        if (cp == codepoint) {
            if (off + ui_cjk_glyph_bytes <= ui_cjk_font_len) {
                return ui_cjk_font + off;
            }
            return 0;
        }
        if (cp < codepoint) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return 0;
}

static int ui_utf8_cont(uint8_t byte)
{
    return (byte & 0xc0u) == 0x80u;
}

uint32_t ui_decode_utf8(const char *text, uint32_t len,
                               uint32_t off, uint32_t *byte_len)
{
    const uint8_t *s = (const uint8_t *)text;
    uint8_t b0;
    if (byte_len) {
        *byte_len = 1;
    }
    if (!text || off >= len) {
        return LEONOS_TEXT_REPLACEMENT_CHAR;
    }
    b0 = s[off];
    if (b0 < 0x80u) {
        return b0;
    }
    if (b0 < 0xc2u) {
        return LEONOS_TEXT_REPLACEMENT_CHAR;
    }
    if (b0 < 0xe0u) {
        if (off + 1u >= len || !ui_utf8_cont(s[off + 1u])) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 2;
        }
        return ((uint32_t)(b0 & 0x1fu) << 6) | (uint32_t)(s[off + 1u] & 0x3fu);
    }
    if (b0 < 0xf0u) {
        uint8_t b1;
        uint8_t b2;
        if (off + 2u >= len) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        if (!ui_utf8_cont(b1) || !ui_utf8_cont(b2) ||
            (b0 == 0xe0u && b1 < 0xa0u) ||
            (b0 == 0xedu && b1 >= 0xa0u)) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 3;
        }
        return ((uint32_t)(b0 & 0x0fu) << 12) |
               ((uint32_t)(b1 & 0x3fu) << 6) |
               (uint32_t)(b2 & 0x3fu);
    }
    if (b0 < 0xf5u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (off + 3u >= len) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        b3 = s[off + 3u];
        if (!ui_utf8_cont(b1) || !ui_utf8_cont(b2) || !ui_utf8_cont(b3) ||
            (b0 == 0xf0u && b1 < 0x90u) ||
            (b0 == 0xf4u && b1 >= 0x90u)) {
            return LEONOS_TEXT_REPLACEMENT_CHAR;
        }
        if (byte_len) {
            *byte_len = 4;
        }
        return ((uint32_t)(b0 & 0x07u) << 18) |
               ((uint32_t)(b1 & 0x3fu) << 12) |
               ((uint32_t)(b2 & 0x3fu) << 6) |
               (uint32_t)(b3 & 0x3fu);
    }
    return LEONOS_TEXT_REPLACEMENT_CHAR;
}

static int ui_is_wide_codepoint(uint32_t cp)
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

uint32_t ui_cell_width(uint32_t cp)
{
    if (cp == 0 || cp == '\n' || cp == '\r') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    return ui_is_wide_codepoint(cp) ? 2u : 1u;
}

int ui_layout_utf8(const char *text, uint32_t byte_len,
                          struct leonos_text_glyph *glyphs, uint32_t capacity,
                          struct leonos_text_layout *out)
{
    if (!text) {
        if (out) {
            out->text = text;
            out->byte_len = 0;
            out->capacity = capacity;
            out->count = 0;
            out->total_cells = 0;
            out->total_px = 0;
            out->glyphs = glyphs;
        }
        return 0;
    }
    if (byte_len == 0) {
        byte_len = ui_strlen(text);
    }
    if (leonos_text_layout_utf8(text, byte_len, glyphs, capacity, out) == 0) {
        return 0;
    }
    uint32_t off = 0;
    uint32_t count = 0;
    uint32_t cells = 0;
    while (off < byte_len) {
        uint32_t len = 1;
        uint32_t cp = ui_decode_utf8(text, byte_len, off, &len);
        uint32_t cw = ui_cell_width(cp);
        if (count < capacity) {
            glyphs[count].codepoint = cp;
            glyphs[count].byte_offset = off;
            glyphs[count].byte_len = len;
            glyphs[count].cell_width = cw;
            glyphs[count].pixel_width = cw * LEONOS_FONT_W;
        }
        cells += cw;
        ++count;
        off += len;
    }
    if (out) {
        out->text = text;
        out->byte_len = byte_len;
        out->capacity = capacity;
        out->count = count;
        out->total_cells = cells;
        out->total_px = cells * LEONOS_FONT_W;
        out->glyphs = glyphs;
    }
    return 0;
}

uint32_t ui_next_codepoint_offset(const char *text, uint32_t len, uint32_t pos)
{
    uint32_t byte_len = 1;
    if (!text || pos >= len) {
        return len;
    }
    (void)ui_decode_utf8(text, len, pos, &byte_len);
    if (byte_len == 0) {
        byte_len = 1;
    }
    pos += byte_len;
    return pos > len ? len : pos;
}

uint32_t ui_prev_codepoint_offset(const char *text, uint32_t pos)
{
    uint32_t prev = 0;
    uint32_t cur = 0;
    uint32_t len = ui_strlen(text);
    if (!text || pos == 0) {
        return 0;
    }
    if (pos > len) {
        pos = len;
    }
    while (cur < pos) {
        prev = cur;
        cur = ui_next_codepoint_offset(text, len, cur);
        if (cur <= prev) {
            break;
        }
    }
    return prev;
}

uint32_t ui_text_cells_between(const char *text, uint32_t start, uint32_t end)
{
    uint32_t cells = 0;
    uint32_t len = ui_strlen(text);
    if (!text) {
        return 0;
    }
    if (start > len) {
        start = len;
    }
    if (end > len) {
        end = len;
    }
    while (start < end) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(text, len, start, &byte_len);
        cells += ui_cell_width(cp);
        start += byte_len ? byte_len : 1u;
    }
    return cells;
}

uint32_t ui_byte_offset_for_cell(const char *text, uint32_t len,
                                        uint32_t start, uint32_t target_cell)
{
    uint32_t pos = start;
    uint32_t cell = 0;
    while (pos < len) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(text, len, pos, &byte_len);
        uint32_t cw = ui_cell_width(cp);
        if (cell + cw > target_cell) {
            return pos;
        }
        cell += cw;
        pos += byte_len ? byte_len : 1u;
        if (cell >= target_cell) {
            return pos;
        }
    }
    return len;
}

uint32_t leonos_ui_text_width(const char *text)
{
    struct leonos_text_layout layout;
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    ui_layout_utf8(text, 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    return layout.total_px;
}

uint32_t leonos_ui_text_fit_chars(uint32_t pixel_width)
{
    return pixel_width / LEONOS_FONT_W;
}

void ui_char(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    char ch, uint32_t fg, uint32_t bg, int transparent)
{
    const uint8_t *glyph = ui_font_glyph(ch);
    for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
        for (uint32_t col = 0; col < LEONOS_FONT_W; ++col) {
            if (glyph[row] & (uint8_t)(0x80u >> col)) {
                leonos_ui_pixel(surface, x + col, y + row, fg);
            } else if (!transparent) {
                leonos_ui_pixel(surface, x + col, y + row, bg);
            }
        }
    }
}

static void ui_tofu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t fg, uint32_t bg, int transparent, uint32_t pixels_w)
{
    if (!transparent) {
        leonos_ui_rect(surface, x, y, pixels_w, LEONOS_FONT_H, bg);
    }
    if (pixels_w < 4) {
        return;
    }
    for (uint32_t col = 1; col + 1 < pixels_w; ++col) {
        leonos_ui_pixel(surface, x + col, y + 1, fg);
        leonos_ui_pixel(surface, x + col, y + LEONOS_FONT_H - 2, fg);
    }
    for (uint32_t row = 1; row + 1 < LEONOS_FONT_H; ++row) {
        leonos_ui_pixel(surface, x + 1, y + row, fg);
        leonos_ui_pixel(surface, x + pixels_w - 2, y + row, fg);
    }
}

static void ui_cjk_char(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t codepoint, uint32_t fg, uint32_t bg, int transparent)
{
    const uint8_t *glyph = ui_cjk_glyph(codepoint);
    if (!glyph) {
        ui_tofu(surface, x, y, fg, bg, transparent, LEONOS_FONT_W * 2u);
        return;
    }
    for (uint32_t row = 0; row < 16; ++row) {
        uint8_t hi = glyph[row * 2u];
        uint8_t lo = glyph[row * 2u + 1u];
        uint16_t bits = ((uint16_t)hi << 8) | lo;
        for (uint32_t col = 0; col < 16; ++col) {
            if (bits & (uint16_t)(0x8000u >> col)) {
                leonos_ui_pixel(surface, x + col, y + row, fg);
            } else if (!transparent) {
                leonos_ui_pixel(surface, x + col, y + row, bg);
            }
        }
    }
}

void ui_codepoint(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t codepoint, uint32_t cell_width,
                         uint32_t fg, uint32_t bg, int transparent)
{
    uint32_t pixels_w = cell_width * LEONOS_FONT_W;
    if (cell_width == 0) {
        return;
    }
    if (codepoint == '\t') {
        if (!transparent) {
            leonos_ui_rect(surface, x, y, pixels_w, LEONOS_FONT_H, bg);
        }
        return;
    }
    if (codepoint >= 32u && codepoint < 127u && cell_width == 1u) {
        ui_char(surface, x, y, (char)codepoint, fg, bg, transparent);
        return;
    }
    if (cell_width >= 2u) {
        ui_cjk_char(surface, x, y, codepoint, fg, bg, transparent);
        return;
    }
    ui_char(surface, x, y, '?', fg, bg, transparent);
}

static void ui_tofu_resized(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t pixels_w, uint32_t pixels_h,
                            uint32_t fg, uint32_t bg)
{
    if (!pixels_w || !pixels_h) {
        return;
    }
    leonos_ui_rect(surface, x, y, pixels_w, pixels_h, bg);
    if (pixels_w < 4 || pixels_h < 4) {
        return;
    }
    leonos_ui_rect(surface, x + 1, y + 1, pixels_w - 2, 1, fg);
    leonos_ui_rect(surface, x + 1, y + pixels_h - 2, pixels_w - 2, 1, fg);
    leonos_ui_rect(surface, x + 1, y + 1, 1, pixels_h - 2, fg);
    leonos_ui_rect(surface, x + pixels_w - 2, y + 1, 1, pixels_h - 2, fg);
}

static void ui_draw_bitmap_resized(struct leonos_ui_surface *surface,
                                   uint32_t x, uint32_t y,
                                   uint32_t dst_w, uint32_t dst_h,
                                   uint32_t src_w, uint32_t src_h,
                                   const uint8_t *bitmap, uint8_t cjk,
                                   uint32_t fg, uint32_t bg)
{
    if (!surface || !dst_w || !dst_h || !src_w || !src_h || !bitmap) {
        return;
    }
    for (uint32_t yy = 0; yy < dst_h; ++yy) {
        uint32_t sy = yy * src_h / dst_h;
        for (uint32_t xx = 0; xx < dst_w; ++xx) {
            uint32_t sx = xx * src_w / dst_w;
            uint8_t on;
            if (cjk) {
                uint16_t bits = ((uint16_t)bitmap[sy * 2U] << 8) |
                                (uint16_t)bitmap[sy * 2U + 1U];
                on = (bits & (uint16_t)(0x8000U >> sx)) ? 1U : 0U;
            } else {
                on = (bitmap[sy] & (uint8_t)(0x80U >> sx)) ? 1U : 0U;
            }
            leonos_ui_pixel(surface, x + xx, y + yy, on ? fg : bg);
        }
    }
}

static void ui_codepoint_resized(struct leonos_ui_surface *surface,
                                 uint32_t x, uint32_t y,
                                 uint32_t codepoint, uint32_t cell_width,
                                 uint32_t fg, uint32_t bg,
                                 uint32_t cell_w, uint32_t cell_h)
{
    uint32_t pixels_w = cell_width * cell_w;
    if (cell_width == 0 || !cell_w || !cell_h) {
        return;
    }
    if (codepoint == '\t') {
        leonos_ui_rect(surface, x, y, pixels_w, cell_h, bg);
        return;
    }
    if (codepoint >= 32U && codepoint < 127U && cell_width == 1U) {
        ui_draw_bitmap_resized(surface, x, y, pixels_w, cell_h,
                               LEONOS_FONT_W, LEONOS_FONT_H,
                               ui_font_glyph((char)codepoint), 0,
                               fg, bg);
        return;
    }
    if (cell_width >= 2U) {
        const uint8_t *glyph = ui_cjk_glyph(codepoint);
        if (glyph) {
            ui_draw_bitmap_resized(surface, x, y, pixels_w, cell_h,
                                   16U, 16U, glyph, 1, fg, bg);
        } else {
            ui_tofu_resized(surface, x, y, pixels_w, cell_h, fg, bg);
        }
        return;
    }
    ui_draw_bitmap_resized(surface, x, y, pixels_w, cell_h,
                           LEONOS_FONT_W, LEONOS_FONT_H,
                           ui_font_glyph('?'), 0, fg, bg);
}

static void ui_draw_layout_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                uint32_t w, const char *text,
                                uint32_t fg, uint32_t bg,
                                int transparent, int clipped)
{
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    struct leonos_text_layout layout;
    uint32_t draw_x = x;
    uint32_t count;
    if (!transparent && clipped) {
        leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H, bg);
    }
    ui_layout_utf8(text ? text : "", 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    count = layout.count < UI_LAYOUT_GLYPH_MAX ? layout.count : UI_LAYOUT_GLYPH_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t px = glyphs[i].pixel_width;
        if (clipped && draw_x + px > x + w) {
            break;
        }
        ui_codepoint(surface, draw_x, y, glyphs[i].codepoint, glyphs[i].cell_width,
                     fg, bg, transparent || clipped);
        draw_x += px;
    }
}

void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    const char *text, uint32_t fg, uint32_t bg)
{
    ui_draw_layout_text(surface, x, y, 0, text, fg, bg, 0, 0);
}

void leonos_ui_text_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const char *text, uint32_t fg, uint32_t bg)
{
    ui_draw_layout_text(surface, x, y, w, text, fg, bg, 0, 1);
}

void leonos_ui_text_resized_clipped(struct leonos_ui_surface *surface,
                                    uint32_t x, uint32_t y, uint32_t w,
                                    const char *text, uint32_t fg, uint32_t bg,
                                    uint32_t cell_w, uint32_t cell_h)
{
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    struct leonos_text_layout layout;
    uint32_t draw_x = x;
    uint32_t count;
    if (!cell_w) {
        cell_w = LEONOS_FONT_W;
    }
    if (!cell_h) {
        cell_h = LEONOS_FONT_H;
    }
    if (cell_w == LEONOS_FONT_W && cell_h == LEONOS_FONT_H) {
        leonos_ui_text_clipped(surface, x, y, w, text, fg, bg);
        return;
    }
    leonos_ui_rect(surface, x, y, w, cell_h, bg);
    ui_layout_utf8(text ? text : "", 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    count = layout.count < UI_LAYOUT_GLYPH_MAX ? layout.count : UI_LAYOUT_GLYPH_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t px = glyphs[i].cell_width * cell_w;
        if (draw_x + px > x + w) {
            break;
        }
        ui_codepoint_resized(surface, draw_x, y, glyphs[i].codepoint,
                             glyphs[i].cell_width, fg, bg, cell_w, cell_h);
        draw_x += px;
    }
}

void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                const char *text, uint32_t fg)
{
    ui_draw_layout_text(surface, x, y, 0, text, fg, 0, 1, 0);
}

void leonos_ui_text_transparent_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                        uint32_t w, const char *text, uint32_t fg)
{
    ui_draw_layout_text(surface, x, y, w, text, fg, 0, 1, 1);
}
