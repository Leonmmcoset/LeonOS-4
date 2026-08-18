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

#include "browser_litehtml.h"

/* Browser document state is owned by the LiteHTML wrapper. */
#define BROWSER_INITIAL_W 860U
#define BROWSER_INITIAL_H 600U
#define BROWSER_MIN_W 560U
#define BROWSER_MIN_H 360U
#define BROWSER_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define BROWSER_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define BROWSER_URL_CAP LEONOS_FS_PATH_LEN
/* Keep the browser's document buffer independent from the small HTTP helper
 * default.  LiteHTML needs the complete source to build a usable DOM. */
#define BROWSER_SOURCE_CAP (128U * 1024U)
#define BROWSER_FORM_BODY_CAP (16U * 1024U)
#define BROWSER_STATUS_CAP 192U
#define BROWSER_TITLE_CAP 72U
#define BROWSER_HISTORY_MAX 16U
#define BROWSER_MAX_COOKIES 32U
#define BROWSER_MAX_BOOKMARKS 16U
#define BROWSER_MAX_AUTH 8U
#define BROWSER_COOKIE_NAME_CAP 48U
#define BROWSER_COOKIE_VALUE_CAP 128U
#define BROWSER_COOKIE_DOMAIN_CAP 80U
#define BROWSER_COOKIE_PATH_CAP 96U
#define BROWSER_COOKIE_HEADER_CAP 768U
#define BROWSER_COOKIE_FILE_CAP 8192U
#define BROWSER_BOOKMARK_TITLE_CAP 64U
#define BROWSER_FIND_CAP 64U
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
#define BROWSER_SCROLL_W 18U
#define BROWSER_NAV_GAP 4U
#define BROWSER_BACK_X 8U
#define BROWSER_BACK_W 64U
#define BROWSER_FORWARD_W 82U
#define BROWSER_REFRESH_W 76U
#define BROWSER_HOME_W 62U
#define BROWSER_STOP_W 58U
#define BROWSER_GO_W 54U
#define BROWSER_DEVTOOLS_MIN_H 118U
#define BROWSER_DEVTOOLS_MAX_H 142U
#define BROWSER_FONT_PATH "0:/system/fonts/times-new-roman.ttf"
#define BROWSER_FONT_FALLBACK_PATH "0:/system/fonts/simsun.ttc"
#define BROWSER_USER_AGENT                                                   \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "        \
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
#define BROWSER_ACCEPT_HEADER                                                \
    "text/html,application/xhtml+xml,application/xml;q=0.9,"               \
    "image/webp,image/apng,*/*;q=0.8"
#define BROWSER_ACCEPT_LANGUAGE "zh-CN,zh;q=0.9,en;q=0.8"
#define BROWSER_IE_SKY 0x00d8e8f8U
#define BROWSER_TEXT_DARK 0x00202020U
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

/* The render surface follows the active window dimensions.  A fixed 4K
 * framebuffer in .bss makes a PIE unnecessarily large and can exhaust the
 * loader's ASLR interval when the browser is embedded by OOBE. */
extern uint32_t *pixels;
extern uint32_t pixel_stride;
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
extern uint32_t browser_scroll_y;
extern uint32_t scroll_x;
extern uint32_t browser_form_count;
extern uint32_t browser_form_control_count;
extern char history[BROWSER_HISTORY_MAX][BROWSER_URL_CAP];
extern uint32_t history_count;
extern int32_t history_index;
extern uint8_t menu_open;
extern uint8_t browser_should_exit;
extern uint8_t browser_embedded;
extern struct leonos_ui_toast_state browser_toast;
extern struct browser_bookmark browser_bookmarks[BROWSER_MAX_BOOKMARKS];
extern uint32_t browser_bookmark_count;
extern char browser_find_query[BROWSER_FIND_CAP];
extern int32_t browser_find_row;
extern uint32_t browser_find_start;
extern uint32_t browser_find_len;
extern uint8_t browser_devtools_open;
extern struct browser_litehtml_document *browser_document;
extern uint32_t browser_document_width;
extern uint32_t browser_document_height;
extern uint8_t browser_pending_form;
extern char browser_pending_form_url[BROWSER_URL_CAP];
extern char browser_pending_form_method[12];
extern char browser_pending_form_body[BROWSER_FORM_BODY_CAP];

int browser_resize_surface(uint32_t width, uint32_t height);
void browser_release_surface(void);

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
uint32_t browser_devtools_height(void);
uint32_t text_x(void);
uint32_t text_y(void);
void clamp_scroll(void);
void browser_scroll_wheel(int32_t delta);
void set_status(const char *text);
const char *net_status_name(uint32_t status);
int parse_http_url(const char *url, struct parsed_http_url *out);
void build_http_url(char *dst, uint32_t cap, const char *host, uint32_t port,
                    uint8_t secure, const char *path);
int is_drive_path(const char *text);
void normalize_location(const char *input, char *out, uint32_t cap);
void render_html_source(const char *source, const char *base_url);
void render_plain_source(const char *source);
int browser_litehtml_fetch_resource(void *opaque, const char *url,
                                    uint8_t **data, uint32_t *size,
                                    char *content_type,
                                    uint32_t content_type_cap);
void rerender_page(void);
void set_page_source(const char *title, const char *source, uint8_t is_html, const char *status);
void render_message_page(const char *title, const char *message, const char *detail);
void push_history(const char *url);
void format_ret_status(char *dst, uint32_t cap, const char *prefix, int32_t ret);
void load_about(void);
void load_http_url(const char *url);
void load_http_form_post(const char *url, const char *body);
void browser_process_pending_form(void);
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
uint32_t document_text_w(void);
uint32_t document_view_h(void);
uint32_t document_content_w(void);
uint32_t document_content_h(void);
uint32_t document_scroll_extent(void);
uint32_t document_scroll_viewport(void);
void draw_document_lines(void);
void draw_browser_menu(void);
void draw_browser_devtools(void);
void draw_browser(void);
void present_browser(void);
void handle_mouse_move(struct leonos_gui_app_event *event);
void browser_embed_init(uint32_t width, uint32_t height, const char *initial_url);
void browser_embed_resize(uint32_t width, uint32_t height);
void browser_embed_draw(struct leonos_ui_surface *surface);
void browser_embed_handle_mouse_button(struct leonos_gui_app_event *event);
void browser_embed_handle_mouse_wheel(struct leonos_gui_app_event *event);
void browser_embed_handle_key(struct leonos_gui_app_event *event);
void browser_embed_handle_mouse_move(struct leonos_gui_app_event *event);
int browser_embed_should_exit(void);
void browser_embed_clear_exit(void);
int browser_embed_input_active(void);
void browser_form_clear_focus(void);
int browser_form_input_active(void);
int browser_form_handle_key(struct leonos_gui_app_event *event);
int handle_toolbar_click(int32_t x, int32_t y);
int address_edit_hit(int32_t x, int32_t y);
void select_address_text(void);
int handle_menu_click(int32_t x, int32_t y, uint8_t pressed);
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
