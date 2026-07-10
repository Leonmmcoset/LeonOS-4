#include "desktop.h"

struct leonos_fb_info fb;
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
uint8_t start_menu_programs_open;
uint8_t start_menu_docs_open;
uint32_t start_menu_programs_scroll;
uint32_t start_menu_docs_scroll;
unsigned long start_menu_anim_start;
uint8_t start_menu_apps_loaded;
uint8_t start_menu_docs_loaded;
uint32_t start_menu_app_count;
uint32_t start_menu_doc_count;
char start_menu_app_labels[START_MENU_MAX_APPS][32];
char start_menu_app_paths[START_MENU_MAX_APPS][LEONOS_FS_PATH_LEN];
char start_menu_doc_labels[START_MENU_MAX_DOCS][48];
char start_menu_doc_paths[START_MENU_MAX_DOCS][LEONOS_FS_PATH_LEN];
struct desktop_item desktop_items[DESKTOP_ITEM_MAX];
uint32_t desktop_item_count;
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
uint32_t wallpaper_pixels[WALLPAPER_MAX_W * WALLPAPER_MAX_H];
uint32_t wallpaper_width;
uint32_t wallpaper_height;
uint8_t wallpaper_loaded;
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
char app_titles[MAX_WINDOWS][48];
char app_texts[MAX_WINDOWS][DESKTOP_APP_TEXT_LEN];
struct leonos_task_info task_infos[LEONOS_TASK_MAX];
uint32_t task_info_count;
uint64_t task_info_tick;
unsigned long last_task_refresh;
struct leonos_ui_surface ui;
uint32_t app_client_scratch[APP_CLIENT_MAX_W * APP_CLIENT_MAX_H];
uint32_t screen[MAX_FB_W * MAX_FB_H];

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
};

const uint32_t desktop_scale_options[DESKTOP_SCALE_COUNT] = {1, 2, 3};
