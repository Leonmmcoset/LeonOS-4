#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define MAX_WINDOWS 8
#define BUILTIN_WINDOWS 4
#define MAX_FB_W 1280
#define MAX_FB_H 800
#define TASKBAR_H LEONOS_UI_TASKBAR_H
#define TITLEBAR_H LEONOS_UI_TITLEBAR_H
#define MIN_W 180
#define MIN_H 96
#define CURSOR_W 16
#define CURSOR_H 16
#define START_MENU_H 206
#define START_MENU_DIRTY_H (START_MENU_H + TASKBAR_H + 8)
#define APP_WINDOW_SLOTS (MAX_WINDOWS - BUILTIN_WINDOWS)
#define APP_CLIENT_MAX_W 760
#define APP_CLIENT_MAX_H 540

#define DRAG_NONE 0
#define DRAG_MOVE 1
#define DRAG_RESIZE 2

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
    uint8_t close_requested;
    uint8_t visible;
    uint8_t minimized;
    uint8_t maximized;
};

struct rect {
    int x;
    int y;
    int w;
    int h;
};

static struct leonos_fb_info fb;
static struct desktop_window windows[MAX_WINDOWS];
static uint8_t z_order[MAX_WINDOWS];
static int active_window;
static int drag_window;
static uint8_t drag_mode;
static int drag_dx;
static int drag_dy;
static int drag_origin_x;
static int drag_origin_y;
static uint32_t drag_origin_w;
static uint32_t drag_origin_h;
static uint8_t previous_buttons;
static uint8_t start_menu_open;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint8_t cursor_visible;
static uint8_t full_redraw_pending;
static char app_titles[MAX_WINDOWS][48];
static char app_texts[MAX_WINDOWS][96];
static struct leonos_task_info task_infos[LEONOS_TASK_MAX];
static uint32_t task_info_count;
static uint64_t task_info_tick;
static unsigned long last_task_refresh;
static struct leonos_ui_surface ui;
static uint32_t window_client_buffers[APP_WINDOW_SLOTS][APP_CLIENT_MAX_W * APP_CLIENT_MAX_H];

static uint32_t screen[MAX_FB_W * MAX_FB_H];

static void send_app_event(uint8_t slot, uint32_t type, int32_t x, int32_t y,
                           int32_t dx, int32_t dy, uint8_t buttons,
                           uint8_t keycode, uint8_t pressed);


static uint32_t fb_w(void)
{
    return fb.width < MAX_FB_W ? fb.width : MAX_FB_W;
}

static uint32_t fb_h(void)
{
    return fb.height < MAX_FB_H ? fb.height : MAX_FB_H;
}

static void copy_text(char *dst, uint32_t dst_len, const char *src)
{
    uint32_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (src) {
        while (i + 1 < dst_len && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int hit_rect(uint32_t x, uint32_t y, int rx, int ry, uint32_t rw, uint32_t rh)
{
    return (int)x >= rx && (int)y >= ry && (int)x < rx + (int)rw && (int)y < ry + (int)rh;
}

static uint32_t taskbar_y(void)
{
    return fb_h() > TASKBAR_H ? fb_h() - TASKBAR_H : 0;
}

static uint32_t running_window_count(void)
{
    uint32_t count = 0;
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible) {
            ++count;
        }
    }
    return count;
}

static uint32_t taskbar_button_width(uint32_t count)
{
    uint32_t available = fb_w() > 112 ? fb_w() - 112 : 0;
    if (count == 0 || available == 0) {
        return 0;
    }
    uint32_t w = available / count;
    if (w > 150) {
        w = 150;
    }
    if (w < 64 && available >= count * 64) {
        w = 64;
    }
    return w;
}

static struct rect rect_make(int x, int y, int w, int h)
{
    struct rect r = {x, y, w, h};
    return r;
}

static struct rect window_rect(uint8_t id)
{
    if (id >= MAX_WINDOWS || !windows[id].visible || windows[id].minimized) {
        return rect_make(0, 0, 0, 0);
    }
    return rect_make(windows[id].x, windows[id].y, (int)windows[id].width, (int)windows[id].height);
}

static struct rect cursor_rect_at(uint32_t x, uint32_t y)
{
    return rect_make((int)x, (int)y, CURSOR_W, CURSOR_H);
}

static struct rect rect_union(struct rect a, struct rect b)
{
    if (a.w <= 0 || a.h <= 0) {
        return b;
    }
    if (b.w <= 0 || b.h <= 0) {
        return a;
    }
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1a = a.x + a.w;
    int y1a = a.y + a.h;
    int x1b = b.x + b.w;
    int y1b = b.y + b.h;
    int x1 = x1a > x1b ? x1a : x1b;
    int y1 = y1a > y1b ? y1a : y1b;
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

static struct rect rect_pad(struct rect r, int pad)
{
    return rect_make(r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2);
}

static struct rect rect_clip(struct rect r)
{
    int max_w = (int)fb_w();
    int max_h = (int)fb_h();
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.x + r.w > max_w) {
        r.w = max_w - r.x;
    }
    if (r.y + r.h > max_h) {
        r.h = max_h - r.y;
    }
    if (r.w < 0) {
        r.w = 0;
    }
    if (r.h < 0) {
        r.h = 0;
    }
    return r;
}

static int rect_intersects(struct rect a, struct rect b)
{
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
           a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    leonos_ui_pixel(&ui, x, y, color);
}

static void rect_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    leonos_ui_rect(&ui, x, y, w, h, color);
}

static void text_draw(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    leonos_ui_text(&ui, x, y, text, fg, bg);
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        append_char(buf, pos, cap, text[i]);
    }
}

