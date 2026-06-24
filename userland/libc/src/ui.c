#include <leonos/psf_font.h>
#include <leonos/ui.h>

static uint32_t ui_strlen(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
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
    const uint8_t *glyph = leonos_psf_glyph(ch);
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

void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    const char *text, uint32_t fg, uint32_t bg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        ui_char(surface, x + i * LEONOS_FONT_W, y, text[i], fg, bg, 0);
    }
}

void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                const char *text, uint32_t fg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        ui_char(surface, x + i * LEONOS_FONT_W, y, text[i], fg, 0, 1);
    }
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
    uint32_t len = ui_strlen(label);
    uint32_t text_w = len * LEONOS_FONT_W;
    uint32_t tx = text_w < w ? x + (w - text_w) / 2 : x + 4;
    uint32_t ty = LEONOS_FONT_H < h ? y + (h - LEONOS_FONT_H) / 2 : y + 2;
    if (pressed) {
        ++tx;
        ++ty;
    }
    leonos_ui_text_transparent(surface, tx, ty, label,
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
    uint32_t title_color = active ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_INACTIVE_TITLE;
    leonos_ui_bevel(surface, x, y, w, h, LEONOS_UI_GRAY, 0);
    leonos_ui_rect(surface, x + 4, y + 4, w > 8 ? w - 8 : 0, LEONOS_UI_TITLEBAR_H, title_color);
    leonos_ui_text(surface, x + 10, y + 9, title, LEONOS_UI_WHITE, title_color);

    uint32_t bx = x + w - 64;
    uint32_t by = y + 6;
    leonos_ui_window_button(surface, bx, by, '_', 0);
    leonos_ui_window_button(surface, bx + 20, by, maximize_label, 0);
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
    if (flags & LEONOS_UI_MENU_SEPARATOR) {
        leonos_ui_rect(surface, x, y + 9, w, 1, LEONOS_UI_DARK);
        leonos_ui_rect(surface, x, y + 10, w, 1, LEONOS_UI_WHITE);
        return;
    }
    if (flags & LEONOS_UI_MENU_SELECTED) {
        leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_ACTIVE_TITLE);
        leonos_ui_text_transparent(surface, x + 4, y + 4, label, LEONOS_UI_WHITE);
    } else {
        leonos_ui_text_transparent(surface, x + 4, y + 4, label, LEONOS_UI_BLACK);
    }
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
    (void)flags;
    leonos_ui_inset(surface, x, y, w, LEONOS_FONT_H + 8, LEONOS_UI_WHITE);
    leonos_ui_text(surface, x + 4, y + 4, text, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
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
    leonos_ui_text(surface, x + 4, y + 2, text, fg, bg);
}
