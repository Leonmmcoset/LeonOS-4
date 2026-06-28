#ifndef LEONOS_DESKTOP_H
#define LEONOS_DESKTOP_H

#include <leonos/gui.h>
#include <leonos/fs.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define MAX_WINDOWS 32
#define BUILTIN_WINDOWS 4
#define MAX_FB_W 1280
#define MAX_FB_H 800
#define TASKBAR_H LEONOS_UI_TASKBAR_H
#define TITLEBAR_H LEONOS_UI_TITLEBAR_H
#define MIN_W 180
#define MIN_H 96
#define FALLBACK_CURSOR_W 16
#define FALLBACK_CURSOR_H 16
#define CURSOR_MAX_W 64
#define CURSOR_MAX_H 64
#define CURSOR_BMP_MAX_BYTES (CURSOR_MAX_W * CURSOR_MAX_H * 4 + 128)
#define CURSOR_BMP_PATH "0:/system/resources/mouse.bmp"
#define OOBE_DONE_PATH "0:/etc/oobe.done"
#define OOBE_APP_PATH "0:/userland/oobe.elf"
#define OOBE_WINDOW_TITLE "LeonOS Setup"
#define START_MENU_W 246
#define START_PROGRAMS_W 220
#define START_MENU_MAX_H 340
#define START_MENU_ANIM_MS 160UL
#define START_MENU_ITEM_H 26
#define START_MENU_DIRTY_H (START_MENU_MAX_H + TASKBAR_H + 8)
#define START_MENU_MAX_APPS LEONOS_FS_MAX_ENTRIES
#define START_MENU_MAX_ITEMS 32
#define APP_WINDOW_SLOTS (MAX_WINDOWS - BUILTIN_WINDOWS)
#define APP_CLIENT_MAX_W 1280
#define APP_CLIENT_MAX_H 800
#define SNAP_MARGIN 24
#define ALT_TAB_MAX_WINDOWS MAX_WINDOWS
#define ALT_TAB_W 336
#define WIN_TAP_MAX_MS 500UL
#define WINDOW_RECOVERABLE_W 96
#define WINDOW_RECOVERABLE_TITLEBAR_H 8

#define DRAG_NONE 0
#define DRAG_MOVE 1
#define DRAG_RESIZE 2

#define SNAP_NONE 0
#define SNAP_TOP 1
#define SNAP_LEFT 2
#define SNAP_RIGHT 3

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
};

struct rect {
    int x;
    int y;
    int w;
    int h;
};

enum start_action_type {
    START_ACTION_RESTORE = 1,
    START_ACTION_SPAWN = 2,
    START_ACTION_SEPARATOR = 3,
    START_ACTION_REBOOT = 4,
    START_ACTION_SHUTDOWN = 5,
    START_ACTION_PROGRAMS = 6,
};

struct start_menu_item {
    const char *label;
    enum start_action_type type;
    uint8_t window_id;
    const char *path;
};

struct start_menu_layout {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t full_h;
    uint32_t visible_h;
    uint32_t visible_start;
};

struct start_programs_layout {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t rows;
    uint32_t cols;
    uint32_t visible_count;
};

extern struct leonos_fb_info fb;
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
extern uint8_t start_menu_programs_open;
extern uint32_t start_menu_programs_scroll;
extern unsigned long start_menu_anim_start;
extern uint8_t start_menu_apps_loaded;
extern uint32_t start_menu_app_count;
extern char start_menu_app_labels[START_MENU_MAX_APPS][32];
extern char start_menu_app_paths[START_MENU_MAX_APPS][LEONOS_FS_PATH_LEN];
extern uint8_t snap_preview_mode;
extern uint8_t alt_left_down;
extern uint8_t alt_right_down;
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
extern uint8_t cursor_visible;
extern uint8_t cursor_bitmap_loaded;
extern uint8_t full_redraw_pending;
extern uint8_t oobe_lock_active;
extern unsigned long oobe_last_spawn_ms;
extern char app_titles[MAX_WINDOWS][48];
extern char app_texts[MAX_WINDOWS][96];
extern struct leonos_task_info task_infos[LEONOS_TASK_MAX];
extern uint32_t task_info_count;
extern uint64_t task_info_tick;
extern unsigned long last_task_refresh;
extern struct leonos_ui_surface ui;
extern uint32_t app_client_scratch[APP_CLIENT_MAX_W * APP_CLIENT_MAX_H];

