#include "desktop.h"

int window_is_ui_demo(const struct desktop_window *w)
{
    return w && text_eq(w->title, "UI Components");
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
        text_draw(body_x + 10, body_y + 12, "Resize window to view all components", LEONOS_UI_BLACK, bg);
        return;
    }

    uint32_t header_h = 32;
    uint32_t left_x = body_x + pad;
    uint32_t right_x = body_x + body_w / 2 + 6;
    uint32_t col_w = body_w / 2 > pad * 2 ? body_w / 2 - pad * 2 : 120;
    uint32_t top = body_y + pad;

    text_draw(left_x, top, "LeonOS UI Component Library", LEONOS_UI_BLACK, bg);
    text_draw(left_x, top + 18, "Buttons, inputs, lists, menus, panels, windows", LEONOS_UI_DARK, bg);

    uint32_t y = top + header_h + 8;
    draw_ui_demo_label(left_x, y, "Buttons", bg);
    leonos_ui_button(&ui, left_x, y + 18, 74, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(&ui, left_x + 84, y + 18, 88, LEONOS_UI_BUTTON_H, "Pressed", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(&ui, left_x + 182, y + 18, 92, LEONOS_UI_BUTTON_H, "Disabled", LEONOS_UI_BUTTON_DISABLED);

    y += 56;
    draw_ui_demo_label(left_x, y, "Checks and Fields", bg);
    leonos_ui_checkbox(&ui, left_x, y + 20, "Checked", 1, 0);
    leonos_ui_checkbox(&ui, left_x, y + 44, "Unchecked", 0, 0);
    leonos_ui_text_field(&ui, left_x + 136, y + 18, col_w > 146 ? col_w - 146 : 120, "Sample text", 0);

    y += 86;
    draw_ui_demo_label(left_x, y, "Progress", bg);
    leonos_ui_progress(&ui, left_x, y + 20, col_w > 24 ? col_w - 24 : 160, 18, 65, 100);
    text_draw(left_x, y + 46, "65 percent", LEONOS_UI_DARK, bg);

    y += 76;
    draw_ui_demo_label(left_x, y, "Panel", bg);
    leonos_ui_panel(&ui, left_x, y + 18, col_w > 24 ? col_w - 24 : 160, 54, LEONOS_UI_LIGHT);
    text_draw(left_x + 10, y + 34, "Inset content panel", LEONOS_UI_BLACK, LEONOS_UI_LIGHT);

    y = top + header_h + 8;
    draw_ui_demo_label(right_x, y, "Menu", bg);
    leonos_ui_menu(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, 90);
    leonos_ui_menu_item(&ui, right_x + 34, y + 28, col_w > 54 ? col_w - 54 : 140, "Normal item", 0);
    leonos_ui_menu_item(&ui, right_x + 34, y + 52, col_w > 54 ? col_w - 54 : 140, "Selected item", LEONOS_UI_MENU_SELECTED);
    leonos_ui_menu_item(&ui, right_x + 34, y + 76, col_w > 54 ? col_w - 54 : 140, "", LEONOS_UI_MENU_SEPARATOR);

    y += 122;
    draw_ui_demo_label(right_x, y, "List", bg);
    leonos_ui_list_header(&ui, right_x, y + 18, col_w > 16 ? col_w - 16 : 180, "Name        State");
    leonos_ui_list_row(&ui, right_x, y + 46, col_w > 16 ? col_w - 16 : 180, "Button      ready", 0);
    leonos_ui_list_row(&ui, right_x, y + 70, col_w > 16 ? col_w - 16 : 180, "TextField   selected", LEONOS_UI_MENU_SELECTED);
    leonos_ui_list_row(&ui, right_x, y + 94, col_w > 16 ? col_w - 16 : 180, "Progress    ready", 0);

    y += 138;
    if (y + 54 < body_y + body_h) {
        draw_ui_demo_label(right_x, y, "Window and Taskbar", bg);
        leonos_ui_window_button(&ui, right_x, y + 20, '_', 0);
        leonos_ui_window_button(&ui, right_x + 24, y + 20, 'M', 0);
        leonos_ui_window_button(&ui, right_x + 48, y + 20, 'X', 0);
        leonos_ui_taskbar_button(&ui, right_x + 86, y + 18,
                                 col_w > 116 ? col_w - 116 : 110, "Task Button",
                                 LEONOS_UI_BUTTON_ACTIVE);
    }
}

static void draw_settings_button(uint32_t x, uint32_t y, uint32_t w,
                                 const char *label, int selected, int disabled)
{
    uint32_t flags = selected ? LEONOS_UI_BUTTON_PRESSED : 0;
    if (disabled) {
        flags |= LEONOS_UI_BUTTON_DISABLED;
    }
    leonos_ui_button(&ui, x, y, w, LEONOS_UI_BUTTON_H, label, flags);
}

void draw_settings_panel(uint32_t body_x, uint32_t body_y,
                         uint32_t body_w, uint32_t body_h, uint32_t bg)
{
    char line[80];
    uint32_t pos = 0;
    uint32_t scale_x;
    uint32_t confirm_y;
    if (body_w < 320 || body_h < 180) {
        text_draw(body_x + 10, body_y + 12, "Resize Settings to change display", LEONOS_UI_BLACK, bg);
        return;
    }
    text_draw(body_x + 16, body_y + 16, "Display", LEONOS_UI_BLACK, bg);

    line[0] = 0;
    append_text(line, &pos, sizeof(line), "Framebuffer ");
    append_dec(line, &pos, sizeof(line), fb.width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), fb.height);
    append_text(line, &pos, sizeof(line), "  Desktop ");
    append_dec(line, &pos, sizeof(line), fb_w());
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), fb_h());
    append_text(line, &pos, sizeof(line), " @ ");
    append_dec(line, &pos, sizeof(line), desktop_scale);
    append_char(line, &pos, sizeof(line), 'x');
    text_draw(body_x + 16, body_y + 36, line, LEONOS_UI_DARK, bg);

    text_draw(body_x + 16, body_y + 66, "Resolution", LEONOS_UI_BLACK, bg);
    for (uint8_t i = 0; i < DESKTOP_MODE_COUNT; ++i) {
        int disabled = !desktop_display_mode_supported(i, desktop_scale_index);
        draw_settings_button(body_x + 16, body_y + 86 + i * 30, 130,
                             desktop_display_modes[i].label,
                             i == desktop_mode_index, disabled);
    }

    scale_x = body_w > 290 ? body_x + 176 : body_x + 156;
    text_draw(scale_x, body_y + 66, "Scale", LEONOS_UI_BLACK, bg);
    for (uint8_t i = 0; i < DESKTOP_SCALE_COUNT; ++i) {
        char label[8];
        uint32_t label_pos = 0;
        label[0] = 0;
        append_dec(label, &label_pos, sizeof(label), desktop_scale_options[i]);
        append_char(label, &label_pos, sizeof(label), 'x');
        draw_settings_button(scale_x, body_y + 86 + i * 30, 78, label,
                             i == desktop_scale_index,
                             !desktop_display_mode_supported(desktop_mode_index, i));
    }

    confirm_y = body_y + 86 + DESKTOP_MODE_COUNT * 30 + 14;
    if (confirm_y + 72 > body_y + body_h) {
        confirm_y = body_y + body_h > 76 ? body_y + body_h - 76 : body_y + 132;
    }
    if (desktop_pending_confirm) {
        unsigned long now = leonos_uptime_ms();
        unsigned long remaining = desktop_confirm_deadline_ms > now
                                      ? desktop_confirm_deadline_ms - now
                                      : 0;
        uint32_t seconds = (uint32_t)((remaining + 999UL) / 1000UL);
        uint32_t panel_w = body_w > 32 ? body_w - 32 : body_w;
        pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), "Keep these display settings? Reverting in ");
        append_dec(line, &pos, sizeof(line), seconds);
        append_text(line, &pos, sizeof(line), "s");
        leonos_ui_panel(&ui, body_x + 16, confirm_y, panel_w, 62, LEONOS_UI_LIGHT);
        text_draw(body_x + 26, confirm_y + 10, line, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
        draw_settings_button(body_x + 26, confirm_y + 34, 82, "Keep", 0, 0);
        draw_settings_button(body_x + 118, confirm_y + 34, 82, "Revert", 0, 0);
    } else if (confirm_y + 20 < body_y + body_h) {
        text_draw(body_x + 16, confirm_y + 4,
                  "Changes are applied temporarily until you keep them.",
                  LEONOS_UI_DARK, bg);
    }
}

