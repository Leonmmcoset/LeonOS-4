#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DEMO_W 420
#define DEMO_H 320

static uint32_t pixels[DEMO_W * DEMO_H];

static void draw_demo(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, DEMO_W, DEMO_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 12, 12, "LeonOS UI Component Library", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 12, 30, "Real app-rendered window content", LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_text(ui, 12, 58, "Buttons", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_button(ui, 12, 78, 74, LEONOS_UI_BUTTON_H, "OK", 0);
    leonos_ui_button(ui, 96, 78, 88, LEONOS_UI_BUTTON_H, "Pressed", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_button(ui, 194, 78, 92, LEONOS_UI_BUTTON_H, "Disabled", LEONOS_UI_BUTTON_DISABLED);

    leonos_ui_text(ui, 12, 120, "Checks and Fields", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_checkbox(ui, 12, 140, "Checked", 1, 0);
    leonos_ui_checkbox(ui, 12, 164, "Unchecked", 0, 0);
    leonos_ui_text_field(ui, 146, 138, 188, "Sample text", 0);

    leonos_ui_text(ui, 12, 204, "Progress", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_progress(ui, 12, 224, 170, 18, 65, 100);
    leonos_ui_text(ui, 12, 248, "65 percent", LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_text(ui, 220, 120, "Menu", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_menu(ui, 220, 140, 170, 90);
    leonos_ui_menu_item(ui, 254, 150, 126, "Normal item", 0);
    leonos_ui_menu_item(ui, 254, 174, 126, "Selected item", LEONOS_UI_MENU_SELECTED);
    leonos_ui_menu_item(ui, 254, 198, 126, "", LEONOS_UI_MENU_SEPARATOR);

    leonos_ui_text(ui, 220, 238, "List", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_list_header(ui, 220, 258, 170, "Name        State");
    leonos_ui_list_row(ui, 220, 286, 170, "Button      ready", 0);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[uidemo.elf] UI component gallery starting");
    printf("[uidemo.elf] pid=%d creating UI Components window\n", getpid());
    window_id = leonos_gui_create_app_window("UI Components", "LeonOS UI component gallery", DEMO_W, DEMO_H);
    if (window_id <= 0) {
        printf("[uidemo.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, DEMO_W, DEMO_H, DEMO_W);
    draw_demo(&ui);
    leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == 1) {
                break;
            }
            if (event.type == 4) {
                draw_demo(&ui);
                leonos_gui_present_window((uint32_t)window_id, DEMO_W, DEMO_H, DEMO_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
    return 0;
}
