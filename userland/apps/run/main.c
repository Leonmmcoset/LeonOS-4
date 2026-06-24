#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define RUN_W 360
#define RUN_H 148
#define PATH_MAX_LEN LEONOS_FS_PATH_LEN

static uint32_t pixels[RUN_W * RUN_H];
static char input_path[PATH_MAX_LEN] = "0:/userland/";
static char status_text[96] = "Enter a file path and press Enter";
static struct leonos_ui_edit_state input_edit;

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

static void append_text(char *dst, uint32_t cap, const char *prefix, int value)
{
    uint32_t pos = 0;
    copy_text(dst, cap, "");
    while (prefix && *prefix && pos + 1 < cap) {
        dst[pos++] = *prefix++;
    }
    if (value < 0 && pos + 1 < cap) {
        dst[pos++] = '-';
        value = -value;
    }
    if (value == 0) {
        if (pos + 1 < cap) {
            dst[pos++] = '0';
        }
    } else {
        char tmp[16];
        uint32_t n = 0;
        while (value > 0 && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (n && pos + 1 < cap) {
            dst[pos++] = tmp[--n];
        }
    }
    dst[pos] = 0;
}

static void draw_run(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, RUN_W, RUN_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, RUN_W, RUN_H, "Run");
    leonos_ui_text(ui, 12, 38, "Open LeonOS program or file path", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 12, 62, "Path:", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, 56, 58, RUN_W - 68, &input_edit, 0);
    leonos_ui_statusbar(ui, RUN_H - 28, 28, status_text);
}

static void launch_path(int window_id)
{
    char *argv[LEONOS_LAUNCH_MAX_ARGS + 1];
    int pid;
    pid = leonos_launch_command_line(input_path, argv, LEONOS_LAUNCH_MAX_ARGS + 1);
    if (pid < 0) {
        if (pid <= LEONOS_LAUNCH_ERR_EMPTY && pid >= LEONOS_LAUNCH_ERR_NO_ASSOCIATION) {
            copy_text(status_text, sizeof(status_text), leonos_launch_error_text(pid));
        } else {
            append_text(status_text, sizeof(status_text), "Launch failed ", pid);
        }
        return;
    }
    printf("[run.elf] launch command=%s pid=%d\n", input_path, pid);
    (void)window_id;
    exit(0);
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)argc;
    (void)argv;
    (void)envp;

    puts("[run.elf] run dialog starting");
    window_id = leonos_gui_create_app_window_ex("Run", "Open file path",
                                                RUN_W, RUN_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[run.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, RUN_W, RUN_H, RUN_W);
    leonos_ui_edit_state_init(&input_edit, input_path, sizeof(input_path));
    input_edit.focused = 1;
    draw_run(&ui);
    leonos_gui_present_window((uint32_t)window_id, RUN_W, RUN_H, RUN_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                if (leonos_ui_edit_state_handle_mouse(&input_edit, event.x, event.y,
                                                      56, 58, RUN_W - 68, event.buttons)) {
                    draw_run(&ui);
                    leonos_gui_present_window((uint32_t)window_id, RUN_W, RUN_H, RUN_W, pixels);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    launch_path(window_id);
                } else if (!leonos_ui_edit_state_handle_key(&input_edit, event.keycode)) {
                    continue;
                }
                draw_run(&ui);
                leonos_gui_present_window((uint32_t)window_id, RUN_W, RUN_H, RUN_W, pixels);
            }
        }
        sleep_ms(10);
    }
}
