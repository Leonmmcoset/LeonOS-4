#include "desktop.h"

#define TITLEBAR_DOUBLE_CLICK_MS 350UL

static uint8_t last_title_click_valid;
static uint8_t last_title_click_window;
static unsigned long last_title_click_ms;

void minimize_window(uint8_t id)
{
    begin_window_minimize_animation(id);
}

void restore_window(uint8_t id)
{
    windows[id].visible = 1;
    bring_to_front(id);
    clamp_window(&windows[id]);
    if (windows[id].minimized) {
        begin_window_restore_animation(id);
    } else {
        if (windows[id].window_id) {
            send_app_event(id, 4, 0, 0, 0, 0, 0, 0, 0);
        }
        full_redraw_pending = 1;
    }
}

int handle_global_key(uint8_t keycode, uint8_t pressed)
{
    char ch;
    if (keycode == LEONOS_KEY_LEFT_SHIFT) {
        desktop_left_shift_down = pressed;
        if (pressed && is_alt_down() && desktop_inputm_hotkey_is_alt_shift()) {
            desktop_inputm_cycle();
            return 1;
        }
        return 0;
    }
    if (keycode == LEONOS_KEY_RIGHT_SHIFT) {
        desktop_right_shift_down = pressed;
        if (pressed && is_alt_down() && desktop_inputm_hotkey_is_alt_shift()) {
            desktop_inputm_cycle();
            return 1;
        }
        return 0;
    }
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
    if (!desktop_inputm_hotkey_is_alt_shift() && is_win_down() &&
        keycode == LEONOS_KEY_SPACE) {
        win_combo_used = 1;
        if (pressed) {
            desktop_inputm_cycle();
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
            spawn_program_path("0:/system/apps/run/run.elf");
            full_redraw_pending = 1;
            return 1;
        }
    }
    return 0;
}

