#include "browser.h"

uint32_t pixels[BROWSER_MAX_W * BROWSER_MAX_H];
struct leonos_ui_surface ui;
int window_id;
uint32_t view_w = BROWSER_INITIAL_W;
uint32_t view_h = BROWSER_INITIAL_H;
char address_input[BROWSER_URL_CAP] = "about:leonos";
struct leonos_ui_edit_state address_edit;
char status_text[BROWSER_STATUS_CAP] = "Ready";
char page_title[BROWSER_TITLE_CAP] = "LeonOS Browser";
char current_location[BROWSER_URL_CAP] = "about:leonos";
char page_source[BROWSER_SOURCE_CAP];
uint8_t page_is_html;
uint8_t source_truncated;
struct browser_line lines[BROWSER_MAX_LINES];
uint32_t line_count = 1;
uint32_t scroll_line;
struct browser_link links[BROWSER_MAX_LINKS];
uint32_t link_count;
char history[BROWSER_HISTORY_MAX][BROWSER_URL_CAP];
uint32_t history_count;
int32_t history_index = -1;
uint8_t menu_open;
uint8_t browser_should_exit;
struct leonos_net_http_get http_result;

uint32_t page_y(void)
{
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + BROWSER_ADDR_H + 4U;
}

uint32_t page_w(void)
{
    return view_w > BROWSER_PAGE_X * 2U ? view_w - BROWSER_PAGE_X * 2U : 80U;
}

uint32_t page_h(void)
{
    uint32_t y = page_y();
    if (view_h <= y + BROWSER_STATUS_H + 4U) {
        return BROWSER_LINE_H;
    }
    return view_h - y - BROWSER_STATUS_H - 4U;
}

uint32_t text_x(void)
{
    return BROWSER_PAGE_X + 8U;
}

uint32_t text_y(void)
{
    return page_y() + 8U;
}

uint32_t text_cols(void)
{
    uint32_t w = page_w();
    uint32_t cols;
    if (w <= BROWSER_SCROLL_W + 24U) {
        return 16U;
    }
    cols = (w - BROWSER_SCROLL_W - 24U) / LEONOS_FONT_W;
    if (cols < 16U) {
        cols = 16U;
    }
    if (cols >= BROWSER_LINE_CHARS) {
        cols = BROWSER_LINE_CHARS - 1U;
    }
    return cols;
}

uint32_t visible_rows(void)
{
    uint32_t h = page_h();
    uint32_t used = 0;
    uint32_t rows = 0;
    if (h <= 16U) {
        return 1U;
    }
    h -= 16U;
    for (uint32_t i = scroll_line; i < line_count; ++i) {
        uint32_t line_h = browser_line_height(lines[i].kind);
        if (rows && used + line_h > h) {
            break;
        }
        used += line_h;
        ++rows;
        if (used >= h) {
            break;
        }
    }
    return rows ? rows : 1U;
}

void clamp_scroll(void)
{
    uint32_t rows = visible_rows();
    if (line_count <= rows) {
        scroll_line = 0;
    } else if (scroll_line + rows > line_count) {
        scroll_line = line_count - rows;
    }
}

void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
}
