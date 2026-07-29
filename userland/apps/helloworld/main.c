#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/ui.h>

#define T(en, zh) leonos_i18n((en), (zh))

#define WIN_W 320U
#define WIN_H 200U

static uint32_t pixels[WIN_W * WIN_H];

int main(void)
{
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    int window_id;
    int done = 0;

    window_id = leonos_gui_create_app_window_ex(
        T("Hello World", "你好世界"),
        T("helloworld", "helloworld"),
        WIN_W, WIN_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return 1;
    }
    leonos_ui_bind(&ui, pixels, WIN_W, WIN_H, WIN_W);

    while (!done) {
        leonos_ui_rect(&ui, 0, 0, WIN_W, WIN_H, LEONOS_UI_WHITE);
        leonos_ui_text(&ui, 80, 60,
                       T("Hello, World!", "你好，世界！"),
                       LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text(&ui, 50, 100,
                       T("Installed via API package",
                         "通过 API 包安装"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
        leonos_ui_button(&ui, WIN_W / 2U - 36U, WIN_H - 52U, 72U,
                         LEONOS_UI_BUTTON_H,
                         T("OK", "确定"), 0);
        leonos_gui_present_window((uint32_t)window_id, WIN_W, WIN_H,
                                  WIN_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                done = 1;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1U)) {
                if (event.x >= (int32_t)(WIN_W / 2U - 36U) &&
                    event.x < (int32_t)(WIN_W / 2U + 36U) &&
                    event.y >= (int32_t)(WIN_H - 52U) &&
                    event.y < (int32_t)(WIN_H - 52U + LEONOS_UI_BUTTON_H)) {
                    done = 1;
                }
            }
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