void draw_window(uint8_t id)
{
    struct desktop_window *w = &windows[id];
    if (!w->visible || w->minimized) {
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
    text_draw_i(w->x + 10, w->y + 9, w->title, LEONOS_UI_WHITE, title_color);
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
        text_draw(body_x + 16, body_y + 18, "Ring-3 desktop shadow blit", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "Dirty redraw reduces flicker", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "Drag and resize window", 0x00000000, w->body_color);
    } else if (id == 1) {
        text_draw(body_x + 16, body_y + 18, "0:/", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "boot  system  userland", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "FAT32 root drive view", 0x00000000, w->body_color);
    } else if (id == 2) {
        draw_settings_panel(body_x, body_y, body_w, body_h, w->body_color);
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
                              "PID PPID STATE KIND CR3        WAKE NAME");
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
        text_draw(body_x + 16, body_y + 18, w->app_text ? w->app_text : "Application window", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 42, "Process window via GUI IPC", 0x00000000, w->body_color);
        text_draw(body_x + 16, body_y + 66, "App exited, desktop owns surface", 0x00000000, w->body_color);
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
    leonos_ui_taskbar_button(&ui, x, y, w > 8 ? w - 8 : w, windows[id].title,
                             active ? LEONOS_UI_BUTTON_ACTIVE : 0);
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
