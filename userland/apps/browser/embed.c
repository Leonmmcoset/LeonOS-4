#include "browser.h"

static uint32_t browser_embed_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void browser_embed_reset(void)
{
    if (browser_document) {
        browser_litehtml_destroy(browser_document);
        browser_document = 0;
    }
    address_input[0] = 0;
    status_text[0] = 0;
    page_title[0] = 0;
    current_location[0] = 0;
    page_source[0] = 0;
    page_is_html = 0;
    source_truncated = 0;
    browser_scroll_y = 0;
    scroll_x = 0;
    browser_form_count = 0;
    browser_form_control_count = 0;
    browser_pending_form = 0;
    browser_pending_form_url[0] = 0;
    browser_pending_form_method[0] = 0;
    browser_pending_form_body[0] = 0;
    history_count = 0;
    history_index = -1;
    menu_open = BROWSER_MENU_NONE;
    browser_should_exit = 0;
    browser_form_clear_focus();
    browser_toast = (struct leonos_ui_toast_state){0};
    for (uint32_t i = 0; i < BROWSER_HISTORY_MAX; ++i) {
        history[i][0] = 0;
    }
}

void browser_embed_resize(uint32_t width, uint32_t height)
{
    if (width < BROWSER_MIN_W) {
        width = BROWSER_MIN_W;
    }
    if (height < BROWSER_MIN_H) {
        height = BROWSER_MIN_H;
    }
    if (view_w != width || view_h != height) {
        view_w = width;
        view_h = height;
        if (page_source[0]) {
            rerender_page();
        }
    }
}

void browser_embed_init(uint32_t width, uint32_t height, const char *initial_url)
{
    uint32_t init_w;
    uint32_t init_h;
    browser_embedded = 1;
    window_id = 0;
    init_w = browser_embed_min_u32(width ? width : BROWSER_INITIAL_W,
                                   BROWSER_MAX_W);
    init_h = browser_embed_min_u32(height ? height : BROWSER_INITIAL_H,
                                   BROWSER_MAX_H);
    leonos_ui_set_font_path(BROWSER_FONT_PATH);
    leonos_ui_set_font_fallback_path(BROWSER_FONT_FALLBACK_PATH);
    view_w = init_w;
    view_h = init_h;
    /* The embedded browser draws into the host application's surface. */
    ui = (struct leonos_ui_surface){0};
    browser_embed_reset();
    browser_bookmarks_load();
    copy_text(address_input, sizeof(address_input), initial_url ? initial_url : "");
    leonos_ui_edit_state_init(&address_edit, address_input, sizeof(address_input));
    address_edit.focused = 0;
    if (initial_url && initial_url[0]) {
        navigate_to(initial_url, 1);
    } else {
        load_about();
        push_history(current_location);
    }
}

void browser_embed_draw(struct leonos_ui_surface *surface)
{
    if (!surface || !surface->pixels) {
        return;
    }
    ui = *surface;
    browser_embed_resize(surface->width, surface->height);
    draw_browser();
}

void browser_embed_handle_mouse_button(struct leonos_gui_app_event *event)
{
    handle_mouse_button(event);
}

void browser_embed_handle_mouse_wheel(struct leonos_gui_app_event *event)
{
    uint32_t before = browser_scroll_y;
    browser_scroll_wheel(-event->dy);
    if (browser_scroll_y != before) {
        present_browser();
    }
}

void browser_embed_handle_mouse_move(struct leonos_gui_app_event *event)
{
    handle_mouse_move(event);
}

void browser_embed_handle_key(struct leonos_gui_app_event *event)
{
    handle_key(event);
}

int browser_embed_should_exit(void)
{
    return browser_should_exit != 0;
}

void browser_embed_clear_exit(void)
{
    browser_should_exit = 0;
}

int browser_embed_input_active(void)
{
    return browser_form_input_active();
}
