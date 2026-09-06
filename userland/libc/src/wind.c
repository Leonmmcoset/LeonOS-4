/* libwind: windowd AF_UNIX client. Exports the historical leonos_gui_*
 * entry points so existing applications keep working without source changes. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <fcntl.h>
#include <leonos/device.h>
#include <leonos/gui.h>
#include <leonos/inputm.h>
#include <leonos/ui.h>
#include <leonos/syscall.h>
#include <leonos/unix_ipc.h>
#include <leonos/windowd.h>
#include <linux/fb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define WIND_MAX_WINDOWS 32u
#define WIND_EVENT_QUEUE 64u
#define WIND_MSG_QUEUE 64u
#define WIND_FRAME_CAP 4096u
#define WIND_CONNECT_RETRY_MS 5000u

struct wind_window {
    uint32_t id;
    int fd;
    void *mapping;
    uint64_t bytes;
    uint32_t stride;
};

static int wind_app_fd = -1;
static int wind_policy_fd = -1;
static struct wind_window wind_windows[WIND_MAX_WINDOWS];
static struct leonos_gui_app_event wind_events[WIND_EVENT_QUEUE];
static uint32_t wind_event_head;
static uint32_t wind_event_tail;
static struct leonos_input_event wind_inputs[WIND_EVENT_QUEUE];
static uint32_t wind_input_head;
static uint32_t wind_input_tail;
static struct leonos_gui_window_msg wind_msgs[WIND_MSG_QUEUE];
static uint32_t wind_msg_head;
static uint32_t wind_msg_tail;
static uint32_t wind_policy_mouse_visible = 1u;
static struct leonos_display_request wind_display_requests[4];
static uint32_t wind_display_request_head;
static uint32_t wind_display_request_tail;
static struct leonos_appearance_request wind_appearance_requests[4];
static uint32_t wind_appearance_request_head;
static uint32_t wind_appearance_request_tail;

typedef uint64_t cpu_set_t;
int sched_getaffinity(int pid, unsigned long cpusetsize, uint64_t *set);
int sched_setaffinity(int pid, unsigned long cpusetsize, const uint64_t *set);

static void wind_sleep_ms(uint32_t ms)
{
    (void)poll(0, 0, (int)ms);
}

static uint32_t now_ms(void)
{
    struct timespec ts = {0};
    (void)clock_gettime(1, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

static int wind_open_connection(const char *path)
{
    uint32_t deadline = now_ms() + WIND_CONNECT_RETRY_MS;
    for (;;) {
        int fd = leonos_ipc_connect(path);
        if (fd >= 0) return fd;
        if (now_ms() >= deadline) return -1;
        wind_sleep_ms(10);
    }
}

static int wind_read_frame(int fd, uint32_t *type, void *payload,
                           uint32_t capacity, uint32_t *length, int *received_fd)
{
    uint8_t buffer[WIND_FRAME_CAP];
    int result;
    if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
    result = leonos_ipc_recv_fd(fd, type, buffer, sizeof(buffer), length, received_fd);
    if (result < 0) return -1;
    if (*length > capacity) {
        errno = EMSGSIZE;
        return -1;
    }
    if (*length) memcpy(payload, buffer, *length);
    return 0;
}

static int wind_wait_type(int fd, uint32_t expected, void *payload,
                          uint32_t capacity, uint32_t *length, int *received_fd)
{
    uint32_t type = 0;
    uint32_t got = 0;
    int ancillary = -1;
    uint32_t deadline = now_ms() + 3000u;
    if (received_fd) *received_fd = -1;
    for (;;) {
        if (wind_read_frame(fd, &type, payload, capacity, &got, &ancillary) == 0) {
            if (type == expected) {
                if (length) *length = got;
                if (received_fd && ancillary >= 0) *received_fd = ancillary;
                return 0;
            }
            if (type == LEONOS_WIN_MSG_EVENT && got >= sizeof(struct leonos_gui_app_event)) {
                struct leonos_gui_app_event event;
                memcpy(&event, payload, sizeof(event));
                wind_events[wind_event_head] = event;
                wind_event_head = (wind_event_head + 1u) % WIND_EVENT_QUEUE;
            } else if (type == LEONOS_WIN_MSG_INPUT && got >= sizeof(struct leonos_input_event)) {
                struct leonos_input_event event;
                memcpy(&event, payload, sizeof(event));
                wind_inputs[wind_input_head] = event;
                wind_input_head = (wind_input_head + 1u) % WIND_EVENT_QUEUE;
            } else if (type == LEONOS_WIN_MSG_WINDOW_NOTIFY && got >= sizeof(struct leonos_gui_window_msg)) {
                struct leonos_gui_window_msg message;
                memcpy(&message, payload, sizeof(message));
                wind_msgs[wind_msg_head] = message;
                wind_msg_head = (wind_msg_head + 1u) % WIND_MSG_QUEUE;
            } else if (type == LEONOS_WIN_MSG_DISPLAY_REQUEST && got >= sizeof(struct leonos_display_request)) {
                struct leonos_display_request request;
                memcpy(&request, payload, sizeof(request));
                wind_display_requests[wind_display_request_head] = request;
                wind_display_request_head = (wind_display_request_head + 1u) % 4u;
            } else if (type == LEONOS_WIN_MSG_APPEARANCE_REQUEST && got >= sizeof(struct leonos_appearance_request)) {
                struct leonos_appearance_request request;
                memcpy(&request, payload, sizeof(request));
                wind_appearance_requests[wind_appearance_request_head] = request;
                wind_appearance_request_head = (wind_appearance_request_head + 1u) % 4u;
            }
        }
        if (now_ms() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        wind_sleep_ms(2);
    }
}

static int wind_pump_fd(int fd)
{
    uint32_t type = 0;
    uint32_t got = 0;
    int result = 0;
    if (fd < 0) return 0;
    for (;;) {
        struct pollfd pollfd = {.fd = fd, .events = POLLIN, .revents = 0};
        uint8_t buffer[WIND_FRAME_CAP];
        int poll_result = poll(&pollfd, 1, 0);
        if (poll_result <= 0) break;
        if (leonos_ipc_recv_fd(fd, &type, buffer, sizeof(buffer), &got, 0) < 0) break;
        result = 1;
        if (type == LEONOS_WIN_MSG_EVENT && got >= sizeof(struct leonos_gui_app_event)) {
            struct leonos_gui_app_event event;
            memcpy(&event, buffer, sizeof(event));
            wind_events[wind_event_head] = event;
            wind_event_head = (wind_event_head + 1u) % WIND_EVENT_QUEUE;
        } else if (type == LEONOS_WIN_MSG_INPUT && got >= sizeof(struct leonos_input_event)) {
            struct leonos_input_event event;
            memcpy(&event, buffer, sizeof(event));
            wind_inputs[wind_input_head] = event;
            wind_input_head = (wind_input_head + 1u) % WIND_EVENT_QUEUE;
        } else if (type == LEONOS_WIN_MSG_WINDOW_NOTIFY && got >= sizeof(struct leonos_gui_window_msg)) {
            struct leonos_gui_window_msg message;
            memcpy(&message, buffer, sizeof(message));
            wind_msgs[wind_msg_head] = message;
            wind_msg_head = (wind_msg_head + 1u) % WIND_MSG_QUEUE;
        } else if (type == LEONOS_WIN_MSG_DISPLAY_REQUEST && got >= sizeof(struct leonos_display_request)) {
            struct leonos_display_request request;
            memcpy(&request, buffer, sizeof(request));
            wind_display_requests[wind_display_request_head] = request;
            wind_display_request_head = (wind_display_request_head + 1u) % 4u;
        } else if (type == LEONOS_WIN_MSG_APPEARANCE_REQUEST && got >= sizeof(struct leonos_appearance_request)) {
            struct leonos_appearance_request request;
            memcpy(&request, buffer, sizeof(request));
            wind_appearance_requests[wind_appearance_request_head] = request;
            wind_appearance_request_head = (wind_appearance_request_head + 1u) % 4u;
        } else if (type == LEONOS_WIN_MSG_MOUSE_VISIBLE && got >= sizeof(struct leonos_win_mouse_visible)) {
            struct leonos_win_mouse_visible state;
            memcpy(&state, buffer, sizeof(state));
            if (state.window_id == 0xffffffffu) wind_policy_mouse_visible = state.visible;
        }
    }
    return result;
}

static int wind_hello(int fd, uint32_t role, const char *token)
{
    struct leonos_win_hello_ack ack = {0};
    uint32_t length = 0;
    if (role == LEONOS_WIN_ROLE_POLICY) {
        struct leonos_win_policy_hello hello;
        memset(&hello, 0, sizeof(hello));
        hello.pid = (uint32_t)getpid();
        strncpy(hello.token, token ? token : LEONOS_WIN_POLICY_TOKEN,
                sizeof(hello.token) - 1u);
        if (leonos_ipc_send(fd, LEONOS_WIN_MSG_POLICY_HELLO, &hello,
                            sizeof(hello)) < 0) return -1;
    } else {
        struct leonos_win_hello hello = {.pid = (uint32_t)getpid(), .role = role};
        if (leonos_ipc_send(fd, LEONOS_WIN_MSG_HELLO, &hello, sizeof(hello)) < 0) {
            return -1;
        }
    }
    if (wind_wait_type(fd, LEONOS_WIN_MSG_HELLO_ACK, &ack, sizeof(ack),
                       &length, 0) < 0) return -1;
    (void)leonos_ipc_set_nonblock(fd, 1);
    return (int)ack.version;
}

static int wind_app_ensure(void)
{
    if (wind_app_fd >= 0) return wind_app_fd;
    wind_app_fd = wind_open_connection(LEONOS_IPC_SOCK_WINDOWD);
    if (wind_app_fd < 0) return -1;
    if (wind_hello(wind_app_fd, LEONOS_WIN_ROLE_APP, 0) < 0) {
        leonos_ipc_close(wind_app_fd);
        wind_app_fd = -1;
        return -1;
    }
    return wind_app_fd;
}

static int wind_policy_ensure(void)
{
    if (wind_policy_fd >= 0) return wind_policy_fd;
    wind_policy_fd = wind_open_connection(LEONOS_IPC_SOCK_WINDOWD);
    if (wind_policy_fd < 0) return -1;
    if (wind_hello(wind_policy_fd, LEONOS_WIN_ROLE_POLICY, LEONOS_WIN_POLICY_TOKEN) < 0) {
        leonos_ipc_close(wind_policy_fd);
        wind_policy_fd = -1;
        return -1;
    }
    return wind_policy_fd;
}

static struct wind_window *wind_find_window(uint32_t window_id)
{
    for (uint32_t i = 0; i < WIND_MAX_WINDOWS; ++i) {
        if (wind_windows[i].id == window_id && wind_windows[i].fd >= 0) {
            return &wind_windows[i];
        }
    }
    return 0;
}

static void wind_release_window(struct wind_window *window)
{
    if (!window) return;
    if (window->mapping && window->bytes) (void)munmap(window->mapping, window->bytes);
    if (window->fd >= 0) close(window->fd);
    memset(window, 0, sizeof(*window));
    window->fd = -1;
}

int leonos_gui_policy_connect(void)
{
    return wind_policy_ensure() >= 0 ? 0 : -1;
}

int leonos_gui_connect(void)
{
    if (wind_policy_fd >= 0) return 1;
    return wind_app_ensure() >= 0 ? 1 : -1;
}

int leonos_gui_create_window(const struct leonos_gui_window *window)
{
    if (!window || !window->width || !window->height || !window->title || !window->text) {
        return -1;
    }
    return leonos_gui_create_app_window_ex(window->title, window->text,
                                           window->width, window->height, window->flags);
}

int leonos_gui_next_event(struct leonos_input_event *event)
{
    if (!event) return -1;
    if (wind_policy_fd < 0) {
        if (wind_policy_ensure() < 0) return -1;
    }
    (void)wind_pump_fd(wind_policy_fd);
    if (wind_input_head == wind_input_tail) return 0;
    *event = wind_inputs[wind_input_tail];
    wind_input_tail = (wind_input_tail + 1u) % WIND_EVENT_QUEUE;
    return 1;
}

unsigned long leonos_uptime_ms(void)
{
    struct timespec ts;
    if (clock_gettime(1, &ts) < 0) return 0;
    return (unsigned long)((uint64_t)ts.tv_sec * 1000ULL +
                           (uint64_t)ts.tv_nsec / 1000000ULL);
}

/* ---- /dev/fb0 standard mmap drawing path ---- */

