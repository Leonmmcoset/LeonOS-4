#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/png.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdint.h>
#include <unistd.h>

#define XIAOBAI_W 760U
#define XIAOBAI_H 760U
#define XIAOBAI_IMAGE_PATH "xiaobai.png"
#define XIAOBAI_IMAGE_MARGIN 8U

static uint32_t pixels[XIAOBAI_W * XIAOBAI_H];
static uint32_t *image_pixels;
static uint32_t image_width;
static uint32_t image_height;

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t index = 0;
    if (!dst || capacity == 0) {
        return;
    }
    while (src && src[index] && index + 1U < capacity) {
        dst[index] = src[index];
        ++index;
    }
    dst[index] = 0;
}

static int change_to_executable_directory(const char *path)
{
    char directory[LEONOS_FS_PATH_LEN];
    uint32_t length = 0;
    uint32_t last_separator = 0;

    if (!path || !path[0]) {
        return chdir("/programs/xiaobai");
    }
    while (path[length]) {
        if (path[length] == '/') {
            last_separator = length;
        }
        ++length;
    }
    if (last_separator == 0 || last_separator >= sizeof(directory)) {
        return chdir("/programs/xiaobai");
    }
    copy_text(directory, sizeof(directory), path);
    directory[last_separator] = 0;
    return chdir(directory);
}

static void draw_image(struct leonos_ui_surface *ui)
{
    uint32_t draw_width;
    uint32_t draw_height;
    uint32_t draw_x;
    uint32_t draw_y;

    leonos_ui_rect(ui, 0, 0, XIAOBAI_W, XIAOBAI_H, LEONOS_UI_BLACK);
    if (!image_pixels || !image_width || !image_height) {
        leonos_ui_text(ui, 24U, XIAOBAI_H / 2U - 16U,
                       "Could not decode xiaobai.png",
                       LEONOS_UI_WHITE, LEONOS_UI_BLACK);
        return;
    }

    draw_width = XIAOBAI_W - XIAOBAI_IMAGE_MARGIN * 2U;
    draw_height = (uint32_t)(((uint64_t)draw_width * image_height) / image_width);
    if (draw_height > XIAOBAI_H - XIAOBAI_IMAGE_MARGIN * 2U) {
        draw_height = XIAOBAI_H - XIAOBAI_IMAGE_MARGIN * 2U;
        draw_width = (uint32_t)(((uint64_t)draw_height * image_width) / image_height);
    }
    if (!draw_width || !draw_height) {
        return;
    }
    draw_x = (XIAOBAI_W - draw_width) / 2U;
    draw_y = (XIAOBAI_H - draw_height) / 2U;
    for (uint32_t y = 0; y < draw_height; ++y) {
        uint32_t source_y = (uint32_t)(((uint64_t)y * image_height) / draw_height);
        for (uint32_t x = 0; x < draw_width; ++x) {
            uint32_t source_x = (uint32_t)(((uint64_t)x * image_width) / draw_width);
            ui->pixels[(draw_y + y) * ui->stride + draw_x + x] =
                image_pixels[source_y * image_width + source_x];
        }
    }
}

int main(int argc, char **argv)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    if (change_to_executable_directory(argc > 0 ? argv[0] : 0) < 0) {
        printf("[xiaobai.elf] could not change to executable directory\n");
    }
    if (leonos_png_decode_file(XIAOBAI_IMAGE_PATH, &image_pixels,
                               &image_width, &image_height) < 0) {
        printf("[xiaobai.elf] relative PNG open failed path=%s\n", XIAOBAI_IMAGE_PATH);
    } else {
        printf("[xiaobai.elf] relative PNG open path=%s size=%dx%d\n",
               XIAOBAI_IMAGE_PATH, (int)image_width, (int)image_height);
    }

    window_id = leonos_gui_create_app_window_ex(
        "xiaobai", "xiaobai PNG libpng relative-path test",
        XIAOBAI_W, XIAOBAI_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[xiaobai.elf] create window failed=%d\n", window_id);
        leonos_png_free(image_pixels);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, XIAOBAI_W, XIAOBAI_H, XIAOBAI_W);
    draw_image(&ui);
    leonos_gui_present_window((uint32_t)window_id, XIAOBAI_W, XIAOBAI_H,
                              XIAOBAI_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
                (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                 event.pressed && event.keycode == LEONOS_KEY_ESCAPE)) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
                draw_image(&ui);
                leonos_gui_present_window((uint32_t)window_id, XIAOBAI_W,
                                          XIAOBAI_H, XIAOBAI_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }

    leonos_png_free(image_pixels);
    return 0;
}
