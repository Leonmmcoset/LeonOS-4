#ifndef LEONOS_GUI_H
#define LEONOS_GUI_H

#include <stdint.h>
#include <leonos/fs.h>

#define LEONOS_GUI_IOCTL_VERSION 0x4c475549UL
#define LEONOS_GUI_IOCTL_PATH_TEST 0x4c504154UL
#define LEONOS_GUI_IOCTL_EVENT 0x4c455654UL
#define LEONOS_GUI_IOCTL_UPTIME_MS 0x4c555054UL
#define LEONOS_GUI_IOCTL_FB_INFO 0x4c464249UL
#define LEONOS_GUI_IOCTL_FB_FILL 0x4c464246UL
#define LEONOS_GUI_IOCTL_FB_RECT 0x4c464252UL
#define LEONOS_GUI_IOCTL_FB_TEXT 0x4c464254UL
#define LEONOS_GUI_IOCTL_FB_PIXEL 0x4c464250UL
#define LEONOS_GUI_IOCTL_FB_BLIT 0x4c46424cUL
#define LEONOS_GUI_IOCTL_FB_SET_MODE 0x4c46424dUL
#define LEONOS_GUI_IOCTL_FB_CAPS 0x4c464243UL
#define LEONOS_GUI_IOCTL_CREATE_WINDOW 0x4c475743UL
#define LEONOS_GUI_IOCTL_POLL_WINDOW 0x4c475750UL
#define LEONOS_GUI_IOCTL_TASKS 0x4c54534bUL
#define LEONOS_GUI_IOCTL_PRESENT_WINDOW 0x4c475046UL
#define LEONOS_GUI_IOCTL_FETCH_WINDOW 0x4c475746UL
#define LEONOS_GUI_IOCTL_WINDOW_EVENT 0x4c475745UL
#define LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT 0x4c475457UL
#define LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT 0x4c475753UL
#define LEONOS_GUI_IOCTL_DESTROY_WINDOW 0x4c475744UL
#define LEONOS_GUI_IOCTL_TASK_KILL 0x4c544b49UL
#define LEONOS_GUI_IOCTL_REBOOT 0x4c524254UL
#define LEONOS_GUI_IOCTL_SHUTDOWN 0x4c534844UL
#define LEONOS_GUI_IOCTL_DISPLAY_STATE 0x4c445350UL
#define LEONOS_GUI_IOCTL_DISPLAY_REQUEST 0x4c445351UL
#define LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST 0x4c445352UL
#define LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE 0x4c445353UL
#define LEONOS_GUI_IOCTL_APPEARANCE_STATE 0x4c415053UL
#define LEONOS_GUI_IOCTL_APPEARANCE_REQUEST 0x4c415052UL
#define LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST 0x4c415050UL
#define LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE 0x4c415042UL
#define LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE 0x4c4d4f55UL
#define LEONOS_GUI_IOCTL_UPDATE_WINDOW 0x4c475755UL
#define LEONOS_GUI_IOCTL_SET_TASKBAR_VISIBLE 0x4c475442UL
#define LEONOS_GUI_IOCTL_CURSOR_REQUEST 0x4c474352UL
#define LEONOS_GUI_IOCTL_MOUSE_STATE 0x4c4d5354UL
#define LEONOS_GUI_IOCTL_CURSOR_REGION 0x4c474347UL

#define LEONOS_DISPLAY_REQUEST_APPLY 1U
#define LEONOS_DISPLAY_REQUEST_KEEP 2U
#define LEONOS_DISPLAY_REQUEST_REVERT 3U
#define LEONOS_DISPLAY_REQUEST_REFRESH 4U

#define LEONOS_FB_CAP_MODE_SET 0x0001U
#define LEONOS_FB_BACKEND_BOOT 0U
#define LEONOS_FB_BACKEND_BOCHS_VBE 1U
#define LEONOS_FB_BACKEND_VMWARE_SVGA 2U

