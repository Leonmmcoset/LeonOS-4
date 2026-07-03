#include "desktop.h"

void alt_tab_rebuild(void)
{
    alt_tab_count = 0;
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        if (windows[id].visible && !windows[id].minimized) {
            alt_tab_ids[alt_tab_count++] = id;
        }
    }
    if (alt_tab_count == 0) {
        alt_tab_active = 0;
        alt_tab_selected = 0;
        return;
    }
    if (alt_tab_selected >= alt_tab_count) {
        alt_tab_selected = 0;
    }
}

void alt_tab_begin(void)
{
    alt_tab_rebuild();
    if (alt_tab_count <= 1) {
        alt_tab_active = 0;
        return;
    }
    alt_tab_active = 1;
    alt_tab_selected = 1;
    full_redraw_pending = 1;
}

void alt_tab_advance(void)
{
    if (!alt_tab_active) {
        alt_tab_begin();
        return;
    }
    alt_tab_rebuild();
    if (alt_tab_count == 0) {
        return;
    }
    alt_tab_selected = (uint8_t)((alt_tab_selected + 1) % alt_tab_count);
    full_redraw_pending = 1;
}

void alt_tab_commit(void)
{
    if (!alt_tab_active || alt_tab_count == 0) {
        alt_tab_active = 0;
        return;
    }
    uint8_t id = alt_tab_ids[alt_tab_selected];
    alt_tab_active = 0;
    restore_window(id);
    full_redraw_pending = 1;
}

void draw_alt_tab_overlay(void)
{
    if (!alt_tab_active || alt_tab_count == 0) {
        return;
    }
    uint32_t row_h = LEONOS_FONT_H + 8;
    uint32_t box_h = 20 + row_h * alt_tab_count;
    uint32_t x = fb_w() > ALT_TAB_W ? (fb_w() - ALT_TAB_W) / 2 : 4;
    uint32_t y = fb_h() > box_h + TASKBAR_H ? (fb_h() - TASKBAR_H - box_h) / 2 : 4;
    leonos_ui_bevel(&ui, x, y, ALT_TAB_W, box_h, LEONOS_UI_GRAY, 0);
    leonos_ui_text(&ui, x + 10, y + 8, leonos_i18n("Switch To", "切换到"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < alt_tab_count; ++i) {
        uint8_t id = alt_tab_ids[i];
        uint32_t flags = i == alt_tab_selected ? LEONOS_UI_MENU_SELECTED : 0;
        leonos_ui_list_row(&ui, x + 8, y + 24 + i * row_h, ALT_TAB_W - 16,
                           windows[id].title ? windows[id].title : leonos_i18n("Window", "窗口"), flags);
    }
}
