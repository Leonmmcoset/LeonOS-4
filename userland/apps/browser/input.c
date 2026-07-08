#include "browser.h"

void activate_link_at(int32_t mx, int32_t my)
{
    uint32_t row;
    uint32_t col;
    uint32_t byte_index;
    uint32_t doc_y;
    uint32_t y_cursor = 0;
    uint32_t align_shift_px;
    uint32_t content_x;
    uint32_t cell_w;
    uint8_t link;
    struct browser_line *line;
    if (!hit_rect_i(mx, my, text_x(), text_y(),
                    document_text_w(),
                    page_h() > 16U ? page_h() - 16U : page_h())) {
        return;
    }
    doc_y = (uint32_t)my - text_y();
    row = scroll_line;
    while (row < line_count) {
        uint32_t line_h = browser_line_height(lines[row].kind);
        if (doc_y < y_cursor + line_h) {
            break;
        }
        y_cursor += line_h;
        ++row;
    }
    if (row >= line_count) {
        return;
    }
    line = &lines[row];
    cell_w = browser_line_cell_w(line->kind);
    if (!cell_w) {
        cell_w = LEONOS_FONT_W;
    }
    content_x = text_x() + (uint32_t)line->indent * LEONOS_FONT_W;
    align_shift_px = line_align_shift_px(line, document_text_w());
    if ((uint32_t)mx < content_x + align_shift_px) {
        return;
    }
    col = ((uint32_t)mx - content_x - align_shift_px) / cell_w;
    if (line->kind == BROWSER_LINE_IMAGE) {
        uint32_t image_cols = 20U / LEONOS_FONT_W;
        if (col < image_cols) {
            return;
        }
        col -= image_cols;
    }
    if (col >= line->cells) {
        return;
    }
    byte_index = browser_line_byte_at_cell(line, col);
    if (byte_index >= line->len) {
        return;
    }
    link = line->link[byte_index];
    if (!link || (uint32_t)(link - 1U) >= link_count) {
        return;
    }
    if (browser_form_handle_href(links[link - 1U].href)) {
        return;
    }
    navigate_to(links[link - 1U].href, 1);
}

int handle_toolbar_click(int32_t x, int32_t y)
{
    if (browser_embedded) {
        if (!hit_rect_i(x, y, 0, 0, view_w, BROWSER_TOOLBAR_H)) {
            return 0;
        }
        if (hit_rect_i(x, y, BROWSER_BACK_X, button_y(),
                       BROWSER_BACK_W, LEONOS_UI_BUTTON_H)) {
            go_back();
            return 1;
        }
        if (hit_rect_i(x, y, toolbar_forward_x(), button_y(),
                       BROWSER_FORWARD_W, LEONOS_UI_BUTTON_H)) {
            go_forward();
            return 1;
        }
        if (hit_rect_i(x, y, toolbar_refresh_x(), button_y(),
                       BROWSER_REFRESH_W, LEONOS_UI_BUTTON_H)) {
            navigate_to(current_location, 0);
            return 1;
        }
        if (hit_rect_i(x, y, toolbar_home_x(), button_y(),
                       BROWSER_HOME_W, LEONOS_UI_BUTTON_H)) {
            browser_should_exit = 1;
            return 1;
        }
        return 0;
    }
    if (!hit_rect_i(x, y, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H + BROWSER_ADDR_H)) {
        return 0;
    }
    if (hit_rect_i(x, y, BROWSER_BACK_X, button_y(), BROWSER_BACK_W, LEONOS_UI_BUTTON_H)) {
        go_back();
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_forward_x(), button_y(), BROWSER_FORWARD_W, LEONOS_UI_BUTTON_H)) {
        go_forward();
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_refresh_x(), button_y(), BROWSER_REFRESH_W, LEONOS_UI_BUTTON_H)) {
        navigate_to(current_location, 0);
        return 1;
    }
    if (hit_rect_i(x, y, toolbar_home_x(), button_y(), BROWSER_HOME_W, LEONOS_UI_BUTTON_H)) {
        navigate_to("about:leonos", 1);
        return 1;
    }
    if (hit_rect_i(x, y, go_x(), address_y(), BROWSER_GO_W, LEONOS_UI_BUTTON_H)) {
        navigate_to(address_input, 1);
        return 1;
    }
    return 0;
}

int address_edit_hit(int32_t x, int32_t y)
{
    if (browser_embedded) {
        return 0;
    }
    return hit_rect_i(x, y, 74, address_y(), address_w(), LEONOS_FONT_H + 8U);
}

void select_address_text(void)
{
    leonos_ui_edit_state_sync(&address_edit);
    address_edit.focused = 1;
    address_edit.selection_anchor = 0;
    address_edit.cursor = address_edit.length;
    address_edit.scroll = 0;
    address_edit.selecting = 0;
    set_status(T("Address selected", "已选中地址"));
}

