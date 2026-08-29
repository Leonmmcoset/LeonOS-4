#ifndef LEONOS_DESKTOP_H
#define LEONOS_DESKTOP_H

#include <leonos/gui.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/i18n.h>
#include <leonos/inputm.h>
#include <leonos/license.h>
#include <leonos/launch.h>
#include <leonos/mouse.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define MAX_WINDOWS 32
#define BUILTIN_WINDOWS 4
#define MAX_FB_W 1920
#define MAX_FB_H 1080
#define DESKTOP_MODE_COUNT 5
#define DESKTOP_SCALE_COUNT 3
#define TASKBAR_H LEONOS_UI_TASKBAR_H
#define TASKBAR_CLOCK_W 92
#define TASKBAR_NET_W 34
#define TASKBAR_INPUTM_W 42
#define TASKBAR_TRAY_W (TASKBAR_CLOCK_W + TASKBAR_NET_W + TASKBAR_INPUTM_W)
#define TITLEBAR_H LEONOS_UI_TITLEBAR_H
#define MIN_W 180
#define MIN_H 96
#define FALLBACK_CURSOR_W 16
#define FALLBACK_CURSOR_H 16
#define CURSOR_TILE_W 32
#define CURSOR_TILE_H 32
#define CURSOR_STYLE_COUNT LEONOS_GUI_CURSOR_STYLE_COUNT
#define CURSOR_MAX_W CURSOR_TILE_W
#define CURSOR_MAX_H (CURSOR_TILE_H * CURSOR_STYLE_COUNT)
#define CURSOR_BMP_MAX_BYTES (CURSOR_MAX_W * CURSOR_MAX_H * 4 + 128)
#define CURSOR_BMP_PATH "/system/resources/mouse.bmp"
#define DESKTOP_CURSOR_REGION_CAP 64
#define WALLPAPER_MAX_W 1280
#define WALLPAPER_MAX_H 720
#define WALLPAPER_BMP_MAX_BYTES (WALLPAPER_MAX_W * WALLPAPER_MAX_H * 4 + 128)
#define DESKTOP_DEFAULT_WALLPAPER_PATH "/system/resources/wallpaper-metro.bmp"
#define WINDOW_BUTTON_ICON_W 16
#define WINDOW_BUTTON_ICON_H 16
#define WINDOW_BUTTON_MINIMIZE_ICON_PATH "/system/resources/window-button-minimize.bmp"
#define WINDOW_BUTTON_MAXIMIZE_ICON_PATH "/system/resources/window-button-maximize.bmp"
#define WINDOW_BUTTON_RESTORE_ICON_PATH "/system/resources/window-button-restore.bmp"
#define WINDOW_BUTTON_CLOSE_ICON_PATH "/system/resources/window-button-close.bmp"
#define APP_ICON_SMALL_W 16
#define APP_ICON_SMALL_H 16
#define APP_ICON_LARGE_W 32
#define APP_ICON_LARGE_H 32
#define APP_ICON_MAX_W APP_ICON_LARGE_W
#define APP_ICON_MAX_H APP_ICON_LARGE_H
#define APP_ICON_W APP_ICON_SMALL_W
#define APP_ICON_H APP_ICON_SMALL_H
#define APP_ICON_BMP_MAX_BYTES (APP_ICON_MAX_W * APP_ICON_MAX_H * 4 + 128)
#define OOBE_DONE_PATH "/system/state/oobe.done"
#define OOBE_APP_PATH "/system/apps/oobe/oobe.elf"
#define OOBE_WINDOW_TITLE "LeonOS Setup"
#define OOBE_WINDOW_TEXT "First-run setup"
#define OOBE_RESPAWN_MS 1000UL
/* A freshly spawned task can take several scheduler ticks before it appears
 * in the task snapshot.  Keep the spawn reservation during that handoff. */
#define OOBE_STARTUP_GRACE_MS 5000UL
#define LOGIN_APP_PATH "/system/apps/login/login.elf"
#define LOGIN_WINDOW_TITLE "LeonOS Login"
#define LOGIN_WINDOW_TEXT "Sign in"
#define LOGIN_RESPAWN_MS 1000UL
/* Login window registration is asynchronous too.  Keep its launch reservation
 * until the task becomes visible to the desktop or has had time to start. */
