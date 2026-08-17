#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdint.h>

#define LAUNCHER_W 640U
#define LAUNCHER_H 320U
#define DOOM_PATH "0:/programs/doom/doom.elf"
#define DEFAULT_IWAD "0:/programs/doom/freedoom1.wad"
#define TASK_STATE_EXITED 3U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[LAUNCHER_W * LAUNCHER_H];
static char iwad_path[LEONOS_FS_PATH_LEN] = DEFAULT_IWAD;
static char extra_args[128];
static char status_text[160] = "Ready";
static struct leonos_ui_edit_state iwad_edit;
static struct leonos_ui_edit_state args_edit;
static uint32_t doom_pid;
static uint8_t disable_sound = 1;
static uint8_t fullscreen;

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) {
        return;
    }
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void set_status_code(const char *prefix, int code)
{
    uint32_t pos = 0;
    char digits[16];
    uint32_t count = 0;
    if (!prefix) {
        prefix = T("Status ", "状态 ");
    }
    while (prefix[pos] && pos + 1U < sizeof(status_text)) {
        status_text[pos] = prefix[pos];
        ++pos;
    }
    if (code < 0 && pos + 1U < sizeof(status_text)) {
        status_text[pos++] = '-';
        code = -code;
    }
    if (code == 0) {
        digits[count++] = '0';
    }
    while (code > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + code % 10);
        code /= 10;
    }
    while (count && pos + 1U < sizeof(status_text)) {
        status_text[pos++] = digits[--count];
    }
    status_text[pos] = 0;
}

static const char *launcher_error_text(int code)
{
    switch (code) {
    case LEONOS_LAUNCH_ERR_EMPTY:
        return T("Arguments are empty", "启动参数为空");
    case LEONOS_LAUNCH_ERR_TOO_MANY_ARGS:
        return T("Too many arguments", "启动参数过多");
    case LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE:
        return T("Missing closing quote", "缺少闭合引号");
    default:
        return leonos_launch_error_text(code);
    }
}

static int hit(int32_t x, int32_t y, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh)
{
    return x >= (int32_t)rx && y >= (int32_t)ry &&
           x < (int32_t)(rx + rw) && y < (int32_t)(ry + rh);
}

