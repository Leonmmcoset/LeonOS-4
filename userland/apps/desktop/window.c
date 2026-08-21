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
    if (window_is_borderless(w)) {
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
        } else if (w->x > max_x) {
            w->x = max_x;
        }
        if (w->y < 0) {
            w->y = 0;
        } else if (w->y > max_y) {
            w->y = max_y;
        }
        return;
    }
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

void desktop_reflow_after_display_change(void)
{
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        struct desktop_window *w = &windows[i];
        if (!w->visible) {
            continue;
        }
        if (window_is_fullscreen(w) || w->maximized || w->snap_mode == SNAP_TOP) {
            w->x = 0;
            w->y = 0;
            w->width = fb_w();
            w->height = taskbar_y();
        } else if (w->snap_mode == SNAP_LEFT) {
            w->x = 0;
            w->y = 0;
            w->width = fb_w() / 2;
            w->height = taskbar_y();
        } else if (w->snap_mode == SNAP_RIGHT) {
            w->width = fb_w() - fb_w() / 2;
            w->height = taskbar_y();
            w->x = (int)(fb_w() - w->width);
            w->y = 0;
        }
        clamp_window(w);
        if (w->window_id) {
            send_app_event(i, LEONOS_GUI_APP_EVENT_RESIZE, 0, 0, 0, 0, 0, 0, 0);
        }
    }
    if (cursor_x >= fb_w()) {
        cursor_x = fb_w() ? fb_w() - 1 : 0;
    }
    if (cursor_y >= fb_h()) {
        cursor_y = fb_h() ? fb_h() - 1 : 0;
    }
    full_redraw_pending = 1;
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
    if (window_is_fullscreen(w) || window_is_borderless(w)) {
        return w->width;
    }
    return w && w->width > 16 ? w->width - 16 : 0;
}

uint32_t window_body_height(const struct desktop_window *w)
{
    if (window_is_fullscreen(w) || window_is_borderless(w)) {
        return w->height;
    }
    return w && w->height > TITLEBAR_H + 18 ? w->height - TITLEBAR_H - 18 : 0;
}

int window_is_fullscreen(const struct desktop_window *w)
{
    return w && (w->flags & LEONOS_GUI_WINDOW_FULLSCREEN) != 0;
}

int window_is_borderless(const struct desktop_window *w)
{
    return w && (w->flags & LEONOS_GUI_WINDOW_BORDERLESS) != 0;
}

int active_window_is_fullscreen(void)
{
    return active_window >= BUILTIN_WINDOWS && active_window < MAX_WINDOWS &&
           window_is_fullscreen(&windows[active_window]) &&
           windows[active_window].visible && !windows[active_window].minimized;
}

int window_allows_resize(const struct desktop_window *w)
{
    return w && !window_is_fullscreen(w) && !window_is_borderless(w) &&
           (w->flags & LEONOS_GUI_WINDOW_NO_RESIZE) == 0;
}

void window_client_origin(const struct desktop_window *w, int *x, int *y)
{
    if (window_is_fullscreen(w) || window_is_borderless(w)) {
        if (x) {
            *x = w ? w->x : 0;
        }
        if (y) {
            *y = w ? w->y : 0;
        }
        return;
    }
    if (x) {
        *x = w ? w->x + 8 : 0;
    }
    if (y) {
        *y = w ? w->y + TITLEBAR_H + 10 : 0;
    }
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
    oobe_lock_on_window_removed(slot);
    login_lock_on_window_removed(slot);
    windows[slot].visible = 0;
    windows[slot].minimized = 0;
    windows[slot].maximized = 0;
    windows[slot].close_requested = 0;
    windows[slot].owner_pid = 0;
    windows[slot].window_id = 0;
    windows[slot].client_width = 0;
    windows[slot].client_height = 0;
    windows[slot].flags = 0;
    windows[slot].icon_path[0] = 0;
    windows[slot].anim = WINDOW_ANIM_NONE;
    windows[slot].anim_start_ms = 0;
    windows[slot].anim_from_x = 0;
    windows[slot].anim_from_y = 0;
    windows[slot].anim_from_w = 0;
    windows[slot].anim_from_h = 0;
    windows[slot].anim_to_x = 0;
    windows[slot].anim_to_y = 0;
    windows[slot].anim_to_w = 0;
    windows[slot].anim_to_h = 0;
    for (uint32_t i = 0; i < DESKTOP_CURSOR_REGION_CAP; ++i) {
        windows[slot].cursor_regions[i].used = 0;
    }
    app_titles[slot][0] = 0;
    app_texts[slot][0] = 0;
    full_redraw_pending = 1;
}