#define LOGIN_STARTUP_GRACE_MS 5000UL
#define SERVICE_DAEMON_PATH "/system/apps/serviced/serviced.elf"
#define NETWORK_CONTROLLER_APP_PATH "/system/apps/netctl/netctl.elf"
#define SERVICE_DAEMON_RETRY_MS 2000UL
#define DISPLAY_CONFIG_PATH "/system/config/display.conf"
#define APPEARANCE_CONFIG_NAME "appearance.conf"
#define SERVICES_CONFIG_PATH "/system/config/services.cfg"
#define SERVICES_CONFIG_MAX 512U
#define DISPLAY_CONFIRM_MS 10000UL
#define START_MENU_W 464
#define START_MENU_MAX_H 448
#define START_MENU_ANIM_MS 160UL
#define WINDOW_ANIM_MS 190UL
#define START_MENU_ITEM_H 26
#define START_MENU_SEARCH_H 28
#define START_MENU_DIRTY_H (START_MENU_MAX_H + TASKBAR_H + 8)
#define START_MENU_MAX_APPS LEONOS_FS_MAX_ENTRIES
#define START_MENU_MAX_DOCS LEONOS_FS_MAX_ENTRIES
#define START_MENU_QUERY_MAX 48
#define START_MENU_VIEW_HOME 0U
#define START_MENU_VIEW_APPS 1U
#define START_MENU_VIEW_DOCUMENTS 2U
#define START_MENU_VIEW_POWER 3U
#define START_MENU_VIEW_SEARCH 4U
#define APP_WINDOW_SLOTS (MAX_WINDOWS - BUILTIN_WINDOWS)
#define APP_CLIENT_MAX_W 1920
#define APP_CLIENT_MAX_H 1080
#define DESKTOP_APP_TEXT_LEN 1024
#define SNAP_MARGIN 24
#define ALT_TAB_MAX_WINDOWS MAX_WINDOWS
#define ALT_TAB_W 336
#define WIN_TAP_MAX_MS 500UL
#define WINDOW_RECOVERABLE_W 96
#define WINDOW_RECOVERABLE_TITLEBAR_H 8
#define DESKTOP_ITEM_MAX LEONOS_FS_MAX_ENTRIES
#define DESKTOP_ITEM_CELL_W 96
#define DESKTOP_ITEM_CELL_H 96
#define DESKTOP_ITEM_GRID_X 8
#define DESKTOP_ITEM_GRID_Y 24
#define DESKTOP_ITEM_DOUBLE_CLICK_MS 500UL
#define DESKTOP_CONTEXT_MENU_W 204
#define DESKTOP_CONTEXT_MENU_COUNT 3
#define DESKTOP_CONTEXT_ACTION_REFRESH 1
#define DESKTOP_CONTEXT_ACTION_OPEN_FOLDER 2
#define DESKTOP_CONTEXT_ACTION_CREATE_SHORTCUT 3
#define DESKTOP_MESSAGE_TITLE_LEN 48
#define DESKTOP_MESSAGE_TEXT_LEN 160
#define DESKTOP_MESSAGE_W 380
#define DESKTOP_MESSAGE_H 150
#define DESKTOP_SHORTCUT_INPUT_W 480
#define DESKTOP_SHORTCUT_INPUT_H 170
#define DESKTOP_INPUTM_CONFIG_NAME ".inputm.conf"
#define DESKTOP_INPUTM_MENU_W 248U
#define DESKTOP_INPUTM_MENU_ROW_H 25U

#define DRAG_NONE 0
#define DRAG_MOVE 1
#define DRAG_RESIZE 2

#define SNAP_NONE 0
#define SNAP_TOP 1
#define SNAP_LEFT 2
#define SNAP_RIGHT 3

#define WINDOW_ANIM_NONE 0
#define WINDOW_ANIM_OPEN 1
#define WINDOW_ANIM_CLOSE 2
#define WINDOW_ANIM_MINIMIZE 3
#define WINDOW_ANIM_RESTORE 4
#define WINDOW_ANIM_MAXIMIZE 5

