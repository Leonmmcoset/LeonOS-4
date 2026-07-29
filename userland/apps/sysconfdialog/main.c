#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/startup.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <string.h>

#define DIALOG_W 560U
#define DIALOG_H 300U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DIALOG_W * DIALOG_H];

static int hit_rect(int32_t x, int32_t y, uint32_t rx, uint32_t ry,
                    uint32_t rw, uint32_t rh)
{
    return x >= (int32_t)rx && y >= (int32_t)ry &&
           x < (int32_t)(rx + rw) && y < (int32_t)(ry + rh);
}

static void append_char(char *text, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1U < cap) {
        text[(*pos)++] = ch;
        text[*pos] = 0;
    }
}

static void append_text(char *text, uint32_t *pos, uint32_t cap, const char *value)
{
    while (value && *value) {
        append_char(text, pos, cap, *value++);
    }
}

static void format_args(char *text, uint32_t cap,
                        const struct leonos_startup_command *command)
{
    uint32_t pos = 0;
    text[0] = 0;
    if (!command->argc) {
        append_text(text, &pos, cap, T("None", "无"));
        return;
    }
    for (uint32_t i = 0; i < command->argc; ++i) {
        if (i) {
            append_char(text, &pos, cap, ' ');
        }
        append_text(text, &pos, cap, command->args[i]);
    }
}

static void draw_dialog(struct leonos_ui_surface *ui,
                        const struct leonos_startup_dialog_request *request,
                        uint8_t remember)
{
    char args[LEONOS_STARTUP_MAX_ARGS * (LEONOS_STARTUP_ARG_LEN + 1U) + 8U];
    leonos_ui_rect(ui, 0, 0, DIALOG_W, DIALOG_H, LEONOS_UI_GRAY);
    leonos_ui_panel(ui, 16, 16, DIALOG_W - 32U, DIALOG_H - 32U, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 36,
                   T("Allow startup application?", "允许开机启动应用？"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 68, DIALOG_W - 64U,
                           T("Allow this app to start a process when you sign in?",
                             "允许此应用在你登录时自动启动一个进程？"),
                           LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 106, T("Requesting application", "申请应用"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 126, DIALOG_W - 64U, request->requester_path,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 158, T("Startup command", "启动命令"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 178, DIALOG_W - 64U, request->command.path,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    format_args(args, sizeof(args), &request->command);
    leonos_ui_text_clipped(ui, 32, 202, DIALOG_W - 64U, args,
                           LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_checkbox(ui, 32, 224,
                       T("Do not ask again if I deny this request", "拒绝后不再询问"),
                       remember, 0);
    leonos_ui_button(ui, DIALOG_W - 196U, DIALOG_H - 52U, 76U,
                     LEONOS_UI_BUTTON_H, T("Deny", "拒绝"), 0);
    leonos_ui_button(ui, DIALOG_W - 108U, DIALOG_H - 52U, 76U,
                     LEONOS_UI_BUTTON_H, T("Allow", "允许"), 0);
}

int main(int argc, char *argv[])
{
    struct leonos_startup_dialog_request request;
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    uint8_t remember = 0;
    int window_id;

    (void)argc;
    (void)argv;
    if (leonos_startup_dialog_get(&request) < 0) {
        return 1;
    }
    window_id = leonos_gui_create_app_window_ex(T("Startup Application", "启动应用"),
                                                T("Startup permission", "启动权限"),
                                                DIALOG_W, DIALOG_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        (void)leonos_startup_dialog_resolve(request.request_id,
                                            LEONOS_STARTUP_DECISION_DENY);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, DIALOG_W, DIALOG_H, DIALOG_W);
    for (;;) {
        draw_dialog(&ui, &request, remember);
        leonos_gui_present_window((uint32_t)window_id, DIALOG_W, DIALOG_H,
                                  DIALOG_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) <= 0) {
            sleep_ms(10);
            continue;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
            (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.keycode == 1U)) {
            (void)leonos_startup_dialog_resolve(request.request_id,
                                                LEONOS_STARTUP_DECISION_DENY);
            break;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.keycode == LEONOS_KEY_ENTER) {
            (void)leonos_startup_dialog_resolve(request.request_id,
                                                LEONOS_STARTUP_DECISION_ALLOW);
            break;
        }
        if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1U)) {
            if (hit_rect(event.x, event.y, 32, 218, 330, LEONOS_UI_BUTTON_H)) {
                remember = remember ? 0U : 1U;
            } else if (hit_rect(event.x, event.y, DIALOG_W - 196U, DIALOG_H - 52U,
                                76U, LEONOS_UI_BUTTON_H)) {
                (void)leonos_startup_dialog_resolve(request.request_id,
                                                    remember ? LEONOS_STARTUP_DECISION_DENY_REMEMBERED
                                                             : LEONOS_STARTUP_DECISION_DENY);
                break;
            } else if (hit_rect(event.x, event.y, DIALOG_W - 108U, DIALOG_H - 52U,
                                76U, LEONOS_UI_BUTTON_H)) {
                (void)leonos_startup_dialog_resolve(request.request_id,
                                                    LEONOS_STARTUP_DECISION_ALLOW);
                break;
            }
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