extern uint32_t screen[MAX_FB_W * MAX_FB_H];

extern const char cursor_art[FALLBACK_CURSOR_H][FALLBACK_CURSOR_W + 1];

uint32_t fb_w(void);
uint32_t fb_h(void);
void copy_text(char *dst, uint32_t dst_len, const char *src);
int text_eq(const char *a, const char *b);
uint16_t read_le16(const uint8_t *p);
uint32_t read_le32(const uint8_t *p);
int32_t read_le32s(const uint8_t *p);
int hit_rect(uint32_t x, uint32_t y, int rx, int ry, uint32_t rw, uint32_t rh);
int text_ends_with(const char *text, const char *suffix);
void copy_app_label_from_elf(char *dst, uint32_t dst_len, const char *name);
uint32_t taskbar_y(void);
uint32_t running_window_count(void);
void start_menu_set_open(uint8_t open);
void start_menu_toggle(void);
uint32_t start_menu_progress(void);
uint32_t taskbar_button_width(uint32_t count);
int is_alt_down(void);
int is_win_down(void);
struct rect rect_make(int x, int y, int w, int h);
struct rect window_rect(uint8_t id);
struct rect cursor_rect_at(uint32_t x, uint32_t y);
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
void window_button_i(int x, int y, char label, uint32_t flags);
void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch);
void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text);
void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value);
void append_hex_fixed(char *buf, uint32_t *pos, uint32_t cap, uint64_t value, uint32_t digits);
int load_cursor_bmp(void);
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
int window_allows_resize(const struct desktop_window *w);
int window_is_snap_candidate(const struct desktop_window *w);
uint8_t snap_mode_for_pointer(uint32_t x, uint32_t y, const struct desktop_window *w);
void remove_window_slot(uint8_t slot);
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
void start_menu_load_apps(void);
void start_menu_ensure_apps(void);
uint32_t build_start_menu_items(struct start_menu_item *items, uint32_t cap);
uint32_t start_menu_height_for_count(uint32_t count);
struct start_menu_layout start_menu_layout_for_count(uint32_t count);
struct start_programs_layout start_programs_layout_for_menu(struct start_menu_layout menu);
void draw_start_programs_menu(struct start_menu_layout menu);
void draw_start_menu(void);
void draw_cursor_shape(uint32_t x, uint32_t y);
void redraw_region(struct rect dirty);
void flush_region(struct rect dirty);
void repaint_and_flush(struct rect dirty);
void redraw_all(void);
int hit_window(uint32_t x, uint32_t y);
void minimize_window(uint8_t id);
void restore_window(uint8_t id);
int handle_global_key(uint8_t keycode, uint8_t pressed);
void toggle_maximize(uint8_t id);
void apply_snap_mode(uint8_t id, uint8_t snap_mode);
void open_app_window_from_msg(const struct leonos_gui_window_msg *msg);
int spawn_program_path(const char *path);
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
void desktop_reboot(void);
void desktop_shutdown(void);
void handle_start_click(uint32_t x, uint32_t y);
int hit_start_menu_area(uint32_t x, uint32_t y);
int handle_taskbar_click(uint32_t x, uint32_t y);
void update_snap_preview(uint32_t x, uint32_t y);
void handle_mouse(uint32_t x, uint32_t y, uint8_t buttons);
void handle_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons);
void init_desktop(void);
void desktop_run(void);

#endif
