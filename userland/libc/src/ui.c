#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

static uint32_t ui_strlen(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
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
    if (!out) {
        return 0;
    }
    switch (keycode) {
    case LEONOS_KEY_BACKSPACE: *out = '\b'; return 1;
    case LEONOS_KEY_TAB: *out = '\t'; return 1;
    case LEONOS_KEY_ENTER: *out = '\n'; return 1;
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
    case 12: *out = '-'; return 1;
    case 13: *out = '='; return 1;
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
    case 26: *out = '['; return 1;
    case 27: *out = ']'; return 1;
    case 30: *out = 'a'; return 1;
    case 31: *out = 's'; return 1;
    case 32: *out = 'd'; return 1;
    case 33: *out = 'f'; return 1;
    case 34: *out = 'g'; return 1;
    case 35: *out = 'h'; return 1;
    case 36: *out = 'j'; return 1;
    case 37: *out = 'k'; return 1;
    case 38: *out = 'l'; return 1;
    case 39: *out = ';'; return 1;
    case 40: *out = '\''; return 1;
    case 41: *out = '`'; return 1;
    case 43: *out = '\\'; return 1;
    case 44: *out = 'z'; return 1;
    case 45: *out = 'x'; return 1;
    case 46: *out = 'c'; return 1;
    case 47: *out = 'v'; return 1;
    case 48: *out = 'b'; return 1;
    case 49: *out = 'n'; return 1;
    case 50: *out = 'm'; return 1;
    case 51: *out = ','; return 1;
    case 52: *out = '.'; return 1;
    case 53: *out = '/'; return 1;
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
                                    uint8_t keycode)
{
    char ch;
    if (!state || !state->focused) {
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
        if (leonos_ui_keycode_to_char(keycode, &ch) && ch >= 32) {
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

int leonos_ui_text_area_state_handle_key(struct leonos_ui_text_area_state *state,
                                         uint8_t keycode, uint32_t w,
                                         uint32_t h)
{
    char ch;
    uint32_t line;
    uint32_t col;
    uint32_t rows = text_area_rows(h);
    if (!state || !state->focused) {
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
            if (text_area_delete_range(state, state->cursor - 1, state->cursor)) {
                leonos_ui_text_area_state_sync(state, w);
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
            leonos_ui_text_area_state_sync(state, w);
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
        if (line > 0) {
            state->cursor = text_area_cursor_from_line_col(state, w, line - 1,
                                                           state->preferred_column == 0xffffffffu ? col : state->preferred_column);
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 80:
        if (line + 1 < state->line_count) {
            state->cursor = text_area_cursor_from_line_col(state, w, line + 1,
                                                           state->preferred_column == 0xffffffffu ? col : state->preferred_column);
            text_area_ensure_cursor_visible(state, w, h);
            return 1;
        }
        return 0;
    case 73:
        line = line > rows ? line - rows : 0;
        state->cursor = text_area_cursor_from_line_col(state, w, line, col);
        text_area_ensure_cursor_visible(state, w, h);
        return 1;
    case 81:
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
        if (leonos_ui_keycode_to_char(keycode, &ch)) {
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
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    result = default_yes ? 1 : 0;
                    break;
                }
                if (event.keycode == 1) {
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
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    result = 1;
                    break;
                }
                if (event.keycode == 1) {
                    break;
                }
                leonos_ui_edit_state_handle_key(&edit, event.keycode);
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
