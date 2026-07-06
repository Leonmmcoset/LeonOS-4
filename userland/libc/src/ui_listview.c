#include <leonos/ui.h>

#include "ui_internal.h"

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
