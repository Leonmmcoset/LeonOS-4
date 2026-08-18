#include "../../../third_party/stardustui/platforms/platform.hpp"

extern "C" {
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/ui.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

long syscall2(long number, long first, long second);
int open(const char *path, int flags, int mode);
long read(int fd, void *buffer, size_t length);
long write(int fd, const void *buffer, size_t length);
int close(int fd);
int unlink(const char *path);
size_t strlen(const char *text);
}

using namespace stardustui;

#define STARDUST_O_WRONLY LEONOS_O_WRONLY
#define STARDUST_O_CREAT LEONOS_O_CREAT
#define STARDUST_O_TRUNC LEONOS_O_TRUNC
#define STARDUST_O_APPEND LEONOS_O_APPEND

#define STARDUST_SYS_NANOSLEEP 35

namespace {
struct LeonosSurface {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
    bool open;
    bool shift;
};

LeonosSurface g_surface = {0, 0, 0, nullptr, false, false};
window_message_proc g_message_proc = nullptr;

uint32_t leonos_color(unsigned int color)
{
    return ((color >> 24) & 0xFFu) << 16 |
           ((color >> 16) & 0xFFu) << 8 |
           ((color >> 8) & 0xFFu);
}

bool valid_surface_point(int x, int y)
{
    return g_surface.pixels != nullptr && x >= 0 && y >= 0 &&
           static_cast<uint32_t>(x) < g_surface.width &&
           static_cast<uint32_t>(y) < g_surface.height;
}

void fill_surface(uint32_t color)
{
    if (g_surface.pixels == nullptr) {
        return;
    }
    const uint32_t count = g_surface.width * g_surface.height;
    for (uint32_t index = 0; index < count; ++index) {
        g_surface.pixels[index] = color;
    }
}

bool resize_surface(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 ||
        width > LEONOS_GUI_MAX_WINDOW_WIDTH ||
        height > LEONOS_GUI_MAX_WINDOW_HEIGHT) {
        return false;
    }
    const uint64_t count = static_cast<uint64_t>(width) * height;
    if (count > static_cast<uint64_t>(LEONOS_GUI_MAX_WINDOW_WIDTH) *
                    LEONOS_GUI_MAX_WINDOW_HEIGHT) {
        return false;
    }
    uint32_t *pixels = static_cast<uint32_t *>(realloc(g_surface.pixels, static_cast<size_t>(count * sizeof(uint32_t))));
    if (pixels == nullptr) {
        return false;
    }
    g_surface.pixels = pixels;
    g_surface.width = width;
    g_surface.height = height;
    fill_surface(0x00FFFFFFu);
    return true;
}

void dispatch_event(const struct leonos_gui_app_event &event)
{
    if (g_message_proc == nullptr) {
        return;
    }
    switch (event.type) {
    case LEONOS_GUI_APP_EVENT_CLOSE:
        g_surface.open = false;
        return;
    case LEONOS_GUI_APP_EVENT_RESIZE:
        if (resize_surface(event.width, event.height)) {
            g_message_proc(kWindowMessageResize, event.width, event.height);
        }
        return;
    case LEONOS_GUI_APP_EVENT_MOUSE_MOVE:
        g_message_proc(kWindowMessageMove, static_cast<unsigned long long>(event.x),
                       static_cast<unsigned long long>(event.y));
        return;
    case LEONOS_GUI_APP_EVENT_MOUSE_BUTTON:
        if (event.pressed) {
            g_message_proc(kWindowMessageLeftButtonDown, static_cast<unsigned long long>(event.x),
                           static_cast<unsigned long long>(event.y));
        } else {
            g_message_proc(kWindowMessageLeftButtonUp, static_cast<unsigned long long>(event.x),
                           static_cast<unsigned long long>(event.y));
        }
        return;
    case LEONOS_GUI_APP_EVENT_KEY_DOWN: {
        if (event.keycode == LEONOS_KEY_LEFT_SHIFT || event.keycode == LEONOS_KEY_RIGHT_SHIFT) {
            g_surface.shift = true;
            return;
        }
        char ch = 0;
        if (leonos_ui_keycode_to_char_shift(event.keycode, g_surface.shift, &ch)) {
            if (ch == '\b' || ch == '\n' || ch == '\t') {
                g_message_proc(kWindowMessageSpecialChar, 0,
                               static_cast<unsigned long long>(static_cast<unsigned char>(ch)));
            } else if (static_cast<unsigned char>(ch) >= 32u) {
                g_message_proc(kWindowMessageChar, 0,
                               static_cast<unsigned long long>(static_cast<unsigned char>(ch)));
            }
        }
        return;
    }
    case LEONOS_GUI_APP_EVENT_KEY_UP:
        if (event.keycode == LEONOS_KEY_LEFT_SHIFT || event.keycode == LEONOS_KEY_RIGHT_SHIFT) {
            g_surface.shift = false;
        }
        return;
    default:
        return;
    }
}
}

void *operator new(stardustui_cpp_size_t size)
{
    return malloc(size);
}

