#ifndef LEONOS_UI_H
#define LEONOS_UI_H

#include <stdint.h>

#define LEONOS_UI_THEME_WIN95 0u
#define LEONOS_UI_THEME_METRO 1u

#define LEONOS_UI_COLOR_SCHEME_BLUE 0u
#define LEONOS_UI_COLOR_SCHEME_TEAL 1u
#define LEONOS_UI_COLOR_SCHEME_GREEN 2u
#define LEONOS_UI_COLOR_SCHEME_PURPLE 3u
#define LEONOS_UI_COLOR_SCHEME_RED 4u
#define LEONOS_UI_COLOR_SCHEME_GRAPHITE 5u
#define LEONOS_UI_COLOR_SCHEME_COUNT 6u

#define LEONOS_UI_COLOR_TEXT 0u
#define LEONOS_UI_COLOR_CONTENT 1u
#define LEONOS_UI_COLOR_SURFACE 2u
#define LEONOS_UI_COLOR_SUBTLE 3u
#define LEONOS_UI_COLOR_MUTED 4u
#define LEONOS_UI_COLOR_ACCENT 5u
#define LEONOS_UI_COLOR_TITLE_INACTIVE 6u
#define LEONOS_UI_COLOR_DESKTOP 7u
#define LEONOS_UI_COLOR_BORDER 8u
#define LEONOS_UI_COLOR_SELECTION 9u

#define LEONOS_UI_BLACK leonos_ui_color(LEONOS_UI_COLOR_TEXT)
#define LEONOS_UI_WHITE leonos_ui_color(LEONOS_UI_COLOR_CONTENT)
#define LEONOS_UI_GRAY leonos_ui_color(LEONOS_UI_COLOR_SURFACE)
#define LEONOS_UI_LIGHT leonos_ui_color(LEONOS_UI_COLOR_SUBTLE)
#define LEONOS_UI_DARK leonos_ui_color(LEONOS_UI_COLOR_MUTED)
#define LEONOS_UI_ACTIVE_TITLE leonos_ui_color(LEONOS_UI_COLOR_ACCENT)
#define LEONOS_UI_INACTIVE_TITLE leonos_ui_color(LEONOS_UI_COLOR_TITLE_INACTIVE)
#define LEONOS_UI_DESKTOP leonos_ui_color(LEONOS_UI_COLOR_DESKTOP)

#define LEONOS_UI_TITLEBAR_H 26u
#define LEONOS_UI_TASKBAR_H 34u
#define LEONOS_UI_BUTTON_H 24u
#define LEONOS_UI_WINDOW_BUTTON_W 18u
#define LEONOS_UI_WINDOW_BUTTON_H 20u

#define LEONOS_UI_BUTTON_PRESSED 0x01u
#define LEONOS_UI_BUTTON_ACTIVE 0x02u
#define LEONOS_UI_BUTTON_DISABLED 0x04u
#define LEONOS_UI_WINDOW_ACTIVE 0x01u
#define LEONOS_UI_WINDOW_NO_RESIZE 0x02u
#define LEONOS_UI_MENU_SEPARATOR 0x01u
#define LEONOS_UI_MENU_SELECTED 0x02u
#define LEONOS_UI_MENU_DISABLED 0x04u
#define LEONOS_UI_EDIT_FOCUSED 0x01u
#define LEONOS_UI_EDIT_READONLY 0x02u
#define LEONOS_UI_EDIT_DISABLED 0x04u
#define LEONOS_UI_EDIT_SECURE 0x08u
#define LEONOS_UI_SCROLLBAR_DISABLED 0x01u
#define LEONOS_UI_TAB_DISABLED 0x01u
#define LEONOS_UI_INPUT_DISABLED 0x01u
#define LEONOS_UI_TOOLBAR_BUTTON_ACTIVE LEONOS_UI_BUTTON_ACTIVE
#define LEONOS_UI_TOOLBAR_BUTTON_PRESSED LEONOS_UI_BUTTON_PRESSED
#define LEONOS_UI_TOOLBAR_BUTTON_DISABLED LEONOS_UI_BUTTON_DISABLED
#define LEONOS_UI_OPEN_WITH_SET_DEFAULT 0x01u
#define LEONOS_UI_TOAST_INFO 0u
#define LEONOS_UI_TOAST_SUCCESS 1u
#define LEONOS_UI_TOAST_WARNING 2u
#define LEONOS_UI_TOAST_ERROR 3u
#define LEONOS_UI_SPLIT_VERTICAL 1u
#define LEONOS_UI_SPLIT_HORIZONTAL 0u
#define LEONOS_UI_TREEVIEW_ITEM_DISABLED 0x01u
#define LEONOS_UI_TREEVIEW_MAX_ITEMS 128u
#define LEONOS_UI_FILE_DIALOG_INPUT_CHECKBOX 1u
#define LEONOS_UI_FILE_DIALOG_INPUT_DROPDOWN 2u
#define LEONOS_UI_FILE_DIALOG_MAX_INPUTS 4u

