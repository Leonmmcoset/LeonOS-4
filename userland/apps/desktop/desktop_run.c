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
                                         .title = leonos_i18n("Desktop Server", "桌面服务"), .body_color = LEONOS_UI_GRAY,
                                         .visible = 0};
    windows[1] = (struct desktop_window){.x = 190, .y = 150, .width = 360, .height = 190,
                                         .restore_x = 190, .restore_y = 150,
                                         .restore_width = 360, .restore_height = 190,
                                         .title = leonos_i18n("File Manager", "文件管理器"), .body_color = LEONOS_UI_WHITE};
    windows[2] = (struct desktop_window){.title = leonos_i18n("Settings", "设置"), .body_color = LEONOS_UI_LIGHT,
                                         };
    windows[3] = (struct desktop_window){.x = 90, .y = 118, .width = 620, .height = 300,
                                         .restore_x = 90, .restore_y = 118,
                                         .restore_width = 620, .restore_height = 300,
                                         .title = leonos_i18n("Task Manager", "任务管理器"), .body_color = LEONOS_UI_WHITE};
    desktop_icon_path_for_app("desktop", windows[0].icon_path,
                              sizeof(windows[0].icon_path));
    desktop_icon_path_for_app("fileman", windows[1].icon_path,
                              sizeof(windows[1].icon_path));
    desktop_icon_path_for_app("settings", windows[2].icon_path,
                              sizeof(windows[2].icon_path));
    desktop_icon_path_for_app("taskmgr", windows[3].icon_path,
                              sizeof(windows[3].icon_path));
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
    desktop_cursor_style = LEONOS_GUI_CURSOR_ARROW;
    desktop_cursor_auto = 1;
    desktop_taskbar_visible = 1;
    load_cursor_bmp();
    desktop_items_clear();
    full_redraw_pending = 1;
    refresh_task_snapshot();
    redraw_all();
}

