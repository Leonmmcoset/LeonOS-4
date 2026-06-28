#include "desktop.h"

void clamp_window_size(struct desktop_window *w)
{
    if (w->width < MIN_W) {
        w->width = MIN_W;
    }
    if (w->height < MIN_H) {
        w->height = MIN_H;
    }
    uint32_t max_w = fb_w() ? fb_w() * 2 : MAX_FB_W;
    uint32_t max_h = taskbar_y() ? taskbar_y() * 2 : fb_h() * 2;
    if (max_w < MIN_W) {
        max_w = MIN_W;
    }
    if (max_h < MIN_H) {
        max_h = MIN_H;
    }
    if (w->width > max_w) {
        w->width = max_w;
    }
    if (w->height > max_h) {
        w->height = max_h;
    }
}

void clamp_window_position_recoverable(struct desktop_window *w)
{
    uint32_t visible_w = min_u32(w->width, WINDOW_RECOVERABLE_W);
    uint32_t visible_title_h = min_u32(TITLEBAR_H, WINDOW_RECOVERABLE_TITLEBAR_H);
    int min_x = -(int)w->width + (int)visible_w;
    int max_x = (int)fb_w() - (int)visible_w;
    int min_y = -(int)TITLEBAR_H + (int)visible_title_h;
    int max_y = (int)taskbar_y() - (int)visible_title_h;

    if (max_x < min_x) {
        max_x = min_x;
    }
    if (max_y < min_y) {
        max_y = min_y;
    }
    if (w->x < min_x) {
        w->x = min_x;
    } else if (w->x > max_x) {
        w->x = max_x;
    }
    if (w->y < min_y) {
        w->y = min_y;
    } else if (w->y > max_y) {
        w->y = max_y;
    }
}

void clamp_window(struct desktop_window *w)
{
    clamp_window_size(w);
    clamp_window_position_recoverable(w);
}

void bring_to_front(uint8_t id)
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

int find_window_slot_by_window_id(uint32_t window_id)
{
    for (uint8_t i = BUILTIN_WINDOWS; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible && windows[i].window_id == window_id) {
            return i;
        }
    }
    return -1;
}

uint32_t window_body_width(const struct desktop_window *w)
{
    return w && w->width > 16 ? w->width - 16 : 0;
}

uint32_t window_body_height(const struct desktop_window *w)
{
    return w && w->height > TITLEBAR_H + 18 ? w->height - TITLEBAR_H - 18 : 0;
}

int window_allows_resize(const struct desktop_window *w)
{
    return w && (w->flags & LEONOS_GUI_WINDOW_NO_RESIZE) == 0;
}

int window_is_snap_candidate(const struct desktop_window *w)
{
    return w && w->visible && !w->minimized && window_allows_resize(w);
}

uint8_t snap_mode_for_pointer(uint32_t x, uint32_t y, const struct desktop_window *w)
{
    (void)w;
    if (y <= SNAP_MARGIN) {
        return SNAP_TOP;
    }
    if (x <= SNAP_MARGIN) {
        return SNAP_LEFT;
    }
    if (x + SNAP_MARGIN >= fb_w()) {
        return SNAP_RIGHT;
    }
    return SNAP_NONE;
}

void remove_window_slot(uint8_t slot)
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
    windows[slot].flags = 0;
    app_titles[slot][0] = 0;
    app_texts[slot][0] = 0;
    full_redraw_pending = 1;
}

void request_close_window(uint8_t slot)
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

void send_app_event(uint8_t slot, uint32_t type, int32_t x, int32_t y,
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

void fetch_window_surface(uint8_t slot)
{
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    uint32_t cap_w;
    uint32_t cap_h;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || !windows[slot].window_id) {
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
                                app_client_scratch, &out_w, &out_h) > 0) {
        windows[slot].client_width = out_w;
        windows[slot].client_height = out_h;
        full_redraw_pending = 1;
    }
}

void draw_app_surface_i(uint8_t id, int body_x, int body_y,
                               uint32_t body_w, uint32_t body_h)
{
    struct rect clip;
    uint32_t out_w = 0;
    uint32_t out_h = 0;
    if (!windows[id].window_id) {
        return;
    }
    if (body_w > APP_CLIENT_MAX_W) {
        body_w = APP_CLIENT_MAX_W;
    }
    if (body_h > APP_CLIENT_MAX_H) {
        body_h = APP_CLIENT_MAX_H;
    }
    if (body_w == 0 || body_h == 0) {
        return;
    }
    if (leonos_gui_fetch_window(windows[id].window_id, body_w, body_h,
                                APP_CLIENT_MAX_W,
                                app_client_scratch, &out_w, &out_h) <= 0) {
        text_draw_i(body_x + 16, body_y + 18,
                    windows[id].app_text ? windows[id].app_text : "Application window",
                    LEONOS_UI_BLACK, windows[id].body_color);
        return;
    }
    windows[id].client_width = out_w;
    windows[id].client_height = out_h;
    clip = rect_clip(rect_make(body_x, body_y, (int)out_w, (int)out_h));
    for (int yy = 0; yy < clip.h; ++yy) {
        uint32_t src_y = (uint32_t)(clip.y - body_y + yy);
        for (int xx = 0; xx < clip.w; ++xx) {
            uint32_t src_x = (uint32_t)(clip.x - body_x + xx);
            put_pixel((uint32_t)(clip.x + xx), (uint32_t)(clip.y + yy),
                      app_client_scratch[(uint64_t)src_y * APP_CLIENT_MAX_W + src_x]);
        }
    }
}