#define LEONOS_WALLPAPER_MODE_FILL 0U
#define LEONOS_WALLPAPER_MODE_FIT 1U
#define LEONOS_WALLPAPER_MODE_CENTER 2U
#define LEONOS_WALLPAPER_MODE_TILE 3U
#define LEONOS_WALLPAPER_MODE_STRETCH 4U
#define LEONOS_WALLPAPER_MODE_COUNT 5U

#define LEONOS_TASK_NAME_LEN 32U
#define LEONOS_TASK_MAX 64U

#define LEONOS_INPUT_MOUSE 1U
#define LEONOS_INPUT_KEYBOARD 2U
#define LEONOS_INPUT_MOUSE_WHEEL 3U

#define LEONOS_KEY_BACKSPACE 14U
#define LEONOS_KEY_TAB 15U
#define LEONOS_KEY_ENTER 28U
#define LEONOS_KEY_LEFT_SHIFT 42U
#define LEONOS_KEY_RIGHT_SHIFT 54U
#define LEONOS_KEY_LEFT_ALT 56U
#define LEONOS_KEY_SPACE 57U
#define LEONOS_KEY_F12 88U
#define LEONOS_KEY_LEFT_WIN 112U
#define LEONOS_KEY_RIGHT_WIN 113U
#define LEONOS_KEY_MENU 114U
#define LEONOS_KEY_RIGHT_ALT 115U
#define LEONOS_KEY_RIGHT_CTRL 116U

#define LEONOS_GUI_APP_EVENT_CLOSE 1U
#define LEONOS_GUI_APP_EVENT_FOCUS 2U
#define LEONOS_GUI_APP_EVENT_BLUR 3U
#define LEONOS_GUI_APP_EVENT_RESIZE 4U
#define LEONOS_GUI_APP_EVENT_MOUSE_MOVE 5U
#define LEONOS_GUI_APP_EVENT_MOUSE_BUTTON 6U
#define LEONOS_GUI_APP_EVENT_KEY_DOWN 7U
#define LEONOS_GUI_APP_EVENT_KEY_UP 8U
#define LEONOS_GUI_APP_EVENT_MOUSE_WHEEL 9U
#define LEONOS_GUI_APP_EVENT_THEME_CHANGED 10U
#define LEONOS_GUI_IDLE_WAIT_MS 100U

#define LEONOS_GUI_WINDOW_NO_RESIZE 0x00000001U
#define LEONOS_GUI_WINDOW_FULLSCREEN 0x00000002U
#define LEONOS_GUI_WINDOW_BORDERLESS 0x00000004U
#define LEONOS_GUI_WINDOW_HIDE_TASKBAR 0x00000008U

#define LEONOS_GUI_WINDOW_UPDATE_TITLE 0x00000001U
#define LEONOS_GUI_WINDOW_UPDATE_BORDERLESS 0x00000002U
#define LEONOS_GUI_WINDOW_UPDATE_TASKBAR 0x00000004U

#define LEONOS_GUI_CURSOR_ARROW 0U
#define LEONOS_GUI_CURSOR_HAND 1U
#define LEONOS_GUI_CURSOR_TEXT 2U
#define LEONOS_GUI_CURSOR_WAIT 3U
#define LEONOS_GUI_CURSOR_CROSSHAIR 4U
#define LEONOS_GUI_CURSOR_MOVE 5U
#define LEONOS_GUI_CURSOR_NO 6U
#define LEONOS_GUI_CURSOR_HELP 7U
#define LEONOS_GUI_CURSOR_PROGRESS 8U
#define LEONOS_GUI_CURSOR_SIZE_NS 9U
#define LEONOS_GUI_CURSOR_SIZE_WE 10U
#define LEONOS_GUI_CURSOR_SIZE_NWSE 11U
#define LEONOS_GUI_CURSOR_SIZE_NESW 12U
#define LEONOS_GUI_CURSOR_UP 13U
#define LEONOS_GUI_CURSOR_APP_STARTING 14U
#define LEONOS_GUI_CURSOR_STYLE_COUNT 15U