static struct rect window_center_min_rect(const struct desktop_window *w)
{
    uint32_t min_w = w && w->width < 96 ? w->width : 96;
    uint32_t min_h = w && w->height < 54 ? w->height : 54;
    int x = w ? w->x + ((int)w->width - (int)min_w) / 2 : 0;
    int y = w ? w->y + ((int)w->height - (int)min_h) / 2 : 0;
    return rect_make(x, y, (int)min_w, (int)min_h);
}

static struct rect window_taskbar_target_rect(uint8_t slot)
{
    uint32_t tb_y = taskbar_y();
    uint32_t button_w = taskbar_button_width(running_window_count());
    uint32_t x = 106;
    if (button_w > 0) {
        for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
            if (!windows[i].visible ||
                (windows[i].flags & LEONOS_GUI_WINDOW_HIDE_TASKBAR) != 0) {
                continue;
            }
            if (i == slot) {
                return rect_make((int)x, (int)tb_y + 5,
                                 (int)(button_w > 8 ? button_w - 8 : button_w),
                                 LEONOS_UI_BUTTON_H);
            }
            x += button_w;
        }
    }
    return rect_make(106, (int)tb_y + 5, 86, LEONOS_UI_BUTTON_H);
}

void begin_window_rect_animation(uint8_t slot, uint8_t anim,
                                 int from_x, int from_y, uint32_t from_w, uint32_t from_h,
                                 int to_x, int to_y, uint32_t to_w, uint32_t to_h)
{
    if (slot >= MAX_WINDOWS || !windows[slot].visible) {
        return;
    }
    windows[slot].anim = anim;
    windows[slot].anim_start_ms = leonos_uptime_ms();
    windows[slot].anim_from_x = from_x;
    windows[slot].anim_from_y = from_y;
    windows[slot].anim_from_w = from_w;
    windows[slot].anim_from_h = from_h;
    windows[slot].anim_to_x = to_x;
    windows[slot].anim_to_y = to_y;
    windows[slot].anim_to_w = to_w;
    windows[slot].anim_to_h = to_h;
    full_redraw_pending = 1;
}

void begin_window_open_animation(uint8_t slot)
{
    struct rect from;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || window_is_fullscreen(&windows[slot]) ||
        window_is_borderless(&windows[slot])) {
        return;
    }
    from = window_center_min_rect(&windows[slot]);
    begin_window_rect_animation(slot, WINDOW_ANIM_OPEN,
                                from.x, from.y, (uint32_t)from.w, (uint32_t)from.h,
                                windows[slot].x, windows[slot].y,
                                windows[slot].width, windows[slot].height);
}

void begin_window_close_animation(uint8_t slot, uint8_t send_close_event)
{
    struct rect to;
    if (slot >= MAX_WINDOWS || !windows[slot].visible) {
        return;
    }
    if (window_is_fullscreen(&windows[slot]) || window_is_borderless(&windows[slot])) {
        if (send_close_event && windows[slot].window_id) {
            send_app_event(slot, 1, 0, 0, 0, 0, 0, 0, 0);
        }
        remove_window_slot(slot);
        return;
    }
    windows[slot].close_requested = send_close_event;
    to = window_center_min_rect(&windows[slot]);
    begin_window_rect_animation(slot, WINDOW_ANIM_CLOSE,
                                windows[slot].x, windows[slot].y,
                                windows[slot].width, windows[slot].height,
                                to.x, to.y, (uint32_t)to.w, (uint32_t)to.h);
}

void begin_window_minimize_animation(uint8_t slot)
{
    struct rect to;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || windows[slot].minimized) {
        return;
    }
    to = window_taskbar_target_rect(slot);
    begin_window_rect_animation(slot, WINDOW_ANIM_MINIMIZE,
                                windows[slot].x, windows[slot].y,
                                windows[slot].width, windows[slot].height,
                                to.x, to.y, (uint32_t)to.w, (uint32_t)to.h);
}

void begin_window_restore_animation(uint8_t slot)
{
    struct rect from;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || !windows[slot].minimized) {
        return;
    }
    from = window_taskbar_target_rect(slot);
    begin_window_rect_animation(slot, WINDOW_ANIM_RESTORE,
                                from.x, from.y, (uint32_t)from.w, (uint32_t)from.h,
                                windows[slot].x, windows[slot].y,
                                windows[slot].width, windows[slot].height);
}

