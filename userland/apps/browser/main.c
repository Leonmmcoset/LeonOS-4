#include "browser.h"

#include <leonos/inputm.h>

static void browser_update_inputm_context(void)
{
    int form_active = browser_litehtml_form_input_active(browser_document);
    struct leonos_inputm_context context = {
        .window_id = (uint32_t)window_id,
        .flags = 0,
    };
    if (address_edit.focused || form_active) {
        context.flags |= LEONOS_INPUTM_CONTEXT_FOCUSED;
    }
    if (browser_litehtml_form_input_secure(browser_document)) {
        context.flags |= LEONOS_INPUTM_CONTEXT_SECURE;
    }
    (void)leonos_inputm_set_context(&context);
}

static void browser_shutdown(void)
{
    if (browser_document) {
        browser_litehtml_destroy(browser_document);
        browser_document = 0;
    }
    browser_release_surface();
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_gui_app_event event;
    const char *initial = "about:leonos";
    (void)envp;
    puts("[browser.elf] browser starting");
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        initial = argv[1];
    }
    if (browser_resize_surface(view_w, view_h) < 0) {
        puts("[browser.elf] render surface allocation failed");
        return 1;
    }
    window_id = leonos_gui_create_app_window_ex(T("LeonOS Browser", "LeonOS 浏览器"),
                                                T("Classic Web Browser", "经典网页浏览器"),
                                                view_w, view_h, 0);
    if (window_id <= 0) {
        printf("[browser.elf] create window failed=%d\n", window_id);
        browser_shutdown();
        return 1;
    }
    leonos_ui_set_font_path(BROWSER_FONT_PATH);
    leonos_ui_set_font_fallback_path(BROWSER_FONT_FALLBACK_PATH);
    leonos_ui_bind(&ui, pixels, view_w, view_h, pixel_stride);
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
        browser_update_inputm_context();
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event,
                                      browser_toast.active ? 20U : LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                browser_shutdown();
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                handle_mouse_button(&event);
                if (browser_should_exit) {
                    browser_shutdown();
                    return 0;
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_MOVE) {
                handle_mouse_move(&event);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                uint32_t before = browser_scroll_y;
                /* InputM reports wheel-up as positive; document coordinates
                 * grow downward, so invert it at the browser boundary. */
                browser_scroll_wheel(-event.dy);
                if (browser_scroll_y != before) {
                    present_browser();
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1) {
                    browser_shutdown();
                    return 0;
                }
                handle_key(&event);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                uint32_t next_width = view_w;
                uint32_t next_height = view_h;
                if (event.width) {
                    next_width = event.width > BROWSER_MAX_W ? BROWSER_MAX_W : event.width;
                    if (next_width < BROWSER_MIN_W) {
                        next_width = BROWSER_MIN_W;
                    }
                }
                if (event.height) {
                    next_height = event.height > BROWSER_MAX_H ? BROWSER_MAX_H : event.height;
                    if (next_height < BROWSER_MIN_H) {
                        next_height = BROWSER_MIN_H;
                    }
                }
                if (browser_resize_surface(next_width, next_height) < 0) {
                    set_status(T("Unable to resize the browser surface", "无法调整浏览器绘制缓冲区"));
                    present_browser();
                    continue;
                }
                view_w = next_width;
                view_h = next_height;
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