struct leonos_ui_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct leonos_ui_rect {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
};

struct leonos_ui_window_parts {
    struct leonos_ui_rect titlebar;
    struct leonos_ui_rect body;
    struct leonos_ui_rect minimize;
    struct leonos_ui_rect maximize;
    struct leonos_ui_rect close;
};

struct leonos_ui_list_column {
    const char *label;
    uint32_t width;
};

struct leonos_ui_edit_state {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
    uint32_t cursor;
    uint32_t scroll;
    uint32_t selection_anchor;
    uint8_t focused;
    uint8_t readonly;
    uint8_t selecting;
};

struct leonos_ui_text_area_state {
    char *buffer;
    uint32_t capacity;
    uint32_t length;
    uint32_t cursor;
    uint32_t selection_anchor;
    uint32_t scroll_line;
    uint32_t preferred_column;
    uint32_t line_count;
    uint8_t focused;
    uint8_t readonly;
    uint8_t selecting;
};

uint32_t leonos_ui_theme(void);
int leonos_ui_theme_set(uint32_t theme);
uint32_t leonos_ui_theme_color_scheme(uint32_t theme);
uint32_t leonos_ui_theme_active_color_scheme(void);
uint32_t leonos_ui_theme_scheme_accent(uint32_t theme, uint32_t scheme);
int leonos_ui_theme_set_color_scheme(uint32_t theme, uint32_t scheme);
int leonos_ui_theme_set_appearance(uint32_t theme,
                                   uint32_t metro_color_scheme,
                                   uint32_t win95_color_scheme);
void leonos_ui_theme_load_system(void);
uint32_t leonos_ui_color(uint32_t role);

struct leonos_ui_listview_state {
    uint32_t row_count;
    uint32_t visible_rows;
    uint32_t row_height;
    uint32_t scroll;
    int32_t selected;
    uint8_t focused;
};

struct leonos_ui_treeview_item {
    uint32_t id;
    uint32_t parent_id;
    const char *const *cells;
    uint32_t flags;
};

struct leonos_ui_treeview_state {
    uint32_t visible_rows;
    uint32_t row_height;
    uint32_t scroll;
    uint32_t selected_id;
    uint32_t visible_count;
    uint32_t collapsed_count;
    uint32_t visible_indices[LEONOS_UI_TREEVIEW_MAX_ITEMS];
    uint32_t collapsed_ids[LEONOS_UI_TREEVIEW_MAX_ITEMS];
    uint8_t visible_depths[LEONOS_UI_TREEVIEW_MAX_ITEMS];
    uint8_t focused;
    uint8_t has_selection;
};

struct leonos_ui_context_menu_item {
    const char *label;
    uint32_t id;
    uint32_t flags;
};

struct leonos_ui_menubar_item {
    const char *label;
    uint32_t id;
    uint32_t width;
    uint32_t flags;
};

struct leonos_ui_dropdown_item {
    const char *label;
    uint32_t id;
    uint32_t flags;
};

struct leonos_ui_file_dialog_input {
    uint32_t type;
    uint32_t id;
    const char *label;
    uint32_t flags;
    uint32_t *value;
    const struct leonos_ui_dropdown_item *items;
    uint32_t item_count;
};

struct leonos_ui_file_dialog_options {
    const struct leonos_ui_file_dialog_input *inputs;
    uint32_t input_count;
};

struct leonos_ui_tab_item {
    const char *label;
    uint32_t id;
    uint32_t flags;
};

struct leonos_ui_tab_state {
    uint32_t selected_id;
    uint32_t hovered_id;
    uint8_t focused;
};

struct leonos_ui_color_input_state {
    uint32_t color;
    uint8_t open;
    uint8_t focused;
    uint8_t channel;
};

struct leonos_ui_date_input_state {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t open;
    uint8_t focused;
    uint8_t part;
};

struct leonos_ui_layout {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t gap;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t row_h;
};

