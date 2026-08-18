#include "browser.h"

uint32_t button_y(void)
{
    if (browser_embedded) {
        return 3U;
    }
    return BROWSER_MENU_H + 3U;
}

uint32_t address_y(void)
{
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + 5U;
}

uint32_t address_w(void)
{
    uint32_t x = 74U;
    uint32_t go_w = BROWSER_GO_W;
    if (view_w <= x + go_w + 20U) {
        return 120U;
    }
    return view_w - x - go_w - 20U;
}

uint32_t go_x(void)
{
    return 74U + address_w() + 8U;
}

uint32_t toolbar_forward_x(void)
{
    return BROWSER_BACK_X + BROWSER_BACK_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_refresh_x(void)
{
    return toolbar_forward_x() + BROWSER_FORWARD_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_home_x(void)
{
    return toolbar_refresh_x() + BROWSER_REFRESH_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_stop_x(void)
{
    return toolbar_home_x() + BROWSER_HOME_W + BROWSER_NAV_GAP;
}

uint32_t toolbar_title_x(void)
{
    return toolbar_stop_x() + BROWSER_STOP_W + 12U;
}

uint32_t menu_row_y(uint32_t row)
{
    return BROWSER_MENU_H + 8U + row * BROWSER_MENU_ROW_STEP;
}

int hit_rect_i(int32_t px, int32_t py, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

void draw_toolbar_button(uint32_t x, uint32_t w, const char *label,
                                uint32_t disabled)
{
    leonos_ui_toolbar_button(&ui, x, button_y(), w, label,
                             disabled ? LEONOS_UI_TOOLBAR_BUTTON_DISABLED : 0);
}

uint32_t document_text_w(void)
{
    return page_w() > BROWSER_SCROLL_W + 24U
               ? page_w() - BROWSER_SCROLL_W - 24U
               : 80U;
}

uint32_t document_view_h(void)
{
    return page_h() > 16U ? page_h() - 16U : page_h();
}

uint32_t document_content_w(void)
{
    uint32_t width = browser_document
                         ? browser_litehtml_content_width(browser_document)
                         : 0U;
    return width > document_text_w() ? width : document_text_w();
}

uint32_t document_content_h(void)
{
    uint32_t height = browser_document
                          ? browser_litehtml_content_height(browser_document)
                          : 0U;
    return height > document_view_h() ? height : document_view_h();
}

uint32_t document_scroll_extent(void)
{
    uint32_t content = document_content_h();
    uint32_t viewport = document_view_h();
    return content > viewport ? content - viewport : 0U;
}

uint32_t document_scroll_viewport(void)
{
    return document_view_h();
}

void draw_document_lines(void)
{
    if (!browser_document) {
        return;
    }
    browser_litehtml_draw(browser_document, &ui,
                          (int32_t)text_x(), (int32_t)text_y(),
                          (int32_t)scroll_x, (int32_t)browser_scroll_y,
                          text_x(), text_y(), document_text_w(),
                          document_view_h());
}

void draw_browser_menu(void)
{
    if (browser_embedded) {
        return;
    }
    struct leonos_ui_menubar_item top_items[] = {
        {T("File", "文件"), BROWSER_MENU_FILE, BROWSER_MENU_FILE_W, 0},
        {T("Edit", "编辑"), BROWSER_MENU_EDIT, BROWSER_MENU_EDIT_W, 0},
        {T("View", "查看"), BROWSER_MENU_VIEW, BROWSER_MENU_VIEW_W, 0},
        {T("Favorites", "收藏夹"), BROWSER_MENU_FAVORITES, BROWSER_MENU_FAVORITES_W, 0},
        {T("Help", "帮助"), BROWSER_MENU_HELP, BROWSER_MENU_HELP_W, 0},
    };
    struct leonos_ui_rect r;
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
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 188U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_EDIT) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Select Address", "选中地址"), BROWSER_CMD_SELECT_ADDRESS, 0},
            {T("Clear Address", "清空地址"), BROWSER_CMD_CLEAR_ADDRESS, 0},
            {T("Find in Page...", "在页面中查找..."), BROWSER_CMD_FIND, 0},
            {T("Find Next", "查找下一个"), BROWSER_CMD_FIND_NEXT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_EDIT, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 192U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_VIEW) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Refresh", "刷新"), BROWSER_CMD_REFRESH, 0},
            {T("Top", "顶部"), BROWSER_CMD_TOP, 0},
            {T("Bottom", "底部"), BROWSER_CMD_BOTTOM, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_VIEW, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 166U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == BROWSER_MENU_FAVORITES) {
        struct leonos_ui_context_menu_item items[BROWSER_MAX_BOOKMARKS + 4U];
        uint32_t count = 0;
        browser_bookmarks_build_menu(items, sizeof(items) / sizeof(items[0]),
                                     &count);
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FAVORITES, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 204U,
                             items, count, 0);
    } else if (menu_open == BROWSER_MENU_HELP) {
        struct leonos_ui_context_menu_item items[] = {
            {T("About Browser", "关于浏览器"), BROWSER_CMD_ABOUT, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_HELP, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 176U,
                             items, sizeof(items) / sizeof(items[0]), 0);
    }
}

void draw_browser_devtools(void)
{
    char line[BROWSER_URL_CAP + BROWSER_TITLE_CAP + 64U];
    uint32_t height = browser_devtools_height();
    uint32_t y;
    uint32_t width;
    uint32_t source_bytes = 0;
    uint32_t pos;
    if (!height || browser_embedded) {
        return;
    }
    y = view_h > height ? view_h - height : 0;
    width = view_w > 12U ? view_w - 12U : view_w;
    while (source_bytes < BROWSER_SOURCE_CAP && page_source[source_bytes]) {
        ++source_bytes;
    }
    leonos_ui_inset(&ui, 6, y + 2U, width, height > 4U ? height - 4U : height,
                    LEONOS_UI_WHITE);
    leonos_ui_rect(&ui, 8, y + 4U, width > 4U ? width - 4U : width, 20U,
                   LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text_clipped(&ui, 16, y + 7U, width > 20U ? width - 20U : width,
                           T("Developer Tools", "开发者工具"),
                           LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Location: ", "地址: "));
    append_text(line, &pos, sizeof(line), current_location);
    leonos_ui_text_clipped(&ui, 16, y + 29U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Document: ", "文档: "));
    append_text(line, &pos, sizeof(line), page_title);
    append_text(line, &pos, sizeof(line), page_is_html ? " [HTML, " : " [text, ");
    append_u32(line, &pos, sizeof(line), source_bytes);
    append_text(line, &pos, sizeof(line), " bytes]");
    if (source_truncated) {
        append_text(line, &pos, sizeof(line), T(" source limit reached", " 源码已截断"));
    }
    leonos_ui_text_clipped(&ui, 16, y + 46U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Viewport: ", "视口: "));
    append_u32(line, &pos, sizeof(line), document_text_w());
    append_text(line, &pos, sizeof(line), " x ");
    append_u32(line, &pos, sizeof(line), document_view_h());
    append_text(line, &pos, sizeof(line), T("; content: ", "; 内容: "));
    append_u32(line, &pos, sizeof(line), document_content_w());
    append_text(line, &pos, sizeof(line), " px");
    leonos_ui_text_clipped(&ui, 16, y + 63U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("LiteHTML: ", "LiteHTML："));
    append_text(line, &pos, sizeof(line), browser_document
                                               ? T("active", "已加载")
                                               : T("unavailable", "不可用"));
    append_text(line, &pos, sizeof(line), T(", forms: ", "，表单："));
    append_u32(line, &pos, sizeof(line), browser_form_count);
    append_text(line, &pos, sizeof(line), T(", controls: ", "，控件："));
    append_u32(line, &pos, sizeof(line), browser_form_control_count);
    leonos_ui_text_clipped(&ui, 16, y + 80U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);

    pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), T("Scroll: y=", "滚动：y="));
    append_u32(line, &pos, sizeof(line), browser_scroll_y);
    append_text(line, &pos, sizeof(line), " / ");
    append_u32(line, &pos, sizeof(line), document_scroll_extent());
    append_text(line, &pos, sizeof(line), ", x=");
    append_u32(line, &pos, sizeof(line), scroll_x);
    append_text(line, &pos, sizeof(line), T("; status: ", "; 状态: "));
    append_text(line, &pos, sizeof(line), status_text);
    leonos_ui_text_clipped(&ui, 16, y + 97U, width > 20U ? width - 20U : width,
                           line, BROWSER_TEXT_DARK, LEONOS_UI_WHITE);
}

void draw_browser(void)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t content_w = document_content_w();
    uint32_t doc_w = document_text_w();
    uint32_t has_hscroll = content_w > doc_w;
    uint32_t scroll_total = document_content_h();
    uint32_t scroll_view = document_scroll_viewport();
    uint32_t scroll_pos = browser_scroll_y;
    uint32_t can_back = history_index > 0;
    uint32_t can_forward = history_index >= 0 && (uint32_t)history_index + 1U < history_count;
    char title_line[BROWSER_TITLE_CAP + 32U];
    uint32_t pos = 0;
    struct leonos_ui_menubar_item top_items[] = {
        {T("File", "文件"), BROWSER_MENU_FILE, BROWSER_MENU_FILE_W, 0},
        {T("Edit", "编辑"), BROWSER_MENU_EDIT, BROWSER_MENU_EDIT_W, 0},
        {T("View", "查看"), BROWSER_MENU_VIEW, BROWSER_MENU_VIEW_W, 0},
        {T("Favorites", "收藏夹"), BROWSER_MENU_FAVORITES, BROWSER_MENU_FAVORITES_W, 0},
        {T("Help", "帮助"), BROWSER_MENU_HELP, BROWSER_MENU_HELP_W, 0},
    };
    leonos_ui_rect(&ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    if (browser_embedded) {
        leonos_ui_toolbar(&ui, 0, 0, view_w, BROWSER_TOOLBAR_H);
        draw_toolbar_button(BROWSER_BACK_X, BROWSER_BACK_W, T("Back", "后退"), !can_back);
        draw_toolbar_button(toolbar_forward_x(), BROWSER_FORWARD_W, T("Forward", "前进"), !can_forward);
        draw_toolbar_button(toolbar_refresh_x(), BROWSER_REFRESH_W, T("Refresh", "刷新"), 0);
        draw_toolbar_button(toolbar_home_x(), BROWSER_HOME_W,
                            T("Setup", "返回"), 0);
        leonos_ui_text_clipped(&ui, toolbar_stop_x(), button_y() + 5U,
                               view_w > toolbar_stop_x() + 12U ? view_w - toolbar_stop_x() - 12U : 80U,
                               T("License Website", "许可证网站"),
                               BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
        leonos_ui_inset(&ui, BROWSER_PAGE_X, p_y, p_w, p_h, LEONOS_UI_WHITE);
        draw_document_lines();
        leonos_ui_vscrollbar(&ui, BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U,
                             p_y + 2U, BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h,
                             scroll_pos, scroll_total, scroll_view,
                             scroll_total <= scroll_view ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
        if (has_hscroll) {
            leonos_ui_hscrollbar(&ui, text_x(), text_y() + document_view_h(),
                                 doc_w, BROWSER_SCROLL_W,
                                 scroll_x, content_w, doc_w, 0);
        }
        leonos_ui_toast_draw(&ui, &browser_toast, leonos_uptime_ms());
        return;
    }
    leonos_ui_menubar_draw(&ui, 0, 0, view_w, top_items,
                           sizeof(top_items) / sizeof(top_items[0]),
                           menu_open);
    leonos_ui_toolbar(&ui, 0, BROWSER_MENU_H, view_w, BROWSER_TOOLBAR_H);
    draw_toolbar_button(BROWSER_BACK_X, BROWSER_BACK_W, T("Back", "后退"), !can_back);
    draw_toolbar_button(toolbar_forward_x(), BROWSER_FORWARD_W, T("Forward", "前进"), !can_forward);
    draw_toolbar_button(toolbar_refresh_x(), BROWSER_REFRESH_W, T("Refresh", "刷新"), 0);
    draw_toolbar_button(toolbar_home_x(), BROWSER_HOME_W, T("Home", "主页"), 0);
    draw_toolbar_button(toolbar_stop_x(), BROWSER_STOP_W, T("Stop", "停止"), 1);
    title_line[0] = 0;
    append_text(title_line, &pos, sizeof(title_line), "LeonOS Browser - ");
    append_text(title_line, &pos, sizeof(title_line), page_title);
    leonos_ui_text_clipped(&ui, toolbar_title_x(), button_y() + 5U,
                           view_w > toolbar_title_x() + 12U ? view_w - toolbar_title_x() - 12U : 80U,
                           title_line, BROWSER_TEXT_DARK, LEONOS_UI_GRAY);
    leonos_ui_rect(&ui, 0, BROWSER_MENU_H + BROWSER_TOOLBAR_H, view_w,
                   BROWSER_ADDR_H, BROWSER_IE_SKY);
    leonos_ui_text(&ui, 12, address_y() + 5U, T("Address", "地址"),
                   BROWSER_TEXT_DARK, BROWSER_IE_SKY);
    leonos_ui_edit_state_draw(&ui, 74, address_y(), address_w(), &address_edit, 0);
    leonos_ui_button(&ui, go_x(), address_y(), BROWSER_GO_W, LEONOS_UI_BUTTON_H,
                     T("Go", "转到"), 0);
    leonos_ui_inset(&ui, BROWSER_PAGE_X, p_y, p_w, p_h, LEONOS_UI_WHITE);
    draw_document_lines();
    leonos_ui_vscrollbar(&ui, BROWSER_PAGE_X + p_w - BROWSER_SCROLL_W - 2U,
                         p_y + 2U, BROWSER_SCROLL_W, p_h > 4U ? p_h - 4U : p_h,
                         scroll_pos, scroll_total, scroll_view,
                         scroll_total <= scroll_view ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    if (has_hscroll) {
        leonos_ui_hscrollbar(&ui, text_x(), text_y() + document_view_h(),
                             doc_w, BROWSER_SCROLL_W,
                             scroll_x, content_w, doc_w, 0);
    }
    draw_browser_menu();
    draw_browser_devtools();
    leonos_ui_toast_draw(&ui, &browser_toast, leonos_uptime_ms());
}

void present_browser(void)
{
    if (browser_embedded) {
        if (!ui.pixels) {
            return;
        }
        draw_browser();
        return;
    }
    if (window_id <= 0) {
        return;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, pixel_stride);
    draw_browser();
    leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                              pixel_stride, pixels);
}
