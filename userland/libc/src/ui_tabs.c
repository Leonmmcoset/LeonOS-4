#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/ui.h>

#define UI_TAB_CLOSE_W 18U

static uint32_t tab_close_width(const struct leonos_ui_tab_item *item)
{
    return item && (item->flags & LEONOS_UI_TAB_CLOSABLE) ? UI_TAB_CLOSE_W : 0;
}

static uint32_t tab_width(const struct leonos_ui_tab_item *item)
{
    return leonos_ui_text_width(item && item->label ? item->label : "") +
           22U + tab_close_width(item);
}

static int tab_find_enabled(const struct leonos_ui_tab_item *items, uint32_t count,
                            uint32_t id)
{
    for (uint32_t index = 0; index < count; ++index) {
        if (items[index].id == id && !(items[index].flags & LEONOS_UI_TAB_DISABLED)) {
            return (int)index;
        }
    }
    return -1;
}

void leonos_ui_tab_state_init(struct leonos_ui_tab_state *state, uint32_t selected_id)
{
    if (!state) {
        return;
    }
    state->selected_id = selected_id;
    state->hovered_id = selected_id;
    state->focused = 0;
}

uint32_t leonos_ui_tab_height(void)
{
    return LEONOS_FONT_H + 10;
}

void leonos_ui_tab_control(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_tab_item *items,
                           uint32_t count, const struct leonos_ui_tab_state *state)
{
    uint32_t cursor = x;
    uint32_t right = x + w;
    uint32_t selected = state ? state->selected_id : 0;
    for (uint32_t index = 0; items && index < count && cursor < right; ++index) {
        uint32_t width = tab_width(&items[index]);
        uint32_t disabled = items[index].flags & LEONOS_UI_TAB_DISABLED;
        uint32_t active = !disabled && items[index].id == selected;
        if (cursor + width > right) {
            width = right - cursor;
        }
        leonos_ui_bevel(surface, cursor, y, width, leonos_ui_tab_height(),
                        active ? LEONOS_UI_WHITE : LEONOS_UI_GRAY,
                        active ? LEONOS_UI_BUTTON_PRESSED : 0);
        uint32_t close_width = tab_close_width(&items[index]);
        uint32_t label_width = width > 20U + close_width ?
                               width - 20U - close_width : 0;
        leonos_ui_text_transparent_clipped(surface, cursor + 10, y + 5,
                                           label_width,
                                           items[index].label ? items[index].label : "",
                                           disabled ? LEONOS_UI_DARK : LEONOS_UI_BLACK);
        if (close_width && width >= close_width) {
            uint32_t icon_x = cursor + width - close_width + 5U;
            uint32_t icon_y = y + (leonos_ui_tab_height() - 8U) / 2U;
            uint32_t icon_color = disabled ? LEONOS_UI_DARK : LEONOS_UI_BLACK;
            for (uint32_t diagonal = 0; diagonal < 8U; ++diagonal) {
                leonos_ui_rect(surface, icon_x + diagonal, icon_y + diagonal,
                               1, 1, icon_color);
                leonos_ui_rect(surface, icon_x + 7U - diagonal, icon_y + diagonal,
                               1, 1, icon_color);
            }
        }
        cursor += width;
    }
}

int leonos_ui_tab_control_handle_mouse(struct leonos_ui_tab_state *state,
                                       int32_t px, int32_t py,
                                       uint32_t x, uint32_t y, uint32_t w,
                                       const struct leonos_ui_tab_item *items,
                                       uint32_t count)
{
    return leonos_ui_tab_control_handle_mouse_ex(state, px, py, x, y, w,
                                                 items, count, 0);
}

int leonos_ui_tab_control_handle_mouse_ex(struct leonos_ui_tab_state *state,
                                          int32_t px, int32_t py,
                                          uint32_t x, uint32_t y, uint32_t w,
                                          const struct leonos_ui_tab_item *items,
                                          uint32_t count, uint32_t *closed_id)
{
    uint32_t cursor = x;
    uint32_t right = x + w;
    if (closed_id) {
        *closed_id = 0;
    }
    if (!state || !items ||
        !leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                       w, leonos_ui_tab_height())) {
        return 0;
    }
    for (uint32_t index = 0; index < count && cursor < right; ++index) {
        uint32_t width = tab_width(&items[index]);
        if (cursor + width > right) {
            width = right - cursor;
        }
        if ((uint32_t)px >= cursor && (uint32_t)px < cursor + width) {
            state->focused = 1;
            state->hovered_id = items[index].id;
            uint32_t close_width = tab_close_width(&items[index]);
            if (closed_id && close_width && width >= close_width &&
                (uint32_t)px >= cursor + width - close_width) {
                *closed_id = items[index].id;
                return 1;
            }
            if (!(items[index].flags & LEONOS_UI_TAB_DISABLED)) {
                state->selected_id = items[index].id;
            }
            return 1;
        }
        cursor += width;
    }
    return 0;
}

int leonos_ui_tab_control_handle_key(struct leonos_ui_tab_state *state,
                                     uint8_t keycode,
                                     const struct leonos_ui_tab_item *items,
                                     uint32_t count)
{
    int current;
    if (!state || !items || count == 0 || keycode != LEONOS_KEY_TAB) {
        return 0;
    }
    current = tab_find_enabled(items, count, state->selected_id);
    for (uint32_t step = 1; step <= count; ++step) {
        uint32_t index = (uint32_t)(current < 0 ? 0 : current) + step;
        index %= count;
        if (!(items[index].flags & LEONOS_UI_TAB_DISABLED)) {
            state->selected_id = items[index].id;
            state->hovered_id = items[index].id;
            state->focused = 1;
            return 1;
        }
    }
    return 0;
}

void leonos_ui_tab_body(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h)
{
    leonos_ui_inset(surface, x, y, w, h, LEONOS_UI_WHITE);
}
