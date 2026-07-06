#include <leonos/ui.h>

#include "ui_internal.h"

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
    ui_window_button_draw(surface, x, y, label, flags);
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