#define POWER_CONFIRM_NONE 0
#define POWER_CONFIRM_REBOOT 1
#define POWER_CONFIRM_SHUTDOWN 2

struct desktop_window {
    int x;
    int y;
    uint32_t width;
    uint32_t height;
    int restore_x;
    int restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    const char *title;
    const char *app_text;
    uint32_t body_color;
    uint32_t owner_pid;
    uint32_t window_id;
    uint32_t client_width;
    uint32_t client_height;
    uint32_t flags;
    uint8_t close_requested;
    uint8_t visible;
    uint8_t minimized;
    uint8_t maximized;
    uint8_t snap_mode;
    char icon_path[LEONOS_FS_PATH_LEN];
    uint8_t anim;
    unsigned long anim_start_ms;
    int anim_from_x;
    int anim_from_y;
    uint32_t anim_from_w;
    uint32_t anim_from_h;
    int anim_to_x;
    int anim_to_y;
    uint32_t anim_to_w;
    uint32_t anim_to_h;
    struct desktop_cursor_region {
        uint8_t used;
        uint32_t id;
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
        uint32_t style;
        uint32_t flags;
    } cursor_regions[DESKTOP_CURSOR_REGION_CAP];
};

struct rect {
    int x;
    int y;
    int w;
    int h;
};

struct desktop_display_mode {
    uint32_t width;
    uint32_t height;
    const char *label;
};

struct desktop_item {
    struct leonos_dir_entry entry;
    char label[LEONOS_FS_NAME_LEN + 1];
    char path[LEONOS_FS_PATH_LEN];
    char icon_path[LEONOS_FS_PATH_LEN];
};

struct desktop_inputm_entry {
    char id[LEONOS_INPUTM_ID_LEN];
    char path[LEONOS_FS_PATH_LEN];
    char abbreviation[LEONOS_INPUTM_ABBREV_LEN];
    uint32_t startup_mode;
    uint32_t order;
    uint8_t enabled;
    uint8_t running;
};