void request_close_window(uint8_t slot)
{
    if (slot >= MAX_WINDOWS || !windows[slot].visible) {
        return;
    }
    if (windows[slot].window_id) {
        if (windows[slot].anim == WINDOW_ANIM_CLOSE) {
            return;
        }
        printf("[desktop.elf] request close wid=%d title=%s\n", windows[slot].window_id, windows[slot].title);
        begin_window_close_animation(slot, 1);
        return;
    }
    begin_window_close_animation(slot, 0);
}

void send_app_event_to_window(uint32_t window_id, uint32_t type,
                              int32_t x, int32_t y, int32_t dx, int32_t dy,
                              uint32_t width, uint32_t height,
                              uint8_t buttons, uint8_t keycode, uint8_t pressed)
{
    struct leonos_gui_app_event event;
    if (!window_id) {
        return;
    }
    event.window_id = window_id;
    event.type = type;
    event.x = x;
    event.y = y;
    event.dx = dx;
    event.dy = dy;
    event.width = width;
    event.height = height;
    event.buttons = buttons;
    event.keycode = keycode;
    event.pressed = pressed;
    event.reserved = 0;
    leonos_gui_send_app_event(&event);
}

void send_app_event(uint8_t slot, uint32_t type, int32_t x, int32_t y,
                           int32_t dx, int32_t dy, uint8_t buttons,
                           uint8_t keycode, uint8_t pressed)
{
    uint32_t client_w;
    uint32_t client_h;
    if (slot >= MAX_WINDOWS || !windows[slot].visible || !windows[slot].window_id) {
        return;
    }
    client_w = window_body_width(&windows[slot]);
    client_h = window_body_height(&windows[slot]);
    send_app_event_to_window(windows[slot].window_id, type, x, y, dx, dy,
                             client_w, client_h, buttons, keycode, pressed);
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

static uint32_t desktop_map_legacy_ui_color(uint32_t color)
{
    if (leonos_ui_theme() != LEONOS_UI_THEME_METRO) {
        return color;
    }
    if (color == 0x00c0c0c0u) {
        return LEONOS_UI_GRAY;
    }
    if (color == 0x00dfdfdfu) {
        return LEONOS_UI_LIGHT;
    }
    if (color == 0x00808080u) {
        return LEONOS_UI_DARK;
    }
    if (color == 0x00000080u) {
        return LEONOS_UI_ACTIVE_TITLE;
    }
    if (color == 0x00008080u) {
        return LEONOS_UI_DESKTOP;
    }
    return color;
}

static void desktop_map_app_surface(uint32_t width, uint32_t height)
{
    if (width > APP_CLIENT_MAX_W) {
        width = APP_CLIENT_MAX_W;
    }
    if (height > APP_CLIENT_MAX_H) {
        height = APP_CLIENT_MAX_H;
    }
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t index = y * APP_CLIENT_MAX_W + x;
            app_client_scratch[index] = desktop_map_legacy_ui_color(app_client_scratch[index]);
        }
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
                    windows[id].app_text ? windows[id].app_text : leonos_i18n("Application window", "应用程序窗口"),
                    LEONOS_UI_BLACK, windows[id].body_color);
        return;
    }
    desktop_map_app_surface(out_w, out_h);
    windows[id].client_width = out_w;
    windows[id].client_height = out_h;
    if (window_is_fullscreen(&windows[id]) && out_w && out_h) {
        uint32_t draw_w;
        uint32_t draw_h;
        int draw_x;
        int draw_y;
        if ((uint64_t)body_w * out_h > (uint64_t)body_h * out_w) {
            draw_h = body_h;
            draw_w = (uint32_t)((uint64_t)body_h * out_w / out_h);
        } else {
            draw_w = body_w;
            draw_h = (uint32_t)((uint64_t)body_w * out_h / out_w);
        }
        if (!draw_w || !draw_h) {
            return;
        }
        draw_x = body_x + ((int)body_w - (int)draw_w) / 2;
        draw_y = body_y + ((int)body_h - (int)draw_h) / 2;
        clip = rect_clip(rect_make(draw_x, draw_y, (int)draw_w, (int)draw_h));
        for (int yy = 0; yy < clip.h; ++yy) {
            uint32_t src_y = (uint32_t)((uint64_t)(clip.y - draw_y + yy) * out_h / draw_h);
            for (int xx = 0; xx < clip.w; ++xx) {
                uint32_t src_x = (uint32_t)((uint64_t)(clip.x - draw_x + xx) * out_w / draw_w);
                put_pixel((uint32_t)(clip.x + xx), (uint32_t)(clip.y + yy),
                          app_client_scratch[(uint64_t)src_y * APP_CLIENT_MAX_W + src_x]);
            }
        }
        return;
    }
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