void *operator new[](stardustui_cpp_size_t size)
{
    return malloc(size);
}

void operator delete(void *ptr) { free(ptr); }
void operator delete[](void *ptr) { free(ptr); }
void operator delete(void *ptr, stardustui_cpp_size_t) { free(ptr); }
void operator delete[](void *ptr, stardustui_cpp_size_t) { free(ptr); }

bool create_window(char *title, int width, int height, bool resizable, unsigned long long *handle)
{
    if (title == nullptr || handle == nullptr || width <= 0 || height <= 0 || g_surface.open) {
        return false;
    }
    const uint32_t flags = resizable ? 0u : LEONOS_GUI_WINDOW_NO_RESIZE;
    const int native_handle = leonos_gui_create_app_window_ex(title, title,
                                                               static_cast<uint32_t>(width),
                                                               static_cast<uint32_t>(height), flags);
    if (native_handle <= 0 || !resize_surface(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        if (native_handle > 0) {
            leonos_gui_destroy_app_window(static_cast<uint32_t>(native_handle));
        }
        return false;
    }
    g_surface.handle = static_cast<uint32_t>(native_handle);
    g_surface.open = true;
    g_surface.shift = false;
    g_message_proc = nullptr;
    *handle = g_surface.handle;
    return true;
}

void print_error(const char *message)
{
    if (message != nullptr) {
        write(1, message, strlen(message));
        write(1, "\n", 1);
    }
}

void log_serial(const char *message)
{
    if (message != nullptr) {
        write(1, message, strlen(message));
    }
}

void append_debug_log(const char *) {}

void refresh_window(unsigned long long handle)
{
    if (g_surface.open && handle == g_surface.handle) {
        leonos_gui_present_window(g_surface.handle, g_surface.width, g_surface.height,
                                  g_surface.width, g_surface.pixels);
    }
}

void set_window_message_processor(unsigned long long, window_message_proc proc)
{
    g_message_proc = proc;
}

void wait_window()
{
    while (is_window_open(g_surface.handle)) {
        pump_window_events();
        (void)syscall2(STARDUST_SYS_NANOSLEEP, 16, 0);
    }
}

void pump_window_events()
{
    struct leonos_gui_app_event event{};
    event.window_id = g_surface.handle;
    while (leonos_gui_poll_app_event(&event) > 0) {
        dispatch_event(event);
        event.window_id = g_surface.handle;
    }
}

bool is_window_open(unsigned long long handle)
{
    return g_surface.open && static_cast<uint32_t>(handle) == g_surface.handle;
}

bool set_window_resizable(unsigned long long handle, bool resizable)
{
    (void)handle;
    (void)resizable;
    /* LeonOS only accepts the no-resize bit while the window is created. */
    return false;
}

bool delete_window(unsigned long long handle)
{
    if (static_cast<uint32_t>(handle) != g_surface.handle ||
        (g_surface.handle == 0 && g_surface.pixels == nullptr)) {
        return false;
    }
    g_surface.open = false;
    leonos_gui_destroy_app_window(g_surface.handle);
    free(g_surface.pixels);
    g_surface = {0, 0, 0, nullptr, false, false};
    g_message_proc = nullptr;
    return true;
}

void draw_pixel(unsigned long long, int x, int y, unsigned int color)
{
    if (valid_surface_point(x, y)) {
        g_surface.pixels[static_cast<uint32_t>(y) * g_surface.width + static_cast<uint32_t>(x)] = leonos_color(color);
    }
}

void draw_rect(unsigned long long, int x, int y, int width, int height, unsigned int color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const uint32_t pixel = leonos_color(color);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (valid_surface_point(x + column, y + row)) {
                g_surface.pixels[static_cast<uint32_t>(y + row) * g_surface.width + static_cast<uint32_t>(x + column)] = pixel;
            }
        }
    }
}

void draw_round_rect(unsigned long long handle, int x, int y, int width, int height,
                     unsigned int radius, unsigned int color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int resolved = radius > static_cast<unsigned int>(width / 2) ? width / 2 : static_cast<int>(radius);
    const int bounded = resolved > height / 2 ? height / 2 : resolved;
    if (bounded <= 0) {
        draw_rect(handle, x, y, width, height, color);
        return;
    }
    const uint32_t pixel = leonos_color(color);
    const int r2 = bounded * bounded;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            int dx = 0;
            int dy = 0;
            if (column < bounded) dx = bounded - column;
            else if (column >= width - bounded) dx = column - (width - bounded - 1);
            if (row < bounded) dy = bounded - row;
            else if (row >= height - bounded) dy = row - (height - bounded - 1);
            if ((dx == 0 || dy == 0 || dx * dx + dy * dy <= r2) && valid_surface_point(x + column, y + row)) {
                g_surface.pixels[static_cast<uint32_t>(y + row) * g_surface.width + static_cast<uint32_t>(x + column)] = pixel;
            }
        }
    }
}

void clear_draw_commands(unsigned long long) {}