extern struct leonos_fb_info fb;
extern struct leonos_fb_capabilities fb_caps;
extern uint32_t desktop_scale;
extern uint32_t desktop_logical_w;
extern uint32_t desktop_logical_h;
extern uint8_t desktop_mode_index;
extern uint8_t desktop_scale_index;
extern uint8_t desktop_pending_confirm;
extern uint8_t desktop_previous_mode_index;
extern uint8_t desktop_previous_scale_index;
extern unsigned long desktop_confirm_deadline_ms;
extern struct desktop_window windows[MAX_WINDOWS];
extern uint8_t z_order[MAX_WINDOWS];
extern int active_window;
extern int drag_window;
extern uint8_t drag_mode;
extern int drag_dx;
extern int drag_dy;
extern int drag_origin_x;
extern int drag_origin_y;
extern uint32_t drag_origin_w;
extern uint32_t drag_origin_h;
extern uint8_t previous_buttons;
extern uint8_t start_menu_open;
extern uint8_t start_menu_animating;
extern uint8_t start_menu_opening;
extern uint8_t start_menu_view;
extern uint32_t start_menu_scroll;
extern uint32_t start_menu_selected;
extern char start_menu_query[START_MENU_QUERY_MAX];
extern unsigned long start_menu_anim_start;
extern uint8_t start_menu_apps_loaded;
extern uint8_t start_menu_docs_loaded;
extern unsigned long start_menu_apps_retry_ms;
extern unsigned long start_menu_docs_retry_ms;
extern uint32_t start_menu_app_count;
extern uint32_t start_menu_doc_count;
extern char start_menu_app_labels[START_MENU_MAX_APPS][32];
extern char start_menu_app_paths[START_MENU_MAX_APPS][LEONOS_FS_PATH_LEN];
extern char start_menu_doc_labels[START_MENU_MAX_DOCS][48];
extern char start_menu_doc_paths[START_MENU_MAX_DOCS][LEONOS_FS_PATH_LEN];
extern struct desktop_item desktop_items[DESKTOP_ITEM_MAX];
extern uint32_t desktop_item_count;
extern uint8_t desktop_items_ready;
extern unsigned long desktop_items_retry_ms;
extern char desktop_folder_path[LEONOS_FS_PATH_LEN];
extern int32_t desktop_selected_item;
extern int32_t desktop_last_click_item;
extern unsigned long desktop_last_click_ms;
extern uint8_t desktop_context_menu_active;
extern uint8_t desktop_context_menu_animating;
extern uint8_t desktop_context_menu_opening;
extern unsigned long desktop_context_menu_anim_start;
extern uint32_t desktop_context_menu_x;
extern uint32_t desktop_context_menu_y;
extern uint8_t desktop_message_active;
extern char desktop_message_title[DESKTOP_MESSAGE_TITLE_LEN];
extern char desktop_message_text[DESKTOP_MESSAGE_TEXT_LEN];
extern uint8_t desktop_shortcut_input_active;
extern uint8_t desktop_shortcut_shift_down;
extern char desktop_shortcut_target[LEONOS_FS_PATH_LEN];
extern uint8_t snap_preview_mode;
extern uint8_t alt_left_down;
extern uint8_t alt_right_down;
extern uint8_t desktop_left_shift_down;
extern uint8_t desktop_right_shift_down;
extern uint8_t win_left_down;
extern uint8_t win_right_down;
extern uint8_t win_combo_used;
extern unsigned long win_down_ms;
extern uint8_t alt_tab_active;
extern uint8_t alt_tab_count;
extern uint8_t alt_tab_selected;
extern uint8_t alt_tab_ids[ALT_TAB_MAX_WINDOWS];
extern uint32_t cursor_x;
extern uint32_t cursor_y;
extern uint32_t cursor_width;
extern uint32_t cursor_height;
extern uint32_t cursor_pixels[CURSOR_MAX_W * CURSOR_MAX_H];
extern uint8_t cursor_hotspot_x[CURSOR_STYLE_COUNT];
extern uint8_t cursor_hotspot_y[CURSOR_STYLE_COUNT];
extern uint8_t cursor_visible;
extern uint8_t cursor_bitmap_loaded;
extern uint32_t desktop_cursor_style;
extern uint8_t desktop_cursor_auto;
extern uint8_t desktop_taskbar_visible;
extern uint32_t wallpaper_pixels[WALLPAPER_MAX_W * WALLPAPER_MAX_H];
extern uint32_t wallpaper_width;
extern uint32_t wallpaper_height;
extern uint8_t wallpaper_loaded;
extern unsigned long wallpaper_retry_ms;
extern uint8_t desktop_boot_theme_default;
extern uint8_t desktop_metro_color_scheme;
extern uint8_t desktop_win95_color_scheme;
extern uint8_t desktop_wallpaper_mode;
extern char desktop_wallpaper_path[LEONOS_FS_PATH_LEN];
extern uint8_t desktop_service_network_icon;
extern uint8_t desktop_service_rtc_clock;
extern uint8_t desktop_service_daemon_started;
extern unsigned long desktop_service_daemon_last_spawn_ms;
extern uint8_t full_redraw_pending;
extern uint8_t desktop_damage_pending;
extern uint8_t desktop_damage_cursor_only;
extern struct rect desktop_damage_rect;
extern uint8_t power_confirm_action;
extern uint8_t oobe_lock_active;
extern unsigned long oobe_last_spawn_ms;
extern uint32_t oobe_spawn_pid;
extern uint8_t login_lock_active;
extern unsigned long login_last_spawn_ms;
extern uint32_t login_spawn_pid;
extern uint8_t desktop_startup_launched;
extern char app_titles[MAX_WINDOWS][48];
extern char app_texts[MAX_WINDOWS][DESKTOP_APP_TEXT_LEN];
extern struct leonos_task_info task_infos[LEONOS_TASK_MAX];
extern uint32_t task_info_count;
extern uint64_t task_info_tick;
extern unsigned long last_task_refresh;
extern struct desktop_inputm_entry desktop_inputm_entries[LEONOS_INPUTM_MAX_PROVIDERS + 1U];
extern uint32_t desktop_inputm_entry_count;
extern struct leonos_inputm_state desktop_inputm_state;
extern uint8_t desktop_inputm_menu_open;
extern char desktop_inputm_status[96];
extern struct leonos_ui_surface ui;
extern uint32_t app_client_scratch[APP_CLIENT_MAX_W * APP_CLIENT_MAX_H];

