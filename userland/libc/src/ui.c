#include <leonos/gui.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/syscall.h>
#include <leonos/text.h>
#include <leonos/ui.h>

#define UI_SYSTEM_FONT_MAX 8192U
#define UI_CJK_FONT_MAX 131072U
#define UI_LAYOUT_GLYPH_MAX 512U
#define UI_CJK_FONT_PATH "0:/system/fonts/cjk16.lbf"

static uint8_t ui_system_font[UI_SYSTEM_FONT_MAX];
static uint8_t ui_cjk_font[UI_CJK_FONT_MAX];
static uint8_t ui_system_font_checked;
static uint8_t ui_cjk_font_checked;
static uint32_t ui_cjk_font_len;
static uint32_t ui_cjk_font_count;
static uint32_t ui_cjk_index_offset;
static uint32_t ui_cjk_bitmap_offset;
static uint32_t ui_cjk_glyph_bytes;
static struct leonos_psf_view ui_font_view;

static uint32_t ui_strlen(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static uint8_t ui_shift_down;

static int ui_is_shift_key(uint8_t keycode)
{
    return keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT;
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
        st.size < 24 || st.size > sizeof(ui_cjk_font)) {
        return;
    }
    int fd = open(UI_CJK_FONT_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
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

static uint32_t ui_decode_utf8(const char *text, uint32_t len,
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
           (cp >= 0xfe10u && cp <= 0xfe19u) ||
           (cp >= 0xfe30u && cp <= 0xfe6fu) ||
           (cp >= 0xff00u && cp <= 0xff60u) ||
           (cp >= 0xffe0u && cp <= 0xffe6u);
}

static uint32_t ui_cell_width(uint32_t cp)
{
    if (cp == 0 || cp == '\n' || cp == '\r') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    return ui_is_wide_codepoint(cp) ? 2u : 1u;
}

static int ui_layout_utf8(const char *text, uint32_t byte_len,
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

static uint32_t ui_next_codepoint_offset(const char *text, uint32_t len, uint32_t pos)
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

static uint32_t ui_prev_codepoint_offset(const char *text, uint32_t pos)
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

static uint32_t ui_text_cells_between(const char *text, uint32_t start, uint32_t end)
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

static uint32_t ui_byte_offset_for_cell(const char *text, uint32_t len,
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

int leonos_ui_hit(uint32_t px, uint32_t py, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    return (int32_t)px >= x && (int32_t)py >= y &&
           (int32_t)px < x + (int32_t)w &&
           (int32_t)py < y + (int32_t)h;
}

int leonos_ui_keycode_to_char(uint8_t keycode, char *out)
{
    return leonos_ui_keycode_to_char_shift(keycode, ui_shift_down, out);
}

int leonos_ui_keycode_to_char_shift(uint8_t keycode, uint8_t shifted, char *out)
{
    if (!out) {
        return 0;
    }
    switch (keycode) {
    case LEONOS_KEY_BACKSPACE: *out = '\b'; return 1;
    case LEONOS_KEY_TAB: *out = '\t'; return 1;
    case LEONOS_KEY_ENTER: *out = '\n'; return 1;
    case 2: *out = shifted ? '!' : '1'; return 1;
    case 3: *out = shifted ? '@' : '2'; return 1;
    case 4: *out = shifted ? '#' : '3'; return 1;
    case 5: *out = shifted ? '$' : '4'; return 1;
    case 6: *out = shifted ? '%' : '5'; return 1;
    case 7: *out = shifted ? '^' : '6'; return 1;
    case 8: *out = shifted ? '&' : '7'; return 1;
    case 9: *out = shifted ? '*' : '8'; return 1;
    case 10: *out = shifted ? '(' : '9'; return 1;
    case 11: *out = shifted ? ')' : '0'; return 1;
    case 12: *out = shifted ? '_' : '-'; return 1;
    case 13: *out = shifted ? '+' : '='; return 1;
    case 16: *out = shifted ? 'Q' : 'q'; return 1;
    case 17: *out = shifted ? 'W' : 'w'; return 1;
    case 18: *out = shifted ? 'E' : 'e'; return 1;
    case 19: *out = shifted ? 'R' : 'r'; return 1;
    case 20: *out = shifted ? 'T' : 't'; return 1;
    case 21: *out = shifted ? 'Y' : 'y'; return 1;
    case 22: *out = shifted ? 'U' : 'u'; return 1;
    case 23: *out = shifted ? 'I' : 'i'; return 1;
    case 24: *out = shifted ? 'O' : 'o'; return 1;
    case 25: *out = shifted ? 'P' : 'p'; return 1;
    case 26: *out = shifted ? '{' : '['; return 1;
    case 27: *out = shifted ? '}' : ']'; return 1;
    case 30: *out = shifted ? 'A' : 'a'; return 1;
    case 31: *out = shifted ? 'S' : 's'; return 1;
    case 32: *out = shifted ? 'D' : 'd'; return 1;
    case 33: *out = shifted ? 'F' : 'f'; return 1;
    case 34: *out = shifted ? 'G' : 'g'; return 1;
    case 35: *out = shifted ? 'H' : 'h'; return 1;
    case 36: *out = shifted ? 'J' : 'j'; return 1;
    case 37: *out = shifted ? 'K' : 'k'; return 1;
    case 38: *out = shifted ? 'L' : 'l'; return 1;
    case 39: *out = shifted ? ':' : ';'; return 1;
    case 40: *out = shifted ? '"' : '\''; return 1;
    case 41: *out = shifted ? '~' : '`'; return 1;
    case 43: *out = shifted ? '|' : '\\'; return 1;
    case 44: *out = shifted ? 'Z' : 'z'; return 1;
    case 45: *out = shifted ? 'X' : 'x'; return 1;
    case 46: *out = shifted ? 'C' : 'c'; return 1;
    case 47: *out = shifted ? 'V' : 'v'; return 1;
    case 48: *out = shifted ? 'B' : 'b'; return 1;
    case 49: *out = shifted ? 'N' : 'n'; return 1;
    case 50: *out = shifted ? 'M' : 'm'; return 1;
    case 51: *out = shifted ? '<' : ','; return 1;
    case 52: *out = shifted ? '>' : '.'; return 1;
    case 53: *out = shifted ? '?' : '/'; return 1;
    case LEONOS_KEY_SPACE: *out = ' '; return 1;
    default:
        return 0;
    }
}
void leonos_ui_bind(struct leonos_ui_surface *surface, uint32_t *pixels,
                    uint32_t width, uint32_t height, uint32_t stride)
{
    if (!surface) {
        return;
    }
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride ? stride : width;
}

void leonos_ui_pixel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t color)
{
    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
        return;
    }
    surface->pixels[(uint64_t)y * surface->stride + x] = color;
}

void leonos_ui_rect(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color)
{
    if (!surface || !surface->pixels || x >= surface->width || y >= surface->height) {
        return;
    }
    if (x + w > surface->width) {
        w = surface->width - x;
    }
    if (y + h > surface->height) {
        h = surface->height - y;
    }
    for (uint32_t yy = y; yy < y + h; ++yy) {
        uint32_t *row = surface->pixels + (uint64_t)yy * surface->stride;
        for (uint32_t xx = x; xx < x + w; ++xx) {
            row[xx] = color;
        }
    }
}

static void ui_char(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
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

static void ui_codepoint(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
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

void leonos_ui_bevel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill, uint32_t flags)
{
    int pressed = (flags & LEONOS_UI_BUTTON_PRESSED) != 0;
    uint32_t tl = pressed ? LEONOS_UI_DARK : LEONOS_UI_WHITE;
    uint32_t br = pressed ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;

    leonos_ui_rect(surface, x, y, w, h, fill);
    leonos_ui_rect(surface, x, y, w, 1, tl);
    leonos_ui_rect(surface, x, y, 1, h, tl);
    leonos_ui_rect(surface, x + w - 1, y, 1, h, br);
    leonos_ui_rect(surface, x, y + h - 1, w, 1, br);
    if (w > 3 && h > 3) {
        leonos_ui_rect(surface, x + 1, y + 1, w - 2, 1, pressed ? LEONOS_UI_BLACK : LEONOS_UI_LIGHT);
        leonos_ui_rect(surface, x + 1, y + 1, 1, h - 2, pressed ? LEONOS_UI_BLACK : LEONOS_UI_LIGHT);
        leonos_ui_rect(surface, x + w - 2, y + 1, 1, h - 2, pressed ? LEONOS_UI_LIGHT : LEONOS_UI_DARK);
        leonos_ui_rect(surface, x + 1, y + h - 2, w - 2, 1, pressed ? LEONOS_UI_LIGHT : LEONOS_UI_DARK);
    }
}

void leonos_ui_inset(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill)
{
    leonos_ui_rect(surface, x, y, w, h, fill);
    leonos_ui_rect(surface, x, y, w, 1, LEONOS_UI_DARK);
    leonos_ui_rect(surface, x, y, 1, h, LEONOS_UI_DARK);
    leonos_ui_rect(surface, x + w - 1, y, 1, h, LEONOS_UI_WHITE);
    leonos_ui_rect(surface, x, y + h - 1, w, 1, LEONOS_UI_WHITE);
}

void leonos_ui_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *label, uint32_t flags)
{
    uint32_t fill = (flags & LEONOS_UI_BUTTON_DISABLED) ? LEONOS_UI_LIGHT : LEONOS_UI_GRAY;
    uint32_t pressed = flags & LEONOS_UI_BUTTON_PRESSED;
    leonos_ui_bevel(surface, x, y, w, h, fill, pressed);
    if (!label) {
        return;
    }
    uint32_t text_w = leonos_ui_text_width(label);
    uint32_t tx = text_w < w ? x + (w - text_w) / 2 : x + 4;
    uint32_t ty = LEONOS_FONT_H < h ? y + (h - LEONOS_FONT_H) / 2 : y + 2;
    if (pressed) {
        ++tx;
        ++ty;
    }
    leonos_ui_text_transparent_clipped(surface, tx, ty, w > 8 ? w - 8 : w, label,
                                       (flags & LEONOS_UI_BUTTON_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK);
}

void leonos_ui_window_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                             char label, uint32_t flags)
{
    char text[2] = {label, 0};
    leonos_ui_button(surface, x, y, LEONOS_UI_WINDOW_BUTTON_W, LEONOS_UI_WINDOW_BUTTON_H, text, flags);
}

void leonos_ui_window(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title, uint32_t flags,
                      struct leonos_ui_window_parts *parts)
{
    leonos_ui_window_ex(surface, x, y, w, h, title, 'M', flags, parts);
}

void leonos_ui_window_ex(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *title, char maximize_label,
                         uint32_t flags, struct leonos_ui_window_parts *parts)
{
    uint32_t active = flags & LEONOS_UI_WINDOW_ACTIVE;
    uint32_t no_resize = flags & LEONOS_UI_WINDOW_NO_RESIZE;
    uint32_t title_color = active ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_INACTIVE_TITLE;
    leonos_ui_bevel(surface, x, y, w, h, LEONOS_UI_GRAY, 0);
    leonos_ui_rect(surface, x + 4, y + 4, w > 8 ? w - 8 : 0, LEONOS_UI_TITLEBAR_H, title_color);
    leonos_ui_text(surface, x + 10, y + 9, title, LEONOS_UI_WHITE, title_color);

    uint32_t bx = x + w - 64;
    uint32_t by = y + 6;
    leonos_ui_window_button(surface, bx, by, '_', 0);
    leonos_ui_window_button(surface, bx + 20, by, maximize_label,
                            no_resize ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_window_button(surface, bx + 40, by, 'X', 0);

    uint32_t body_x = x + 8;
    uint32_t body_y = y + LEONOS_UI_TITLEBAR_H + 10;
    uint32_t body_w = w > 16 ? w - 16 : 0;
    uint32_t body_h = h > LEONOS_UI_TITLEBAR_H + 18 ? h - LEONOS_UI_TITLEBAR_H - 18 : 0;
    leonos_ui_inset(surface, body_x, body_y, body_w, body_h, LEONOS_UI_WHITE);

    if (parts) {
        parts->titlebar = (struct leonos_ui_rect){(int32_t)x + 4, (int32_t)y + 4, w > 8 ? w - 8 : 0, LEONOS_UI_TITLEBAR_H};
        parts->body = (struct leonos_ui_rect){(int32_t)body_x, (int32_t)body_y, body_w, body_h};
        parts->minimize = (struct leonos_ui_rect){(int32_t)bx, (int32_t)by, LEONOS_UI_WINDOW_BUTTON_W, LEONOS_UI_WINDOW_BUTTON_H};
        parts->maximize = (struct leonos_ui_rect){(int32_t)bx + 20, (int32_t)by, LEONOS_UI_WINDOW_BUTTON_W, LEONOS_UI_WINDOW_BUTTON_H};
        parts->close = (struct leonos_ui_rect){(int32_t)bx + 40, (int32_t)by, LEONOS_UI_WINDOW_BUTTON_W, LEONOS_UI_WINDOW_BUTTON_H};
    }
}

void leonos_ui_taskbar(struct leonos_ui_surface *surface, uint32_t y, uint32_t h)
{
    leonos_ui_rect(surface, 0, y, surface ? surface->width : 0, h, LEONOS_UI_GRAY);
    leonos_ui_rect(surface, 0, y, surface ? surface->width : 0, 2, LEONOS_UI_WHITE);
    leonos_ui_rect(surface, 0, y + 2, surface ? surface->width : 0, 1, LEONOS_UI_DARK);
}

void leonos_ui_taskbar_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, const char *label, uint32_t flags)
{
    leonos_ui_button(surface, x, y, w, LEONOS_UI_BUTTON_H, label,
                     (flags & LEONOS_UI_BUTTON_ACTIVE) ? LEONOS_UI_BUTTON_PRESSED : 0);
}

void leonos_ui_menu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    leonos_ui_bevel(surface, x, y, w, h, LEONOS_UI_GRAY, 0);
    if (h > 8) {
        leonos_ui_rect(surface, x + 4, y + 4, 26, h - 8, LEONOS_UI_ACTIVE_TITLE);
    }
}

void leonos_ui_menu_item(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, const char *label, uint32_t flags)
{
    uint32_t disabled = flags & LEONOS_UI_MENU_DISABLED;
    if (flags & LEONOS_UI_MENU_SEPARATOR) {
        leonos_ui_rect(surface, x, y + 9, w, 1, LEONOS_UI_DARK);
        leonos_ui_rect(surface, x, y + 10, w, 1, LEONOS_UI_WHITE);
        return;
    }
    if ((flags & LEONOS_UI_MENU_SELECTED) && !disabled) {
        leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_ACTIVE_TITLE);
        leonos_ui_text_transparent(surface, x + 4, y + 4, label, LEONOS_UI_WHITE);
    } else {
        leonos_ui_text_transparent(surface, x + 4, y + 4, label,
                                   disabled ? LEONOS_UI_DARK : LEONOS_UI_BLACK);
    }
}

uint32_t leonos_ui_context_menu_height(uint32_t count)
{
    return 8 + count * (LEONOS_FONT_H + 8);
}

void leonos_ui_context_menu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const struct leonos_ui_context_menu_item *items,
                            uint32_t count)
{
    leonos_ui_context_menu_animated(surface, x, y, w, items, count, 1000);
}