struct leonos_ui_tree_item {
    const char *label;
    uint32_t id;
    uint32_t depth;
    uint32_t flags;
};

struct leonos_ui_property_item {
    const char *label;
    const char *value;
    uint32_t flags;
};

struct leonos_ui_split_pane_state {
    uint32_t vertical;
    uint32_t split;
    uint32_t min_first;
    uint32_t min_second;
    uint32_t splitter_size;
    uint8_t dragging;
    struct leonos_ui_rect first;
    struct leonos_ui_rect splitter;
    struct leonos_ui_rect second;
};

struct leonos_ui_toast_state {
    char message[160];
    unsigned long start_ms;
    uint32_t duration_ms;
    uint32_t kind;
    uint8_t active;
};

#define LEONOS_UI_TREE_EXPANDED 0x01u
#define LEONOS_UI_TREE_SELECTED 0x02u
#define LEONOS_UI_TREE_LEAF 0x04u

void leonos_ui_bind(struct leonos_ui_surface *surface, uint32_t *pixels,
                    uint32_t width, uint32_t height, uint32_t stride);
int leonos_ui_set_font_path(const char *path);
int leonos_ui_set_font_fallback_path(const char *path);
uint32_t leonos_ui_text_width(const char *text);
uint32_t leonos_ui_text_fit_chars(uint32_t pixel_width);
int leonos_ui_hit(uint32_t px, uint32_t py, int32_t x, int32_t y, uint32_t w, uint32_t h);
int leonos_ui_keycode_to_char(uint8_t keycode, char *out);
int leonos_ui_keycode_to_char_shift(uint8_t keycode, uint8_t shifted, char *out);
void leonos_ui_pixel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t color);
void leonos_ui_rect(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color);
void leonos_ui_codepoint(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t codepoint, uint32_t cell_width,
                         uint32_t fg, uint32_t bg);
void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    const char *text, uint32_t fg, uint32_t bg);
void leonos_ui_text_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const char *text, uint32_t fg, uint32_t bg);
void leonos_ui_text_resized_clipped(struct leonos_ui_surface *surface,
                                    uint32_t x, uint32_t y, uint32_t w,
                                    const char *text, uint32_t fg, uint32_t bg,
                                    uint32_t cell_w, uint32_t cell_h);
void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                const char *text, uint32_t fg);
void leonos_ui_text_transparent_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                        uint32_t w, const char *text, uint32_t fg);
void leonos_ui_bevel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill, uint32_t flags);
void leonos_ui_inset(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t fill);
void leonos_ui_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *label, uint32_t flags);
void leonos_ui_window_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                             char label, uint32_t flags);
void leonos_ui_window(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title, uint32_t flags,
                      struct leonos_ui_window_parts *parts);
void leonos_ui_window_ex(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *title, char maximize_label,
                         uint32_t flags, struct leonos_ui_window_parts *parts);
void leonos_ui_taskbar(struct leonos_ui_surface *surface, uint32_t y, uint32_t h);
void leonos_ui_taskbar_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, const char *label, uint32_t flags);
void leonos_ui_menu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h);
void leonos_ui_menu_item(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, const char *label, uint32_t flags);
uint32_t leonos_ui_context_menu_height(uint32_t count);
void leonos_ui_context_menu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const struct leonos_ui_context_menu_item *items,
                            uint32_t count);
void leonos_ui_context_menu_animated(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                     uint32_t w, const struct leonos_ui_context_menu_item *items,
                                     uint32_t count, uint32_t progress);
int leonos_ui_context_menu_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                               uint32_t w, const struct leonos_ui_context_menu_item *items,
                               uint32_t count, uint32_t *out_id);
void leonos_ui_layout_begin(struct leonos_ui_layout *layout, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t gap);
struct leonos_ui_rect leonos_ui_layout_next(struct leonos_ui_layout *layout,
                                            uint32_t preferred_w, uint32_t preferred_h);
uint32_t leonos_ui_anim_progress(unsigned long now, unsigned long start,
                                 unsigned long duration_ms);
uint32_t leonos_ui_anim_ease_out(uint32_t progress);
uint32_t leonos_ui_anim_lerp(uint32_t from, uint32_t to, uint32_t progress);
void leonos_ui_activity_bar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t progress);
void leonos_ui_tree(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const struct leonos_ui_tree_item *items,
                    uint32_t count, uint32_t row_h);
int leonos_ui_tree_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                       uint32_t w, const struct leonos_ui_tree_item *items,
                       uint32_t count, uint32_t row_h, uint32_t *out_id);
