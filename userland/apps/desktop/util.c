#include "desktop.h"

uint32_t fb_w(void)
{
    return desktop_logical_w ? desktop_logical_w : MAX_FB_W;
}

uint32_t fb_h(void)
{
    return desktop_logical_h ? desktop_logical_h : MAX_FB_H;
}

uint32_t desktop_scale_for_framebuffer(uint32_t width, uint32_t height)
{
    uint32_t scale = 1;
    while ((width / scale) > MAX_FB_W || (height / scale) > MAX_FB_H) {
        ++scale;
    }
    return scale ? scale : 1;
}

uint32_t desktop_scale_fit_for_mode(uint32_t width, uint32_t height)
{
    uint32_t scale = 1;
    for (uint8_t i = 0; i < DESKTOP_SCALE_COUNT; ++i) {
        uint32_t candidate = desktop_scale_options[i];
        if (width * candidate <= fb.width && height * candidate <= fb.height) {
            scale = candidate;
        }
    }
    return scale;
}

int desktop_display_mode_supported(uint8_t mode_index, uint8_t scale_index)
{
    if (mode_index >= DESKTOP_MODE_COUNT || scale_index >= DESKTOP_SCALE_COUNT) {
        return 0;
    }
    uint32_t scale = desktop_scale_options[scale_index];
    uint32_t width = desktop_display_modes[mode_index].width;
    uint32_t height = desktop_display_modes[mode_index].height;
    return width > 0 && height > 0 &&
           width <= MAX_FB_W && height <= MAX_FB_H &&
           width * scale <= fb.width && height * scale <= fb.height;
}

void desktop_apply_display_settings(uint8_t mode_index, uint8_t scale_index)
{
    if (!desktop_display_mode_supported(mode_index, scale_index)) {
        return;
    }
    desktop_mode_index = mode_index;
    desktop_scale_index = scale_index;
    desktop_scale = desktop_scale_options[scale_index];
    desktop_logical_w = desktop_display_modes[mode_index].width;
    desktop_logical_h = desktop_display_modes[mode_index].height;
    leonos_ui_bind(&ui, screen, desktop_logical_w, desktop_logical_h, MAX_FB_W);
    if (windows[0].visible || windows[1].visible || windows[2].visible || windows[3].visible) {
        desktop_reflow_after_display_change();
    }
    desktop_publish_display_state();
}

void desktop_publish_display_state(void)
{
    struct leonos_display_state state;
    unsigned long now = leonos_uptime_ms();
    state.fb_width = fb.width;
    state.fb_height = fb.height;
    state.logical_width = fb_w();
    state.logical_height = fb_h();
    state.scale = desktop_scale;
    state.mode_index = desktop_mode_index;
    state.scale_index = desktop_scale_index;
    state.pending_confirm = desktop_pending_confirm;
    state.confirm_remaining_ms = desktop_pending_confirm && desktop_confirm_deadline_ms > now
                                     ? (uint32_t)(desktop_confirm_deadline_ms - now)
                                     : 0;
    (void)leonos_display_publish_state(&state);
}

void desktop_handle_display_requests(void)
{
    struct leonos_display_request request;
    while (leonos_display_poll_request(&request) > 0) {
        if (request.action == LEONOS_DISPLAY_REQUEST_APPLY) {
            desktop_apply_display_settings_pending((uint8_t)request.mode_index,
                                                   (uint8_t)request.scale_index);
        } else if (request.action == LEONOS_DISPLAY_REQUEST_KEEP) {
            desktop_confirm_display_settings();
        } else if (request.action == LEONOS_DISPLAY_REQUEST_REVERT) {
            desktop_revert_display_settings();
        } else if (request.action == LEONOS_DISPLAY_REQUEST_REFRESH) {
            full_redraw_pending = 1;
        }
        desktop_publish_display_state();
    }
}

