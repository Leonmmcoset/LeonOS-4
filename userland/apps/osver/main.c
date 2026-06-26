#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define OSVER_W 520
#define OSVER_H 260

static uint32_t pixels[OSVER_W * OSVER_H];
static struct leonos_system_info info;
static char status_text[96] = "System version information";

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static void draw_label_value(struct leonos_ui_surface *ui, uint32_t y,
                             const char *label, const char *value)
{
    leonos_ui_text(ui, 30, y, label, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_edit(ui, 150, y - 4, OSVER_W - 180,
                   value ? value : "",
                   text_len(value),
                   0, LEONOS_UI_EDIT_READONLY);
}

static void draw_osver(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, OSVER_W, OSVER_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, OSVER_W, OSVER_H, "About LeonOS");
    leonos_ui_text(ui, 28, 42, "LeonOS 4", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 28, 62, "Kernel and middle layer build details", LEONOS_UI_DARK, LEONOS_UI_GRAY);

    draw_label_value(ui, 94, "Kernel name:", info.kernel_name);
    draw_label_value(ui, 122, "Kernel version:", info.kernel_version);
    draw_label_value(ui, 150, "Middle layer:", info.middlelayer_name);
    draw_label_value(ui, 178, "Build time:", info.build_time);
    draw_label_value(ui, 206, "Copyright:", info.copyright);
    leonos_ui_statusbar(ui, OSVER_H - 28, 28, status_text);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    int ret;

    puts("[osver.elf] system version viewer starting");
    ret = leonos_system_info(&info);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text), "Could not read system version information");
        copy_text(info.kernel_name, sizeof(info.kernel_name), "unknown");
        copy_text(info.kernel_version, sizeof(info.kernel_version), "0.0.0-0000");
        copy_text(info.middlelayer_name, sizeof(info.middlelayer_name), "unknown");
        copy_text(info.build_time, sizeof(info.build_time), "unknown");
        copy_text(info.copyright, sizeof(info.copyright),
                  "Copyright LeonMMcoset 2021-2026. All rights reserved.");
    }

    window_id = leonos_gui_create_app_window_ex("About LeonOS", "System version",
                                                OSVER_W, OSVER_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[osver.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, OSVER_W, OSVER_H, OSVER_W);
    draw_osver(&ui);
    leonos_gui_present_window((uint32_t)window_id, OSVER_W, OSVER_H, OSVER_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed && event.keycode == 1) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_osver(&ui);
                leonos_gui_present_window((uint32_t)window_id, OSVER_W, OSVER_H, OSVER_W, pixels);
            }
        }
        sleep_ms(20);
    }
}
