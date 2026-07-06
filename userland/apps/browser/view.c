#include "browser.h"

uint32_t button_y(void)
{
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

void draw_line_run(uint32_t x, uint32_t y, const char *text,
                          uint32_t len, uint32_t fg, uint32_t bg,
                          uint8_t underline, uint8_t bold,
                          uint8_t italic, uint8_t code,
                          uint32_t cell_w, uint32_t cell_h,
                          uint32_t cell_count)
{
    char tmp[BROWSER_LINE_CHARS];
    uint32_t copy_len = len;
    if (!cell_w) {
        cell_w = LEONOS_FONT_W;
    }
    if (!cell_h) {
        cell_h = LEONOS_FONT_H;
    }
    if (!cell_count) {
        cell_count = copy_len;
    }
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1U;
    }
    for (uint32_t i = 0; i < copy_len; ++i) {
        tmp[i] = text[i];
    }
    tmp[copy_len] = 0;
    if (code && copy_len) {
        if (bg == LEONOS_UI_WHITE) {
            bg = BROWSER_CODE_BG;
        }
        leonos_ui_rect(&ui, x > 1U ? x - 1U : x, y > 1U ? y - 1U : y,
                       cell_count * cell_w + 2U, cell_h + 2U,
                       bg);
    }
    leonos_ui_text_resized_clipped(&ui, x, y, cell_count * cell_w,
                                   tmp, fg, bg, cell_w, cell_h);
    if (bold) {
        leonos_ui_text_resized_clipped(&ui, x + 1U, y, cell_count * cell_w,
                                       tmp, fg, bg, cell_w, cell_h);
    }
    if (italic) {
        for (uint32_t n = 0; n < cell_count; ++n) {
            uint32_t sx = x + n * cell_w + 1U;
            leonos_ui_rect(&ui, sx, y + cell_h - 3U, 3U, 1U, fg);
        }
    }
    if (underline && copy_len) {
        leonos_ui_rect(&ui, x, y + cell_h - 1U,
                       cell_count * cell_w, 1U, fg);
    }
}

uint8_t line_is_heading(uint8_t kind)
{
    return kind == BROWSER_LINE_HEADING1 ||
           kind == BROWSER_LINE_HEADING2 ||
           kind == BROWSER_LINE_HEADING3;
}

uint32_t browser_line_cell_w(uint8_t kind)
{
    if (kind == BROWSER_LINE_HEADING1) {
        return 16U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return 12U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return 10U;
    }
    return LEONOS_FONT_W;
}

uint32_t browser_line_cell_h(uint8_t kind)
{
    if (kind == BROWSER_LINE_HEADING1) {
        return 32U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return 24U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return 20U;
    }
    return LEONOS_FONT_H;
}

uint32_t browser_line_height(uint8_t kind)
{
    uint32_t cell_h = browser_line_cell_h(kind);
    if (kind == BROWSER_LINE_HEADING1) {
        return cell_h + 8U;
    }
    if (kind == BROWSER_LINE_HEADING2) {
        return cell_h + 6U;
    }
    if (kind == BROWSER_LINE_HEADING3) {
        return cell_h + 4U;
    }
    return cell_h + 2U;
}

uint32_t browser_line_cells_between(const struct browser_line *line,
                                    uint32_t start, uint32_t end)
{
    uint32_t cells = 0;
    if (!line || start >= line->len) {
        return 0;
    }
    if (end > line->len) {
        end = line->len;
    }
    for (uint32_t i = start; i < end; ++i) {
        cells += line->cell_width[i];
    }
    return cells;
}

uint32_t browser_line_next_byte(const struct browser_line *line,
                                uint32_t pos)
{
    if (!line || pos >= line->len) {
        return line ? line->len : 0;
    }
    ++pos;
    while (pos < line->len && line->cell_width[pos] == 0) {
        ++pos;
    }
    return pos;
}

uint32_t browser_line_byte_at_cell(const struct browser_line *line,
                                   uint32_t cell)
{
    uint32_t cells = 0;
    if (!line) {
        return 0;
    }
    for (uint32_t i = 0; i < line->len; i = browser_line_next_byte(line, i)) {
        uint32_t cw = line->cell_width[i] ? line->cell_width[i] : 1U;
        if (cell < cells + cw) {
            return i;
        }
        cells += cw;
    }
    return line->len;
}

uint32_t document_text_w(void)
{
    return page_w() > BROWSER_SCROLL_W + 24U
               ? page_w() - BROWSER_SCROLL_W - 24U
               : 80U;
}

