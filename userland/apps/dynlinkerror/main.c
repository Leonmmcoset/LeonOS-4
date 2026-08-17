#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DYNLINK_ERROR_W 560u
#define DYNLINK_ERROR_H 238u
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DYNLINK_ERROR_W * DYNLINK_ERROR_H];

static int point_in_rect(int32_t px, int32_t py, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static void draw_error(struct leonos_ui_surface *ui, const char *program,
                       const char *library)
{
    const char *program_path = program && program[0] ? program : T("(unknown program)", "(未知程序)");
    const char *library_path = library && library[0] ? library : T("(unknown library)", "(未知库)");

    leonos_ui_rect(ui, 0, 0, DYNLINK_ERROR_W, DYNLINK_ERROR_H, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, 22, T("The application could not be started.", "无法启动该应用程序。"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, 50, T("Application:", "应用程序："), LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_edit(ui, 126, 46, DYNLINK_ERROR_W - 150, program_path, 0, 0,
                   LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(ui, 24, 88, T("Required shared library is missing:", "缺少必需的动态链接库："),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_edit(ui, 24, 112, DYNLINK_ERROR_W - 48, library_path, 0, 0,
                   LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(ui, 24, 150,
                   T("Restore the library from the system image, then try again.",
                     "请从系统镜像恢复该库，然后重试。"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_button(ui, DYNLINK_ERROR_W - 104, DYNLINK_ERROR_H - 42, 80,
                     LEONOS_UI_BUTTON_H, T("Close", "关闭"), 0);
}

int main(int argc, char **argv)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    const char *program = argc > 1 ? argv[1] : "";
    const char *library = argc > 2 ? argv[2] : "";
    int window_id;

    printf("[dynlinkerror.elf] unable to start %s: missing %s\n",
           program && program[0] ? program : "(unknown program)",
           library && library[0] ? library : "(unknown library)");
    window_id = leonos_gui_create_app_window_ex(
        T("Dynamic Link Error", "动态链接错误"),
        T("Required shared library is missing", "缺少必需的动态链接库"),
        DYNLINK_ERROR_W, DYNLINK_ERROR_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[dynlinkerror.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, DYNLINK_ERROR_W, DYNLINK_ERROR_H, DYNLINK_ERROR_W);
    draw_error(&ui, program, library);
    leonos_gui_present_window((uint32_t)window_id, DYNLINK_ERROR_W,
                              DYNLINK_ERROR_H, DYNLINK_ERROR_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) <= 0) {
            sleep_ms(10);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
            (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed &&
             (event.keycode == LEONOS_KEY_ESCAPE || event.keycode == LEONOS_KEY_ENTER)) ||
            (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u) &&
             point_in_rect(event.x, event.y, DYNLINK_ERROR_W - 104,
                           DYNLINK_ERROR_H - 42, 80, LEONOS_UI_BUTTON_H))) {
            break;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
            event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
            event.type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
            draw_error(&ui, program, library);
            leonos_gui_present_window((uint32_t)window_id, DYNLINK_ERROR_W,
                                      DYNLINK_ERROR_H, DYNLINK_ERROR_W, pixels);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