static int wind_fb_fd(void)
{
    static int fd = -1;
    if (fd < 0) fd = open(LEONOS_DEV_FB0, LEONOS_O_RDWR, 0);
    return fd;
}

static void *wind_fb_map(void)
{
    static void *mapping;
    struct leonos_fb_info info;
    if (mapping) return mapping;
    if (leonos_fb_info(&info) < 0) return 0;
    mapping = mmap(0, (size_t)info.pitch * info.height,
                   PROT_READ | PROT_WRITE, MAP_SHARED, wind_fb_fd(), 0);
    return mapping;
}

int leonos_fb_info(struct leonos_fb_info *info)
{
    struct fb_var_screeninfo variable;
    struct fb_fix_screeninfo fixed;
    int fd = wind_fb_fd();
    if (!info || fd < 0) return -1;
    memset(&variable, 0, sizeof(variable));
    memset(&fixed, 0, sizeof(fixed));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0) return -1;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fixed) < 0) return -1;
    info->width = variable.xres;
    info->height = variable.yres;
    info->pitch = fixed.line_length;
    info->bpp = (uint8_t)variable.bits_per_pixel;
    return 0;
}

int leonos_fb_capabilities(struct leonos_fb_capabilities *caps)
{
    struct leonos_fb_info info;
    if (!caps || leonos_fb_info(&info) < 0) return -1;
    memset(caps, 0, sizeof(*caps));
    caps->bytes_per_pixel = 4;
    caps->capabilities = LEONOS_FB_CAP_MODE_SET;
    caps->max_width = LEONOS_GUI_MAX_WINDOW_WIDTH;
    caps->max_height = LEONOS_GUI_MAX_WINDOW_HEIGHT;
    caps->max_bytes = info.pitch * info.height;
    caps->backend = LEONOS_FB_BACKEND_BOOT;
    return 0;
}