void draw_text(unsigned long long, int x, int y, unsigned int color, unsigned int size,
               const stardustui::string &text)
{
    if (g_surface.pixels == nullptr) return;
    struct leonos_ui_surface surface = {g_surface.pixels, g_surface.width, g_surface.height, g_surface.width};
    (void)size;
    leonos_ui_text_transparent_clipped(&surface, static_cast<uint32_t>(x < 0 ? 0 : x),
                                       static_cast<uint32_t>(y < 0 ? 0 : y),
                                       g_surface.width, text.c_str(), leonos_color(color));
}

void draw_text_on_solid_background(unsigned long long, int x, int y, unsigned int color,
                                   unsigned int size, unsigned int background_color,
                                   const stardustui::string &text)
{
    if (g_surface.pixels == nullptr) return;
    struct leonos_ui_surface surface = {g_surface.pixels, g_surface.width, g_surface.height, g_surface.width};
    const uint32_t cell_h = size == 0 ? 16u : size;
    const uint32_t cell_w = cell_h / 2u < 6u ? 6u : cell_h / 2u;
    const uint32_t width = leonos_ui_text_width(text.c_str()) * cell_w / 8u + cell_w;
    leonos_ui_text_resized_clipped(&surface, static_cast<uint32_t>(x < 0 ? 0 : x),
                                   static_cast<uint32_t>(y < 0 ? 0 : y), width,
                                   text.c_str(), leonos_color(color),
                                   leonos_color(background_color), cell_w, cell_h);
}

unsigned int calc_text_width(const stardustui::string &text, unsigned int size)
{
    const uint32_t cell_h = size == 0 ? 16u : size;
    const uint32_t cell_w = cell_h / 2u < 6u ? 6u : cell_h / 2u;
    return leonos_ui_text_width(text.c_str()) * cell_w / 8u;
}

unsigned int calc_text_height(const stardustui::string &, unsigned int size)
{
    return size == 0 ? 16u : size;
}

void sleep_ms(unsigned long long ms)
{
    (void)syscall2(STARDUST_SYS_NANOSLEEP, static_cast<long>(ms), 0);
}

namespace stardustui {
bool set_text_font_path(const stardustui::string &) { return true; }
bool set_text_font_memory(const stardustui::File::byte *data, int size) { return size == 0 || data != nullptr; }
void clear_text_font() {}

bool file_exists_platform(const char *path)
{
    struct leonos_stat info{};
    return path != nullptr && stat(path, &info) == 0;
}

bool file_remove_platform(const char *path)
{
    return path != nullptr && unlink(path) == 0;
}

bool file_read_bytes_platform(const char *path, File::byte *&out_data, int &out_size)
{
    out_data = nullptr;
    out_size = 0;
    if (path == nullptr || path[0] == '\0') return false;
    struct leonos_stat info{};
    if (stat(path, &info) != 0 || info.size > 0x7FFFFFFFULL) return false;
    const int fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) return false;
    if (info.size == 0) { close(fd); return true; }
    File::byte *data = new File::byte[static_cast<int>(info.size)];
    if (data == nullptr) { close(fd); return false; }
    uint64_t done = 0;
    while (done < info.size) {
        long got = read(fd, data + done, static_cast<size_t>(info.size - done));
        if (got <= 0) { delete[] data; close(fd); return false; }
        done += static_cast<uint64_t>(got);
    }
    close(fd);
    out_data = data;
    out_size = static_cast<int>(info.size);
    return true;
}

bool file_write_bytes_platform(const char *path, const File::byte *data, int size)
{
    if (path == nullptr || size < 0 || (size > 0 && data == nullptr)) return false;
    const int fd = open(path, STARDUST_O_WRONLY | STARDUST_O_CREAT | STARDUST_O_TRUNC, 0666);
    if (fd < 0) return false;
    int done = 0;
    while (done < size) {
        long wrote = write(fd, data + done, static_cast<size_t>(size - done));
        if (wrote <= 0) { close(fd); return false; }
        done += static_cast<int>(wrote);
    }
    close(fd);
    return true;
}

bool file_append_text_platform(const char *path, const char *text, int length)
{
    if (path == nullptr || text == nullptr || length < 0) return false;
    const int fd = open(path, STARDUST_O_WRONLY | STARDUST_O_CREAT | STARDUST_O_APPEND, 0666);
    if (fd < 0) return false;
    int done = 0;
    while (done < length) {
        long wrote = write(fd, text + done, static_cast<size_t>(length - done));
        if (wrote <= 0) { close(fd); return false; }
        done += static_cast<int>(wrote);
    }
    close(fd);
    return true;
}

bool socket_connect_platform(const char *, unsigned short, long long &out_handle) { out_handle = 0; return false; }
bool socket_close_platform(long long) { return true; }
bool socket_send_platform(long long, const unsigned char *, int, int &out_sent) { out_sent = 0; return false; }
bool socket_receive_platform(long long, unsigned char *, int, int &out_received) { out_received = 0; return false; }
bool http_request_platform(const HttpRequest &, vector<unsigned char> &, string &out_error) { out_error.assign("network unavailable"); return false; }
}