void toggle_maximize(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    int from_x;
    int from_y;
    uint32_t from_w;
    uint32_t from_h;
    if (!window_allows_resize(w)) {
        return;
    }
    from_x = w->x;
    from_y = w->y;
    from_w = w->width;
    from_h = w->height;
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
        begin_window_rect_animation(id, WINDOW_ANIM_MAXIMIZE,
                                    from_x, from_y, from_w, from_h,
                                    w->x, w->y, w->width, w->height);
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
    begin_window_rect_animation(id, WINDOW_ANIM_MAXIMIZE,
                                from_x, from_y, from_w, from_h,
                                w->x, w->y, w->width, w->height);
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
            begin_window_close_animation((uint8_t)existing, 0);
        }
        return;
    }
    if (msg->type == 4) {
        existing = find_window_slot_by_window_id(msg->window_id);
        if (existing >= 0) {
            struct desktop_window *w = &windows[existing];
            uint32_t old_body_w = window_body_width(w);
            uint32_t old_body_h = window_body_height(w);
            copy_text(app_titles[existing], sizeof(app_titles[existing]),
                      msg->title[0] ? msg->title : leonos_i18n("Application", "应用程序"));
            w->title = app_titles[existing];
            w->flags = msg->flags;
            clamp_window(w);
            if (old_body_w != window_body_width(w) || old_body_h != window_body_height(w)) {
                send_app_event((uint8_t)existing, LEONOS_GUI_APP_EVENT_RESIZE,
                               0, 0, 0, 0, 0, 0, 0);
            }
            full_redraw_pending = 1;
        }
        return;
    }
    if (msg->type == 5) {
        uint8_t visible = msg->data ? 1 : 0;
        if (desktop_taskbar_visible != visible) {
            desktop_taskbar_visible = visible;
            start_menu_set_open(0);
            desktop_reflow_after_display_change();
        }
        return;
    }
    if (msg->type == 6) {
        if (msg->flags & LEONOS_GUI_CURSOR_REQUEST_POSITION) {
            int32_t requested_x = (int32_t)msg->width;
            int32_t requested_y = (int32_t)msg->height;
            cursor_x = requested_x < 0 ? 0 : (uint32_t)requested_x;
            cursor_y = requested_y < 0 ? 0 : (uint32_t)requested_y;
            if (cursor_x >= fb_w()) {
                cursor_x = fb_w() ? fb_w() - 1 : 0;
            }
            if (cursor_y >= fb_h()) {
                cursor_y = fb_h() ? fb_h() - 1 : 0;
            }
            cursor_visible = 1;
        }
        if ((msg->flags & LEONOS_GUI_CURSOR_REQUEST_STYLE) &&
            msg->data < LEONOS_GUI_CURSOR_STYLE_COUNT) {
            desktop_cursor_style = msg->data;
        }
        full_redraw_pending = 1;
        return;
    }
    if (msg->type != 1) {
        return;
    }
    if (oobe_lock_blocks_window_msg(msg) || login_lock_blocks_window_msg(msg)) {
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

    copy_text(app_titles[slot], sizeof(app_titles[slot]),
              msg->title[0] ? msg->title : leonos_i18n("Application", "应用程序"));
    copy_text(app_texts[slot], sizeof(app_texts[slot]),
              msg->text[0] ? msg->text : leonos_i18n("Application window", "应用程序窗口"));
    uint32_t fullscreen = (msg->flags & LEONOS_GUI_WINDOW_FULLSCREEN) != 0;
    uint32_t borderless = (msg->flags & LEONOS_GUI_WINDOW_BORDERLESS) != 0;
    uint32_t width = fullscreen || borderless ? msg->width : msg->width + 16;
    uint32_t height = fullscreen || borderless ? msg->height : msg->height + TITLEBAR_H + 18;
    if (fullscreen) {
        width = fb_w();
        height = fb_h();
    }
    if (!fullscreen && width < MIN_W) {
        width = MIN_W;
    }
    if (!fullscreen && height < MIN_H) {
        height = MIN_H;
    }
    uint32_t offset = (slot - BUILTIN_WINDOWS) * 34;
    windows[slot] = (struct desktop_window){
        .x = fullscreen ? 0 : 220 + (int)offset,
        .y = fullscreen ? 0 : 170 + (int)offset,
        .width = width,
        .height = height,
        .restore_x = fullscreen ? 0 : 220 + (int)offset,
        .restore_y = fullscreen ? 0 : 170 + (int)offset,
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
        .maximized = fullscreen ? 1 : 0,
    };
    desktop_icon_path_for_app(msg->app_path, windows[slot].icon_path,
                              sizeof(windows[slot].icon_path));
    clamp_window(&windows[slot]);
    bring_to_front(slot);
    begin_window_open_animation(slot);
    send_app_event(slot, 4, 0, 0, 0, 0, 0, 0, 0);
    send_app_event(slot, LEONOS_GUI_APP_EVENT_THEME_CHANGED,
                   (int32_t)leonos_ui_theme(),
                   (int32_t)desktop_metro_color_scheme,
                   (int32_t)desktop_win95_color_scheme,
                   0, 0, 0, 0);
    fetch_window_surface(slot);
    printf("[desktop.elf] GUI window from pid=%d wid=%d title=%s\n", msg->pid, msg->window_id, windows[slot].title);
}

int spawn_program_path(const char *path)
{
    char *argv[2];
    int pid;
    argv[0] = (char *)path;
    argv[1] = 0;
    pid = leonos_launch_argv(argv);
    printf("[desktop.elf] spawn %s pid=%d\n", path, pid);
    return pid;
}

int spawn_help_path(const char *path)
{
    char *argv[3];
    int pid;
    argv[0] = "0:/programs/oshlp/oshlp.elf";
    argv[1] = (char *)path;
    argv[2] = 0;
    pid = leonos_spawn_argv(argv[0], argv);
    printf("[desktop.elf] spawn help %s pid=%d\n", path ? path : "", pid);
    return pid;
}

void maybe_launch_oobe(void)
{
    struct leonos_stat st;
    struct leonos_auth_status auth_status;
    /* The desktop may reach this helper through both the startup path and the
     * login fallback path.  Once the OOBE lock owns startup, only its update
     * routine may decide whether another instance is needed. */
    if (oobe_lock_active) {
        return;
    }
    if (stat("0:/system/apps/installer/installer.elf", &st) == 0 &&
        st.type == LEONOS_FS_TYPE_FILE &&
        stat(OOBE_APP_PATH, &st) < 0) {
        puts("[desktop.elf] installer runtime detected; OOBE disabled");
        return;
    }
    if (oobe_done_marker_exists()) {
        puts("[desktop.elf] OOBE done; launching login");
        maybe_launch_login();
        return;
    }
    auth_status = (struct leonos_auth_status){0};
    (void)leonos_auth_status(&auth_status);
    puts(auth_status.has_admin
             ? "[desktop.elf] OOBE completion marker missing; launching oobe.elf"
             : "[desktop.elf] administrator account missing; launching oobe.elf");
    oobe_lock_active = 1;
    oobe_last_spawn_ms = leonos_uptime_ms();
    {
        int pid = spawn_program_path(OOBE_APP_PATH);
        oobe_spawn_pid = pid > 0 ? (uint32_t)pid : 0;
    }
}