void leonos_ui_context_menu_animated(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                     uint32_t w, const struct leonos_ui_context_menu_item *items,
                                     uint32_t count, uint32_t progress)
{
    uint32_t row_h = LEONOS_FONT_H + 8;
    uint32_t h = leonos_ui_context_menu_height(count);
    uint32_t visible_h;
    if (progress > 1000) {
        progress = 1000;
    }
    progress = (progress * (2000 - progress)) / 1000;
    visible_h = (h * progress + 999) / 1000;
    if (!visible_h) {
        return;
    }
    leonos_ui_bevel(surface, x, y, w, visible_h, LEONOS_UI_GRAY, 0);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row_y = y + 4 + i * row_h;
        if (row_y + row_h > y + visible_h - 2) {
            break;
        }
        uint32_t flags = items ? items[i].flags : LEONOS_UI_MENU_DISABLED;
        const char *label = items ? items[i].label : "";
        if (flags & LEONOS_UI_MENU_SEPARATOR) {
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2, w > 8 ? w - 8 : w,
                           1, LEONOS_UI_DARK);
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2 + 1,
                           w > 8 ? w - 8 : w, 1, LEONOS_UI_WHITE);
            continue;
        }
        leonos_ui_text_transparent_clipped(surface, x + 8, row_y + 4,
                                           w > 16 ? w - 16 : w,
                                           label ? label : "",
                                           (flags & LEONOS_UI_MENU_DISABLED)
                                               ? LEONOS_UI_DARK
                                               : LEONOS_UI_BLACK);
    }
}

int leonos_ui_context_menu_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                               uint32_t w, const struct leonos_ui_context_menu_item *items,
                               uint32_t count, uint32_t *out_id)
{
    uint32_t row_h = LEONOS_FONT_H + 8;
    uint32_t h = leonos_ui_context_menu_height(count);
    uint32_t index;
    if (out_id) {
        *out_id = 0;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, h)) {
        return 0;
    }
    if (py < (int32_t)y + 4) {
        return 1;
    }
    index = ((uint32_t)py - y - 4) / row_h;
    if (!items || index >= count ||
        (items[index].flags & (LEONOS_UI_MENU_SEPARATOR | LEONOS_UI_MENU_DISABLED))) {
        return 1;
    }
    if (out_id) {
        *out_id = items[index].id;
    }
    return 1;
}

void leonos_ui_layout_begin(struct leonos_ui_layout *layout, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t gap)
{
    if (!layout) {
        return;
    }
    layout->x = x;
    layout->y = y;
    layout->w = w;
    layout->h = h;
    layout->gap = gap;
    layout->cursor_x = x;
    layout->cursor_y = y;
    layout->row_h = 0;
}

struct leonos_ui_rect leonos_ui_layout_next(struct leonos_ui_layout *layout,
                                            uint32_t preferred_w, uint32_t preferred_h)
{
    struct leonos_ui_rect rect = {0, 0, 0, 0};
    uint32_t right;
    if (!layout || layout->w == 0 || layout->h == 0) {
        return rect;
    }
    if (preferred_h == 0) {
        preferred_h = LEONOS_UI_BUTTON_H;
    }
    if (preferred_w == 0 || preferred_w > layout->w) {
        preferred_w = layout->w;
    }
    right = layout->x + layout->w;
    if (layout->cursor_x != layout->x &&
        layout->cursor_x + preferred_w > right) {
        layout->cursor_x = layout->x;
        layout->cursor_y += layout->row_h + layout->gap;
        layout->row_h = 0;
    }
    if (layout->cursor_y >= layout->y + layout->h) {
        return rect;
    }
    if (layout->cursor_y + preferred_h > layout->y + layout->h) {
        preferred_h = layout->y + layout->h - layout->cursor_y;
    }
    rect.x = (int32_t)layout->cursor_x;
    rect.y = (int32_t)layout->cursor_y;
    rect.w = preferred_w;
    rect.h = preferred_h;
    layout->cursor_x += preferred_w + layout->gap;
    if (preferred_h > layout->row_h) {
        layout->row_h = preferred_h;
    }
    return rect;
}

uint32_t leonos_ui_anim_progress(unsigned long now, unsigned long start,
                                 unsigned long duration_ms)
{
    unsigned long elapsed;
    if (duration_ms == 0) {
        return 1000;
    }
    if (now <= start) {
        return 0;
    }
    elapsed = now - start;
    if (elapsed >= duration_ms) {
        return 1000;
    }
    return (uint32_t)((elapsed * 1000UL) / duration_ms);
}

uint32_t leonos_ui_anim_ease_out(uint32_t progress)
{
    if (progress > 1000) {
        progress = 1000;
    }
    return (progress * (2000 - progress)) / 1000;
}

uint32_t leonos_ui_anim_lerp(uint32_t from, uint32_t to, uint32_t progress)
{
    if (progress > 1000) {
        progress = 1000;
    }
    if (to >= from) {
        return from + ((to - from) * progress) / 1000;
    }
    return from - ((from - to) * progress) / 1000;
}

void leonos_ui_activity_bar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t progress)
{
    uint32_t inner_w;
    uint32_t inner_h;
    uint32_t block_w;
    uint32_t range;
    uint32_t eased = leonos_ui_anim_ease_out(progress % 1000);
    leonos_ui_inset(surface, x, y, w, h, LEONOS_UI_WHITE);
    if (w <= 4 || h <= 4) {
        return;
    }
    inner_w = w - 4;
    inner_h = h - 4;
    block_w = inner_w / 4;
    if (block_w < 12) {
        block_w = inner_w < 12 ? inner_w : 12;
    }
    range = inner_w > block_w ? inner_w - block_w : 0;
    leonos_ui_rect(surface, x + 2 + (range * eased) / 1000, y + 2,
                   block_w, inner_h, LEONOS_UI_ACTIVE_TITLE);
}

void leonos_ui_tree(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const struct leonos_ui_tree_item *items,
                    uint32_t count, uint32_t row_h)
{
    if (row_h < LEONOS_FONT_H + 4) {
        row_h = LEONOS_FONT_H + 4;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row_y = y + i * row_h;
        uint32_t indent = items ? items[i].depth * 14 : 0;
        uint32_t bg = (items && (items[i].flags & LEONOS_UI_TREE_SELECTED))
                          ? LEONOS_UI_ACTIVE_TITLE
                          : LEONOS_UI_WHITE;
        uint32_t fg = bg == LEONOS_UI_ACTIVE_TITLE ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        uint32_t icon_x = x + 4 + indent;
        uint32_t text_x = x + 20 + indent;
        leonos_ui_rect(surface, x, row_y, w, row_h, bg);
        if (items && !(items[i].flags & LEONOS_UI_TREE_LEAF)) {
            leonos_ui_button(surface, icon_x, row_y + 3, 12, 12,
                             (items[i].flags & LEONOS_UI_TREE_EXPANDED) ? "-" : "+", 0);
        } else {
            leonos_ui_rect(surface, icon_x + 4, row_y + 8, 4, 4, fg);
        }
        if (text_x < x + w) {
            leonos_ui_text_transparent_clipped(surface, text_x, row_y + 4,
                                               x + w - text_x,
                                               items ? items[i].label : "",
                                               fg);
        }
    }
}

int leonos_ui_tree_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                       uint32_t w, const struct leonos_ui_tree_item *items,
                       uint32_t count, uint32_t row_h, uint32_t *out_id)
{
    uint32_t index;
    if (out_id) {
        *out_id = 0;
    }
    if (row_h < LEONOS_FONT_H + 4) {
        row_h = LEONOS_FONT_H + 4;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                       w, count * row_h)) {
        return 0;
    }
    index = ((uint32_t)py - y) / row_h;
    if (!items || index >= count) {
        return 1;
    }
    if (out_id) {
        *out_id = items[index].id;
    }
    return 1;
}

void leonos_ui_panel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t color)
{
    leonos_ui_inset(surface, x, y, w, h, color);
}

void leonos_ui_checkbox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        const char *label, int checked, uint32_t flags)
{
    (void)flags;
    leonos_ui_inset(surface, x, y + 2, 14, 14, LEONOS_UI_WHITE);
    if (checked) {
        leonos_ui_rect(surface, x + 3, y + 8, 2, 2, LEONOS_UI_BLACK);
        leonos_ui_rect(surface, x + 5, y + 10, 2, 2, LEONOS_UI_BLACK);
        leonos_ui_rect(surface, x + 7, y + 8, 2, 2, LEONOS_UI_BLACK);
        leonos_ui_rect(surface, x + 9, y + 6, 2, 2, LEONOS_UI_BLACK);
    }
    leonos_ui_text_transparent(surface, x + 22, y, label, LEONOS_UI_BLACK);
}

void leonos_ui_progress(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t value, uint32_t max)
{
    leonos_ui_inset(surface, x, y, w, h, LEONOS_UI_WHITE);
    if (max == 0) {
        return;
    }
    if (value > max) {
        value = max;
    }
    uint32_t fill_w = w > 4 ? ((w - 4) * value) / max : 0;
    leonos_ui_rect(surface, x + 2, y + 2, fill_w, h > 4 ? h - 4 : 0, LEONOS_UI_ACTIVE_TITLE);
}

void leonos_ui_text_field(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const char *text, uint32_t flags)
{
    leonos_ui_edit(surface, x, y, w, text, ui_strlen(text), 0, flags);
}

void leonos_ui_list_header(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const char *label)
{
    leonos_ui_bevel(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_GRAY, 0);
    leonos_ui_text_transparent(surface, x + 6, y + 4, label, LEONOS_UI_BLACK);
}

void leonos_ui_list_row(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const char *text, uint32_t flags)
{
    uint32_t selected = flags & LEONOS_UI_MENU_SELECTED;
    uint32_t bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
    uint32_t fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
    leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H + 4, bg);
    leonos_ui_text_clipped(surface, x + 4, y + 2, w > 8 ? w - 8 : w, text, fg, bg);
}

static uint32_t scrollbar_thumb_pos(uint32_t track_len, uint32_t value, uint32_t max, uint32_t page,
                                    uint32_t *thumb_len)
{
    uint32_t len;
    uint32_t range;
    if (track_len < 8) {
        *thumb_len = track_len;
        return 0;
    }
    if (max <= page || max == 0) {
        *thumb_len = track_len;
        return 0;
    }
    len = (track_len * page) / max;
    if (len < 12) {
        len = 12;
    }
    if (len > track_len) {
        len = track_len;
    }
    range = track_len - len;
    if (value > max - page) {
        value = max - page;
    }
    *thumb_len = len;
    return range ? (range * value) / (max - page) : 0;
}

