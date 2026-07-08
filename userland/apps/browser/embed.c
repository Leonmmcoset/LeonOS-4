#include "browser.h"

#define BROWSER_EMBED_EDIT_W 420U
#define BROWSER_EMBED_EDIT_H 172U
#define BROWSER_EMBED_EDIT_BUTTON_W 72U
#define BROWSER_EMBED_EDIT_FIELD_Y 72U

static uint32_t browser_embed_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t browser_embed_edit_w(void)
{
    if (view_w <= 40U) {
        return view_w;
    }
    return browser_embed_min_u32(BROWSER_EMBED_EDIT_W, view_w - 32U);
}

static uint32_t browser_embed_edit_h(void)
{
    if (view_h <= 40U) {
        return view_h;
    }
    return browser_embed_min_u32(BROWSER_EMBED_EDIT_H, view_h - 32U);
}

static uint32_t browser_embed_edit_x(void)
{
    uint32_t w = browser_embed_edit_w();
    return view_w > w ? (view_w - w) / 2U : 0;
}

static uint32_t browser_embed_edit_y(void)
{
    uint32_t h = browser_embed_edit_h();
    return view_h > h ? (view_h - h) / 2U : 0;
}

static uint32_t browser_embed_edit_field_w(void)
{
    uint32_t w = browser_embed_edit_w();
    return w > 32U ? w - 32U : w;
}

static void browser_embed_finish_form_edit(uint8_t accept)
{
    struct browser_form_control *control;
    if (!browser_embed_edit_active) {
        return;
    }
    if (accept && browser_embed_edit_control < browser_form_control_count) {
        control = &browser_form_controls[browser_embed_edit_control];
        if (control->kind == BROWSER_FORM_CONTROL_TEXT ||
            control->kind == BROWSER_FORM_CONTROL_PASSWORD) {
            copy_text(control->value, sizeof(control->value),
                      browser_embed_edit_value);
            rerender_page();
        }
    }
    browser_embed_edit_active = 0;
    browser_embed_edit_state.focused = 0;
}

static void browser_embed_draw_form_edit(void)
{
    struct browser_form_control *control;
    uint32_t w;
    uint32_t h;
    uint32_t x;
    uint32_t y;
    uint32_t button_y;
    if (!browser_embed_edit_active ||
        browser_embed_edit_control >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[browser_embed_edit_control];
    w = browser_embed_edit_w();
    h = browser_embed_edit_h();
    x = browser_embed_edit_x();
    y = browser_embed_edit_y();
    button_y = h > 38U ? y + h - 38U : y;
    leonos_ui_dialog(&ui, x, y, w, h, T("Form input", "表单输入"));
    leonos_ui_text_clipped(&ui, x + 16U, y + 46U,
                           w > 32U ? w - 32U : w,
                           control->label[0] ? control->label : control->name,
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit_state_draw(&ui, x + 16U, y + BROWSER_EMBED_EDIT_FIELD_Y,
                              browser_embed_edit_field_w(),
                              &browser_embed_edit_state, 0);
    if (w > 184U && h > 48U) {
        leonos_ui_button(&ui, x + w - 168U, button_y,
                         BROWSER_EMBED_EDIT_BUTTON_W, LEONOS_UI_BUTTON_H,
                         T("OK", "确定"), 0);
        leonos_ui_button(&ui, x + w - 88U, button_y,
                         BROWSER_EMBED_EDIT_BUTTON_W, LEONOS_UI_BUTTON_H,
                         T("Cancel", "取消"), 0);
    }
}

static int browser_embed_form_edit_handle_mouse(struct leonos_gui_app_event *event)
{
    uint32_t buttons;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t button_y;
    if (!browser_embed_edit_active || !event) {
        return 0;
    }
    buttons = event->buttons;
    if (event->pressed) {
        buttons |= 1U;
    }
    x = browser_embed_edit_x();
    y = browser_embed_edit_y();
    w = browser_embed_edit_w();
    h = browser_embed_edit_h();
    button_y = h > 38U ? y + h - 38U : y;
    if (!buttons) {
        leonos_ui_edit_state_handle_mouse(&browser_embed_edit_state,
                                          event->x, event->y,
                                          x + 16U,
                                          y + BROWSER_EMBED_EDIT_FIELD_Y,
                                          browser_embed_edit_field_w(),
                                          buttons);
        return 1;
    }
    if ((buttons & 1U) &&
        w > 184U && h > 48U &&
        hit_rect_i(event->x, event->y, x + w - 168U, button_y,
                   BROWSER_EMBED_EDIT_BUTTON_W, LEONOS_UI_BUTTON_H)) {
        browser_embed_finish_form_edit(1);
        return 1;
    }
    if ((buttons & 1U) &&
        w > 184U && h > 48U &&
        hit_rect_i(event->x, event->y, x + w - 88U, button_y,
                   BROWSER_EMBED_EDIT_BUTTON_W, LEONOS_UI_BUTTON_H)) {
        browser_embed_finish_form_edit(0);
        return 1;
    }
    leonos_ui_edit_state_handle_mouse(&browser_embed_edit_state,
                                      event->x, event->y,
                                      x + 16U,
                                      y + BROWSER_EMBED_EDIT_FIELD_Y,
                                      browser_embed_edit_field_w(),
                                      buttons);
    return 1;
}

static int browser_embed_form_edit_handle_key(struct leonos_gui_app_event *event)
{
    if (!browser_embed_edit_active || !event) {
        return 0;
    }
    if (event->pressed && event->keycode == LEONOS_KEY_ENTER) {
        browser_embed_finish_form_edit(1);
        return 1;
    }
    if (event->pressed && event->keycode == 1U) {
        browser_embed_finish_form_edit(0);
        return 1;
    }
    leonos_ui_edit_state_handle_key(&browser_embed_edit_state,
                                    event->keycode, event->pressed);
    return 1;
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
    link_count = 0;
    browser_form_count = 0;
    browser_form_control_count = 0;
    history_count = 0;
    history_index = -1;
    menu_open = BROWSER_MENU_NONE;
    browser_should_exit = 0;
    browser_embed_edit_active = 0;
    browser_embed_edit_control = 0;
    browser_embed_edit_value[0] = 0;
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
    browser_embed_draw_form_edit();
}

void browser_embed_handle_mouse_button(struct leonos_gui_app_event *event)
{
    if (browser_embed_form_edit_handle_mouse(event)) {
        return;
    }
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
    if (browser_embed_form_edit_handle_key(event)) {
        return;
    }
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
    return browser_embed_edit_active != 0;
}

void browser_embed_start_form_edit(uint32_t control_index)
{
    struct browser_form_control *control;
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind != BROWSER_FORM_CONTROL_TEXT &&
        control->kind != BROWSER_FORM_CONTROL_PASSWORD) {
        return;
    }
    browser_embed_edit_control = control_index;
    copy_text(browser_embed_edit_value, sizeof(browser_embed_edit_value),
              control->value);
    leonos_ui_edit_state_init(&browser_embed_edit_state,
                              browser_embed_edit_value,
                              sizeof(browser_embed_edit_value));
    browser_embed_edit_state.focused = 1;
    browser_embed_edit_active = 1;
    menu_open = BROWSER_MENU_NONE;
    set_status(T("Editing form input", "正在编辑表单输入"));
}