static int oobe_process_alive(void)
{
    struct leonos_task_info tasks[LEONOS_TASK_MAX];
    uint64_t tick;
    unsigned long now;
    int count;
    if (!oobe_spawn_pid) {
        return 0;
    }
    count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &tick);
    if (count < 0) {
        return 1;
    }
    for (int i = 0; i < count; ++i) {
        if (tasks[i].pid == oobe_spawn_pid && tasks[i].state != 3) {
            return 1;
        }
    }
    /* fork/exec publication is asynchronous.  A missing task in the first
     * snapshot is therefore still a live spawn reservation, not proof that
     * the process exited. */
    now = leonos_uptime_ms();
    if (now - oobe_last_spawn_ms < OOBE_STARTUP_GRACE_MS) {
        return 1;
    }
    oobe_spawn_pid = 0;
    return 0;
}

int oobe_done_marker_exists(void)
{
    struct leonos_auth_status status;
    struct leonos_license_info license;
    status = (struct leonos_auth_status){0};
    if (leonos_auth_status(&status) < 0 || !status.has_admin) {
        return 0;
    }
    if (leonos_license_required()) {
        license = (struct leonos_license_info){0};
        if (leonos_license_status(&license) < 0 ||
            license.status != LEONOS_LICENSE_STATUS_OK) {
            return 0;
        }
    }
    /* The marker is a recoverable cache; account and license state are authoritative. */
    return 1;
}

int window_is_oobe(const struct desktop_window *w)
{
    return w && w->visible && text_eq(w->title, OOBE_WINDOW_TITLE) &&
           text_eq(w->app_text, OOBE_WINDOW_TEXT);
}

int window_msg_is_oobe(const struct leonos_gui_window_msg *msg)
{
    return msg && text_eq(msg->title, OOBE_WINDOW_TITLE) &&
           text_eq(msg->text, OOBE_WINDOW_TEXT);
}

int oobe_window_slot(void)
{
    for (uint8_t i = BUILTIN_WINDOWS; i < MAX_WINDOWS; ++i) {
        if (window_is_oobe(&windows[i])) {
            return i;
        }
    }
    return -1;
}

void oobe_lock_update(void)
{
    unsigned long now;
    if (!oobe_lock_active) {
        return;
    }
    /* Successful OOBE sign-in assigns the desktop task a session before its
     * window teardown is observed.  That in-memory identity is authoritative
     * and avoids re-opening OOBE while the account database write is settling. */
    if (desktop_session_logged_in()) {
        oobe_lock_active = 0;
        full_redraw_pending = 1;
        maybe_launch_login();
        return;
    }
    if (oobe_done_marker_exists()) {
        oobe_lock_active = 0;
        full_redraw_pending = 1;
        maybe_launch_login();
        return;
    }
    if (oobe_window_slot() >= 0) {
        return;
    }
    if (oobe_process_alive()) {
        return;
    }
    now = leonos_uptime_ms();
    if (now - oobe_last_spawn_ms >= OOBE_RESPAWN_MS) {
        oobe_last_spawn_ms = now;
        {
            int pid = spawn_program_path(OOBE_APP_PATH);
            oobe_spawn_pid = pid > 0 ? (uint32_t)pid : 0;
        }
    }
}

void oobe_lock_on_window_removed(uint8_t slot)
{
    if (oobe_lock_active && slot < MAX_WINDOWS && window_is_oobe(&windows[slot])) {
        /* Let the OOBE completion writes become observable before considering
         * a replacement.  The previous zero timestamp caused an immediate
         * respawn on some cold-storage timings. */
        oobe_last_spawn_ms = leonos_uptime_ms();
    }
}