static void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[20];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static void append_hex_fixed(char *buf, uint32_t *pos, uint32_t cap, uint64_t value, uint32_t digits)
{
    const char *hex = "0123456789abcdef";
    append_text(buf, pos, cap, "0x");
    for (int32_t shift = (int32_t)(digits * 4); shift > 0; shift -= 4) {
        append_char(buf, pos, cap, hex[(value >> (uint32_t)(shift - 4)) & 0xf]);
    }
}

static const char *task_state_name(uint32_t state)
{
    switch (state) {
    case 0:
        return "ready";
    case 1:
        return "run";
    case 2:
        return "sleep";
    case 3:
        return "exit";
    default:
        return "?";
    }
}

static const char *task_kind_name(uint32_t kind)
{
    return kind == 1 ? "user" : "kern";
}

static void task_line(char *buf, uint32_t cap, const struct leonos_task_info *task)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, task->pid);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->parent_pid);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task_state_name(task->state));
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task_kind_name(task->kind));
    append_text(buf, &pos, cap, "  ");
    append_hex_fixed(buf, &pos, cap, task->cr3, 8);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->wake_tick);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task->name);
}

static void refresh_task_snapshot(void)
{
    int count = leonos_task_snapshot(task_infos, LEONOS_TASK_MAX, &task_info_tick);
    task_info_count = count > 0 ? (uint32_t)count : 0;
    last_task_refresh = leonos_uptime_ms();
}

static void clamp_window(struct desktop_window *w)
{
    if (w->width < MIN_W) {
        w->width = MIN_W;
    }
    if (w->height < MIN_H) {
        w->height = MIN_H;
    }
    if (w->width > fb_w()) {
        w->width = fb_w();
    }
    uint32_t max_h = taskbar_y() > 8 ? taskbar_y() - 8 : fb_h();
    if (w->height > max_h) {
        w->height = max_h;
    }
    int max_x = (int)fb_w() - (int)w->width;
    int max_y = (int)taskbar_y() - (int)w->height;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }
    if (w->x < 0) {
        w->x = 0;
    }
    if (w->y < 0) {
        w->y = 0;
    }
    if (w->x > max_x) {
        w->x = max_x;
    }
    if (w->y > max_y) {
        w->y = max_y;
    }
}

static void bring_to_front(uint8_t id)
{
    int previous_active = active_window;
    uint8_t pos = MAX_WINDOWS;
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (z_order[i] == id) {
            pos = i;
            break;
        }
    }
    if (pos == MAX_WINDOWS) {
        return;
    }
    for (uint8_t i = pos; i + 1 < MAX_WINDOWS; ++i) {
        z_order[i] = z_order[i + 1];
    }
    z_order[MAX_WINDOWS - 1] = id;
    active_window = id;
    if (previous_active != active_window) {
        if (previous_active >= BUILTIN_WINDOWS && previous_active < MAX_WINDOWS && windows[previous_active].window_id) {
            send_app_event((uint8_t)previous_active, 3, 0, 0, 0, 0, 0, 0, 0);
        }
        if (active_window >= BUILTIN_WINDOWS && active_window < MAX_WINDOWS && windows[active_window].window_id) {
            send_app_event((uint8_t)active_window, 2, 0, 0, 0, 0, 0, 0, 0);
        }
        full_redraw_pending = 1;
    }
}

static int find_window_slot_by_window_id(uint32_t window_id)
{
    for (uint8_t i = BUILTIN_WINDOWS; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible && windows[i].window_id == window_id) {
            return i;
        }
    }
    return -1;
}

static uint32_t *client_buffer_for_slot(uint8_t slot)
{
    if (slot < BUILTIN_WINDOWS || slot >= MAX_WINDOWS) {
        return 0;
    }
    return window_client_buffers[slot - BUILTIN_WINDOWS];
}

static uint32_t window_body_width(const struct desktop_window *w)
{
    return w && w->width > 16 ? w->width - 16 : 0;
}

static uint32_t window_body_height(const struct desktop_window *w)
{
    return w && w->height > TITLEBAR_H + 18 ? w->height - TITLEBAR_H - 18 : 0;
}

static void remove_window_slot(uint8_t slot)
{
    if (windows[slot].window_id) {
        printf("[desktop.elf] remove window wid=%d title=%s\n", windows[slot].window_id, windows[slot].title);
    }
    windows[slot].visible = 0;
    windows[slot].minimized = 0;
    windows[slot].maximized = 0;
    windows[slot].close_requested = 0;
    windows[slot].owner_pid = 0;
    windows[slot].window_id = 0;
    windows[slot].client_width = 0;
    windows[slot].client_height = 0;
    app_titles[slot][0] = 0;
    app_texts[slot][0] = 0;
    full_redraw_pending = 1;
}

static void request_close_window(uint8_t slot)
{
    if (slot >= MAX_WINDOWS || !windows[slot].visible) {
        return;
    }
    if (windows[slot].window_id) {
        windows[slot].close_requested = 1;
        printf("[desktop.elf] request close wid=%d title=%s\n", windows[slot].window_id, windows[slot].title);
        send_app_event(slot, 1, 0, 0, 0, 0, 0, 0, 0);
        full_redraw_pending = 1;
        return;
    }
    windows[slot].visible = 0;
    windows[slot].minimized = 0;
    full_redraw_pending = 1;
}