int leonos_fb_set_mode(uint32_t width, uint32_t height)
{
    struct fb_var_screeninfo variable;
    int fd = wind_fb_fd();
    if (fd < 0 || !width || !height) return -1;
    memset(&variable, 0, sizeof(variable));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0) return -1;
    variable.xres = width;
    variable.yres = height;
    variable.xres_virtual = width;
    variable.yres_virtual = height;
    return ioctl(fd, FBIOPUT_VSCREENINFO, &variable);
}

int leonos_fb_fill(uint32_t color)
{
    struct leonos_fb_info info;
    void *mapping = wind_fb_map();
    uint32_t *pixels;
    uint32_t count;
    if (!mapping || leonos_fb_info(&info) < 0) return -1;
    pixels = (uint32_t *)mapping;
    count = ((uint32_t)info.pitch / 4u) * info.height;
    for (uint32_t i = 0; i < count; ++i) pixels[i] = color;
    return 0;
}

int leonos_fb_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color)
{
    struct leonos_fb_info info;
    void *mapping = wind_fb_map();
    uint8_t *base;
    if (!mapping || leonos_fb_info(&info) < 0) return -1;
    if (x >= info.width || y >= info.height) return 0;
    if (width > info.width - x) width = info.width - x;
    if (height > info.height - y) height = info.height - y;
    base = (uint8_t *)mapping + y * info.pitch + x * 4u;
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t *pixels = (uint32_t *)(base + row * info.pitch);
        for (uint32_t col = 0; col < width; ++col) pixels[col] = color;
    }
    return 0;
}