void draw_document_line_frame(const struct browser_line *line,
                                     uint32_t x, uint32_t y,
                                     uint32_t width)
{
    uint32_t content_x;
    uint32_t content_w;
    uint32_t bg;
    uint32_t border;
    uint32_t cell_h;
    uint32_t line_h;
    if (!line) {
        return;
    }
    content_x = x + (uint32_t)line->indent * LEONOS_FONT_W;
    content_w = width > (content_x - x) ? width - (content_x - x) : width;
    cell_h = browser_line_cell_h(line->kind);
    line_h = browser_line_height(line->kind);
    bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : LEONOS_UI_WHITE;
    border = line->border_color != BROWSER_COLOR_UNSET
                 ? line->border_color
                 : BROWSER_TABLE_BORDER;
    if (line->kind == BROWSER_LINE_HR) {
        uint32_t hr = line->border_color != BROWSER_COLOR_UNSET
                          ? line->border_color
                          : LEONOS_UI_DARK;
        leonos_ui_rect(&ui, x, y + line_h / 2U, width, 1U, hr);
        leonos_ui_rect(&ui, x, y + line_h / 2U + 1U, width, 1U,
                       LEONOS_UI_LIGHT);
        return;
    }
    if (line->line_bg != BROWSER_COLOR_UNSET) {
        leonos_ui_rect(&ui, content_x, y - 1U,
                       content_w > 6U ? content_w - 6U : content_w,
                       line_h > 1U ? line_h - 1U : line_h, bg);
    }
    if (line->border_color != BROWSER_COLOR_UNSET &&
        line->kind != BROWSER_LINE_TABLE &&
        line->kind != BROWSER_LINE_BLOCKQUOTE) {
        uint32_t bar_x = content_x >= 5U ? content_x - 5U : content_x;
        leonos_ui_rect(&ui, bar_x, y - 1U, 3U,
                       line_h > 1U ? line_h - 1U : line_h, border);
    }
    if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
        uint32_t bar_x = content_x >= 8U ? content_x - 8U : x;
        leonos_ui_rect(&ui, bar_x, y - 1U, 3U,
                       line_h > 1U ? line_h - 1U : line_h,
                       border);
        leonos_ui_rect(&ui, bar_x + 3U, y - 1U,
                       content_w > 3U ? content_w - 3U : content_w,
                       line_h > 1U ? line_h - 1U : line_h,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_QUOTE_BG);
        return;
    }
    if (line->kind == BROWSER_LINE_TABLE) {
        uint32_t row_w = content_w > 6U ? content_w - 6U : content_w;
        leonos_ui_rect(&ui, content_x, y - 1U, row_w,
                       line_h > 1U ? line_h - 1U : line_h,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_TABLE_BG);
        leonos_ui_rect(&ui, content_x, y - 1U, row_w, 1U, border);
        leonos_ui_rect(&ui, content_x, y + cell_h + 1U,
                       row_w, 1U, border);
        for (uint32_t i = 0; i < line->len; i = browser_line_next_byte(line, i)) {
            if (line->text[i] == '|') {
                uint32_t prefix_cells = browser_line_cells_between(line, 0, i);
                uint32_t vx = content_x + prefix_cells * LEONOS_FONT_W +
                              LEONOS_FONT_W / 2U;
                leonos_ui_rect(&ui, vx, y - 1U, 1U,
                               line_h > 1U ? line_h - 1U : line_h,
                               border);
            }
        }
        return;
    }
    if (line->kind == BROWSER_LINE_IMAGE) {
        leonos_ui_rect(&ui, content_x, y - 1U, content_w > 6U ? content_w - 6U : content_w,
                       line_h,
                       line->line_bg != BROWSER_COLOR_UNSET ? bg : BROWSER_IMAGE_BG);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 14U, 14U, LEONOS_UI_WHITE);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 14U, 1U, border);
        leonos_ui_rect(&ui, content_x + 2U, y + 14U, 14U, 1U, border);
        leonos_ui_rect(&ui, content_x + 2U, y + 1U, 1U, 14U, border);
        leonos_ui_rect(&ui, content_x + 15U, y + 1U, 1U, 14U, border);
    }
}

uint32_t line_align_shift_px(const struct browser_line *line,
                                    uint32_t doc_w)
{
    uint32_t indent_px;
    uint32_t cell_w;
    uint32_t content_w;
    uint32_t text_w;
    uint32_t image_w;
    if (!line || line->align == BROWSER_ALIGN_LEFT ||
        doc_w <= (uint32_t)line->indent * LEONOS_FONT_W) {
        return 0;
    }
    indent_px = (uint32_t)line->indent * LEONOS_FONT_W;
    cell_w = browser_line_cell_w(line->kind);
    image_w = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
    content_w = doc_w - indent_px;
    text_w = image_w + line->cells * cell_w;
    if (content_w <= text_w) {
        return 0;
    }
    if (line->align == BROWSER_ALIGN_RIGHT) {
        return content_w - text_w;
    }
    return (content_w - text_w) / 2U;
}

