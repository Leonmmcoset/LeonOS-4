#include "desktop.h"

uint32_t fb_w(void)
{
    return fb.width < MAX_FB_W ? fb.width : MAX_FB_W;
}

uint32_t fb_h(void)
{
    return fb.height < MAX_FB_H ? fb.height : MAX_FB_H;
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

