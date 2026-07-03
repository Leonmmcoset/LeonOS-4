#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define HELLO_W 300
#define HELLO_H 150
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[HELLO_W * HELLO_H];

static void draw_hello(struct leonos_ui_surface *ui, uint32_t hover)
{
    leonos_ui_rect(ui, 0, 0, HELLO_W, HELLO_H, LEONOS_UI_WHITE);
    leonos_ui_panel(ui, 10, 10, HELLO_W - 20, HELLO_H - 20, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 22, 26, T("Hello from hello.elf", "来自 hello.elf 的问候"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 22, 50, T("This window is rendered by the app", "此窗口由应用程序渲染"), LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_button(ui, 22, 88, 88, LEONOS_UI_BUTTON_H, T("Hello", "你好"),
                     hover ? LEONOS_UI_BUTTON_PRESSED : 0);
    leonos_ui_text(ui, 124, 92, hover ? T("Button pressed", "按钮已按下") : T("Window server v2", "窗口服务 v2"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    uint32_t hover = 0;
    puts("[hello.elf] hello from LeonOS 4 Ring-3 ELF");
    printf("[hello.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window_ex(T("Hello", "你好"), T("Hello from hello.elf process", "来自 hello.elf 进程的问候"),
                                                HELLO_W, HELLO_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[hello.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, HELLO_W, HELLO_H, HELLO_W);
    draw_hello(&ui, hover);
    leonos_gui_present_window((uint32_t)window_id, HELLO_W, HELLO_H, HELLO_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == 1) {
                break;
            }
            if (event.type == 6) {
                hover = event.x >= 22 && event.x < 110 && event.y >= 88 && event.y < 112;
                draw_hello(&ui, hover);
                leonos_gui_present_window((uint32_t)window_id, HELLO_W, HELLO_H, HELLO_W, pixels);
            }
            if (event.type == 4) {
                draw_hello(&ui, hover);
                leonos_gui_present_window((uint32_t)window_id, HELLO_W, HELLO_H, HELLO_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
    return 0;
}
