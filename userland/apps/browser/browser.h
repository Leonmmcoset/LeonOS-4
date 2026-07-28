#ifndef LEONOS_BROWSER_APP_H
#define LEONOS_BROWSER_APP_H

#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/auth.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <leonos/ui.h>

#include "litehtml_core.h"

#define BROWSER_INITIAL_W 860U
#define BROWSER_INITIAL_H 600U
#define BROWSER_MIN_W 560U
#define BROWSER_MIN_H 360U
#define BROWSER_MAX_W 1180U
#define BROWSER_MAX_H 760U
#define BROWSER_URL_CAP LEONOS_FS_PATH_LEN
#define BROWSER_SOURCE_CAP 8192U
#define BROWSER_STATUS_CAP 192U
#define BROWSER_TITLE_CAP 72U
#define BROWSER_MAX_LINES 512U
#define BROWSER_MAX_LINKS 96U
#define BROWSER_HISTORY_MAX 16U
#define BROWSER_MAX_FORMS 8U
#define BROWSER_MAX_FORM_CONTROLS 32U
#define BROWSER_MAX_COOKIES 32U
#define BROWSER_MAX_BOOKMARKS 16U
#define BROWSER_MAX_AUTH 8U
#define BROWSER_MAX_FORM_OPTIONS 64U
#define BROWSER_FORM_NAME_CAP 48U
#define BROWSER_FORM_VALUE_CAP 128U
#define BROWSER_FORM_LABEL_CAP 72U
#define BROWSER_FORM_METHOD_CAP 8U
#define BROWSER_FORM_SELECT_CELLS 24U
#define BROWSER_FORM_TEXTAREA_CELLS 32U
#define BROWSER_COOKIE_NAME_CAP 48U
#define BROWSER_COOKIE_VALUE_CAP 128U
#define BROWSER_COOKIE_DOMAIN_CAP 80U
#define BROWSER_COOKIE_PATH_CAP 96U
#define BROWSER_COOKIE_HEADER_CAP 768U
#define BROWSER_COOKIE_FILE_CAP 8192U
#define BROWSER_BOOKMARK_TITLE_CAP 64U
#define BROWSER_FIND_CAP 64U
#define BROWSER_FORM_INPUT_CELLS 22U
#define BROWSER_FORM_WIDGET_H LEONOS_UI_BUTTON_H
#define BROWSER_MENU_H 26U
#define BROWSER_TOOLBAR_H 30U
#define BROWSER_ADDR_H 34U
#define BROWSER_MENU_ITEM_H (LEONOS_FONT_H + 8U)
#define BROWSER_MENU_ROW_STEP 26U
#define BROWSER_MENU_FILE_X 8U
#define BROWSER_MENU_FILE_W 52U
#define BROWSER_MENU_EDIT_X 64U
#define BROWSER_MENU_EDIT_W 52U
#define BROWSER_MENU_VIEW_X 120U
#define BROWSER_MENU_VIEW_W 56U
#define BROWSER_MENU_FAVORITES_X 182U
#define BROWSER_MENU_FAVORITES_W 92U
#define BROWSER_MENU_HELP_X 280U
#define BROWSER_MENU_HELP_W 52U
#define BROWSER_PAGE_X 8U
#define BROWSER_LINE_H (LEONOS_FONT_H + 2U)
#define BROWSER_SCROLL_W 18U
#define BROWSER_NAV_GAP 4U
#define BROWSER_BACK_X 8U
#define BROWSER_BACK_W 64U
#define BROWSER_FORWARD_W 82U
#define BROWSER_REFRESH_W 76U
#define BROWSER_HOME_W 62U
#define BROWSER_STOP_W 58U
#define BROWSER_GO_W 54U
#define BROWSER_FONT_PATH "0:/system/fonts/times-new-roman.ttf"
#define BROWSER_FONT_FALLBACK_PATH "0:/system/fonts/simsun.ttc"
#define BROWSER_LINK_BLUE 0x000000eeU
#define BROWSER_TEXT_DARK 0x00202020U
#define BROWSER_IE_NAVY 0x00000080U
#define BROWSER_IE_SKY 0x00d8e8f8U
#define BROWSER_QUOTE_BG 0x00f5f5f5U
#define BROWSER_TABLE_BG 0x00f7fbffU
#define BROWSER_TABLE_BORDER 0x00a8b8c8U
#define BROWSER_CODE_BG 0x00eeeeeeU
#define BROWSER_IMAGE_BG 0x00f0f4f8U
#define T(en, zh) leonos_i18n((en), (zh))

