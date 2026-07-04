#include "desktop.h"

void init_desktop(void)
{
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        windows[i] = (struct desktop_window){0};
        z_order[i] = i;
    }
    windows[0] = (struct desktop_window){.x = 120, .y = 84, .width = 420, .height = 220,
                                         .restore_x = 120, .restore_y = 84,
                                         .restore_width = 420, .restore_height = 220,
                                         .title = leonos_i18n("Desktop Server", "桌面服务"), .body_color = 0x00c0c0c0,
                                         .icon_id = DESKTOP_ICON_DESKTOP,
                                         .visible = 0};
    windows[1] = (struct desktop_window){.x = 190, .y = 150, .width = 360, .height = 190,
                                         .restore_x = 190, .restore_y = 150,
                                         .restore_width = 360, .restore_height = 190,
                                         .title = leonos_i18n("File Manager", "文件管理器"), .body_color = 0x00ffffff,
                                         .icon_id = DESKTOP_ICON_FILEMAN};
    windows[2] = (struct desktop_window){.title = leonos_i18n("Settings", "设置"), .body_color = 0x00dfdfdf,
                                         .icon_id = DESKTOP_ICON_SETTINGS};
    windows[3] = (struct desktop_window){.x = 90, .y = 118, .width = 620, .height = 300,
                                         .restore_x = 90, .restore_y = 118,
                                         .restore_width = 620, .restore_height = 300,
                                         .title = leonos_i18n("Task Manager", "任务管理器"), .body_color = 0x00ffffff,
                                         .icon_id = DESKTOP_ICON_TASKMGR};
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
    snap_preview_mode = SNAP_NONE;
    alt_left_down = 0;
    alt_right_down = 0;
    win_left_down = 0;
    win_right_down = 0;
    alt_tab_active = 0;
    alt_tab_count = 0;
    alt_tab_selected = 0;
    cursor_x = 320;
    cursor_y = 240;
    cursor_visible = 1;
    load_cursor_bmp();
    full_redraw_pending = 1;
    refresh_task_snapshot();
    redraw_all();
}

void desktop_run(void)
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
    desktop_load_display_config();
    printf("[desktop.elf] display mode %s scale=%dx logical=%dx%d\n",
           desktop_display_modes[desktop_mode_index].label,
           (int)desktop_scale, fb_w(), fb_h());
    leonos_ui_bind(&ui, screen, fb_w(), fb_h(), MAX_FB_W);
    desktop_publish_display_state();

    init_desktop();
    puts("[desktop.elf] Ring-3 desktop uses shadow framebuffer blit");
    maybe_launch_oobe();

    unsigned long last_log = 0;
    unsigned long last_clock_second = leonos_uptime_ms() / 1000UL;
    unsigned idle_spins = 0;
    for (;;) {
        struct leonos_gui_window_msg window_msg;
        int did_work = 0;
        while (leonos_gui_poll_window(&window_msg) > 0) {
            open_app_window_from_msg(&window_msg);
            did_work = 1;
        }
        desktop_handle_display_requests();
        oobe_lock_update();
        login_lock_update();
        desktop_update_window_animations();

        struct leonos_input_event event;
        while (leonos_gui_next_event(&event) > 0) {
            did_work = 1;
            if (desktop_scale > 1) {
                event.x /= (int32_t)desktop_scale;
                event.y /= (int32_t)desktop_scale;
                event.dx /= (int32_t)desktop_scale;
                event.dy = event.type == LEONOS_INPUT_MOUSE_WHEEL
                               ? event.dy
                               : event.dy / (int32_t)desktop_scale;
            }
            if (event.type == LEONOS_INPUT_MOUSE) {
                handle_mouse((uint32_t)event.x, (uint32_t)event.y, event.buttons);
            } else if (event.type == LEONOS_INPUT_MOUSE_WHEEL) {
                handle_mouse_wheel((uint32_t)event.x, (uint32_t)event.y,
                                   event.dy, event.buttons);
            } else if (event.type == LEONOS_INPUT_KEYBOARD) {
                if (!active_window_is_fullscreen()) {
                    if (handle_global_key(event.keycode, event.pressed)) {
                        continue;
                    }
                }
                if (alt_tab_active) {
                    continue;
                }
                if (active_window >= BUILTIN_WINDOWS && active_window < MAX_WINDOWS &&
                    windows[active_window].window_id) {
                    send_app_event((uint8_t)active_window,
                                   event.pressed ? 7u : 8u,
                                   0, 0, 0, 0, 0, event.keycode, event.pressed);
                }
                printf("[desktop.elf] key scancode=%x pressed=%d\n", event.keycode, event.pressed);
            }
        }
        if (full_redraw_pending) {
            redraw_all();
            did_work = 1;
        }

        unsigned long now = leonos_uptime_ms();
        if (now / 1000UL != last_clock_second) {
            last_clock_second = now / 1000UL;
            repaint_and_flush(rect_make(0, (int)taskbar_y(), (int)fb_w(), TASKBAR_H));
            did_work = 1;
        }
        if (now - last_task_refresh >= 500) {
            refresh_task_snapshot();
            if (windows[3].visible && !windows[3].minimized) {
                redraw_all();
                did_work = 1;
            }
        }
        if (now - last_log >= 5000) {
            puts("[desktop.elf] window server alive");
            last_log = now;
        }
        desktop_update_display_confirmation();
        if (did_work) {
            idle_spins = 0;
            continue;
        }
        if ((idle_spins++ & 31u) == 31u) {
            sleep_ms(1);
        } else {
            sched_yield();
        }
    }
}
