#include <leonos/auth.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/inputm.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <termios.h>
#include <unistd.h>

#define LOGIN_MAX_W 1920
#define LOGIN_MAX_H 1080
#define LOGIN_INITIAL_W 800
#define LOGIN_INITIAL_H 600
#define LOGIN_WINDOW_TITLE "LeonOS Login"
#define LOGIN_WINDOW_TEXT "Sign in"
#define LOGIN_KEY_ESCAPE 1U
#define LOGIN_KEY_UP 72U
#define LOGIN_KEY_DOWN 80U
#define LOGIN_LIST_HEADER_H (LEONOS_FONT_H + 8U)
#define LOGIN_USER_ROW_H (LEONOS_FONT_H + 4U)
#define LOGIN_VISIBLE_USERS 5U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[LOGIN_MAX_W * LOGIN_MAX_H];
static uint32_t surface_w = LOGIN_INITIAL_W;
static uint32_t surface_h = LOGIN_INITIAL_H;
static struct leonos_user_info users[LEONOS_AUTH_MAX_USERS];
static uint32_t user_count;
static uint32_t selected_user;
static char password[LEONOS_AUTH_PASSWORD_LEN];
static struct leonos_ui_edit_state password_edit;
static char status_text[128] = "Select an account";

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
    while ((width / scale) > LOGIN_MAX_W || (height / scale) > LOGIN_MAX_H) {
        ++scale;
    }
    if (scale > 1) {
        width /= scale;
        height /= scale;
    }
    surface_w = width ? width : LOGIN_INITIAL_W;
    surface_h = height ? height : LOGIN_INITIAL_H;
}

static void update_surface_size_from_framebuffer(void)
{
    struct leonos_fb_info fb;
    if (leonos_fb_info(&fb) >= 0) {
        update_surface_size(fb.width, fb.height);
    }
}

