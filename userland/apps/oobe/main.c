#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define OOBE_MAX_W 1920
#define OOBE_MAX_H 1080
#define OOBE_INITIAL_W 800
#define OOBE_INITIAL_H 600
#define OOBE_DONE_PATH "0:/etc/oobe.done"
#define OOBE_KEY_ESCAPE 1U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[OOBE_MAX_W * OOBE_MAX_H];
static uint32_t surface_w = OOBE_INITIAL_W;
static uint32_t surface_h = OOBE_INITIAL_H;
static char username[LEONOS_AUTH_USERNAME_LEN] = "admin";
static char password[LEONOS_AUTH_PASSWORD_LEN];
static struct leonos_ui_edit_state username_edit;
static struct leonos_ui_edit_state password_edit;
static uint8_t active_field;
static char status_text[128] = "Create the first administrator account";

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

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
    surface_w = width ? width : OOBE_INITIAL_W;
    surface_h = height ? height : OOBE_INITIAL_H;
}

static void update_surface_size_from_framebuffer(void)
{
    struct leonos_fb_info fb;
    if (leonos_fb_info(&fb) >= 0) {
        update_surface_size(fb.width, fb.height);
    }
}

static int write_completion_marker(void)
{
    static const char done[] = "OOBE done\n";
    int fd = open(OOBE_DONE_PATH, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, done, sizeof(done) - 1);
    close(fd);
    return wrote == (long)(sizeof(done) - 1) ? 0 : -1;
}

static void password_mask(char *dst, uint32_t cap)
{
    uint32_t i = 0;
    while (password[i] && i + 1 < cap) {
        dst[i] = '*';
        ++i;
    }
    dst[i] = 0;
}

static int username_valid(void)
{
    uint32_t len = 0;
    while (username[len]) {
        char ch = username[len];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_')) {
            return 0;
        }
        ++len;
    }
    return len > 0;
}

static void draw_oobe(struct leonos_ui_surface *ui)
{
    uint32_t panel_w = surface_w > 680 ? 560 : surface_w > 48 ? surface_w - 40 : surface_w;
    uint32_t panel_h = 320;
    uint32_t panel_x = surface_w > panel_w ? (surface_w - panel_w) / 2 : 0;
    uint32_t panel_y = surface_h > panel_h ? (surface_h - panel_h) / 2 : 0;
    char masked[LEONOS_AUTH_PASSWORD_LEN];
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_GRAY);
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h > 116 ? 116 : surface_h,
                   LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 52, T("Out-of-box experience", "开箱体验"),
                   LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_panel(ui, panel_x, panel_y, panel_w, panel_h, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 24,
                   T("Create administrator", "创建管理员"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 58,
                   T("The first account controls system settings and users.",
                     "第一个账户将管理系统设置和用户。"),
                   LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 112, T("Username", "用户名"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_edit_state_draw(ui, panel_x + 132, panel_y + 106, panel_w - 164,
                              &username_edit,
                              active_field == 0 ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_text(ui, panel_x + 24, panel_y + 152, T("Password", "密码"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    password_mask(masked, sizeof(masked));
    leonos_ui_edit(ui, panel_x + 132, panel_y + 146, panel_w - 164,
                   masked, password_edit.cursor, password_edit.scroll,
                   active_field == 1 ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_button(ui, panel_x + panel_w - 148, panel_y + panel_h - 54,
                     120, LEONOS_UI_BUTTON_H, T("Create", "创建"), 0);
    leonos_ui_text_clipped(ui, panel_x + 24, panel_y + panel_h - 48,
                           panel_w - 184, status_text,
                           LEONOS_UI_DARK, LEONOS_UI_LIGHT);
}

static int create_admin(void)
{
    struct leonos_user_info user;
    if (!username_valid()) {
        copy_text(status_text, sizeof(status_text),
                  T("Use lowercase letters, digits, and underscore only.",
                    "用户名只能包含小写字母、数字和下划线。"));
        return 0;
    }
    if (!password[0]) {
        copy_text(status_text, sizeof(status_text), T("Password required.", "请输入密码。"));
        return 0;
    }
    if (leonos_auth_create_user(username, password, LEONOS_AUTH_ROLE_ADMIN, &user) < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not create administrator.", "无法创建管理员。"));
        return 0;
    }
    if (leonos_auth_login(username, password, &user) < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Created account, but sign-in failed.", "账户已创建，但登录失败。"));
        return 0;
    }
    if (write_completion_marker() < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Created account, but setup marker failed.", "账户已创建，但完成标记写入失败。"));
        return 0;
    }
    return 1;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    struct leonos_auth_status status;
    int window_id;
    puts("[oobe.elf] starting first-run setup");
    status = (struct leonos_auth_status){0};
    if (leonos_auth_status(&status) == 0 && status.has_admin) {
        (void)write_completion_marker();
        return 0;
    }
    update_surface_size_from_framebuffer();
    leonos_ui_edit_state_init(&username_edit, username, sizeof(username));
    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
    window_id = leonos_gui_create_app_window_ex("LeonOS Setup", "First-run setup",
                                                surface_w, surface_h,
                                                LEONOS_GUI_WINDOW_FULLSCREEN);
    if (window_id <= 0) {
        printf("[oobe.elf] create window failed=%d\n", window_id);
        return 1;
    }
    for (;;) {
        leonos_ui_bind(&ui, pixels, surface_w, surface_h, OOBE_MAX_W);
        draw_oobe(&ui);
        leonos_gui_present_window((uint32_t)window_id, surface_w, surface_h,
                                  OOBE_MAX_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                update_surface_size(event.width, event.height);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed) {
                if (event.keycode == LEONOS_KEY_TAB) {
                    active_field = active_field ? 0 : 1;
                } else if (event.keycode == LEONOS_KEY_ENTER) {
                    if (create_admin()) {
                        break;
                    }
                } else if (event.keycode != OOBE_KEY_ESCAPE) {
                    if (active_field == 0) {
                        (void)leonos_ui_edit_state_handle_key(&username_edit,
                                                              event.keycode, event.pressed);
                    } else {
                        (void)leonos_ui_edit_state_handle_key(&password_edit,
                                                              event.keycode, event.pressed);
                    }
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                uint32_t panel_w = surface_w > 680 ? 560 : surface_w > 48 ? surface_w - 40 : surface_w;
                uint32_t panel_h = 320;
                uint32_t panel_x = surface_w > panel_w ? (surface_w - panel_w) / 2 : 0;
                uint32_t panel_y = surface_h > panel_h ? (surface_h - panel_h) / 2 : 0;
                if (hit_rect_i(event.x, event.y, (int32_t)(panel_x + 132),
                               (int32_t)(panel_y + 106), (int32_t)(panel_w - 164),
                               LEONOS_FONT_H + 8)) {
                    active_field = 0;
                } else if (hit_rect_i(event.x, event.y, (int32_t)(panel_x + 132),
                                      (int32_t)(panel_y + 146), (int32_t)(panel_w - 164),
                                      LEONOS_FONT_H + 8)) {
                    active_field = 1;
                } else if (hit_rect_i(event.x, event.y,
                                      (int32_t)(panel_x + panel_w - 148),
                                      (int32_t)(panel_y + panel_h - 54),
                                      120, LEONOS_UI_BUTTON_H) &&
                           create_admin()) {
                    break;
                }
            }
        } else {
            sleep_ms(20);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
    return 0;
}