int leonos_fb_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    static const uint8_t glyph[][7] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x5f,0x00,0x00,0x00,0x00},
        {0x00,0x07,0x00,0x07,0x00,0x00,0x00},
        {0x14,0x7f,0x14,0x7f,0x14,0x00,0x00},
        {0x24,0x2a,0x7f,0x2a,0x12,0x00,0x00},
        {0x23,0x13,0x08,0x64,0x62,0x00,0x00},
        {0x36,0x49,0x55,0x22,0x50,0x00,0x00},
        {0x00,0x05,0x03,0x00,0x00,0x00,0x00},
    };
    if (!text) return -1;
    for (uint32_t i = 0; text[i]; ++i) {
        uint8_t ch = (uint8_t)text[i];
        const uint8_t *rows = ch < sizeof(glyph) / sizeof(glyph[0]) ? glyph[ch] : glyph[0];
        for (uint32_t row = 0; row < 7u; ++row) {
            for (uint32_t bit = 0; bit < 5u; ++bit) {
                uint32_t color = (rows[row] & (1u << bit)) ? fg : bg;
                (void)leonos_fb_rect(x + i * 6u + bit, y + row, 1, 1, color);
            }
        }
    }
    return 0;
}

uint32_t leonos_fb_pixel(uint32_t x, uint32_t y)
{
    struct leonos_fb_info info;
    void *mapping = wind_fb_map();
    if (!mapping || leonos_fb_info(&info) < 0) return 0;
    if (x >= info.width || y >= info.height) return 0;
    return *(uint32_t *)((uint8_t *)mapping + y * info.pitch + x * 4u);
}