enum browser_menu {
    BROWSER_MENU_NONE = 0,
    BROWSER_MENU_FILE = 1,
    BROWSER_MENU_EDIT = 2,
    BROWSER_MENU_VIEW = 3,
    BROWSER_MENU_FAVORITES = 4,
    BROWSER_MENU_HELP = 5,
};

enum browser_menu_command {
    BROWSER_CMD_HOME = 101,
    BROWSER_CMD_REFRESH = 102,
    BROWSER_CMD_CLOSE = 103,
    BROWSER_CMD_SELECT_ADDRESS = 201,
    BROWSER_CMD_CLEAR_ADDRESS = 202,
    BROWSER_CMD_FIND = 203,
    BROWSER_CMD_FIND_NEXT = 204,
    BROWSER_CMD_TOP = 301,
    BROWSER_CMD_BOTTOM = 302,
    BROWSER_CMD_FAV_HOME = 401,
    BROWSER_CMD_FAV_EXAMPLE = 402,
    BROWSER_CMD_FAV_ADD = 403,
    BROWSER_CMD_FAV_MANAGE = 404,
    BROWSER_CMD_FAV_BOOKMARK_BASE = 420,
    BROWSER_CMD_DOWNLOAD = 450,
    BROWSER_CMD_ABOUT = 501,
};

struct parsed_http_url {
    char host[LEONOS_NET_HOSTNAME_LEN];
    char path[LEONOS_NET_HTTP_PATH_LEN];
    uint32_t port;
    uint8_t secure;
};

enum browser_form_control_kind {
    BROWSER_FORM_CONTROL_TEXT = 1,
    BROWSER_FORM_CONTROL_PASSWORD = 2,
    BROWSER_FORM_CONTROL_HIDDEN = 3,
    BROWSER_FORM_CONTROL_SUBMIT = 4,
    BROWSER_FORM_CONTROL_CHECKBOX = 5,
    BROWSER_FORM_CONTROL_RADIO = 6,
    BROWSER_FORM_CONTROL_SELECT = 7,
    BROWSER_FORM_CONTROL_TEXTAREA = 8,
    BROWSER_FORM_CONTROL_RESET = 9,
};

enum browser_form_control_flags {
    BROWSER_FORM_CONTROL_CHECKED = 0x00000001U,
    BROWSER_FORM_CONTROL_DISABLED = 0x00000002U,
    BROWSER_FORM_CONTROL_READONLY = 0x00000004U,
    BROWSER_FORM_CONTROL_PLACEHOLDER = 0x00000008U,
};

struct browser_form {
    char action[BROWSER_URL_CAP];
    char method[BROWSER_FORM_METHOD_CAP];
    uint32_t first_control;
    uint32_t control_count;
};

struct browser_form_control {
    uint8_t kind;
    uint8_t form_index;
    uint16_t reserved;
    uint32_t flags;
    uint32_t first_option;
    uint32_t option_count;
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
};