int handle_menu_click(int32_t x, int32_t y)
{
    if (browser_embedded) {
        return 0;
    }
    struct leonos_ui_menubar_item top_items[] = {
        {T("File", "文件"), BROWSER_MENU_FILE, BROWSER_MENU_FILE_W, 0},
        {T("Edit", "编辑"), BROWSER_MENU_EDIT, BROWSER_MENU_EDIT_W, 0},
        {T("View", "查看"), BROWSER_MENU_VIEW, BROWSER_MENU_VIEW_W, 0},
        {T("Favorites", "收藏夹"), BROWSER_MENU_FAVORITES, BROWSER_MENU_FAVORITES_W, 0},
        {T("Help", "帮助"), BROWSER_MENU_HELP, BROWSER_MENU_HELP_W, 0},
    };
    struct leonos_ui_rect r;
    uint32_t id = 0;
    if (leonos_ui_menubar_hit(x, y, 0, 0, top_items,
                              sizeof(top_items) / sizeof(top_items[0]),
                              &id)) {
        if (id) {
            menu_open = menu_open == id ? BROWSER_MENU_NONE : (uint8_t)id;
            address_edit.focused = 0;
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Home", "主页"), BROWSER_CMD_HOME, 0},
            {T("Refresh", "刷新"), BROWSER_CMD_REFRESH, 0},
            {T("Close", "关闭"), BROWSER_CMD_CLOSE, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FILE, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     188U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_HOME) {
                navigate_to("about:leonos", 1);
            } else if (id == BROWSER_CMD_REFRESH) {
                navigate_to(current_location, 0);
            } else if (id == BROWSER_CMD_CLOSE) {
                browser_should_exit = 1;
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_EDIT) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Select Address", "选中地址"), BROWSER_CMD_SELECT_ADDRESS, 0},
            {T("Clear Address", "清空地址"), BROWSER_CMD_CLEAR_ADDRESS, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_EDIT, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     192U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_SELECT_ADDRESS) {
                select_address_text();
            } else if (id == BROWSER_CMD_CLEAR_ADDRESS) {
                address_input[0] = 0;
                leonos_ui_edit_state_sync(&address_edit);
                address_edit.focused = 1;
                set_status(T("Address cleared", "地址已清空"));
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_VIEW) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Refresh", "刷新"), BROWSER_CMD_REFRESH, 0},
            {T("Top", "顶部"), BROWSER_CMD_TOP, 0},
            {T("Bottom", "底部"), BROWSER_CMD_BOTTOM, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_VIEW, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     166U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_REFRESH) {
                navigate_to(current_location, 0);
            } else if (id == BROWSER_CMD_TOP) {
                scroll_line = 0;
                set_status(T("Top of page", "页面顶部"));
            } else if (id == BROWSER_CMD_BOTTOM) {
                uint32_t rows = visible_rows();
                scroll_line = line_count > rows ? line_count - rows : 0;
                set_status(T("Bottom of page", "页面底部"));
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_FAVORITES) {
        struct leonos_ui_context_menu_item items[] = {
            {T("LeonOS Home", "LeonOS 主页"), BROWSER_CMD_FAV_HOME, 0},
            {"example.com", BROWSER_CMD_FAV_EXAMPLE, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FAVORITES, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     204U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_FAV_HOME) {
                navigate_to("about:leonos", 1);
            } else if (id == BROWSER_CMD_FAV_EXAMPLE) {
                navigate_to("http://example.com/", 1);
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_HELP) {
        struct leonos_ui_context_menu_item items[] = {
            {T("About Browser", "关于浏览器"), BROWSER_CMD_ABOUT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_HELP, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     176U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_ABOUT) {
                leonos_ui_show_message_box(T("LeonOS Browser", "LeonOS 浏览器"),
                                           T("Classic HTTP browser for LeonOS 4.",
                                             "LeonOS 4 经典 HTTP 浏览器。"),
                                           T("OK", "确定"));
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    return 0;
}

void handle_mouse_button(struct leonos_gui_app_event *event)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t scroll_x = BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U;
    uint32_t buttons = event->buttons;
    if (event->pressed) {
        buttons |= 1U;
    }
    if (!(buttons & 1U)) {
        return;
    }
    if (!browser_embedded && handle_menu_click(event->x, event->y)) {
        present_browser();
        return;
    }
    if (address_edit_hit(event->x, event->y) &&
        leonos_ui_edit_state_handle_mouse(&address_edit, event->x, event->y,
                                          74, address_y(), address_w(),
                                          buttons)) {
        present_browser();
        return;
    }
    if (handle_toolbar_click(event->x, event->y)) {
        menu_open = BROWSER_MENU_NONE;
        if (!address_edit_hit(event->x, event->y)) {
            address_edit.focused = 0;
        }
        present_browser();
        return;
    }
    if (address_edit.focused) {
        address_edit.focused = 0;
    }
    menu_open = BROWSER_MENU_NONE;
    if (hit_rect_i(event->x, event->y, scroll_x, p_y + 2U,
                   BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h)) {
        if (leonos_ui_vscrollbar_handle_mouse(&scroll_line,
                                              line_count ? line_count : 1U,
                                              visible_rows(),
                                              scroll_x, p_y + 2U,
                                              BROWSER_SCROLL_W,
                                              p_h > 4U ? p_h - 4U : p_h,
                                              event->x, event->y)) {
            present_browser();
        }
        return;
    }
    activate_link_at(event->x, event->y);
    present_browser();
}

void handle_key(struct leonos_gui_app_event *event)
{
    if (!event->pressed) {
        if (!browser_embedded) {
            leonos_ui_edit_state_handle_key(&address_edit, event->keycode,
                                            event->pressed);
        }
        return;
    }
    if (event->keycode == 1) {
        return;
    }
    if (!browser_embedded &&
        event->keycode == LEONOS_KEY_ENTER && address_edit.focused) {
        navigate_to(address_input, 1);
        present_browser();
        return;
    }
    if (!browser_embedded &&
        leonos_ui_edit_state_handle_key(&address_edit, event->keycode, event->pressed)) {
        present_browser();
        return;
    }
    if (event->keycode == 73U) {
        uint32_t rows = visible_rows();
        scroll_line = scroll_line > rows ? scroll_line - rows : 0;
        present_browser();
    } else if (event->keycode == 81U) {
        scroll_line += visible_rows();
        clamp_scroll();
        present_browser();
    }
}