void desktop_run(void)
{
    puts("[desktop.elf] Ring-3 Win98-style window server starting");

    int policy = leonos_gui_policy_connect();
    int version = policy == 0 ? leonos_gui_connect() : -1;
    printf("[desktop.elf] windowd policy connect=%d protocol=%d\n", policy, version);
    printf("[desktop.elf] GUI protocol version=%d\n", version);
    printf("[desktop.elf] pid=%d service=window-server\n", getpid());
    int framebuffer_result = leonos_fb_info(&fb);
    if (framebuffer_result < 0) {
        printf("[desktop.elf] framebuffer query failed ret=%d\n", framebuffer_result);
        for (;;) {
            sleep_ms(1000);
        }
    }
    if (leonos_fb_capabilities(&fb_caps) < 0) {
        fb_caps.bytes_per_pixel = 4;
        fb_caps.capabilities = 0;
        fb_caps.max_width = fb.width;
        fb_caps.max_height = fb.height;
        fb_caps.max_bytes = fb.pitch * fb.height;
        fb_caps.backend = LEONOS_FB_BACKEND_BOOT;
    }
    printf("[desktop.elf] framebuffer %dx%d bpp=%d\n", fb.width, fb.height, fb.bpp);
    desktop_load_display_config();
    desktop_load_appearance_config();
    (void)desktop_load_service_config();
    printf("[desktop.elf] display mode %s scale=%dx logical=%dx%d\n",
           desktop_display_modes[desktop_mode_index].label,
           (int)desktop_scale, fb_w(), fb_h());
    leonos_ui_bind(&ui, screen, fb_w(), fb_h(), MAX_FB_W);
    desktop_publish_display_state();
    desktop_publish_appearance_state();

    init_desktop();
    desktop_inputm_load_config();
    puts("[desktop.elf] Ring-3 desktop uses shadow framebuffer blit");
    desktop_service_daemon_update();
    maybe_launch_oobe();

    unsigned long last_log = 0;
    unsigned long last_clock_second = leonos_uptime_ms() / 1000UL;
    unsigned long last_services_refresh = leonos_uptime_ms();
    unsigned long last_inputm_refresh = 0;
    unsigned long last_desktop_items_poll = 0;
    unsigned idle_sleep_ms = 10;
    int last_mouse_visible = 1;
    for (;;) {
        struct leonos_gui_window_msg window_msg;
        int did_work = 0;
        uint32_t window_budget = 64;
        while (window_budget-- && leonos_gui_poll_window(&window_msg) > 0) {
            open_app_window_from_msg(&window_msg);
            did_work = 1;
        }
        desktop_handle_display_requests();
        desktop_handle_appearance_requests();
        oobe_lock_update();
        login_lock_update();
        desktop_update_window_animations();

        struct leonos_input_event event;
        struct leonos_input_event deferred_motion = {0};
        uint8_t have_deferred_motion = 0;
        /* Keep a continuously moving pointer from starving repaint. The
         * kernel coalesces moves, but a busy device can still refill the
         * queue while this loop is running. */
        uint32_t event_budget = 64;
        while (event_budget-- && leonos_gui_next_event(&event) > 0) {
            did_work = 1;
            if (desktop_scale > 1) {
                event.x /= (int32_t)desktop_scale;
                event.y /= (int32_t)desktop_scale;
                event.dx /= (int32_t)desktop_scale;
                event.dy = event.type == LEONOS_INPUT_MOUSE_WHEEL
                               ? event.dy
                               : event.dy / (int32_t)desktop_scale;
            }
            /* A plain pointer move has no ordering-sensitive action. Keep
             * only the newest one in this batch so a high-rate mouse cannot
             * make the desktop render dozens of stale cursor positions. Any
             * button transition, wheel, or keyboard event flushes it first. */
            if (event.type == LEONOS_INPUT_MOUSE && event.buttons == 0 &&
                previous_buttons == 0 && drag_window < 0) {
                deferred_motion = event;
                have_deferred_motion = 1;
                continue;
            }
            if (have_deferred_motion) {
                handle_mouse((uint32_t)deferred_motion.x,
                             (uint32_t)deferred_motion.y,
                             deferred_motion.buttons);
                have_deferred_motion = 0;
            }
            if (event.type == LEONOS_INPUT_MOUSE) {
                handle_mouse((uint32_t)event.x, (uint32_t)event.y, event.buttons);
            } else if (event.type == LEONOS_INPUT_MOUSE_WHEEL) {
                handle_mouse_wheel((uint32_t)event.x, (uint32_t)event.y,
                                   event.dy, event.buttons);
            } else if (event.type == LEONOS_INPUT_KEYBOARD) {
                leonos_ui_caps_lock_event(event.keycode, event.pressed);
                if (desktop_handle_shortcut_input_key(event.keycode, event.pressed)) {
                    continue;
                }
                if (desktop_handle_message_key(event.keycode, event.pressed)) {
                    continue;
                }
                if (!active_window_is_fullscreen()) {
                    if (handle_global_key(event.keycode, event.pressed)) {
                        continue;
                    }
                }
                if (alt_tab_active) {
                    continue;
                }
                if (start_menu_handle_key(event.keycode, event.pressed)) {
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
        if (have_deferred_motion) {
            handle_mouse((uint32_t)deferred_motion.x,
                         (uint32_t)deferred_motion.y,
                         deferred_motion.buttons);
        }
        if (desktop_cursor_auto) {
            uint32_t next_cursor_style = desktop_cursor_style_for_pointer(cursor_x, cursor_y);
            if (next_cursor_style != desktop_cursor_style) {
                struct rect old_cursor_rect = cursor_rect_for_style(
                    cursor_x, cursor_y, desktop_cursor_style);
                desktop_cursor_style = next_cursor_style;
                desktop_queue_cursor_damage(rect_union(
                    old_cursor_rect,
                    cursor_rect_for_style(cursor_x, cursor_y, next_cursor_style)));
                did_work = 1;
            }
        }
        if (full_redraw_pending) {
            redraw_all();
            did_work = 1;
        } else if (desktop_damage_pending) {
            struct rect damage = desktop_damage_rect;
            desktop_damage_pending = 0;
            desktop_damage_cursor_only = 0;
            desktop_damage_rect = rect_make(0, 0, 0, 0);
            repaint_and_flush(damage);
            did_work = 1;
        }
        int mouse_visible = leonos_gui_mouse_visible();
        if (mouse_visible != last_mouse_visible) {
            last_mouse_visible = mouse_visible;
            redraw_all();
            did_work = 1;
        }

        unsigned long now = leonos_uptime_ms();
        desktop_poll_network_state();
        if (!wallpaper_loaded && now >= wallpaper_retry_ms) {
            if (load_wallpaper_bmp()) {
                full_redraw_pending = 1;
            }
            wallpaper_retry_ms = now + 1000UL;
            did_work = 1;
        }
        if (!desktop_items_ready && now >= desktop_items_retry_ms) {
            if (desktop_refresh_items() < 0) {
                desktop_items_retry_ms = now + 1000UL;
            } else {
                /* Only a successful directory refresh changes the desktop
                 * composition. Retrying a missing/unavailable home every
                 * second must not force a full-screen repaint; doing so
                 * starves the software cursor and makes taskbar motion update
                 * at the one-second retry cadence. */
                full_redraw_pending = 1;
            }
            did_work = 1;
        }
        if (desktop_items_ready && desktop_folder_path[0] &&
            now - last_desktop_items_poll >= 1000UL) {
            int changed;
            last_desktop_items_poll = now;
            changed = desktop_items_directory_changed();
            if (changed > 0) {
                if (desktop_refresh_items() == 0) {
                    full_redraw_pending = 1;
                    did_work = 1;
                }
            }
        }
        if (desktop_taskbar_visible && now / 1000UL != last_clock_second) {
            last_clock_second = now / 1000UL;
            /* The clock is the only once-per-second taskbar change. Updating
             * the entire panel forces a synchronous VMware SVGA sync and can
             * stall pointer motion for the duration of the device wait. */
            if (desktop_service_rtc_clock && fb_w() >= TASKBAR_CLOCK_W + 8U) {
                repaint_and_flush(rect_make((int)fb_w() - TASKBAR_CLOCK_W,
                                            (int)taskbar_y(),
                                            TASKBAR_CLOCK_W, TASKBAR_H));
            }
            did_work = 1;
        }
        if (now - last_services_refresh >= 2000UL) {
            last_services_refresh = now;
            desktop_service_daemon_update();
            if (desktop_load_service_config()) {
                if (desktop_taskbar_visible) {
                    repaint_and_flush(rect_make(0, (int)taskbar_y(), (int)fb_w(), TASKBAR_H));
                    did_work = 1;
                }
            }
        }
        if (now - last_task_refresh >= 500) {
            refresh_task_snapshot();
            if (windows[3].visible && !windows[3].minimized) {
                redraw_all();
                did_work = 1;
            }
        }
        if (now - last_inputm_refresh >= 100UL) {
            last_inputm_refresh = now;
            desktop_inputm_refresh();
        }
        if (now - last_log >= 5000) {
            puts("[desktop.elf] window server alive");
            last_log = now;
        }
        desktop_update_display_confirmation();
        if (did_work) {
            idle_sleep_ms = 10;
            continue;
        }
        sleep_ms(idle_sleep_ms);
        if (idle_sleep_ms < 50) {
            idle_sleep_ms += 10;
        }
    }
}