static void send_app_event(uint8_t slot, uint32_t type, int32_t x, int32_t y,
                           int32_t dx, int32_t dy, uint8_t buttons,
                           uint8_t keycode, uint8_t pressed)
{
    struct leonos_gui_app_event event;
    uint32_t client_w;
    uint32_t client_h;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || !windows[slot].window_id) {
        return;
    }
    client_w = window_body_width(&windows[slot]);
    client_h = window_body_height(&windows[slot]);
    event.window_id = windows[slot].window_id;
    event.type = type;
    event.x = x;
    event.y = y;
    event.dx = dx;
    event.dy = dy;
    event.width = client_w;
    event.height = client_h;
    event.buttons = buttons;
    event.keycode = keycode;
    event.pressed = pressed;
    event.reserved = 0;
    leonos_gui_send_app_event(&event);
}

static void fetch_window_surface(uint8_t slot)
{
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    uint32_t cap_w;
    uint32_t cap_h;
    uint32_t *buffer = client_buffer_for_slot(slot);
    if (slot >= MAX_WINDOWS || !windows[slot].visible || !windows[slot].window_id) {
        return;
    }
    if (!buffer) {
        return;
    }
    cap_w = window_body_width(&windows[slot]);
    cap_h = window_body_height(&windows[slot]);
    if (cap_w > APP_CLIENT_MAX_W) {
        cap_w = APP_CLIENT_MAX_W;
    }
    if (cap_h > APP_CLIENT_MAX_H) {
        cap_h = APP_CLIENT_MAX_H;
    }
    if (cap_w == 0 || cap_h == 0) {
        return;
    }
    if (leonos_gui_fetch_window(windows[slot].window_id, cap_w, cap_h, APP_CLIENT_MAX_W,
                                buffer, &out_w, &out_h) > 0) {
        windows[slot].client_width = out_w;
        windows[slot].client_height = out_h;
        full_redraw_pending = 1;
    }
}

static void draw_app_surface(uint8_t id, uint32_t body_x, uint32_t body_y,
                             uint32_t body_w, uint32_t body_h)
{
    struct desktop_window *w = &windows[id];
    uint32_t copy_w;
    uint32_t copy_h;
    uint32_t *buffer = client_buffer_for_slot(id);
    if (!w->client_width || !w->client_height) {
        text_draw(body_x + 16, body_y + 18, w->app_text ? w->app_text : "Application window",
                  LEONOS_UI_BLACK, w->body_color);
        return;
    }
    if (!buffer) {
        return;
    }
    copy_w = w->client_width < body_w ? w->client_width : body_w;
    copy_h = w->client_height < body_h ? w->client_height : body_h;
    for (uint32_t yy = 0; yy < copy_h; ++yy) {
        for (uint32_t xx = 0; xx < copy_w; ++xx) {
            put_pixel(body_x + xx, body_y + yy, buffer[(uint64_t)yy * APP_CLIENT_MAX_W + xx]);
        }
    }
}

static int window_is_ui_demo(const struct desktop_window *w)
{
    return w && text_eq(w->title, "UI Components");
}

static void draw_ui_demo_label(uint32_t x, uint32_t y, const char *label, uint32_t bg)
{
    leonos_ui_text(&ui, x, y, label, LEONOS_UI_BLACK, bg);
}

