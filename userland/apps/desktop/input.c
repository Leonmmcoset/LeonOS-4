#include "desktop.h"

void minimize_window(uint8_t id)
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

void restore_window(uint8_t id)
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

int handle_global_key(uint8_t keycode, uint8_t pressed)
{
    char ch;
    if (keycode == LEONOS_KEY_LEFT_ALT) {
        alt_left_down = pressed;
        if (!pressed && !is_alt_down()) {
            alt_tab_commit();
        }
        return 1;
    }
    if (keycode == LEONOS_KEY_RIGHT_ALT) {
        alt_right_down = pressed;
        if (!pressed && !is_alt_down()) {
            alt_tab_commit();
        }
        return 1;
    }
    if (keycode == LEONOS_KEY_LEFT_WIN) {
        if (pressed && !win_left_down && !win_right_down) {
            win_down_ms = leonos_uptime_ms();
        }
        if (!pressed && win_left_down && !win_right_down && !win_combo_used &&
            leonos_uptime_ms() - win_down_ms <= WIN_TAP_MAX_MS) {
            start_menu_toggle();
        }
        win_left_down = pressed;
        if (!win_left_down && !win_right_down) {
            win_combo_used = 0;
            win_down_ms = 0;
        }
        return 1;
    }
    if (keycode == LEONOS_KEY_RIGHT_WIN) {
        if (pressed && !win_left_down && !win_right_down) {
            win_down_ms = leonos_uptime_ms();
        }
        if (!pressed && win_right_down && !win_left_down && !win_combo_used &&
            leonos_uptime_ms() - win_down_ms <= WIN_TAP_MAX_MS) {
            start_menu_toggle();
        }
        win_right_down = pressed;
        if (!win_left_down && !win_right_down) {
            win_combo_used = 0;
            win_down_ms = 0;
        }
        return 1;
    }
    if (!pressed) {
        return 0;
    }
    if (keycode == LEONOS_KEY_TAB && is_alt_down()) {
        alt_tab_advance();
        return 1;
    }
    if (is_win_down() && keycode_to_ascii(keycode, &ch)) {
        win_combo_used = 1;
        ch = lower_ascii(ch);
        if (ch == 'r') {
            start_menu_set_open(0);
            spawn_program_path("0:/userland/run.elf");
            full_redraw_pending = 1;
            return 1;
        }
    }
    return 0;
}