int oobe_lock_blocks_window_msg(const struct leonos_gui_window_msg *msg)
{
    return oobe_lock_active && !window_msg_is_oobe(msg) &&
           !(msg && text_eq(msg->title, "Application Page Fault"));
}

int handle_oobe_lock_mouse(uint32_t x, uint32_t y, uint8_t buttons)
{
    int slot;
    (void)x;
    (void)y;
    (void)buttons;
    if (!oobe_lock_active) {
        return 0;
    }
    slot = oobe_window_slot();
    if (slot >= 0) {
        bring_to_front((uint8_t)slot);
    }
    start_menu_set_open(0);
    return 1;
}

int handle_oobe_lock_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons)
{
    (void)x;
    (void)y;
    (void)wheel;
    (void)buttons;
    return oobe_lock_active ? 1 : 0;
}

int desktop_session_logged_in(void)
{
    struct leonos_user_info user;
    user = (struct leonos_user_info){0};
    return leonos_auth_current(&user) == 0 && user.uid != 0;
}

static int login_process_alive(void)
{
    struct leonos_task_info tasks[LEONOS_TASK_MAX];
    uint64_t tick;
    unsigned long now;
    int count;
    if (!login_spawn_pid) {
        return 0;
    }
    count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &tick);
    if (count < 0) {
        return 1;
    }
    for (int i = 0; i < count; ++i) {
        if (tasks[i].pid == login_spawn_pid && tasks[i].state != 3) {
            return 1;
        }
    }
    /* The child can run before it is published in a task snapshot.  Do not
     * interpret that short handoff as a failed login launch. */
    now = leonos_uptime_ms();
    if (now - login_last_spawn_ms < LOGIN_STARTUP_GRACE_MS) {
        return 1;
    }
    login_spawn_pid = 0;
    return 0;
}

void maybe_launch_login(void)
{
    struct leonos_auth_status status;
    if (desktop_session_logged_in()) {
        login_lock_active = 0;
        login_spawn_pid = 0;
        desktop_load_appearance_config();
        desktop_inputm_load_config();
        desktop_inputm_launch_login_providers();
        (void)desktop_refresh_items();
        desktop_launch_startup_apps();
        full_redraw_pending = 1;
        return;
    }
    if (!oobe_done_marker_exists()) {
        return;
    }
    status = (struct leonos_auth_status){0};
    if (leonos_auth_status(&status) < 0 || !status.has_admin) {
        maybe_launch_oobe();
        return;
    }
    login_lock_active = 1;
    if (login_window_slot() >= 0 || login_process_alive()) {
        return;
    }
    login_last_spawn_ms = leonos_uptime_ms();
    {
        int pid = spawn_program_path(LOGIN_APP_PATH);
        login_spawn_pid = pid > 0 ? (uint32_t)pid : 0;
    }
}

int window_is_login(const struct desktop_window *w)
{
    return w && w->visible && text_eq(w->title, LOGIN_WINDOW_TITLE) &&
           text_eq(w->app_text, LOGIN_WINDOW_TEXT);
}

int window_msg_is_login(const struct leonos_gui_window_msg *msg)
{
    return msg && text_eq(msg->title, LOGIN_WINDOW_TITLE) &&
           text_eq(msg->text, LOGIN_WINDOW_TEXT);
}

int login_window_slot(void)
{
    for (uint8_t i = BUILTIN_WINDOWS; i < MAX_WINDOWS; ++i) {
        if (window_is_login(&windows[i])) {
            return i;
        }
    }
    return -1;
}

void login_lock_update(void)
{
    unsigned long now;
    if (!login_lock_active) {
        return;
    }
    if (desktop_session_logged_in()) {
        login_lock_active = 0;
        login_spawn_pid = 0;
        desktop_load_appearance_config();
        desktop_inputm_load_config();
        desktop_inputm_launch_login_providers();
        (void)desktop_refresh_items();
        desktop_launch_startup_apps();
        full_redraw_pending = 1;
        return;
    }
    if (login_window_slot() >= 0) {
        return;
    }
    if (login_process_alive()) {
        return;
    }
    now = leonos_uptime_ms();
    if (now - login_last_spawn_ms >= LOGIN_RESPAWN_MS) {
        login_last_spawn_ms = now;
        {
            int pid = spawn_program_path(LOGIN_APP_PATH);
            login_spawn_pid = pid > 0 ? (uint32_t)pid : 0;
        }
    }
}