extern uint32_t screen[MAX_FB_W * MAX_FB_H];

extern const char cursor_art[FALLBACK_CURSOR_H][FALLBACK_CURSOR_W + 1];
extern const struct desktop_display_mode desktop_display_modes[DESKTOP_MODE_COUNT];
extern const uint32_t desktop_scale_options[DESKTOP_SCALE_COUNT];

uint32_t fb_w(void);
uint32_t fb_h(void);
uint32_t desktop_scale_for_framebuffer(uint32_t width, uint32_t height);
uint32_t desktop_scale_fit_for_mode(uint32_t width, uint32_t height);
int desktop_display_mode_supported(uint8_t mode_index, uint8_t scale_index);
int desktop_apply_display_settings(uint8_t mode_index, uint8_t scale_index);
void desktop_apply_display_settings_pending(uint8_t mode_index, uint8_t scale_index);
void desktop_confirm_display_settings(void);
void desktop_revert_display_settings(void);
void desktop_update_display_confirmation(void);
void desktop_load_display_config(void);
int desktop_save_display_config(void);
void desktop_load_appearance_config(void);
int desktop_save_appearance_config(void);
void desktop_reflow_after_display_change(void);
void copy_text(char *dst, uint32_t dst_len, const char *src);
int text_eq(const char *a, const char *b);
uint16_t read_le16(const uint8_t *p);
uint32_t read_le32(const uint8_t *p);
int32_t read_le32s(const uint8_t *p);
int hit_rect(uint32_t x, uint32_t y, int rx, int ry, uint32_t rw, uint32_t rh);
int text_ends_with(const char *text, const char *suffix);
void copy_app_label_from_elf(char *dst, uint32_t dst_len, const char *name);
void desktop_icon_path_for_app(const char *app_path, char *dst, uint32_t dst_len);
uint32_t taskbar_y(void);
uint32_t desktop_tray_width(void);
uint32_t running_window_count(void);
int desktop_load_service_config(void);
void desktop_service_daemon_update(void);
void start_menu_set_open(uint8_t open);
void start_menu_toggle(void);
uint32_t start_menu_progress(void);
uint32_t desktop_ease_percent(uint32_t percent);
int desktop_window_animation_active(void);
void desktop_update_window_animations(void);
uint32_t taskbar_button_width(uint32_t count);
int is_alt_down(void);
int is_win_down(void);
struct rect rect_make(int x, int y, int w, int h);
struct rect window_rect(uint8_t id);
struct rect cursor_rect_at(uint32_t x, uint32_t y);
struct rect cursor_rect_for_style(uint32_t x, uint32_t y, uint32_t style);
struct rect rect_union(struct rect a, struct rect b);
struct rect rect_pad(struct rect r, int pad);
struct rect rect_clip(struct rect r);
int rect_intersects(struct rect a, struct rect b);
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
void put_pixel_i(int x, int y, uint32_t color);
void rect_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void rect_fill_i(int x, int y, int w, int h, uint32_t color);
void bevel_i(int x, int y, int w, int h, uint32_t fill, uint32_t flags);
void text_draw(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void text_draw_i(int x, int y, const char *text, uint32_t fg, uint32_t bg);
void text_draw_transparent_i(int x, int y, const char *text, uint32_t fg);
void draw_app_icon(const char *icon_path, int x, int y);
void draw_app_icon_large(const char *icon_path, int x, int y);
void window_button_i(int x, int y, char label, uint32_t flags);
void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch);
void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text);
void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value);
void append_hex_fixed(char *buf, uint32_t *pos, uint32_t cap, uint64_t value, uint32_t digits);
int load_cursor_bmp(void);
int load_wallpaper_bmp(void);
char lower_ascii(char ch);
int keycode_to_ascii(uint8_t keycode, char *out);
const char *task_state_name(uint32_t state);
const char *task_kind_name(uint32_t kind);
void task_line(char *buf, uint32_t cap, const struct leonos_task_info *task);
void refresh_task_snapshot(void);
uint32_t min_u32(uint32_t a, uint32_t b);
void clamp_window_size(struct desktop_window *w);
void clamp_window_position_recoverable(struct desktop_window *w);
void clamp_window(struct desktop_window *w);
void bring_to_front(uint8_t id);
int find_window_slot_by_window_id(uint32_t window_id);
uint32_t window_body_width(const struct desktop_window *w);
uint32_t window_body_height(const struct desktop_window *w);
int window_is_fullscreen(const struct desktop_window *w);
int window_is_borderless(const struct desktop_window *w);
int active_window_is_fullscreen(void);
int window_allows_resize(const struct desktop_window *w);
int window_is_snap_candidate(const struct desktop_window *w);
uint8_t snap_mode_for_pointer(uint32_t x, uint32_t y, const struct desktop_window *w);
void window_client_origin(const struct desktop_window *w, int *x, int *y);
void remove_window_slot(uint8_t slot);
void begin_window_open_animation(uint8_t slot);
void begin_window_rect_animation(uint8_t slot, uint8_t anim,
                                 int from_x, int from_y, uint32_t from_w, uint32_t from_h,
                                 int to_x, int to_y, uint32_t to_w, uint32_t to_h);
