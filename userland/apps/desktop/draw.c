#include "desktop.h"

struct rect rect_make(int x, int y, int w, int h)
{
    struct rect r = {x, y, w, h};
    return r;
}

struct rect window_rect(uint8_t id)
{
    if (id >= MAX_WINDOWS || !windows[id].visible || windows[id].minimized) {
        return rect_make(0, 0, 0, 0);
    }
    return rect_make(windows[id].x, windows[id].y, (int)windows[id].width, (int)windows[id].height);
}

struct rect cursor_rect_at(uint32_t x, uint32_t y)
{
    /* Cover both the previous and next hotspot when a hover style changes. */
    return rect_make((int)x - CURSOR_TILE_W, (int)y - CURSOR_TILE_H,
                     CURSOR_TILE_W * 2, CURSOR_TILE_H * 2);
}

struct rect rect_union(struct rect a, struct rect b)
{
    if (a.w <= 0 || a.h <= 0) {
        return b;
    }
    if (b.w <= 0 || b.h <= 0) {
        return a;
    }
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1a = a.x + a.w;
    int y1a = a.y + a.h;
    int x1b = b.x + b.w;
    int y1b = b.y + b.h;
    int x1 = x1a > x1b ? x1a : x1b;
    int y1 = y1a > y1b ? y1a : y1b;
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

struct rect rect_pad(struct rect r, int pad)
{
    return rect_make(r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2);
}

struct rect rect_clip(struct rect r)
{
    int max_w = (int)fb_w();
    int max_h = (int)fb_h();
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.x + r.w > max_w) {
        r.w = max_w - r.x;
    }
    if (r.y + r.h > max_h) {
        r.h = max_h - r.y;
    }
    if (r.w < 0) {
        r.w = 0;
    }
    if (r.h < 0) {
        r.h = 0;
    }
    return r;
}

int rect_intersects(struct rect a, struct rect b)
{
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
           a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    leonos_ui_pixel(&ui, x, y, color);
}

void put_pixel_i(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb_w() || y >= (int)fb_h()) {
        return;
    }
    leonos_ui_pixel(&ui, (uint32_t)x, (uint32_t)y, color);
}

void rect_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    leonos_ui_rect(&ui, x, y, w, h, color);
}

