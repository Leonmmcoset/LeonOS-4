#include "fileman.h"

int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == FILEMAN_MENU_FILE ? FILEMAN_MENU_NONE : FILEMAN_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 64, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == FILEMAN_MENU_VIEW ? FILEMAN_MENU_NONE : FILEMAN_MENU_VIEW;
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    if (menu_open == FILEMAN_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 8, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_valid()) {
                execute_action(FILEMAN_ACTION_OPEN);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 34, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_file()) {
                execute_action(FILEMAN_ACTION_OPEN_WITH);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 60, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_file()) {
                execute_action(FILEMAN_ACTION_DEFAULT_PROGRAM);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 86, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_file()) {
                execute_action(FILEMAN_ACTION_CREATE_SHORTCUT);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 112, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_valid()) {
                execute_action(FILEMAN_ACTION_DETAILS);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 138, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_mutable()) {
                execute_action(FILEMAN_ACTION_RENAME);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 164, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_mutable()) {
                execute_action(FILEMAN_ACTION_DELETE);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 216, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            execute_action(FILEMAN_ACTION_NEW_FOLDER);
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 242, 162, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            execute_action(FILEMAN_ACTION_REFRESH);
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    if (menu_open == FILEMAN_MENU_VIEW) {
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            reload_dir();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            navigate_root();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            leonos_ui_show_message_box(T("File Manager", "文件资源管理器"), T("Browse FAT32 files and launch apps.", "浏览 FAT32 文件并启动应用。"), "OK");
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    return 0;
}

int handle_context_menu_click(int32_t x, int32_t y)
{
    struct leonos_ui_context_menu_item items[FILEMAN_CONTEXT_MENU_COUNT];
    uint32_t action = 0;
    if (!context_menu_active) {
        return 0;
    }
    build_context_menu_items(items, FILEMAN_CONTEXT_MENU_COUNT);
    if (leonos_ui_context_menu_hit(x, y, context_menu_x, context_menu_y,
                                   FILEMAN_CONTEXT_MENU_W, items,
                                   FILEMAN_CONTEXT_MENU_COUNT, &action)) {
        context_menu_set_active(0);
        if (action) {
            execute_action(action);
        }
        return 1;
    }
    context_menu_set_active(0);
    return 0;
}

void show_context_menu_at(int32_t x, int32_t y, int32_t target)
{
    uint32_t menu_h = leonos_ui_context_menu_height(FILEMAN_CONTEXT_MENU_COUNT);
    menu_open = FILEMAN_MENU_NONE;
    if (target >= 0 && (uint32_t)target < entry_count) {
        file_list.selected = target;
        if ((uint32_t)target < file_list.scroll) {
            file_list.scroll = (uint32_t)target;
        } else if ((uint32_t)target >= file_list.scroll + file_list.visible_rows) {
            file_list.scroll = (uint32_t)target - file_list.visible_rows + 1;
        }
    } else {
        file_list.selected = -1;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    context_menu_x = (uint32_t)x;
    context_menu_y = (uint32_t)y;
    if (context_menu_x + FILEMAN_CONTEXT_MENU_W > view_w) {
        context_menu_x = view_w > FILEMAN_CONTEXT_MENU_W ? view_w - FILEMAN_CONTEXT_MENU_W : 0;
    }
    if (context_menu_y + menu_h > view_h - STATUS_H) {
        context_menu_y = view_h - STATUS_H > menu_h
                             ? view_h - STATUS_H - menu_h
                             : 0;
    }
    context_menu_set_active(1);
}

void handle_right_click(int32_t x, int32_t y)
{
    int32_t index = list_index_at(x, y);
    if (index >= 0) {
        set_status(entries[index].name);
    } else {
        set_status("Folder actions");
    }
    show_context_menu_at(x, y, index);
}

void handle_click(int32_t x, int32_t y)
{
    struct fileman_layout l = current_layout();
    if (handle_context_menu_click(x, y)) {
        return;
    }
    if (handle_menu_click(x, y)) {
        context_menu_set_active(0);
        return;
    }
    menu_open = FILEMAN_MENU_NONE;
    context_menu_set_active(0);
    if (x >= 8 && x < 62 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        navigate_up();
        return;
    }
    if (x >= 72 && x < 132 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        open_selected_entry();
        return;
    }
    if (x >= 142 && x < 218 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        reload_dir();
        return;
    }
    if (l.tree_w && hit_rect_i(x, y, (int32_t)l.tree_x, (int32_t)l.tree_y,
                               (int32_t)l.tree_w, (int32_t)l.tree_h)) {
        struct leonos_ui_tree_item tree_items[6];
        uint32_t tree_count = build_tree_items(tree_items, sizeof(tree_items) / sizeof(tree_items[0]));
        uint32_t id = 0;
        if (leonos_ui_tree_hit(x, y, l.tree_x + 2, l.tree_y + 4, l.tree_w - 4,
                               tree_items, tree_count,
                               TREE_ROW_H, &id)) {
            const char *path = tree_path_for_id(id);
            if (path) {
                navigate_to_path(path);
            }
        }
        return;
    }
    if (x >= (int32_t)l.scrollbar_x && y >= (int32_t)(l.list_y + 2) &&
        y < (int32_t)(l.list_y + l.scrollbar_h)) {
        leonos_ui_vscrollbar_handle_mouse(&file_list.scroll,
                                          entry_count > l.visible_rows ? entry_count : l.visible_rows,
                                          l.visible_rows,
                                          l.scrollbar_x, l.list_y + 2, 18,
                                          l.scrollbar_h - 4,
                                          x, y);
        return;
    }
    {
        uint32_t activate = 0;
        int32_t before = file_list.selected;
        unsigned long now = leonos_uptime_ms();
        int32_t row = (y - (int32_t)l.rows_y) / (int32_t)ROW_H;
        uint32_t index;
        if (row < 0) {
            return;
        }
        index = file_list.scroll + (uint32_t)row;
        if (index >= entry_count) {
            last_click_index = -1;
            last_click_ms = 0;
            return;
        }
        if (!leonos_ui_listview_state_handle_mouse(&file_list, x, y, l.list_x + 2,
                                                   l.rows_y, l.list_w, &activate)) {
            return;
        }
        activate = before == file_list.selected &&
                   file_list.selected >= 0 &&
                   last_click_index == file_list.selected &&
                   now - last_click_ms <= 500;
        last_click_index = file_list.selected;
        last_click_ms = now;
        if (activate) {
            open_selected_entry();
        } else if (file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count) {
            set_status(entries[file_list.selected].name);
        }
    }
}

void handle_key(uint8_t keycode)
{
    uint32_t activate = 0;
    if (leonos_ui_listview_state_handle_key(&file_list, keycode, &activate)) {
        if (activate) {
            open_selected_entry();
        } else if (file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count) {
            set_status(entries[file_list.selected].name);
        }
    }
}

int handle_wheel(int32_t x, int32_t y, int32_t wheel)
{
    struct fileman_layout l = current_layout();
    if (hit_rect_i(x, y, (int32_t)l.list_x, (int32_t)l.list_y,
                   (int32_t)(l.list_w + 24), (int32_t)l.list_h)) {
        return leonos_ui_listview_state_handle_wheel(&file_list, wheel);
    }
    return 0;
}