void login_lock_on_window_removed(uint8_t slot)
{
    if (login_lock_active && slot < MAX_WINDOWS && window_is_login(&windows[slot])) {
        login_last_spawn_ms = leonos_uptime_ms();
    }
}

int login_lock_blocks_window_msg(const struct leonos_gui_window_msg *msg)
{
    return login_lock_active && !window_msg_is_login(msg) &&
           !(msg && text_eq(msg->title, "Application Page Fault"));
}

int handle_login_lock_mouse(uint32_t x, uint32_t y, uint8_t buttons)
{
    int slot;
    (void)x;
    (void)y;
    (void)buttons;
    if (!login_lock_active) {
        return 0;
    }
    slot = login_window_slot();
    if (slot >= 0) {
        bring_to_front((uint8_t)slot);
    }
    start_menu_set_open(0);
    return 1;
}

int handle_login_lock_mouse_wheel(uint32_t x, uint32_t y, int32_t wheel, uint8_t buttons)
{
    (void)x;
    (void)y;
    (void)wheel;
    (void)buttons;
    return login_lock_active ? 1 : 0;
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

void desktop_logout(void)
{
    printf("[desktop.elf] logout requested from Start menu\n");
    leonos_auth_logout();
    desktop_load_appearance_config();
    desktop_inputm_load_config();
    desktop_items_clear();
    desktop_message_active = 0;
    desktop_shortcut_input_active = 0;
    desktop_shortcut_target[0] = 0;
    login_lock_active = 1;
    login_last_spawn_ms = 0;
    login_spawn_pid = 0;
    desktop_startup_launched = 0;
    start_menu_set_open(0);
    maybe_launch_login();
    full_redraw_pending = 1;
}

void desktop_launch_startup_apps(void)
{
    int launched;
    if (desktop_startup_launched) {
        return;
    }
    desktop_startup_launched = 1;
    launched = leonos_startup_launch_current_user();
    printf("[desktop.elf] user startup applications launched=%d\n", launched);
}

void desktop_request_power_confirm(uint8_t action)
{
    if (action != POWER_CONFIRM_REBOOT && action != POWER_CONFIRM_SHUTDOWN) {
        return;
    }
    power_confirm_action = action;
    start_menu_set_open(0);
    full_redraw_pending = 1;
}

int desktop_handle_power_confirm_click(uint32_t x, uint32_t y)
{
    enum { W = 360, H = 150 };
    uint32_t dialog_x = fb_w() > W ? (fb_w() - W) / 2 : 0;
    uint32_t dialog_y = fb_h() > H ? (fb_h() - H) / 2 : 0;
    uint8_t action = power_confirm_action;
    if (!action) {
        return 0;
    }
    if (hit_rect(x, y, (int)dialog_x + W - 168, (int)dialog_y + H - 38,
                 72, LEONOS_UI_BUTTON_H)) {
        power_confirm_action = POWER_CONFIRM_NONE;
        full_redraw_pending = 1;
        if (action == POWER_CONFIRM_REBOOT) {
            desktop_reboot();
        } else {
            desktop_shutdown();
        }
        return 1;
    }
    if (hit_rect(x, y, (int)dialog_x + W - 88, (int)dialog_y + H - 38,
                 72, LEONOS_UI_BUTTON_H) ||
        !hit_rect(x, y, (int)dialog_x, (int)dialog_y, W, H)) {
        power_confirm_action = POWER_CONFIRM_NONE;
        full_redraw_pending = 1;
        return 1;
    }
    return 1;
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
    start_menu_handle_click(x, y);
}

int hit_start_menu_area(uint32_t x, uint32_t y)
{
    return start_menu_hit_test(x, y);
}

int handle_taskbar_click(uint32_t x, uint32_t y)
{
    uint32_t tb_y = taskbar_y();
    if (!desktop_taskbar_visible) {
        return 0;
    }
    if (!hit_rect(x, y, 0, (int)tb_y, fb_w(), TASKBAR_H) && !hit_start_menu_area(x, y)) {
        return 0;
    }
    handle_start_click(x, y);
    if (hit_rect(x, y, 6, (int)tb_y + 5, 86, 24) || hit_start_menu_area(x, y)) {
        return 1;
    }
    if (desktop_service_network_icon &&
        fb_w() >= desktop_tray_width() + 8U) {
        uint32_t network_x = fb_w() -
                              (desktop_service_rtc_clock ? TASKBAR_CLOCK_W : 0U) -
                              TASKBAR_NET_W;
        if (hit_rect(x, y, (int)network_x + 4, (int)tb_y + 5,
                     TASKBAR_NET_W - 6, LEONOS_UI_BUTTON_H)) {
            spawn_program_path(NETWORK_CONTROLLER_APP_PATH);
            start_menu_set_open(0);
            return 1;
        }
    }
    uint32_t bx = 106;
    uint32_t button_w = taskbar_button_width(running_window_count());
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible &&
            (windows[i].flags & LEONOS_GUI_WINDOW_HIDE_TASKBAR) == 0 &&
            button_w > 0) {
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
    uint8_t right = buttons & 2;
    uint8_t was_right = previous_buttons & 2;
    uint8_t new_left = left && !was_left;
    uint8_t new_right = right && !was_right;
    int hover_id = hit_window(x, y);
    struct rect dirty = cursor_visible ? cursor_rect_at(cursor_x, cursor_y) : rect_make(0, 0, 0, 0);
    dirty = rect_union(dirty, cursor_rect_at(x, y));

    if (desktop_shortcut_input_active) {
        if (new_left || new_right) {
            (void)desktop_handle_shortcut_input_click(x, y);
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
        return;
    }

    if (new_left && desktop_inputm_handle_click(x, y)) {
        previous_buttons = buttons;
        cursor_x = x;
        cursor_y = y;
        cursor_visible = 1;
        redraw_all();
        return;
    }

    if (desktop_message_active) {
        if (new_left || new_right) {
            (void)desktop_handle_message_click(x, y);
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
        return;
    }

    if (power_confirm_action) {
        if (new_left) {
            (void)desktop_handle_power_confirm_click(x, y);
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
        return;
    }

    if (active_window_is_fullscreen() &&
        windows[active_window].window_id) {
        send_app_event((uint8_t)active_window, 5, (int32_t)x, (int32_t)y,
                       (int32_t)x - (int32_t)cursor_x, (int32_t)y - (int32_t)cursor_y,
                       buttons, 0, left ? 1 : 0);
        if (buttons != previous_buttons) {
            send_app_event((uint8_t)active_window, 6, (int32_t)x, (int32_t)y,
                           0, 0, buttons, 0, left ? 1 : 0);
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
        return;
    }

    if ((oobe_lock_active && handle_oobe_lock_mouse(x, y, buttons)) ||
        (login_lock_active && handle_login_lock_mouse(x, y, buttons))) {
        previous_buttons = buttons;
        cursor_x = x;
        cursor_y = y;
        cursor_visible = 1;
        if (full_redraw_pending) {
            redraw_all();
        } else {
            repaint_and_flush(rect_pad(dirty, 2));
        }
        return;
    }

    if (desktop_context_menu_active && (new_left || new_right)) {
        (void)desktop_handle_context_menu_click(x, y);
        previous_buttons = buttons;
        cursor_x = x;
        cursor_y = y;
        cursor_visible = 1;
        if (full_redraw_pending) {
            redraw_all();
        } else {
            repaint_and_flush(rect_pad(dirty, 2));
        }
        return;
    }

    if (new_right && hover_id < 0 && y < taskbar_y()) {
        (void)desktop_handle_background_right_click(x, y);
        previous_buttons = buttons;
        cursor_x = x;
        cursor_y = y;
        cursor_visible = 1;
        if (full_redraw_pending) {
            redraw_all();
        } else {
            repaint_and_flush(rect_pad(dirty, 2));
        }
        return;
    }

    if (!(left && !was_left) &&
        hover_id >= BUILTIN_WINDOWS && hover_id < MAX_WINDOWS && windows[hover_id].window_id &&
        !(left && drag_window >= 0)) {
        struct desktop_window *hover = &windows[hover_id];
        int origin_x;
        int origin_y;
        window_client_origin(hover, &origin_x, &origin_y);
        int client_x = (int)x - origin_x;
        int client_y = (int)y - origin_y;
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

    if (new_left) {
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
                int origin_x;
                int origin_y;
                window_client_origin(w, &origin_x, &origin_y);
                int bx = w->x + (int)w->width - 64;
                int by = w->y + 7;
                int resizable = window_allows_resize(w);
                if (!window_is_fullscreen(w) && !window_is_borderless(w) &&
                    hit_rect(x, y, resizable ? bx : bx + 20, by, 18, 20)) {
                    minimize_window((uint8_t)id);
                } else if (!window_is_fullscreen(w) && !window_is_borderless(w) &&
                           hit_rect(x, y, bx + 20, by, 18, 20) && resizable) {
                    toggle_maximize((uint8_t)id);
                } else if (!window_is_fullscreen(w) && !window_is_borderless(w) &&
                           hit_rect(x, y, bx + 40, by, 18, 20)) {
                    request_close_window((uint8_t)id);
                } else if (resizable &&
                           hit_rect(x, y, w->x + (int)w->width - 18, w->y + (int)w->height - 18, 18, 18) &&
                           !w->maximized) {
                    drag_window = id;
                    drag_mode = DRAG_RESIZE;
                    drag_origin_x = (int)x;
                    drag_origin_y = (int)y;
                    drag_origin_w = w->width;
                    drag_origin_h = w->height;
                } else if (!window_is_fullscreen(w) && !window_is_borderless(w) &&
                           hit_rect(x, y, w->x + 4, w->y + 4,
                                    w->width > 8 ? w->width - 8 : 0, TITLEBAR_H)) {
                    unsigned long now = leonos_uptime_ms();
                    if (resizable &&
                        last_title_click_valid &&
                        last_title_click_window == (uint8_t)id &&
                        now - last_title_click_ms <= TITLEBAR_DOUBLE_CLICK_MS) {
                        last_title_click_valid = 0;
                        drag_window = -1;
                        drag_mode = DRAG_NONE;
                        toggle_maximize((uint8_t)id);
                    } else {
                        last_title_click_valid = 1;
                        last_title_click_window = (uint8_t)id;
                        last_title_click_ms = now;
                        if (!w->maximized) {
                            drag_window = id;
                            drag_mode = DRAG_MOVE;
                            drag_dx = (int)x - w->x;
                            drag_dy = (int)y - w->y;
                            snap_preview_mode = SNAP_NONE;
                        }
                    }
                } else if (w->window_id) {
                    int client_x = (int)x - origin_x;
                    int client_y = (int)y - origin_y;
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
            } else if (desktop_handle_background_click(x, y)) {
                dirty = rect_union(dirty, rect_make(0, 0, (int)fb_w(), (int)taskbar_y()));
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
    if (power_confirm_action) {
        return;
    }
    if (desktop_shortcut_input_active || desktop_message_active || desktop_context_menu_active) {
        return;
    }
    if (active_window_is_fullscreen() &&
        windows[active_window].window_id) {
        send_app_event((uint8_t)active_window, LEONOS_GUI_APP_EVENT_MOUSE_WHEEL,
                       (int32_t)x, (int32_t)y, 0, wheel, buttons, 0, 0);
        return;
    }
    if ((oobe_lock_active && handle_oobe_lock_mouse_wheel(x, y, wheel, buttons)) ||
        (login_lock_active && handle_login_lock_mouse_wheel(x, y, wheel, buttons))) {
        return;
    }
    if (start_menu_handle_wheel(x, y, wheel)) {
        redraw_all();
        return;
    }
    int hover_id = hit_window(x, y);
    if (hover_id >= BUILTIN_WINDOWS && hover_id < MAX_WINDOWS && windows[hover_id].window_id) {
        struct desktop_window *hover = &windows[hover_id];
        int origin_x;
        int origin_y;
        window_client_origin(hover, &origin_x, &origin_y);
        int client_x = (int)x - origin_x;
        int client_y = (int)y - origin_y;
        if (client_x >= 0 && client_y >= 0) {
            send_app_event((uint8_t)hover_id, LEONOS_GUI_APP_EVENT_MOUSE_WHEEL,
                           client_x, client_y, 0, wheel, buttons, 0, 0);
        }
    }
}
