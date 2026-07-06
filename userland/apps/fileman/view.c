#include "fileman.h"

void draw_fileman(struct leonos_ui_surface *ui)
{
    struct fileman_layout l = current_layout();
    struct leonos_ui_list_column cols[] = {
        {T("Type", "类型"), 58},
        {T("Name", "名称"), l.list_w > 58 ? l.list_w - 58 : 120},
    };
    struct leonos_ui_tree_item tree_items[6];
    uint32_t tree_count = build_tree_items(tree_items, sizeof(tree_items) / sizeof(tree_items[0]));
    for (uint32_t i = 0; i < tree_count; ++i) {
        const char *path = tree_path_for_id(tree_items[i].id);
        if (text_eq(current_path, path)) {
            tree_items[i].flags |= LEONOS_UI_TREE_SELECTED;
        }
    }
    file_list.visible_rows = l.visible_rows;
    leonos_ui_listview_state_set_count(&file_list, entry_count);
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, view_w);
    leonos_ui_menubar_item(ui, 8, 0, 54, T("File", "文件"), menu_open == FILEMAN_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, T("View", "查看"), menu_open == FILEMAN_MENU_VIEW);

    leonos_ui_toolbar(ui, 0, 30, view_w, 42);
    leonos_ui_toolbar_button(ui, 8, TOOLBAR_Y, 54, T("Up", "上级"), 0);
    leonos_ui_toolbar_button(ui, 72, TOOLBAR_Y, 60, T("Open", "打开"), 0);
    leonos_ui_toolbar_button(ui, 142, TOOLBAR_Y, 76, T("Refresh", "刷新"), 0);
    leonos_ui_edit(ui, 230, TOOLBAR_Y, view_w > 238 ? view_w - 238 : 120,
                   current_path, text_len(current_path), 0, LEONOS_UI_EDIT_READONLY);

    if (l.tree_w) {
        leonos_ui_scroll_view_frame(ui, l.tree_x, l.tree_y, l.tree_w, l.tree_h);
        leonos_ui_tree(ui, l.tree_x + 2, l.tree_y + 4, l.tree_w - 4, tree_items,
                       tree_count, TREE_ROW_H);
    }
    leonos_ui_scroll_view_frame(ui, l.list_x, l.list_y, l.list_w + 22, l.list_h);
    leonos_ui_listview_header(ui, l.list_x + 2, l.list_y + 2, l.list_w, cols, 2);
    for (uint32_t row = 0; row < file_list.visible_rows; ++row) {
        uint32_t i = file_list.scroll + row;
        const char *cells[2];
        if (i >= entry_count) {
            break;
        }
        cells[0] = entry_type_name(&entries[i]);
        cells[1] = entries[i].name;
        leonos_ui_listview_row(ui, l.list_x + 2, l.rows_y + row * ROW_H, l.list_w, cols, cells, 2,
                               file_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, l.scrollbar_x, l.list_y + 2, 18, l.scrollbar_h - 4,
                         file_list.scroll, entry_count > l.visible_rows ? entry_count : l.visible_rows,
                         l.visible_rows,
                         entry_count <= l.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    leonos_ui_statusbar(ui, view_h - STATUS_H, STATUS_H, status_text);

    if (menu_open == FILEMAN_MENU_FILE) {
        uint32_t has_item = selected_entry_valid();
        uint32_t has_file = selected_entry_is_file();
        uint32_t has_mutable = selected_entry_is_mutable();
        leonos_ui_menu(ui, 8, MENU_BAR_H, 204, 268);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 162, T("Open", "打开"),
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 162, T("Open With...", "打开方式..."),
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 162, T("Default Program...", "默认程序..."),
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 162, T("Create Shortcut", "创建快捷方式"),
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 112, 162, T("Details", "详细信息"),
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 138, 162, T("Rename", "重命名"),
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 164, 162, T("Delete", "删除"),
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 190, 162, "", LEONOS_UI_MENU_SEPARATOR);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 216, 162, T("New Folder", "新建文件夹"), 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 242, 162, T("Refresh", "刷新"), 0);
    } else if (menu_open == FILEMAN_MENU_VIEW) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, T("Refresh", "刷新"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, T("Root", "根目录"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, T("About", "关于"), 0);
    }
    if (context_menu_active || context_menu_animating) {
        struct leonos_ui_context_menu_item items[FILEMAN_CONTEXT_MENU_COUNT];
        build_context_menu_items(items, FILEMAN_CONTEXT_MENU_COUNT);
        uint32_t progress = context_menu_animating
                                ? leonos_ui_anim_progress(leonos_uptime_ms(), context_menu_anim_start, 120)
                                : 1000;
        if (progress >= 1000) {
            context_menu_animating = 0;
            progress = context_menu_active ? 1000 : 0;
        } else if (!context_menu_opening) {
            progress = 1000 - progress;
        }
        leonos_ui_context_menu_animated(ui, context_menu_x, context_menu_y, FILEMAN_CONTEXT_MENU_W,
                                        items, FILEMAN_CONTEXT_MENU_COUNT, progress);
    }
}


void present_fileman(uint32_t window_id, struct leonos_ui_surface *ui)
{
    leonos_ui_bind(ui, pixels, view_w, view_h, FILEMAN_MAX_W);
    draw_fileman(ui);
    leonos_gui_present_window(window_id, view_w, view_h, FILEMAN_MAX_W, pixels);
}

