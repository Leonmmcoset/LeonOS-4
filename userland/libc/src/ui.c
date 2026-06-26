#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define UI_SYSTEM_FONT_MAX 8192U

static uint8_t ui_system_font[UI_SYSTEM_FONT_MAX];
static uint8_t ui_system_font_checked;
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

uint32_t leonos_ui_text_width(const char *text)
{
    return ui_strlen(text) * LEONOS_FONT_W;
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

void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    const char *text, uint32_t fg, uint32_t bg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        ui_char(surface, x + i * LEONOS_FONT_W, y, text[i], fg, bg, 0);
    }
}

void leonos_ui_text_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const char *text, uint32_t fg, uint32_t bg)
{
    uint32_t max = leonos_ui_text_fit_chars(w);
    for (uint32_t i = 0; i < max; ++i) {
        char ch = text && text[i] ? text[i] : ' ';
        ui_char(surface, x + i * LEONOS_FONT_W, y, ch, fg, bg, 0);
        if (!text || !text[i]) {
            for (++i; i < max; ++i) {
                ui_char(surface, x + i * LEONOS_FONT_W, y, ' ', fg, bg, 0);
            }
            return;
        }
    }
}

void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                const char *text, uint32_t fg)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        ui_char(surface, x + i * LEONOS_FONT_W, y, text[i], fg, 0, 1);
    }
}