void leonos_ui_panel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t w, uint32_t h, uint32_t color);
void leonos_ui_checkbox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        const char *label, int checked, uint32_t flags);
void leonos_ui_progress(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t value, uint32_t max);
void leonos_ui_text_field(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const char *text, uint32_t flags);
void leonos_ui_list_header(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const char *label);
void leonos_ui_list_row(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const char *text, uint32_t flags);
void leonos_ui_vscrollbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t value, uint32_t max,
                          uint32_t page, uint32_t flags);
void leonos_ui_hscrollbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t value, uint32_t max,
                          uint32_t page, uint32_t flags);
void leonos_ui_scroll_view_frame(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                                 uint32_t w, uint32_t h);
void leonos_ui_edit(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t w, const char *text, uint32_t cursor, uint32_t scroll,
                    uint32_t flags);
void leonos_ui_edit_state_init(struct leonos_ui_edit_state *state, char *buffer,
                               uint32_t capacity);
void leonos_ui_edit_state_sync(struct leonos_ui_edit_state *state);
void leonos_ui_edit_state_draw(struct leonos_ui_surface *surface, uint32_t x,
                               uint32_t y, uint32_t w,
                               struct leonos_ui_edit_state *state,
                               uint32_t flags);
int leonos_ui_edit_state_handle_key(struct leonos_ui_edit_state *state,
                                    uint8_t keycode, uint8_t pressed);
int leonos_ui_edit_state_handle_mouse(struct leonos_ui_edit_state *state,
                                      int32_t px, int32_t py, uint32_t x,
                                      uint32_t y, uint32_t w, uint32_t buttons);
void leonos_ui_text_area(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, const char *text, uint32_t cursor,
                         uint32_t scroll_line, uint32_t flags);
void leonos_ui_text_area_state_init(struct leonos_ui_text_area_state *state,
                                    char *buffer, uint32_t capacity);
void leonos_ui_text_area_state_sync(struct leonos_ui_text_area_state *state,
                                    uint32_t w);
void leonos_ui_text_area_state_draw(struct leonos_ui_surface *surface, uint32_t x,
                                    uint32_t y, uint32_t w, uint32_t h,
                                    struct leonos_ui_text_area_state *state,
                                    uint32_t flags);
int leonos_ui_text_area_state_handle_key(struct leonos_ui_text_area_state *state,
                                         uint8_t keycode, uint8_t pressed,
                                         uint32_t w, uint32_t h);
int leonos_ui_text_area_state_handle_mouse(struct leonos_ui_text_area_state *state,
                                           int32_t px, int32_t py, uint32_t x,
                                           uint32_t y, uint32_t w, uint32_t h,
                                           uint32_t buttons);
uint32_t leonos_ui_text_area_line_count(struct leonos_ui_text_area_state *state,
                                        uint32_t w);
void leonos_ui_listview_header(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                               uint32_t w, const struct leonos_ui_list_column *cols,
                               uint32_t count);
void leonos_ui_listview_row(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const struct leonos_ui_list_column *cols,
                            const char *const cells[], uint32_t count, uint32_t flags);
void leonos_ui_listview_state_init(struct leonos_ui_listview_state *state,
                                   uint32_t visible_rows, uint32_t row_height);
void leonos_ui_listview_state_set_count(struct leonos_ui_listview_state *state,
                                        uint32_t row_count);
int leonos_ui_listview_state_handle_key(struct leonos_ui_listview_state *state,
                                        uint8_t keycode, uint32_t *activated);
int leonos_ui_listview_state_handle_mouse(struct leonos_ui_listview_state *state,
                                          int32_t px, int32_t py, uint32_t x,
                                          uint32_t rows_y, uint32_t w,
                                          uint32_t *activated);
int leonos_ui_listview_state_handle_wheel(struct leonos_ui_listview_state *state,
                                          int32_t wheel_delta);
void leonos_ui_treeview_state_init(struct leonos_ui_treeview_state *state,
                                   uint32_t visible_rows, uint32_t row_height);
void leonos_ui_treeview_state_set_viewport(struct leonos_ui_treeview_state *state,
                                           uint32_t visible_rows);
void leonos_ui_treeview_state_sync(struct leonos_ui_treeview_state *state,
                                   const struct leonos_ui_treeview_item *items,
                                   uint32_t count);