struct browser_form_option {
    uint32_t control_index;
    char value[BROWSER_FORM_VALUE_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
};

struct browser_cookie {
    char domain[BROWSER_COOKIE_DOMAIN_CAP];
    char path[BROWSER_COOKIE_PATH_CAP];
    char name[BROWSER_COOKIE_NAME_CAP];
    char value[BROWSER_COOKIE_VALUE_CAP];
    uint32_t flags;
    uint64_t expires_unix;
};

struct browser_bookmark {
    char title[BROWSER_BOOKMARK_TITLE_CAP];
    char url[BROWSER_URL_CAP];
};

struct browser_basic_auth {
    char host[LEONOS_NET_HOSTNAME_LEN];
    uint32_t port;
    char header[256];
};

extern uint32_t pixels[BROWSER_MAX_W * BROWSER_MAX_H];
extern struct leonos_ui_surface ui;
extern int window_id;
extern uint32_t view_w;
extern uint32_t view_h;
extern char address_input[BROWSER_URL_CAP];
extern struct leonos_ui_edit_state address_edit;
extern char status_text[BROWSER_STATUS_CAP];
extern char page_title[BROWSER_TITLE_CAP];
extern char current_location[BROWSER_URL_CAP];
extern char page_source[BROWSER_SOURCE_CAP];
extern uint8_t page_is_html;
extern uint8_t source_truncated;
extern struct browser_line lines[BROWSER_MAX_LINES];
extern uint32_t line_count;
extern uint32_t scroll_line;
extern uint32_t scroll_x;
extern struct browser_link links[BROWSER_MAX_LINKS];
extern uint32_t link_count;
extern struct browser_form browser_forms[BROWSER_MAX_FORMS];
extern uint32_t browser_form_count;
extern struct browser_form_control browser_form_controls[BROWSER_MAX_FORM_CONTROLS];
extern uint32_t browser_form_control_count;
extern struct browser_form_option browser_form_options[BROWSER_MAX_FORM_OPTIONS];
extern uint32_t browser_form_option_count;
extern char history[BROWSER_HISTORY_MAX][BROWSER_URL_CAP];
extern uint32_t history_count;
extern int32_t history_index;
extern uint8_t menu_open;
extern uint8_t browser_should_exit;
extern uint8_t browser_embedded;
extern uint8_t browser_form_focus_active;
extern uint32_t browser_form_focus_control;
extern struct leonos_ui_edit_state browser_form_edit_state;
extern struct leonos_ui_toast_state browser_toast;
extern struct browser_bookmark browser_bookmarks[BROWSER_MAX_BOOKMARKS];
extern uint32_t browser_bookmark_count;
extern char browser_find_query[BROWSER_FIND_CAP];
extern int32_t browser_find_row;
extern uint32_t browser_find_start;
extern uint32_t browser_find_len;

void copy_text(char *dst, uint32_t cap, const char *src);
char ascii_tolower(char ch);
int text_eq(const char *a, const char *b);
int text_eq_ignore_case(const char *a, const char *b);
int starts_with_ignore_case(const char *text, const char *prefix);
int ends_with_ignore_case(const char *text, const char *suffix);
int is_space_char(char ch);
int is_digit(char ch);
void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch);
void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src);
void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value);
void append_i32(char *dst, uint32_t *pos, uint32_t cap, int32_t value);
void trim_copy(char *dst, uint32_t cap, const char *src);
uint32_t page_y(void);
uint32_t page_w(void);
uint32_t page_h(void);
uint32_t text_x(void);
uint32_t text_y(void);
uint32_t text_cols(void);
uint32_t visible_rows(void);
void clamp_scroll(void);
void set_status(const char *text);
const char *net_status_name(uint32_t status);
int parse_http_url(const char *url, struct parsed_http_url *out);
void build_http_url(char *dst, uint32_t cap, const char *host, uint32_t port,
                    uint8_t secure, const char *path);
int is_drive_path(const char *text);
void normalize_location(const char *input, char *out, uint32_t cap);
void render_html_source(const char *source, const char *base_url);
void render_plain_source(const char *source);
void rerender_page(void);
void set_page_source(const char *title, const char *source, uint8_t is_html, const char *status);
void render_message_page(const char *title, const char *message, const char *detail);
void push_history(const char *url);
void format_ret_status(char *dst, uint32_t cap, const char *prefix, int32_t ret);
void load_about(void);
void load_http_url(const char *url);
void load_http_form_post(const char *url, const char *body);
int browser_http_get_with_cookies(const char *url, uint32_t timeout_ms,
                                  char *response_body,
                                  uint32_t response_body_capacity,
                                  char *response_headers,
                                  uint32_t response_headers_capacity,
                                  struct leonos_http_response *response);
int browser_http_post_with_cookies(const char *url, const char *body,
                                   char *response_body,
                                   uint32_t response_body_capacity,
                                   char *response_headers,
                                   uint32_t response_headers_capacity,
                                   struct leonos_http_response *response);
