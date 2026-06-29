#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define OOBE_MAX_W 1920
#define OOBE_MAX_H 1080
#define OOBE_INITIAL_W 800
#define OOBE_INITIAL_H 600
#define OOBE_DONE_PATH "0:/etc/oobe.done"
#define OOBE_PAGE_COUNT 3

static uint32_t pixels[OOBE_MAX_W * OOBE_MAX_H];
static uint32_t surface_w = OOBE_INITIAL_W;
static uint32_t surface_h = OOBE_INITIAL_H;
static uint32_t page_index;

struct oobe_layout {
    uint32_t header_h;
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_w;
    uint32_t panel_h;
    uint32_t button_x;
    uint32_t button_y;
    uint32_t button_w;
    uint32_t button_h;
};

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void update_surface_size(uint32_t width, uint32_t height)
{
    uint32_t scale = 1;
    while ((width / scale) > OOBE_MAX_W || (height / scale) > OOBE_MAX_H) {
        ++scale;
    }
    if (scale > 1) {
        width /= scale;
        height /= scale;
    }
    if (width == 0 || width > OOBE_MAX_W) {
        width = OOBE_MAX_W;
    }
    if (height == 0 || height > OOBE_MAX_H) {
        height = OOBE_MAX_H;
    }
    surface_w = width;
    surface_h = height;
}

static void update_surface_size_from_framebuffer(void)
{
    struct leonos_fb_info fb;
    if (leonos_fb_info(&fb) >= 0) {
        update_surface_size(fb.width, fb.height);
    }
}

static struct oobe_layout get_oobe_layout(void)
{
    struct oobe_layout layout;
    uint32_t panel_right;
    layout.header_h = surface_h > 420 ? 118 : 84;
    layout.panel_w = surface_w > 720 ? 640 : surface_w > 64 ? surface_w - 40 : surface_w;
    layout.panel_h = surface_h > 420 ? 220 : 176;
    layout.panel_x = surface_w > layout.panel_w ? (surface_w - layout.panel_w) / 2 : 0;
    layout.panel_y = layout.header_h + 42;
    layout.button_h = LEONOS_UI_BUTTON_H;
    layout.button_w = surface_w > 112 ? 96 : surface_w > 16 ? surface_w - 16 : surface_w;
    panel_right = layout.panel_x + layout.panel_w;
    if (panel_right > layout.button_w + 20) {
        layout.button_x = panel_right - layout.button_w - 20;
    } else {
        layout.button_x = surface_w > layout.button_w ? (surface_w - layout.button_w) / 2 : 0;
    }
    if (layout.button_x + layout.button_w > surface_w) {
        layout.button_x = surface_w > layout.button_w ? surface_w - layout.button_w : 0;
    }
    layout.button_y = surface_h > layout.button_h + 18 ? surface_h - layout.button_h - 18 : 0;
    return layout;
}

static void draw_oobe(struct leonos_ui_surface *ui)
{
    const char *title = "Welcome to LeonOS";
    const char *line1 = "This first-run setup will prepare your system.";
    const char *line2 = "Click Next to continue.";
    const char *button = page_index + 1 >= OOBE_PAGE_COUNT ? "Finish" : "Next";
    struct oobe_layout layout = get_oobe_layout();

    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_GRAY);
    leonos_ui_rect(ui, 0, 0, surface_w, layout.header_h, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 48, "Out-of-box experience", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    if (surface_h > 520) {
        leonos_ui_text(ui, 24, 74, "First-run setup", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    }
    leonos_ui_panel(ui, layout.panel_x, layout.panel_y, layout.panel_w, layout.panel_h, LEONOS_UI_LIGHT);

    if (page_index == 1) {
        title = "Test Step 1";
        line1 = "This is a placeholder page for future setup options.";
        line2 = "The OOBE flow already supports multiple pages.";
    } else if (page_index == 2) {
        title = "Test Step 2";
        line1 = "Finish will write the OOBE completion marker.";
        line2 = "After reboot, setup will not open again.";
    }
    leonos_ui_text(ui, layout.panel_x + 20, layout.panel_y + 22, title, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, layout.panel_x + 20, layout.panel_y + 58, line1, LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, layout.panel_x + 20, layout.panel_y + 82, line2, LEONOS_UI_DARK, LEONOS_UI_LIGHT);

    for (uint32_t i = 0; i < OOBE_PAGE_COUNT; ++i) {
        uint32_t x = layout.panel_x + 20 + i * 22;
        uint32_t color = i == page_index ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_DARK;
        leonos_ui_rect(ui, x, layout.panel_y + layout.panel_h - 34, 14, 14, color);
    }
    leonos_ui_button(ui, layout.button_x, layout.button_y, layout.button_w, layout.button_h, button, 0);
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
    update_surface_size_from_framebuffer();
    window_id = leonos_gui_create_app_window_ex("LeonOS Setup", "First-run setup",
                                                surface_w, surface_h,
                                                LEONOS_GUI_WINDOW_FULLSCREEN);
    if (window_id <= 0) {
        printf("[oobe.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, surface_w, surface_h, OOBE_MAX_W);
    for (;;) {
        leonos_ui_bind(&ui, pixels, surface_w, surface_h, OOBE_MAX_W);
        draw_oobe(&ui);
        leonos_gui_present_window((uint32_t)window_id, surface_w, surface_h,
                                  OOBE_MAX_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                update_surface_size(event.width, event.height);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                if (event.keycode == LEONOS_KEY_ENTER && advance_or_finish()) {
                    break;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1u)) {
                struct oobe_layout layout = get_oobe_layout();
                if (hit_rect_i(event.x, event.y, (int32_t)layout.button_x, (int32_t)layout.button_y,
                               (int32_t)layout.button_w, (int32_t)layout.button_h) &&
                    advance_or_finish()) {
                    break;
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