int leonos_fb_blit(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t stride, const uint32_t *pixels)
{
    struct leonos_fb_info info;
    void *mapping = wind_fb_map();
    if (!pixels || !mapping || leonos_fb_info(&info) < 0) return -1;
    if (x >= info.width || y >= info.height) return 0;
    if (width > info.width - x) width = info.width - x;
    if (height > info.height - y) height = info.height - y;
    for (uint32_t row = 0; row < height; ++row) {
        memcpy((uint8_t *)mapping + (y + row) * info.pitch + x * 4u,
               (const uint8_t *)pixels + row * stride, (size_t)width * 4u);
    }
    return 0;
}

/* ---- window protocol ---- */

int leonos_gui_create_app_window(const char *title, const char *text,
                                 uint32_t width, uint32_t height)
{
    return leonos_gui_create_app_window_ex(title, text, width, height, 0);
}

int leonos_gui_create_app_window_ex(const char *title, const char *text,
                                    uint32_t width, uint32_t height, uint32_t flags)
{
    struct leonos_win_create request;
    struct leonos_win_create_ack ack;
    struct wind_window *window = 0;
    uint32_t length = 0;
    int fd = -1;
    int shm_fd = -1;
    int64_t bytes;
    if (!title || !text || !width || !height || width > LEONOS_GUI_MAX_WINDOW_WIDTH ||
        height > LEONOS_GUI_MAX_WINDOW_HEIGHT) return -1;
    fd = wind_app_ensure();
    if (fd < 0) return -1;
    memset(&request, 0, sizeof(request));
    request.width = width;
    request.height = height;
    request.flags = flags;
    strncpy(request.title, title, sizeof(request.title) - 1u);
    strncpy(request.text, text, sizeof(request.text) - 1u);
    if (leonos_ipc_send(fd, LEONOS_WIN_MSG_CREATE, &request, sizeof(request)) < 0) {
        return -1;
    }
    if (wind_wait_type(fd, LEONOS_WIN_MSG_CREATE_ACK, &ack, sizeof(ack),
                       &length, &shm_fd) < 0) return -1;
    if (!ack.window_id || shm_fd < 0) {
        if (shm_fd >= 0) close(shm_fd);
        return -1;
    }
    for (uint32_t i = 0; i < WIND_MAX_WINDOWS; ++i) {
        if (wind_windows[i].fd < 0) { window = &wind_windows[i]; break; }
    }
    if (!window) {
        close(shm_fd);
        return -1;
    }
    bytes = (int64_t)ack.stride * ack.height;
    window->id = ack.window_id;
    window->fd = shm_fd;
    window->stride = ack.stride;
    window->bytes = (uint64_t)bytes;
    window->mapping = mmap(0, (size_t)bytes, PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
    if (window->mapping == MAP_FAILED || !window->mapping) {
        window->mapping = 0;
        wind_release_window(window);
        return -1;
    }
    {
        struct leonos_appearance_state appearance;
        if (leonos_appearance_get_state(&appearance) == 0) {
            (void)leonos_ui_theme_set_appearance(appearance.theme,
                                                 appearance.metro_color_scheme,
                                                 appearance.win95_color_scheme);
        }
    }
    (void)leonos_inputm_note_gui_window(ack.window_id);
    return (int)ack.window_id;
}

int leonos_gui_destroy_app_window(uint32_t window_id)
{
    struct leonos_win_destroy request = {.window_id = window_id};
    struct wind_window *window = wind_find_window(window_id);
    int fd = wind_app_fd;
    if (fd < 0) fd = wind_app_ensure();
    if (fd < 0) return -1;
    if (leonos_ipc_send(fd, LEONOS_WIN_MSG_DESTROY, &request,
                        sizeof(request)) < 0) return -1;
    if (window) wind_release_window(window);
    return 0;
}

int leonos_gui_update_window(const struct leonos_gui_window_update *update)
{
    struct leonos_win_update request;
    int fd;
    if (!update) return -1;
    fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (fd < 0) return -1;
    memset(&request, 0, sizeof(request));
    request.window_id = update->window_id;
    request.mask = update->mask;
    request.flags = update->flags;
    if (update->title) strncpy(request.title, update->title, sizeof(request.title) - 1u);
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_UPDATE, &request, sizeof(request));
}