void toggle_maximize(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    if (!window_allows_resize(w)) {
        return;
    }
    if (w->maximized) {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->width = w->restore_width;
        w->height = w->restore_height;
        w->maximized = 0;
        w->snap_mode = SNAP_NONE;
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
    w->snap_mode = SNAP_TOP;
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

void apply_snap_mode(uint8_t id, uint8_t snap_mode)
{
    struct desktop_window *w = &windows[id];
    if (!window_is_snap_candidate(w)) {
        return;
    }
    if (snap_mode == SNAP_NONE) {
        return;
    }
    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_width = w->width;
    w->restore_height = w->height;
    w->snap_mode = snap_mode;
    w->maximized = snap_mode == SNAP_TOP ? 1 : 0;
    if (snap_mode == SNAP_TOP) {
        w->x = 0;
        w->y = 0;
        w->width = fb_w();
        w->height = taskbar_y();
    } else if (snap_mode == SNAP_LEFT) {
        w->x = 0;
        w->y = 0;
        w->width = fb_w() / 2;
        w->height = taskbar_y();
    } else if (snap_mode == SNAP_RIGHT) {
        w->width = fb_w() - fb_w() / 2;
        w->height = taskbar_y();
        w->x = (int)(fb_w() - w->width);
        w->y = 0;
    }
    clamp_window(w);
    if (w->window_id) {
        send_app_event(id, 4, 0, 0, 0, 0, 0, 0, 0);
    }
    full_redraw_pending = 1;
}

void open_app_window_from_msg(const struct leonos_gui_window_msg *msg)
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
        printf("[desktop.elf] no window slot for pid=%d wid=%d title=%s\n",
               msg->pid, msg->window_id, msg->title);
        return;
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
        .flags = msg->flags,
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

int spawn_program_path(const char *path)
{
    int pid = execve(path, 0, 0);
    printf("[desktop.elf] spawn %s pid=%d\n", path, pid);
    return pid;
}

void maybe_launch_oobe(void)
{
    struct leonos_stat st;
    if (stat(OOBE_DONE_PATH, &st) == 0 && st.type == LEONOS_FS_TYPE_FILE) {
        return;
    }
    puts("[desktop.elf] OOBE completion marker missing; launching oobe.elf");
    spawn_program_path(OOBE_APP_PATH);
}

void desktop_reboot(void)
{
    printf("[desktop.elf] restart requested from Start menu\n");
    leonos_system_reboot();
}

void desktop_shutdown(void)
{
    printf("[desktop.elf] shutdown requested from Start menu\n");
    leonos_system_shutdown();
}

void handle_start_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (hit_rect(x, y, 6, (int)tb_y + 5, 86, 24)) {
        start_menu_toggle();
        return;
    }
    if (!start_menu_open && !start_menu_animating) {
        return;
    }
    struct start_menu_item items[START_MENU_MAX_ITEMS];
    uint32_t count = build_start_menu_items(items, START_MENU_MAX_ITEMS);
    struct start_menu_layout layout = start_menu_layout_for_count(count);
    struct start_programs_layout programs = start_programs_layout_for_menu(layout);
    if (start_menu_programs_open &&
        hit_rect(x, y, (int)programs.x, (int)programs.y, programs.w, programs.h)) {
        if (start_menu_app_count && x >= programs.x + 34) {
            uint32_t rel_x = x - programs.x - 34;
            uint32_t rel_y = y > programs.y + 8 ? y - programs.y - 8 : 0;
            uint32_t col = rel_x / START_PROGRAMS_W;
            uint32_t row = rel_y / START_MENU_ITEM_H;
            uint32_t index = start_menu_programs_scroll + col * programs.rows + row;
            if (col < programs.cols && row < programs.rows && index < start_menu_app_count) {
                spawn_program_path(start_menu_app_paths[index]);
                start_menu_set_open(0);
            }
        }
        return;
    }
    if (!hit_rect(x, y, (int)layout.x, (int)layout.y, layout.w, layout.visible_h)) {
        start_menu_set_open(0);
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item_y_full = 8 + i * START_MENU_ITEM_H;
        if (item_y_full + START_MENU_ITEM_H <= layout.visible_start) {
            continue;
        }
        int item_y = (int)layout.y + (int)item_y_full - (int)layout.visible_start;
        if (item_y < (int)layout.y + 4 ||
            item_y + START_MENU_ITEM_H > (int)layout.y + (int)layout.visible_h ||
            !hit_rect(x, y, (int)layout.x + 34, item_y, layout.w - 52, START_MENU_ITEM_H)) {
            continue;
        }
        if (items[i].type == START_ACTION_RESTORE) {
            restore_window(items[i].window_id);
        } else if (items[i].type == START_ACTION_SPAWN) {
            spawn_program_path(items[i].path);
        } else if (items[i].type == START_ACTION_PROGRAMS) {
            start_menu_programs_open = start_menu_programs_open ? 0 : 1;
            start_menu_programs_scroll = 0;
            full_redraw_pending = 1;
            return;
        } else if (items[i].type == START_ACTION_REBOOT) {
            start_menu_set_open(0);
            desktop_reboot();
            return;
        } else if (items[i].type == START_ACTION_SHUTDOWN) {
            start_menu_set_open(0);
            desktop_shutdown();
            return;
        }
        break;
    }
    start_menu_set_open(0);
}

int hit_start_menu_area(uint32_t x, uint32_t y)
{
    if (!start_menu_open && !start_menu_animating) {
        return 0;
    }
    struct start_menu_item items[START_MENU_MAX_ITEMS];
    uint32_t count = build_start_menu_items(items, START_MENU_MAX_ITEMS);
    struct start_menu_layout layout = start_menu_layout_for_count(count);
    if (hit_rect(x, y, (int)layout.x, (int)layout.y, layout.w, layout.visible_h)) {
        return 1;
    }
    if (start_menu_programs_open) {
        struct start_programs_layout programs = start_programs_layout_for_menu(layout);
        return hit_rect(x, y, (int)programs.x, (int)programs.y, programs.w, programs.h);
    }
    return 0;
}

int handle_taskbar_click(uint32_t x, uint32_t y)
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
                start_menu_set_open(0);
                return 1;
            }
            bx += button_w;
        }
    }
    start_menu_set_open(0);
    return 1;
}

void update_snap_preview(uint32_t x, uint32_t y)
{
    uint8_t next = SNAP_NONE;
    if (drag_window >= 0 && drag_mode == DRAG_MOVE &&
        window_is_snap_candidate(&windows[drag_window])) {
        next = snap_mode_for_pointer(x, y, &windows[drag_window]);
    }
    if (next != snap_preview_mode) {
        snap_preview_mode = next;
        full_redraw_pending = 1;
    }
}