void load_local_file(const char *path);
void navigate_to(const char *input, uint8_t add_to_history);
void browser_start_download(const char *url);
void go_back(void);
void go_forward(void);
uint32_t button_y(void);
uint32_t address_y(void);
uint32_t address_w(void);
uint32_t go_x(void);
uint32_t toolbar_forward_x(void);
uint32_t toolbar_refresh_x(void);
uint32_t toolbar_home_x(void);
uint32_t toolbar_stop_x(void);
uint32_t toolbar_title_x(void);
uint32_t menu_row_y(uint32_t row);
int hit_rect_i(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void draw_toolbar_button(uint32_t x, uint32_t w, const char *label, uint32_t flags);
void draw_line_run(uint32_t x, uint32_t y, const char *text,
                   uint32_t len, uint32_t fg, uint32_t bg,
                   uint8_t underline, uint8_t bold,
                   uint8_t italic, uint8_t code,
                   uint32_t cell_w, uint32_t cell_h,
                   uint32_t cell_count);
uint8_t line_is_heading(uint8_t kind);
uint32_t browser_line_cell_w(uint8_t kind);
uint32_t browser_line_cell_h(uint8_t kind);
uint32_t browser_line_height(uint8_t kind);
uint32_t browser_line_render_height(const struct browser_line *line);
uint32_t browser_line_cells_between(const struct browser_line *line,
                                    uint32_t start, uint32_t end);
uint32_t browser_line_next_byte(const struct browser_line *line,
                                uint32_t pos);
uint32_t browser_line_byte_at_cell(const struct browser_line *line,
                                   uint32_t cell);
uint32_t document_text_w(void);
uint32_t document_view_h(void);
uint32_t document_content_w(void);
void draw_document_line_frame(const struct browser_line *line, int32_t x, uint32_t y, uint32_t width);
uint32_t line_align_shift_px(const struct browser_line *line, uint32_t doc_w);
void draw_document_lines(void);
void draw_browser_menu(void);
void draw_browser(void);
void present_browser(void);
void browser_embed_init(uint32_t width, uint32_t height, const char *initial_url);
void browser_embed_resize(uint32_t width, uint32_t height);
void browser_embed_draw(struct leonos_ui_surface *surface);
void browser_embed_handle_mouse_button(struct leonos_gui_app_event *event);
void browser_embed_handle_mouse_wheel(struct leonos_gui_app_event *event);
void browser_embed_handle_key(struct leonos_gui_app_event *event);
int browser_embed_should_exit(void);
void browser_embed_clear_exit(void);
int browser_embed_input_active(void);
int activate_link_at(int32_t mx, int32_t my);
void browser_forms_clear(void);
const char *browser_forms_render_inline_source(const char *source,
                                               const char *base_url);
void browser_form_clear_focus(void);
void browser_form_rebind_focus(void);
int browser_form_input_active(void);
int browser_form_line_has_control(const struct browser_line *line);
int browser_form_control_from_href(const char *href, uint32_t *control_index);
void browser_form_control_rect(uint32_t control_index,
                               struct leonos_ui_rect *rect);
void browser_draw_form_control(uint32_t x, uint32_t y, uint32_t w,
                               uint32_t control_index);
int browser_form_handle_click(const char *href, int32_t mx, int32_t my);
int browser_form_handle_key(struct leonos_gui_app_event *event);
int handle_toolbar_click(int32_t x, int32_t y);
int address_edit_hit(int32_t x, int32_t y);
void select_address_text(void);
int handle_menu_click(int32_t x, int32_t y);
void handle_mouse_button(struct leonos_gui_app_event *event);
void handle_key(struct leonos_gui_app_event *event);
void browser_bookmarks_load(void);
void browser_bookmarks_add_current(void);
int browser_show_bookmark_manager(char *out_url, uint32_t out_cap);
void browser_bookmarks_build_menu(struct leonos_ui_context_menu_item *items,
                                  uint32_t capacity, uint32_t *out_count);
int browser_bookmarks_handle_command(uint32_t command, char *out_url,
                                     uint32_t out_cap);
void browser_auth_append_header(const char *url, char *dst, uint32_t *pos,
                                uint32_t cap);
int browser_auth_retry_from_challenge(const char *url, const char *headers);
void browser_find_prompt(void);
void browser_find_next(void);
int browser_find_match_boundary(uint32_t row, uint32_t offset);

#endif