static void draw_ui_demo_gallery(uint32_t body_x, uint32_t body_y,
                                 uint32_t body_w, uint32_t body_h,
                                 uint32_t bg)
{
    uint32_t pad = 14;
    if (body_w < 300 || body_h < 220) {
        text_draw(body_x + 10, body_y + 12, "Resize window to view all components", LEONOS_UI_BLACK, bg);
        return;
    }

    uint32_t header_h = 32;
    uint32_t left_x = body_x + pad;
    uint32_t right_x = body_x + body_w / 2 + 6;
    uint32_t col_w = body_w / 2 > pad * 2 ? body_w / 2 - pad * 2 : 120;
    uint32_t top = body_y + pad;

    text_draw(left_x, top, "LeonOS UI Component Library", LEONOS_UI_BLACK, bg);
    text_draw(left_x, top + 18, "Buttons, inputs, lists, menus, panels, windows", LEONOS_UI_DARK, bg);

    uint32_t y = top + header_h + 8;
    draw_ui_demo_label(left_x, y, "Buttons", bg);
    leonos_ui_button(&ui, left_x, y + 18, 74, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(&ui, left_x + 84, y + 18, 88, LEONOS_UI_BUTTON_H, "Pressed", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(&ui, left_x + 182, y + 18, 92, LEONOS_UI_BUTTON_H, "Disabled", LEONOS_UI_BUTTON_DISABLED);

    y += 56;
    draw_ui_demo_label(left_x, y, "Checks and Fields", bg);
    leonos_ui_checkbox(&ui, left_x, y + 20, "Checked", 1, 0);
    leonos_ui_checkbox(&ui, left_x, y + 44, "Unchecked", 0, 0);
    leonos_ui_text_field(&ui, left_x + 136, y + 18, col_w > 146 ? col_w - 146 : 120, "Sample text", 0);

    y += 86;
    draw_ui_demo_label(left_x, y, "Progress", bg);
    leonos_ui_progress(&ui, left_x, y + 20, col_w > 24 ? col_w - 24 : 160, 18, 65, 100);
    text_draw(left_x, y + 46, "65 percent", LEONOS_UI_DARK, bg);

    y += 76;
    draw_ui_demo_label(left_x, y, "Panel", bg);
    leonos_ui_panel(&ui, left_x, y + 18, col_w > 24 ? col_w - 24 : 160, 54, LEONOS_UI_LIGHT);
    text_draw(left_x + 10, y + 34, "Inset content panel", LEONOS_UI_BLACK, LEONOS_UI_LIGHT);

    y = top + header_h + 8;
    draw_ui_demo_label(right_x, y, "Menu", bg);
    leonos_ui_menu(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, 90);
    leonos_ui_menu_item(&ui, right_x + 34, y + 28, col_w > 54 ? col_w - 54 : 140, "Normal item", 0);
    leonos_ui_menu_item(&ui, right_x + 34, y + 52, col_w > 54 ? col_w - 54 : 140, "Selected item", LEONOS_UI_MENU_SELECTED);
    leonos_ui_menu_item(&ui, right_x + 34, y + 76, col_w > 54 ? col_w - 54 : 140, "", LEONOS_UI_MENU_SEPARATOR);

    y += 122;
    draw_ui_demo_label(right_x, y, "List", bg);
    leonos_ui_list_header(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, "Name        State");
    leonos_ui_list_row(&ui, right_x, y + 46, col_w > 16 ? col_w - 16 : 180, "Button      ready", 0);
    leonos_ui_list_row(&ui, right_x, y + 70, col_w > 16 ? col_w - 16 : 180, "TextField   selected", LEONOS_UI_MENU_SELECTED);
    leonos_ui_list_row(&ui, right_x, y + 94, col_w > 16 ? col_w - 16 : 180, "Progress    ready", 0);

    y += 138;
    if (y + 54 < body_y + body_h) {
        draw_ui_demo_label(right_x, y, "Window and Taskbar", bg);
        leonos_ui_window_button(&ui, right_x, y + 20, '_', 0);
        leonos_ui_window_button(&ui, right_x + 24, y + 20, 'M', 0);
        leonos_ui_window_button(&ui, right_x + 48, y + 20, 'X', 0);
        leonos_ui_taskbar_button(&ui, right_x + 86, y + 18,
                                 col_w > 116 ? col_w - 116 : 110, "Task Button",
                                 LEONOS_UI_BUTTON_ACTIVE);
    }
}

static void draw_window(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    if (!w->visible || w->minimized) {
        return;
    }

    struct leonos_ui_window_parts parts;
    leonos_ui_window_ex(&ui, (uint32_t)w->x, (uint32_t)w->y, w->width, w->height, w->title,
                        w->maximized ? 'r' : 'M',
                        active_window == id ? LEONOS_UI_WINDOW_ACTIVE : 0, &parts);

    uint32_t body_x = (uint32_t)parts.body.x;
    uint32_t body_y = (uint32_t)parts.body.y;
    uint32_t body_w = parts.body.w;
    uint32_t body_h = parts.body.h;
    leonos_ui_inset(&ui, body_x, body_y, body_w, body_h, w->body_color);

    if (id == 0) {
        text_draw(body_x + 16, body_y + 18, "Ring-3 desktop shadow blit", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "Dirty redraw reduces flicker", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "Drag and resize window", 0x00000000, w->body_color);
    } else if (id == 1) {
        text_draw(body_x + 16, body_y + 18, "0:/", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "boot  system  userland", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "FAT32 root drive view", 0x00000000, w->body_color);
    } else if (id == 2) {
        text_draw(body_x + 16, body_y + 18, "Settings", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "Win98 style controls", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "Window state in userland", 0x00000000, w->body_color);
    } else if (id == 3) {
        char line[112];
        uint32_t pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), "tick=");
        append_dec(line, &pos, sizeof(line), task_info_tick);
        append_text(line, &pos, sizeof(line), " tasks=");
        append_dec(line, &pos, sizeof(line), task_info_count);
        text_draw(body_x + 10, body_y + 12, line, 0x00000000, w->body_color);
        leonos_ui_list_header(&ui, body_x + 8, body_y + 30,
                              body_w > 16 ? body_w - 16 : 0,
                              "PID PPID STATE KIND CR3        WAKE NAME");
        uint32_t max_rows = (body_h > 70) ? (body_h - 70) / (LEONOS_FONT_H + 2) : 0;
        if (max_rows > task_info_count) {
            max_rows = task_info_count;
        }
        if (max_rows > 10) {
            max_rows = 10;
        }
        for (uint32_t i = 0; i < max_rows; ++i) {
            task_line(line, sizeof(line), &task_infos[i]);
            leonos_ui_list_row(&ui, body_x + 8, body_y + 60 + i * (LEONOS_FONT_H + 4),
                               body_w > 16 ? body_w - 16 : 0, line, 0);
        }
    } else if (w->window_id) {
        draw_app_surface(id, body_x, body_y, body_w, body_h);
    } else if (window_is_ui_demo(w)) {
        draw_ui_demo_gallery(body_x, body_y, body_w, body_h, w->body_color);
    } else {
        text_draw(body_x + 16, body_y + 18, w->app_text ? w->app_text : "Application window", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "Process window via GUI IPC", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "App exited, desktop owns surface", 0x00000000, w->body_color);
    }

    rect_fill((uint32_t)w->x + w->width - 13, (uint32_t)w->y + w->height - 13, 9, 1, 0x00808080);
    rect_fill((uint32_t)w->x + w->width - 9, (uint32_t)w->y + w->height - 17, 1, 9, 0x00808080);
    rect_fill((uint32_t)w->x + w->width - 10, (uint32_t)w->y + w->height - 10, 6, 1, 0x00000000);
    rect_fill((uint32_t)w->x + w->width - 6, (uint32_t)w->y + w->height - 14, 1, 6, 0x00000000);
}