static void desktop_choose_default_display(void)
{
    desktop_mode_index = 0;
    desktop_scale_index = 0;
    if (desktop_display_mode_supported(desktop_mode_index, desktop_scale_index)) {
        desktop_apply_display_settings(desktop_mode_index, desktop_scale_index);
        return;
    }
    for (uint8_t i = 0; i < DESKTOP_MODE_COUNT; ++i) {
        uint32_t fit = desktop_scale_fit_for_mode(desktop_display_modes[i].width,
                                                  desktop_display_modes[i].height);
        for (uint8_t s = 0; s < DESKTOP_SCALE_COUNT; ++s) {
            if (desktop_scale_options[s] == fit &&
                desktop_display_mode_supported(i, s)) {
                desktop_apply_display_settings(i, s);
                return;
            }
        }
    }
    desktop_apply_display_settings(DESKTOP_MODE_COUNT - 1, 0);
}

static int parse_display_config(const char *buf, uint8_t *mode, uint8_t *scale)
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t scale_value = 0;
    for (uint32_t i = 0; buf && buf[i]; ++i) {
        if ((buf[i] == 'w' && buf[i + 1] == '=') ||
            (buf[i] == 'w' && buf[i + 1] == 'i' && buf[i + 2] == 'd' &&
             buf[i + 3] == 't' && buf[i + 4] == 'h' && buf[i + 5] == '=')) {
            width = 0;
            i += buf[i + 1] == '=' ? 2 : 6;
            for (; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                width = width * 10 + (uint32_t)(buf[i] - '0');
            }
        }
        if ((buf[i] == 'h' && buf[i + 1] == '=') ||
            (buf[i] == 'h' && buf[i + 1] == 'e' && buf[i + 2] == 'i' &&
             buf[i + 3] == 'g' && buf[i + 4] == 'h' && buf[i + 5] == 't' &&
             buf[i + 6] == '=')) {
            height = 0;
            i += buf[i + 1] == '=' ? 2 : 7;
            for (; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                height = height * 10 + (uint32_t)(buf[i] - '0');
            }
        }
        if (buf[i] == 's' && buf[i + 1] == 'c' && buf[i + 2] == 'a' &&
            buf[i + 3] == 'l' && buf[i + 4] == 'e' && buf[i + 5] == '=') {
            scale_value = 0;
            for (i += 6; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                scale_value = scale_value * 10 + (uint32_t)(buf[i] - '0');
            }
        }
    }
    for (uint8_t m = 0; m < DESKTOP_MODE_COUNT; ++m) {
        if (desktop_display_modes[m].width != width ||
            desktop_display_modes[m].height != height) {
            continue;
        }
        for (uint8_t s = 0; s < DESKTOP_SCALE_COUNT; ++s) {
            if (desktop_scale_options[s] == scale_value &&
                desktop_display_mode_supported(m, s)) {
                *mode = m;
                *scale = s;
                return 1;
            }
        }
    }
    return 0;
}

void desktop_load_display_config(void)
{
    char buf[128];
    uint8_t mode = 0;
    uint8_t scale = 0;
    int fd = open(DISPLAY_CONFIG_PATH, 0, 0);
    if (fd >= 0) {
        long got = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (got > 0) {
            buf[got < (long)sizeof(buf) ? got : (long)sizeof(buf) - 1] = 0;
            if (parse_display_config(buf, &mode, &scale)) {
                desktop_apply_display_settings(mode, scale);
                return;
            }
        }
    }
    desktop_choose_default_display();
}

int desktop_save_display_config(void)
{
    char buf[96];
    uint32_t pos = 0;
    int fd;
    long wrote;
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), "width=");
    append_dec(buf, &pos, sizeof(buf), desktop_display_modes[desktop_mode_index].width);
    append_char(buf, &pos, sizeof(buf), '\n');
    append_text(buf, &pos, sizeof(buf), "height=");
    append_dec(buf, &pos, sizeof(buf), desktop_display_modes[desktop_mode_index].height);
    append_char(buf, &pos, sizeof(buf), '\n');
    append_text(buf, &pos, sizeof(buf), "scale=");
    append_dec(buf, &pos, sizeof(buf), desktop_scale_options[desktop_scale_index]);
    append_char(buf, &pos, sizeof(buf), '\n');
    fd = open(DISPLAY_CONFIG_PATH,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, buf, pos);
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    return wrote == (long)pos ? 0 : -5;
}

