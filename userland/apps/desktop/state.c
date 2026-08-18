#include "desktop.h"

struct leonos_fb_info fb;
struct leonos_fb_capabilities fb_caps;
uint32_t desktop_scale = 1;
uint32_t desktop_logical_w = MAX_FB_W;
uint32_t desktop_logical_h = MAX_FB_H;
uint8_t desktop_mode_index;
uint8_t desktop_scale_index;
uint8_t desktop_pending_confirm;
uint8_t desktop_previous_mode_index;
uint8_t desktop_previous_scale_index;
unsigned long desktop_confirm_deadline_ms;
struct desktop_window windows[MAX_WINDOWS];
uint8_t z_order[MAX_WINDOWS];
int active_window;
int drag_window;
uint8_t drag_mode;
int drag_dx;
int drag_dy;
int drag_origin_x;
int drag_origin_y;
uint32_t drag_origin_w;
uint32_t drag_origin_h;
uint8_t previous_buttons;
uint8_t start_menu_open;
uint8_t start_menu_animating;
uint8_t start_menu_opening;
uint8_t start_menu_view;
uint32_t start_menu_scroll;
uint32_t start_menu_selected;
char start_menu_query[START_MENU_QUERY_MAX];
unsigned long start_menu_anim_start;
uint8_t start_menu_apps_loaded;
uint8_t start_menu_docs_loaded;
unsigned long start_menu_apps_retry_ms;
unsigned long start_menu_docs_retry_ms;
uint32_t start_menu_app_count;
uint32_t start_menu_doc_count;
char start_menu_app_labels[START_MENU_MAX_APPS][32];
char start_menu_app_paths[START_MENU_MAX_APPS][LEONOS_FS_PATH_LEN];
char start_menu_doc_labels[START_MENU_MAX_DOCS][48];
char start_menu_doc_paths[START_MENU_MAX_DOCS][LEONOS_FS_PATH_LEN];
struct desktop_item desktop_items[DESKTOP_ITEM_MAX];
uint32_t desktop_item_count;
uint8_t desktop_items_ready;
unsigned long desktop_items_retry_ms;
char desktop_folder_path[LEONOS_FS_PATH_LEN];
int32_t desktop_selected_item = -1;
int32_t desktop_last_click_item = -1;
unsigned long desktop_last_click_ms;
uint8_t desktop_context_menu_active;
uint8_t desktop_context_menu_animating;
uint8_t desktop_context_menu_opening;
unsigned long desktop_context_menu_anim_start;
uint32_t desktop_context_menu_x;
uint32_t desktop_context_menu_y;
uint8_t desktop_message_active;
char desktop_message_title[DESKTOP_MESSAGE_TITLE_LEN];
char desktop_message_text[DESKTOP_MESSAGE_TEXT_LEN];
uint8_t desktop_shortcut_input_active;
uint8_t desktop_shortcut_shift_down;
char desktop_shortcut_target[LEONOS_FS_PATH_LEN];
uint8_t snap_preview_mode;
uint8_t alt_left_down;
uint8_t alt_right_down;
uint8_t desktop_left_shift_down;
uint8_t desktop_right_shift_down;
uint8_t win_left_down;
uint8_t win_right_down;
uint8_t win_combo_used;
unsigned long win_down_ms;
uint8_t alt_tab_active;
uint8_t alt_tab_count;
uint8_t alt_tab_selected;
uint8_t alt_tab_ids[ALT_TAB_MAX_WINDOWS];
uint32_t cursor_x;
uint32_t cursor_y;
uint32_t cursor_width = FALLBACK_CURSOR_W;
uint32_t cursor_height = FALLBACK_CURSOR_H;
uint32_t cursor_pixels[CURSOR_MAX_W * CURSOR_MAX_H];
uint8_t cursor_visible;
uint8_t cursor_bitmap_loaded;
uint32_t desktop_cursor_style = LEONOS_GUI_CURSOR_ARROW;
uint8_t desktop_taskbar_visible = 1;
uint32_t wallpaper_pixels[WALLPAPER_MAX_W * WALLPAPER_MAX_H];
uint32_t wallpaper_width;
uint32_t wallpaper_height;
uint8_t wallpaper_loaded;
unsigned long wallpaper_retry_ms;
uint8_t desktop_boot_theme_default = LEONOS_UI_THEME_METRO;
uint8_t desktop_metro_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
uint8_t desktop_win95_color_scheme = LEONOS_UI_COLOR_SCHEME_BLUE;
uint8_t desktop_wallpaper_mode = LEONOS_WALLPAPER_MODE_FILL;
char desktop_wallpaper_path[LEONOS_FS_PATH_LEN] = DESKTOP_DEFAULT_WALLPAPER_PATH;
uint8_t desktop_service_network_icon = 1;
uint8_t desktop_service_rtc_clock = 1;
uint8_t desktop_service_daemon_started;
unsigned long desktop_service_daemon_last_spawn_ms;
uint8_t full_redraw_pending;
uint8_t power_confirm_action;
uint8_t oobe_lock_active;
unsigned long oobe_last_spawn_ms;
uint8_t login_lock_active;
unsigned long login_last_spawn_ms;
uint8_t desktop_startup_launched;
char app_titles[MAX_WINDOWS][48];
char app_texts[MAX_WINDOWS][DESKTOP_APP_TEXT_LEN];
struct leonos_task_info task_infos[LEONOS_TASK_MAX];
uint32_t task_info_count;
uint64_t task_info_tick;
unsigned long last_task_refresh;
struct desktop_inputm_entry desktop_inputm_entries[LEONOS_INPUTM_MAX_PROVIDERS + 1U];
uint32_t desktop_inputm_entry_count;
struct leonos_inputm_state desktop_inputm_state;
uint8_t desktop_inputm_menu_open;
char desktop_inputm_status[96];
struct leonos_ui_surface ui;
uint32_t *app_client_scratch;
uint32_t desktop_client_stride;
uint32_t desktop_client_height;
uint32_t desktop_surface_stride;
uint32_t *screen;

const char cursor_art[FALLBACK_CURSOR_H][FALLBACK_CURSOR_W + 1] = {
    "X...............",
    "XO..............",
    "XOX.............",
    "XOOX............",
    "XOOOX...........",
    "XOOOOX..........",
    "XOOOOOX.........",
    "XOOOOOOX........",
    "XOOOOOOOX.......",
    "XOOOOOOOOX......",
    "XOOOOOOOOOX.....",
    "XOOOOX..........",
    "XOOOOX..........",
    "XOOOOX..........",
    "XOOOXX..........",
    "XXXXX...........",
};

const struct desktop_display_mode desktop_display_modes[DESKTOP_MODE_COUNT] = {
    {1920, 1080, "1920 x 1080"},
    {1600, 900, "1600 x 900"},
    {1280, 800, "1280 x 800"},
    {1280, 720, "1280 x 720"},
    {1024, 768, "1024 x 768"},
    {3840, 2160, "3840 x 2160"},
    {4096, 2160, "4096 x 2160"},
};

const uint32_t desktop_scale_options[DESKTOP_SCALE_COUNT] = {1, 2, 3};
