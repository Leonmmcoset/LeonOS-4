#include <leonos/ui.h>

#include "ui_internal.h"

static uint32_t treeview_count(uint32_t count)
{
    return count > LEONOS_UI_TREEVIEW_MAX_ITEMS ? LEONOS_UI_TREEVIEW_MAX_ITEMS : count;
}

static int treeview_item_index(const struct leonos_ui_treeview_item *items,
                               uint32_t count, uint32_t id)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int treeview_parent_index(const struct leonos_ui_treeview_item *items,
                                 uint32_t count, uint32_t index)
{
    uint32_t parent_id;
    if (!items || index >= count) {
        return -1;
    }
    parent_id = items[index].parent_id;
    if (parent_id == 0 || parent_id == items[index].id) {
        return -1;
    }
    return treeview_item_index(items, count, parent_id);
}

static int treeview_has_children(const struct leonos_ui_treeview_item *items,
                                 uint32_t count, uint32_t index)
{
    if (!items || index >= count || items[index].id == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (i != index && items[i].parent_id == items[index].id) {
            return 1;
        }
    }
    return 0;
}

static int treeview_is_collapsed(const struct leonos_ui_treeview_state *state,
                                 uint32_t id)
{
    if (!state) {
        return 0;
    }
    for (uint32_t i = 0; i < state->collapsed_count; ++i) {
        if (state->collapsed_ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static void treeview_remove_collapsed(struct leonos_ui_treeview_state *state,
                                      uint32_t id)
{
    if (!state) {
        return;
    }
    for (uint32_t i = 0; i < state->collapsed_count; ++i) {
        if (state->collapsed_ids[i] == id) {
            for (uint32_t next = i + 1; next < state->collapsed_count; ++next) {
                state->collapsed_ids[next - 1] = state->collapsed_ids[next];
            }
            --state->collapsed_count;
            return;
        }
    }
}

static void treeview_set_collapsed(struct leonos_ui_treeview_state *state,
                                   uint32_t id, int collapsed)
{
    if (!state) {
        return;
    }
    if (!collapsed) {
        treeview_remove_collapsed(state, id);
        return;
    }
    if (treeview_is_collapsed(state, id) ||
        state->collapsed_count >= LEONOS_UI_TREEVIEW_MAX_ITEMS) {
        return;
    }
    state->collapsed_ids[state->collapsed_count++] = id;
}

static void treeview_prune_collapsed(struct leonos_ui_treeview_state *state,
                                     const struct leonos_ui_treeview_item *items,
                                     uint32_t count)
{
    uint32_t kept = 0;
    if (!state) {
        return;
    }
    if (state->collapsed_count > LEONOS_UI_TREEVIEW_MAX_ITEMS) {
        state->collapsed_count = LEONOS_UI_TREEVIEW_MAX_ITEMS;
    }
    for (uint32_t i = 0; i < state->collapsed_count; ++i) {
        int index = treeview_item_index(items, count, state->collapsed_ids[i]);
        if (index >= 0 && treeview_has_children(items, count, (uint32_t)index)) {
            state->collapsed_ids[kept++] = state->collapsed_ids[i];
        }
    }
    state->collapsed_count = kept;
}

static void treeview_append_visible(struct leonos_ui_treeview_state *state,
                                    const struct leonos_ui_treeview_item *items,
                                    uint32_t count, uint32_t index, uint8_t depth,
                                    uint8_t visited[LEONOS_UI_TREEVIEW_MAX_ITEMS])
{
    if (!state || !items || index >= count || visited[index] ||
        state->visible_count >= LEONOS_UI_TREEVIEW_MAX_ITEMS) {
        return;
    }
    visited[index] = 1;
    state->visible_indices[state->visible_count] = index;
    state->visible_depths[state->visible_count] = depth;
    ++state->visible_count;
    if (treeview_is_collapsed(state, items[index].id)) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!visited[i] && items[index].id != 0 &&
            items[i].parent_id == items[index].id) {
            treeview_append_visible(state, items, count, i,
                                    depth < 31U ? depth + 1U : depth, visited);
        }
    }
}

/* Keep the fallback walk from reintroducing descendants skipped by collapse. */
static int treeview_has_collapsed_ancestor(const struct leonos_ui_treeview_state *state,
                                           const struct leonos_ui_treeview_item *items,
                                           uint32_t count, uint32_t index)
{
    for (uint32_t steps = 0; steps < count; ++steps) {
        int parent = treeview_parent_index(items, count, index);
        if (parent < 0) {
            return 0;
        }
        if (treeview_is_collapsed(state, items[parent].id)) {
            return 1;
        }
        index = (uint32_t)parent;
    }
    return 0;
}

static int treeview_selected_row(const struct leonos_ui_treeview_state *state,
                                 const struct leonos_ui_treeview_item *items)
{
    if (!state || !items || !state->has_selection) {
        return -1;
    }
    for (uint32_t i = 0; i < state->visible_count; ++i) {
        if (items[state->visible_indices[i]].id == state->selected_id) {
            return (int)i;
        }
    }
    return -1;
}

static void treeview_clamp(struct leonos_ui_treeview_state *state,
                           const struct leonos_ui_treeview_item *items)
{
    uint32_t visible;
    uint32_t max_scroll;
    int selected_row;
    if (!state) {
        return;
    }
    visible = state->visible_rows ? state->visible_rows : 1;
    if (state->visible_count == 0) {
        state->scroll = 0;
        state->has_selection = 0;
        return;
    }
    max_scroll = state->visible_count > visible ? state->visible_count - visible : 0;
    if (state->scroll > max_scroll) {
        state->scroll = max_scroll;
    }
    selected_row = treeview_selected_row(state, items);
    if (selected_row < 0) {
        state->selected_id = items[state->visible_indices[0]].id;
        state->has_selection = 1;
        selected_row = 0;
    }
    if ((uint32_t)selected_row < state->scroll) {
        state->scroll = (uint32_t)selected_row;
    } else if ((uint32_t)selected_row >= state->scroll + visible) {
        state->scroll = (uint32_t)selected_row - visible + 1U;
    }
}

static void treeview_select_row(struct leonos_ui_treeview_state *state,
                                const struct leonos_ui_treeview_item *items,
                                uint32_t row)
{
    if (!state || !items || row >= state->visible_count) {
        return;
    }
    state->selected_id = items[state->visible_indices[row]].id;
    state->has_selection = 1;
    treeview_clamp(state, items);
}

void leonos_ui_treeview_state_init(struct leonos_ui_treeview_state *state,
                                   uint32_t visible_rows, uint32_t row_height)
{
    if (!state) {
        return;
    }
    for (uint32_t i = 0; i < sizeof(*state); ++i) {
        ((uint8_t *)state)[i] = 0;
    }
    state->visible_rows = visible_rows ? visible_rows : 1;
    state->row_height = row_height ? row_height : LEONOS_FONT_H + 8U;
}

void leonos_ui_treeview_state_set_viewport(struct leonos_ui_treeview_state *state,
                                           uint32_t visible_rows)
{
    if (!state) {
        return;
    }
    state->visible_rows = visible_rows ? visible_rows : 1;
}

void leonos_ui_treeview_state_sync(struct leonos_ui_treeview_state *state,
                                   const struct leonos_ui_treeview_item *items,
                                   uint32_t count)
{
    uint8_t visited[LEONOS_UI_TREEVIEW_MAX_ITEMS] = {0};
    count = treeview_count(count);
    if (!state) {
        return;
    }
    state->visible_count = 0;
    if (!items || count == 0) {
        treeview_clamp(state, items);
        return;
    }
    treeview_prune_collapsed(state, items, count);
    for (uint32_t i = 0; i < count; ++i) {
        if (treeview_parent_index(items, count, i) < 0) {
            treeview_append_visible(state, items, count, i, 0, visited);
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!visited[i] &&
            !treeview_has_collapsed_ancestor(state, items, count, i)) {
            treeview_append_visible(state, items, count, i, 0, visited);
        }
    }
    treeview_clamp(state, items);
}

void leonos_ui_treeview(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const struct leonos_ui_list_column *cols,
                        uint32_t col_count,
                        const struct leonos_ui_treeview_item *items,
                        uint32_t count, struct leonos_ui_treeview_state *state)
{
    uint32_t row_h;
    uint32_t rows_y;
    uint32_t rows;
    if (!surface || !state) {
        return;
    }
    if (state->row_height < LEONOS_FONT_H + 4U) {
        state->row_height = LEONOS_FONT_H + 4U;
    }
    leonos_ui_treeview_state_sync(state, items, count);
    row_h = state->row_height;
    rows_y = y + LEONOS_FONT_H + 12U;
    leonos_ui_listview_header(surface, x, y, w, cols, col_count);
    rows = state->visible_count > state->scroll ? state->visible_count - state->scroll : 0;
    if (rows > state->visible_rows) {
        rows = state->visible_rows;
    }
    for (uint32_t row = 0; row < rows; ++row) {
        uint32_t visible_index = state->scroll + row;
        uint32_t item_index = state->visible_indices[visible_index];
        uint32_t depth = state->visible_depths[visible_index];
        uint32_t bg = state->has_selection && items[item_index].id == state->selected_id
                          ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
        uint32_t fg = bg == LEONOS_UI_ACTIVE_TITLE ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        uint32_t row_y = rows_y + row * row_h;
        uint32_t cx = x;
        uint32_t indent = depth * 14U;
        int has_children = treeview_has_children(items, treeview_count(count), item_index);
        leonos_ui_rect(surface, x, row_y, w, row_h, bg);
        for (uint32_t col = 0; col < col_count && cx < x + w; ++col) {
            uint32_t cw = cols[col].width ? cols[col].width : x + w - cx;
            uint32_t text_x = cx + 4U;
            if (cx + cw > x + w) {
                cw = x + w - cx;
            }
            if (col == 0) {
                uint32_t glyph_x = cx + 4U + indent;
                text_x = glyph_x + 16U;
                if (has_children && glyph_x + 10U < cx + cw) {
                    leonos_ui_rect(surface, glyph_x, row_y + 6U, 10U, 10U, bg);
                    leonos_ui_rect(surface, glyph_x, row_y + 6U, 10U, 1U, fg);
                    leonos_ui_rect(surface, glyph_x, row_y + 15U, 10U, 1U, fg);
                    leonos_ui_rect(surface, glyph_x, row_y + 6U, 1U, 10U, fg);
                    leonos_ui_rect(surface, glyph_x + 9U, row_y + 6U, 1U, 10U, fg);
                    leonos_ui_rect(surface, glyph_x + 2U, row_y + 10U, 6U, 1U, fg);
                    if (treeview_is_collapsed(state, items[item_index].id)) {
                        leonos_ui_rect(surface, glyph_x + 5U, row_y + 8U, 1U, 6U, fg);
                    }
                }
            }
            if (text_x < cx + cw) {
                leonos_ui_text_clipped(surface, text_x, row_y + 4U,
                                       cx + cw - text_x,
                                       items[item_index].cells && items[item_index].cells[col]
                                           ? items[item_index].cells[col] : "",
                                       fg, bg);
            }
            cx += cw;
        }
    }
}

int leonos_ui_treeview_state_handle_key(struct leonos_ui_treeview_state *state,
                                        const struct leonos_ui_treeview_item *items,
                                        uint32_t count, uint8_t keycode,
                                        uint32_t *activated)
{
    int selected_row;
    uint32_t selected_index;
    uint32_t visible;
    if (activated) {
        *activated = 0;
    }
    if (!state || !state->focused) {
        return 0;
    }
    leonos_ui_treeview_state_sync(state, items, count);
    if (state->visible_count == 0) {
        return 0;
    }
    selected_row = treeview_selected_row(state, items);
    if (selected_row < 0) {
        return 0;
    }
    selected_index = state->visible_indices[selected_row];
    visible = state->visible_rows ? state->visible_rows : 1;
    switch (keycode) {
    case 72:
        if (selected_row > 0) {
            treeview_select_row(state, items, (uint32_t)selected_row - 1U);
            return 1;
        }
        return 0;
    case 80:
        if ((uint32_t)selected_row + 1U < state->visible_count) {
            treeview_select_row(state, items, (uint32_t)selected_row + 1U);
            return 1;
        }
        return 0;
    case 73:
        treeview_select_row(state, items,
                            (uint32_t)selected_row > visible
                                ? (uint32_t)selected_row - visible : 0);
        return 1;
    case 81:
        treeview_select_row(state, items,
                            (uint32_t)selected_row + visible < state->visible_count
                                ? (uint32_t)selected_row + visible : state->visible_count - 1U);
        return 1;
    case 71:
        treeview_select_row(state, items, 0);
        return 1;
    case 79:
        treeview_select_row(state, items, state->visible_count - 1U);
        return 1;
    case 75:
        if (treeview_has_children(items, treeview_count(count), selected_index) &&
            !treeview_is_collapsed(state, items[selected_index].id)) {
            treeview_set_collapsed(state, items[selected_index].id, 1);
            leonos_ui_treeview_state_sync(state, items, count);
            return 1;
        }
        {
            int parent = treeview_parent_index(items, treeview_count(count), selected_index);
            if (parent >= 0) {
                state->selected_id = items[parent].id;
                state->has_selection = 1;
                leonos_ui_treeview_state_sync(state, items, count);
                return 1;
            }
        }
        return 0;
    case 77:
        if (treeview_has_children(items, treeview_count(count), selected_index)) {
            if (treeview_is_collapsed(state, items[selected_index].id)) {
                treeview_set_collapsed(state, items[selected_index].id, 0);
                leonos_ui_treeview_state_sync(state, items, count);
            } else if ((uint32_t)selected_row + 1U < state->visible_count &&
                       state->visible_depths[selected_row + 1] >
                           state->visible_depths[selected_row]) {
                treeview_select_row(state, items, (uint32_t)selected_row + 1U);
            }
            return 1;
        }
        return 0;
    case LEONOS_KEY_ENTER:
        if (treeview_has_children(items, treeview_count(count), selected_index)) {
            treeview_set_collapsed(state, items[selected_index].id,
                                   !treeview_is_collapsed(state, items[selected_index].id));
            leonos_ui_treeview_state_sync(state, items, count);
        }
        if (activated) {
            *activated = 1;
        }
        return 1;
    default:
        return 0;
    }
}

int leonos_ui_treeview_state_handle_mouse(struct leonos_ui_treeview_state *state,
                                          const struct leonos_ui_treeview_item *items,
                                          uint32_t count, int32_t px, int32_t py,
                                          uint32_t x, uint32_t rows_y, uint32_t w,
                                          uint32_t *activated)
{
    uint32_t row;
    uint32_t visible_index;
    uint32_t item_index;
    uint32_t glyph_x;
    if (activated) {
        *activated = 0;
    }
    if (!state) {
        return 0;
    }
    leonos_ui_treeview_state_sync(state, items, count);
    if (!leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)rows_y,
                       w, state->visible_rows * state->row_height)) {
        state->focused = 0;
        return 0;
    }
    state->focused = 1;
    row = ((uint32_t)py - rows_y) / state->row_height;
    visible_index = state->scroll + row;
    if (visible_index >= state->visible_count) {
        return 1;
    }
    item_index = state->visible_indices[visible_index];
    glyph_x = x + 4U + state->visible_depths[visible_index] * 14U;
    if (treeview_has_children(items, treeview_count(count), item_index) &&
        px >= (int32_t)glyph_x && px < (int32_t)(glyph_x + 12U)) {
        state->selected_id = items[item_index].id;
        state->has_selection = 1;
        treeview_set_collapsed(state, items[item_index].id,
                               !treeview_is_collapsed(state, items[item_index].id));
        leonos_ui_treeview_state_sync(state, items, count);
        return 1;
    }
    if (state->has_selection && state->selected_id == items[item_index].id && activated) {
        *activated = 1;
    }
    treeview_select_row(state, items, visible_index);
    return 1;
}

int leonos_ui_treeview_state_handle_wheel(struct leonos_ui_treeview_state *state,
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
    if (state->visible_count <= visible) {
        return 0;
    }
    old = state->scroll;
    max_scroll = state->visible_count - visible;
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