void desktop_apply_display_settings_pending(uint8_t mode_index, uint8_t scale_index)
{
    if (!desktop_display_mode_supported(mode_index, scale_index)) {
        return;
    }
    if (!desktop_pending_confirm) {
        desktop_previous_mode_index = desktop_mode_index;
        desktop_previous_scale_index = desktop_scale_index;
    }
    desktop_apply_display_settings(mode_index, scale_index);
    desktop_pending_confirm = 1;
    desktop_confirm_deadline_ms = leonos_uptime_ms() + DISPLAY_CONFIRM_MS;
    full_redraw_pending = 1;
}

void desktop_confirm_display_settings(void)
{
    if (!desktop_pending_confirm) {
        return;
    }
    if (desktop_save_display_config() < 0) {
        puts("[desktop.elf] failed to save display.conf");
    }
    desktop_pending_confirm = 0;
    full_redraw_pending = 1;
    desktop_publish_display_state();
}

void desktop_revert_display_settings(void)
{
    if (!desktop_pending_confirm) {
        return;
    }
    desktop_pending_confirm = 0;
    desktop_apply_display_settings(desktop_previous_mode_index, desktop_previous_scale_index);
    full_redraw_pending = 1;
    desktop_publish_display_state();
}

void desktop_update_display_confirmation(void)
{
    if (desktop_pending_confirm && leonos_uptime_ms() >= desktop_confirm_deadline_ms) {
        desktop_revert_display_settings();
    } else {
        desktop_publish_display_state();
    }
}

void copy_text(char *dst, uint32_t dst_len, const char *src)
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

int text_eq(const char *a, const char *b)
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

uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int32_t read_le32s(const uint8_t *p)
{
    return (int32_t)read_le32(p);
}

int hit_rect(uint32_t x, uint32_t y, int rx, int ry, uint32_t rw, uint32_t rh)
{
    return (int)x >= rx && (int)y >= ry && (int)x < rx + (int)rw && (int)y < ry + (int)rh;
}

int text_ends_with(const char *text, const char *suffix)
{
    uint32_t text_len = 0;
    uint32_t suffix_len = 0;
    while (text && text[text_len]) {
        ++text_len;
    }
    while (suffix && suffix[suffix_len]) {
        ++suffix_len;
    }
    if (suffix_len == 0 || suffix_len > text_len) {
        return 0;
    }
    return text_eq(text + text_len - suffix_len, suffix);
}

uint8_t desktop_icon_for_elf(const char *name)
{
    if (text_eq(name, "terminal.elf")) {
        return DESKTOP_ICON_TERMINAL;
    }
    if (text_eq(name, "notepad.elf")) {
        return DESKTOP_ICON_NOTEPAD;
    }
    if (text_eq(name, "settings.elf")) {
        return DESKTOP_ICON_SETTINGS;
    }
    if (text_eq(name, "calc.elf")) {
        return DESKTOP_ICON_CALC;
    }
    if (text_eq(name, "minesweeper.elf")) {
        return DESKTOP_ICON_MINESWEEPER;
    }
    if (text_eq(name, "fileman.elf")) {
        return DESKTOP_ICON_FILEMAN;
    }
    if (text_eq(name, "taskmgr.elf")) {
        return DESKTOP_ICON_TASKMGR;
    }
    if (text_eq(name, "run.elf")) {
        return DESKTOP_ICON_RUN;
    }
    if (text_eq(name, "devmgr.elf")) {
        return DESKTOP_ICON_SETTINGS;
    }
    if (text_eq(name, "desktop.elf")) {
        return DESKTOP_ICON_DESKTOP;
    }
    return DESKTOP_ICON_APP;
}

