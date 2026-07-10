#include "browser.h"

static uint32_t browser_embed_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void browser_embed_reset(void)
{
    address_input[0] = 0;
    status_text[0] = 0;
    page_title[0] = 0;
    current_location[0] = 0;
    page_source[0] = 0;
    page_is_html = 0;
    source_truncated = 0;
    line_count = 1;
    scroll_line = 0;
    scroll_x = 0;
    link_count = 0;
    browser_form_count = 0;
    browser_form_control_count = 0;
    browser_form_option_count = 0;
    history_count = 0;
    history_index = -1;
    menu_open = BROWSER_MENU_NONE;
    browser_should_exit = 0;
    browser_form_clear_focus();
    browser_toast = (struct leonos_ui_toast_state){0};
    for (uint32_t i = 0; i < BROWSER_MAX_LINES; ++i) {
        lines[i] = (struct browser_line){0};
    }
    for (uint32_t i = 0; i < BROWSER_MAX_LINKS; ++i) {
        links[i] = (struct browser_link){0};
    }
    for (uint32_t i = 0; i < BROWSER_MAX_FORMS; ++i) {
        browser_forms[i] = (struct browser_form){0};
    }
    for (uint32_t i = 0; i < BROWSER_MAX_FORM_CONTROLS; ++i) {
        browser_form_controls[i] = (struct browser_form_control){0};
    }
    for (uint32_t i = 0; i < BROWSER_MAX_FORM_OPTIONS; ++i) {
        browser_form_options[i] = (struct browser_form_option){0};
    }
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
    leonos_ui_bind(&ui, pixels, init_w, init_h, BROWSER_MAX_W);
    view_w = init_w;
    view_h = init_h;
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
    if (leonos_ui_vscrollbar_handle_wheel(&scroll_line,
                                          line_count ? line_count : 1U,
                                          visible_rows(), event->dy)) {
        present_browser();
    }
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
