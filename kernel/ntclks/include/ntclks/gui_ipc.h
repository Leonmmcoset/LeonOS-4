/*
 * LeonOS GUI IPC interface: defines kernel/window-server message structures.
 * Exposes window lifecycle, buffers, focus, input, and application events.
 */
#ifndef NTCLKS_GUI_IPC_H
#define NTCLKS_GUI_IPC_H

#include <ntclks/types.h>
#include <leonos/fs.h>

#define GUI_IPC_WINDOW_TITLE_MAX 48u
#define GUI_IPC_WINDOW_TEXT_MAX 1024u
#define GUI_IPC_WINDOW_PATH_MAX LEONOS_FS_PATH_LEN
/* GUI surfaces are backed by kernel pages and must stay within the native UI limits. */
#define GUI_IPC_MAX_WINDOW_WIDTH 1920u
#define GUI_IPC_MAX_WINDOW_HEIGHT 1080u
#define GUI_IPC_MAX_WINDOW_STRIDE GUI_IPC_MAX_WINDOW_WIDTH

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

#define GUI_IPC_WINDOW_MSG_CREATE 1u
#define GUI_IPC_WINDOW_MSG_DIRTY 2u
#define GUI_IPC_WINDOW_MSG_CLOSE 3u
#define GUI_IPC_WINDOW_MSG_UPDATE 4u
#define GUI_IPC_WINDOW_MSG_TASKBAR 5u
#define GUI_IPC_WINDOW_MSG_CURSOR 6u
#define GUI_IPC_WINDOW_MSG_CURSOR_REGION 7u

#define GUI_IPC_WINDOW_NO_RESIZE 0x00000001u
#define GUI_IPC_WINDOW_FULLSCREEN 0x00000002u
#define GUI_IPC_WINDOW_BORDERLESS 0x00000004u
#define GUI_IPC_WINDOW_HIDE_TASKBAR 0x00000008u
#define GUI_IPC_WINDOW_FLAGS_MUTABLE (GUI_IPC_WINDOW_BORDERLESS | GUI_IPC_WINDOW_HIDE_TASKBAR)

#define GUI_IPC_WINDOW_UPDATE_TITLE 0x00000001u
#define GUI_IPC_WINDOW_UPDATE_BORDERLESS 0x00000002u
#define GUI_IPC_WINDOW_UPDATE_TASKBAR 0x00000004u
#define GUI_IPC_WINDOW_UPDATE_ALL (GUI_IPC_WINDOW_UPDATE_TITLE | \
                                   GUI_IPC_WINDOW_UPDATE_BORDERLESS | \
                                   GUI_IPC_WINDOW_UPDATE_TASKBAR)

#define GUI_IPC_CURSOR_ARROW 0u
#define GUI_IPC_CURSOR_STYLE_COUNT 15u
#define GUI_IPC_CURSOR_REQUEST_POSITION 0x00000001u
#define GUI_IPC_CURSOR_REQUEST_STYLE 0x00000002u
#define GUI_IPC_CURSOR_REQUEST_AUTO 0x00000004u
#define GUI_IPC_CURSOR_REQUEST_ALL (GUI_IPC_CURSOR_REQUEST_POSITION | \
                                    GUI_IPC_CURSOR_REQUEST_STYLE | \
                                    GUI_IPC_CURSOR_REQUEST_AUTO)
#define GUI_IPC_CURSOR_REGION_SET 1u
#define GUI_IPC_CURSOR_REGION_REMOVE 2u
#define GUI_IPC_CURSOR_REGION_CLEAR 3u
#define GUI_IPC_CURSOR_REGION_DISABLED 0x00000001u


