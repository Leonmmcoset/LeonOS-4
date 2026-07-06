#include <leonos/syscall.h>
#include <leonos/ui.h>

#include "ui_internal.h"

#define UI_WINDOW_BUTTON_ICON_W 16U
#define UI_WINDOW_BUTTON_ICON_H 16U
#define UI_WINDOW_BUTTON_ICON_MAX_BYTES (UI_WINDOW_BUTTON_ICON_W * UI_WINDOW_BUTTON_ICON_H * 4U + 128U)
#define UI_WINDOW_BUTTON_MINIMIZE_ICON_PATH "0:/system/resources/window-button-minimize.bmp"
#define UI_WINDOW_BUTTON_MAXIMIZE_ICON_PATH "0:/system/resources/window-button-maximize.bmp"
#define UI_WINDOW_BUTTON_RESTORE_ICON_PATH "0:/system/resources/window-button-restore.bmp"
#define UI_WINDOW_BUTTON_CLOSE_ICON_PATH "0:/system/resources/window-button-close.bmp"

static uint16_t ui_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ui_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t ui_read_le32s(const uint8_t *p)
{
    return (int32_t)ui_read_le32(p);
}

struct ui_window_button_icon {
    const char *path;
    uint8_t checked;
    uint8_t loaded;
    uint32_t width;
    uint32_t height;
    uint32_t pixels[UI_WINDOW_BUTTON_ICON_W * UI_WINDOW_BUTTON_ICON_H];
};

enum {
    UI_WINDOW_BUTTON_ICON_MINIMIZE = 0,
    UI_WINDOW_BUTTON_ICON_MAXIMIZE = 1,
    UI_WINDOW_BUTTON_ICON_RESTORE = 2,
    UI_WINDOW_BUTTON_ICON_CLOSE = 3,
};

static struct ui_window_button_icon ui_window_button_icons[] = {
    {UI_WINDOW_BUTTON_MINIMIZE_ICON_PATH, 0, 0, 0, 0, {0}},
    {UI_WINDOW_BUTTON_MAXIMIZE_ICON_PATH, 0, 0, 0, 0, {0}},
    {UI_WINDOW_BUTTON_RESTORE_ICON_PATH, 0, 0, 0, 0, {0}},
    {UI_WINDOW_BUTTON_CLOSE_ICON_PATH, 0, 0, 0, 0, {0}},
};