void leonos_ui_treeview(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const struct leonos_ui_list_column *cols,
                        uint32_t col_count,
                        const struct leonos_ui_treeview_item *items,
                        uint32_t count, struct leonos_ui_treeview_state *state);
int leonos_ui_treeview_state_handle_key(struct leonos_ui_treeview_state *state,
                                        const struct leonos_ui_treeview_item *items,
                                        uint32_t count, uint8_t keycode,
                                        uint32_t *activated);
int leonos_ui_treeview_state_handle_mouse(struct leonos_ui_treeview_state *state,
                                          const struct leonos_ui_treeview_item *items,
                                          uint32_t count, int32_t px, int32_t py,
                                          uint32_t x, uint32_t rows_y, uint32_t w,
                                          uint32_t *activated);
int leonos_ui_treeview_state_handle_wheel(struct leonos_ui_treeview_state *state,
                                          int32_t wheel_delta);
int leonos_ui_vscrollbar_handle_mouse(uint32_t *value, uint32_t max, uint32_t page,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h, int32_t px, int32_t py);
int leonos_ui_vscrollbar_handle_wheel(uint32_t *value, uint32_t max, uint32_t page,
                                      int32_t wheel_delta);
int leonos_ui_hscrollbar_handle_mouse(uint32_t *value, uint32_t max, uint32_t page,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h, int32_t px, int32_t py);
void leonos_ui_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, const char *title);
void leonos_ui_message_box(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const char *title,
                           const char *message, const char *button);
void leonos_ui_confirm_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, const char *title,
                              const char *message, uint32_t default_yes);
void leonos_ui_input_dialog(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, const char *title,
                            const char *label, const char *value, uint32_t flags);
int leonos_ui_show_message_box(const char *title, const char *message,
                               const char *button);
int leonos_ui_show_confirm_dialog(const char *title, const char *message,
                                  uint32_t default_yes);
int leonos_ui_show_input_dialog(const char *title, const char *label,
                                char *value, uint32_t capacity);
int leonos_ui_show_password_dialog(const char *title, const char *label,
                                   char *value, uint32_t capacity);
int leonos_ui_show_open_dialog(const char *title, char *path, uint32_t capacity,
                               const char *filter_label, const char *filter_ext);
int leonos_ui_show_open_dialog_with_options(const char *title, char *path,
                                            uint32_t capacity,
                                            const char *filter_label,
                                            const char *filter_ext,
                                            const struct leonos_ui_file_dialog_options *options);
int leonos_ui_show_open_with_dialog(const char *title, const char *path,
                                    char *program_path, uint32_t capacity,
                                    uint32_t *remember, uint32_t flags);
int leonos_ui_show_save_dialog_ex(const char *title, char *value, uint32_t capacity,
                                  const char *filter_label, const char *filter_ext);
int leonos_ui_show_save_dialog_with_options(const char *title, char *value,
                                            uint32_t capacity,
                                            const char *filter_label,
                                            const char *filter_ext,
                                            const struct leonos_ui_file_dialog_options *options);
int leonos_ui_show_save_dialog(const char *title, char *value, uint32_t capacity);
void leonos_ui_combobox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const char *text, uint32_t open, uint32_t flags);
uint32_t leonos_ui_dropdown_height(uint32_t count, uint32_t row_h,
                                   uint32_t progress);
void leonos_ui_dropdown(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, const struct leonos_ui_dropdown_item *items,
                        uint32_t count, uint32_t selected_id, uint32_t row_h,
                        uint32_t progress);
int leonos_ui_dropdown_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_dropdown_item *items,
                           uint32_t count, uint32_t row_h, uint32_t progress,
                           uint32_t *out_id);
void leonos_ui_radio(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     const char *label, int checked, uint32_t flags);
void leonos_ui_groupbox(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, const char *title);
void leonos_ui_tab_state_init(struct leonos_ui_tab_state *state, uint32_t selected_id);
uint32_t leonos_ui_tab_height(void);
void leonos_ui_tab_control(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_tab_item *items,
                           uint32_t count, const struct leonos_ui_tab_state *state);
int leonos_ui_tab_control_handle_mouse(struct leonos_ui_tab_state *state,
                                       int32_t px, int32_t py,
                                       uint32_t x, uint32_t y, uint32_t w,
                                       const struct leonos_ui_tab_item *items,
                                       uint32_t count);
int leonos_ui_tab_control_handle_key(struct leonos_ui_tab_state *state,
                                     uint8_t keycode,
                                     const struct leonos_ui_tab_item *items,
                                     uint32_t count);