int leonos_gui_set_window_title(uint32_t window_id, const char *title)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id, .mask = LEONOS_GUI_WINDOW_UPDATE_TITLE,
        .flags = 0, .title = title};
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_window_borderless(uint32_t window_id, uint32_t borderless)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id, .mask = LEONOS_GUI_WINDOW_UPDATE_BORDERLESS,
        .flags = borderless ? LEONOS_GUI_WINDOW_BORDERLESS : 0};
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_window_taskbar_visible(uint32_t window_id, uint32_t visible)
{
    struct leonos_gui_window_update update = {
        .window_id = window_id, .mask = LEONOS_GUI_WINDOW_UPDATE_TASKBAR,
        .flags = visible ? 0 : LEONOS_GUI_WINDOW_HIDE_TASKBAR};
    return leonos_gui_update_window(&update);
}

int leonos_gui_set_taskbar_visible(uint32_t window_id, uint32_t visible)
{
    struct leonos_win_taskbar request = {.window_id = window_id, .visible = visible ? 1u : 0u};
    int fd = wind_policy_fd >= 0 ? wind_policy_fd : wind_policy_ensure();
    if (fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_TASKBAR, &request, sizeof(request));
}

int leonos_gui_poll_window(struct leonos_gui_window_msg *message)
{
    if (!message) return -1;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    (void)wind_pump_fd(wind_policy_fd);
    if (wind_msg_head == wind_msg_tail) return 0;
    *message = wind_msgs[wind_msg_tail];
    wind_msg_tail = (wind_msg_tail + 1u) % WIND_MSG_QUEUE;
    return 1;
}

int leonos_gui_present_window(uint32_t window_id, uint32_t width, uint32_t height,
                              uint32_t stride, const uint32_t *pixels)
{
    struct leonos_win_present request = {
        .window_id = window_id, .width = width, .height = height, .stride = stride};
    struct wind_window *window = wind_find_window(window_id);
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!pixels || fd < 0) return -1;
    (void)leonos_ui_present_for_pixels(pixels, window_id);
    if (window && window->mapping) {
        uint32_t copy_height = height;
        uint32_t copy_width = width;
        if (copy_width > window->stride / 4u) copy_width = window->stride / 4u;
        if ((uint64_t)copy_height * window->stride > window->bytes) {
            copy_height = (uint32_t)(window->bytes / window->stride);
        }
        for (uint32_t row = 0; row < copy_height; ++row) {
            memcpy((uint8_t *)window->mapping + row * window->stride,
                   (const uint8_t *)pixels + row * stride,
                   (size_t)copy_width * 4u);
        }
    }
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_PRESENT, &request, sizeof(request));
}

int leonos_gui_fetch_window(uint32_t window_id, uint32_t capacity_width,
                            uint32_t capacity_height, uint32_t stride,
                            uint32_t *pixels, uint32_t *out_width, uint32_t *out_height)
{
    struct leonos_win_fetch request = {
        .window_id = window_id, .capacity_width = capacity_width,
        .capacity_height = capacity_height, .stride = stride};
    struct leonos_win_fetch_ack ack;
    uint32_t length = 0;
    int fd = -1;
    int shm_fd = -1;
    void *mapping;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    if (leonos_ipc_send(wind_policy_fd, LEONOS_WIN_MSG_FETCH, &request,
                        sizeof(request)) < 0) return -1;
    if (wind_wait_type(wind_policy_fd, LEONOS_WIN_MSG_FETCH_ACK, &ack,
                       sizeof(ack), &length, &shm_fd) < 0) return -1;
    if (out_width) *out_width = ack.width;
    if (out_height) *out_height = ack.height;
    if (!pixels || shm_fd < 0) {
        if (shm_fd >= 0) close(shm_fd);
        return -1;
    }
    mapping = mmap(0, (size_t)ack.stride * ack.height, PROT_READ, MAP_SHARED,
                   shm_fd, 0);
    if (mapping == MAP_FAILED || !mapping) {
        close(shm_fd);
        return -1;
    }
    uint32_t copy_width = capacity_width < ack.width ? capacity_width : ack.width;
    uint32_t copy_height = capacity_height < ack.height ? capacity_height : ack.height;
    for (uint32_t row = 0; row < copy_height; ++row) {
        memcpy((uint8_t *)pixels + row * stride,
               (const uint8_t *)mapping + row * ack.stride,
               (size_t)copy_width * 4u);
    }
    (void)munmap(mapping, (size_t)ack.stride * ack.height);
    close(shm_fd);
    return 0;
}