void rect_fill_i(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    struct rect r = rect_clip(rect_make(x, y, w, h));
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    leonos_ui_rect(&ui, (uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h, color);
}

void bevel_i(int x, int y, int w, int h, uint32_t fill, uint32_t flags)
{
    if (leonos_ui_theme() == LEONOS_UI_THEME_METRO) {
        uint32_t border = (flags & LEONOS_UI_BUTTON_PRESSED)
                              ? LEONOS_UI_ACTIVE_TITLE
                              : leonos_ui_color(LEONOS_UI_COLOR_BORDER);
        rect_fill_i(x, y, w, h, fill);
        rect_fill_i(x, y, w, 1, border);
        rect_fill_i(x, y, 1, h, border);
        rect_fill_i(x + w - 1, y, 1, h, border);
        rect_fill_i(x, y + h - 1, w, 1, border);
        return;
    }
    uint32_t tl = (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_DARK : LEONOS_UI_WHITE;
    uint32_t br = (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_WHITE : LEONOS_UI_DARK;
    if (w <= 0 || h <= 0) {
        return;
    }
    rect_fill_i(x, y, w, h, fill);
    rect_fill_i(x, y, w, 1, tl);
    rect_fill_i(x, y, 1, h, tl);
    rect_fill_i(x + w - 1, y, 1, h, br);
    rect_fill_i(x, y + h - 1, w, 1, br);
    if (w > 2 && h > 2) {
        rect_fill_i(x + 1, y + 1, w - 2, 1,
                    (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_BLACK : LEONOS_UI_LIGHT);
        rect_fill_i(x + 1, y + 1, 1, h - 2,
                    (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_BLACK : LEONOS_UI_LIGHT);
        rect_fill_i(x + w - 2, y + 1, 1, h - 2,
                    (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_LIGHT : LEONOS_UI_DARK);
        rect_fill_i(x + 1, y + h - 2, w - 2, 1,
                    (flags & LEONOS_UI_BUTTON_PRESSED) ? LEONOS_UI_LIGHT : LEONOS_UI_DARK);
    }
}

#define APP_ICON_CACHE_MAX 64

struct app_icon_cache_entry {
    uint8_t used;
    char path[LEONOS_FS_PATH_LEN];
    uint32_t width;
    uint32_t height;
    uint32_t pixels[APP_ICON_MAX_W * APP_ICON_MAX_H];
};

static struct app_icon_cache_entry app_icon_cache[APP_ICON_CACHE_MAX];
static uint32_t app_icon_cache_next;
static uint8_t bmp_scratch[WALLPAPER_BMP_MAX_BYTES];

static int load_bmp_argb(const char *path, uint32_t max_w, uint32_t max_h,
                         uint32_t max_bytes, uint32_t *out_pixels,
                         uint32_t out_stride, uint32_t *out_w, uint32_t *out_h);

static int draw_icon_pixels(const struct app_icon_cache_entry *entry, int x, int y,
                            uint32_t draw_w, uint32_t draw_h)
{
    if (!entry || !entry->used || !entry->width || !entry->height ||
        !draw_w || !draw_h) {
        return 0;
    }
    for (uint32_t yy = 0; yy < draw_h; ++yy) {
        uint32_t src_y = yy * entry->height / draw_h;
        if (src_y >= entry->height) {
            src_y = entry->height - 1;
        }
        for (uint32_t xx = 0; xx < draw_w; ++xx) {
            uint32_t src_x = xx * entry->width / draw_w;
            uint32_t argb;
            if (src_x >= entry->width) {
                src_x = entry->width - 1;
            }
            argb = entry->pixels[src_y * APP_ICON_MAX_W + src_x];
            if ((argb >> 24) == 0) {
                continue;
            }
            put_pixel_i(x + (int)xx, y + (int)yy, argb & 0x00ffffffu);
        }
    }
    return 1;
}

static const struct app_icon_cache_entry *load_cached_app_icon(const char *icon_path)
{
    struct app_icon_cache_entry *entry;
    if (!icon_path || !icon_path[0]) {
        return 0;
    }
    for (uint32_t i = 0; i < APP_ICON_CACHE_MAX; ++i) {
        if (app_icon_cache[i].used && text_eq(app_icon_cache[i].path, icon_path)) {
            return &app_icon_cache[i];
        }
    }
    entry = &app_icon_cache[app_icon_cache_next++ % APP_ICON_CACHE_MAX];
    entry->used = 0;
    entry->path[0] = 0;
    entry->width = 0;
    entry->height = 0;
    if (!load_bmp_argb(icon_path, APP_ICON_MAX_W, APP_ICON_MAX_H,
                       APP_ICON_BMP_MAX_BYTES, entry->pixels,
                       APP_ICON_MAX_W, &entry->width, &entry->height)) {
        return 0;
    }
    copy_text(entry->path, sizeof(entry->path), icon_path);
    entry->used = 1;
    return entry;
}

static void draw_fallback_app_icon(int x, int y, uint32_t w, uint32_t h)
{
    int doc_w;
    int doc_h;
    int doc_x;
    int doc_y;
    int fold;
    if (w < 8 || h < 8) {
        return;
    }
    rect_fill_i(x, y, (int)w, (int)h, LEONOS_UI_GRAY);
    rect_fill_i(x, y, (int)w, 1, LEONOS_UI_WHITE);
    rect_fill_i(x, y, 1, (int)h, LEONOS_UI_WHITE);
    rect_fill_i(x + (int)w - 1, y, 1, (int)h, LEONOS_UI_BLACK);
    rect_fill_i(x, y + (int)h - 1, (int)w, 1, LEONOS_UI_BLACK);

    doc_w = (int)w / 2;
    doc_h = (int)h * 5 / 8;
    if (doc_w < 8) {
        doc_w = 8;
    }
    if (doc_h < 10) {
        doc_h = 10;
    }
    if (doc_w + 4 > (int)w) {
        doc_w = (int)w - 4;
    }
    if (doc_h + 4 > (int)h) {
        doc_h = (int)h - 4;
    }
    doc_x = x + ((int)w - doc_w) / 2;
    doc_y = y + ((int)h - doc_h) / 2;
    fold = doc_w / 4;
    if (fold < 2) {
        fold = 2;
    }
    rect_fill_i(doc_x, doc_y, doc_w, doc_h, 0x00ffffff);
    rect_fill_i(doc_x, doc_y, doc_w, 1, LEONOS_UI_DARK);
    rect_fill_i(doc_x, doc_y, 1, doc_h, LEONOS_UI_DARK);
    rect_fill_i(doc_x + doc_w - 1, doc_y, 1, doc_h, LEONOS_UI_DARK);
    rect_fill_i(doc_x, doc_y + doc_h - 1, doc_w, 1, LEONOS_UI_DARK);
    rect_fill_i(doc_x + doc_w - fold, doc_y + 1, fold - 1, fold - 1, 0x00d8d8d8);
    rect_fill_i(doc_x + 2, doc_y + doc_h / 3, doc_w - 4, 1, 0x00000080);
    rect_fill_i(doc_x + 2, doc_y + doc_h / 2, doc_w - 5, 1, 0x00000080);
}

static void draw_app_icon_sized(const char *icon_path, int x, int y,
                                uint32_t draw_w, uint32_t draw_h)
{
    if (draw_icon_pixels(load_cached_app_icon(icon_path), x, y, draw_w, draw_h)) {
        return;
    }
    draw_fallback_app_icon(x, y, draw_w, draw_h);
}

void draw_app_icon(const char *icon_path, int x, int y)
{
    draw_app_icon_sized(icon_path, x, y, APP_ICON_SMALL_W, APP_ICON_SMALL_H);
}

void draw_app_icon_large(const char *icon_path, int x, int y)
{
    draw_app_icon_sized(icon_path, x, y, APP_ICON_LARGE_W, APP_ICON_LARGE_H);
}

void text_draw(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    leonos_ui_text(&ui, x, y, text, fg, bg);
}

void text_draw_i(int x, int y, const char *text, uint32_t fg, uint32_t bg)
{
    if (x >= 0 && y >= 0) {
        leonos_ui_text(&ui, (uint32_t)x, (uint32_t)y, text, fg, bg);
        return;
    }
    for (uint32_t i = 0; text && text[i]; ++i) {
        int gx = x + (int)i * (int)LEONOS_FONT_W;
        if (gx + (int)LEONOS_FONT_W <= 0 || gx >= (int)fb_w() ||
            y + (int)LEONOS_FONT_H <= 0 || y >= (int)fb_h()) {
            continue;
        }
        for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
            for (uint32_t col = 0; col < LEONOS_FONT_W; ++col) {
                uint32_t color = bg;
                const uint8_t *glyph = leonos_psf_glyph(text[i]);
                if (glyph[row] & (uint8_t)(0x80u >> col)) {
                    color = fg;
                }
                put_pixel_i(gx + (int)col, y + (int)row, color);
            }
        }
    }
}

void text_draw_transparent_i(int x, int y, const char *text, uint32_t fg)
{
    if (x >= 0 && y >= 0) {
        leonos_ui_text_transparent(&ui, (uint32_t)x, (uint32_t)y, text, fg);
        return;
    }
    for (uint32_t i = 0; text && text[i]; ++i) {
        int gx = x + (int)i * (int)LEONOS_FONT_W;
        if (gx + (int)LEONOS_FONT_W <= 0 || gx >= (int)fb_w() ||
            y + (int)LEONOS_FONT_H <= 0 || y >= (int)fb_h()) {
            continue;
        }
        const uint8_t *glyph = leonos_psf_glyph(text[i]);
        for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
            for (uint32_t col = 0; col < LEONOS_FONT_W; ++col) {
                if (glyph[row] & (uint8_t)(0x80u >> col)) {
                    put_pixel_i(gx + (int)col, y + (int)row, fg);
                }
            }
        }
    }
}

static const char *window_button_icon_path(char label)
{
    switch (label) {
    case '_':
        return WINDOW_BUTTON_MINIMIZE_ICON_PATH;
    case 'M':
    case 'm':
        return WINDOW_BUTTON_MAXIMIZE_ICON_PATH;
    case 'r':
    case 'R':
        return WINDOW_BUTTON_RESTORE_ICON_PATH;
    case 'X':
    case 'x':
        return WINDOW_BUTTON_CLOSE_ICON_PATH;
    default:
        return 0;
    }
}

static int draw_window_button_icon(const char *icon_path, int x, int y,
                                   uint32_t draw_w, uint32_t draw_h,
                                   uint32_t color)
{
    const struct app_icon_cache_entry *entry = load_cached_app_icon(icon_path);
    if (!entry || !entry->used || !entry->width || !entry->height ||
        !draw_w || !draw_h) {
        return 0;
    }
    for (uint32_t yy = 0; yy < draw_h; ++yy) {
        uint32_t src_y = yy * entry->height / draw_h;
        if (src_y >= entry->height) {
            src_y = entry->height - 1;
        }
        for (uint32_t xx = 0; xx < draw_w; ++xx) {
            uint32_t src_x = xx * entry->width / draw_w;
            uint32_t argb;
            if (src_x >= entry->width) {
                src_x = entry->width - 1;
            }
            argb = entry->pixels[src_y * APP_ICON_MAX_W + src_x];
            if ((argb >> 24) == 0) {
                continue;
            }
            put_pixel_i(x + (int)xx, y + (int)yy, color);
        }
    }
    return 1;
}

static void draw_window_button_outline_symbol(int x, int y, int w, int h,
                                              uint32_t color)
{
    rect_fill_i(x, y, w, 1, color);
    rect_fill_i(x, y, 1, h, color);
    rect_fill_i(x + w - 1, y, 1, h, color);
    rect_fill_i(x, y + h - 1, w, 1, color);
}

static void draw_fallback_window_button_symbol(int x, int y, char label,
                                               uint32_t color)
{
    switch (label) {
    case '_':
        rect_fill_i(x + 3, y + 12, 10, 2, color);
        break;
    case 'M':
    case 'm':
        draw_window_button_outline_symbol(x + 3, y + 3, 10, 9, color);
        rect_fill_i(x + 4, y + 4, 8, 1, color);
        break;
    case 'r':
    case 'R':
        draw_window_button_outline_symbol(x + 5, y + 3, 8, 7, color);
        rect_fill_i(x + 6, y + 4, 6, 1, color);
        draw_window_button_outline_symbol(x + 3, y + 6, 8, 7, color);
        rect_fill_i(x + 4, y + 7, 6, 1, color);
        break;
    case 'X':
    case 'x':
        for (int i = 0; i < 8; ++i) {
            rect_fill_i(x + 4 + i, y + 4 + i, 2, 2, color);
            rect_fill_i(x + 4 + i, y + 11 - i, 2, 2, color);
        }
        break;
    default:
        break;
    }
}

void window_button_i(int x, int y, char label, uint32_t flags)
{
    int pressed = (flags & LEONOS_UI_BUTTON_PRESSED) != 0;
    uint32_t icon_color = (flags & LEONOS_UI_BUTTON_DISABLED) ?
        LEONOS_UI_DARK : LEONOS_UI_BLACK;
    int icon_x = x + ((int)LEONOS_UI_WINDOW_BUTTON_W - WINDOW_BUTTON_ICON_W) / 2 + pressed;
    int icon_y = y + ((int)LEONOS_UI_WINDOW_BUTTON_H - WINDOW_BUTTON_ICON_H) / 2 + pressed;
    const char *icon_path = window_button_icon_path(label);
    bevel_i(x, y, LEONOS_UI_WINDOW_BUTTON_W, LEONOS_UI_WINDOW_BUTTON_H,
            LEONOS_UI_GRAY, flags);
    if (!draw_window_button_icon(icon_path, icon_x, icon_y,
                                 WINDOW_BUTTON_ICON_W, WINDOW_BUTTON_ICON_H,
                                 icon_color)) {
        draw_fallback_window_button_symbol(icon_x, icon_y, label, icon_color);
    }
}

void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        append_char(buf, pos, cap, text[i]);
    }
}

void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[20];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

void append_hex_fixed(char *buf, uint32_t *pos, uint32_t cap, uint64_t value, uint32_t digits)
{
    const char *hex = "0123456789abcdef";
    append_text(buf, pos, cap, "0x");
    for (int32_t shift = (int32_t)(digits * 4); shift > 0; shift -= 4) {
        append_char(buf, pos, cap, hex[(value >> (uint32_t)(shift - 4)) & 0xf]);
    }
}

static int load_bmp_argb(const char *path, uint32_t max_w, uint32_t max_h,
                         uint32_t max_bytes, uint32_t *out_pixels,
                         uint32_t out_stride, uint32_t *out_w, uint32_t *out_h)
{
    uint8_t *bmp = bmp_scratch;
    int fd;
    struct leonos_stat st;
    uint32_t len = 0;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t height_signed;
    uint32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t row_stride;
    int top_down;

    if (!path || !out_pixels || max_w == 0 || max_h == 0 ||
        out_stride < max_w || max_bytes > sizeof(bmp_scratch)) {
        return 0;
    }
    if (stat(path, &st) < 0 || st.type != LEONOS_FS_TYPE_FILE ||
        st.size < 54 || st.size > max_bytes) {
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    while (len < st.size && len < max_bytes) {
        long got = read(fd, bmp + len, (uint32_t)st.size - len);
        if (got < 0) {
            close(fd);
            return 0;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    if (len < 54 || bmp[0] != 'B' || bmp[1] != 'M') {
        return 0;
    }

    pixel_offset = read_le32(bmp + 10);
    dib_size = read_le32(bmp + 14);
    if (dib_size < 40 || pixel_offset >= len) {
        return 0;
    }
    width = read_le32s(bmp + 18);
    height_signed = read_le32s(bmp + 22);
    planes = read_le16(bmp + 26);
    bpp = read_le16(bmp + 28);
    compression = read_le32(bmp + 30);
    if (width <= 0 || height_signed == 0 || planes != 1 ||
        (bpp != 24 && bpp != 32) || compression != 0) {
        return 0;
    }
    top_down = height_signed < 0;
    height = top_down ? (uint32_t)(-height_signed) : (uint32_t)height_signed;
    if ((uint32_t)width > max_w || height > max_h) {
        return 0;
    }
    row_stride = ((((uint32_t)width * bpp) + 31u) / 32u) * 4u;
    if (pixel_offset + row_stride * height > len) {
        return 0;
    }
    for (uint32_t y = 0; y < max_h; ++y) {
        for (uint32_t x = 0; x < max_w; ++x) {
            out_pixels[y * out_stride + x] = 0;
        }
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = top_down ? y : height - 1u - y;
        const uint8_t *row = bmp + pixel_offset + src_y * row_stride;
        for (uint32_t x = 0; x < (uint32_t)width; ++x) {
            const uint8_t *px = row + x * (bpp / 8u);
            uint32_t b = px[0];
            uint32_t g = px[1];
            uint32_t r = px[2];
            uint32_t a = bpp == 32 ? px[3] : 0xffu;
            if (bpp == 24 && r == 0xffu && g == 0 && b == 0xffu) {
                a = 0;
            }
            out_pixels[y * out_stride + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    if (out_w) {
        *out_w = (uint32_t)width;
    }
    if (out_h) {
        *out_h = height;
    }
    return 1;
}

int load_cursor_bmp(void)
{
    uint32_t atlas_width = 0;
    uint32_t atlas_height = 0;
    cursor_bitmap_loaded = 0;
    cursor_width = CURSOR_TILE_W;
    cursor_height = CURSOR_TILE_H;
    if (!load_bmp_argb(CURSOR_BMP_PATH, CURSOR_MAX_W, CURSOR_MAX_H,
                       CURSOR_BMP_MAX_BYTES, cursor_pixels, CURSOR_MAX_W,
                       &atlas_width, &atlas_height) ||
        atlas_width != CURSOR_TILE_W || atlas_height != CURSOR_MAX_H) {
        cursor_width = FALLBACK_CURSOR_W;
        cursor_height = FALLBACK_CURSOR_H;
        return 0;
    }
    cursor_bitmap_loaded = 1;
    printf("[desktop.elf] loaded cursor atlas %s %dx%d (%d styles)\n",
           CURSOR_BMP_PATH, (int)atlas_width, (int)atlas_height,
           CURSOR_STYLE_COUNT);
    return 1;
}

int load_wallpaper_bmp(void)
{
    wallpaper_loaded = 0;
    wallpaper_width = 0;
    wallpaper_height = 0;
    if (!desktop_wallpaper_path[0]) {
        return 0;
    }
    if (!load_bmp_argb(desktop_wallpaper_path, WALLPAPER_MAX_W, WALLPAPER_MAX_H,
                       WALLPAPER_BMP_MAX_BYTES, wallpaper_pixels, WALLPAPER_MAX_W,
                       &wallpaper_width, &wallpaper_height)) {
        return 0;
    }
    wallpaper_loaded = 1;
    wallpaper_retry_ms = 0;
    printf("[desktop.elf] loaded wallpaper %s %dx%d\n",
           desktop_wallpaper_path, (int)wallpaper_width, (int)wallpaper_height);
    return 1;
}

char lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

int keycode_to_ascii(uint8_t keycode, char *out)
{
    if (!out) {
        return 0;
    }
    switch (keycode) {
    case 2: *out = '1'; return 1;
    case 3: *out = '2'; return 1;
    case 4: *out = '3'; return 1;
    case 5: *out = '4'; return 1;
    case 6: *out = '5'; return 1;
    case 7: *out = '6'; return 1;
    case 8: *out = '7'; return 1;
    case 9: *out = '8'; return 1;
    case 10: *out = '9'; return 1;
    case 11: *out = '0'; return 1;
    case 16: *out = 'q'; return 1;
    case 17: *out = 'w'; return 1;
    case 18: *out = 'e'; return 1;
    case 19: *out = 'r'; return 1;
    case 20: *out = 't'; return 1;
    case 21: *out = 'y'; return 1;
    case 22: *out = 'u'; return 1;
    case 23: *out = 'i'; return 1;
    case 24: *out = 'o'; return 1;
    case 25: *out = 'p'; return 1;
    case 30: *out = 'a'; return 1;
    case 31: *out = 's'; return 1;
    case 32: *out = 'd'; return 1;
    case 33: *out = 'f'; return 1;
    case 34: *out = 'g'; return 1;
    case 35: *out = 'h'; return 1;
    case 36: *out = 'j'; return 1;
    case 37: *out = 'k'; return 1;
    case 38: *out = 'l'; return 1;
    case 44: *out = 'z'; return 1;
    case 45: *out = 'x'; return 1;
    case 46: *out = 'c'; return 1;
    case 47: *out = 'v'; return 1;
    case 48: *out = 'b'; return 1;
    case 49: *out = 'n'; return 1;
    case 50: *out = 'm'; return 1;
    default:
        return 0;
    }
}

const char *task_state_name(uint32_t state)
{
    switch (state) {
    case 0:
        return leonos_i18n("ready", "就绪");
    case 1:
        return leonos_i18n("run", "运行");
    case 2:
        return leonos_i18n("sleep", "睡眠");
    case 3:
        return leonos_i18n("exit", "退出");
    default:
        return "?";
    }
}

const char *task_kind_name(uint32_t kind)
{
    return kind == 1 ? leonos_i18n("user", "用户") : leonos_i18n("kern", "内核");
}

void task_line(char *buf, uint32_t cap, const struct leonos_task_info *task)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, task->pid);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->parent_pid);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task_state_name(task->state));
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task_kind_name(task->kind));
    append_text(buf, &pos, cap, "  ");
    append_hex_fixed(buf, &pos, cap, task->cr3, 8);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->wake_tick);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task->name);
}

void refresh_task_snapshot(void)
{
    int count = leonos_task_snapshot(task_infos, LEONOS_TASK_MAX, &task_info_tick);
    task_info_count = count > 0 ? (uint32_t)count : 0;
    last_task_refresh = leonos_uptime_ms();
}

uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}