void handle_mouse(uint32_t x, uint32_t y, uint8_t buttons)
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

    if (!(left && !was_left) &&
        hover_id >= BUILTIN_WINDOWS && hover_id < MAX_WINDOWS && windows[hover_id].window_id &&
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
            w->maximized = 0;
            w->snap_mode = SNAP_NONE;
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

    update_snap_preview(x, y);

    if (!left && was_left) {
        if (drag_window >= 0 && drag_mode == DRAG_MOVE && snap_preview_mode != SNAP_NONE) {
            apply_snap_mode((uint8_t)drag_window, snap_preview_mode);
        }
        drag_window = -1;
        drag_mode = DRAG_NONE;
        if (snap_preview_mode != SNAP_NONE) {
            snap_preview_mode = SNAP_NONE;
            full_redraw_pending = 1;
        }
    }

    if (left && !was_left) {
        if (handle_taskbar_click(x, y)) {
            full_redraw_pending = 1;
            int menu_top = (int)taskbar_y() - START_MENU_MAX_H - 8;
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
                start_menu_set_open(0);
                dirty = rect_union(dirty, rect_pad(old_rect, 4));
                int bx = w->x + (int)w->width - 64;
                int by = w->y + 7;
                if (hit_rect(x, y, bx, by, 18, 20)) {
                    minimize_window((uint8_t)id);
                } else if (hit_rect(x, y, bx + 20, by, 18, 20) && window_allows_resize(w)) {
                    toggle_maximize((uint8_t)id);
                } else if (hit_rect(x, y, bx + 40, by, 18, 20)) {
                    request_close_window((uint8_t)id);
                } else if (window_allows_resize(w) &&
                           hit_rect(x, y, w->x + (int)w->width - 18, w->y + (int)w->height - 18, 18, 18) &&
                           !w->maximized) {
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
                    snap_preview_mode = SNAP_NONE;
                } else if (w->window_id) {
                    int client_x = (int)x - (w->x + 8);
                    int client_y = (int)y - (w->y + TITLEBAR_H + 10);
                    if (client_x >= 0 && client_y >= 0) {
                        send_app_event((uint8_t)id, 6, client_x, client_y, 0, 0, buttons, 0, left ? 1 : 0);
                    }
                }
                dirty = rect_union(dirty, rect_pad(window_rect((uint8_t)id), 4));
                dirty = rect_union(dirty, rect_make(0, (int)taskbar_y(), (int)fb_w(), TASKBAR_H));
            } else if (start_menu_open || start_menu_animating) {
                start_menu_set_open(0);
                int menu_top = (int)taskbar_y() - START_MENU_MAX_H - 8;
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

void handle_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons)
{
    if (x >= fb_w()) {
        x = fb_w() ? fb_w() - 1 : 0;
    }
    if (y >= fb_h()) {
        y = fb_h() ? fb_h() - 1 : 0;
    }
    if (start_menu_open && start_menu_programs_open && wheel != 0) {
        struct start_menu_item items[START_MENU_MAX_ITEMS];
        uint32_t count = build_start_menu_items(items, START_MENU_MAX_ITEMS);
        struct start_menu_layout menu = start_menu_layout_for_count(count);
        struct start_programs_layout programs = start_programs_layout_for_menu(menu);
        if (hit_rect(x, y, (int)programs.x, (int)programs.y, programs.w, programs.h) &&
            start_menu_app_count > programs.visible_count) {
            uint32_t steps = wheel < 0 ? (uint32_t)(-wheel) : (uint32_t)wheel;
            uint32_t max_scroll = start_menu_app_count - programs.visible_count;
            if (steps == 0) {
                steps = 1;
            }
            if (wheel > 0) {
                start_menu_programs_scroll = start_menu_programs_scroll > steps
                                                 ? start_menu_programs_scroll - steps
                                                 : 0;
            } else {
                start_menu_programs_scroll = start_menu_programs_scroll + steps < max_scroll
                                                 ? start_menu_programs_scroll + steps
                                                 : max_scroll;
            }
            full_redraw_pending = 1;
            redraw_all();
            return;
        }
    }
    int hover_id = hit_window(x, y);
    if (hover_id >= BUILTIN_WINDOWS && hover_id < MAX_WINDOWS && windows[hover_id].window_id) {
        struct desktop_window *hover = &windows[hover_id];
        int client_x = (int)x - (hover->x + 8);
        int client_y = (int)y - (hover->y + TITLEBAR_H + 10);
        if (client_x >= 0 && client_y >= 0) {
            send_app_event((uint8_t)hover_id, LEONOS_GUI_APP_EVENT_MOUSE_WHEEL,
                           client_x, client_y, 0, wheel, buttons, 0, 0);
        }
    }
}