void begin_window_minimize_animation(uint8_t slot);
void begin_window_restore_animation(uint8_t slot);
void begin_window_close_animation(uint8_t slot, uint8_t send_close_event);
void request_close_window(uint8_t slot);
void send_app_event_to_window(uint32_t window_id, uint32_t type,
                              int32_t x, int32_t y, int32_t dx, int32_t dy,
                              uint32_t width, uint32_t height,
                              uint8_t buttons, uint8_t keycode, uint8_t pressed);
void send_app_event(uint8_t slot, uint32_t type, int32_t x, int32_t y,
                    int32_t dx, int32_t dy, uint8_t buttons,
                    uint8_t keycode, uint8_t pressed);
void fetch_window_surface(uint8_t slot);
void draw_app_surface_i(uint8_t id, int body_x, int body_y,
                        uint32_t body_w, uint32_t body_h);
int window_is_ui_demo(const struct desktop_window *w);
void draw_ui_demo_label(uint32_t x, uint32_t y, const char *label, uint32_t bg);
void draw_ui_demo_gallery(uint32_t body_x, uint32_t body_y,
                          uint32_t body_w, uint32_t body_h, uint32_t bg);
void draw_window(uint8_t id);
void draw_taskbar_button(uint8_t id, uint32_t x, uint32_t y, uint32_t w);
void draw_snap_preview(void);
void alt_tab_rebuild(void);
void alt_tab_begin(void);
void alt_tab_advance(void);
void alt_tab_commit(void);
void draw_alt_tab_overlay(void);
int start_menu_is_hidden_app(const char *name);
int start_menu_load_apps(void);
void start_menu_ensure_apps(void);
uint32_t start_menu_filtered_app_count(void);
uint32_t start_menu_filtered_app_index(uint32_t filtered_index);
void start_menu_load_docs(void);
void start_menu_ensure_docs(void);
int start_menu_handle_key(uint8_t keycode, uint8_t pressed);
void start_menu_handle_click(uint32_t x, uint32_t y);
int start_menu_hit_test(uint32_t x, uint32_t y);
uint32_t start_menu_cursor_style(uint32_t x, uint32_t y);
int start_menu_handle_wheel(uint32_t x, uint32_t y, int32_t wheel);
void draw_start_menu(void);
void desktop_items_clear(void);
int desktop_refresh_items(void);
int desktop_items_directory_changed(void);
void draw_desktop_items(struct rect dirty);
void draw_desktop_context_menu(void);
void draw_desktop_message(void);
void draw_desktop_shortcut_input(void);
void desktop_show_message(const char *title, const char *message);
int desktop_handle_message_click(uint32_t x, uint32_t y);
int desktop_handle_message_key(uint8_t keycode, uint8_t pressed);
int desktop_handle_shortcut_input_click(uint32_t x, uint32_t y);
int desktop_handle_shortcut_input_key(uint8_t keycode, uint8_t pressed);
int desktop_handle_context_menu_click(uint32_t x, uint32_t y);
int desktop_handle_background_click(uint32_t x, uint32_t y);
int desktop_handle_background_right_click(uint32_t x, uint32_t y);
void draw_power_confirm(void);
void draw_cursor_shape(uint32_t x, uint32_t y);
uint32_t desktop_cursor_style_for_pointer(uint32_t x, uint32_t y);
void redraw_region(struct rect dirty);
void flush_region(struct rect dirty);
void repaint_and_flush(struct rect dirty);
void repaint_cursor_and_flush(struct rect dirty);
void redraw_all(void);
void desktop_queue_damage(struct rect dirty);
void desktop_queue_cursor_damage(struct rect dirty);
int hit_window(uint32_t x, uint32_t y);
void minimize_window(uint8_t id);
void restore_window(uint8_t id);
int handle_global_key(uint8_t keycode, uint8_t pressed);
void toggle_maximize(uint8_t id);
void apply_snap_mode(uint8_t id, uint8_t snap_mode);
void open_app_window_from_msg(const struct leonos_gui_window_msg *msg);
int spawn_program_path(const char *path);
int spawn_help_path(const char *path);
void maybe_launch_oobe(void);
int oobe_done_marker_exists(void);
int window_is_oobe(const struct desktop_window *w);
int window_msg_is_oobe(const struct leonos_gui_window_msg *msg);
int oobe_window_slot(void);
void oobe_lock_update(void);
void oobe_lock_on_window_removed(uint8_t slot);
int oobe_lock_blocks_window_msg(const struct leonos_gui_window_msg *msg);
int handle_oobe_lock_mouse(uint32_t x, uint32_t y, uint8_t buttons);
int handle_oobe_lock_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons);
void maybe_launch_login(void);
int desktop_session_logged_in(void);
int window_is_login(const struct desktop_window *w);
int window_msg_is_login(const struct leonos_gui_window_msg *msg);
int login_window_slot(void);
void login_lock_update(void);
void login_lock_on_window_removed(uint8_t slot);
int login_lock_blocks_window_msg(const struct leonos_gui_window_msg *msg);
int handle_login_lock_mouse(uint32_t x, uint32_t y, uint8_t buttons);
int handle_login_lock_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons);
void desktop_launch_startup_apps(void);
void desktop_reboot(void);
void desktop_shutdown(void);
void desktop_logout(void);
void desktop_request_power_confirm(uint8_t action);
int desktop_handle_power_confirm_click(uint32_t x, uint32_t y);
void handle_start_click(uint32_t x, uint32_t y);
int hit_start_menu_area(uint32_t x, uint32_t y);
int handle_taskbar_click(uint32_t x, uint32_t y);
void desktop_inputm_load_config(void);
void desktop_inputm_refresh(void);
void desktop_inputm_launch_login_providers(void);
void desktop_inputm_cycle(void);
int desktop_inputm_hotkey_is_alt_shift(void);
int desktop_inputm_handle_click(uint32_t x, uint32_t y);
uint32_t desktop_inputm_cursor_style(uint32_t x, uint32_t y);
void draw_inputm_overlay(void);
void desktop_publish_display_state(void);
void desktop_handle_display_requests(void);
void desktop_publish_appearance_state(void);
void desktop_handle_appearance_requests(void);
void desktop_apply_theme(uint32_t theme);
void desktop_apply_appearance(const struct leonos_appearance_request *request);
void update_snap_preview(uint32_t x, uint32_t y);
void handle_mouse(uint32_t x, uint32_t y, uint8_t buttons);
void handle_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons);
void init_desktop(void);
void desktop_run(void);

#endif