static void draw_taskbar_button(uint8_t id, uint32_t x, uint32_t y, uint32_t w)
{
    if (!windows[id].visible) {
        return;
    }
    int active = active_window == id && !windows[id].minimized;
    leonos_ui_taskbar_button(&ui, x, y, w > 8 ? w - 8 : w, windows[id].title,
                             active ? LEONOS_UI_BUTTON_ACTIVE : 0);
}

static void draw_start_menu(void)
{
    if (!start_menu_open) {
        return;
    }
    uint32_t y = taskbar_y();
    uint32_t menu_h = START_MENU_H;
    uint32_t menu_y = y > menu_h ? y - menu_h : 0;
    leonos_ui_menu(&ui, 6, menu_y, 226, menu_h);
    leonos_ui_menu_item(&ui, 40, menu_y + 10, 182, "Desktop Server", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 36, 182, "File Manager", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 62, 182, "Settings", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 88, 182, "Task Manager", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 114, 182, "", LEONOS_UI_MENU_SEPARATOR);
    leonos_ui_menu_item(&ui, 40, menu_y + 122, 182, "Programs > Hello", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 148, 182, "UI Components", 0);
    leonos_ui_menu_item(&ui, 40, menu_y + 174, 182, "Terminal", 0);
}

static void draw_cursor_shape(uint32_t x, uint32_t y)
{
    if (x + CURSOR_W > fb_w()) {
        x = fb_w() > CURSOR_W ? fb_w() - CURSOR_W : 0;
    }
    if (y + CURSOR_H > fb_h()) {
        y = fb_h() > CURSOR_H ? fb_h() - CURSOR_H : 0;
    }
    for (uint32_t i = 0; i < 15; ++i) {
        put_pixel(x, y + i, 0x00000000);
        if (i < 11) {
            put_pixel(x + i, y + i, 0x00000000);
        }
    }
    for (uint32_t i = 1; i < 10; ++i) {
        rect_fill(x + 1, y + i, i, 1, 0x00ffffff);
    }
    rect_fill(x + 5, y + 11, 3, 1, 0x00000000);
    rect_fill(x + 6, y + 12, 3, 1, 0x00000000);
    rect_fill(x + 7, y + 13, 3, 1, 0x00000000);
}

static void redraw_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }

    rect_fill((uint32_t)dirty.x, (uint32_t)dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h, 0x00008080);

    if (rect_intersects(dirty, rect_make(8, 32, 72, 58))) {
        rect_fill(24, 32, 48, 38, 0x00c0c0c0);
        rect_fill(24, 32, 48, 2, 0x00ffffff);
        rect_fill(24, 32, 2, 38, 0x00ffffff);
        rect_fill(70, 32, 2, 38, 0x00000000);
        rect_fill(24, 68, 48, 2, 0x00000000);
        text_draw(16, 78, "0:/", 0x00ffffff, 0x00008080);
    }
    if (rect_intersects(dirty, rect_make(8, 112, 72, 58))) {
        rect_fill(24, 112, 48, 38, 0x00c0c0c0);
        rect_fill(24, 112, 48, 2, 0x00ffffff);
        rect_fill(24, 112, 2, 38, 0x00ffffff);
        rect_fill(70, 112, 2, 38, 0x00000000);
        rect_fill(24, 148, 48, 2, 0x00000000);
        text_draw(8, 158, "Apps", 0x00ffffff, 0x00008080);
    }

    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        draw_window(z_order[i]);
    }

    uint32_t tb_y = taskbar_y();
    if ((uint32_t)(dirty.y + dirty.h) >= tb_y) {
        leonos_ui_taskbar(&ui, tb_y, TASKBAR_H);
        leonos_ui_button(&ui, 6, tb_y + 5, 86, LEONOS_UI_BUTTON_H, "Start",
                         start_menu_open ? LEONOS_UI_BUTTON_PRESSED : 0);
        uint32_t x = 106;
        uint32_t button_w = taskbar_button_width(running_window_count());
        for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
            if (windows[i].visible && button_w > 0) {
                draw_taskbar_button(i, x, tb_y + 5, button_w);
                x += button_w;
            }
        }
    }

    draw_start_menu();
    if (cursor_visible) {
        draw_cursor_shape(cursor_x, cursor_y);
    }
}

static void flush_region(struct rect dirty)
{
    dirty = rect_clip(dirty);
    if (dirty.w <= 0 || dirty.h <= 0) {
        return;
    }
    const uint32_t *src = screen + (uint64_t)dirty.y * MAX_FB_W + dirty.x;
    leonos_fb_blit((uint32_t)dirty.x, (uint32_t)dirty.y, (uint32_t)dirty.w, (uint32_t)dirty.h, MAX_FB_W, src);
}

static void repaint_and_flush(struct rect dirty)
{
    dirty = rect_clip(dirty);
    redraw_region(dirty);
    flush_region(dirty);
}

static void redraw_all(void)
{
    struct rect full = rect_make(0, 0, (int)fb_w(), (int)fb_h());
    redraw_region(full);
    flush_region(full);
    full_redraw_pending = 0;
}

static int hit_window(uint32_t x, uint32_t y)
{
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        struct desktop_window *w = &windows[id];
        if (w->visible && !w->minimized && hit_rect(x, y, w->x, w->y, w->width, w->height)) {
            return id;
        }
    }
    return -1;
}

static void minimize_window(uint8_t id)
{
    windows[id].minimized = 1;
    if (active_window == id) {
        active_window = -1;
        for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
            uint8_t next = z_order[zi];
            if (windows[next].visible && !windows[next].minimized) {
                active_window = next;
                break;
            }
        }
    }
    full_redraw_pending = 1;
}

