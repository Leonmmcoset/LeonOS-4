#include "desktop.h"

int start_menu_is_hidden_app(const char *name)
{
    return text_eq(name, "init.elf") ||
           text_eq(name, "desktop.elf") ||
           text_eq(name, "oobe.elf") ||
           text_eq(name, "login.elf") ||
           text_eq(name, "shell.elf");
}

void start_menu_load_apps(void)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    start_menu_app_count = 0;
    start_menu_apps_loaded = 1;
    if (leonos_list_dir("0:/userland", entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < count && start_menu_app_count < START_MENU_MAX_APPS; ++i) {
        if (entries[i].type != LEONOS_FS_TYPE_FILE ||
            !text_ends_with(entries[i].name, ".elf") ||
            start_menu_is_hidden_app(entries[i].name)) {
            continue;
        }
        copy_app_label_from_elf(start_menu_app_labels[start_menu_app_count],
                                sizeof(start_menu_app_labels[start_menu_app_count]),
                                entries[i].name);
        start_menu_app_icons[start_menu_app_count] = desktop_icon_for_elf(entries[i].name);
        copy_text(start_menu_app_paths[start_menu_app_count],
                  sizeof(start_menu_app_paths[start_menu_app_count]),
                  "0:/userland/");
        uint32_t pos = 0;
        while (start_menu_app_paths[start_menu_app_count][pos]) {
            ++pos;
        }
        append_text(start_menu_app_paths[start_menu_app_count], &pos,
                    sizeof(start_menu_app_paths[start_menu_app_count]),
                    entries[i].name);
        ++start_menu_app_count;
    }
}

void start_menu_ensure_apps(void)
{
    if (!start_menu_apps_loaded) {
        start_menu_load_apps();
    }
}

uint32_t build_start_menu_items(struct start_menu_item *items, uint32_t cap)
{
    uint32_t count = 0;
#define ADD_ITEM(label_, type_, win_, icon_, path_) do { \
        if (count < cap) { \
            items[count++] = (struct start_menu_item){label_, type_, win_, icon_, path_}; \
        } \
    } while (0)
    ADD_ITEM(leonos_i18n("Desktop Server", "桌面服务"), START_ACTION_RESTORE, 0, DESKTOP_ICON_DESKTOP, 0);
    ADD_ITEM(leonos_i18n("Settings", "设置"), START_ACTION_SPAWN_ONCE, 0, DESKTOP_ICON_SETTINGS, "0:/userland/settings.elf");
    ADD_ITEM("", START_ACTION_SEPARATOR, 0, DESKTOP_ICON_APP, 0);
    start_menu_ensure_apps();
    ADD_ITEM(leonos_i18n("Programs >", "程序 >"), START_ACTION_PROGRAMS, 0, DESKTOP_ICON_APP, 0);
    uint32_t before_minimized = count;
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        if (windows[id].visible && windows[id].minimized && count < cap) {
            if (count == before_minimized) {
                ADD_ITEM("", START_ACTION_SEPARATOR, 0, DESKTOP_ICON_APP, 0);
            }
            items[count++] = (struct start_menu_item){
                windows[id].title ? windows[id].title : leonos_i18n("Window", "窗口"),
                START_ACTION_RESTORE,
                id,
                windows[id].icon_id,
                0,
            };
        }
    }
    ADD_ITEM("", START_ACTION_SEPARATOR, 0, DESKTOP_ICON_APP, 0);
    if (desktop_session_logged_in()) {
        ADD_ITEM(leonos_i18n("Log Out", "注销"), START_ACTION_LOGOUT, 0, DESKTOP_ICON_RUN, 0);
    }
    ADD_ITEM(leonos_i18n("Restart", "重启"), START_ACTION_REBOOT, 0, DESKTOP_ICON_RUN, 0);
    ADD_ITEM(leonos_i18n("Shut Down", "关机"), START_ACTION_SHUTDOWN, 0, DESKTOP_ICON_RUN, 0);
#undef ADD_ITEM
    return count;
}

uint32_t start_menu_height_for_count(uint32_t count)
{
    uint32_t h = 16 + count * START_MENU_ITEM_H;
    if (h > START_MENU_MAX_H) {
        h = START_MENU_MAX_H;
    }
    if (h < 80) {
        h = 80;
    }
    return h;
}

struct start_menu_layout start_menu_layout_for_count(uint32_t count)
{
    struct start_menu_layout layout;
    uint32_t progress = start_menu_progress();
    layout.x = 6;
    layout.w = START_MENU_W;
    layout.full_h = start_menu_height_for_count(count);
    layout.visible_h = (layout.full_h * progress + 99) / 100;
    if (layout.visible_h < 12 && (start_menu_open || start_menu_animating)) {
        layout.visible_h = 12;
    }
    layout.y = taskbar_y() > layout.visible_h ? taskbar_y() - layout.visible_h : 0;
    layout.visible_start = layout.full_h > layout.visible_h ? layout.full_h - layout.visible_h : 0;
    return layout;
}