void leonos_ui_text_transparent_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                        uint32_t w, const char *text, uint32_t fg)
{
    uint32_t max = leonos_ui_text_fit_chars(w);
    for (uint32_t i = 0; text && text[i] && i < max; ++i) {
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
    uint32_t max_chars;
    const char *visible = text ? text : "";
    uint32_t len = ui_strlen(visible);
    if (scroll > len) {
        scroll = len;
    }
    max_chars = w > 8 ? leonos_ui_text_fit_chars(w - 8) : 0;
    leonos_ui_inset(surface, x, y, w, h, bg);
    leonos_ui_text_clipped(surface, x + 4, y + 4, w > 8 ? w - 8 : w, visible + scroll, fg, bg);
    if ((flags & LEONOS_UI_EDIT_FOCUSED) && !(flags & LEONOS_UI_EDIT_DISABLED)) {
        if (cursor < scroll) {
            cursor = scroll;
        }
        if (cursor > scroll + max_chars) {
            cursor = scroll + max_chars;
        }
        leonos_ui_rect(surface, x + 4 + (cursor - scroll) * LEONOS_FONT_W, y + 4, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
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
    if (state->cursor > state->scroll + cols) {
        state->scroll = state->cursor - cols;
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
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
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
    for (uint32_t i = 0; i < cols; ++i) {
        uint32_t idx = state->scroll + i;
        char ch = idx < state->length ? state->buffer[idx] : ' ';
        uint32_t ch_bg = bg;
        uint32_t ch_fg = fg;
        if (idx >= sel_start && idx < sel_end && edit_has_selection(state)) {
            ch_bg = LEONOS_UI_ACTIVE_TITLE;
            ch_fg = LEONOS_UI_WHITE;
        }
        ui_char(surface, text_x + i * LEONOS_FONT_W, text_y, ch, ch_fg, ch_bg, 0);
    }
    if ((draw_flags & LEONOS_UI_EDIT_FOCUSED) && !(draw_flags & LEONOS_UI_EDIT_DISABLED)) {
        uint32_t cursor = state->cursor;
        if (cursor < state->scroll) {
            cursor = state->scroll;
        }
        if (cursor > state->scroll + cols) {
            cursor = state->scroll + cols;
        }
        leonos_ui_rect(surface, text_x + (cursor - state->scroll) * LEONOS_FONT_W,
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
            edit_delete_range(state, state->cursor - 1, state->cursor);
            edit_clear_selection(state);
            return 1;
        }
        return 0;
    case LEONOS_KEY_ENTER:
        return 0;
    case 75:
        if (state->cursor > 0) {
            --state->cursor;
            edit_clear_selection(state);
            return 1;
        }
        return 0;
    case 77:
        if (state->cursor < state->length) {
            ++state->cursor;
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
        idx += ((uint32_t)px - x - 4) / LEONOS_FONT_W;
    }
    if (idx > state->length) {
        idx = state->length;
    }
    state->cursor = idx;
    if (!state->selecting) {
        state->selection_anchor = state->cursor;
        state->selecting = 1;
    }
    if (cols && state->cursor > state->scroll + cols) {
        state->scroll = state->cursor - cols;
    }
    return 1;
}

void leonos_ui_text_area(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *text, uint32_t cursor,
                         uint32_t scroll_line, uint32_t flags)
{
    uint32_t rows = h > 8 ? (h - 8) / LEONOS_FONT_H : 0;
    uint32_t current = 0;
    uint32_t row = 0;
    uint32_t line_len = 0;
    uint32_t text_pos = 0;
    char line[128];
    (void)cursor;
    leonos_ui_scroll_view_frame(surface, x, y, w, h);
    while (text && row < rows) {
        char ch = text[text_pos++];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n' || ch == 0 || line_len + 1 >= sizeof(line) ||
            line_len >= leonos_ui_text_fit_chars(w > 8 ? w - 8 : w)) {
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
            if (ch == 0) {
                break;
            }
            if (ch != '\n') {
                --text_pos;
            }
            continue;
        }
        line[line_len++] = ch < 32 ? '?' : ch;
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
    for (uint32_t i = 0; i < cursor; ++i) {
        char ch = state->buffer[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            ++line;
            col = 0;
            continue;
        }
        if (col >= cols) {
            ++line;
            col = 0;
        }
        ++col;
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
        if (state->buffer[pos] == '\r') {
            continue;
        }
        if (state->buffer[pos] == '\n') {
            if (line == target_line) {
                return pos;
            }
            ++line;
            col = 0;
            continue;
        }
        if (col >= cols) {
            ++line;
            col = 0;
            if (line > target_line) {
                return pos;
            }
            if (line == target_line && col >= target_col) {
                return pos;
            }
        }
        ++col;
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
    state->scroll_line = 0;
    state->preferred_column = 0xffffffffu;
    state->line_count = 1;
    state->focused = 0;
    state->readonly = 0;
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
    for (uint32_t i = 0; i < state->length; ++i) {
        char ch = state->buffer[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            ++lines;
            col = 0;
            continue;
        }
        if (col >= cols) {
            ++lines;
            col = 0;
        }
        ++col;
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
    uint32_t draw_flags = flags;
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
    leonos_ui_text_area(surface, x, y, w, h, state->buffer, state->cursor,
                        state->scroll_line, draw_flags & ~LEONOS_UI_EDIT_FOCUSED);
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
    return 1;
}

static int text_area_insert_char(struct leonos_ui_text_area_state *state, char ch)
{
    if (!state || !state->buffer || state->readonly || state->capacity == 0 ||
        state->length + 1 >= state->capacity) {
        return 0;
    }
    for (uint32_t i = state->length + 1; i > state->cursor; --i) {
        state->buffer[i] = state->buffer[i - 1];
    }
    state->buffer[state->cursor++] = ch;
    ++state->length;
    return 1;
}

static int text_area_delete_char(struct leonos_ui_text_area_state *state, uint32_t index)
{
    return text_area_delete_range(state, index, index + 1);
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
        if (state->cursor > 0) {
            if (text_area_delete_char(state, state->cursor - 1)) {
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
            --state->cursor;
            text_area_cursor_line_col(state, w, state->cursor, &line, &col);
            state->preferred_column = col;
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 77:
        if (state->cursor < state->length) {
            ++state->cursor;
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
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 80:
        line += 1;
        if (line >= state->line_count) {
            line = state->line_count ? state->line_count - 1 : 0;
        }
        state->cursor = text_area_cursor_from_line_col(state, w, line,
                                                       state->preferred_column == 0xffffffffu ? col : state->preferred_column);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 73:
        rows = text_area_rows(h);
        line = line > rows ? line - rows : 0;
        state->cursor = text_area_cursor_from_line_col(state, w, line, col);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 81:
        rows = text_area_rows(h);
        line += rows ? rows : 1;
        if (line >= state->line_count) {
            line = state->line_count ? state->line_count - 1 : 0;
        }
        state->cursor = text_area_cursor_from_line_col(state, w, line, col);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 71:
        state->cursor = text_area_cursor_from_line_col(state, w, line, 0);
        state->preferred_column = 0;
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 79:
        state->cursor = text_area_cursor_from_line_col(state, w, line, 0xffffffffu);
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
    if (!state || !(buttons & 1u)) {
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
    state->cursor = text_area_cursor_from_line_col(state, w, line, col);
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
    leonos_ui_dialog(surface, x, y, w, h, title);
    leonos_ui_text_clipped(surface, x + 16, y + 46, w > 32 ? w - 32 : w, message, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
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
    enum { W = 300, H = 150 };
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