int leonos_gui_poll_app_event(struct leonos_gui_app_event *event)
{
    if (!event) return -1;
    if (wind_app_fd < 0) return 0;
    (void)wind_pump_fd(wind_app_fd);
    if (wind_event_head == wind_event_tail) return 0;
    *event = wind_events[wind_event_tail];
    wind_event_tail = (wind_event_tail + 1u) % WIND_EVENT_QUEUE;
    return 1;
}

int leonos_gui_wait_app_event(struct leonos_gui_app_event *event, uint32_t timeout_ms)
{
    uint32_t deadline;
    int result;
    if (!event) return -1;
    deadline = now_ms() + timeout_ms;
    for (;;) {
        result = leonos_gui_poll_app_event(event);
        if (result != 0) return result;
        if (timeout_ms && now_ms() >= deadline) return 0;
        wind_sleep_ms(2);
    }
}

int leonos_gui_send_app_event(const struct leonos_gui_app_event *event)
{
    if (!event) return -1;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    return leonos_ipc_send(wind_policy_fd, LEONOS_WIN_MSG_EVENT, event,
                           sizeof(*event));
}

int leonos_gui_set_mouse_visible(uint32_t window_id, uint32_t visible)
{
    struct leonos_win_mouse_visible request = {
        .window_id = window_id, .visible = visible ? 1u : 0u};
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_MOUSE_VISIBLE, &request,
                           sizeof(request));
}

int leonos_gui_mouse_visible(void)
{
    struct leonos_win_mouse_visible query = {.window_id = 0xffffffffu, .visible = 0};
    int fd = wind_policy_fd >= 0 ? wind_policy_fd : wind_policy_ensure();
    if (fd < 0) return -1;
    if (leonos_ipc_send(fd, LEONOS_WIN_MSG_MOUSE_VISIBLE, &query,
                        sizeof(query)) < 0) return -1;
    (void)wind_wait_type(fd, LEONOS_WIN_MSG_MOUSE_VISIBLE, &query,
                         sizeof(query), 0, 0);
    return (int)query.visible;
}

int leonos_mouse_hide(uint32_t window_id) { return leonos_gui_set_mouse_visible(window_id, 0); }
int leonos_mouse_show(uint32_t window_id) { return leonos_gui_set_mouse_visible(window_id, 1); }
int leonos_mouse_is_visible(void) { return leonos_gui_mouse_visible(); }

int leonos_gui_cursor_request(const struct leonos_gui_cursor_request *request)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!request || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_CURSOR_REQUEST, request,
                           sizeof(*request));
}

int leonos_gui_set_cursor_position(uint32_t window_id, int32_t x, int32_t y)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id, .x = x, .y = y, .style = LEONOS_GUI_CURSOR_ARROW,
        .flags = LEONOS_GUI_CURSOR_REQUEST_POSITION};
    return leonos_gui_cursor_request(&request);
}

int leonos_gui_set_cursor_style(uint32_t window_id, uint32_t style)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id, .style = style,
        .flags = LEONOS_GUI_CURSOR_REQUEST_STYLE};
    return leonos_gui_cursor_request(&request);
}

int leonos_gui_set_cursor_auto(uint32_t window_id)
{
    struct leonos_gui_cursor_request request = {
        .window_id = window_id, .style = LEONOS_GUI_CURSOR_ARROW,
        .flags = LEONOS_GUI_CURSOR_REQUEST_AUTO};
    return leonos_gui_cursor_request(&request);
}

int leonos_mouse_set_position(uint32_t window_id, int32_t x, int32_t y)
{ return leonos_gui_set_cursor_position(window_id, x, y); }
int leonos_mouse_set_style(uint32_t window_id, uint32_t style)
{ return leonos_gui_set_cursor_style(window_id, style); }
int leonos_mouse_set_auto(uint32_t window_id)
{ return leonos_gui_set_cursor_auto(window_id); }

