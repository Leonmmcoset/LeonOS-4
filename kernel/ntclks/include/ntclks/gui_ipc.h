#ifndef NTCLKS_GUI_IPC_H
#define NTCLKS_GUI_IPC_H

#include <ntclks/types.h>
#include <leonos/fs.h>

#define GUI_IPC_WINDOW_TITLE_MAX 48u
#define GUI_IPC_WINDOW_TEXT_MAX 1024u
#define GUI_IPC_WINDOW_PATH_MAX LEONOS_FS_PATH_LEN

#define GUI_IPC_WINDOW_MSG_CREATE 1u
#define GUI_IPC_WINDOW_MSG_DIRTY 2u
#define GUI_IPC_WINDOW_MSG_CLOSE 3u

#define GUI_IPC_WINDOW_NO_RESIZE 0x00000001u
#define GUI_IPC_WINDOW_FULLSCREEN 0x00000002u

#define GUI_IPC_APP_EVENT_CLOSE 1u
#define GUI_IPC_APP_EVENT_FOCUS 2u
#define GUI_IPC_APP_EVENT_BLUR 3u
#define GUI_IPC_APP_EVENT_RESIZE 4u
#define GUI_IPC_APP_EVENT_MOUSE_MOVE 5u
#define GUI_IPC_APP_EVENT_MOUSE_BUTTON 6u
#define GUI_IPC_APP_EVENT_KEY_DOWN 7u
#define GUI_IPC_APP_EVENT_KEY_UP 8u
#define GUI_IPC_APP_EVENT_MOUSE_WHEEL 9u

#define GUI_IPC_DISPLAY_REQUEST_APPLY 1u
#define GUI_IPC_DISPLAY_REQUEST_KEEP 2u
#define GUI_IPC_DISPLAY_REQUEST_REVERT 3u
#define GUI_IPC_DISPLAY_REQUEST_REFRESH 4u

struct gui_ipc_window {
    uint32_t type;
    uint32_t pid;
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    char title[GUI_IPC_WINDOW_TITLE_MAX];
    char text[GUI_IPC_WINDOW_TEXT_MAX];
    char app_path[GUI_IPC_WINDOW_PATH_MAX];
};

struct gui_ipc_app_event {
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

struct gui_ipc_display_state {
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

struct gui_ipc_display_request {
    uint32_t action;
    uint32_t mode_index;
    uint32_t scale_index;
};

void gui_ipc_init(void);
int32_t gui_ipc_create_window(uint32_t pid, uint32_t width, uint32_t height,
                              const char *title, const char *text, uint32_t flags);
int gui_ipc_post_system_window(uint32_t pid, uint32_t width, uint32_t height,
                               const char *title, const char *text,
                               const char *app_path, uint32_t flags);
int gui_ipc_pop_window(struct gui_ipc_window *out);
int gui_ipc_present_window(uint32_t pid, uint32_t window_id, uint32_t width, uint32_t height,
                           uint32_t stride, const uint32_t *pixels);
int gui_ipc_destroy_window(uint32_t pid, uint32_t window_id);
int gui_ipc_fetch_window(uint32_t window_id, uint32_t capacity_width, uint32_t capacity_height,
                         uint32_t stride, uint32_t *pixels,
                         uint32_t *out_width, uint32_t *out_height);
int gui_ipc_push_event(uint32_t window_id, const struct gui_ipc_app_event *event);
int gui_ipc_pop_event(uint32_t pid, uint32_t window_id, struct gui_ipc_app_event *out);
void gui_ipc_destroy_owner(uint32_t pid);
int gui_ipc_display_state(struct gui_ipc_display_state *out);
int gui_ipc_publish_display_state(const struct gui_ipc_display_state *state);
int gui_ipc_request_display(const struct gui_ipc_display_request *request);
int gui_ipc_pop_display_request(struct gui_ipc_display_request *out);

#endif
