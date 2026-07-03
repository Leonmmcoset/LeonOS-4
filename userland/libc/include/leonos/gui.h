#ifndef LEONOS_GUI_H
#define LEONOS_GUI_H

#include <stdint.h>

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
#define LEONOS_GUI_IOCTL_CREATE_WINDOW 0x4c475743UL
#define LEONOS_GUI_IOCTL_POLL_WINDOW 0x4c475750UL
#define LEONOS_GUI_IOCTL_TASKS 0x4c54534bUL
#define LEONOS_GUI_IOCTL_PRESENT_WINDOW 0x4c475046UL
#define LEONOS_GUI_IOCTL_FETCH_WINDOW 0x4c475746UL
#define LEONOS_GUI_IOCTL_WINDOW_EVENT 0x4c475745UL
#define LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT 0x4c475753UL
#define LEONOS_GUI_IOCTL_DESTROY_WINDOW 0x4c475744UL
#define LEONOS_GUI_IOCTL_TASK_KILL 0x4c544b49UL
#define LEONOS_GUI_IOCTL_REBOOT 0x4c524254UL
#define LEONOS_GUI_IOCTL_SHUTDOWN 0x4c534844UL
#define LEONOS_GUI_IOCTL_DISPLAY_STATE 0x4c445350UL
#define LEONOS_GUI_IOCTL_DISPLAY_REQUEST 0x4c445351UL
#define LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST 0x4c445352UL
#define LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE 0x4c445353UL

#define LEONOS_DISPLAY_REQUEST_APPLY 1U
#define LEONOS_DISPLAY_REQUEST_KEEP 2U
#define LEONOS_DISPLAY_REQUEST_REVERT 3U
#define LEONOS_DISPLAY_REQUEST_REFRESH 4U

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

#define LEONOS_GUI_WINDOW_NO_RESIZE 0x00000001U
#define LEONOS_GUI_WINDOW_FULLSCREEN 0x00000002U

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

struct leonos_gui_window_msg {
    uint32_t type;
    uint32_t pid;
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    char title[48];
    char text[96];
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

struct leonos_task_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t reserved;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[LEONOS_TASK_NAME_LEN];
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

int leonos_gui_connect(void);
int leonos_gui_create_window(const struct leonos_gui_window *window);
int leonos_gui_next_event(struct leonos_input_event *event);
unsigned long leonos_uptime_ms(void);
int leonos_fb_info(struct leonos_fb_info *info);
int leonos_fb_fill(uint32_t color);
int leonos_fb_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
int leonos_fb_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
uint32_t leonos_fb_pixel(uint32_t x, uint32_t y);
int leonos_fb_blit(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride, const uint32_t *pixels);
int leonos_gui_create_app_window(const char *title, const char *text, uint32_t width, uint32_t height);
int leonos_gui_create_app_window_ex(const char *title, const char *text, uint32_t width, uint32_t height, uint32_t flags);
int leonos_gui_destroy_app_window(uint32_t window_id);
int leonos_gui_poll_window(struct leonos_gui_window_msg *message);
int leonos_gui_present_window(uint32_t window_id, uint32_t width, uint32_t height,
                              uint32_t stride, const uint32_t *pixels);
int leonos_gui_fetch_window(uint32_t window_id, uint32_t capacity_width, uint32_t capacity_height,
                            uint32_t stride, uint32_t *pixels,
                            uint32_t *out_width, uint32_t *out_height);
int leonos_gui_poll_app_event(struct leonos_gui_app_event *event);
int leonos_gui_send_app_event(const struct leonos_gui_app_event *event);
int leonos_task_snapshot(struct leonos_task_info *tasks, uint32_t capacity, uint64_t *tick);
int leonos_task_kill(uint32_t pid);
int leonos_display_get_state(struct leonos_display_state *state);
int leonos_display_request(const struct leonos_display_request *request);
int leonos_display_poll_request(struct leonos_display_request *request);
int leonos_display_publish_state(const struct leonos_display_state *state);

#endif