int leonos_mouse_get_state(struct leonos_mouse_state *state)
{
    if (!state) return -1;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    memset(state, 0, sizeof(*state));
    state->present = 1;
    state->absolute = 1;
    return 1;
}

int leonos_mouse_get_position(int32_t *x, int32_t *y)
{
    struct leonos_mouse_state state;
    int result;
    if (!x || !y) return -1;
    result = leonos_mouse_get_state(&state);
    if (result > 0) { *x = state.x; *y = state.y; }
    return result;
}

int leonos_mouse_set_region(const struct leonos_gui_cursor_region_request *region)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!region || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_CURSOR_REGION, region,
                           sizeof(*region));
}

int leonos_mouse_clear_regions(uint32_t window_id)
{
    struct leonos_gui_cursor_region_request region = {
        .window_id = window_id, .operation = LEONOS_GUI_CURSOR_REGION_CLEAR};
    return window_id ? leonos_mouse_set_region(&region) : -1;
}

int leonos_task_affinity_get(uint32_t pid, uint64_t *mask)
{
    return sched_getaffinity((pid_t)pid, sizeof(*mask), (cpu_set_t *)mask);
}

int leonos_task_affinity_set(uint32_t pid, uint64_t mask)
{
    return sched_setaffinity((pid_t)pid, sizeof(mask), (const cpu_set_t *)&mask);
}

int leonos_task_kill(uint32_t pid)
{
    return kill((pid_t)pid, SIGTERM);
}

int leonos_display_get_state(struct leonos_display_state *state)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!state || fd < 0) return -1;
    memset(state, 0, sizeof(*state));
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_DISPLAY_STATE, state, 0) < 0 ? -1 :
           (wind_wait_type(fd, LEONOS_WIN_MSG_DISPLAY_STATE, state,
                           sizeof(*state), 0, 0) < 0 ? -1 : 0);
}

int leonos_display_request(const struct leonos_display_request *request)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!request || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_DISPLAY_REQUEST, request,
                           sizeof(*request));
}

int leonos_display_poll_request(struct leonos_display_request *request)
{
    if (!request) return -1;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    (void)wind_pump_fd(wind_policy_fd);
    if (wind_display_request_head == wind_display_request_tail) return 0;
    *request = wind_display_requests[wind_display_request_tail];
    wind_display_request_tail = (wind_display_request_tail + 1u) % 4u;
    return 1;
}

int leonos_display_publish_state(const struct leonos_display_state *state)
{
    int fd = wind_policy_fd >= 0 ? wind_policy_fd : wind_policy_ensure();
    if (!state || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_DISPLAY_STATE, state,
                           sizeof(*state));
}

int leonos_appearance_get_state(struct leonos_appearance_state *state)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!state || fd < 0) return -1;
    memset(state, 0, sizeof(*state));
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_APPEARANCE_STATE, state, 0) < 0 ? -1 :
           (wind_wait_type(fd, LEONOS_WIN_MSG_APPEARANCE_STATE, state,
                           sizeof(*state), 0, 0) < 0 ? -1 : 0);
}

int leonos_appearance_request_theme(const struct leonos_appearance_request *request)
{
    int fd = wind_app_fd >= 0 ? wind_app_fd : wind_app_ensure();
    if (!request || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_APPEARANCE_REQUEST, request,
                           sizeof(*request));
}

int leonos_appearance_poll_request(struct leonos_appearance_request *request)
{
    if (!request) return -1;
    if (wind_policy_fd < 0 && wind_policy_ensure() < 0) return -1;
    (void)wind_pump_fd(wind_policy_fd);
    if (wind_appearance_request_head == wind_appearance_request_tail) return 0;
    *request = wind_appearance_requests[wind_appearance_request_tail];
    wind_appearance_request_tail = (wind_appearance_request_tail + 1u) % 4u;
    return 1;
}

int leonos_appearance_publish_state(const struct leonos_appearance_state *state)
{
    int fd = wind_policy_fd >= 0 ? wind_policy_fd : wind_policy_ensure();
    if (!state || fd < 0) return -1;
    return leonos_ipc_send(fd, LEONOS_WIN_MSG_APPEARANCE_STATE, state,
                           sizeof(*state));
}