#define LEONOS_GUI_CURSOR_REQUEST_POSITION 0x00000001U
#define LEONOS_GUI_CURSOR_REQUEST_STYLE 0x00000002U
#define LEONOS_GUI_CURSOR_REQUEST_AUTO 0x00000004U
#define LEONOS_GUI_CURSOR_REQUEST_ALL (LEONOS_GUI_CURSOR_REQUEST_POSITION | \
                                      LEONOS_GUI_CURSOR_REQUEST_STYLE | \
                                      LEONOS_GUI_CURSOR_REQUEST_AUTO)
#define LEONOS_GUI_WINDOW_MSG_CURSOR_REGION 7U
#define LEONOS_GUI_CURSOR_REGION_SET 1U
#define LEONOS_GUI_CURSOR_REGION_REMOVE 2U
#define LEONOS_GUI_CURSOR_REGION_CLEAR 3U
#define LEONOS_GUI_CURSOR_REGION_DISABLED 0x00000001U


struct leonos_gui_window {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    const char *title;
    const char *text;
    uint32_t flags;
};

struct leonos_input_event {
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t keycode;
    uint8_t pressed;
};

struct leonos_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

struct leonos_fb_capabilities {
    uint8_t bytes_per_pixel;
    uint8_t reserved;
    uint16_t capabilities;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bytes;
    uint32_t backend;
};

struct leonos_fb_mode {
    uint32_t width;
    uint32_t height;
};

struct leonos_fb_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
};

struct leonos_fb_text {
    uint32_t x;
    uint32_t y;
    uint32_t fg;
    uint32_t bg;
    const char *text;
};

struct leonos_fb_blit {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const uint32_t *pixels;
};

struct leonos_gui_create {
    uint32_t width;
    uint32_t height;
    const char *title;
    const char *text;
    uint32_t flags;
};

struct leonos_gui_window_update {
    uint32_t window_id;
    uint32_t mask;
    uint32_t flags;
    const char *title;
};

struct leonos_gui_taskbar_request {
    uint32_t window_id;
    uint32_t visible;
};

struct leonos_gui_cursor_request {
    uint32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t style;
    uint32_t flags;
};

struct leonos_mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    uint8_t visible;
    uint8_t present;
    uint8_t absolute;
};

struct leonos_gui_cursor_region_request {
    uint32_t window_id;
    uint32_t region_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t style;
    uint32_t flags;
    uint32_t operation;
};

struct leonos_gui_window_msg {
    uint32_t type;
    uint32_t pid;
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t data;
    char title[48];
    char text[1024];
    char app_path[LEONOS_FS_PATH_LEN];
    int32_t cursor_x;
    int32_t cursor_y;
    uint32_t cursor_region_id;
    uint32_t cursor_style;
    uint32_t cursor_flags;
    uint32_t cursor_operation;
};

struct leonos_gui_present {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const uint32_t *pixels;
};

struct leonos_gui_fetch {
    uint32_t window_id;
    uint32_t capacity_width;
    uint32_t capacity_height;
    uint32_t stride;
    uint32_t out_width;
    uint32_t out_height;
    uint32_t *pixels;
};

struct leonos_gui_app_event {
    uint32_t window_id;
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint32_t width;
    uint32_t height;
    uint8_t buttons;
    uint8_t keycode;
    uint8_t pressed;
    uint8_t reserved;
};

struct leonos_gui_wait_app_event {
    struct leonos_gui_app_event event;
    uint32_t timeout_ms;
};

#define LEONOS_TASK_SNAPSHOT_FLAG_ELEVATED_ADMIN 0x00000010U

struct leonos_task_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint32_t memory_kib;
    uint64_t cpu_ticks;
    int32_t priority;
    uint32_t pending_signals;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[LEONOS_TASK_NAME_LEN];
    char username[32];
};

