#include "browser.h"

int main(int argc, char **argv, char **envp)
{
    struct leonos_gui_app_event event;
    const char *initial = "about:leonos";
    (void)envp;
    puts("[browser.elf] browser starting");
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        initial = argv[1];
    }
    window_id = leonos_gui_create_app_window_ex(T("LeonOS Browser", "LeonOS 浏览器"),
                                                T("Classic Web Browser", "经典网页浏览器"),
                                                view_w, view_h, 0);
    if (window_id <= 0) {
        printf("[browser.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_set_font_path(BROWSER_FONT_PATH);
    leonos_ui_set_font_fallback_path(BROWSER_FONT_FALLBACK_PATH);
    leonos_ui_bind(&ui, pixels, view_w, view_h, BROWSER_MAX_W);
    leonos_ui_edit_state_init(&address_edit, address_input, sizeof(address_input));
    address_edit.focused = 1;
    browser_bookmarks_load();
    load_about();
    if (!text_eq(initial, "about:leonos")) {
        navigate_to(initial, 1);
    } else {
        push_history(current_location);
    }
    present_browser();
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event,
                                      browser_toast.active ? 20U : LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                handle_mouse_button(&event);
                if (browser_should_exit) {
                    return 0;
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_vscrollbar_handle_wheel(&scroll_line,
                                                      line_count ? line_count : 1U,
                                                      visible_rows(), event.dy)) {
                    present_browser();
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    return 0;
                }
                handle_key(&event);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width) {
                    view_w = event.width > BROWSER_MAX_W ? BROWSER_MAX_W : event.width;
                    if (view_w < BROWSER_MIN_W) {
                        view_w = BROWSER_MIN_W;
                    }
                }
                if (event.height) {
                    view_h = event.height > BROWSER_MAX_H ? BROWSER_MAX_H : event.height;
                    if (view_h < BROWSER_MIN_H) {
                        view_h = BROWSER_MIN_H;
                    }
                }
                rerender_page();
                present_browser();
                continue;
            }
        } else if (browser_toast.active) {
            present_browser();
            sleep_ms(10);
        } else {
            sleep_ms(10);
        }
    }
}