uint8_t desktop_icon_for_title(const char *title)
{
    if (text_eq(title, "Terminal") || text_eq(title, "LeonOS Terminal") || text_eq(title, "终端") || text_eq(title, "LeonOS 终端")) {
        return DESKTOP_ICON_TERMINAL;
    }
    if (text_eq(title, "Notepad") || text_eq(title, "记事本")) {
        return DESKTOP_ICON_NOTEPAD;
    }
    if (text_eq(title, "Settings") || text_eq(title, "设置")) {
        return DESKTOP_ICON_SETTINGS;
    }
    if (text_eq(title, "Calculator") || text_eq(title, "计算器")) {
        return DESKTOP_ICON_CALC;
    }
    if (text_eq(title, "Minesweeper") || text_eq(title, "扫雷")) {
        return DESKTOP_ICON_MINESWEEPER;
    }
    if (text_eq(title, "File Manager") || text_eq(title, "文件管理器")) {
        return DESKTOP_ICON_FILEMAN;
    }
    if (text_eq(title, "Task Manager") || text_eq(title, "任务管理器")) {
        return DESKTOP_ICON_TASKMGR;
    }
    if (text_eq(title, "Run") || text_eq(title, "运行")) {
        return DESKTOP_ICON_RUN;
    }
    if (text_eq(title, "Device Manager") || text_eq(title, "设备管理器")) {
        return DESKTOP_ICON_SETTINGS;
    }
    if (text_eq(title, "Desktop Server") || text_eq(title, "桌面服务")) {
        return DESKTOP_ICON_DESKTOP;
    }
    return DESKTOP_ICON_APP;
}

void copy_app_label_from_elf(char *dst, uint32_t dst_len, const char *name)
{
    uint32_t i = 0;
    uint32_t len = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (text_eq(name, "fileman.elf")) {
        copy_text(dst, dst_len, leonos_i18n("File Manager", "文件管理器"));
        return;
    }
    if (text_eq(name, "taskmgr.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Task Manager", "任务管理器"));
        return;
    }
    if (text_eq(name, "uidemo.elf")) {
        copy_text(dst, dst_len, leonos_i18n("UI Components", "界面组件"));
        return;
    }
    if (text_eq(name, "osver.elf")) {
        copy_text(dst, dst_len, leonos_i18n("OS Version", "系统版本"));
        return;
    }
    if (text_eq(name, "bugtest.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Kernel Bug Test", "内核错误测试"));
        return;
    }
    if (text_eq(name, "settings.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Settings", "设置"));
        return;
    }
    if (text_eq(name, "devmgr.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Device Manager", "设备管理器"));
        return;
    }
    if (text_eq(name, "notepad.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Notepad", "记事本"));
        return;
    }
    if (text_eq(name, "terminal.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Terminal", "终端"));
        return;
    }
    if (text_eq(name, "calc.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Calculator", "计算器"));
        return;
    }
    if (text_eq(name, "minesweeper.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Minesweeper", "扫雷"));
        return;
    }
    if (text_eq(name, "run.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Run", "运行"));
        return;
    }
    if (text_eq(name, "hello.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Hello", "你好"));
        return;
    }
    if (text_eq(name, "cjktest.elf")) {
        copy_text(dst, dst_len, leonos_i18n("CJK Test", "中文测试"));
        return;
    }
    if (text_eq(name, "oobe.elf")) {
        copy_text(dst, dst_len, leonos_i18n("Setup", "设置向导"));
        return;
    }
    while (name && name[len]) {
        ++len;
    }
    if (len > 4 && text_ends_with(name, ".elf")) {
        len -= 4;
    }
    while (name && i < len && i + 1 < dst_len) {
        dst[i] = name[i] == '_' ? ' ' : name[i];
        ++i;
    }
    if (i == 0) {
        copy_text(dst, dst_len, name ? name : leonos_i18n("Application", "应用程序"));
    } else {
        dst[i] = 0;
    }
    if (dst[0] >= 'a' && dst[0] <= 'z') {
        dst[0] = (char)(dst[0] - 'a' + 'A');
    }
}

