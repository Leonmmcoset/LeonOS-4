#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define OOBE_W 560
#define OOBE_H 360
#define OOBE_DONE_PATH "0:/etc/oobe.done"
#define OOBE_PAGE_COUNT 3
#define OOBE_KEY_ESCAPE 1U

static uint32_t pixels[OOBE_W * OOBE_H];
static uint32_t page_index;

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void draw_oobe(struct leonos_ui_surface *ui)
{
    const char *title = "Welcome to LeonOS";
    const char *line1 = "This first-run setup will prepare your system.";
    const char *line2 = "Click Next to continue.";
    const char *button = page_index + 1 >= OOBE_PAGE_COUNT ? "Finish" : "Next";
    leonos_ui_rect(ui, 0, 0, OOBE_W, OOBE_H, LEONOS_UI_GRAY);
    leonos_ui_rect(ui, 0, 0, OOBE_W, 84, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 48, "Out-of-box experience", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_panel(ui, 20, 104, OOBE_W - 40, 176, LEONOS_UI_LIGHT);

    if (page_index == 1) {
        title = "Test Step 1";
        line1 = "This is a placeholder page for future setup options.";
        line2 = "The OOBE flow already supports multiple pages.";
    } else if (page_index == 2) {
        title = "Test Step 2";
        line1 = "Finish will write the OOBE completion marker.";
        line2 = "After reboot, setup will not open again.";
    }
    leonos_ui_text(ui, 40, 126, title, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 40, 158, line1, LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 40, 182, line2, LEONOS_UI_DARK, LEONOS_UI_LIGHT);

    for (uint32_t i = 0; i < OOBE_PAGE_COUNT; ++i) {
        uint32_t x = 40 + i * 22;
        uint32_t color = i == page_index ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_DARK;
        leonos_ui_rect(ui, x, 246, 14, 14, color);
    }
    leonos_ui_button(ui, OOBE_W - 108, OOBE_H - 44, 88, LEONOS_UI_BUTTON_H,
                     button, 0);
}

static int write_completion_marker(void)
{
    static const char done[] = "OOBE已完成\n";
    int fd = open(OOBE_DONE_PATH, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, done, sizeof(done) - 1);
    close(fd);
    return wrote == (long)(sizeof(done) - 1) ? 0 : -1;
}

static int advance_or_finish(void)
{
    if (page_index + 1 < OOBE_PAGE_COUNT) {
        ++page_index;
        return 0;
    }
    return write_completion_marker() == 0 ? 1 : 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[oobe.elf] starting first-run setup");
    window_id = leonos_gui_create_app_window_ex("LeonOS Setup", "First-run setup",
                                                OOBE_W, OOBE_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[oobe.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, OOBE_W, OOBE_H, OOBE_W);
    for (;;) {
        draw_oobe(&ui);
        leonos_gui_present_window((uint32_t)window_id, OOBE_W, OOBE_H,
                                  OOBE_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                if (event.keycode == LEONOS_KEY_ENTER && advance_or_finish()) {
                    break;
                }
                if (event.keycode == OOBE_KEY_ESCAPE) {
                    break;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1u) &&
                hit_rect_i(event.x, event.y, OOBE_W - 108, OOBE_H - 44,
                           88, (int32_t)LEONOS_UI_BUTTON_H) &&
                advance_or_finish()) {
                break;
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
