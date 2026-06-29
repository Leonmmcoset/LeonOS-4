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
        if (buf[i] == 'w' && buf[i + 1] == '=') {
            width = 0;
            for (i += 2; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                width = width * 10 + (uint32_t)(buf[i] - '0');
            }
        }
        if (buf[i] == 'h' && buf[i + 1] == '=') {
            height = 0;
            for (i += 2; buf[i] >= '0' && buf[i] <= '9'; ++i) {
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
}

void desktop_revert_display_settings(void)
{
    if (!desktop_pending_confirm) {
        return;
    }
    desktop_pending_confirm = 0;
    desktop_apply_display_settings(desktop_previous_mode_index, desktop_previous_scale_index);
    full_redraw_pending = 1;
}

void desktop_update_display_confirmation(void)
{
    if (desktop_pending_confirm && leonos_uptime_ms() >= desktop_confirm_deadline_ms) {
        desktop_revert_display_settings();
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

void copy_app_label_from_elf(char *dst, uint32_t dst_len, const char *name)
{
    uint32_t i = 0;
    uint32_t len = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (text_eq(name, "fileman.elf")) {
        copy_text(dst, dst_len, "File Manager");
        return;
    }
    if (text_eq(name, "taskmgr.elf")) {
        copy_text(dst, dst_len, "Task Manager");
        return;
    }
    if (text_eq(name, "uidemo.elf")) {
        copy_text(dst, dst_len, "UI Components");
        return;
    }
    if (text_eq(name, "osver.elf")) {
        copy_text(dst, dst_len, "OS Version");
        return;
    }
    if (text_eq(name, "bugtest.elf")) {
        copy_text(dst, dst_len, "Kernel Bug Test");
        return;
    }
    if (text_eq(name, "oobe.elf")) {
        copy_text(dst, dst_len, "Setup");
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
        copy_text(dst, dst_len, name ? name : "Application");
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
    return start_menu_opening ? p : 100u - p;
}

uint32_t taskbar_button_width(uint32_t count)
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

int is_alt_down(void)
{
    return alt_left_down || alt_right_down;
}

int is_win_down(void)
{
    return win_left_down || win_right_down;
}
