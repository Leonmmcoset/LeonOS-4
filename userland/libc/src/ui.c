#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdlib.h>

#include "ui_internal.h"

#define UI_T(en, zh) leonos_i18n((en), (zh))

void leonos_ui_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title)
{
    leonos_ui_bevel(surface, x, y, w, h, LEONOS_UI_GRAY, 0);
    leonos_ui_rect(surface, x + 4, y + 4, w > 8 ? w - 8 : 0, LEONOS_UI_TITLEBAR_H, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text_clipped(surface, x + 10, y + 9, w > 20 ? w - 20 : w, title, LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
}

#define UI_MESSAGE_TEXT_TOP 46u
#define UI_MESSAGE_BOTTOM_PAD 44u
#define UI_MESSAGE_LINE_STEP (LEONOS_FONT_H + 2u)
#define UI_MESSAGE_MIN_W 180u
#define UI_MESSAGE_DEFAULT_W 360u
#define UI_CONFIRM_DEFAULT_W 320u
#define UI_MESSAGE_MIN_H 150u
#define UI_MESSAGE_MAX_H 420u

static uint32_t ui_message_inner_width(uint32_t w)
{
    return w > 32 ? w - 32 : w;
}

static uint32_t ui_message_max_cells(uint32_t w)
{
    uint32_t cells = leonos_ui_text_fit_chars(ui_message_inner_width(w));
    return cells ? cells : 1u;
}

static uint32_t ui_message_skip_wrap_spaces(const char *text, uint32_t len, uint32_t pos)
{
    while (pos < len && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

static void ui_message_next_line(const char *text, uint32_t len, uint32_t start,
                                 uint32_t max_cells, uint32_t *line_end,
                                 uint32_t *next)
{
    uint32_t pos = start;
    uint32_t cells = 0;
    uint32_t last_break = 0;
    uint32_t last_break_next = 0;
    uint8_t has_break = 0;
    if (!line_end || !next) {
        return;
    }
    *line_end = start;
    *next = len;
    if (!text || start >= len) {
        return;
    }
    while (pos < len) {
        uint32_t byte_len = 1;
        uint32_t cp = ui_decode_utf8(text, len, pos, &byte_len);
        uint32_t cw;
        if (cp == '\r' || cp == '\n') {
            *line_end = pos;
            *next = pos + (byte_len ? byte_len : 1u);
            if (cp == '\r' && *next < len && text[*next] == '\n') {
                ++(*next);
            }
            return;
        }
        cw = ui_cell_width(cp);
        if (!cw) {
            cw = 1;
        }
        if (cells && cells + cw > max_cells) {
            if (has_break && last_break > start) {
                *line_end = last_break;
                *next = ui_message_skip_wrap_spaces(text, len, last_break_next);
            } else {
                *line_end = pos;
                *next = pos;
            }
            return;
        }
        cells += cw;
        pos += byte_len ? byte_len : 1u;
        *line_end = pos;
        *next = pos;
        if (cp == ' ' || cp == '\t') {
            has_break = 1;
            last_break = pos - (byte_len ? byte_len : 1u);
            last_break_next = pos;
        }
    }
}

static uint32_t ui_message_line_count(const char *message, uint32_t max_cells)
{
    uint32_t len = ui_strlen(message);
    uint32_t pos = 0;
    uint32_t count = 0;
    if (!message || !message[0]) {
        return 1;
    }
    while (pos < len && count < 512) {
        uint32_t end = pos;
        uint32_t next = pos;
        ui_message_next_line(message, len, pos, max_cells, &end, &next);
        pos = next > pos ? next : pos + 1;
        ++count;
    }
    return count ? count : 1;
}

static void ui_message_copy_line(char *line, uint32_t capacity,
                                 const char *message, uint32_t start,
                                 uint32_t end, uint32_t max_cells,
                                 uint8_t truncated)
{
    uint32_t n = 0;
    uint32_t src;
    if (!line || !capacity) {
        return;
    }
    if (truncated) {
        uint32_t prefix_cells = max_cells > 3 ? max_cells - 3 : 0;
        end = prefix_cells ? ui_byte_offset_for_cell(message, end, start, prefix_cells) : start;
    }
    src = start;
    while (message && src < end && n + 1 < capacity) {
        line[n++] = message[src++];
    }
    if (truncated && capacity > 4) {
        while (n + 4 > capacity && n > 0) {
            --n;
        }
        line[n++] = '.';
        line[n++] = '.';
        line[n++] = '.';
    }
    line[n < capacity ? n : capacity - 1] = 0;
}

static void ui_message_draw_wrapped(struct leonos_ui_surface *surface,
                                    uint32_t x, uint32_t y, uint32_t w,
                                    uint32_t h, const char *message)
{
    uint32_t inner_w = ui_message_inner_width(w);
    uint32_t max_cells = ui_message_max_cells(w);
    uint32_t line_y = y + UI_MESSAGE_TEXT_TOP;
    uint32_t text_len = ui_strlen(message);
    uint32_t pos = 0;
    uint32_t bottom = h > UI_MESSAGE_BOTTOM_PAD ? y + h - UI_MESSAGE_BOTTOM_PAD : y + h;
    uint32_t visible_lines = bottom > line_y ? (bottom - line_y) / UI_MESSAGE_LINE_STEP : 0;
    uint32_t drawn = 0;
    if (!message || !message[0]) {
        leonos_ui_text_clipped(surface, x + 16, line_y, inner_w, "",
                               LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        return;
    }
    while (pos < text_len && drawn < visible_lines) {
        char line[384];
        uint32_t start = pos;
        uint32_t end = pos;
        uint32_t next = pos;
        uint8_t truncated;
        ui_message_next_line(message, text_len, pos, max_cells, &end, &next);
        if (end == start && next == start && next < text_len) {
            uint32_t byte_len = 1;
            (void)ui_decode_utf8(message, text_len, next, &byte_len);
            end = next + (byte_len ? byte_len : 1u);
            next = end;
        }
        truncated = (drawn + 1 >= visible_lines && next < text_len) ? 1 : 0;
        ui_message_copy_line(line, sizeof(line), message, start, end, max_cells, truncated);
        leonos_ui_text_clipped(surface, x + 16, line_y, inner_w,
                               line, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        pos = next > pos ? next : pos + 1;
        line_y += UI_MESSAGE_LINE_STEP;
        ++drawn;
    }
}

void leonos_ui_message_box(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const char *title,
                           const char *message, const char *button)
{
    leonos_ui_dialog(surface, x, y, w, h, title);
    ui_message_draw_wrapped(surface, x, y, w, h, message);
    leonos_ui_button(surface, x + w / 2 - 36, y + h - 38, 72, LEONOS_UI_BUTTON_H, button ? button : "OK", 0);
}

void leonos_ui_confirm_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, const char *title,
                              const char *message, uint32_t default_yes)
{
    leonos_ui_dialog(surface, x, y, w, h, title);
    ui_message_draw_wrapped(surface, x, y, w, h, message);
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
    enum { MAX_W = UI_MESSAGE_DEFAULT_W, MAX_H = UI_MESSAGE_MAX_H };
    static uint32_t pixels[MAX_W * MAX_H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct leonos_display_state display;
    uint32_t screen_w = 640;
    uint32_t screen_h = 480;
    uint32_t max_w;
    uint32_t max_h;
    uint32_t w;
    uint32_t h;
    uint32_t line_count;
    uint32_t needed_h;
    int result = 0;
    if (leonos_display_get_state(&display) == 0 &&
        display.logical_width && display.logical_height) {
        screen_w = display.logical_width;
        screen_h = display.logical_height;
    }
    max_w = screen_w > 40 ? screen_w - 40 : screen_w;
    if (max_w > MAX_W) {
        max_w = MAX_W;
    }
    if (max_w < UI_MESSAGE_MIN_W) {
        max_w = UI_MESSAGE_MIN_W;
    }
    max_h = screen_h > 60 ? screen_h - 60 : screen_h;
    if (max_h > MAX_H) {
        max_h = MAX_H;
    }
    if (max_h < 110) {
        max_h = 110;
    }
    w = UI_MESSAGE_DEFAULT_W;
    if (w > max_w) {
        w = max_w;
    }
    line_count = ui_message_line_count(message ? message : "", ui_message_max_cells(w));
    needed_h = UI_MESSAGE_TEXT_TOP + line_count * UI_MESSAGE_LINE_STEP + 50;
    h = needed_h < UI_MESSAGE_MIN_H ? UI_MESSAGE_MIN_H : needed_h;
    if (h > max_h) {
        h = max_h;
    }
    int window_id = leonos_gui_create_app_window_ex(title ? title : "Message",
                                                    message ? message : "",
                                                    w, h, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, w, h, MAX_W);
    leonos_ui_rect(&surface, 0, 0, w, h, LEONOS_UI_GRAY);
    leonos_ui_message_box(&surface, 0, 0, w, h, title ? title : "Message",
                          message ? message : "", button ? button : "OK");
    leonos_gui_present_window((uint32_t)window_id, w, h, MAX_W, pixels);
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
                event.x >= (int32_t)(w / 2 - 36) && event.x < (int32_t)(w / 2 + 36) &&
                event.y >= (int32_t)(h - 38) && event.y < (int32_t)(h - 38 + LEONOS_UI_BUTTON_H)) {
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
    enum { MAX_W = UI_CONFIRM_DEFAULT_W, MAX_H = UI_MESSAGE_MAX_H };
    static uint32_t pixels[MAX_W * MAX_H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct leonos_display_state display;
    uint32_t screen_w = 640;
    uint32_t screen_h = 480;
    uint32_t max_w;
    uint32_t max_h;
    uint32_t w;
    uint32_t h;
    uint32_t line_count;
    uint32_t needed_h;
    int result = 0;
    if (leonos_display_get_state(&display) == 0 &&
        display.logical_width && display.logical_height) {
        screen_w = display.logical_width;
        screen_h = display.logical_height;
    }
    max_w = screen_w > 40 ? screen_w - 40 : screen_w;
    if (max_w > MAX_W) {
        max_w = MAX_W;
    }
    if (max_w < UI_MESSAGE_MIN_W) {
        max_w = UI_MESSAGE_MIN_W;
    }
    max_h = screen_h > 60 ? screen_h - 60 : screen_h;
    if (max_h > MAX_H) {
        max_h = MAX_H;
    }
    if (max_h < 110) {
        max_h = 110;
    }
    w = UI_CONFIRM_DEFAULT_W;
    if (w > max_w) {
        w = max_w;
    }
    line_count = ui_message_line_count(message ? message : "", ui_message_max_cells(w));
    needed_h = UI_MESSAGE_TEXT_TOP + line_count * UI_MESSAGE_LINE_STEP + 50;
    h = needed_h < UI_MESSAGE_MIN_H ? UI_MESSAGE_MIN_H : needed_h;
    if (h > max_h) {
        h = max_h;
    }
    int window_id = leonos_gui_create_app_window_ex(title ? title : "Confirm",
                                                    message ? message : "",
                                                    w, h, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, w, h, MAX_W);
    leonos_ui_rect(&surface, 0, 0, w, h, LEONOS_UI_GRAY);
    leonos_ui_confirm_dialog(&surface, 0, 0, w, h, title ? title : "Confirm",
                             message ? message : "", default_yes);
    leonos_gui_present_window((uint32_t)window_id, w, h, MAX_W, pixels);
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
                if (event.x >= (int32_t)(w - 168) && event.x < (int32_t)(w - 96) &&
                    event.y >= (int32_t)(h - 38) && event.y < (int32_t)(h - 38 + LEONOS_UI_BUTTON_H)) {
                    result = 1;
                    break;
                }
                if (event.x >= (int32_t)(w - 88) && event.x < (int32_t)(w - 16) &&
                    event.y >= (int32_t)(h - 38) && event.y < (int32_t)(h - 38 + LEONOS_UI_BUTTON_H)) {
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

static uint32_t ui_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
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
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_FILE_DIALOG_LIST_BODY_X,
                                  UI_FILE_DIALOG_LIST_BODY_Y,
                                  UI_FILE_DIALOG_LIST_BODY_W,
                                  UI_FILE_DIALOG_LIST_ROWS * UI_FILE_DIALOG_ROW_H) ||
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_FILE_DIALOG_SCROLL_X,
                                  UI_FILE_DIALOG_LIST_Y,
                                  UI_FILE_DIALOG_SCROLL_W,
                                  UI_FILE_DIALOG_LIST_H)) {
                    if (leonos_ui_listview_state_handle_wheel(&list_state, event.dy)) {
                        list_state.focused = 1;
                        name_edit.focused = 0;
                    }
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

enum {
    UI_OPEN_WITH_W = 432,
    UI_OPEN_WITH_H = 340,
    UI_OPEN_WITH_X = 0,
    UI_OPEN_WITH_Y = 0,
    UI_OPEN_WITH_ROW_H = 34,
    UI_OPEN_WITH_VISIBLE_ROWS = 3,
    UI_OPEN_WITH_LIST_X = 16,
    UI_OPEN_WITH_LIST_Y = 170,
    UI_OPEN_WITH_LIST_W = UI_OPEN_WITH_W - 32,
    UI_OPEN_WITH_SCROLL_W = 18,
    UI_OPEN_WITH_ROW_W = UI_OPEN_WITH_LIST_W - 26,
    UI_OPEN_WITH_BUTTON_Y = UI_OPEN_WITH_H - 38
};

static const struct leonos_launch_assoc_app *ui_open_with_find_app(
    const struct leonos_launch_assoc_app *apps,
    uint32_t app_count,
    const char *program_path)
{
    for (uint32_t i = 0; i < app_count; ++i) {
        if (ui_text_eq(apps[i].program_path, program_path)) {
            return &apps[i];
        }
    }
    return 0;
}

static int ui_open_with_find_index(const struct leonos_launch_assoc_app *apps,
                                   uint32_t app_count,
                                   const char *program_path)
{
    for (uint32_t i = 0; i < app_count; ++i) {
        if (ui_text_eq(apps[i].program_path, program_path)) {
            return (int)i;
        }
    }
    return -1;
}

static const char *ui_open_with_app_label(const struct leonos_launch_assoc_app *apps,
                                          uint32_t app_count,
                                          const char *program_path,
                                          char *buffer,
                                          uint32_t capacity)
{
    const struct leonos_launch_assoc_app *app =
        ui_open_with_find_app(apps, app_count, program_path);
    if (app) {
        return app->name;
    }
    if (!program_path || !program_path[0]) {
        return UI_T("None", "无");
    }
    ui_copy_text(buffer, capacity, program_path);
    return buffer;
}

static void ui_open_with_draw(struct leonos_ui_surface *surface,
                              const char *title,
                              const char *path,
                              const char *extension,
                              const char *default_label,
                              const struct leonos_launch_assoc_app *apps,
                              uint32_t app_count,
                              const struct leonos_ui_listview_state *list_state,
                              uint32_t remember,
                              uint32_t can_remember,
                              uint32_t set_default_mode)
{
    uint32_t list_h = list_state->visible_rows * UI_OPEN_WITH_ROW_H + 8;
    uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X + UI_OPEN_WITH_LIST_W - UI_OPEN_WITH_SCROLL_W;
    leonos_ui_rect(surface, 0, 0, UI_OPEN_WITH_W, UI_OPEN_WITH_H, LEONOS_UI_GRAY);
    leonos_ui_dialog(surface, UI_OPEN_WITH_X, UI_OPEN_WITH_Y,
                     UI_OPEN_WITH_W, UI_OPEN_WITH_H, title ? title : UI_T("Open With", "打开方式"));
    leonos_ui_text_clipped(surface, 16, 44, UI_OPEN_WITH_W - 32,
                           set_default_mode
                               ? UI_T("Choose a default program for this file type:",
                                      "选择此文件类型的默认程序:")
                               : UI_T("Choose a program to open this file:",
                                      "选择用于打开此文件的程序:"),
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(surface, 16, 68, UI_T("File:", "文件:"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 58, 64, UI_OPEN_WITH_W - 74, path ? path : "",
                   ui_strlen(path), 0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(surface, 16, 92, UI_T("Extension:", "扩展名:"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 82, 88, 84,
                   extension && extension[0] ? extension : UI_T("(none)", "(无)"),
                   ui_strlen(extension && extension[0] ? extension : UI_T("(none)", "(无)")),
                   0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(surface, 180, 92, UI_T("Default:", "默认:"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(surface, 244, 88, UI_OPEN_WITH_W - 260,
                   default_label ? default_label : UI_T("None", "无"),
                   ui_strlen(default_label ? default_label : UI_T("None", "无")),
                   0, LEONOS_UI_EDIT_READONLY);
    if (set_default_mode) {
        leonos_ui_checkbox(surface, 16, 118, UI_T("Update default program", "更新默认程序"), 1,
                           LEONOS_UI_BUTTON_DISABLED);
    } else {
        leonos_ui_checkbox(surface, 16, 118, UI_T("Always use this app", "始终使用此应用"),
                           can_remember ? (int)remember : 0,
                           can_remember ? 0 : LEONOS_UI_BUTTON_DISABLED);
    }
    leonos_ui_text(surface, 16, 144, UI_T("Programs:", "程序:"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_inset(surface, UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                    UI_OPEN_WITH_LIST_W, list_h, LEONOS_UI_WHITE);
    for (uint32_t row = 0; row < list_state->visible_rows; ++row) {
        uint32_t i = list_state->scroll + row;
        uint32_t row_x = UI_OPEN_WITH_LIST_X + 4;
        uint32_t row_y = UI_OPEN_WITH_LIST_Y + 4 + row * UI_OPEN_WITH_ROW_H;
        uint32_t selected;
        uint32_t bg;
        uint32_t fg;
        uint32_t detail_fg;
        if (i >= app_count) {
            break;
        }
        selected = list_state->selected == (int32_t)i;
        bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
        fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        detail_fg = selected ? LEONOS_UI_LIGHT : LEONOS_UI_DARK;
        leonos_ui_rect(surface, row_x, row_y, UI_OPEN_WITH_ROW_W,
                       UI_OPEN_WITH_ROW_H, bg);
        leonos_ui_text_clipped(surface, row_x + 8, row_y + 3,
                               UI_OPEN_WITH_ROW_W - 16, apps[i].name, fg, bg);
        leonos_ui_text_clipped(surface, row_x + 8, row_y + 18,
                               UI_OPEN_WITH_ROW_W - 16, apps[i].detail,
                               detail_fg, bg);
    }
    leonos_ui_vscrollbar(surface, scrollbar_x, UI_OPEN_WITH_LIST_Y,
                         UI_OPEN_WITH_SCROLL_W, list_h,
                         list_state->scroll,
                         ui_max_u32(app_count, list_state->visible_rows),
                         list_state->visible_rows,
                         app_count <= list_state->visible_rows
                             ? LEONOS_UI_SCROLLBAR_DISABLED
                             : 0);
    leonos_ui_button(surface, UI_OPEN_WITH_W - 194, UI_OPEN_WITH_BUTTON_Y,
                     96, LEONOS_UI_BUTTON_H,
                     set_default_mode ? UI_T("Set Default", "设为默认") : UI_T("Open", "打开"), 0);
    leonos_ui_button(surface, UI_OPEN_WITH_W - 88, UI_OPEN_WITH_BUTTON_Y,
                     72, LEONOS_UI_BUTTON_H, UI_T("Cancel", "取消"), 0);
}

int leonos_ui_show_open_with_dialog(const char *title, const char *path,
                                    char *program_path, uint32_t capacity,
                                    uint32_t *remember, uint32_t flags)
{
    static uint32_t pixels[UI_OPEN_WITH_W * UI_OPEN_WITH_H];
    struct leonos_ui_surface surface;
    struct leonos_gui_app_event event;
    struct leonos_ui_listview_state list_state;
    const struct leonos_launch_assoc_app *apps;
    uint32_t app_count = 0;
    uint32_t remember_value = remember ? *remember : 0;
    uint32_t set_default_mode = (flags & LEONOS_UI_OPEN_WITH_SET_DEFAULT) != 0;
    uint32_t can_remember;
    char extension[16];
    char default_program[LEONOS_FS_PATH_LEN];
    char default_label_buf[LEONOS_FS_PATH_LEN];
    const char *default_program_ptr;
    const char *default_label;
    int selected = 0;
    int result = 0;
    int window_id;

    if (!path || !path[0] || !program_path || capacity == 0) {
        return -1;
    }
    apps = leonos_launch_assoc_apps(&app_count);
    if (!apps || app_count == 0) {
        return -1;
    }
    extension[0] = 0;
    can_remember = leonos_launch_get_extension_for_path(path, extension,
                                                        sizeof(extension)) != 0;
    default_program[0] = 0;
    default_program_ptr = leonos_launch_resolve_default_app_for_path(path);
    if (default_program_ptr) {
        ui_copy_text(default_program, sizeof(default_program), default_program_ptr);
        selected = ui_open_with_find_index(apps, app_count, default_program);
        if (selected < 0) {
            selected = 0;
        }
    }
    default_label = ui_open_with_app_label(apps, app_count, default_program,
                                           default_label_buf,
                                           sizeof(default_label_buf));
    if (!can_remember) {
        remember_value = 0;
    }
    leonos_ui_listview_state_init(&list_state,
                                  app_count > UI_OPEN_WITH_VISIBLE_ROWS
                                      ? UI_OPEN_WITH_VISIBLE_ROWS
                                      : app_count,
                                  UI_OPEN_WITH_ROW_H);
    leonos_ui_listview_state_set_count(&list_state, app_count);
    list_state.selected = selected;
    list_state.focused = 1;

    window_id = leonos_gui_create_app_window_ex(title ? title : UI_T("Open With", "打开方式"),
                                                path,
                                                UI_OPEN_WITH_W, UI_OPEN_WITH_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return window_id;
    }
    leonos_ui_bind(&surface, pixels, UI_OPEN_WITH_W, UI_OPEN_WITH_H,
                   UI_OPEN_WITH_W);
    for (;;) {
        uint32_t activated = 0;
        ui_open_with_draw(&surface, title ? title : UI_T("Open With", "打开方式"), path,
                          extension, default_label, apps, app_count,
                          &list_state, remember_value, can_remember,
                          set_default_mode);
        leonos_gui_present_window((uint32_t)window_id, UI_OPEN_WITH_W,
                                  UI_OPEN_WITH_H, UI_OPEN_WITH_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if ((event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                 event.type == LEONOS_GUI_APP_EVENT_KEY_UP) && event.pressed) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    result = 1;
                    break;
                }
                if (event.keycode == 1) {
                    break;
                }
                if (event.keycode == LEONOS_KEY_SPACE &&
                    !set_default_mode && can_remember) {
                    remember_value = remember_value ? 0 : 1;
                    continue;
                }
                if (leonos_ui_listview_state_handle_key(&list_state,
                                                        event.keycode,
                                                        &activated)) {
                    if (activated) {
                        result = 1;
                        break;
                    }
                    continue;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1u)) {
                uint32_t list_h = list_state.visible_rows * UI_OPEN_WITH_ROW_H + 8;
                uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X +
                                       UI_OPEN_WITH_LIST_W -
                                       UI_OPEN_WITH_SCROLL_W;
                if (event.x >= (int32_t)(UI_OPEN_WITH_W - 194) &&
                    event.x < (int32_t)(UI_OPEN_WITH_W - 98) &&
                    event.y >= (int32_t)UI_OPEN_WITH_BUTTON_Y &&
                    event.y < (int32_t)(UI_OPEN_WITH_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    result = 1;
                    break;
                }
                if (event.x >= (int32_t)(UI_OPEN_WITH_W - 88) &&
                    event.x < (int32_t)(UI_OPEN_WITH_W - 16) &&
                    event.y >= (int32_t)UI_OPEN_WITH_BUTTON_Y &&
                    event.y < (int32_t)(UI_OPEN_WITH_BUTTON_Y + LEONOS_UI_BUTTON_H)) {
                    break;
                }
                if (!set_default_mode && can_remember &&
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  16, 118, 180, LEONOS_FONT_H + 8)) {
                    remember_value = remember_value ? 0 : 1;
                    continue;
                }
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  scrollbar_x, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_SCROLL_W, list_h)) {
                    leonos_ui_vscrollbar_handle_mouse(&list_state.scroll,
                                                       ui_max_u32(app_count,
                                                                  list_state.visible_rows),
                                                       list_state.visible_rows,
                                                       scrollbar_x,
                                                       UI_OPEN_WITH_LIST_Y,
                                                       UI_OPEN_WITH_SCROLL_W,
                                                       list_h,
                                                       event.x, event.y);
                    continue;
                }
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_LIST_W, list_h)) {
                    if (leonos_ui_listview_state_handle_mouse(&list_state,
                                                              event.x, event.y,
                                                              UI_OPEN_WITH_LIST_X + 4,
                                                              UI_OPEN_WITH_LIST_Y + 4,
                                                              UI_OPEN_WITH_ROW_W,
                                                              &activated) &&
                        activated) {
                        result = 1;
                        break;
                    }
                    continue;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                uint32_t list_h = list_state.visible_rows * UI_OPEN_WITH_ROW_H + 8;
                uint32_t scrollbar_x = UI_OPEN_WITH_LIST_X +
                                       UI_OPEN_WITH_LIST_W -
                                       UI_OPEN_WITH_SCROLL_W;
                if (leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  UI_OPEN_WITH_LIST_X, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_LIST_W, list_h) ||
                    leonos_ui_hit((uint32_t)event.x, (uint32_t)event.y,
                                  scrollbar_x, UI_OPEN_WITH_LIST_Y,
                                  UI_OPEN_WITH_SCROLL_W, list_h)) {
                    leonos_ui_listview_state_handle_wheel(&list_state, event.dy);
                    continue;
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    if (!result) {
        return 0;
    }
    if (list_state.selected < 0 || (uint32_t)list_state.selected >= app_count) {
        return -1;
    }
    ui_copy_text(program_path, capacity, apps[list_state.selected].program_path);
    if (remember) {
        *remember = set_default_mode ? 1 : remember_value;
    }
    return 1;
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

uint32_t leonos_ui_dropdown_height(uint32_t count, uint32_t row_h,
                                   uint32_t progress)
{
    uint32_t full_h;
    uint32_t eased;
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    full_h = 8 + count * row_h;
    if (progress > 1000) {
        progress = 1000;
    }
    if (progress == 0 || full_h == 0) {
        return 0;
    }
    eased = leonos_ui_anim_ease_out(progress);
    return (full_h * eased + 999) / 1000;
}

void leonos_ui_dropdown(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const struct leonos_ui_dropdown_item *items,
                        uint32_t count, uint32_t selected_id, uint32_t row_h,
                        uint32_t progress)
{
    uint32_t visible_h = leonos_ui_dropdown_height(count, row_h, progress);
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    if (!visible_h || !w) {
        return;
    }
    leonos_ui_bevel(surface, x, y, w, visible_h, LEONOS_UI_WHITE, 0);
    if (visible_h <= 8) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row_y = y + 4 + i * row_h;
        uint32_t flags = items ? items[i].flags : LEONOS_UI_MENU_DISABLED;
        const char *label = items ? items[i].label : "";
        uint32_t row_bottom = row_y + row_h;
        if (row_y >= y + visible_h - 3) {
            break;
        }
        if (row_bottom > y + visible_h - 3) {
            continue;
        }
        if (flags & LEONOS_UI_MENU_SEPARATOR) {
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2,
                           w > 8 ? w - 8 : w, 1, LEONOS_UI_DARK);
            leonos_ui_rect(surface, x + 4, row_y + row_h / 2 + 1,
                           w > 8 ? w - 8 : w, 1, LEONOS_UI_WHITE);
            continue;
        }
        if (items && items[i].id == selected_id && !(flags & LEONOS_UI_MENU_DISABLED)) {
            leonos_ui_rect(surface, x + 3, row_y, w > 6 ? w - 6 : w,
                           row_h, LEONOS_UI_ACTIVE_TITLE);
            leonos_ui_text_transparent_clipped(surface, x + 8, row_y + 4,
                                               w > 16 ? w - 16 : w,
                                               label ? label : "",
                                               LEONOS_UI_WHITE);
        } else {
            leonos_ui_text_transparent_clipped(surface, x + 8, row_y + 4,
                                               w > 16 ? w - 16 : w,
                                               label ? label : "",
                                               (flags & LEONOS_UI_MENU_DISABLED)
                                                   ? LEONOS_UI_DARK
                                                   : LEONOS_UI_BLACK);
        }
    }
}

int leonos_ui_dropdown_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_dropdown_item *items,
                           uint32_t count, uint32_t row_h, uint32_t progress,
                           uint32_t *out_id)
{
    uint32_t visible_h = leonos_ui_dropdown_height(count, row_h, progress);
    uint32_t index;
    if (out_id) {
        *out_id = 0;
    }
    if (row_h < LEONOS_FONT_H + 8) {
        row_h = LEONOS_FONT_H + 8;
    }
    if (!visible_h ||
        !leonos_ui_hit((uint32_t)px, (uint32_t)py, (int32_t)x, (int32_t)y,
                       w, visible_h) ||
        py < (int32_t)y + 4) {
        return 0;
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