void draw_document_lines(void)
{
    uint32_t px = text_x();
    uint32_t py = text_y();
    uint32_t page_bottom = page_y() + page_h() - 8U;
    uint32_t doc_w = document_text_w();
    uint32_t text_bg = LEONOS_UI_WHITE;
    uint32_t y = py;
    for (uint32_t row = scroll_line; row < line_count && y < page_bottom; ++row) {
        struct browser_line *line = &lines[row];
        uint32_t line_px = px + (uint32_t)line->indent * LEONOS_FONT_W;
        uint32_t image_text_offset = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
        uint32_t cell_w = browser_line_cell_w(line->kind);
        uint32_t cell_h = browser_line_cell_h(line->kind);
        uint32_t line_h = browser_line_height(line->kind);
        uint32_t start = 0;
        if (line->len == 0 && line->kind != BROWSER_LINE_HR) {
            y += line_h;
            continue;
        }
        draw_document_line_frame(line, px, y, doc_w);
        if (line->kind == BROWSER_LINE_HR) {
            y += line_h;
            continue;
        }
        line_px += line_align_shift_px(line, doc_w);
        while (start < line->len) {
            uint8_t link = line->link[start];
            uint8_t style = line->style[start];
            uint32_t run_fg = line->fg[start];
            uint32_t run_bg = line->bg[start];
            uint32_t end = browser_line_next_byte(line, start);
            uint32_t start_cells = browser_line_cells_between(line, 0, start);
            uint32_t run_cells;
            uint32_t fg = line_is_heading(line->kind) ? BROWSER_IE_NAVY : BROWSER_TEXT_DARK;
            uint32_t bg = line->line_bg != BROWSER_COLOR_UNSET ? line->line_bg : text_bg;
            uint8_t underline = 0;
            uint8_t bold = line_is_heading(line->kind) ||
                           (style & BROWSER_TEXT_BOLD);
            uint8_t italic = (style & BROWSER_TEXT_ITALIC) != 0;
            uint8_t code = (style & BROWSER_TEXT_CODE) != 0;
            while (end < line->len && line->link[end] == link &&
                   line->style[end] == style &&
                   line->fg[end] == run_fg &&
                   line->bg[end] == run_bg) {
                end = browser_line_next_byte(line, end);
            }
            run_cells = browser_line_cells_between(line, start, end);
            if (run_fg != BROWSER_COLOR_UNSET) {
                fg = run_fg;
            }
            if (run_bg != BROWSER_COLOR_UNSET) {
                bg = run_bg;
            }
            if (link) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = BROWSER_LINK_BLUE;
                }
                underline = 1;
                bold = (style & BROWSER_TEXT_BOLD) != 0;
            } else if (line->kind == BROWSER_LINE_MUTED) {
                fg = LEONOS_UI_DARK;
            } else if (line->kind == BROWSER_LINE_BLOCKQUOTE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00484848U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_QUOTE_BG;
                }
            } else if (line->kind == BROWSER_LINE_TABLE) {
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_TABLE_BG;
                }
            } else if (line->kind == BROWSER_LINE_IMAGE) {
                if (run_fg == BROWSER_COLOR_UNSET) {
                    fg = 0x00304050U;
                }
                if (run_bg == BROWSER_COLOR_UNSET &&
                    line->line_bg == BROWSER_COLOR_UNSET) {
                    bg = BROWSER_IMAGE_BG;
                }
            }
            if (line->kind == BROWSER_LINE_HEADING1 ||
                (style & BROWSER_TEXT_UNDERLINE)) {
                underline = 1;
            }
            draw_line_run(line_px + image_text_offset + start_cells * cell_w, y,
                          line->text + start, end - start, fg, bg,
                          underline, bold, italic, code, cell_w, cell_h,
                          run_cells);
            start = end;
        }
        y += line_h;
    }
}

void draw_browser_menu(void)
{
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
        struct leonos_ui_context_menu_item items[] = {
            {T("LeonOS Home", "LeonOS 主页"), BROWSER_CMD_FAV_HOME, 0},
            {"example.com", BROWSER_CMD_FAV_EXAMPLE, 0},
        };
        leonos_ui_menubar_item_rect(0, 0, top_items,
                                    sizeof(top_items) / sizeof(top_items[0]),
                                    BROWSER_MENU_FAVORITES, &r);
        leonos_ui_menu_popup(&ui, (uint32_t)r.x, BROWSER_MENU_H, 204U,
                             items, sizeof(items) / sizeof(items[0]), 0);
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

void draw_browser(void)
{
    uint32_t p_y = page_y();
    uint32_t p_w = page_w();
    uint32_t p_h = page_h();
    uint32_t rows = visible_rows();
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
                         scroll_line, line_count ? line_count : 1U, rows,
                         line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    if (source_truncated) {
        char truncated[BROWSER_STATUS_CAP];
        copy_text(truncated, sizeof(truncated), status_text);
        pos = (uint32_t)strlen(truncated);
        append_text(truncated, &pos, sizeof(truncated), T("  Truncated", "  已截断"));
        leonos_ui_statusbar(&ui, view_h - BROWSER_STATUS_H, BROWSER_STATUS_H, truncated);
    } else {
        leonos_ui_statusbar(&ui, view_h - BROWSER_STATUS_H, BROWSER_STATUS_H, status_text);
    }
    draw_browser_menu();
    leonos_ui_toast_draw(&ui, &browser_toast, leonos_uptime_ms());
}

void present_browser(void)
{
    if (window_id <= 0) {
        return;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, BROWSER_MAX_W);
    draw_browser();
    leonos_gui_present_window((uint32_t)window_id, view_w, view_h,
                              BROWSER_MAX_W, pixels);
}