static void draw_launcher(struct leonos_ui_surface *ui)
{
    uint32_t launch_flags = doom_pid ? LEONOS_UI_BUTTON_DISABLED : 0;
    leonos_ui_rect(ui, 0, 0, LAUNCHER_W, LAUNCHER_H, LEONOS_UI_GRAY);
    leonos_ui_rect(ui, 0, 0, LAUNCHER_W, 42, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 22, 14, T("DOOM Launcher", "DOOM 启动器"), LEONOS_UI_WHITE,
                   LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 60, T("IWAD path", "IWAD 路径"), LEONOS_UI_BLACK,
                   LEONOS_UI_GRAY);
    leonos_ui_edit_state_draw(ui, 24, 80, LAUNCHER_W - 48U, &iwad_edit, 0);
    leonos_ui_text(ui, 24, 118, T("Extra DOOM arguments", "附加 DOOM 参数"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit_state_draw(ui, 24, 138, LAUNCHER_W - 48U, &args_edit, 0);
    leonos_ui_checkbox(ui, 24, 178, T("Disable sound", "禁用声音"), disable_sound, 0);
    leonos_ui_checkbox(ui, 208, 178, T("Fullscreen", "全屏"), fullscreen, 0);
    leonos_ui_button(ui, 24, 216, 144, LEONOS_UI_BUTTON_H, T("Launch", "启动"),
                     launch_flags);
    leonos_ui_button(ui, 178, 216, 104, LEONOS_UI_BUTTON_H, T("Reset", "重置"), 0);
    if (doom_pid) {
        leonos_ui_activity_bar(ui, 24, 258, LAUNCHER_W - 48U, 10,
                               (uint32_t)((leonos_uptime_ms() / 4UL) % 1000UL));
    }
    leonos_ui_statusbar(ui, LAUNCHER_H - 28U, 28, status_text);
}

static void reset_settings(void)
{
    copy_text(iwad_path, sizeof(iwad_path), DEFAULT_IWAD);
    extra_args[0] = 0;
    disable_sound = 1;
    fullscreen = 0;
    leonos_ui_edit_state_sync(&iwad_edit);
    leonos_ui_edit_state_sync(&args_edit);
    copy_text(status_text, sizeof(status_text), T("Settings reset", "设置已重置"));
}

static void launch_doom(void)
{
    char args_copy[sizeof(extra_args)];
    char *argv[LEONOS_LAUNCH_MAX_ARGS + 1];
    char *extra_argv[LEONOS_LAUNCH_MAX_ARGS + 1];
    uint32_t argc = 0;
    int extra_count;
    int pid;

    if (doom_pid) {
        return;
    }
    if (!iwad_path[0]) {
        copy_text(status_text, sizeof(status_text), T("An IWAD path is required", "需要指定 IWAD 路径"));
        return;
    }
    argv[argc++] = DOOM_PATH;
    argv[argc++] = "-iwad";
    argv[argc++] = iwad_path;
    if (disable_sound) {
        argv[argc++] = "-nosound";
    }
    if (!fullscreen) {
        argv[argc++] = "-windowed";
    }
    copy_text(args_copy, sizeof(args_copy), extra_args);
    extra_count = leonos_cmdline_split(args_copy, extra_argv,
                                       LEONOS_LAUNCH_MAX_ARGS - argc + 1U);
    if (extra_args[0] && extra_count < 0) {
        copy_text(status_text, sizeof(status_text), launcher_error_text(extra_count));
        return;
    }
    if (extra_count > 0) {
        for (int i = 0; i < extra_count; ++i) {
            argv[argc++] = extra_argv[i];
        }
    }
    argv[argc] = 0;
    pid = leonos_spawn_argv(DOOM_PATH, argv);
    if (pid < 0) {
        set_status_code(T("Launch failed: ", "启动失败: "), pid);
        return;
    }
    doom_pid = (uint32_t)pid;
    copy_text(status_text, sizeof(status_text),
               T("Starting DOOM: the game window shows loading progress",
                 "正在启动 DOOM: 游戏窗口将显示加载进度"));
}

static void update_doom_status(void)
{
    struct leonos_task_info tasks[LEONOS_TASK_MAX];
    uint64_t tick;
    int snapshot_count;
    uint32_t count;
    if (!doom_pid) {
        return;
    }
    snapshot_count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &tick);
    if (snapshot_count < 0) {
        return;
    }
    count = (uint32_t)snapshot_count;
    (void)tick;
    for (uint32_t i = 0; i < count; ++i) {
        if (tasks[i].pid != doom_pid) {
            continue;
        }
        if (tasks[i].state == TASK_STATE_EXITED) {
            int exit_status = 0;
            int reaped = wait4((int)doom_pid, &exit_status, 0, 0);
            int code = reaped > 0 ? ((exit_status >> 8) & 0xff) : -1;
            doom_pid = 0;
            set_status_code(T("DOOM exited with code ", "DOOM 已退出，代码 "), code);
        }
        return;
    }
    doom_pid = 0;
    copy_text(status_text, sizeof(status_text), T("DOOM is no longer running", "DOOM 已不再运行"));
}

static int handle_mouse(struct leonos_gui_app_event *event)
{
    int changed = 0;
    if (!event || !(event->buttons & 3U)) {
        return 0;
    }
    if (hit(event->x, event->y, 24, 80, LAUNCHER_W - 48U, LEONOS_FONT_H + 8U)) {
        args_edit.focused = 0;
        changed |= leonos_ui_edit_state_handle_mouse(&iwad_edit, event->x, event->y,
                                                     24, 80, LAUNCHER_W - 48U,
                                                     event->buttons);
    } else if (hit(event->x, event->y, 24, 138, LAUNCHER_W - 48U, LEONOS_FONT_H + 8U)) {
        iwad_edit.focused = 0;
        changed |= leonos_ui_edit_state_handle_mouse(&args_edit, event->x, event->y,
                                                     24, 138, LAUNCHER_W - 48U,
                                                     event->buttons);
    } else {
        iwad_edit.focused = 0;
        args_edit.focused = 0;
        if (hit(event->x, event->y, 24, 178, 156, LEONOS_UI_BUTTON_H)) {
            disable_sound = !disable_sound;
            changed = 1;
        } else if (hit(event->x, event->y, 208, 178, 136, LEONOS_UI_BUTTON_H)) {
            fullscreen = !fullscreen;
            changed = 1;
        } else if (hit(event->x, event->y, 24, 216, 144, LEONOS_UI_BUTTON_H)) {
            launch_doom();
            changed = 1;
        } else if (hit(event->x, event->y, 178, 216, 104, LEONOS_UI_BUTTON_H)) {
            reset_settings();
            changed = 1;
        }
    }
    return changed;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    copy_text(status_text, sizeof(status_text), T("Ready", "就绪"));
    window_id = leonos_gui_create_app_window_ex(T("DOOM Launcher", "DOOM 启动器"),
                                                 T("Configure and start DOOM", "配置并启动 DOOM"),
                                                 LAUNCHER_W, LAUNCHER_H,
                                                 LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return 1;
    }
    leonos_ui_bind(&ui, pixels, LAUNCHER_W, LAUNCHER_H, LAUNCHER_W);
    leonos_ui_edit_state_init(&iwad_edit, iwad_path, sizeof(iwad_path));
    leonos_ui_edit_state_init(&args_edit, extra_args, sizeof(extra_args));
    iwad_edit.focused = 1;

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, 40U) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                (void)handle_mouse(&event);
            } else if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                       event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER && !doom_pid) {
                    launch_doom();
                } else {
                    (void)leonos_ui_edit_state_handle_key(&iwad_edit, event.keycode,
                                                          event.pressed);
                    (void)leonos_ui_edit_state_handle_key(&args_edit, event.keycode,
                                                          event.pressed);
                }
            }
        }
        update_doom_status();
        draw_launcher(&ui);
        leonos_gui_present_window((uint32_t)window_id, LAUNCHER_W, LAUNCHER_H,
                                  LAUNCHER_W, pixels);
        sleep_ms(10);
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