#define GUI_IPC_APP_EVENT_CLOSE 1u
#define GUI_IPC_APP_EVENT_FOCUS 2u
#define GUI_IPC_APP_EVENT_BLUR 3u
#define GUI_IPC_APP_EVENT_RESIZE 4u
#define GUI_IPC_APP_EVENT_MOUSE_MOVE 5u
#define GUI_IPC_APP_EVENT_MOUSE_BUTTON 6u
#define GUI_IPC_APP_EVENT_KEY_DOWN 7u
#define GUI_IPC_APP_EVENT_KEY_UP 8u
#define GUI_IPC_APP_EVENT_MOUSE_WHEEL 9u
#define GUI_IPC_APP_EVENT_THEME_CHANGED 10u

#define GUI_IPC_DISPLAY_REQUEST_APPLY 1u
#define GUI_IPC_DISPLAY_REQUEST_KEEP 2u
#define GUI_IPC_DISPLAY_REQUEST_REVERT 3u
#define GUI_IPC_DISPLAY_REQUEST_REFRESH 4u

#define GUI_IPC_COLOR_SCHEME_COUNT 7u

#define GUI_IPC_WALLPAPER_MODE_FILL 0u
#define GUI_IPC_WALLPAPER_MODE_FIT 1u
#define GUI_IPC_WALLPAPER_MODE_CENTER 2u
#define GUI_IPC_WALLPAPER_MODE_TILE 3u
#define GUI_IPC_WALLPAPER_MODE_STRETCH 4u
#define GUI_IPC_WALLPAPER_MODE_COUNT 5u

struct gui_ipc_window {
    uint32_t type;
    uint32_t pid;
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t data;
    char title[GUI_IPC_WINDOW_TITLE_MAX];
    char text[GUI_IPC_WINDOW_TEXT_MAX];
    char app_path[GUI_IPC_WINDOW_PATH_MAX];
    int32_t cursor_x;
    int32_t cursor_y;
    uint32_t cursor_region_id;
    uint32_t cursor_style;
    uint32_t cursor_flags;
    uint32_t cursor_operation;
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

struct gui_ipc_appearance_state {
    uint32_t theme;
    uint32_t metro_color_scheme;
    uint32_t win95_color_scheme;
    uint32_t wallpaper_mode;
    char wallpaper_path[LEONOS_FS_PATH_LEN];
};

struct gui_ipc_appearance_request {
    uint32_t theme;
    uint32_t metro_color_scheme;
    uint32_t win95_color_scheme;
    uint32_t wallpaper_mode;
    char wallpaper_path[LEONOS_FS_PATH_LEN];
};

struct gui_ipc_mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    uint8_t visible;
    uint8_t present;
    uint8_t absolute;
};

/**
 * @brief Initialize the GUI IPC subsystem: window slots, queues, and default state.
 */
void gui_ipc_init(void);
/**
 * @brief Check that width/height/stride fit the native UI limits and write the needed byte size to bytes.
 */
int gui_ipc_validate_surface_geometry(uint32_t width, uint32_t height,
                                      uint32_t stride, uint64_t *bytes);
/**
 * @brief Create a window for pid and return its id, or 0 on failure.
 */
int32_t gui_ipc_create_window(uint32_t pid, uint32_t width, uint32_t height,
                              const char *title, const char *text, uint32_t flags);
/**
 * @brief Ask the window server to create a system window (e.g. taskbar) backed by app_path.
 */
int gui_ipc_post_system_window(uint32_t pid, uint32_t width, uint32_t height,
                               const char *title, const char *text,
                               const char *app_path, uint32_t flags);
/**
 * @brief Dequeue the next window message (create/dirty/close/...) for the window server.
 */
int gui_ipc_pop_window(uint32_t caller_pid, struct gui_ipc_window *out);
/**
 * @brief Copy the app's pixel buffer into the window's back buffer and mark it dirty.
 */
int gui_ipc_present_window(uint32_t pid, uint32_t window_id, uint32_t width, uint32_t height,
                           uint32_t stride, const uint32_t *pixels);
/**
 * @brief Destroy the window owned by pid; returns 0 on failure.
 */