static void restore_window(uint8_t id)
{
    windows[id].visible = 1;
    windows[id].minimized = 0;
    bring_to_front(id);
    clamp_window(&windows[id]);
    if (windows[id].window_id) {
        send_app_event(id, 4, 0, 0, 0, 0, 0, 0, 0);
    }
    full_redraw_pending = 1;
}

static void toggle_maximize(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    if (w->maximized) {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->width = w->restore_width;
        w->height = w->restore_height;
        w->maximized = 0;
        clamp_window(w);
        if (w->window_id) {
            send_app_event(id, 4, 0, 0, 0, 0, 0, 0, 0);
        }
        full_redraw_pending = 1;
        return;
    }
    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_width = w->width;
    w->restore_height = w->height;
    w->x = 0;
    w->y = 0;
    w->width = fb_w();
    w->height = taskbar_y();
    w->maximized = 1;
    if (w->window_id) {
        send_app_event(id, 4, 0, 0, 0, 0, 0, 0, 0);
    }
    full_redraw_pending = 1;
}

static void open_app_window_from_msg(const struct leonos_gui_window_msg *msg)
{
    int existing;
    if (!msg) {
        return;
    }
    if (msg->type == 2) {
        existing = find_window_slot_by_window_id(msg->window_id);
        if (existing >= 0) {
            fetch_window_surface((uint8_t)existing);
        }
        return;
    }
    if (msg->type == 3) {
        existing = find_window_slot_by_window_id(msg->window_id);
        if (existing >= 0) {
            remove_window_slot((uint8_t)existing);
        }
        return;
    }
    if (msg->type != 1) {
        return;
    }
    uint8_t slot = MAX_WINDOWS;
    for (uint8_t i = BUILTIN_WINDOWS; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_WINDOWS) {
        slot = BUILTIN_WINDOWS;
    }

    copy_text(app_titles[slot], sizeof(app_titles[slot]), msg->title[0] ? msg->title : "Application");
    copy_text(app_texts[slot], sizeof(app_texts[slot]), msg->text[0] ? msg->text : "Application window");
    uint32_t width = msg->width + 16;
    uint32_t height = msg->height + TITLEBAR_H + 18;
    if (width < MIN_W) {
        width = MIN_W;
    }
    if (height < MIN_H) {
        height = MIN_H;
    }
    uint32_t offset = (slot - BUILTIN_WINDOWS) * 34;
    windows[slot] = (struct desktop_window){
        .x = 220 + (int)offset,
        .y = 170 + (int)offset,
        .width = width,
        .height = height,
        .restore_x = 220 + (int)offset,
        .restore_y = 170 + (int)offset,
        .restore_width = width,
        .restore_height = height,
        .title = app_titles[slot],
        .app_text = app_texts[slot],
        .body_color = 0x00ffffff,
        .owner_pid = msg->pid,
        .window_id = msg->window_id,
        .client_width = 0,
        .client_height = 0,
        .close_requested = 0,
        .visible = 1,
        .minimized = 0,
        .maximized = 0,
    };
    clamp_window(&windows[slot]);
    bring_to_front(slot);
    send_app_event(slot, 4, 0, 0, 0, 0, 0, 0, 0);
    fetch_window_surface(slot);
    printf("[desktop.elf] GUI window from pid=%d wid=%d title=%s\n", msg->pid, msg->window_id, windows[slot].title);
}

static void handle_start_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (hit_rect(x, y, 6, (int)tb_y + 5, 86, 24)) {
        start_menu_open = !start_menu_open;
        full_redraw_pending = 1;
        return;
    }
    if (!start_menu_open) {
        return;
    }
    uint32_t menu_h = START_MENU_H;
    uint32_t menu_y = tb_y > menu_h ? tb_y - menu_h : 0;
    if (!hit_rect(x, y, 6, (int)menu_y, 226, menu_h)) {
        start_menu_open = 0;
        full_redraw_pending = 1;
        return;
    }
    if (hit_rect(x, y, 40, (int)menu_y + 8, 166, 24)) {
        restore_window(0);
    } else if (hit_rect(x, y, 40, (int)menu_y + 34, 166, 24)) {
        int pid = execve("0:/userland/fileman.elf", 0, 0);
        printf("[desktop.elf] spawn fileman.elf pid=%d\n", pid);
    } else if (hit_rect(x, y, 40, (int)menu_y + 60, 166, 24)) {
        restore_window(2);
    } else if (hit_rect(x, y, 40, (int)menu_y + 86, 166, 24)) {
        int pid = execve("0:/userland/taskmgr.elf", 0, 0);
        printf("[desktop.elf] spawn taskmgr.elf pid=%d\n", pid);
    } else if (hit_rect(x, y, 40, (int)menu_y + 120, 182, 24)) {
        int pid = execve("0:/userland/hello.elf", 0, 0);
        printf("[desktop.elf] spawn hello.elf pid=%d\n", pid);
    } else if (hit_rect(x, y, 40, (int)menu_y + 146, 182, 24)) {
        int pid = execve("0:/userland/uidemo.elf", 0, 0);
        printf("[desktop.elf] spawn uidemo.elf pid=%d\n", pid);
    } else if (hit_rect(x, y, 40, (int)menu_y + 172, 182, 24)) {
        int pid = execve("0:/userland/terminal.elf", 0, 0);
        printf("[desktop.elf] spawn terminal.elf pid=%d\n", pid);
    }
    start_menu_open = 0;
    full_redraw_pending = 1;
}

static int hit_start_menu_area(uint32_t x, uint32_t y)
{
    if (!start_menu_open) {
        return 0;
    }
    uint32_t tb_y = taskbar_y();
    uint32_t menu_y = tb_y > START_MENU_H ? tb_y - START_MENU_H : 0;
    return hit_rect(x, y, 6, (int)menu_y, 226, START_MENU_H);
}

