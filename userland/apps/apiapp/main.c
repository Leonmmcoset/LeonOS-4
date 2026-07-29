#include <leonos/api.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/ui.h>
#include <string.h>

#define T(en, zh) leonos_i18n((en), (zh))

#define WIZARD_W 480U
#define WIZARD_H 360U

static uint32_t wizard_pixels[WIZARD_W * WIZARD_H];

struct install_state {
    char api_path[LEONOS_API_PATH_MAX];
    char install_path[LEONOS_API_PATH_MAX];
    uint32_t create_shortcut;
    struct leonos_api_info info;
    int step;
};

static void draw_welcome_page(struct leonos_ui_surface *ui,
                              const struct leonos_api_info *info)
{
    leonos_ui_rect(ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 24, T("Application Installer", "应用安装程序"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 64,
                   T("Ready to install:", "准备安装："),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 88, WIZARD_W - 64U, info->name,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    if (info->version[0]) {
        leonos_ui_text(ui, 32, 112,
                       T("Version:", "版本："),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
        leonos_ui_text(ui, 120, 112, info->version,
                       LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_text(ui, 32, 144,
                   T("Install location:", "安装位置："),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 168, WIZARD_W - 64U,
                           info->default_path,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_button(ui, WIZARD_W - 120U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("Next >", "下一步 >"), 0);
    leonos_ui_button(ui, WIZARD_W - 216U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("Cancel", "取消"), 0);
}

static void draw_install_page(struct leonos_ui_surface *ui,
                              struct install_state *state)
{
    leonos_ui_rect(ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 24,
                   T("Install Options", "安装选项"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 64,
                   T("Install path:", "安装路径："),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 32, 88, WIZARD_W - 64U, state->install_path,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_checkbox(ui, 32, 132,
                       T("Create desktop shortcut", "创建桌面快捷方式"),
                       (int)state->create_shortcut, 0);
    leonos_ui_button(ui, WIZARD_W - 120U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("Install", "安装"), 0);
    leonos_ui_button(ui, WIZARD_W - 216U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("Cancel", "取消"), 0);
    leonos_ui_button(ui, WIZARD_W - 312U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("< Back", "< 上一步"), 0);
}

static void draw_finish_page(struct leonos_ui_surface *ui, int success,
                             const char *name)
{
    leonos_ui_rect(ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);
    if (success) {
        leonos_ui_text(ui, 32, 24,
                       T("Installation Complete", "安装完成"),
                       LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text(ui, 32, 64,
                       T("Successfully installed:", "成功安装："),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 32, 88, WIZARD_W - 64U, name,
                               LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    } else {
        leonos_ui_text(ui, 32, 24,
                       T("Installation Failed", "安装失败"),
                       LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text(ui, 32, 64,
                       T("Could not install:", "无法安装："),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 32, 88, WIZARD_W - 64U, name,
                               LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_button(ui, WIZARD_W - 120U, WIZARD_H - 52U, 88U,
                     LEONOS_UI_BUTTON_H,
                     T("Close", "关闭"), 0);
}

static int run_wizard(const char *api_path)
{
    struct install_state state;
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    int window_id;
    int done = 0;
    int result = 0;
    int step = 0;
    uint32_t api_path_len;

    memset(&state, 0, sizeof(state));
    api_path_len = (uint32_t)strlen(api_path);
    if (api_path_len >= sizeof(state.api_path)) {
        return 1;
    }
    memcpy(state.api_path, api_path, api_path_len + 1U);

    if (!leonos_api_parse_info(api_path, &state.info)) {
        return 1;
    }
    memcpy(state.install_path, state.info.default_path,
           strlen(state.info.default_path) + 1U);
    state.create_shortcut = state.info.desktop_shortcut;

    window_id = leonos_gui_create_app_window_ex(
        T("API Installer", "API 安装程序"),
        T("API Installer", "API 安装程序"),
        WIZARD_W, WIZARD_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        return 1;
    }
    leonos_ui_bind(&ui, wizard_pixels, WIZARD_W, WIZARD_H, WIZARD_W);

    while (!done) {
        leonos_ui_rect(&ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);

        if (step == 0) {
            draw_welcome_page(&ui, &state.info);
        } else if (step == 1) {
            draw_install_page(&ui, &state);
        } else {
            draw_finish_page(&ui, result, state.info.name);
        }
        leonos_gui_present_window((uint32_t)window_id, WIZARD_W, WIZARD_H,
                                  WIZARD_W, wizard_pixels);

        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1U)) {
                if (step == 0) {
                    if (event.x >= (int32_t)(WIZARD_W - 120) &&
                        event.x < (int32_t)(WIZARD_W - 32) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        step = 1;
                    }
                    if (event.x >= (int32_t)(WIZARD_W - 216) &&
                        event.x < (int32_t)(WIZARD_W - 128) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        done = 1;
                    }
                } else if (step == 1) {
                    if (event.x >= (int32_t)(WIZARD_W - 120) &&
                        event.x < (int32_t)(WIZARD_W - 32) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        result = leonos_api_install(
                            state.api_path, state.install_path,
                            state.create_shortcut);
                        step = 2;
                    }
                    if (event.x >= (int32_t)(WIZARD_W - 216) &&
                        event.x < (int32_t)(WIZARD_W - 128) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        done = 1;
                    }
                    if (event.x >= (int32_t)(WIZARD_W - 312) &&
                        event.x < (int32_t)(WIZARD_W - 224) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        step = 0;
                    }
                    if (event.x >= 32 &&
                        event.x < 200 &&
                        event.y >= 132 &&
                        event.y < 152) {
                        state.create_shortcut =
                            state.create_shortcut ? 0U : 1U;
                    }
                } else {
                    if (event.x >= (int32_t)(WIZARD_W - 120) &&
                        event.x < (int32_t)(WIZARD_W - 32) &&
                        event.y >= (int32_t)(WIZARD_H - 52) &&
                        event.y < (int32_t)(WIZARD_H - 52 + LEONOS_UI_BUTTON_H)) {
                        done = 1;
                    }
                }
            }
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return result ? 0 : 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || !argv[1] || !argv[1][0]) {
        return 1;
    }
    return run_wizard(argv[1]);
}