void leonos_ui_tab_body(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h);
void leonos_ui_color_input_state_init(struct leonos_ui_color_input_state *state,
                                      uint32_t color);
void leonos_ui_color_input(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                           uint32_t w, const struct leonos_ui_color_input_state *state,
                           uint32_t flags);
int leonos_ui_color_input_handle_mouse(struct leonos_ui_color_input_state *state,
                                       int32_t px, int32_t py,
                                       uint32_t x, uint32_t y, uint32_t w,
                                       uint32_t flags);
int leonos_ui_color_input_handle_key(struct leonos_ui_color_input_state *state,
                                     uint8_t keycode, uint32_t flags);
void leonos_ui_date_input_state_init(struct leonos_ui_date_input_state *state,
                                     uint16_t year, uint8_t month, uint8_t day);
void leonos_ui_date_input(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const struct leonos_ui_date_input_state *state,
                          uint32_t flags);
int leonos_ui_date_input_handle_mouse(struct leonos_ui_date_input_state *state,
                                      int32_t px, int32_t py,
                                      uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t flags);
int leonos_ui_date_input_handle_key(struct leonos_ui_date_input_state *state,
                                    uint8_t keycode, uint32_t flags);
void leonos_ui_statusbar(struct leonos_ui_surface *surface, uint32_t y, uint32_t h,
                         const char *text);
void leonos_ui_toolbar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h);
void leonos_ui_toolbar_button(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                              uint32_t w, const char *label, uint32_t flags);
void leonos_ui_splitter(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t vertical);
void leonos_ui_menubar(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                       uint32_t w);
void leonos_ui_menubar_item(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const char *label, uint32_t active);
void leonos_ui_menubar_draw(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                            uint32_t w, const struct leonos_ui_menubar_item *items,
                            uint32_t count, uint32_t active_id);
int leonos_ui_menubar_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                          const struct leonos_ui_menubar_item *items,
                          uint32_t count, uint32_t *out_id);
int leonos_ui_menubar_item_rect(uint32_t x, uint32_t y,
                                const struct leonos_ui_menubar_item *items,
                                uint32_t count, uint32_t id,
                                struct leonos_ui_rect *out_rect);
uint32_t leonos_ui_menu_popup_height(uint32_t count);
void leonos_ui_menu_popup(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                          uint32_t w, const struct leonos_ui_context_menu_item *items,
                          uint32_t count, uint32_t selected_id);
int leonos_ui_menu_popup_hit(int32_t px, int32_t py, uint32_t x, uint32_t y,
                             uint32_t w, const struct leonos_ui_context_menu_item *items,
                             uint32_t count, uint32_t *out_id);
void leonos_ui_property_grid(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                             uint32_t w, const struct leonos_ui_property_item *items,
                             uint32_t count, uint32_t label_w, uint32_t row_h);
void leonos_ui_split_pane_init(struct leonos_ui_split_pane_state *state,
                               uint32_t vertical, uint32_t split,
                               uint32_t min_first, uint32_t min_second);
void leonos_ui_split_pane_layout(struct leonos_ui_split_pane_state *state,
                                 uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void leonos_ui_split_pane_draw(struct leonos_ui_surface *surface,
                               const struct leonos_ui_split_pane_state *state);
int leonos_ui_split_pane_handle_mouse(struct leonos_ui_split_pane_state *state,
                                      int32_t px, int32_t py, uint32_t buttons);
void leonos_ui_slider(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t value, uint32_t max,
                      uint32_t flags);
int leonos_ui_slider_handle_mouse(uint32_t *value, uint32_t max,
                                  uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                  int32_t px, int32_t py);
void leonos_ui_stepper(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                       uint32_t w, uint32_t h, int32_t value, int32_t min,
                       int32_t max, uint32_t flags);
int leonos_ui_stepper_handle_mouse(int32_t *value, int32_t min, int32_t max,
                                   int32_t step, uint32_t x, uint32_t y,
                                   uint32_t w, uint32_t h, int32_t px, int32_t py);
void leonos_ui_toast_show(struct leonos_ui_toast_state *state, const char *message,
                          unsigned long now, uint32_t duration_ms, uint32_t kind);
void leonos_ui_toast_clear(struct leonos_ui_toast_state *state);
int leonos_ui_toast_active(struct leonos_ui_toast_state *state, unsigned long now);
void leonos_ui_toast_draw(struct leonos_ui_surface *surface,
                          struct leonos_ui_toast_state *state,
                          unsigned long now);

#endif

