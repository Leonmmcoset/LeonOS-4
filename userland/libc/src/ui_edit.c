#include <leonos/ui.h>
#include <leonos/inputm.h>

#include "ui_internal.h"

void leonos_ui_edit(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const char *text, uint32_t cursor, uint32_t scroll,
                    uint32_t flags)
{
    uint32_t h = LEONOS_FONT_H + 8;
    leonos_ui_cursor_region(surface, (int32_t)x, (int32_t)y, w, h,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_NO : LEONOS_GUI_CURSOR_TEXT,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_REGION_DISABLED : 0);
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
                       x + 4 + ui_text_pixels_between(visible, scroll, cursor),
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
    uint32_t visible_width = w > 8 ? w - 8 : 0;
    if (!state || visible_width == 0) {
        return;
    }
    if (state->cursor < state->scroll) {
        state->scroll = state->cursor;
    }
    while (ui_text_pixels_between(state->buffer, state->scroll, state->cursor) > visible_width &&
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
    uint32_t text_width = w > 8 ? w - 8 : 0;
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
    leonos_ui_cursor_region(surface, (int32_t)x, (int32_t)y, w, h,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_NO : LEONOS_GUI_CURSOR_TEXT,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_REGION_DISABLED : 0);
    leonos_ui_edit_state_sync(state);
    if (state->focused && !state->readonly && !(flags & LEONOS_UI_EDIT_DISABLED)) {
        uint32_t context_flags = LEONOS_INPUTM_CONTEXT_FOCUSED;
        if (flags & LEONOS_UI_EDIT_SECURE) {
            context_flags |= LEONOS_INPUTM_CONTEXT_SECURE;
        }
        (void)leonos_inputm_set_current_context(context_flags,
                                                (int32_t)x, (int32_t)y,
                                                w, h);
    }
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
    for (uint32_t i = 0; i < draw_count && draw_x < text_x + text_width; ++i) {
        uint32_t idx = state->scroll + glyphs[i].byte_offset;
        uint32_t px = glyphs[i].pixel_width;
        uint32_t ch_bg = bg;
        uint32_t ch_fg = fg;
        if (draw_x + px > text_x + text_width) {
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
                       text_x + ui_text_pixels_between(state->buffer, state->scroll, cursor),
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

static int edit_insert_text(struct leonos_ui_edit_state *state, const char *text)
{
    uint32_t text_len = 0;
    if (!state || !state->buffer || state->readonly || !text) {
        return 0;
    }
    while (text[text_len]) {
        ++text_len;
    }
    if (!text_len) {
        return 0;
    }
    if (edit_has_selection(state)) {
        uint32_t start;
        uint32_t end;
        edit_selection_range(state, &start, &end);
        edit_delete_range(state, start, end);
    }
    if (state->length + text_len >= state->capacity) {
        return 0;
    }
    for (uint32_t i = state->length + text_len + 1U;
         i > state->cursor + text_len; --i) {
        state->buffer[i - 1U] = state->buffer[i - text_len - 1U];
    }
    for (uint32_t i = 0; i < text_len; ++i) {
        state->buffer[state->cursor + i] = text[i];
    }
    state->cursor += text_len;
    state->length += text_len;
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
    if (keycode == 0) {
        char text[LEONOS_INPUTM_TEXT_LEN];
        return leonos_inputm_take_text(text, sizeof(text)) ?
                   edit_insert_text(state, text) : 0;
    }
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
            (void)leonos_inputm_set_current_context(0, 0, 0, 0, 0);
            return 1;
        }
        return 0;
    }
    if (!(buttons & 1u)) {
        state->selecting = 0;
        return 0;
    }
    state->focused = 1;
    if (!state->readonly) {
        (void)leonos_inputm_set_current_context(LEONOS_INPUTM_CONTEXT_FOCUSED,
                                                (int32_t)x, (int32_t)y, w, h);
    }
    idx = state->scroll;
    if (px > (int32_t)x + 4) {
        idx = ui_byte_offset_for_pixel(state->buffer, state->length, state->scroll,
                                       (uint32_t)px - x - 4);
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

static uint32_t text_area_text_width(uint32_t w);

void leonos_ui_text_area(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *text, uint32_t cursor,
                         uint32_t scroll_line, uint32_t flags)
{
    leonos_ui_cursor_region(surface, (int32_t)x, (int32_t)y, w, h,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_NO : LEONOS_GUI_CURSOR_TEXT,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_REGION_DISABLED : 0);
    uint32_t rows = h > 8 ? (h - 8) / LEONOS_FONT_H : 0;
    uint32_t text_width = text_area_text_width(w);
    uint32_t current = 0;
    uint32_t row = 0;
    uint32_t line_len = 0;
    uint32_t line_pixels = 0;
    uint32_t text_pos = 0;
    char line[128];
    (void)cursor;
    leonos_ui_scroll_view_frame(surface, x, y, w, h);
    while (text && row < rows) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(text, ui_strlen(text), text_pos, &byte_len);
        uint32_t pixel_width = ui_codepoint_pixel_width(cp);
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
            (line_pixels + pixel_width > text_width && line_len != 0)) {
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
            line_pixels = 0;
            if (cp == '\n') {
                text_pos += byte_len;
            }
            continue;
        }
        for (uint32_t i = 0; i < byte_len && line_len + 1 < sizeof(line); ++i) {
            line[line_len++] = text[text_pos + i];
        }
        line_pixels += pixel_width;
        text_pos += byte_len;
    }
    if ((flags & LEONOS_UI_EDIT_FOCUSED) && row < rows) {
        leonos_ui_rect(surface, x + 4, y + 4 + row * LEONOS_FONT_H, 1, LEONOS_FONT_H, LEONOS_UI_BLACK);
    }
}

static uint32_t text_area_text_width(uint32_t w)
{
    return w > 8 ? w - 8 : 1;
}

static uint32_t text_area_rows(uint32_t h)
{
    return h > 8 ? (h - 8) / LEONOS_FONT_H : 0;
}

static void text_area_cursor_line_col(struct leonos_ui_text_area_state *state,
                                      uint32_t w, uint32_t cursor,
                                      uint32_t *out_line, uint32_t *out_col)
{
    uint32_t text_width = text_area_text_width(w);
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
        uint32_t pixel_width = ui_codepoint_pixel_width(cp);
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
        if (col + pixel_width > text_width && col != 0) {
            ++line;
            col = 0;
        }
        col += pixel_width;
        i += byte_len;
    }
    *out_line = line;
    *out_col = col;
}

static uint32_t text_area_cursor_from_line_col(struct leonos_ui_text_area_state *state,
                                               uint32_t w, uint32_t target_line,
                                               uint32_t target_col)
{
    uint32_t text_width = text_area_text_width(w);
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
        uint32_t pixel_width;
        cp = ui_decode_utf8(state->buffer, state->length, pos, &byte_len);
        pixel_width = ui_codepoint_pixel_width(cp);
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
        if (col + pixel_width > text_width && col != 0) {
            ++line;
            col = 0;
            if (line > target_line) {
                return pos;
            }
            if (line == target_line && col >= target_col) {
                return pos;
            }
        }
        if (line == target_line && col + pixel_width > target_col) {
            return pos;
        }
        col += pixel_width;
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
    uint32_t text_width = text_area_text_width(w);
    uint32_t lines = 1;
    uint32_t col = 0;
    if (!state || !state->buffer) {
        return 1;
    }
    for (uint32_t i = 0; i < state->length;) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(state->buffer, state->length, i, &byte_len);
        uint32_t pixel_width = ui_codepoint_pixel_width(cp);
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
        if (col + pixel_width > text_width && col != 0) {
            ++lines;
            col = 0;
        }
        col += pixel_width;
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
    uint32_t text_width = text_area_text_width(w);
    uint32_t draw_flags = flags;
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    if (!state) {
        leonos_ui_text_area(surface, x, y, w, h, "", 0, 0, flags);
        return;
    }
    leonos_ui_cursor_region(surface, (int32_t)x, (int32_t)y, w, h,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_NO : LEONOS_GUI_CURSOR_TEXT,
                            (flags & LEONOS_UI_EDIT_DISABLED)
                                ? LEONOS_GUI_CURSOR_REGION_DISABLED : 0);
    leonos_ui_text_area_state_sync(state, w);
    if (state->focused && !state->readonly && !(flags & LEONOS_UI_EDIT_DISABLED)) {
        uint32_t context_flags = LEONOS_INPUTM_CONTEXT_FOCUSED;
        if (flags & LEONOS_UI_EDIT_SECURE) {
            context_flags |= LEONOS_INPUTM_CONTEXT_SECURE;
        }
        (void)leonos_inputm_set_current_context(context_flags,
                                                (int32_t)x, (int32_t)y, w, h);
    }
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
        uint32_t draw_right = x + 4 + text_width;
        while (pos < state->length && draw_x < draw_right) {
            uint32_t byte_len = 1;
            uint32_t cp = ui_decode_utf8(state->buffer, state->length, pos, &byte_len);
            uint32_t px = ui_codepoint_pixel_width(cp);
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
                         cp, ui_cell_width(cp), ch_fg, ch_bg, 0);
            draw_x += px;
            pos += byte_len;
        }
    }
    if ((draw_flags & LEONOS_UI_EDIT_FOCUSED) && !(draw_flags & LEONOS_UI_EDIT_DISABLED)) {
        text_area_cursor_line_col(state, w, state->cursor, &cursor_line, &cursor_col);
        if (cursor_line >= state->scroll_line && cursor_line < state->scroll_line + rows) {
            uint32_t cx = x + 4 + cursor_col;
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

static int text_area_insert_text(struct leonos_ui_text_area_state *state, const char *text)
{
    uint32_t text_len = 0;
    if (!state || !state->buffer || state->readonly || !text) {
        return 0;
    }
    while (text[text_len]) {
        ++text_len;
    }
    if (!text_len) {
        return 0;
    }
    if (text_area_has_selection(state)) {
        uint32_t start;
        uint32_t end;
        text_area_selection_range(state, &start, &end);
        if (!text_area_delete_range(state, start, end)) {
            return 0;
        }
    }
    if (state->length + text_len >= state->capacity) {
        return 0;
    }
    for (uint32_t i = state->length + text_len + 1U;
         i > state->cursor + text_len; --i) {
        state->buffer[i - 1U] = state->buffer[i - text_len - 1U];
    }
    for (uint32_t i = 0; i < text_len; ++i) {
        state->buffer[state->cursor + i] = text[i];
    }
    state->cursor += text_len;
    state->length += text_len;
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
    if (keycode == 0) {
        char text[LEONOS_INPUTM_TEXT_LEN];
        int changed = leonos_inputm_take_text(text, sizeof(text)) ?
                          text_area_insert_text(state, text) : 0;
        if (changed) {
            text_area_cursor_line_col(state, w, state->cursor, &line, &col);
            state->preferred_column = col;
            leonos_ui_text_area_state_sync(state, w);
            text_area_ensure_cursor_visible(state, w, h);
        }
        return changed;
    }
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
                text_area_cursor_line_col(state, w, state->cursor, &line, &col);
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
        (void)leonos_inputm_set_current_context(0, 0, 0, 0, 0);
        return 1;
    }
    state->focused = 1;
    if (!state->readonly) {
        (void)leonos_inputm_set_current_context(LEONOS_INPUTM_CONTEXT_FOCUSED,
                                                (int32_t)x, (int32_t)y, w, h);
    }
    line = state->scroll_line;
    if (py > (int32_t)y + 4) {
        line += ((uint32_t)py - y - 4) / LEONOS_FONT_H;
    }
    col = 0;
    if (px > (int32_t)x + 4) {
        col = (uint32_t)px - x - 4;
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