static int ui_load_bmp_argb(const char *path, uint32_t max_w, uint32_t max_h,
                            uint32_t max_bytes, uint32_t *out_pixels,
                            uint32_t out_stride, uint32_t *out_w,
                            uint32_t *out_h)
{
    uint8_t bmp[UI_WINDOW_BUTTON_ICON_MAX_BYTES];
    struct leonos_stat st;
    int fd;
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
        out_stride < max_w || max_bytes > sizeof(bmp)) {
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

    pixel_offset = ui_read_le32(bmp + 10);
    dib_size = ui_read_le32(bmp + 14);
    if (dib_size < 40 || pixel_offset >= len) {
        return 0;
    }
    width = ui_read_le32s(bmp + 18);
    height_signed = ui_read_le32s(bmp + 22);
    planes = ui_read_le16(bmp + 26);
    bpp = ui_read_le16(bmp + 28);
    compression = ui_read_le32(bmp + 30);
    if (width <= 0 || height_signed == 0 || planes != 1 ||
        (bpp != 24 && bpp != 32) || compression != 0) {
        return 0;
    }
    top_down = height_signed < 0;
    height = top_down ? (uint32_t)(-height_signed) : (uint32_t)height_signed;
    if ((uint32_t)width > max_w || height > max_h) {
        return 0;
    }
    row_stride = ((((uint32_t)width * bpp) + 31U) / 32U) * 4U;
    if (pixel_offset + row_stride * height > len) {
        return 0;
    }
    for (uint32_t y = 0; y < max_h; ++y) {
        for (uint32_t x = 0; x < max_w; ++x) {
            out_pixels[y * out_stride + x] = 0;
        }
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = top_down ? y : height - 1U - y;
        const uint8_t *row = bmp + pixel_offset + src_y * row_stride;
        for (uint32_t x = 0; x < (uint32_t)width; ++x) {
            const uint8_t *px = row + x * (bpp / 8U);
            uint32_t b = px[0];
            uint32_t g = px[1];
            uint32_t r = px[2];
            uint32_t a = bpp == 32 ? px[3] : 0xffU;
            if (bpp == 24 && r == 0xffU && g == 0 && b == 0xffU) {
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

static struct ui_window_button_icon *ui_window_button_icon_for_label(char label)
{
    switch (label) {
    case '_':
        return &ui_window_button_icons[UI_WINDOW_BUTTON_ICON_MINIMIZE];
    case 'M':
    case 'm':
        return &ui_window_button_icons[UI_WINDOW_BUTTON_ICON_MAXIMIZE];
    case 'r':
    case 'R':
        return &ui_window_button_icons[UI_WINDOW_BUTTON_ICON_RESTORE];
    case 'X':
    case 'x':
        return &ui_window_button_icons[UI_WINDOW_BUTTON_ICON_CLOSE];
    default:
        return 0;
    }
}

static int ui_ensure_window_button_icon(struct ui_window_button_icon *icon)
{
    if (!icon) {
        return 0;
    }
    if (icon->checked) {
        return icon->loaded;
    }
    icon->checked = 1;
    icon->loaded = ui_load_bmp_argb(icon->path, UI_WINDOW_BUTTON_ICON_W,
                                    UI_WINDOW_BUTTON_ICON_H,
                                    UI_WINDOW_BUTTON_ICON_MAX_BYTES,
                                    icon->pixels, UI_WINDOW_BUTTON_ICON_W,
                                    &icon->width, &icon->height) ? 1 : 0;
    return icon->loaded;
}

static int ui_draw_window_button_icon(struct leonos_ui_surface *surface,
                                      struct ui_window_button_icon *icon,
                                      uint32_t x, uint32_t y,
                                      uint32_t draw_w, uint32_t draw_h,
                                      uint32_t color)
{
    if (!ui_ensure_window_button_icon(icon) || !icon->width || !icon->height ||
        !draw_w || !draw_h) {
        return 0;
    }
    for (uint32_t yy = 0; yy < draw_h; ++yy) {
        uint32_t src_y = yy * icon->height / draw_h;
        if (src_y >= icon->height) {
            src_y = icon->height - 1;
        }
        for (uint32_t xx = 0; xx < draw_w; ++xx) {
            uint32_t src_x = xx * icon->width / draw_w;
            uint32_t argb;
            if (src_x >= icon->width) {
                src_x = icon->width - 1;
            }
            argb = icon->pixels[src_y * UI_WINDOW_BUTTON_ICON_W + src_x];
            if ((argb >> 24) == 0) {
                continue;
            }
            leonos_ui_pixel(surface, x + xx, y + yy, color);
        }
    }
    return 1;
}

static void ui_window_button_outline_symbol(struct leonos_ui_surface *surface,
                                            uint32_t x, uint32_t y,
                                            uint32_t w, uint32_t h,
                                            uint32_t color)
{
    leonos_ui_rect(surface, x, y, w, 1, color);
    leonos_ui_rect(surface, x, y, 1, h, color);
    leonos_ui_rect(surface, x + w - 1, y, 1, h, color);
    leonos_ui_rect(surface, x, y + h - 1, w, 1, color);
}

static void ui_draw_fallback_window_button_symbol(struct leonos_ui_surface *surface,
                                                  uint32_t x, uint32_t y,
                                                  char label, uint32_t color)
{
    switch (label) {
    case '_':
        leonos_ui_rect(surface, x + 3, y + 12, 10, 2, color);
        break;
    case 'M':
    case 'm':
        ui_window_button_outline_symbol(surface, x + 3, y + 3, 10, 9, color);
        leonos_ui_rect(surface, x + 4, y + 4, 8, 1, color);
        break;
    case 'r':
    case 'R':
        ui_window_button_outline_symbol(surface, x + 5, y + 3, 8, 7, color);
        leonos_ui_rect(surface, x + 6, y + 4, 6, 1, color);
        ui_window_button_outline_symbol(surface, x + 3, y + 6, 8, 7, color);
        leonos_ui_rect(surface, x + 4, y + 7, 6, 1, color);
        break;
    case 'X':
    case 'x':
        for (uint32_t i = 0; i < 8; ++i) {
            leonos_ui_rect(surface, x + 4 + i, y + 4 + i, 2, 2, color);
            leonos_ui_rect(surface, x + 4 + i, y + 11 - i, 2, 2, color);
        }
        break;
    default:
        break;
    }
}


void ui_window_button_draw(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           char label, uint32_t flags)
{
    uint32_t pressed = (flags & LEONOS_UI_BUTTON_PRESSED) ? 1U : 0U;
    uint32_t color = (flags & LEONOS_UI_BUTTON_DISABLED) ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
    uint32_t icon_x = x + (LEONOS_UI_WINDOW_BUTTON_W - UI_WINDOW_BUTTON_ICON_W) / 2U + pressed;
    uint32_t icon_y = y + (LEONOS_UI_WINDOW_BUTTON_H - UI_WINDOW_BUTTON_ICON_H) / 2U + pressed;
    struct ui_window_button_icon *icon = ui_window_button_icon_for_label(label);
    leonos_ui_button(surface, x, y, LEONOS_UI_WINDOW_BUTTON_W,
                     LEONOS_UI_WINDOW_BUTTON_H, 0, flags);
    if (!ui_draw_window_button_icon(surface, icon, icon_x, icon_y,
                                    UI_WINDOW_BUTTON_ICON_W,
                                    UI_WINDOW_BUTTON_ICON_H, color)) {
        ui_draw_fallback_window_button_symbol(surface, icon_x, icon_y, label, color);
    }
}