uint32_t taskbar_y(void)
{
    return fb_h() > TASKBAR_H ? fb_h() - TASKBAR_H : 0;
}

uint32_t running_window_count(void)
{
    uint32_t count = 0;
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible) {
            ++count;
        }
    }
    return count;
}

void start_menu_set_open(uint8_t open)
{
    if (start_menu_open == open && !start_menu_animating) {
        return;
    }
    if (open) {
        start_menu_apps_loaded = 0;
    } else {
        start_menu_programs_open = 0;
        start_menu_programs_scroll = 0;
    }
    start_menu_open = open;
    start_menu_opening = open;
    start_menu_animating = 1;
    start_menu_anim_start = leonos_uptime_ms();
    full_redraw_pending = 1;
}

void start_menu_toggle(void)
{
    start_menu_set_open(start_menu_open ? 0 : 1);
}

uint32_t start_menu_progress(void)
{
    if (!start_menu_animating) {
        return start_menu_open ? 100 : 0;
    }
    unsigned long elapsed = leonos_uptime_ms() - start_menu_anim_start;
    if (elapsed >= START_MENU_ANIM_MS) {
        start_menu_animating = 0;
        return start_menu_open ? 100 : 0;
    }
    uint32_t p = (uint32_t)((elapsed * 100UL) / START_MENU_ANIM_MS);
    p = desktop_ease_percent(p);
    return start_menu_opening ? p : 100u - p;
}

uint32_t desktop_ease_percent(uint32_t percent)
{
    uint32_t inv;
    if (percent >= 100) {
        return 100;
    }
    inv = 100 - percent;
    return 100 - (inv * inv) / 100;
}

int desktop_window_animation_active(void)
{
    unsigned long now = leonos_uptime_ms();
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible && windows[i].anim &&
            now - windows[i].anim_start_ms < WINDOW_ANIM_MS) {
            return 1;
        }
    }
    return 0;
}

void desktop_update_window_animations(void)
{
    unsigned long now = leonos_uptime_ms();
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible || !windows[i].anim ||
            now - windows[i].anim_start_ms < WINDOW_ANIM_MS) {
            continue;
        }
        if (windows[i].anim == WINDOW_ANIM_CLOSE) {
            uint8_t send_close = windows[i].close_requested;
            uint32_t window_id = windows[i].window_id;
            windows[i].anim = WINDOW_ANIM_NONE;
            windows[i].close_requested = 0;
            if (send_close && window_id) {
                send_app_event(i, 1, 0, 0, 0, 0, 0, 0, 0);
            }
            remove_window_slot(i);
        } else if (windows[i].anim == WINDOW_ANIM_MINIMIZE) {
            windows[i].anim = WINDOW_ANIM_NONE;
            windows[i].minimized = 1;
            if (active_window == i) {
                active_window = -1;
                for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
                    uint8_t next = z_order[zi];
                    if (windows[next].visible && !windows[next].minimized &&
                        windows[next].anim != WINDOW_ANIM_CLOSE) {
                        active_window = next;
                        break;
                    }
                }
            }
        } else if (windows[i].anim == WINDOW_ANIM_RESTORE) {
            windows[i].anim = WINDOW_ANIM_NONE;
            windows[i].minimized = 0;
            bring_to_front(i);
            if (windows[i].window_id) {
                send_app_event(i, 4, 0, 0, 0, 0, 0, 0, 0);
            }
        } else {
            windows[i].anim = WINDOW_ANIM_NONE;
        }
        full_redraw_pending = 1;
    }
}

uint32_t taskbar_button_width(uint32_t count)
{
    uint32_t available = fb_w() > 112 + TASKBAR_CLOCK_W ? fb_w() - 112 - TASKBAR_CLOCK_W : 0;
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

int is_alt_down(void)
{
    return alt_left_down || alt_right_down;
}

int is_win_down(void)
{
    return win_left_down || win_right_down;
}
