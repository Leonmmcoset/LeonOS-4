#include "browser.h"

#include <stdlib.h>

uint32_t *pixels;
uint32_t pixel_stride;
static uint32_t pixel_height;
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
uint32_t browser_scroll_y;
uint32_t scroll_x;
uint32_t browser_form_count;
uint32_t browser_form_control_count;
char history[BROWSER_HISTORY_MAX][BROWSER_URL_CAP];
uint32_t history_count;
int32_t history_index = -1;
uint8_t menu_open;
uint8_t browser_should_exit;
uint8_t browser_embedded;
struct leonos_ui_toast_state browser_toast;
struct browser_bookmark browser_bookmarks[BROWSER_MAX_BOOKMARKS];
uint32_t browser_bookmark_count;
char browser_find_query[BROWSER_FIND_CAP];
int32_t browser_find_row = -1;
uint32_t browser_find_start;
uint32_t browser_find_len;
uint8_t browser_devtools_open;
struct browser_litehtml_document *browser_document;
uint32_t browser_document_width;
uint32_t browser_document_height;
uint8_t browser_pending_form;
char browser_pending_form_url[BROWSER_URL_CAP];
char browser_pending_form_method[12];
char browser_pending_form_body[BROWSER_FORM_BODY_CAP];

int browser_resize_surface(uint32_t width, uint32_t height)
{
    uint64_t count;
    uint32_t *new_pixels;
    if (!width || !height || width > BROWSER_MAX_W || height > BROWSER_MAX_H) {
        return -1;
    }
    if (pixels && pixel_stride == width && pixel_height == height) {
        return 0;
    }
    count = (uint64_t)width * height;
    if (count > (uint64_t)SIZE_MAX / sizeof(*new_pixels)) {
        return -1;
    }
    new_pixels = (uint32_t *)malloc((size_t)count * sizeof(*new_pixels));
    if (!new_pixels) {
        return -1;
    }
    free(pixels);
    pixels = new_pixels;
    pixel_stride = width;
    pixel_height = height;
    return 0;
}

void browser_release_surface(void)
{
    free(pixels);
    pixels = 0;
    pixel_stride = 0;
    pixel_height = 0;
}

uint32_t browser_devtools_height(void)
{
    uint32_t height;
    if (browser_embedded || !browser_devtools_open) {
        return 0;
    }
    height = view_h / 3U;
    if (height < BROWSER_DEVTOOLS_MIN_H) {
        height = BROWSER_DEVTOOLS_MIN_H;
    }
    if (height > BROWSER_DEVTOOLS_MAX_H) {
        height = BROWSER_DEVTOOLS_MAX_H;
    }
    return height;
}

uint32_t page_y(void)
{
    if (browser_embedded) {
        return BROWSER_TOOLBAR_H + 4U;
    }
    return BROWSER_MENU_H + BROWSER_TOOLBAR_H + BROWSER_ADDR_H + 4U;
}

uint32_t page_w(void)
{
    return view_w > BROWSER_PAGE_X * 2U ? view_w - BROWSER_PAGE_X * 2U : 80U;
}

uint32_t page_h(void)
{
    uint32_t y = page_y();
    uint32_t content_w;
    uint32_t devtools_h = browser_devtools_height();
    uint32_t bottom_reserve = devtools_h ? devtools_h + 4U : 0U;
    if (view_h <= y + bottom_reserve + 4U) {
        return LEONOS_FONT_H + 2U;
    }
    content_w = document_content_w();
    if (content_w > document_text_w() &&
        view_h > y + bottom_reserve + BROWSER_SCROLL_W + 8U) {
        return view_h - y - bottom_reserve - BROWSER_SCROLL_W - 4U;
    }
    return view_h - y - bottom_reserve - 4U;
}

uint32_t text_x(void)
{
    return BROWSER_PAGE_X + 8U;
}

uint32_t text_y(void)
{
    return page_y() + 8U;
}

void clamp_scroll(void)
{
    uint32_t viewport = document_view_h();
    uint32_t content_h = document_content_h();
    uint32_t content_w = document_content_w();
    uint32_t max_y = content_h > viewport ? content_h - viewport : 0U;
    if (browser_scroll_y > max_y) {
        browser_scroll_y = max_y;
    }
    if (scroll_x + document_text_w() > content_w) {
        scroll_x = content_w > document_text_w() ? content_w - document_text_w() : 0;
    }
}

void browser_scroll_wheel(int32_t delta)
{
    int64_t next;
    if (!browser_document || delta == 0) {
        return;
    }
    next = (int64_t)browser_scroll_y + (int64_t)delta * 48;
    if (next < 0) {
        next = 0;
    }
    browser_scroll_y = (uint32_t)next;
    clamp_scroll();
}

void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
    if (text && text[0]) {
        leonos_ui_toast_show(&browser_toast, text, leonos_uptime_ms(),
                             2200, LEONOS_UI_TOAST_INFO);
    }
}