int gui_ipc_destroy_window(uint32_t pid, uint32_t window_id);
/**
 * @brief Apply the title/flags/taskbar updates selected by mask to the window.
 */
int gui_ipc_update_window(uint32_t pid, uint32_t window_id, uint32_t mask,
                          uint32_t flags, const char *title);
/**
 * @brief Show or hide the taskbar for a window.
 */
int gui_ipc_set_taskbar_visible(uint32_t pid, uint32_t window_id, uint32_t visible);
/**
 * @brief Request a cursor shape/style at (x,y) for a window.
 */
int gui_ipc_request_cursor(uint32_t pid, uint32_t window_id, int32_t x, int32_t y,
                           uint32_t style, uint32_t flags);
int gui_ipc_request_cursor_region(uint32_t pid, uint32_t window_id,
                                  uint32_t region_id, int32_t x, int32_t y,
                                  uint32_t width, uint32_t height,
                                  uint32_t style, uint32_t flags,
                                  uint32_t operation);
int gui_ipc_mouse_state(struct gui_ipc_mouse_state *out);
/**
 * @brief Read a window's pixels back for compositing, clipped to the capacity; returns the copied size.
 */
int gui_ipc_fetch_window(uint32_t caller_pid, uint32_t window_id,
                         uint32_t capacity_width, uint32_t capacity_height,
                         uint32_t stride, uint32_t *pixels,
                         uint32_t *out_width, uint32_t *out_height);
/**
 * @brief Push an app event (key/mouse/focus/...) into the window's event queue.
 */
int gui_ipc_push_event(uint32_t caller_pid, uint32_t window_id,
                       const struct gui_ipc_app_event *event);
/**
 * @brief Dequeue the oldest event for the window into out; returns 0 when empty.
 */
int gui_ipc_pop_event(uint32_t pid, uint32_t window_id, struct gui_ipc_app_event *out);
/**
 * @brief Show or hide the cursor over a window.
 */
int gui_ipc_set_mouse_visible(uint32_t pid, uint32_t window_id, uint32_t visible);
/**
 * @brief Return non-zero when the cursor is currently visible.
 */
int gui_ipc_mouse_visible(void);
/**
 * @brief Free all windows and resources owned by pid (called when the process exits).
 */
void gui_ipc_destroy_owner(uint32_t pid);
/**
 * @brief Copy the current display mode/scale state into out; returns 0 when unset.
 */
int gui_ipc_display_state(struct gui_ipc_display_state *out);
/**
 * @brief Record the display state published by the window server.
 */
int gui_ipc_publish_display_state(uint32_t caller_pid,
                                  const struct gui_ipc_display_state *state);
/**
 * @brief Queue an app's request to change display mode/scale for the window server.
 */
int gui_ipc_request_display(const struct gui_ipc_display_request *request);
/**
 * @brief Dequeue the pending display request for the window server.
 */
int gui_ipc_pop_display_request(uint32_t caller_pid,
                                struct gui_ipc_display_request *out);
/**
 * @brief Copy the current theme/wallpaper appearance state into out.
 */
int gui_ipc_appearance_state(struct gui_ipc_appearance_state *out);
/**
 * @brief Record the appearance state published by the window server.
 */
int gui_ipc_publish_appearance_state(uint32_t caller_pid,
                                     const struct gui_ipc_appearance_state *state);
/**
 * @brief Queue an app's appearance change request for the window server.
 */
int gui_ipc_request_appearance(const struct gui_ipc_appearance_request *request);
/**
 * @brief Dequeue the pending appearance request for the window server.
 */
int gui_ipc_pop_appearance_request(uint32_t caller_pid,
                                   struct gui_ipc_appearance_request *out);
/**
 * @brief Set the boot-time appearance theme (0 or 1).
 */
void gui_ipc_set_boot_theme(uint32_t theme);
/**
 * @brief Return the current appearance theme.
 */
uint32_t gui_ipc_appearance_theme(void);

#endif
