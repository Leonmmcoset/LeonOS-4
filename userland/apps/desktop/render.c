#include "desktop.h"

int window_is_ui_demo(const struct desktop_window *w)
{
    return w && (text_eq(w->title, "UI Components") || text_eq(w->title, "界面组件"));
}

void draw_ui_demo_label(uint32_t x, uint32_t y, const char *label, uint32_t bg)
{
    leonos_ui_text(&ui, x, y, label, LEONOS_UI_BLACK, bg);
}

void draw_ui_demo_gallery(uint32_t body_x, uint32_t body_y,
                                 uint32_t body_w, uint32_t body_h,
                                 uint32_t bg)
{
    uint32_t pad = 14;
    if (body_w < 300 || body_h < 220) {
        text_draw(body_x + 10, body_y + 12,
                  leonos_i18n("Resize window to view all components", "调整窗口大小以查看所有组件"),
                  LEONOS_UI_BLACK, bg);
        return;
    }

    uint32_t header_h = 32;
    uint32_t left_x = body_x + pad;
    uint32_t right_x = body_x + body_w / 2 + 6;
    uint32_t col_w = body_w / 2 > pad * 2 ? body_w / 2 - pad * 2 : 120;
    uint32_t top = body_y + pad;

    text_draw(left_x, top, leonos_i18n("LeonOS UI Component Library", "LeonOS 界面组件库"), LEONOS_UI_BLACK, bg);
    text_draw(left_x, top + 18, leonos_i18n("Buttons, inputs, lists, menus, panels, windows", "按钮、输入框、列表、菜单、面板、窗口"), LEONOS_UI_DARK, bg);

    uint32_t y = top + header_h + 8;
    draw_ui_demo_label(left_x, y, leonos_i18n("Buttons", "按钮"), bg);
    leonos_ui_button(&ui, left_x, y + 18, 74, LEONOS_UI_BUTTON_H, leonos_i18n("OK", "确定"), 0);
    leonos_ui_button(&ui, left_x + 84, y + 18, 88, LEONOS_UI_BUTTON_H, leonos_i18n("Pressed", "已按下"), LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(&ui, left_x + 182, y + 18, 92, LEONOS_UI_BUTTON_H, leonos_i18n("Disabled", "已禁用"), LEONOS_UI_BUTTON_DISABLED);

    y += 56;
    draw_ui_demo_label(left_x, y, leonos_i18n("Checks and Fields", "复选框和输入框"), bg);
    leonos_ui_checkbox(&ui, left_x, y + 20, leonos_i18n("Checked", "已选中"), 1, 0);
    leonos_ui_checkbox(&ui, left_x, y + 44, leonos_i18n("Unchecked", "未选中"), 0, 0);
    leonos_ui_text_field(&ui, left_x + 136, y + 18, col_w > 146 ? col_w - 146 : 120, leonos_i18n("Sample text", "示例文本"), 0);

    y += 86;
    draw_ui_demo_label(left_x, y, leonos_i18n("Progress", "进度"), bg);
    leonos_ui_progress(&ui, left_x, y + 20, col_w > 24 ? col_w - 24 : 160, 18, 65, 100);
    text_draw(left_x, y + 46, leonos_i18n("65 percent", "65%"), LEONOS_UI_DARK, bg);

    y += 76;
    draw_ui_demo_label(left_x, y, leonos_i18n("Panel", "面板"), bg);
    leonos_ui_panel(&ui, left_x, y + 18, col_w > 24 ? col_w - 24 : 160, 54, LEONOS_UI_LIGHT);
    text_draw(left_x + 10, y + 34, leonos_i18n("Inset content panel", "内嵌内容面板"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);

    y = top + header_h + 8;
    draw_ui_demo_label(right_x, y, leonos_i18n("Menu", "菜单"), bg);
    leonos_ui_menu(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, 90);
    leonos_ui_menu_item(&ui, right_x + 34, y + 28, col_w > 54 ? col_w - 54 : 140, leonos_i18n("Normal item", "普通项目"), 0);
    leonos_ui_menu_item(&ui, right_x + 34, y + 52, col_w > 54 ? col_w - 54 : 140, leonos_i18n("Selected item", "选中项目"), LEONOS_UI_MENU_SELECTED);
    leonos_ui_menu_item(&ui, right_x + 34, y + 76, col_w > 54 ? col_w - 54 : 140, "", LEONOS_UI_MENU_SEPARATOR);

    y += 122;
    draw_ui_demo_label(right_x, y, leonos_i18n("List", "列表"), bg);
    leonos_ui_list_header(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, leonos_i18n("Name        State", "名称        状态"));
    leonos_ui_list_row(&ui, right_x, y + 46, col_w > 16 ? col_w - 16 : 180, leonos_i18n("Button      ready", "按钮        就绪"), 0);
    leonos_ui_list_row(&ui, right_x, y + 70, col_w > 16 ? col_w - 16 : 180, leonos_i18n("TextField   selected", "输入框      已选中"), LEONOS_UI_MENU_SELECTED);
    leonos_ui_list_row(&ui, right_x, y + 94, col_w > 16 ? col_w - 16 : 180, leonos_i18n("Progress    ready", "进度        就绪"), 0);

    y += 138;
    if (y + 54 < body_y + body_h) {
        draw_ui_demo_label(right_x, y, leonos_i18n("Window and Taskbar", "窗口和任务栏"), bg);
        leonos_ui_window_button(&ui, right_x, y + 20, '_', 0);
        leonos_ui_window_button(&ui, right_x + 24, y + 20, 'M', 0);
        leonos_ui_window_button(&ui, right_x + 48, y + 20, 'X', 0);
        leonos_ui_taskbar_button(&ui, right_x + 86, y + 18,
                                 col_w > 116 ? col_w - 116 : 110, leonos_i18n("Task Button", "任务按钮"),
                                 LEONOS_UI_BUTTON_ACTIVE);
    }
}

static uint32_t window_animation_percent(const struct desktop_window *w)
{
    unsigned long elapsed;
    uint32_t raw;
    uint32_t eased;
    if (!w || !w->anim) {
        return 100;
    }
    elapsed = leonos_uptime_ms() - w->anim_start_ms;
    raw = elapsed >= WINDOW_ANIM_MS ? 100 : (uint32_t)((elapsed * 100UL) / WINDOW_ANIM_MS);
    eased = desktop_ease_percent(raw);
    return eased;
}

static uint32_t interp_u32(uint32_t from, uint32_t to, uint32_t percent)
{
    int64_t value = (int64_t)from + ((int64_t)to - (int64_t)from) * percent / 100;
    return value <= 0 ? 0 : (uint32_t)value;
}

static void draw_window_text_block(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                   const char *text, uint32_t fg, uint32_t bg)
{
    char line[160];
    const char *src = text && text[0] ? text : leonos_i18n("Application window", "应用程序窗口");
    uint32_t yy = y;
    uint32_t max_chars;
    if (!w || !h) {
        return;
    }
    max_chars = w / LEONOS_FONT_W;
    if (max_chars == 0) {
        return;
    }
    if (max_chars >= sizeof(line)) {
        max_chars = sizeof(line) - 1;
    }
    while (*src && yy + LEONOS_FONT_H <= y + h) {
        uint32_t n = 0;
        uint32_t draw_n;
        uint32_t last_space = 0;
        while (src[n] && src[n] != '\n' && n < max_chars) {
            if (src[n] == ' ') {
                last_space = n;
            }
            ++n;
        }
        draw_n = n;
        if (src[n] && src[n] != '\n' && last_space > 0) {
            draw_n = last_space;
        }
        for (uint32_t i = 0; i < draw_n; ++i) {
            line[i] = src[i];
        }
        line[draw_n] = 0;
        leonos_ui_text_clipped(&ui, x, yy, w, line, fg, bg);
        if (src[draw_n] == '\n') {
            src += draw_n + 1;
        } else if (draw_n < n && src[draw_n] == ' ') {
            src += draw_n + 1;
        } else {
            src += n;
        }
        while (*src == ' ') {
            ++src;
        }
        yy += LEONOS_FONT_H + 3;
    }
}

static void draw_window_animation_frame(const struct desktop_window *w)
{
    uint32_t percent = window_animation_percent(w);
    int x = w->anim_from_x + (int)(((int64_t)w->anim_to_x - w->anim_from_x) * percent / 100);
    int y = w->anim_from_y + (int)(((int64_t)w->anim_to_y - w->anim_from_y) * percent / 100);
    uint32_t anim_w = interp_u32(w->anim_from_w, w->anim_to_w, percent);
    uint32_t anim_h = interp_u32(w->anim_from_h, w->anim_to_h, percent);
    uint32_t title_color = (active_window >= 0 && &windows[active_window] == w)
                               ? LEONOS_UI_ACTIVE_TITLE
                               : LEONOS_UI_INACTIVE_TITLE;
    if (anim_w < 8 || anim_h < 8) {
        return;
    }
    rect_fill_i(x, y, (int)anim_w, (int)anim_h, LEONOS_UI_GRAY);
    rect_fill_i(x, y, (int)anim_w, 1, LEONOS_UI_WHITE);
    rect_fill_i(x, y, 1, (int)anim_h, LEONOS_UI_WHITE);
    rect_fill_i(x + (int)anim_w - 1, y, 1, (int)anim_h, LEONOS_UI_BLACK);
    rect_fill_i(x, y + (int)anim_h - 1, (int)anim_w, 1, LEONOS_UI_BLACK);
    if (anim_w > 16 && anim_h > TITLEBAR_H + 10) {
        rect_fill_i(x + 4, y + 4, (int)anim_w - 8, TITLEBAR_H, title_color);
        if (anim_w > 48) {
            draw_app_icon(w->icon_path, x + 8, y + 7);
        }
        if (anim_w > 120 && w->title) {
            text_draw_i(x + 28, y + 9, w->title, LEONOS_UI_WHITE, title_color);
        }
    }
}

void draw_window(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    if (!w->visible || (w->minimized && w->anim != WINDOW_ANIM_RESTORE)) {
        return;
    }
    if (w->anim) {
        draw_window_animation_frame(w);
        return;
    }
    if (window_is_fullscreen(w)) {
        rect_fill_i(w->x, w->y, (int)w->width, (int)w->height, w->body_color);
        if (w->window_id) {
            draw_app_surface_i(id, w->x, w->y, w->width, w->height);
        }
        return;
    }

    uint32_t window_flags = active_window == id ? LEONOS_UI_WINDOW_ACTIVE : 0;
    if (!window_allows_resize(w)) {
        window_flags |= LEONOS_UI_WINDOW_NO_RESIZE;
    }
    uint32_t title_color = (window_flags & LEONOS_UI_WINDOW_ACTIVE) ?
        LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_INACTIVE_TITLE;
    rect_fill_i(w->x, w->y, (int)w->width, (int)w->height, LEONOS_UI_GRAY);
    rect_fill_i(w->x, w->y, (int)w->width, 1, LEONOS_UI_WHITE);
    rect_fill_i(w->x, w->y, 1, (int)w->height, LEONOS_UI_WHITE);
    rect_fill_i(w->x + (int)w->width - 1, w->y, 1, (int)w->height, LEONOS_UI_BLACK);
    rect_fill_i(w->x, w->y + (int)w->height - 1, (int)w->width, 1, LEONOS_UI_BLACK);
    rect_fill_i(w->x + 1, w->y + 1, (int)w->width - 2, 1, LEONOS_UI_LIGHT);
    rect_fill_i(w->x + 1, w->y + 1, 1, (int)w->height - 2, LEONOS_UI_LIGHT);
    rect_fill_i(w->x + (int)w->width - 2, w->y + 1, 1, (int)w->height - 2, LEONOS_UI_DARK);
    rect_fill_i(w->x + 1, w->y + (int)w->height - 2, (int)w->width - 2, 1, LEONOS_UI_DARK);
    rect_fill_i(w->x + 4, w->y + 4, (int)w->width - 8, TITLEBAR_H, title_color);
    draw_app_icon(w->icon_path, w->x + 8, w->y + 7);
    text_draw_i(w->x + 28, w->y + 9, w->title, LEONOS_UI_WHITE, title_color);
    int bx_i = w->x + (int)w->width - 64;
    int by_i = w->y + 6;
    window_button_i(bx_i, by_i, '_', 0);
    window_button_i(bx_i + 20, by_i, w->maximized ? 'r' : 'M',
                    window_allows_resize(w) ? 0 : LEONOS_UI_BUTTON_DISABLED);
    window_button_i(bx_i + 40, by_i, 'X', 0);

    int body_x_i = w->x + 8;
    int body_y_i = w->y + TITLEBAR_H + 10;
    uint32_t body_w = w->width > 16 ? w->width - 16 : 0;
    uint32_t body_h = w->height > TITLEBAR_H + 18 ? w->height - TITLEBAR_H - 18 : 0;
    rect_fill_i(body_x_i, body_y_i, (int)body_w, (int)body_h, w->body_color);
    rect_fill_i(body_x_i, body_y_i, (int)body_w, 1, LEONOS_UI_DARK);
    rect_fill_i(body_x_i, body_y_i, 1, (int)body_h, LEONOS_UI_DARK);
    rect_fill_i(body_x_i + (int)body_w - 1, body_y_i, 1, (int)body_h, LEONOS_UI_WHITE);
    rect_fill_i(body_x_i, body_y_i + (int)body_h - 1, (int)body_w, 1, LEONOS_UI_WHITE);
    if (w->window_id) {
        draw_app_surface_i(id, body_x_i, body_y_i, body_w, body_h);
        goto draw_resize_grip;
    }
    if (body_x_i < 0 || body_y_i < 0) {
        goto draw_resize_grip;
    }
    uint32_t body_x = (uint32_t)body_x_i;
    uint32_t body_y = (uint32_t)body_y_i;

    if (id == 0) {
        text_draw(body_x + 16, body_y + 18, leonos_i18n("Ring-3 desktop shadow blit", "Ring-3 桌面阴影缓冲绘制"), 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, leonos_i18n("Dirty redraw reduces flicker", "脏区重绘减少闪烁"), 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, leonos_i18n("Drag and resize window", "拖动并调整窗口大小"), 0x00000000, w->body_color);
    } else if (id == 1) {
        text_draw(body_x + 16, body_y + 18, "0:/", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, leonos_i18n("boot  system  userland", "boot  system  userland"), 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, leonos_i18n("FAT32 root drive view", "FAT32 根驱动器视图"), 0x00000000, w->body_color);
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
                              leonos_i18n("PID PPID STATE KIND CR3        WAKE NAME", "PID PPID 状态 类型 CR3        唤醒 名称"));
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
    } else if (window_is_ui_demo(w)) {
        draw_ui_demo_gallery(body_x, body_y, body_w, body_h, w->body_color);
    } else {
        draw_window_text_block(body_x + 16, body_y + 18,
                               body_w > 32 ? body_w - 32 : body_w,
                               body_h > 36 ? body_h - 36 : body_h,
                               w->app_text,
                               0x00000000,
                               w->body_color);
    }

draw_resize_grip:
    if (window_allows_resize(w) && !w->maximized) {
        int grip_x = w->x + (int)w->width;
        int grip_y = w->y + (int)w->height;
        rect_fill_i(grip_x - 13, grip_y - 13, 9, 1, 0x00808080);
        rect_fill_i(grip_x - 9, grip_y - 17, 1, 9, 0x00808080);
        rect_fill_i(grip_x - 10, grip_y - 10, 6, 1, 0x00000000);
        rect_fill_i(grip_x - 6, grip_y - 14, 1, 6, 0x00000000);
    }
}

void draw_taskbar_button(uint8_t id, uint32_t x, uint32_t y, uint32_t w)
{
    if (!windows[id].visible) {
        return;
    }
    int active = active_window == id && !windows[id].minimized;
    uint32_t button_w = w > 8 ? w - 8 : w;
    leonos_ui_taskbar_button(&ui, x, y, button_w, "",
                             active ? LEONOS_UI_BUTTON_ACTIVE : 0);
    if (button_w >= 26) {
        draw_app_icon(windows[id].icon_path, (int)x + 6, (int)y + 4);
    }
    if (button_w >= 48) {
        leonos_ui_text_clipped(&ui, x + 28, y + 5, button_w - 32, windows[id].title,
                               LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    }
}

void draw_snap_preview(void)
{
    struct rect target;
    if (!snap_preview_mode) {
        return;
    }
    if (snap_preview_mode == SNAP_TOP) {
        target = rect_make(0, 0, (int)fb_w(), (int)taskbar_y());
    } else if (snap_preview_mode == SNAP_LEFT) {
        target = rect_make(0, 0, (int)(fb_w() / 2), (int)taskbar_y());
    } else {
        target = rect_make((int)(fb_w() / 2), 0, (int)(fb_w() - fb_w() / 2), (int)taskbar_y());
    }
    target = rect_clip(target);
    if (target.w <= 0 || target.h <= 0) {
        return;
    }
    rect_fill((uint32_t)target.x, (uint32_t)target.y, (uint32_t)target.w, (uint32_t)target.h, 0x0060b0ff);
    if (target.w > 4 && target.h > 4) {
        rect_fill((uint32_t)target.x + 2, (uint32_t)target.y + 2,
                  (uint32_t)target.w - 4, (uint32_t)target.h - 4, 0x00cce6ff);
    }
}