struct start_programs_layout start_programs_layout_for_menu(struct start_menu_layout menu)
{
    struct start_programs_layout layout;
    uint32_t max_h = taskbar_y() > 12 ? taskbar_y() - 12 : START_MENU_MAX_H;
    uint32_t available_right;
    uint32_t available_left;
    uint32_t max_cols;
    uint32_t rows = max_h > 16 ? (max_h - 16) / START_MENU_ITEM_H : 1;
    if (rows == 0) {
        rows = 1;
    }
    start_menu_ensure_apps();
    if (start_menu_app_count && rows > start_menu_app_count) {
        rows = start_menu_app_count;
    }
    if (rows == 0) {
        rows = 1;
    }
    layout.cols = start_menu_app_count > rows ? (start_menu_app_count + rows - 1) / rows : 1;
    if (layout.cols == 0) {
        layout.cols = 1;
    }
    available_right = fb_w() > menu.x + menu.w ? fb_w() - (menu.x + menu.w) + 2 : 0;
    available_left = menu.x + 2;
    max_cols = available_right > available_left ? available_right : available_left;
    max_cols = max_cols / START_PROGRAMS_W;
    if (max_cols == 0) {
        max_cols = 1;
    }
    if (layout.cols > max_cols) {
        layout.cols = max_cols;
        rows = (start_menu_app_count + layout.cols - 1) / layout.cols;
        if (rows == 0) {
            rows = 1;
        }
        if (rows * START_MENU_ITEM_H + 16 > max_h) {
            rows = max_h > 16 ? (max_h - 16) / START_MENU_ITEM_H : 1;
        }
        if (rows == 0) {
            rows = 1;
        }
    }
    layout.rows = rows;
    layout.w = layout.cols * START_PROGRAMS_W;
    layout.h = 16 + rows * START_MENU_ITEM_H;
    layout.visible_count = layout.rows * layout.cols;
    if (layout.visible_count == 0) {
        layout.visible_count = 1;
    }
    if (start_menu_programs_scroll >= start_menu_app_count) {
        start_menu_programs_scroll = start_menu_app_count ? start_menu_app_count - 1 : 0;
    }
    if (start_menu_programs_scroll + layout.visible_count > start_menu_app_count) {
        start_menu_programs_scroll = start_menu_app_count > layout.visible_count
                                         ? start_menu_app_count - layout.visible_count
                                         : 0;
    }
    layout.x = menu.x + menu.w - 2;
    if (layout.x + layout.w > fb_w()) {
        layout.x = menu.x > layout.w ? menu.x - layout.w + 2 : 0;
    }
    layout.y = menu.y;
    if (layout.y + layout.h > taskbar_y()) {
        layout.y = taskbar_y() > layout.h ? taskbar_y() - layout.h : 0;
    }
    return layout;
}

void draw_start_programs_menu(struct start_menu_layout menu)
{
    if (!start_menu_programs_open) {
        return;
    }
    struct start_programs_layout layout = start_programs_layout_for_menu(menu);
    leonos_ui_menu(&ui, layout.x, layout.y, layout.w, layout.h);
    if (!start_menu_app_count) {
        leonos_ui_menu_item(&ui, layout.x + 34, layout.y + 8,
                            START_PROGRAMS_W - 44, leonos_i18n("No programs", "没有程序"), LEONOS_UI_MENU_DISABLED);
        return;
    }
    for (uint32_t visible = 0; visible < layout.visible_count; ++visible) {
        uint32_t i = start_menu_programs_scroll + visible;
        uint32_t col = visible / layout.rows;
        uint32_t row = visible % layout.rows;
        uint32_t item_x = layout.x + 34 + col * START_PROGRAMS_W;
        uint32_t item_y = layout.y + 8 + row * START_MENU_ITEM_H;
        if (i >= start_menu_app_count) {
            break;
        }
        draw_app_icon(start_menu_app_icons[i], (int)item_x - 22, (int)item_y + 4);
        leonos_ui_menu_item(&ui, item_x, item_y, START_PROGRAMS_W - 44,
                            start_menu_app_labels[i], 0);
    }
}

void draw_start_menu(void)
{
    if (!start_menu_open && !start_menu_animating) {
        return;
    }
    struct start_menu_item items[START_MENU_MAX_ITEMS];
    uint32_t count = build_start_menu_items(items, START_MENU_MAX_ITEMS);
    struct start_menu_layout layout = start_menu_layout_for_count(count);
    leonos_ui_menu(&ui, layout.x, layout.y, layout.w, layout.visible_h);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item_y_full = 8 + i * START_MENU_ITEM_H;
        if (item_y_full + START_MENU_ITEM_H <= layout.visible_start) {
            continue;
        }
        int item_y = (int)layout.y + (int)item_y_full - (int)layout.visible_start;
        if (item_y < (int)layout.y + 4 ||
            item_y + START_MENU_ITEM_H > (int)layout.y + (int)layout.visible_h) {
            continue;
        }
        if (items[i].type == START_ACTION_SEPARATOR) {
            leonos_ui_menu_item(&ui, layout.x + 34, (uint32_t)item_y + 8,
                                layout.w - 52,
                                "", LEONOS_UI_MENU_SEPARATOR);
        } else {
            draw_app_icon(items[i].icon_id, (int)layout.x + 10, item_y + 4);
            leonos_ui_menu_item(&ui, layout.x + 34, (uint32_t)item_y,
                                layout.w - 52,
                                items[i].label, 0);
        }
    }
    draw_start_programs_menu(layout);
}
