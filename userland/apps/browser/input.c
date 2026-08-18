#include "browser.h"

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

int handle_menu_click(int32_t x, int32_t y, uint8_t pressed)
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
            if (!pressed) {
                return 1;
            }
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
            {T("Download Current Page", "下载当前页面"), BROWSER_CMD_DOWNLOAD, 0},
            {T("Close", "关闭"), BROWSER_CMD_CLOSE, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FILE, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     188U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            if (pressed) {
                return 1;
            }
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_HOME) {
                navigate_to("about:leonos", 1);
            } else if (id == BROWSER_CMD_REFRESH) {
                navigate_to(current_location, 0);
            } else if (id == BROWSER_CMD_DOWNLOAD) {
                browser_start_download(current_location);
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
            {T("Find in Page...", "在页面中查找..."), BROWSER_CMD_FIND, 0},
            {T("Find Next", "查找下一个"), BROWSER_CMD_FIND_NEXT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_EDIT, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     192U, items,
                                     sizeof(items) / sizeof(items[0]), &id)) {
            if (pressed) {
                return 1;
            }
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_SELECT_ADDRESS) {
                select_address_text();
            } else if (id == BROWSER_CMD_CLEAR_ADDRESS) {
                address_input[0] = 0;
                leonos_ui_edit_state_sync(&address_edit);
                address_edit.focused = 1;
                set_status(T("Address cleared", "地址已清空"));
            } else if (id == BROWSER_CMD_FIND) {
                browser_find_prompt();
            } else if (id == BROWSER_CMD_FIND_NEXT) {
                browser_find_next();
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
            if (pressed) {
                return 1;
            }
            menu_open = BROWSER_MENU_NONE;
            if (id == BROWSER_CMD_REFRESH) {
                navigate_to(current_location, 0);
            } else if (id == BROWSER_CMD_TOP) {
                browser_scroll_y = 0;
                set_status(T("Top of page", "页面顶部"));
            } else if (id == BROWSER_CMD_BOTTOM) {
                browser_scroll_y = document_scroll_extent();
                set_status(T("Bottom of page", "页面底部"));
            }
            return 1;
        }
        menu_open = BROWSER_MENU_NONE;
        return 1;
    }
    if (menu_open == BROWSER_MENU_FAVORITES) {
        struct leonos_ui_context_menu_item items[BROWSER_MAX_BOOKMARKS + 4U];
        char url[BROWSER_URL_CAP];
        uint32_t count = 0;
        browser_bookmarks_build_menu(items, sizeof(items) / sizeof(items[0]),
                                     &count);
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FAVORITES, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x, BROWSER_MENU_H,
                                     204U, items,
                                     count, &id)) {
            if (pressed) {
                return 1;
            }
            menu_open = BROWSER_MENU_NONE;
            if (browser_bookmarks_handle_command(id, url, sizeof(url)) &&
                url[0]) {
                navigate_to(url, 1);
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
            if (pressed) {
                return 1;
            }
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
    uint32_t vscroll_x = BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U;
    uint32_t hscroll_y = text_y() + document_view_h();
    uint32_t buttons = event->buttons;
    if (event->pressed) {
        buttons |= 1U;
    }
    if (!(buttons & 1U) && !(browser_document && !event->pressed)) {
        return;
    }
    if (!browser_embedded && handle_menu_click(event->x, event->y,
                                               event->pressed)) {
        browser_form_clear_focus();
        present_browser();
        return;
    }
    if (address_edit_hit(event->x, event->y) &&
        leonos_ui_edit_state_handle_mouse(&address_edit, event->x, event->y,
                                          74, address_y(), address_w(),
                                          buttons)) {
        browser_form_clear_focus();
        present_browser();
        return;
    }
    if (handle_toolbar_click(event->x, event->y)) {
        menu_open = BROWSER_MENU_NONE;
        if (!address_edit_hit(event->x, event->y)) {
            address_edit.focused = 0;
        }
        browser_form_clear_focus();
        present_browser();
        return;
    }
    if (address_edit.focused) {
        address_edit.focused = 0;
    }
    menu_open = BROWSER_MENU_NONE;
    if (hit_rect_i(event->x, event->y, vscroll_x, p_y + 2U,
                   BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h)) {
        int changed = leonos_ui_vscrollbar_handle_mouse(
            &browser_scroll_y, document_content_h(), document_view_h(),
            vscroll_x, p_y + 2U, BROWSER_SCROLL_W,
            p_h > 4U ? p_h - 4U : p_h, event->x, event->y);
        if (changed) {
            clamp_scroll();
            present_browser();
        }
        return;
    }
    if (document_content_w() > document_text_w() &&
        hit_rect_i(event->x, event->y, text_x(), hscroll_y,
                   document_text_w(), BROWSER_SCROLL_W)) {
        if (leonos_ui_hscrollbar_handle_mouse(&scroll_x,
                                              document_content_w(),
                                              document_text_w(),
                                              text_x(), hscroll_y,
                                              document_text_w(),
                                              BROWSER_SCROLL_W,
                                              event->x, event->y)) {
            present_browser();
        }
        return;
    }
    {
        int32_t doc_x = event->x - (int32_t)text_x() + (int32_t)scroll_x;
        int32_t doc_y = event->y - (int32_t)text_y() + (int32_t)browser_scroll_y;
        int handled = event->pressed
                          ? browser_litehtml_lbutton_down(browser_document, doc_x, doc_y)
                          : browser_litehtml_lbutton_up(browser_document, doc_x, doc_y);
        if (!handled) {
            browser_form_clear_focus();
        }
    }
    browser_process_pending_form();
    present_browser();
}

void handle_mouse_move(struct leonos_gui_app_event *event)
{
    if (!event || !browser_document) {
        return;
    }
    if (hit_rect_i(event->x, event->y, text_x(), text_y(),
                   document_text_w(), document_view_h())) {
        int32_t doc_x = event->x - (int32_t)text_x() + (int32_t)scroll_x;
        int32_t doc_y = event->y - (int32_t)text_y() + (int32_t)browser_scroll_y;
        (void)browser_litehtml_mouse_move(browser_document, doc_x, doc_y);
    } else {
        (void)browser_litehtml_mouse_leave(browser_document);
    }
    present_browser();
}

void handle_key(struct leonos_gui_app_event *event)
{
    if (!browser_embedded && event->pressed && event->keycode == LEONOS_KEY_F12) {
        browser_devtools_open = browser_devtools_open ? 0 : 1;
        clamp_scroll();
        present_browser();
        return;
    }
    if (browser_form_handle_key(event)) {
        browser_process_pending_form();
        present_browser();
        return;
    }
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
        browser_scroll_wheel(-1);
        present_browser();
    } else if (event->keycode == 81U) {
        browser_scroll_wheel(1);
        present_browser();
    }
}