void leonos_ui_vscrollbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t value, uint32_t max,
                          uint32_t page, uint32_t flags)
{
    uint32_t arrow_h = w < h / 2 ? w : h / 2;
    uint32_t track_y = y + arrow_h;
    uint32_t track_h = h > arrow_h * 2 ? h - arrow_h * 2 : 0;
    uint32_t thumb_h;
    uint32_t thumb_y;
    uint32_t disabled = flags & LEONOS_UI_SCROLLBAR_DISABLED;
    leonos_ui_button(surface, x, y, w, arrow_h, "^", disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(surface, x, y + h - arrow_h, w, arrow_h, "v", disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_inset(surface, x, track_y, w, track_h, LEONOS_UI_LIGHT);
    if (disabled || track_h == 0) {
        return;
    }
    thumb_y = scrollbar_thumb_pos(track_h, value, max, page, &thumb_h);
    leonos_ui_bevel(surface, x + 2, track_y + thumb_y, w > 4 ? w - 4 : w, thumb_h, LEONOS_UI_GRAY, 0);
}

void leonos_ui_hscrollbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t value, uint32_t max,
                          uint32_t page, uint32_t flags)
{
    uint32_t arrow_w = h < w / 2 ? h : w / 2;
    uint32_t track_x = x + arrow_w;
    uint32_t track_w = w > arrow_w * 2 ? w - arrow_w * 2 : 0;
    uint32_t thumb_w;
    uint32_t thumb_x;
    uint32_t disabled = flags & LEONOS_UI_SCROLLBAR_DISABLED;
    leonos_ui_button(surface, x, y, arrow_w, h, "<", disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(surface, x + w - arrow_w, y, arrow_w, h, ">", disabled ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_inset(surface, track_x, y, track_w, h, LEONOS_UI_LIGHT);
    if (disabled || track_w == 0) {
        return;
    }
    thumb_x = scrollbar_thumb_pos(track_w, value, max, page, &thumb_w);
    leonos_ui_bevel(surface, track_x + thumb_x, y + 2, thumb_w, h > 4 ? h - 4 : h, LEONOS_UI_GRAY, 0);
}

void leonos_ui_scroll_view_frame(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h)
{
    leonos_ui_inset(surface, x, y, w, h, LEONOS_UI_WHITE);
}

void leonos_ui_edit(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const char *text, uint32_t cursor, uint32_t scroll,
                    uint32_t flags)
{
    uint32_t h = LEONOS_FONT_H + 8;
    uint32_t bg = (flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_LIGHT : LEONOS_UI_WHITE;
    uint32_t fg = (flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
    const char *visible = text ? text : "";
    uint32_t len = ui_strlen(visible);
    if (scroll > len) {
        scroll = len;
    }
    leonos_ui_inset(surface, x, y, w, h, bg);
    leonos_ui_text_clipped(surface, x + 4, y + 4, w > 8 ? w - 8 : w, visible + scroll, fg, bg);
    if ((flags & LEONOS_UI_EDIT_FOCUSED) && !(flags & LEONOS_UI_EDIT_DISABLED)) {
        if (cursor < scroll) {
            cursor = scroll;
        }
        leonos_ui_rect(surface,
                       x + 4 + ui_text_cells_between(visible, scroll, cursor) * LEONOS_FONT_W,
                       y + 4, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
    }
}

static void edit_delete_range(struct leonos_ui_edit_state *state, uint32_t start, uint32_t end)
{
    if (!state || !state->buffer || start >= end || end > state->length) {
        return;
    }
    for (uint32_t i = start; i + end - start <= state->length; ++i) {
        state->buffer[i] = state->buffer[i + end - start];
    }
    state->length -= end - start;
    state->cursor = start;
}

static int edit_has_selection(const struct leonos_ui_edit_state *state)
{
    return state && state->selection_anchor != state->cursor &&
           state->selection_anchor <= state->length && state->cursor <= state->length;
}

static void edit_selection_range(const struct leonos_ui_edit_state *state,
                                 uint32_t *start, uint32_t *end)
{
    if (state->selection_anchor < state->cursor) {
        *start = state->selection_anchor;
        *end = state->cursor;
    } else {
        *start = state->cursor;
        *end = state->selection_anchor;
    }
}

static void edit_clear_selection(struct leonos_ui_edit_state *state)
{
    state->selection_anchor = state->cursor;
}

static void edit_ensure_cursor_visible(struct leonos_ui_edit_state *state, uint32_t w)
{
    uint32_t cols = w > 8 ? leonos_ui_text_fit_chars(w - 8) : 0;
    if (!state || cols == 0) {
        return;
    }
    if (state->cursor < state->scroll) {
        state->scroll = state->cursor;
    }
    while (ui_text_cells_between(state->buffer, state->scroll, state->cursor) > cols &&
           state->scroll < state->cursor) {
        state->scroll = ui_next_codepoint_offset(state->buffer, state->length, state->scroll);
    }
}

void leonos_ui_edit_state_init(struct leonos_ui_edit_state *state, char *buffer,
                               uint32_t capacity)
{
    if (!state) {
        return;
    }
    state->buffer = buffer;
    state->capacity = capacity;
    state->length = ui_strlen(buffer);
    if (state->length >= capacity && capacity) {
        state->length = capacity - 1;
        buffer[state->length] = 0;
    }
    state->cursor = state->length;
    state->scroll = 0;
    state->selection_anchor = state->cursor;
    state->focused = 0;
    state->readonly = 0;
    state->selecting = 0;
}

void leonos_ui_edit_state_sync(struct leonos_ui_edit_state *state)
{
    if (!state || !state->buffer) {
        return;
    }
    state->length = ui_strlen(state->buffer);
    if (state->capacity && state->length >= state->capacity) {
        state->length = state->capacity - 1;
        state->buffer[state->length] = 0;
    }
    if (state->cursor > state->length) {
        state->cursor = state->length;
    }
    if (state->selection_anchor > state->length) {
        state->selection_anchor = state->cursor;
    }
    if (state->scroll > state->length) {
        state->scroll = state->length;
    }
}

void leonos_ui_edit_state_draw(struct leonos_ui_surface *surface, uint32_t x,
                               uint32_t y, uint32_t w,
                               struct leonos_ui_edit_state *state,
                               uint32_t flags)
{
    uint32_t draw_flags = flags;
    uint32_t h = LEONOS_FONT_H + 8;
    uint32_t text_x = x + 4;
    uint32_t text_y = y + 4;
    uint32_t cols = w > 8 ? leonos_ui_text_fit_chars(w - 8) : 0;
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    struct leonos_text_layout layout;
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    uint32_t draw_x;
    uint32_t draw_count;
    uint32_t fg;
    uint32_t bg;
    if (!state) {
        leonos_ui_edit(surface, x, y, w, "", 0, 0, flags);
        return;
    }
    leonos_ui_edit_state_sync(state);
    if (state->focused) {
        draw_flags |= LEONOS_UI_EDIT_FOCUSED;
    }
    if (state->readonly) {
        draw_flags |= LEONOS_UI_EDIT_READONLY;
    }
    edit_ensure_cursor_visible(state, w);
    bg = (draw_flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_LIGHT : LEONOS_UI_WHITE;
    fg = (draw_flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
    leonos_ui_inset(surface, x, y, w, h, bg);
    if (edit_has_selection(state)) {
        edit_selection_range(state, &sel_start, &sel_end);
    }
    draw_x = text_x;
    ui_layout_utf8(state->buffer + state->scroll, state->length - state->scroll,
                   glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    draw_count = layout.count < UI_LAYOUT_GLYPH_MAX ? layout.count : UI_LAYOUT_GLYPH_MAX;
    for (uint32_t i = 0; i < draw_count && draw_x < text_x + cols * LEONOS_FONT_W; ++i) {
        uint32_t idx = state->scroll + glyphs[i].byte_offset;
        uint32_t px = glyphs[i].pixel_width;
        uint32_t ch_bg = bg;
        uint32_t ch_fg = fg;
        if (draw_x + px > text_x + cols * LEONOS_FONT_W) {
            break;
        }
        if (idx < sel_end && idx + glyphs[i].byte_len > sel_start && edit_has_selection(state)) {
            ch_bg = LEONOS_UI_ACTIVE_TITLE;
            ch_fg = LEONOS_UI_WHITE;
        }
        ui_codepoint(surface, draw_x, text_y, glyphs[i].codepoint, glyphs[i].cell_width,
                     ch_fg, ch_bg, 0);
        draw_x += px;
    }
    if ((draw_flags & LEONOS_UI_EDIT_FOCUSED) && !(draw_flags & LEONOS_UI_EDIT_DISABLED)) {
        uint32_t cursor = state->cursor;
        if (cursor < state->scroll) {
            cursor = state->scroll;
        }
        leonos_ui_rect(surface,
                       text_x + ui_text_cells_between(state->buffer, state->scroll, cursor) * LEONOS_FONT_W,
                       text_y, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
    }
}

static int edit_insert_char(struct leonos_ui_edit_state *state, char ch)
{
    if (!state || !state->buffer || state->readonly || state->capacity == 0 || ch < 32) {
        return 0;
    }
    if (edit_has_selection(state)) {
        uint32_t start;
        uint32_t end;
        edit_selection_range(state, &start, &end);
        edit_delete_range(state, start, end);
    }
    if (state->length + 1 >= state->capacity) {
        return 0;
    }
    for (uint32_t i = state->length + 1; i > state->cursor; --i) {
        state->buffer[i] = state->buffer[i - 1];
    }
    state->buffer[state->cursor++] = ch;
    ++state->length;
    edit_clear_selection(state);
    return 1;
}

int leonos_ui_edit_state_handle_key(struct leonos_ui_edit_state *state,
                                    uint8_t keycode, uint8_t pressed)
{
    char ch;
    if (!state) {
        return 0;
    }
    if (ui_is_shift_key(keycode)) {
        ui_shift_down = pressed ? 1 : 0;
        return 0;
    }
    if (!pressed) {
        return 0;
    }
    leonos_ui_edit_state_sync(state);
    switch (keycode) {
    case LEONOS_KEY_BACKSPACE:
        if (state->readonly) {
            return 0;
        }
        if (edit_has_selection(state)) {
            uint32_t start;
            uint32_t end;
            edit_selection_range(state, &start, &end);
            edit_delete_range(state, start, end);
            edit_clear_selection(state);
            return 1;
        }
        if (state->cursor > 0) {
            edit_delete_range(state, ui_prev_codepoint_offset(state->buffer, state->cursor),
                              state->cursor);
            edit_clear_selection(state);
            return 1;
        }
        return 0;
    case LEONOS_KEY_ENTER:
        return 0;
    case 75:
        if (state->cursor > 0) {
            state->cursor = ui_prev_codepoint_offset(state->buffer, state->cursor);
            edit_clear_selection(state);
            return 1;
        }
        return 0;
    case 77:
        if (state->cursor < state->length) {
            state->cursor = ui_next_codepoint_offset(state->buffer, state->length, state->cursor);
            edit_clear_selection(state);
            return 1;
        }
        return 0;
    case 71:
        state->cursor = 0;
        edit_clear_selection(state);
        return 1;
    case 79:
        state->cursor = state->length;
        edit_clear_selection(state);
        return 1;
    default:
        if (leonos_ui_keycode_to_char_shift(keycode, ui_shift_down, &ch) && ch >= 32) {
            return edit_insert_char(state, ch);
        }
        return 0;
    }
}
int leonos_ui_edit_state_handle_mouse(struct leonos_ui_edit_state *state,
                                      int32_t px, int32_t py, uint32_t x,
                                      uint32_t y, uint32_t w, uint32_t buttons)
{
    uint32_t h = LEONOS_FONT_H + 8;
    uint32_t cols = w > 8 ? leonos_ui_text_fit_chars(w - 8) : 0;
    uint32_t idx;
    if (!state) {
        return 0;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, h)) {
        if (buttons & 1u) {
            state->focused = 0;
            state->selecting = 0;
            return 1;
        }
        return 0;
    }
    if (!(buttons & 1u)) {
        state->selecting = 0;
        return 0;
    }
    state->focused = 1;
    idx = state->scroll;
    if (px > (int32_t)x + 4) {
        idx = ui_byte_offset_for_cell(state->buffer, state->length, state->scroll,
                                      ((uint32_t)px - x - 4) / LEONOS_FONT_W);
    }
    if (idx > state->length) {
        idx = state->length;
    }
    state->cursor = idx;
    if (!state->selecting) {
        state->selection_anchor = state->cursor;
        state->selecting = 1;
    }
    (void)cols;
    edit_ensure_cursor_visible(state, w);
    return 1;
}

static uint32_t text_area_cols(uint32_t w);

void leonos_ui_text_area(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *text, uint32_t cursor,
                         uint32_t scroll_line, uint32_t flags)
{
    uint32_t rows = h > 8 ? (h - 8) / LEONOS_FONT_H : 0;
    uint32_t cols = text_area_cols(w);
    uint32_t current = 0;
    uint32_t row = 0;
    uint32_t line_len = 0;
    uint32_t line_cells = 0;
    uint32_t text_pos = 0;
    char line[128];
    (void)cursor;
    leonos_ui_scroll_view_frame(surface, x, y, w, h);
    while (text && row < rows) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(text, ui_strlen(text), text_pos, &byte_len);
        uint32_t cw = ui_cell_width(cp);
        if (text[text_pos] == 0) {
            line[line_len] = 0;
            if (current >= scroll_line) {
                leonos_ui_text_clipped(surface, x + 4, y + 4 + row * LEONOS_FONT_H,
                                      w > 8 ? w - 8 : w, line,
                                      (flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK,
                                      LEONOS_UI_WHITE);
                ++row;
            }
            break;
        }
        if (cp == '\r') {
            text_pos += byte_len;
            continue;
        }
        if (cp == '\n' || line_len + byte_len >= sizeof(line) ||
            (line_cells + cw > cols && line_len != 0)) {
            line[line_len] = 0;
            if (current >= scroll_line) {
                leonos_ui_text_clipped(surface, x + 4, y + 4 + row * LEONOS_FONT_H,
                                      w > 8 ? w - 8 : w, line,
                                      (flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK,
                                      LEONOS_UI_WHITE);
                ++row;
            }
            ++current;
            line_len = 0;
            line_cells = 0;
            if (cp == '\n') {
                text_pos += byte_len;
            }
            continue;
        }
        for (uint32_t i = 0; i < byte_len && line_len + 1 < sizeof(line); ++i) {
            line[line_len++] = text[text_pos + i];
        }
        line_cells += cw;
        text_pos += byte_len;
    }
    if ((flags & LEONOS_UI_EDIT_FOCUSED) && row < rows) {
        leonos_ui_rect(surface, x + 4, y + 4 + row * LEONOS_FONT_H, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
    }
}

static uint32_t text_area_cols(uint32_t w)
{
    uint32_t cols = w > 8 ? leonos_ui_text_fit_chars(w - 8) : 0;
    return cols ? cols : 1;
}

static uint32_t text_area_rows(uint32_t h)
{
    return h > 8 ? (h - 8) / LEONOS_FONT_H : 0;
}

static void text_area_cursor_line_col(struct leonos_ui_text_area_state *state,
                                      uint32_t w, uint32_t cursor,
                                      uint32_t *out_line, uint32_t *out_col)
{
    uint32_t cols = text_area_cols(w);
    uint32_t line = 0;
    uint32_t col = 0;
    if (!state || !state->buffer) {
        *out_line = 0;
        *out_col = 0;
        return;
    }
    if (cursor > state->length) {
        cursor = state->length;
    }
    for (uint32_t i = 0; i < cursor;) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(state->buffer, state->length, i, &byte_len);
        uint32_t cw = ui_cell_width(cp);
        if (cp == '\r') {
            i += byte_len;
            continue;
        }
        if (cp == '\n') {
            ++line;
            col = 0;
            i += byte_len;
            continue;
        }
        if (col + cw > cols && col != 0) {
            ++line;
            col = 0;
        }
        col += cw;
        i += byte_len;
    }
    *out_line = line;
    *out_col = col;
}

static uint32_t text_area_cursor_from_line_col(struct leonos_ui_text_area_state *state,
                                               uint32_t w, uint32_t target_line,
                                               uint32_t target_col)
{
    uint32_t cols = text_area_cols(w);
    uint32_t line = 0;
    uint32_t col = 0;
    if (!state || !state->buffer) {
        return 0;
    }
    for (uint32_t pos = 0; pos <= state->length; ++pos) {
        if (line == target_line && col >= target_col) {
            return pos;
        }
        if (pos == state->length) {
            return state->length;
        }
        uint32_t byte_len = 1;
        uint32_t cp;
        uint32_t cw;
        cp = ui_decode_utf8(state->buffer, state->length, pos, &byte_len);
        cw = ui_cell_width(cp);
        if (cp == '\r') {
            continue;
        }
        if (cp == '\n') {
            if (line == target_line) {
                return pos;
            }
            ++line;
            col = 0;
            continue;
        }
        if (col + cw > cols && col != 0) {
            ++line;
            col = 0;
            if (line > target_line) {
                return pos;
            }
            if (line == target_line && col >= target_col) {
                return pos;
            }
        }
        if (line == target_line && col + cw > target_col) {
            return pos;
        }
        col += cw;
        if (byte_len > 1) {
            pos += byte_len - 1u;
        }
    }
    return state->length;
}

static void text_area_ensure_cursor_visible(struct leonos_ui_text_area_state *state,
                                            uint32_t w, uint32_t h)
{
    uint32_t line;
    uint32_t col;
    uint32_t rows = text_area_rows(h);
    if (!state || rows == 0) {
        return;
    }
    text_area_cursor_line_col(state, w, state->cursor, &line, &col);
    (void)col;
    if (line < state->scroll_line) {
        state->scroll_line = line;
    } else if (line >= state->scroll_line + rows) {
        state->scroll_line = line - rows + 1;
    }
}

void leonos_ui_text_area_state_init(struct leonos_ui_text_area_state *state,
                                    char *buffer, uint32_t capacity)
{
    if (!state) {
        return;
    }
    state->buffer = buffer;
    state->capacity = capacity;
    state->length = ui_strlen(buffer);
    if (state->length >= capacity && capacity) {
        state->length = capacity - 1;
        buffer[state->length] = 0;
    }
    state->cursor = state->length;
    state->selection_anchor = state->cursor;
    state->scroll_line = 0;
    state->preferred_column = 0xffffffffu;
    state->line_count = 1;
    state->focused = 0;
    state->readonly = 0;
    state->selecting = 0;
}

static int text_area_has_selection(const struct leonos_ui_text_area_state *state)
{
    return state && state->selection_anchor != state->cursor &&
           state->selection_anchor <= state->length && state->cursor <= state->length;
}

static void text_area_selection_range(const struct leonos_ui_text_area_state *state,
                                      uint32_t *start, uint32_t *end)
{
    if (state->selection_anchor < state->cursor) {
        *start = state->selection_anchor;
        *end = state->cursor;
    } else {
        *start = state->cursor;
        *end = state->selection_anchor;
    }
}

static void text_area_clear_selection(struct leonos_ui_text_area_state *state)
{
    state->selection_anchor = state->cursor;
}

uint32_t leonos_ui_text_area_line_count(struct leonos_ui_text_area_state *state,
                                        uint32_t w)
{
    uint32_t cols = text_area_cols(w);
    uint32_t lines = 1;
    uint32_t col = 0;
    if (!state || !state->buffer) {
        return 1;
    }
    for (uint32_t i = 0; i < state->length;) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(state->buffer, state->length, i, &byte_len);
        uint32_t cw = ui_cell_width(cp);
        if (cp == '\r') {
            i += byte_len;
            continue;
        }
        if (cp == '\n') {
            ++lines;
            col = 0;
            i += byte_len;
            continue;
        }
        if (col + cw > cols && col != 0) {
            ++lines;
            col = 0;
        }
        col += cw;
        i += byte_len;
    }
    return lines ? lines : 1;
}

void leonos_ui_text_area_state_sync(struct leonos_ui_text_area_state *state,
                                    uint32_t w)
{
    if (!state || !state->buffer) {
        return;
    }
    state->length = ui_strlen(state->buffer);
    if (state->capacity && state->length >= state->capacity) {
        state->length = state->capacity - 1;
        state->buffer[state->length] = 0;
    }
    if (state->cursor > state->length) {
        state->cursor = state->length;
    }
    if (state->selection_anchor > state->length) {
        state->selection_anchor = state->cursor;
    }
    state->line_count = leonos_ui_text_area_line_count(state, w);
    if (state->scroll_line >= state->line_count) {
        state->scroll_line = state->line_count ? state->line_count - 1 : 0;
    }
}

void leonos_ui_text_area_state_draw(struct leonos_ui_surface *surface, uint32_t x,
                                    uint32_t y, uint32_t w, uint32_t h,
                                    struct leonos_ui_text_area_state *state,
                                    uint32_t flags)
{
    uint32_t cursor_line;
    uint32_t cursor_col;
    uint32_t rows = text_area_rows(h);
    uint32_t cols = text_area_cols(w);
    uint32_t draw_flags = flags;
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    if (!state) {
        leonos_ui_text_area(surface, x, y, w, h, "", 0, 0, flags);
        return;
    }
    leonos_ui_text_area_state_sync(state, w);
    if (state->focused) {
        draw_flags |= LEONOS_UI_EDIT_FOCUSED;
    }
    if (state->readonly) {
        draw_flags |= LEONOS_UI_EDIT_READONLY;
    }
    leonos_ui_scroll_view_frame(surface, x, y, w, h);
    if (text_area_has_selection(state)) {
        text_area_selection_range(state, &sel_start, &sel_end);
    }
    for (uint32_t row = 0; row < rows; ++row) {
        uint32_t line = state->scroll_line + row;
        uint32_t pos = text_area_cursor_from_line_col(state, w, line, 0);
        uint32_t draw_x = x + 4;
        uint32_t draw_right = x + 4 + cols * LEONOS_FONT_W;
        while (pos < state->length && draw_x < draw_right) {
            uint32_t byte_len = 1;
            uint32_t cp = ui_decode_utf8(state->buffer, state->length, pos, &byte_len);
            uint32_t cw = ui_cell_width(cp);
            uint32_t px = cw * LEONOS_FONT_W;
            uint32_t ch_bg = LEONOS_UI_WHITE;
            uint32_t ch_fg = (draw_flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
            uint32_t next_line;
            uint32_t next_col;
            if (cp == '\r') {
                pos += byte_len;
                continue;
            }
            if (cp == '\n') {
                if (pos >= sel_start && pos < sel_end && text_area_has_selection(state)) {
                    ch_bg = LEONOS_UI_ACTIVE_TITLE;
                    ch_fg = LEONOS_UI_WHITE;
                }
                ui_char(surface, draw_x, y + 4 + row * LEONOS_FONT_H,
                        ' ', ch_fg, ch_bg, 0);
                break;
            }
            text_area_cursor_line_col(state, w, pos, &next_line, &next_col);
            (void)next_col;
            if (next_line != line || draw_x + px > draw_right) {
                break;
            }
            if (pos < sel_end && pos + byte_len > sel_start && text_area_has_selection(state)) {
                ch_bg = LEONOS_UI_ACTIVE_TITLE;
                ch_fg = LEONOS_UI_WHITE;
            }
            ui_codepoint(surface, draw_x, y + 4 + row * LEONOS_FONT_H,
                         cp, cw, ch_fg, ch_bg, 0);
            draw_x += px;
            pos += byte_len;
        }
    }
    if ((draw_flags & LEONOS_UI_EDIT_FOCUSED) && !(draw_flags & LEONOS_UI_EDIT_DISABLED)) {
        text_area_cursor_line_col(state, w, state->cursor, &cursor_line, &cursor_col);
        if (cursor_line >= state->scroll_line && cursor_line < state->scroll_line + rows) {
            uint32_t cx = x + 4 + cursor_col * LEONOS_FONT_W;
            uint32_t cy = y + 4 + (cursor_line - state->scroll_line) * LEONOS_FONT_H;
            if (cx < x + w - 2) {
                leonos_ui_rect(surface, cx, cy, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
            }
        }
    }
}

static int text_area_delete_range(struct leonos_ui_text_area_state *state,
                                  uint32_t start, uint32_t end)
{
    if (!state || !state->buffer || state->readonly || start >= end || end > state->length) {
        return 0;
    }
    for (uint32_t i = start; i + end - start <= state->length; ++i) {
        state->buffer[i] = state->buffer[i + end - start];
    }
    state->length -= end - start;
    state->cursor = start;
    text_area_clear_selection(state);
    return 1;
}

static int text_area_insert_char(struct leonos_ui_text_area_state *state, char ch)
{
    if (!state || !state->buffer || state->readonly || state->capacity == 0) {
        return 0;
    }
    if (text_area_has_selection(state)) {
        uint32_t start;
        uint32_t end;
        text_area_selection_range(state, &start, &end);
        text_area_delete_range(state, start, end);
    }
    if (state->length + 1 >= state->capacity) {
        return 0;
    }
    for (uint32_t i = state->length + 1; i > state->cursor; --i) {
        state->buffer[i] = state->buffer[i - 1];
    }
    state->buffer[state->cursor++] = ch;
    ++state->length;
    text_area_clear_selection(state);
    return 1;
}

static int text_area_delete_char(struct leonos_ui_text_area_state *state, uint32_t index)
{
    if (!state) {
        return 0;
    }
    return text_area_delete_range(state, index,
                                  ui_next_codepoint_offset(state->buffer,
                                                           state->length,
                                                           index));
}

int leonos_ui_text_area_state_handle_key(struct leonos_ui_text_area_state *state,
                                         uint8_t keycode, uint8_t pressed, uint32_t w,
                                         uint32_t h)
{
    char ch;
    uint32_t line;
    uint32_t col;
    uint32_t rows;
    if (!state) {
        return 0;
    }
    if (ui_is_shift_key(keycode)) {
        ui_shift_down = pressed ? 1 : 0;
        return 0;
    }
    if (!pressed) {
        return 0;
    }
    leonos_ui_text_area_state_sync(state, w);
    text_area_cursor_line_col(state, w, state->cursor, &line, &col);
    switch (keycode) {
    case LEONOS_KEY_BACKSPACE:
        if (state->readonly) {
            return 0;
        }
        if (text_area_has_selection(state)) {
            uint32_t start;
            uint32_t end;
            text_area_selection_range(state, &start, &end);
            if (text_area_delete_range(state, start, end)) {
                text_area_ensure_cursor_visible(state, w, h);
                return 1;
            }
            return 0;
        }
        if (state->cursor > 0) {
            if (text_area_delete_char(state, ui_prev_codepoint_offset(state->buffer,
                                                                      state->cursor))) {
                if (col > 0) {
                    --col;
                }
                state->preferred_column = col;
                text_area_ensure_cursor_visible(state, w, h);
                return 1;
            }
        }
        return 0;
    case LEONOS_KEY_ENTER:
        if (state->readonly) {
            return 0;
        }
        if (text_area_insert_char(state, '\n')) {
            state->preferred_column = 0xffffffffu;
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 75:
        if (state->cursor > 0) {
            state->cursor = ui_prev_codepoint_offset(state->buffer, state->cursor);
            text_area_clear_selection(state);
            text_area_cursor_line_col(state, w, state->cursor, &line, &col);
            state->preferred_column = col;
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 77:
        if (state->cursor < state->length) {
            state->cursor = ui_next_codepoint_offset(state->buffer, state->length, state->cursor);
            text_area_clear_selection(state);
            text_area_cursor_line_col(state, w, state->cursor, &line, &col);
            state->preferred_column = col;
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 72:
        line = line > 0 ? line - 1 : 0;
        state->cursor = text_area_cursor_from_line_col(state, w, line,
                                                       state->preferred_column == 0xffffffffu ? col : state->preferred_column);
        text_area_clear_selection(state);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 80:
        line += 1;
        if (line >= state->line_count) {
            line = state->line_count ? state->line_count - 1 : 0;
        }
        state->cursor = text_area_cursor_from_line_col(state, w, line,
                                                       state->preferred_column == 0xffffffffu ? col : state->preferred_column);
        text_area_clear_selection(state);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 73:
        rows = text_area_rows(h);
        line = line > rows ? line - rows : 0;
        state->cursor = text_area_cursor_from_line_col(state, w, line, col);
        text_area_clear_selection(state);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 81:
        rows = text_area_rows(h);
        line += rows ? rows : 1;
        if (line >= state->line_count) {
            line = state->line_count ? state->line_count - 1 : 0;
        }
        state->cursor = text_area_cursor_from_line_col(state, w, line, col);
        text_area_clear_selection(state);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 71:
        state->cursor = text_area_cursor_from_line_col(state, w, line, 0);
        text_area_clear_selection(state);
        state->preferred_column = 0;
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 79:
        state->cursor = text_area_cursor_from_line_col(state, w, line, 0xffffffffu);
        text_area_clear_selection(state);
        text_area_cursor_line_col(state, w, state->cursor, &line, &col);
        state->preferred_column = col;
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    default:
        if (leonos_ui_keycode_to_char_shift(keycode, ui_shift_down, &ch)) {
            if (state->readonly) {
                return 0;
            }
            if (ch == '\t') {
                ch = ' ';
            }
            if (ch >= 32) {
                int changed = text_area_insert_char(state, ch);
                text_area_cursor_line_col(state, w, state->cursor, &line, &col);
                state->preferred_column = col;
                if (changed) {
                    leonos_ui_text_area_state_sync(state, w);
                    text_area_ensure_cursor_visible(state, w, h);
                }
                return changed;
            }
        }
        return 0;
    }
}
int leonos_ui_text_area_state_handle_mouse(struct leonos_ui_text_area_state *state,
                                           int32_t px, int32_t py, uint32_t x,
                                           uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t buttons)
{
    uint32_t line;
    uint32_t col;
    if (!state) {
        return 0;
    }
    if (!(buttons & 1u)) {
        state->selecting = 0;
        return 0;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, h)) {
        state->focused = 0;
        return 1;
    }
    state->focused = 1;
    line = state->scroll_line;
    if (py > (int32_t)y + 4) {
        line += ((uint32_t)py - y - 4) / LEONOS_FONT_H;
    }
    col = 0;
    if (px > (int32_t)x + 4) {
        col = ((uint32_t)px - x - 4) / LEONOS_FONT_W;
    }
    {
        uint32_t next = text_area_cursor_from_line_col(state, w, line, col);
        if (!state->selecting) {
            state->selection_anchor = next;
            state->selecting = 1;
        }
        state->cursor = next;
    }
    state->preferred_column = col;
    return 1;
}

void leonos_ui_listview_header(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                               uint32_t w, const struct leonos_ui_list_column *cols,
                               uint32_t count)
{
    uint32_t cx = x;
    leonos_ui_bevel(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_GRAY, 0);
    for (uint32_t i = 0; i < count && cx < x + w; ++i) {
        uint32_t cw = cols[i].width ? cols[i].width : (x + w - cx);
        if (cx + cw > x + w) {
            cw = x + w - cx;
        }
        leonos_ui_text_transparent_clipped(surface, cx + 6, y + 4, cw > 12 ? cw - 12 : cw,
                                           cols[i].label, LEONOS_UI_BLACK);
        if (i + 1 < count && cw > 1) {
            leonos_ui_rect(surface, cx + cw - 1, y + 2, 1, LEONOS_FONT_H + 4, LEONOS_UI_DARK);
        }
        cx += cw;
    }
}

void leonos_ui_listview_row(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const struct leonos_ui_list_column *cols,
                            const char *const cells[], uint32_t count, uint32_t flags)
{
    uint32_t selected = flags & LEONOS_UI_MENU_SELECTED;
    uint32_t bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
    uint32_t fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
    uint32_t cx = x;
    leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H + 4, bg);
    for (uint32_t i = 0; i < count && cx < x + w; ++i) {
        uint32_t cw = cols[i].width ? cols[i].width : (x + w - cx);
        if (cx + cw > x + w) {
            cw = x + w - cx;
        }
        leonos_ui_text_clipped(surface, cx + 4, y + 2, cw > 8 ? cw - 8 : cw,
                               cells ? cells[i] : "", fg, bg);
        cx += cw;
    }
}

void leonos_ui_listview_state_init(struct leonos_ui_listview_state *state,
                                   uint32_t visible_rows, uint32_t row_height)
{
    if (!state) {
        return;
    }
    state->row_count = 0;
    state->visible_rows = visible_rows;
    state->row_height = row_height ? row_height : (LEONOS_FONT_H + 8);
    state->scroll = 0;
    state->selected = -1;
    state->focused = 0;
}

static void listview_clamp(struct leonos_ui_listview_state *state)
{
    uint32_t visible;
    uint32_t max_scroll;
    if (!state) {
        return;
    }
    visible = state->visible_rows ? state->visible_rows : 1;
    max_scroll = state->row_count > visible ? state->row_count - visible : 0;
    if (state->scroll > max_scroll) {
        state->scroll = max_scroll;
    }
    if (state->row_count == 0) {
        state->selected = -1;
        state->scroll = 0;
        return;
    }
    if (state->selected >= (int32_t)state->row_count) {
        state->selected = (int32_t)state->row_count - 1;
    }
}

static void listview_ensure_selected_visible(struct leonos_ui_listview_state *state)
{
    uint32_t selected;
    uint32_t visible;
    if (!state || state->selected < 0) {
        return;
    }
    selected = (uint32_t)state->selected;
    visible = state->visible_rows ? state->visible_rows : 1;
    if (selected < state->scroll) {
        state->scroll = selected;
    } else if (selected >= state->scroll + visible) {
        state->scroll = selected - visible + 1;
    }
    listview_clamp(state);
}

void leonos_ui_listview_state_set_count(struct leonos_ui_listview_state *state,
                                        uint32_t row_count)
{
    if (!state) {
        return;
    }
    state->row_count = row_count;
    listview_clamp(state);
}

int leonos_ui_listview_state_handle_key(struct leonos_ui_listview_state *state,
                                        uint8_t keycode, uint32_t *activated)
{
    uint32_t visible;
    if (activated) {
        *activated = 0;
    }
    if (!state || !state->focused || state->row_count == 0) {
        return 0;
    }
    visible = state->visible_rows ? state->visible_rows : 1;
    if (state->selected < 0) {
        state->selected = 0;
    }
    switch (keycode) {
    case 72:
        if (state->selected > 0) {
            --state->selected;
            listview_ensure_selected_visible(state);
            return 1;
        }
        return 0;
    case 80:
        if ((uint32_t)state->selected + 1 < state->row_count) {
            ++state->selected;
            listview_ensure_selected_visible(state);
            return 1;
        }
        return 0;
    case 73:
        if ((uint32_t)state->selected > visible) {
            state->selected -= (int32_t)visible;
        } else {
            state->selected = 0;
        }
        listview_ensure_selected_visible(state);
        return 1;
    case 81:
        if ((uint32_t)state->selected + visible < state->row_count) {
            state->selected += (int32_t)visible;
        } else {
            state->selected = (int32_t)state->row_count - 1;
        }
        listview_ensure_selected_visible(state);
        return 1;
    case 71:
        state->selected = 0;
        listview_ensure_selected_visible(state);
        return 1;
    case 79:
        state->selected = (int32_t)state->row_count - 1;
        listview_ensure_selected_visible(state);
        return 1;
    case LEONOS_KEY_ENTER:
        if (activated && state->selected >= 0) {
            *activated = 1;
        }
        return state->selected >= 0;
    default:
        return 0;
    }
}

int leonos_ui_listview_state_handle_mouse(struct leonos_ui_listview_state *state,
                                          int32_t px, int32_t py, uint32_t x,
                                          uint32_t rows_y, uint32_t w,
                                          uint32_t *activated)
{
    uint32_t row;
    uint32_t index;
    if (activated) {
        *activated = 0;
    }
    if (!state) {
        return 0;
    }
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)rows_y,
                       w, state->visible_rows * state->row_height)) {
        state->focused = 0;
        return 0;
    }
    state->focused = 1;
    row = ((uint32_t)py - rows_y) / state->row_height;
    index = state->scroll + row;
    if (index >= state->row_count) {
        return 1;
    }
    if (state->selected == (int32_t)index && activated) {
        *activated = 1;
    }
    state->selected = (int32_t)index;
    listview_ensure_selected_visible(state);
    return 1;
}

int leonos_ui_listview_state_handle_wheel(struct leonos_ui_listview_state *state,
                                          int32_t wheel_delta)
{
    uint32_t old;
    uint32_t max_scroll;
    uint32_t visible;
    uint32_t steps;
    if (!state || wheel_delta == 0) {
        return 0;
    }
    visible = state->visible_rows ? state->visible_rows : 1;
    if (state->row_count <= visible) {
        return 0;
    }
    old = state->scroll;
    max_scroll = state->row_count - visible;
    steps = wheel_delta < 0 ? (uint32_t)(-wheel_delta) : (uint32_t)wheel_delta;
    if (steps == 0) {
        steps = 1;
    }
    if (wheel_delta > 0) {
        state->scroll = state->scroll > steps ? state->scroll - steps : 0;
    } else {
        state->scroll = state->scroll + steps < max_scroll ? state->scroll + steps : max_scroll;
    }
    return old != state->scroll;
}

int leonos_ui_vscrollbar_handle_mouse(uint32_t *value, uint32_t max, uint32_t page,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h, int32_t px, int32_t py)
{
    uint32_t old;
    uint32_t arrow_h;
    uint32_t max_value;
    if (!value || max <= page || !leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, h)) {
        return 0;
    }
    old = *value;
    max_value = max - page;
    arrow_h = w < h / 2 ? w : h / 2;
    if (py < (int32_t)(y + arrow_h)) {
        if (*value > 0) {
            --(*value);
        }
    } else if (py >= (int32_t)(y + h - arrow_h)) {
        if (*value < max_value) {
            ++(*value);
        }
    } else if (py < (int32_t)(y + h / 2)) {
        *value = *value > page ? *value - page : 0;
    } else {
        *value = *value + page < max_value ? *value + page : max_value;
    }
    return old != *value;
}

int leonos_ui_vscrollbar_handle_wheel(uint32_t *value, uint32_t max, uint32_t page,
                                      int32_t wheel_delta)
{
    uint32_t old;
    uint32_t max_value;
    uint32_t steps;
    if (!value || max <= page || wheel_delta == 0) {
        return 0;
    }
    old = *value;
    max_value = max - page;
    steps = wheel_delta < 0 ? (uint32_t)(-wheel_delta) : (uint32_t)wheel_delta;
    if (steps == 0) {
        steps = 1;
    }
    if (wheel_delta > 0) {
        *value = *value > steps ? *value - steps : 0;
    } else {
        *value = *value + steps < max_value ? *value + steps : max_value;
    }
    return old != *value;
}

int leonos_ui_hscrollbar_handle_mouse(uint32_t *value, uint32_t max, uint32_t page,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h, int32_t px, int32_t py)
{
    uint32_t old;
    uint32_t arrow_w;
    uint32_t max_value;
    if (!value || max <= page || !leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, h)) {
        return 0;
    }
    old = *value;
    max_value = max - page;
    arrow_w = h < w / 2 ? h : w / 2;
    if (px < (int32_t)(x + arrow_w)) {
        if (*value > 0) {
            --(*value);
        }
    } else if (px >= (int32_t)(x + w - arrow_w)) {
        if (*value < max_value) {
            ++(*value);
        }
    } else if (px < (int32_t)(x + w / 2)) {
        *value = *value > page ? *value - page : 0;
    } else {
        *value = *value + page < max_value ? *value + page : max_value;
    }
    return old != *value;
}

void leonos_ui_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title)
{
    leonos_ui_bevel(surface, x, y, w, h, LEONOS_UI_GRAY, 0);
    leonos_ui_rect(surface, x + 4, y + 4, w > 8 ? w - 8 : 0, LEONOS_UI_TITLEBAR_H, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text_clipped(surface, x + 10, y + 9, w > 20 ? w - 20 : w, title, LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
}

void leonos_ui_message_box(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const char *title,
                           const char *message, const char *button)
{
    uint32_t line_y = y + 46;
    uint32_t line_start = 0;
    uint32_t line_len = 0;
    uint32_t max_chars = leonos_ui_text_fit_chars(w > 32 ? w - 32 : w);
    leonos_ui_dialog(surface, x, y, w, h, title);
    for (uint32_t i = 0; message && line_y + LEONOS_FONT_H < y + h - 44; ++i) {
        char ch = message[i];
        if (ch != '\n' && ch != 0 && line_len < max_chars) {
            ++line_len;
            continue;
        }
        {
            char line[96];
            uint32_t n = 0;
            while (n < line_len && n + 1 < sizeof(line)) {
                line[n] = message[line_start + n];
                ++n;
            }
            line[n] = 0;
            leonos_ui_text_clipped(surface, x + 16, line_y, w > 32 ? w - 32 : w,
                                   line, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        }
        line_y += LEONOS_FONT_H + 2;
        if (ch == 0) {
            break;
        }
        line_start = ch == '\n' ? i + 1 : i;
        line_len = 0;
    }
    leonos_ui_button(surface, x + w / 2 - 36, y + h - 38, 72, LEONOS_UI_BUTTON_H, button ? button : "OK", 0);
}

void leonos_ui_confirm_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, const char *title,
                              const char *message, uint32_t default_yes)
{
    leonos_ui_dialog(surface, x, y, w, h, title);
    leonos_ui_text_clipped(surface, x + 16, y + 46, w > 32 ? w - 32 : w, message, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_button(surface, x + w - 168, y + h - 38, 72, LEONOS_UI_BUTTON_H, "Yes",
                     default_yes ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_button(surface, x + w - 88, y + h - 38, 72, LEONOS_UI_BUTTON_H, "No",
                     default_yes ? 0 : LEONOS_UI_BUTTON_PRESSED);
}

void leonos_ui_input_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, const char *title,
                            const char *label, const char *value, uint32_t flags)
{
    leonos_ui_dialog(surface, x, y, w, h, title);
    leonos_ui_text_clipped(surface, x + 16, y + 46, w > 32 ? w - 32 : w, label, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, x + 16, y + 70, w > 32 ? w - 32 : w, value, ui_strlen(value), 0, flags);
    leonos_ui_button(surface, x + w - 168, y + h - 38, 72, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(surface, x + w - 88, y + h - 38, 72, LEONOS_UI_BUTTON_H, "Cancel", 0);
}

int leonos_ui_show_message_box(const char *title, const char *message,
                               const char *button)
{
    enum { W = 360, H = 240 };
    static uint32_t pixels[W * H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    int result = 0;
    int window_id = leonos_gui_create_app_window_ex(title ? title : "Message",
                                                    message ? message : "",
                                                    W, H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, W, H, W);
    leonos_ui_rect(&surface, 0, 0, W, H, LEONOS_UI_GRAY);
    leonos_ui_message_box(&surface, 0, 0, W, H, title ? title : "Message",
                          message ? message : "", button ? button : "OK");
    leonos_gui_present_window((uint32_t)window_id, W, H, W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                (event.keycode == LEONOS_KEY_ENTER || event.keycode == 1)) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u) &&
                event.x >= (int32_t)(W / 2 - 36) && event.x < (int32_t)(W / 2 + 36) &&
                event.y >= (int32_t)(H - 38) && event.y < (int32_t)(H - 38 + LEONOS_UI_BUTTON_H)) {
                break;
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return result;
}

int leonos_ui_show_confirm_dialog(const char *title, const char *message,
                                  uint32_t default_yes)
{
    enum { W = 320, H = 150 };
    static uint32_t pixels[W * H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    int result = 0;
    int window_id = leonos_gui_create_app_window_ex(title ? title : "Confirm",
                                                    message ? message : "",
                                                    W, H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, W, H, W);
    leonos_ui_rect(&surface, 0, 0, W, H, LEONOS_UI_GRAY);
    leonos_ui_confirm_dialog(&surface, 0, 0, W, H, title ? title : "Confirm",
                             message ? message : "", default_yes);
    leonos_gui_present_window((uint32_t)window_id, W, H, W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    result = default_yes ? 1 : 0;
                    break;
                }
                if (event.pressed && event.keycode == 1) {
                    break;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (event.x >= (int32_t)(W - 168) && event.x < (int32_t)(W - 96) &&
                    event.y >= (int32_t)(H - 38) && event.y < (int32_t)(H - 38 + LEONOS_UI_BUTTON_H)) {
                    result = 1;
                    break;
                }
                if (event.x >= (int32_t)(W - 88) && event.x < (int32_t)(W - 16) &&
                    event.y >= (int32_t)(H - 38) && event.y < (int32_t)(H - 38 + LEONOS_UI_BUTTON_H)) {
                    break;
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return result;
}

int leonos_ui_show_input_dialog(const char *title, const char *label,
                                char *value, uint32_t capacity)
{
    enum { W = 360, H = 172 };
    static uint32_t pixels[W * H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct leonos_ui_edit_state edit;
    char original[128];
    int result = 0;
    int window_id;
    if (!value || capacity == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < sizeof(original); ++i) {
        original[i] = 0;
    }
    for (uint32_t i = 0; i + 1 < sizeof(original) && i + 1 < capacity && value[i]; ++i) {
        original[i] = value[i];
    }
    window_id = leonos_gui_create_app_window_ex(title ? title : "Input",
                                                label ? label : "",
                                                W, H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, W, H, W);
    leonos_ui_edit_state_init(&edit, value, capacity);
    edit.focused = 1;
    for (;;) {
        leonos_ui_rect(&surface, 0, 0, W, H, LEONOS_UI_GRAY);
        leonos_ui_dialog(&surface, 0, 0, W, H, title ? title : "Input");
        leonos_ui_text_clipped(&surface, 16, 46, W - 32, label ? label : "",
                               LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        leonos_ui_edit_state_draw(&surface, 16, 72, W - 32, &edit, 0);
        leonos_ui_button(&surface, W - 168, H - 38, 72, LEONOS_UI_BUTTON_H, "OK", 0);
        leonos_ui_button(&surface, W - 88, H - 38, 72, LEONOS_UI_BUTTON_H, "Cancel", 0);
        leonos_gui_present_window((uint32_t)window_id, W, H, W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    result = 1;
                    break;
                }
                if (event.pressed && event.keycode == 1) {
                    break;
                }
                leonos_ui_edit_state_handle_key(&edit, event.keycode, event.pressed);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (event.x >= (int32_t)(W - 168) && event.x < (int32_t)(W - 96) &&
                    event.y >= (int32_t)(H - 38) && event.y < (int32_t)(H - 38 + LEONOS_UI_BUTTON_H)) {
                    result = 1;
                    break;
                }
                if (event.x >= (int32_t)(W - 88) && event.x < (int32_t)(W - 16) &&
                    event.y >= (int32_t)(H - 38) && event.y < (int32_t)(H - 38 + LEONOS_UI_BUTTON_H)) {
                    break;
                }
                leonos_ui_edit_state_handle_mouse(&edit, event.x, event.y, 16, 72, W - 32, event.buttons);
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    if (!result) {
        uint32_t i = 0;
        while (i + 1 < capacity && i + 1 < sizeof(original) && original[i]) {
            value[i] = original[i];
            ++i;
        }
        value[i] = 0;
    }
    return result;
}

struct ui_file_dialog_entry {
    struct leonos_dir_entry dir_entry;
    char display[LEONOS_FS_NAME_LEN + 4];
};

enum {
    UI_FILE_DIALOG_W = 520,
    UI_FILE_DIALOG_H = 404,
    UI_FILE_DIALOG_MAX_ENTRIES = 64,
    UI_FILE_DIALOG_MARGIN = 16,
    UI_FILE_DIALOG_NAV_BUTTON_X = UI_FILE_DIALOG_W - 78,
    UI_FILE_DIALOG_NAV_BUTTON_W = 54,
    UI_FILE_DIALOG_UP_Y = 38,
    UI_FILE_DIALOG_ROOT_Y = 66,
    UI_FILE_DIALOG_LIST_X = 16,
    UI_FILE_DIALOG_LIST_Y = 94,
    UI_FILE_DIALOG_LIST_ROWS = 8,
    UI_FILE_DIALOG_ROW_H = LEONOS_FONT_H + 4,
    UI_FILE_DIALOG_LIST_BODY_X = UI_FILE_DIALOG_LIST_X + 2,
    UI_FILE_DIALOG_LIST_BODY_Y = UI_FILE_DIALOG_LIST_Y + 2,
    UI_FILE_DIALOG_LIST_BODY_W = 404,
    UI_FILE_DIALOG_SCROLL_W = 18,
    UI_FILE_DIALOG_SCROLL_X = UI_FILE_DIALOG_LIST_BODY_X + UI_FILE_DIALOG_LIST_BODY_W,
    UI_FILE_DIALOG_LIST_H = UI_FILE_DIALOG_LIST_ROWS * UI_FILE_DIALOG_ROW_H + 4,
    UI_FILE_DIALOG_LIST_FRAME_W = UI_FILE_DIALOG_LIST_BODY_W + UI_FILE_DIALOG_SCROLL_W + 2,
    UI_FILE_DIALOG_NAME_LABEL_Y = UI_FILE_DIALOG_LIST_Y + UI_FILE_DIALOG_LIST_H + 18,
    UI_FILE_DIALOG_NAME_EDIT_X = 108,
    UI_FILE_DIALOG_NAME_EDIT_Y = UI_FILE_DIALOG_NAME_LABEL_Y - 4,
    UI_FILE_DIALOG_NAME_EDIT_W = UI_FILE_DIALOG_W - UI_FILE_DIALOG_NAME_EDIT_X - UI_FILE_DIALOG_MARGIN,
    UI_FILE_DIALOG_TYPE_LABEL_Y = UI_FILE_DIALOG_NAME_LABEL_Y + 32,
    UI_FILE_DIALOG_TYPE_EDIT_X = 132,
    UI_FILE_DIALOG_TYPE_EDIT_Y = UI_FILE_DIALOG_TYPE_LABEL_Y - 4,
    UI_FILE_DIALOG_TYPE_EDIT_W = UI_FILE_DIALOG_W - UI_FILE_DIALOG_TYPE_EDIT_X - UI_FILE_DIALOG_MARGIN,
    UI_FILE_DIALOG_STATUS_Y = UI_FILE_DIALOG_TYPE_LABEL_Y + 24,
    UI_FILE_DIALOG_STATUS_H = 24,
    UI_FILE_DIALOG_BUTTON_W = 78,
    UI_FILE_DIALOG_BUTTON_Y = UI_FILE_DIALOG_H - 38
};

static void ui_copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    if (src) {
        while (i + 1 < capacity && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static void ui_append_char(char *dst, uint32_t *pos, uint32_t capacity, char ch)
{
    if (!dst || !pos || *pos + 1 >= capacity) {
        return;
    }
    dst[*pos] = ch;
    ++(*pos);
    dst[*pos] = 0;
}

static void ui_append_text(char *dst, uint32_t *pos, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    while (src && src[i]) {
        ui_append_char(dst, pos, capacity, src[i]);
        ++i;
    }
}

static uint32_t ui_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static int ui_text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static char ui_ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int ui_text_eq_ignore_case(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && ui_ascii_lower(*a) == ui_ascii_lower(*b)) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int ui_path_is_root(const char *path)
{
    return ui_text_eq(path, "0:/");
}

static void ui_build_parent_path(char *dst, uint32_t capacity, const char *path)
{
    uint32_t len;
    ui_copy_text(dst, capacity, path);
    if (ui_path_is_root(dst)) {
        return;
    }
    len = ui_strlen(dst);
    while (len > 3 && dst[len - 1] != '/') {
        dst[--len] = 0;
    }
    if (len > 3) {
        dst[len - 1] = 0;
    } else {
        ui_copy_text(dst, capacity, "0:/");
    }
}

static void ui_build_child_path(char *dst, uint32_t capacity,
                                const char *dir, const char *name)
{
    uint32_t pos = 0;
    if (!dst || capacity == 0) {
        return;
    }
    dst[0] = 0;
    ui_append_text(dst, &pos, capacity, dir);
    if (!ui_path_is_root(dir)) {
        ui_append_char(dst, &pos, capacity, '/');
    }
    ui_append_text(dst, &pos, capacity, name);
}

static const char *ui_path_basename(const char *path)
{
    const char *base = path;
    for (uint32_t i = 0; path && path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1;
        }
    }
    return base ? base : "";
}

static int ui_path_extension_matches(const char *name, const char *filter_ext)
{
    uint32_t name_len;
    uint32_t ext_len;
    if (!filter_ext || !filter_ext[0]) {
        return 1;
    }
    name_len = ui_strlen(name);
    ext_len = ui_strlen(filter_ext);
    if (ext_len > name_len) {
        return 0;
    }
    return ui_text_eq_ignore_case(name + name_len - ext_len, filter_ext);
}

static void ui_file_dialog_entry_text(struct ui_file_dialog_entry *entry)
{
    uint32_t pos = 0;
    entry->display[0] = 0;
    if (entry->dir_entry.type == LEONOS_FS_TYPE_DIR) {
        ui_append_text(entry->display, &pos, sizeof(entry->display), "[");
        ui_append_text(entry->display, &pos, sizeof(entry->display), entry->dir_entry.name);
        ui_append_text(entry->display, &pos, sizeof(entry->display), "]");
    } else {
        ui_copy_text(entry->display, sizeof(entry->display), entry->dir_entry.name);
    }
}

static int ui_file_dialog_load_entries(const char *path,
                                       struct ui_file_dialog_entry *entries,
                                       uint32_t capacity,
                                       uint32_t *out_count,
                                       const char *filter_ext)
{
    int fd;
    uint32_t count = 0;
    if (!entries || !out_count) {
        return -1;
    }
    *out_count = 0;
    fd = open(path, 0, 0);
    if (fd < 0) {
        return fd;
    }
    while (count < capacity) {
        int ret = leonos_readdir(fd, &entries[count].dir_entry);
        if (ret < 0) {
            close(fd);
            return ret;
        }
        if (ret == 0) {
            break;
        }
        if (entries[count].dir_entry.type == LEONOS_FS_TYPE_FILE &&
            !ui_path_extension_matches(entries[count].dir_entry.name, filter_ext)) {
            continue;
        }
        ui_file_dialog_entry_text(&entries[count]);
        ++count;
    }
    close(fd);
    *out_count = count;
    return 0;
}

static void ui_file_dialog_status(char *status, uint32_t capacity,
                                  const char *prefix, const char *path)
{
    uint32_t pos = 0;
    if (!status || capacity == 0) {
        return;
    }
    status[0] = 0;
    ui_append_text(status, &pos, capacity, prefix);
    ui_append_text(status, &pos, capacity, path);
}

static void ui_file_dialog_select_entry(const char *dir_path,
                                        const struct ui_file_dialog_entry *entry,
                                        char *filename, uint32_t filename_cap)
{
    (void)dir_path;
    if (!entry || !filename || filename_cap == 0) {
        return;
    }
    if (entry->dir_entry.type == LEONOS_FS_TYPE_FILE) {
        ui_copy_text(filename, filename_cap, entry->dir_entry.name);
    } else {
        ui_copy_text(filename, filename_cap, "");
    }
}

static void ui_file_dialog_sync_name_edit(struct leonos_ui_edit_state *state)
{
    if (!state) {
        return;
    }
    leonos_ui_edit_state_sync(state);
    state->cursor = state->length;
    state->selection_anchor = state->cursor;
    state->scroll = 0;
    state->selecting = 0;
}

static int ui_file_dialog_activate(const char *title, int save_mode,
                                   char *dir_path, uint32_t dir_cap,
                                   char *filename, uint32_t file_cap,
                                   struct ui_file_dialog_entry *entries,
                                   uint32_t entry_cap,
                                   uint32_t *entry_count,
                                   struct leonos_ui_listview_state *list_state,
                                   const char *filter_ext,
                                   char *status, uint32_t status_cap)
{
    char full_path[LEONOS_FS_PATH_LEN];
    struct leonos_stat st;
    int ret;
    if (!save_mode) {
        if (list_state->selected < 0 || (uint32_t)list_state->selected >= *entry_count) {
            ui_file_dialog_status(status, status_cap, "Select a file in ", dir_path);
            return 0;
        }
        if (entries[list_state->selected].dir_entry.type == LEONOS_FS_TYPE_DIR) {
            ui_build_child_path(full_path, sizeof(full_path), dir_path,
                                entries[list_state->selected].dir_entry.name);
            ui_copy_text(dir_path, dir_cap, full_path);
            ui_copy_text(filename, file_cap, "");
            ret = ui_file_dialog_load_entries(dir_path, entries, entry_cap,
                                              entry_count, filter_ext);
            if (ret < 0) {
                ui_file_dialog_status(status, status_cap, "Open dir failed ", dir_path);
                return 0;
            }
            leonos_ui_listview_state_set_count(list_state, *entry_count);
            list_state->selected = *entry_count ? 0 : -1;
            list_state->scroll = 0;
            ui_file_dialog_status(status, status_cap, "Opened ", dir_path);
            return 0;
        }
        ui_build_child_path(full_path, sizeof(full_path), dir_path, filename);
        if (stat(full_path, &st) < 0 || st.type != LEONOS_FS_TYPE_FILE) {
            ui_file_dialog_status(status, status_cap, "File not found ", full_path);
            return 0;
        }
        ui_copy_text(filename, file_cap, full_path);
        return 1;
    }
    if (!filename[0]) {
        ui_file_dialog_status(status, status_cap, "Enter a file name in ", dir_path);
        return 0;
    }
    if (!ui_path_extension_matches(filename, filter_ext)) {
        ui_file_dialog_status(status, status_cap, "File type must match ", filter_ext ? filter_ext : "");
        return 0;
    }
    if (filename[0] == '0' && filename[1] == ':' && filename[2] == '/') {
        ui_copy_text(full_path, sizeof(full_path), filename);
    } else {
        ui_build_child_path(full_path, sizeof(full_path), dir_path, filename);
    }
    if (stat(full_path, &st) == 0 && st.type == LEONOS_FS_TYPE_FILE) {
        if (!leonos_ui_show_confirm_dialog(title ? title : "Save As",
                                           "This file already exists. Replace it?",
                                           0)) {
            ui_file_dialog_status(status, status_cap, "Overwrite canceled for ", full_path);
            return 0;
        }
    }
    ui_copy_text(filename, file_cap, full_path);
    return 1;
}

static void ui_file_dialog_draw(struct leonos_ui_surface *surface,
                                const char *title, int save_mode,
                                const char *dir_path,
                                const char *filter_label,
                                const char *status,
                                struct ui_file_dialog_entry *entries,
                                uint32_t entry_count,
                                struct leonos_ui_listview_state *list_state,
                                struct leonos_ui_edit_state *name_edit)
{
    leonos_ui_rect(surface, 0, 0, UI_FILE_DIALOG_W, UI_FILE_DIALOG_H, LEONOS_UI_GRAY);
    leonos_ui_dialog(surface, 0, 0, UI_FILE_DIALOG_W, UI_FILE_DIALOG_H,
                     title ? title : (save_mode ? "Save As" : "Open"));
    leonos_ui_text(surface, 16, 42, "Look in:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 72, 38, UI_FILE_DIALOG_W - 88, dir_path, ui_strlen(dir_path),
                   0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(surface, 16, 68, "Files:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_scroll_view_frame(surface, UI_FILE_DIALOG_LIST_X, UI_FILE_DIALOG_LIST_Y,
                                UI_FILE_DIALOG_LIST_FRAME_W, UI_FILE_DIALOG_LIST_H);
    for (uint32_t row = 0; row < list_state->visible_rows; ++row) {
        uint32_t index = list_state->scroll + row;
        if (index >= entry_count) {
            break;
        }
        leonos_ui_list_row(surface, UI_FILE_DIALOG_LIST_BODY_X,
                           UI_FILE_DIALOG_LIST_BODY_Y + row * list_state->row_height,
                           UI_FILE_DIALOG_LIST_BODY_W, entries[index].display,
                           list_state->selected == (int32_t)index ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(surface, UI_FILE_DIALOG_SCROLL_X, UI_FILE_DIALOG_LIST_Y,
                         UI_FILE_DIALOG_SCROLL_W, UI_FILE_DIALOG_LIST_H,
                         list_state->scroll,
                         entry_count > list_state->visible_rows ? entry_count : list_state->visible_rows,
                         list_state->visible_rows,
                         entry_count <= list_state->visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_button(surface, UI_FILE_DIALOG_NAV_BUTTON_X, UI_FILE_DIALOG_UP_Y,
                     UI_FILE_DIALOG_NAV_BUTTON_W, LEONOS_UI_BUTTON_H, "Up", 0);
    leonos_ui_button(surface, UI_FILE_DIALOG_NAV_BUTTON_X, UI_FILE_DIALOG_ROOT_Y,
                     UI_FILE_DIALOG_NAV_BUTTON_W, LEONOS_UI_BUTTON_H, "Root", 0);
    leonos_ui_text(surface, 16, UI_FILE_DIALOG_NAME_LABEL_Y,
                   save_mode ? "File name:" : "Selection:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit_state_draw(surface, UI_FILE_DIALOG_NAME_EDIT_X,
                              UI_FILE_DIALOG_NAME_EDIT_Y,
                              UI_FILE_DIALOG_NAME_EDIT_W, name_edit, 0);
    leonos_ui_text(surface, 16, UI_FILE_DIALOG_TYPE_LABEL_Y, "Files of type:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, UI_FILE_DIALOG_TYPE_EDIT_X, UI_FILE_DIALOG_TYPE_EDIT_Y,
                   UI_FILE_DIALOG_TYPE_EDIT_W,
                   filter_label ? filter_label : "All files",
                   ui_strlen(filter_label ? filter_label : "All files"),
                   0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_statusbar(surface, UI_FILE_DIALOG_STATUS_Y, UI_FILE_DIALOG_STATUS_H, status);
    leonos_ui_button(surface, UI_FILE_DIALOG_W - 180, UI_FILE_DIALOG_BUTTON_Y,
                     UI_FILE_DIALOG_BUTTON_W, LEONOS_UI_BUTTON_H,
                     save_mode ? "Save" : "Open", 0);
    leonos_ui_button(surface, UI_FILE_DIALOG_W - 94, UI_FILE_DIALOG_BUTTON_Y,
                     UI_FILE_DIALOG_BUTTON_W, LEONOS_UI_BUTTON_H, "Cancel", 0);
}

static int ui_show_file_dialog_common(const char *title, int save_mode,
                                      char *path, uint32_t capacity,
                                      const char *filter_label,
                                      const char *filter_ext)
{
    static uint32_t pixels[UI_FILE_DIALOG_W * UI_FILE_DIALOG_H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct ui_file_dialog_entry entries[UI_FILE_DIALOG_MAX_ENTRIES];
    struct leonos_ui_listview_state list_state;
    struct leonos_ui_edit_state name_edit;
    char original[LEONOS_FS_PATH_LEN];
    char dir_path[LEONOS_FS_PATH_LEN];
    char file_name[LEONOS_FS_PATH_LEN];
    char status[128];
    uint32_t entry_count = 0;
    int result = 0;
    int window_id;
    int load_ret;
    if (!path || capacity == 0) {
        return -1;
    }
    ui_copy_text(original, sizeof(original), path);
    if (path[0] == '0' && path[1] == ':' && path[2] == '/') {
        ui_build_parent_path(dir_path, sizeof(dir_path), path);
        ui_copy_text(file_name, sizeof(file_name), ui_path_basename(path));
    } else {
        ui_copy_text(dir_path, sizeof(dir_path), "0:/");
        ui_copy_text(file_name, sizeof(file_name), path);
    }
    window_id = leonos_gui_create_app_window_ex(title ? title : (save_mode ? "Save As" : "Open"),
                                                dir_path, UI_FILE_DIALOG_W, UI_FILE_DIALOG_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, UI_FILE_DIALOG_W, UI_FILE_DIALOG_H,
                   UI_FILE_DIALOG_W);
    leonos_ui_listview_state_init(&list_state, UI_FILE_DIALOG_LIST_ROWS,
                                  UI_FILE_DIALOG_ROW_H);
    list_state.focused = 1;
    leonos_ui_edit_state_init(&name_edit, file_name, sizeof(file_name));
    name_edit.focused = save_mode ? 1 : 0;
    ui_file_dialog_sync_name_edit(&name_edit);
    load_ret = ui_file_dialog_load_entries(dir_path, entries, UI_FILE_DIALOG_MAX_ENTRIES,
                                           &entry_count, filter_ext);
    if (load_ret < 0) {
        ui_file_dialog_status(status, sizeof(status), "Open dir failed ", dir_path);
    } else {
        ui_file_dialog_status(status, sizeof(status), "Ready in ", dir_path);
    }
    leonos_ui_listview_state_set_count(&list_state, entry_count);
    list_state.selected = entry_count ? 0 : -1;
    if (!save_mode && list_state.selected >= 0) {
        ui_file_dialog_select_entry(dir_path, &entries[list_state.selected],
                                    file_name, sizeof(file_name));
        ui_file_dialog_sync_name_edit(&name_edit);
    }
    for (;;) {
        ui_file_dialog_draw(&surface, title, save_mode, dir_path, filter_label, status,
                            entries, entry_count, &list_state, &name_edit);
        leonos_gui_present_window((uint32_t)window_id, UI_FILE_DIALOG_W,
                                  UI_FILE_DIALOG_H, UI_FILE_DIALOG_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                uint32_t activated = 0;
                if (event.pressed && event.keycode == 1) {
                    break;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_TAB) {
                    name_edit.focused = name_edit.focused ? 0 : 1;
                    list_state.focused = name_edit.focused ? 0 : 1;
                    continue;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER) {
                    if (ui_file_dialog_activate(title, save_mode, dir_path, sizeof(dir_path),
                                                file_name, sizeof(file_name), entries,
                                                UI_FILE_DIALOG_MAX_ENTRIES,
                                                &entry_count, &list_state, filter_ext,
                                                status, sizeof(status))) {
                        ui_copy_text(path, capacity, file_name);
                        result = 1;
                        break;
                    }
                    ui_file_dialog_sync_name_edit(&name_edit);
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                    continue;
                }
                if (name_edit.focused) {
                    leonos_ui_edit_state_handle_key(&name_edit, event.keycode, event.pressed);
                } else if (leonos_ui_listview_state_handle_key(&list_state, event.keycode, &activated)) {
                    if (list_state.selected >= 0 && (uint32_t)list_state.selected < entry_count) {
                        ui_file_dialog_select_entry(dir_path, &entries[list_state.selected],
                                                    file_name, sizeof(file_name));
                        ui_file_dialog_sync_name_edit(&name_edit);
                    }
                    if (activated &&
                        ui_file_dialog_activate(title, save_mode, dir_path, sizeof(dir_path),
                                                file_name, sizeof(file_name), entries,
                                                UI_FILE_DIALOG_MAX_ENTRIES,
                                                &entry_count, &list_state, filter_ext,
                                                status, sizeof(status))) {
                        ui_copy_text(path, capacity, file_name);
                        result = 1;
                        break;
                    }
                    ui_file_dialog_sync_name_edit(&name_edit);
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                uint32_t activated = 0;
                if (event.x >= (int32_t)(UI_FILE_DIALOG_W - 180) &&
                    event.x < (int32_t)(UI_FILE_DIALOG_W - 102) &&
                    event.y >= (int32_t)UI_FILE_DIALOG_BUTTON_Y &&
                    event.y < (int32_t)(UI_FILE_DIALOG_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    if (ui_file_dialog_activate(title, save_mode, dir_path, sizeof(dir_path),
                                                file_name, sizeof(file_name), entries,
                                                UI_FILE_DIALOG_MAX_ENTRIES,
                                                &entry_count, &list_state, filter_ext,
                                                status, sizeof(status))) {
                        ui_copy_text(path, capacity, file_name);
                        result = 1;
                        break;
                    }
                    ui_file_dialog_sync_name_edit(&name_edit);
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                    continue;
                }
                if (event.x >= (int32_t)(UI_FILE_DIALOG_W - 94) &&
                    event.x < (int32_t)(UI_FILE_DIALOG_W - 16) &&
                    event.y >= (int32_t)UI_FILE_DIALOG_BUTTON_Y &&
                    event.y < (int32_t)(UI_FILE_DIALOG_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    break;
                }
                if (event.x >= (int32_t)UI_FILE_DIALOG_NAV_BUTTON_X &&
                    event.x < (int32_t)(UI_FILE_DIALOG_NAV_BUTTON_X + UI_FILE_DIALOG_NAV_BUTTON_W) &&
                    event.y >= (int32_t)UI_FILE_DIALOG_UP_Y &&
                    event.y < (int32_t)(UI_FILE_DIALOG_UP_Y + LEONOS_UI_BUTTON_H)) {
                    ui_build_parent_path(dir_path, sizeof(dir_path), dir_path);
                    ui_copy_text(file_name, sizeof(file_name), "");
                    ui_file_dialog_sync_name_edit(&name_edit);
                    load_ret = ui_file_dialog_load_entries(dir_path, entries,
                                                           UI_FILE_DIALOG_MAX_ENTRIES,
                                                           &entry_count, filter_ext);
                    if (load_ret < 0) {
                        ui_file_dialog_status(status, sizeof(status), "Open dir failed ", dir_path);
                    } else {
                        ui_file_dialog_status(status, sizeof(status), "Opened ", dir_path);
                    }
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                    list_state.selected = entry_count ? 0 : -1;
                    list_state.scroll = 0;
                    continue;
                }
                if (event.x >= (int32_t)UI_FILE_DIALOG_NAV_BUTTON_X &&
                    event.x < (int32_t)(UI_FILE_DIALOG_NAV_BUTTON_X + UI_FILE_DIALOG_NAV_BUTTON_W) &&
                    event.y >= (int32_t)UI_FILE_DIALOG_ROOT_Y &&
                    event.y < (int32_t)(UI_FILE_DIALOG_ROOT_Y + LEONOS_UI_BUTTON_H)) {
                    ui_copy_text(dir_path, sizeof(dir_path), "0:/");
                    ui_copy_text(file_name, sizeof(file_name), "");
                    ui_file_dialog_sync_name_edit(&name_edit);
                    load_ret = ui_file_dialog_load_entries(dir_path, entries,
                                                           UI_FILE_DIALOG_MAX_ENTRIES,
                                                           &entry_count, filter_ext);
                    if (load_ret < 0) {
                        ui_file_dialog_status(status, sizeof(status), "Open dir failed ", dir_path);
                    } else {
                        ui_file_dialog_status(status, sizeof(status), "Opened ", dir_path);
                    }
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                    list_state.selected = entry_count ? 0 : -1;
                    list_state.scroll = 0;
                    continue;
                }
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_FILE_DIALOG_NAME_EDIT_X,
                                  UI_FILE_DIALOG_NAME_EDIT_Y,
                                  UI_FILE_DIALOG_NAME_EDIT_W,
                                  LEONOS_FONT_H + 8) &&
                    leonos_ui_edit_state_handle_mouse(&name_edit, event.x, event.y,
                                                      UI_FILE_DIALOG_NAME_EDIT_X,
                                                      UI_FILE_DIALOG_NAME_EDIT_Y,
                                                      UI_FILE_DIALOG_NAME_EDIT_W,
                                                      event.buttons)) {
                    name_edit.focused = 1;
                    list_state.focused = 0;
                    continue;
                }
                if (event.x >= (int32_t)UI_FILE_DIALOG_SCROLL_X &&
                    event.x < (int32_t)(UI_FILE_DIALOG_SCROLL_X + UI_FILE_DIALOG_SCROLL_W) &&
                    event.y >= (int32_t)UI_FILE_DIALOG_LIST_Y &&
                    event.y < (int32_t)(UI_FILE_DIALOG_LIST_Y + UI_FILE_DIALOG_LIST_H)) {
                    leonos_ui_vscrollbar_handle_mouse(&list_state.scroll,
                                                       entry_count > list_state.visible_rows ? entry_count : list_state.visible_rows,
                                                       list_state.visible_rows,
                                                       UI_FILE_DIALOG_SCROLL_X,
                                                       UI_FILE_DIALOG_LIST_Y,
                                                       UI_FILE_DIALOG_SCROLL_W,
                                                       UI_FILE_DIALOG_LIST_H,
                                                       event.x, event.y);
                    continue;
                }
                if (leonos_ui_listview_state_handle_mouse(&list_state, event.x, event.y,
                                                          UI_FILE_DIALOG_LIST_BODY_X,
                                                          UI_FILE_DIALOG_LIST_BODY_Y,
                                                          UI_FILE_DIALOG_LIST_BODY_W,
                                                          &activated)) {
                    name_edit.focused = 0;
                    list_state.focused = 1;
                    if (list_state.selected >= 0 && (uint32_t)list_state.selected < entry_count) {
                        ui_file_dialog_select_entry(dir_path, &entries[list_state.selected],
                                                    file_name, sizeof(file_name));
                        ui_file_dialog_sync_name_edit(&name_edit);
                    }
                    if (activated &&
                        ui_file_dialog_activate(title, save_mode, dir_path, sizeof(dir_path),
                                                file_name, sizeof(file_name), entries,
                                                UI_FILE_DIALOG_MAX_ENTRIES,
                                                &entry_count, &list_state, filter_ext,
                                                status, sizeof(status))) {
                        ui_copy_text(path, capacity, file_name);
                        result = 1;
                        break;
                    }
                    ui_file_dialog_sync_name_edit(&name_edit);
                    leonos_ui_listview_state_set_count(&list_state, entry_count);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_FILE_DIALOG_LIST_BODY_X,
                                  UI_FILE_DIALOG_LIST_BODY_Y,
                                  UI_FILE_DIALOG_LIST_BODY_W,
                                  UI_FILE_DIALOG_LIST_ROWS * UI_FILE_DIALOG_ROW_H) ||
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_FILE_DIALOG_SCROLL_X,
                                  UI_FILE_DIALOG_LIST_Y,
                                  UI_FILE_DIALOG_SCROLL_W,
                                  UI_FILE_DIALOG_LIST_H)) {
                    if (leonos_ui_listview_state_handle_wheel(&list_state, event.dy)) {
                        list_state.focused = 1;
                        name_edit.focused = 0;
                    }
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    if (!result) {
        ui_copy_text(path, capacity, original);
    }
    return result;
}

int leonos_ui_show_open_dialog(const char *title, char *path, uint32_t capacity,
                               const char *filter_label, const char *filter_ext)
{
    return ui_show_file_dialog_common(title ? title : "Open", 0,
                                      path, capacity, filter_label, filter_ext);
}

enum {
    UI_OPEN_WITH_W = 432,
    UI_OPEN_WITH_H = 296,
    UI_OPEN_WITH_X = 0,
    UI_OPEN_WITH_Y = 0,
    UI_OPEN_WITH_ROW_H = 34,
    UI_OPEN_WITH_VISIBLE_ROWS = 4,
    UI_OPEN_WITH_LIST_X = 16,
    UI_OPEN_WITH_LIST_Y = 170,
    UI_OPEN_WITH_LIST_W = UI_OPEN_WITH_W - 32,
    UI_OPEN_WITH_SCROLL_W = 18,
    UI_OPEN_WITH_ROW_W = UI_OPEN_WITH_LIST_W - 26,
    UI_OPEN_WITH_BUTTON_Y = UI_OPEN_WITH_H - 38
};

static const struct leonos_launch_assoc_app *ui_open_with_find_app(
    const struct leonos_launch_assoc_app *apps,
    uint32_t app_count,
    const char *program_path)
{
    for (uint32_t i = 0; i < app_count; ++i) {
        if (ui_text_eq(apps[i].program_path, program_path)) {
            return &apps[i];
        }
    }
    return 0;
}

static int ui_open_with_find_index(const struct leonos_launch_assoc_app *apps,
                                   uint32_t app_count,
                                   const char *program_path)
{
    for (uint32_t i = 0; i < app_count; ++i) {
        if (ui_text_eq(apps[i].program_path, program_path)) {
            return (int)i;
        }
    }
    return -1;
}

static const char *ui_open_with_app_label(const struct leonos_launch_assoc_app *apps,
                                          uint32_t app_count,
                                          const char *program_path,
                                          char *buffer,
                                          uint32_t capacity)
{
    const struct leonos_launch_assoc_app *app =
        ui_open_with_find_app(apps, app_count, program_path);
    if (app) {
        return app->name;
    }
    if (!program_path || !program_path[0]) {
        return "None";
    }
    ui_copy_text(buffer, capacity, program_path);
    return buffer;
}

static void ui_open_with_draw(struct leonos_ui_surface *surface,
                              const char *title,
                              const char *path,
                              const char *extension,
                              const char *default_label,
                              const struct leonos_launch_assoc_app *apps,
                              uint32_t app_count,
                              const struct leonos_ui_listview_state *list_state,
                              uint32_t remember,
                              uint32_t can_remember,
                              uint32_t set_default_mode)
{
    uint32_t list_h = list_state->visible_rows * UI_OPEN_WITH_ROW_H + 8;
    uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X + UI_OPEN_WITH_LIST_W - UI_OPEN_WITH_SCROLL_W;
    leonos_ui_rect(surface, 0, 0, UI_OPEN_WITH_W, UI_OPEN_WITH_H, LEONOS_UI_GRAY);
    leonos_ui_dialog(surface, UI_OPEN_WITH_X, UI_OPEN_WITH_Y,
                     UI_OPEN_WITH_W, UI_OPEN_WITH_H, title ? title : "Open With");
    leonos_ui_text_clipped(surface, 16, 44, UI_OPEN_WITH_W - 32,
                           set_default_mode
                               ? "Choose a default program for this file type:"
                               : "Choose a program to open this file:",
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(surface, 16, 68, "File:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 58, 64, UI_OPEN_WITH_W - 74, path ? path : "",
                   ui_strlen(path), 0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(surface, 16, 92, "Extension:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 82, 88, 84,
                   extension && extension[0] ? extension : "(none)",
                   ui_strlen(extension && extension[0] ? extension : "(none)"),
                   0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(surface, 180, 92, "Default:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 244, 88, UI_OPEN_WITH_W - 260,
                   default_label ? default_label : "None",
                   ui_strlen(default_label ? default_label : "None"),
                   0, LEONOS_UI_EDIT_READONLY);
    if (set_default_mode) {
        leonos_ui_checkbox(surface, 16, 118, "Update default program", 1,
                           LEONOS_UI_BUTTON_DISABLED);
    } else {
        leonos_ui_checkbox(surface, 16, 118, "Always use this app",
                           can_remember ? (int)remember : 0,
                           can_remember ? 0 : LEONOS_UI_BUTTON_DISABLED);
    }
    leonos_ui_text(surface, 16, 144, "Programs:", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_inset(surface, UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                    UI_OPEN_WITH_LIST_W, list_h, LEONOS_UI_WHITE);
    for (uint32_t row = 0; row < list_state->visible_rows; ++row) {
        uint32_t i = list_state->scroll + row;
        uint32_t row_x = UI_OPEN_WITH_LIST_X + 4;
        uint32_t row_y = UI_OPEN_WITH_LIST_Y + 4 + row * UI_OPEN_WITH_ROW_H;
        uint32_t selected;
        uint32_t bg;
        uint32_t fg;
        uint32_t detail_fg;
        if (i >= app_count) {
            break;
        }
        selected = list_state->selected == (int32_t)i;
        bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
        fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        detail_fg = selected ? LEONOS_UI_LIGHT : LEONOS_UI_DARK;
        leonos_ui_rect(surface, row_x, row_y, UI_OPEN_WITH_ROW_W,
                       UI_OPEN_WITH_ROW_H, bg);
        leonos_ui_text_clipped(surface, row_x + 8, row_y + 3,
                               UI_OPEN_WITH_ROW_W - 16, apps[i].name, fg, bg);
        leonos_ui_text_clipped(surface, row_x + 8, row_y + 18,
                               UI_OPEN_WITH_ROW_W - 16, apps[i].detail,
                               detail_fg, bg);
    }
    leonos_ui_vscrollbar(surface, scrollbar_x, UI_OPEN_WITH_LIST_Y,
                         UI_OPEN_WITH_SCROLL_W, list_h,
                         list_state->scroll,
                         ui_max_u32(app_count, list_state->visible_rows),
                         list_state->visible_rows,
                         app_count <= list_state->visible_rows
                             ? LEONOS_UI_SCROLLBAR_DISABLED
                             : 0);
    leonos_ui_button(surface, UI_OPEN_WITH_W - 194, UI_OPEN_WITH_BUTTON_Y,
                     96, LEONOS_UI_BUTTON_H,
                     set_default_mode ? "Set Default" : "Open", 0);
    leonos_ui_button(surface, UI_OPEN_WITH_W - 88, UI_OPEN_WITH_BUTTON_Y,
                     72, LEONOS_UI_BUTTON_H, "Cancel", 0);
}

int leonos_ui_show_open_with_dialog(const char *title, const char *path,
                                    char *program_path, uint32_t capacity,
                                    uint32_t *remember, uint32_t flags)
{
    static uint32_t pixels[UI_OPEN_WITH_W * UI_OPEN_WITH_H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct leonos_ui_listview_state list_state;
    const struct leonos_launch_assoc_app *apps;
    uint32_t app_count = 0;
    uint32_t remember_value = remember ? *remember : 0;
    uint32_t set_default_mode = (flags & LEONOS_UI_OPEN_WITH_SET_DEFAULT) != 0;
    uint32_t can_remember;
    char extension[16];
    char default_program[LEONOS_FS_PATH_LEN];
    char default_label_buf[LEONOS_FS_PATH_LEN];
    const char *default_program_ptr;
    const char *default_label;
    int selected = 0;
    int result = 0;
    int window_id;

    if (!path || !path[0] || !program_path || capacity == 0) {
        return -1;
    }
    apps = leonos_launch_assoc_apps(&app_count);
    if (!apps || app_count == 0) {
        return -1;
    }
    extension[0] = 0;
    can_remember = leonos_launch_get_extension_for_path(path, extension,
                                                        sizeof(extension)) != 0;
    default_program[0] = 0;
    default_program_ptr = leonos_launch_resolve_default_app_for_path(path);
    if (default_program_ptr) {
        ui_copy_text(default_program, sizeof(default_program), default_program_ptr);
        selected = ui_open_with_find_index(apps, app_count, default_program);
        if (selected < 0) {
            selected = 0;
        }
    }
    default_label = ui_open_with_app_label(apps, app_count, default_program,
                                           default_label_buf,
                                           sizeof(default_label_buf));
    if (!can_remember) {
        remember_value = 0;
    }
    leonos_ui_listview_state_init(&list_state,
                                  app_count > UI_OPEN_WITH_VISIBLE_ROWS
                                      ? UI_OPEN_WITH_VISIBLE_ROWS
                                      : app_count,
                                  UI_OPEN_WITH_ROW_H);
    leonos_ui_listview_state_set_count(&list_state, app_count);
    list_state.selected = selected;
    list_state.focused = 1;

    window_id = leonos_gui_create_app_window_ex(title ? title : "Open With",
                                                path,
                                                UI_OPEN_WITH_W, UI_OPEN_WITH_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, UI_OPEN_WITH_W, UI_OPEN_WITH_H,
                   UI_OPEN_WITH_W);
    for (;;) {
        uint32_t activated = 0;
        ui_open_with_draw(&surface, title ? title : "Open With", path,
                          extension, default_label, apps, app_count,
                          &list_state, remember_value, can_remember,
                          set_default_mode);
        leonos_gui_present_window((uint32_t)window_id, UI_OPEN_WITH_W,
                                  UI_OPEN_WITH_H, UI_OPEN_WITH_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if ((event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                 event.type == LEONOS_GUI_APP_EVENT_KEY_UP) && event.pressed) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    result = 1;
                    break;
                }
                if (event.keycode == 1) {
                    break;
                }
                if (event.keycode == LEONOS_KEY_SPACE &&
                    !set_default_mode && can_remember) {
                    remember_value = remember_value ? 0 : 1;
                    continue;
                }
                if (leonos_ui_listview_state_handle_key(&list_state,
                                                        event.keycode,
                                                        &activated)) {
                    if (activated) {
                        result = 1;
                        break;
                    }
                    continue;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1u)) {
                uint32_t list_h = list_state.visible_rows * UI_OPEN_WITH_ROW_H + 8;
                uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X +
                                       UI_OPEN_WITH_LIST_W -
                                       UI_OPEN_WITH_SCROLL_W;
                if (event.x >= (int32_t)(UI_OPEN_WITH_W - 194) &&
                    event.x < (int32_t)(UI_OPEN_WITH_W - 98) &&
                    event.y >= (int32_t)UI_OPEN_WITH_BUTTON_Y &&
                    event.y < (int32_t)(UI_OPEN_WITH_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    result = 1;
                    break;
                }
                if (event.x >= (int32_t)(UI_OPEN_WITH_W - 88) &&
                    event.x < (int32_t)(UI_OPEN_WITH_W - 16) &&
                    event.y >= (int32_t)UI_OPEN_WITH_BUTTON_Y &&
                    event.y < (int32_t)(UI_OPEN_WITH_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    break;
                }
                if (!set_default_mode && can_remember &&
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  16, 118, 180, LEONOS_FONT_H + 8)) {
                    remember_value = remember_value ? 0 : 1;
                    continue;
                }
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  scrollbar_x, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_SCROLL_W, list_h)) {
                    leonos_ui_vscrollbar_handle_mouse(&list_state.scroll,
                                                       ui_max_u32(app_count,
                                                                  list_state.visible_rows),
                                                       list_state.visible_rows,
                                                       scrollbar_x,
                                                       UI_OPEN_WITH_LIST_Y,
                                                       UI_OPEN_WITH_SCROLL_W,
                                                       list_h,
                                                       event.x, event.y);
                    continue;
                }
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_LIST_W, list_h)) {
                    if (leonos_ui_listview_state_handle_mouse(&list_state,
                                                              event.x, event.y,
                                                              UI_OPEN_WITH_LIST_X + 4,
                                                              UI_OPEN_WITH_LIST_Y + 4,
                                                              UI_OPEN_WITH_ROW_W,
                                                              &activated) &&
                        activated) {
                        result = 1;
                        break;
                    }
                    continue;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                uint32_t list_h = list_state.visible_rows * UI_OPEN_WITH_ROW_H + 8;
                uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X +
                                       UI_OPEN_WITH_LIST_W -
                                       UI_OPEN_WITH_SCROLL_W;
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_LIST_W, list_h) ||
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  scrollbar_x, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_SCROLL_W, list_h)) {
                    leonos_ui_listview_state_handle_wheel(&list_state, event.dy);
                    continue;
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    if (!result) {
        return 0;
    }
    if (list_state.selected < 0 || (uint32_t)list_state.selected >= app_count) {
        return -1;
    }
    ui_copy_text(program_path, capacity, apps[list_state.selected].program_path);
    if (remember) {
        *remember = set_default_mode ? 1 : remember_value;
    }
    return 1;
}

int leonos_ui_show_save_dialog_ex(const char *title, char *value, uint32_t capacity,
                                  const char *filter_label, const char *filter_ext)
{
    return ui_show_file_dialog_common(title ? title : "Save As", 1,
                                      value, capacity, filter_label, filter_ext);
}

int leonos_ui_show_save_dialog(const char *title, char *value, uint32_t capacity)
{
    return leonos_ui_show_save_dialog_ex(title ? title : "Save As",
                                         value,
                                         capacity,
                                         "All files (*.*)",
                                         0);
}

void leonos_ui_combobox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const char *text, uint32_t open, uint32_t flags)
{
    uint32_t h = LEONOS_FONT_H + 8;
    leonos_ui_edit(surface, x, y, w, text, ui_strlen(text), 0,
                   (flags & LEONOS_UI_EDIT_DISABLED) ? LEONOS_UI_EDIT_DISABLED : 0);
    leonos_ui_button(surface, x + w - h, y, h, h, open ? "^" : "v", flags & LEONOS_UI_EDIT_DISABLED ? LEONOS_UI_BUTTON_DISABLED : 0);
}

uint32_t leonos_ui_dropdown_height(uint32_t count, uint32_t row_h,
                                   uint32_t progress)
{
    uint32_t full_h;
    uint32_t eased;
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    full_h = 8 + count * row_h;
    if (progress > 1000) {
        progress = 1000;
    }
    if (progress == 0 || full_h == 0) {
        return 0;
    }
    eased = leonos_ui_anim_ease_out(progress);
    return (full_h * eased + 999) / 1000;
}

void leonos_ui_dropdown(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const struct leonos_ui_dropdown_item *items,
                        uint32_t count, uint32_t selected_id, uint32_t row_h,
                        uint32_t progress)
{
    uint32_t visible_h = leonos_ui_dropdown_height(count, row_h, progress);
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    if (!visible_h || !w) {
        return;
    }
    leonos_ui_bevel(surface, x, y, w, visible_h, LEONOS_UI_WHITE, 0);
    if (visible_h <= 8) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row_y = y + 4 + i * row_h;
        uint32_t flags = items ? items[i].flags : LEONOS_UI_MENU_DISABLED;
        const char *label = items ? items[i].label : "";
        uint32_t row_bottom = row_y + row_h;
        if (row_y >= y + visible_h - 3) {
            break;
        }
        if (row_bottom > y + visible_h - 3) {
            continue;
        }
        if (flags & LEONOS_UI_MENU_SEPARATOR) {
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2,
                           w > 8 ? w - 8 : w, 1, LEONOS_UI_DARK);
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2 + 1,
                           w > 8 ? w - 8 : w, 1, LEONOS_UI_WHITE);
            continue;
        }
        if (items && items[i].id == selected_id && !(flags & LEONOS_UI_MENU_DISABLED)) {
            leonos_ui_rect(surface, x + 3, row_y, w > 6 ? w - 6 : w,
                           row_h, LEONOS_UI_ACTIVE_TITLE);
            leonos_ui_text_transparent_clipped(surface, x + 8, row_y + 4,
                                               w > 16 ? w - 16 : w,
                                               label ? label : "",
                                               LEONOS_UI_WHITE);
        } else {
            leonos_ui_text_transparent_clipped(surface, x + 8, row_y + 4,
                                               w > 16 ? w - 16 : w,
                                               label ? label : "",
                                               (flags & LEONOS_UI_MENU_DISABLED)
                                                   ? LEONOS_UI_DARK
                                                   : LEONOS_UI_BLACK);
        }
    }
}

int leonos_ui_dropdown_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_dropdown_item *items,
                           uint32_t count, uint32_t row_h, uint32_t progress,
                           uint32_t *out_id)
{
    uint32_t visible_h = leonos_ui_dropdown_height(count, row_h, progress);
    uint32_t index;
    if (out_id) {
        *out_id = 0;
    }
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    if (!visible_h ||
        !leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                       w, visible_h) ||
        py < (int32_t)y + 4) {
        return 0;
    }
    index = ((uint32_t)py - y - 4) / row_h;
    if (!items || index >= count ||
        (items[index].flags & (LEONOS_UI_MENU_SEPARATOR | LEONOS_UI_MENU_DISABLED))) {
        return 1;
    }
    if (out_id) {
        *out_id = items[index].id;
    }
    return 1;
}

void leonos_ui_radio(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     const char *label, int checked, uint32_t flags)
{
    uint32_t fg = (flags & LEONOS_UI_BUTTON_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
    leonos_ui_rect(surface, x + 3, y + 2, 8, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(surface, x + 2, y + 3, 10, 1, LEONOS_UI_BLACK);
    leonos_ui_rect(surface, x + 1, y + 4, 12, 8, LEONOS_UI_BLACK);
    leonos_ui_rect(surface, x + 2, y + 5, 10, 6, LEONOS_UI_WHITE);
    if (checked) {
        leonos_ui_rect(surface, x + 5, y + 7, 4, 2, LEONOS_UI_BLACK);
    }
    leonos_ui_text_transparent(surface, x + 22, y, label, fg);
}

void leonos_ui_groupbox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, const char *title)
{
    leonos_ui_rect(surface, x, y + 8, w, 1, LEONOS_UI_DARK);
    leonos_ui_rect(surface, x, y + h - 1, w, 1, LEONOS_UI_WHITE);
    leonos_ui_rect(surface, x, y + 8, 1, h - 8, LEONOS_UI_DARK);
    leonos_ui_rect(surface, x + w - 1, y + 8, 1, h - 8, LEONOS_UI_WHITE);
    leonos_ui_rect(surface, x + 8, y, leonos_ui_text_width(title) + 8, LEONOS_FONT_H, LEONOS_UI_WHITE);
    leonos_ui_text_transparent(surface, x + 12, y, title, LEONOS_UI_BLACK);
}

void leonos_ui_tabs(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const char *const labels[], uint32_t count,
                    uint32_t active)
{
    uint32_t tab_x = x;
    uint32_t tab_h = LEONOS_FONT_H + 10;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t tw = leonos_ui_text_width(labels[i]) + 22;
        if (tab_x + tw > x + w) {
            tw = x + w - tab_x;
        }
        leonos_ui_bevel(surface, tab_x, y, tw, tab_h, i == active ? LEONOS_UI_WHITE : LEONOS_UI_GRAY, 0);
        leonos_ui_text_transparent_clipped(surface, tab_x + 10, y + 5, tw > 20 ? tw - 20 : tw,
                                           labels[i], LEONOS_UI_BLACK);
        tab_x += tw;
        if (tab_x >= x + w) {
            break;
        }
    }
}

int leonos_ui_tabs_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                       uint32_t w, const char *const labels[], uint32_t count)
{
    uint32_t tab_x = x;
    uint32_t tab_h = LEONOS_FONT_H + 10;
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y, w, tab_h)) {
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t tw = leonos_ui_text_width(labels[i]) + 22;
        if (tab_x + tw > x + w) {
            tw = x + w - tab_x;
        }
        if ((uint32_t)px >= tab_x && (uint32_t)px < tab_x + tw) {
            return (int)i;
        }
        tab_x += tw;
        if (tab_x >= x + w) {
            break;
        }
    }
    return -1;
}

void leonos_ui_tab_body(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h)
{
    leonos_ui_inset(surface, x, y, w, h, LEONOS_UI_WHITE);
}

void leonos_ui_statusbar(struct leonos_ui_surface *surface, uint32_t y, uint32_t h,
                         const char *text)
{
    uint32_t w = surface ? surface->width : 0;
    leonos_ui_bevel(surface, 0, y, w, h, LEONOS_UI_GRAY, 0);
    leonos_ui_text_transparent_clipped(surface, 8, y + (h > LEONOS_FONT_H ? (h - LEONOS_FONT_H) / 2 : 0),
                                       w > 16 ? w - 16 : w, text, LEONOS_UI_BLACK);
}

void leonos_ui_toolbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h)
{
    leonos_ui_rect(surface, x, y, w, h, LEONOS_UI_GRAY);
    leonos_ui_rect(surface, x, y + h - 2, w, 1, LEONOS_UI_DARK);
    leonos_ui_rect(surface, x, y + h - 1, w, 1, LEONOS_UI_WHITE);
}

void leonos_ui_toolbar_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, const char *label, uint32_t flags)
{
    leonos_ui_button(surface, x, y, w, LEONOS_UI_BUTTON_H, label, flags);
}

void leonos_ui_splitter(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t vertical)
{
    leonos_ui_rect(surface, x, y, w, h, LEONOS_UI_GRAY);
    if (vertical) {
        leonos_ui_rect(surface, x, y, 1, h, LEONOS_UI_DARK);
        leonos_ui_rect(surface, x + w - 1, y, 1, h, LEONOS_UI_WHITE);
    } else {
        leonos_ui_rect(surface, x, y, w, 1, LEONOS_UI_DARK);
        leonos_ui_rect(surface, x, y + h - 1, w, 1, LEONOS_UI_WHITE);
    }
}

void leonos_ui_menubar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                       uint32_t w)
{
    leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_GRAY);
    leonos_ui_rect(surface, x, y + LEONOS_FONT_H + 7, w, 1, LEONOS_UI_DARK);
}

void leonos_ui_menubar_item(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const char *label, uint32_t active)
{
    if (active) {
        leonos_ui_bevel(surface, x, y + 2, w, LEONOS_FONT_H + 4, LEONOS_UI_LIGHT, LEONOS_UI_BUTTON_PRESSED);
    }
    leonos_ui_text_transparent_clipped(surface, x + 8, y + 4, w > 16 ? w - 16 : w, label, LEONOS_UI_BLACK);
}