struct leonos_task_snapshot {
    uint32_t capacity;
    uint32_t count;
    uint64_t tick;
    struct leonos_task_info *tasks;
};

struct leonos_display_state {
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t scale;
    uint32_t mode_index;
    uint32_t scale_index;
    uint32_t pending_confirm;
    uint32_t confirm_remaining_ms;
};

struct leonos_display_request {
    uint32_t action;
    uint32_t mode_index;
    uint32_t scale_index;
};

struct leonos_appearance_state {
    uint32_t theme;
    uint32_t metro_color_scheme;
    uint32_t win95_color_scheme;
    uint32_t wallpaper_mode;
    char wallpaper_path[LEONOS_FS_PATH_LEN];
};

struct leonos_appearance_request {
    uint32_t theme;
    uint32_t metro_color_scheme;
    uint32_t win95_color_scheme;
    uint32_t wallpaper_mode;
    char wallpaper_path[LEONOS_FS_PATH_LEN];
};

int leonos_gui_connect(void);
int leonos_gui_create_window(const struct leonos_gui_window *window);
int leonos_gui_next_event(struct leonos_input_event *event);
unsigned long leonos_uptime_ms(void);
int leonos_fb_info(struct leonos_fb_info *info);
int leonos_fb_capabilities(struct leonos_fb_capabilities *caps);
int leonos_fb_set_mode(uint32_t width, uint32_t height);
int leonos_fb_fill(uint32_t color);
int leonos_fb_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
int leonos_fb_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
uint32_t leonos_fb_pixel(uint32_t x, uint32_t y);
int leonos_fb_blit(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride, const uint32_t *pixels);
int leonos_gui_create_app_window(const char *title, const char *text, uint32_t width, uint32_t height);
int leonos_gui_create_app_window_ex(const char *title, const char *text, uint32_t width, uint32_t height, uint32_t flags);
int leonos_gui_destroy_app_window(uint32_t window_id);
int leonos_gui_update_window(const struct leonos_gui_window_update *update);
int leonos_gui_set_window_title(uint32_t window_id, const char *title);
int leonos_gui_set_window_borderless(uint32_t window_id, uint32_t borderless);
int leonos_gui_set_window_taskbar_visible(uint32_t window_id, uint32_t visible);
int leonos_gui_set_taskbar_visible(uint32_t window_id, uint32_t visible);
int leonos_gui_poll_window(struct leonos_gui_window_msg *message);
int leonos_gui_present_window(uint32_t window_id, uint32_t width, uint32_t height,
                              uint32_t stride, const uint32_t *pixels);
int leonos_gui_fetch_window(uint32_t window_id, uint32_t capacity_width, uint32_t capacity_height,
                            uint32_t stride, uint32_t *pixels,
                            uint32_t *out_width, uint32_t *out_height);
int leonos_gui_poll_app_event(struct leonos_gui_app_event *event);
int leonos_gui_wait_app_event(struct leonos_gui_app_event *event, uint32_t timeout_ms);
int leonos_gui_send_app_event(const struct leonos_gui_app_event *event);
int leonos_gui_set_mouse_visible(uint32_t window_id, uint32_t visible);
int leonos_gui_mouse_visible(void);
int leonos_mouse_get_state(struct leonos_mouse_state *state);
int leonos_task_snapshot(struct leonos_task_info *tasks, uint32_t capacity, uint64_t *tick);
int leonos_task_kill(uint32_t pid);
int leonos_display_get_state(struct leonos_display_state *state);
int leonos_display_request(const struct leonos_display_request *request);
int leonos_display_poll_request(struct leonos_display_request *request);
int leonos_display_publish_state(const struct leonos_display_state *state);
int leonos_appearance_get_state(struct leonos_appearance_state *state);
int leonos_appearance_request_theme(const struct leonos_appearance_request *request);
int leonos_appearance_poll_request(struct leonos_appearance_request *request);
int leonos_appearance_publish_state(const struct leonos_appearance_state *state);

#endif