static int handle_taskbar_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (!hit_rect(x, y, 0, (int)tb_y, fb_w(), TASKBAR_H) && !hit_start_menu_area(x, y)) {
        return 0;
    }
    handle_start_click(x, y);
    if (hit_rect(x, y, 6, (int)tb_y + 5, 86, 24) || hit_start_menu_area(x, y)) {
        return 1;
    }
    uint32_t bx = 106;
    uint32_t button_w = taskbar_button_width(running_window_count());
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible && button_w > 0) {
            uint32_t hit_w = button_w > 8 ? button_w - 8 : button_w;
            if (hit_rect(x, y, (int)bx, (int)tb_y + 5, hit_w, 24)) {
                if (active_window == i && !windows[i].minimized) {
                    minimize_window(i);
                } else {
                    restore_window(i);
                }
                start_menu_open = 0;
                return 1;
            }
            bx += button_w;
        }
    }
    start_menu_open = 0;
    return 1;
}

static void handle_mouse(uint32_t x, uint32_t y, uint8_t buttons)
{
    if (x >= fb_w()) {
        x = fb_w() ? fb_w() - 1 : 0;
    }
    if (y >= fb_h()) {
        y = fb_h() ? fb_h() - 1 : 0;
    }

    uint8_t left = buttons & 1;
    uint8_t was_left = previous_buttons & 1;
    int hover_id = hit_window(x, y);
    struct rect dirty = cursor_visible ? cursor_rect_at(cursor_x, cursor_y) : rect_make(0, 0, 0, 0);
    dirty = rect_union(dirty, cursor_rect_at(x, y));

    if (hover_id >= BUILTIN_WINDOWS && hover_id < MAX_WINDOWS && windows[hover_id].window_id &&
        !(left && drag_window >= 0)) {
        struct desktop_window *hover = &windows[hover_id];
        int client_x = (int)x - (hover->x + 8);
        int client_y = (int)y - (hover->y + TITLEBAR_H + 10);
        if (client_x >= 0 && client_y >= 0) {
            send_app_event((uint8_t)hover_id, 5, client_x, client_y,
                           (int32_t)x - (int32_t)cursor_x, (int32_t)y - (int32_t)cursor_y,
                           buttons, 0, left ? 1 : 0);
            if (buttons != previous_buttons) {
                send_app_event((uint8_t)hover_id, 6, client_x, client_y,
                               0, 0, buttons, 0, left ? 1 : 0);
            }
        }
    }

    if (left && drag_window >= 0) {
        struct rect old_rect = window_rect((uint8_t)drag_window);
        struct desktop_window *w = &windows[drag_window];
        if (drag_mode == DRAG_MOVE) {
            w->x = (int)x - drag_dx;
            w->y = (int)y - drag_dy;
            clamp_window(w);
            dirty = rect_union(dirty, rect_pad(old_rect, 4));
            dirty = rect_union(dirty, rect_pad(window_rect((uint8_t)drag_window), 4));
        } else if (drag_mode == DRAG_RESIZE) {
            int new_w = (int)drag_origin_w + (int)x - drag_origin_x;
            int new_h = (int)drag_origin_h + (int)y - drag_origin_y;
            w->width = new_w < MIN_W ? MIN_W : (uint32_t)new_w;
            w->height = new_h < MIN_H ? MIN_H : (uint32_t)new_h;
            clamp_window(w);
            if (w->window_id) {
                send_app_event((uint8_t)drag_window, 4, 0, 0, 0, 0, 0, 0, 0);
            }
            dirty = rect_union(dirty, rect_pad(old_rect, 4));
            dirty = rect_union(dirty, rect_pad(window_rect((uint8_t)drag_window), 4));
        }
    }

    if (!left && was_left) {
        drag_window = -1;
        drag_mode = DRAG_NONE;
    }

    if (left && !was_left) {
        if (handle_taskbar_click(x, y)) {
            int menu_top = (int)taskbar_y() - START_MENU_H - 8;
            if (menu_top < 0) {
                menu_top = 0;
            }
            dirty = rect_union(dirty, rect_make(0, menu_top, (int)fb_w(), START_MENU_DIRTY_H));
        } else {
            int id = hover_id;
            if (id >= 0) {
                struct rect old_rect = window_rect((uint8_t)id);
                struct desktop_window *w = &windows[id];
                bring_to_front((uint8_t)id);
                start_menu_open = 0;
                dirty = rect_union(dirty, rect_pad(old_rect, 4));
                uint32_t bx = (uint32_t)w->x + w->width - 64;
                uint32_t by = (uint32_t)w->y + 7;
                if (hit_rect(x, y, (int)bx, (int)by, 18, 20)) {
                    minimize_window((uint8_t)id);
                } else if (hit_rect(x, y, (int)bx + 20, (int)by, 18, 20)) {
                    toggle_maximize((uint8_t)id);
                } else if (hit_rect(x, y, (int)bx + 40, (int)by, 18, 20)) {
                    request_close_window((uint8_t)id);
                } else if (hit_rect(x, y, w->x + (int)w->width - 18, w->y + (int)w->height - 18, 18, 18) && !w->maximized) {
                    drag_window = id;
                    drag_mode = DRAG_RESIZE;
                    drag_origin_x = (int)x;
                    drag_origin_y = (int)y;
                    drag_origin_w = w->width;
                    drag_origin_h = w->height;
                } else if (hit_rect(x, y, w->x + 4, w->y + 4, w->width > 8 ? w->width - 8 : 0, TITLEBAR_H) && !w->maximized) {
                    drag_window = id;
                    drag_mode = DRAG_MOVE;
                    drag_dx = (int)x - w->x;
                    drag_dy = (int)y - w->y;
                } else if (w->window_id) {
                    int client_x = (int)x - (w->x + 8);
                    int client_y = (int)y - (w->y + TITLEBAR_H + 10);
                    if (client_x >= 0 && client_y >= 0) {
                        send_app_event((uint8_t)id, 6, client_x, client_y, 0, 0, buttons, 0, left ? 1 : 0);
                    }
                }
                dirty = rect_union(dirty, rect_pad(window_rect((uint8_t)id), 4));
                dirty = rect_union(dirty, rect_make(0, (int)taskbar_y(), (int)fb_w(), TASKBAR_H));
            } else if (start_menu_open) {
                start_menu_open = 0;
                int menu_top = (int)taskbar_y() - START_MENU_H - 8;
                if (menu_top < 0) {
                    menu_top = 0;
                }
                dirty = rect_union(dirty, rect_make(0, menu_top, (int)fb_w(), START_MENU_DIRTY_H));
            }
        }
    }

    previous_buttons = buttons;
    cursor_x = x;
    cursor_y = y;
    cursor_visible = 1;
    if (full_redraw_pending) {
        redraw_all();
    } else {
        repaint_and_flush(rect_pad(dirty, 2));
    }
}