static void refresh_users(void)
{
    uint32_t count = 0;
    if (leonos_auth_list_users(users, LEONOS_AUTH_MAX_USERS, 0, &count) < 0) {
        count = 0;
    }
    user_count = count > LEONOS_AUTH_MAX_USERS ? LEONOS_AUTH_MAX_USERS : count;
    if (selected_user >= user_count) {
        selected_user = user_count ? user_count - 1 : 0;
    }
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

static void draw_login(struct leonos_ui_surface *ui)
{
    uint32_t panel_w = surface_w > 620 ? 520 : surface_w > 48 ? surface_w - 40 : surface_w;
    uint32_t panel_h = 360;
    uint32_t panel_x = surface_w > panel_w ? (surface_w - panel_w) / 2 : 0;
    uint32_t panel_y = surface_h > panel_h ? (surface_h - panel_h) / 2 : 0;
    uint32_t list_x = panel_x + 24;
    uint32_t list_y = panel_y + 112;
    uint32_t list_w = panel_w - 48;
    char masked[LEONOS_AUTH_PASSWORD_LEN];

    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_DESKTOP);
    leonos_ui_panel(ui, panel_x, panel_y, panel_w, panel_h, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 24, "LeonOS 4", LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 50, T("Sign in", "登录"), LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_list_header(ui, list_x, list_y - LOGIN_LIST_HEADER_H, list_w, T("Users", "用户"));
    if (user_count == 0) {
        leonos_ui_text(ui, list_x + 8, list_y + 8,
                       T("No enabled accounts", "没有可用账户"),
                       LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    }
    for (uint32_t i = 0; i < user_count && i < LOGIN_VISIBLE_USERS; ++i) {
        uint32_t flags = i == selected_user ? LEONOS_UI_MENU_SELECTED : 0;
        leonos_ui_list_row(ui, list_x, list_y + i * LOGIN_USER_ROW_H, list_w,
                           users[i].username, flags);
    }
    leonos_ui_text(ui, list_x, panel_y + 238, T("Password", "密码"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    password_mask(masked, sizeof(masked));
    leonos_ui_edit(ui, list_x + 92, panel_y + 232, list_w - 92,
                   masked, password_edit.cursor, password_edit.scroll,
                   LEONOS_UI_EDIT_FOCUSED);
    leonos_ui_button(ui, panel_x + panel_w - 124, panel_y + panel_h - 54,
                     96, LEONOS_UI_BUTTON_H, T("Sign in", "登录"),
                     user_count ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_text_clipped(ui, panel_x + 24, panel_y + panel_h - 48,
                           panel_w - 160, status_text,
                           LEONOS_UI_DARK, LEONOS_UI_LIGHT);
}

static int try_login(void)
{
    struct leonos_user_info user;
    if (user_count == 0) {
        copy_text(status_text, sizeof(status_text), T("No account available", "没有可用账户"));
        return 0;
    }
    if (!password[0]) {
        copy_text(status_text, sizeof(status_text), T("Password required", "请输入密码"));
        return 0;
    }
    if (leonos_auth_login(users[selected_user].username, password, &user) == 0) {
        copy_text(status_text, sizeof(status_text), T("Signed in", "已登录"));
        return 1;
    }
    password[0] = 0;
    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
    copy_text(status_text, sizeof(status_text), T("Invalid password", "密码错误"));
    return 0;
}

static int tty_read_line(const char *prompt, char *buffer, uint32_t capacity,
                         uint8_t masked)
{
    uint32_t length = 0;
    char input;
    if (!buffer || capacity < 2U) {
        return 0;
    }
    buffer[0] = 0;
    if (prompt) {
        write(1, prompt, strlen(prompt));
    }
    while (read(0, &input, 1) > 0) {
        if (input == '\r') {
            continue;
        }
        if (input == '\n') {
            buffer[length] = 0;
            /* Ordinary input is echoed by the terminal; secret input has
             * ECHO disabled and needs an explicit line ending. */
            if (masked) {
                write(1, "\r\n", 2);
            }
            return 1;
        }
        if (input == '\b' || (uint8_t)input == 127U) {
            if (length) {
                --length;
                write(1, "\b \b", 3);
            }
            continue;
        }
        if ((uint8_t)input >= 32U && length + 1U < capacity) {
            buffer[length++] = input;
            buffer[length] = 0;
            if (masked) {
                write(1, "*", 1);
            }
        }
    }
    return 0;
}

static int tty_read_secret(const char *prompt, char *buffer, uint32_t capacity)
{
    struct termios termios;
    struct termios saved_termios;
    int ret;

    /* Login is a PTY child. The fd interface is available to it, unlike the
     * owner-only PTY management interface. */
    if (tcgetattr(0, &termios) != 0) {
        return 0;
    }
    saved_termios = termios;
    termios.c_lflag &= (tcflag_t)~(ECHO | ECHONL);
    if (tcsetattr(0, TCSANOW, &termios) != 0) {
        return 0;
    }
    ret = tty_read_line(prompt, buffer, capacity, 1);
    (void)tcsetattr(0, TCSANOW, &saved_termios);
    return ret;
}

static int tty_login_main(void)
{
    char username_input[LEONOS_AUTH_USERNAME_LEN];
    char password_input[LEONOS_AUTH_PASSWORD_LEN];
    struct leonos_user_info user;
    puts("LeonOS login");
    refresh_users();
    if (!user_count) {
        puts("No enabled accounts.");
        return 1;
    }
    for (;;) {
        if (!tty_read_line("Username: ", username_input, sizeof(username_input), 0)) {
            return 1;
        }
        if (!tty_read_secret("Password: ", password_input, sizeof(password_input))) {
            puts("Secure password input is unavailable.");
            return 1;
        }
        if (leonos_auth_login(username_input, password_input, &user) == 0) {
            puts("Login successful.");
            return 0;
        }
        puts("Login failed.");
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    if (isatty(STDIN_FILENO)) {
        return tty_login_main();
    }
    puts("[login.elf] starting login UI");
    update_surface_size_from_framebuffer();
    refresh_users();
    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
    window_id = leonos_gui_create_app_window_ex(LOGIN_WINDOW_TITLE, LOGIN_WINDOW_TEXT,
                                                surface_w, surface_h,
                                                LEONOS_GUI_WINDOW_FULLSCREEN);
    if (window_id <= 0) {
        printf("[login.elf] create window failed=%d\n", window_id);
        return 1;
    }
    {
        struct leonos_inputm_context context = {
            .window_id = (uint32_t)window_id,
            .flags = LEONOS_INPUTM_CONTEXT_FOCUSED | LEONOS_INPUTM_CONTEXT_SECURE,
        };
        (void)leonos_inputm_set_context(&context);
    }
    leonos_ui_bind(&ui, pixels, surface_w, surface_h, LOGIN_MAX_W);
    for (;;) {
        draw_login(&ui);
        leonos_gui_present_window((uint32_t)window_id, surface_w, surface_h,
                                  LOGIN_MAX_W, pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                update_surface_size(event.width, event.height);
                leonos_ui_bind(&ui, pixels, surface_w, surface_h, LOGIN_MAX_W);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed) {
                if (event.keycode == LOGIN_KEY_UP && selected_user > 0) {
                    --selected_user;
                    password[0] = 0;
                    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
                } else if (event.keycode == LOGIN_KEY_DOWN && selected_user + 1 < user_count) {
                    ++selected_user;
                    password[0] = 0;
                    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
                } else if (event.keycode == LEONOS_KEY_ENTER) {
                    if (try_login()) {
                        break;
                    }
                } else if (event.keycode != LOGIN_KEY_ESCAPE) {
                    (void)leonos_ui_edit_state_handle_key(&password_edit,
                                                          event.keycode,
                                                          event.pressed);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                uint32_t panel_w = surface_w > 620 ? 520 : surface_w > 48 ? surface_w - 40 : surface_w;
                uint32_t panel_h = 360;
                uint32_t panel_x = surface_w > panel_w ? (surface_w - panel_w) / 2 : 0;
                uint32_t panel_y = surface_h > panel_h ? (surface_h - panel_h) / 2 : 0;
                uint32_t list_x = panel_x + 24;
                uint32_t list_y = panel_y + 112;
                uint32_t list_w = panel_w - 48;
                for (uint32_t i = 0; i < user_count && i < LOGIN_VISIBLE_USERS; ++i) {
                    if (hit_rect_i(event.x, event.y, (int32_t)list_x,
                                   (int32_t)(list_y + i * LOGIN_USER_ROW_H),
                                   (int32_t)list_w, (int32_t)LOGIN_USER_ROW_H)) {
                        selected_user = i;
                        password[0] = 0;
                        leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
                    }
                }
                if (hit_rect_i(event.x, event.y, (int32_t)(panel_x + panel_w - 124),
                               (int32_t)(panel_y + panel_h - 54), 96,
                               LEONOS_UI_BUTTON_H) && try_login()) {
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