static void init_desktop(void)
{
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        windows[i] = (struct desktop_window){0};
        z_order[i] = i;
    }
    windows[0] = (struct desktop_window){.x = 120, .y = 84, .width = 420, .height = 220,
                                         .restore_x = 120, .restore_y = 84,
                                         .restore_width = 420, .restore_height = 220,
                                         .title = "Desktop Server", .body_color = 0x00c0c0c0,
                                         .visible = 1};
    windows[1] = (struct desktop_window){.x = 190, .y = 150, .width = 360, .height = 190,
                                         .restore_x = 190, .restore_y = 150,
                                         .restore_width = 360, .restore_height = 190,
                                         .title = "File Manager", .body_color = 0x00ffffff};
    windows[2] = (struct desktop_window){.x = 270, .y = 210, .width = 320, .height = 170,
                                         .restore_x = 270, .restore_y = 210,
                                         .restore_width = 320, .restore_height = 170,
                                         .title = "Settings", .body_color = 0x00dfdfdf};
    windows[3] = (struct desktop_window){.x = 90, .y = 118, .width = 620, .height = 300,
                                         .restore_x = 90, .restore_y = 118,
                                         .restore_width = 620, .restore_height = 300,
                                         .title = "Task Manager", .body_color = 0x00ffffff};
    z_order[0] = 4;
    z_order[1] = 5;
    z_order[2] = 6;
    z_order[3] = 1;
    z_order[4] = 2;
    z_order[5] = 3;
    z_order[6] = 0;
    active_window = 0;
    drag_window = -1;
    drag_mode = DRAG_NONE;
    cursor_x = 320;
    cursor_y = 240;
    cursor_visible = 1;
    full_redraw_pending = 1;
    refresh_task_snapshot();
    redraw_all();
}

int main(void)
{
    puts("[desktop.elf] Ring-3 Win98-style window server starting");

    int version = leonos_gui_connect();
    printf("[desktop.elf] GUI protocol version=%d\n", version);
    printf("[desktop.elf] pid=%d service=window-server\n", getpid());
    if (leonos_fb_info(&fb) < 0) {
        puts("[desktop.elf] framebuffer unavailable");
        for (;;) {
            sleep_ms(1000);
        }
    }
    printf("[desktop.elf] framebuffer %dx%d bpp=%d\n", fb.width, fb.height, fb.bpp);
    if (fb.width > MAX_FB_W || fb.height > MAX_FB_H) {
        printf("[desktop.elf] framebuffer clipped to %dx%d shadow buffer\n", MAX_FB_W, MAX_FB_H);
    }
    leonos_ui_bind(&ui, screen, fb_w(), fb_h(), MAX_FB_W);

    init_desktop();
    puts("[desktop.elf] Ring-3 desktop uses shadow framebuffer blit");

    unsigned long last_log = 0;
    for (;;) {
        struct leonos_gui_window_msg window_msg;
        if (leonos_gui_poll_window(&window_msg) > 0) {
            open_app_window_from_msg(&window_msg);
        }

        struct leonos_input_event event;
        int got = leonos_gui_next_event(&event);
        if (got > 0) {
            if (event.type == LEONOS_INPUT_MOUSE) {
                handle_mouse((uint32_t)event.x, (uint32_t)event.y, event.buttons);
            } else if (event.type == LEONOS_INPUT_KEYBOARD && event.pressed) {
                if (active_window >= BUILTIN_WINDOWS && active_window < MAX_WINDOWS &&
                    windows[active_window].window_id) {
                    send_app_event((uint8_t)active_window, 7, 0, 0, 0, 0, 0, event.keycode, 1);
                }
                printf("[desktop.elf] key scancode=%x\n", event.keycode);
            }
        } else if (full_redraw_pending) {
            redraw_all();
        }

        unsigned long now = leonos_uptime_ms();
        if (now - last_task_refresh >= 500) {
            refresh_task_snapshot();
            if (windows[3].visible && !windows[3].minimized) {
                redraw_all();
            }
        }
        if (now - last_log >= 5000) {
            puts("[desktop.elf] window server alive");
            last_log = now;
        }
        sleep_ms(10);
    }
}
