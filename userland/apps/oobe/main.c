#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/inputm.h>
#include <leonos/license.h>
#include <leonos/net.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <termios.h>
#include <unistd.h>

#define OOBE_MAX_W 1920
#define OOBE_MAX_H 1080
#define OOBE_INITIAL_W 800
#define OOBE_INITIAL_H 600
#define OOBE_DONE_PATH "/system/state/oobe.done"
#define OOBE_KEY_ESCAPE 1U
#define OOBE_ACCOUNT_READY_RETRIES 50U
#define OOBE_ACCOUNT_READY_RETRY_MS 20U
#define T(en, zh) leonos_i18n((en), (zh))

#if CONFIG_LICENSE_DEBUG_LOG
#define OOBE_LOG(...) printf(__VA_ARGS__)
#define OOBE_LOG_LINE(text) puts(text)
#else
#define OOBE_LOG(...) ((void)0)
#define OOBE_LOG_LINE(text) ((void)0)
#endif

void browser_embed_init(uint32_t width, uint32_t height, const char *initial_url);
void browser_embed_resize(uint32_t width, uint32_t height);
void browser_embed_draw(struct leonos_ui_surface *surface);
void browser_embed_handle_mouse_button(struct leonos_gui_app_event *event);
void browser_embed_handle_mouse_wheel(struct leonos_gui_app_event *event);
void browser_embed_handle_key(struct leonos_gui_app_event *event);
int browser_embed_should_exit(void);
void browser_embed_clear_exit(void);
int browser_embed_input_active(void);

enum oobe_page {
    OOBE_PAGE_LICENSE = 0,
    OOBE_PAGE_ADMIN = 1,
    OOBE_PAGE_LICENSE_BROWSER = 2,
};

enum license_field {
    LICENSE_FIELD_ONLINE_EMAIL = 0,
    LICENSE_FIELD_ONLINE_KEY = 1,
    LICENSE_FIELD_OFFLINE_EMAIL = 2,
    LICENSE_FIELD_OFFLINE_KEY = 3,
    LICENSE_FIELD_COUNT = 4,
};

static uint32_t pixels[OOBE_MAX_W * OOBE_MAX_H];
static uint32_t surface_w = OOBE_INITIAL_W;
static uint32_t surface_h = OOBE_INITIAL_H;
static char username[LEONOS_AUTH_USERNAME_LEN] = "root";
static char password[LEONOS_AUTH_PASSWORD_LEN];
static char online_email[LEONOS_LICENSE_EMAIL_LEN];
static char online_key[LEONOS_LICENSE_KEY_LEN];
static char offline_email[LEONOS_LICENSE_EMAIL_LEN];
static char offline_key[LEONOS_LICENSE_KEY_LEN];
static struct leonos_ui_edit_state username_edit;
static struct leonos_ui_edit_state password_edit;
static struct leonos_ui_edit_state online_email_edit;
static struct leonos_ui_edit_state online_key_edit;
static struct leonos_ui_edit_state offline_email_edit;
static struct leonos_ui_edit_state offline_key_edit;
/* Start administrator setup with the password field selected. */
static uint8_t active_admin_field = 1;
static uint8_t active_license_field;
static uint8_t current_page = OOBE_PAGE_LICENSE;
static uint8_t license_ready;
static uint8_t show_renew_dhcp;
static char status_text[160] = "Create the first administrator account";
static char license_status_text[160] = "Activate LeonOS before creating the first administrator";

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

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

static void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
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
    struct leonos_stat st;
    int fd;
    int close_ret;
    long wrote;

    fd = open(OOBE_DONE_PATH, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, done, sizeof(done) - 1);
    close_ret = close(fd);
    if (wrote != (long)(sizeof(done) - 1)) {
        return -1;
    }
    if (close_ret < 0) {
        return close_ret;
    }
    st = (struct leonos_stat){0};
    if (leonos_stat_legacy(OOBE_DONE_PATH, &st) < 0 ||
        st.type != LEONOS_FS_TYPE_FILE ||
        st.size != sizeof(done) - 1U) {
        return -1;
    }
    return 0;
}

static int license_is_valid(void)
{
    struct leonos_license_info info;
    if (!leonos_license_required()) {
        return 1;
    }
    info = (struct leonos_license_info){0};
    if (leonos_license_status(&info) < 0) {
        return 0;
    }
    return info.status == LEONOS_LICENSE_STATUS_OK;
}

static int admin_exists(void)
{
    struct leonos_auth_status status;
    status = (struct leonos_auth_status){0};
    return leonos_auth_status(&status) == 0 && status.has_admin;
}

/* The account database is persisted through the storage service.  On an AHCI
 * cold boot, its write completion can become visible to a follow-up auth
 * request a tick later.  Do not turn that short handoff into a second OOBE. */
static int wait_for_created_admin(void)
{
    for (uint32_t attempt = 0; attempt < OOBE_ACCOUNT_READY_RETRIES; ++attempt) {
        if (admin_exists()) {
            return 1;
        }
        sleep_ms(OOBE_ACCOUNT_READY_RETRY_MS);
    }
    return 0;
}

static int login_created_admin(const char *account_name, const char *account_password,
                               struct leonos_user_info *user)
{
    int ret = -1;
    for (uint32_t attempt = 0; attempt < OOBE_ACCOUNT_READY_RETRIES; ++attempt) {
        ret = leonos_auth_login(account_name, account_password, user);
        if (ret == 0) {
            return 0;
        }
        sleep_ms(OOBE_ACCOUNT_READY_RETRY_MS);
    }
    return ret;
}

static int both_setup_parts_complete(void)
{
    return license_is_valid() && admin_exists();
}

static int finish_if_complete(char *detail, uint32_t detail_cap)
{
    if (!both_setup_parts_complete()) {
        return 0;
    }
    if (write_completion_marker() < 0) {
        OOBE_LOG_LINE("[oobe.elf] setup complete; completion marker write failed");
    }
    copy_text(detail, detail_cap, T("Setup complete.", "设置已完成。"));
    return 1;
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

static void panel_geometry(uint32_t desired_w, uint32_t desired_h,
                           uint32_t *x, uint32_t *y,
                           uint32_t *w, uint32_t *h)
{
    *w = surface_w > desired_w + 40U ? desired_w : surface_w > 48U ? surface_w - 40U : surface_w;
    *h = desired_h;
    *x = surface_w > *w ? (surface_w - *w) / 2U : 0;
    if (surface_h > 136U + *h) {
        *y = 124U + (surface_h - 136U - *h) / 2U;
    } else {
        *y = surface_h > *h ? (surface_h - *h) / 2U : 0;
    }
}

static struct leonos_ui_edit_state *license_edit_state(uint8_t field)
{
    switch (field) {
    case LICENSE_FIELD_ONLINE_EMAIL:
        return &online_email_edit;
    case LICENSE_FIELD_ONLINE_KEY:
        return &online_key_edit;
    case LICENSE_FIELD_OFFLINE_EMAIL:
        return &offline_email_edit;
    case LICENSE_FIELD_OFFLINE_KEY:
        return &offline_key_edit;
    default:
        return &online_email_edit;
    }
}

static struct leonos_ui_edit_state *admin_edit_state(void)
{
    return active_admin_field == 0 ? &username_edit : &password_edit;
}

static struct leonos_ui_edit_state *current_edit_state(void)
{
    if (current_page == OOBE_PAGE_LICENSE) {
        return license_edit_state(active_license_field);
    }
    return admin_edit_state();
}

static void draw_header(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h, LEONOS_UI_GRAY);
    leonos_ui_rect(ui, 0, 0, surface_w, surface_h > 116 ? 116 : surface_h,
                   LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 24, "LeonOS 4", LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(ui, 24, 52, T("Out-of-box experience", "开箱体验"),
                   LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
}

static void draw_license_page(struct leonos_ui_surface *ui)
{
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_w;
    uint32_t panel_h;
    uint32_t edit_x;
    uint32_t edit_w;
    char install_id[LEONOS_LICENSE_INSTALL_ID_LEN];
    char line[192];
    uint32_t pos;
    panel_geometry(720U, 470U, &panel_x, &panel_y, &panel_w, &panel_h);
    edit_x = panel_x + 150U;
    edit_w = panel_w > 182U ? panel_w - 182U : 120U;
    if (leonos_license_install_id(install_id, sizeof(install_id)) < 0 ||
        !install_id[0]) {
        copy_text(install_id, sizeof(install_id), "unavailable");
    }
    leonos_ui_panel(ui, panel_x, panel_y, panel_w, panel_h, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 20,
                   T("License activation", "许可证激活"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 48,
                   T("Activate online or enter a 50-character offline key.",
                     "请在线激活，或输入 50 位离线密钥。"),
                   LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    line[0] = 0;
    pos = 0;
    append_text(line, &pos, sizeof(line), "Machine ID: ");
    append_text(line, &pos, sizeof(line), install_id);
    leonos_ui_text_clipped(ui, panel_x + 24, panel_y + 74,
                           panel_w > 48U ? panel_w - 48U : panel_w,
                           line, LEONOS_UI_DARK, LEONOS_UI_LIGHT);

    leonos_ui_text(ui, panel_x + 24, panel_y + 136,
                   T("Online", "在线验证"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 170, T("Email", "邮箱"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_edit_state_draw(ui, edit_x, panel_y + 164, edit_w,
                              &online_email_edit,
                              active_license_field == LICENSE_FIELD_ONLINE_EMAIL
                                  ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_text(ui, panel_x + 24, panel_y + 210, T("Key", "密钥"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_edit_state_draw(ui, edit_x, panel_y + 204, edit_w,
                              &online_key_edit,
                              active_license_field == LICENSE_FIELD_ONLINE_KEY
                                  ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_button(ui, panel_x + 24, panel_y + 242,
                     162, LEONOS_UI_BUTTON_H,
                     T("License Website", "许可证网站"), 0);
    leonos_ui_button(ui, panel_x + panel_w - 190, panel_y + 242,
                     162, LEONOS_UI_BUTTON_H, T("Activate Online", "在线激活"), 0);

    leonos_ui_text(ui, panel_x + 24, panel_y + 296,
                   T("Offline", "离线验证"), LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, panel_x + 24, panel_y + 330, T("Email", "邮箱"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_edit_state_draw(ui, edit_x, panel_y + 324, edit_w,
                              &offline_email_edit,
                              active_license_field == LICENSE_FIELD_OFFLINE_EMAIL
                                  ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_text(ui, panel_x + 24, panel_y + 370, T("Offline key", "离线密钥"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_edit_state_draw(ui, edit_x, panel_y + 364, edit_w,
                              &offline_key_edit,
                              active_license_field == LICENSE_FIELD_OFFLINE_KEY
                                  ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_button(ui, panel_x + panel_w - 190, panel_y + 402,
                     162, LEONOS_UI_BUTTON_H, T("Activate Offline", "离线激活"), 0);
    if (show_renew_dhcp) {
        leonos_ui_button(ui, panel_x + 24, panel_y + 402,
                         132, LEONOS_UI_BUTTON_H, "Renew DHCP", 0);
    }
    leonos_ui_text_clipped(ui, panel_x + 24, panel_y + panel_h - 24,
                           panel_w > 48U ? panel_w - 48U : panel_w,
                           license_status_text,
                           LEONOS_UI_DARK, LEONOS_UI_LIGHT);
}

static void draw_admin_page(struct leonos_ui_surface *ui)
{
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_w;
    uint32_t panel_h;
    char masked[LEONOS_AUTH_PASSWORD_LEN];
    panel_geometry(560U, 320U, &panel_x, &panel_y, &panel_w, &panel_h);
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
                              active_admin_field == 0 ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_text(ui, panel_x + 24, panel_y + 152, T("Password", "密码"),
                   LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    password_mask(masked, sizeof(masked));
    leonos_ui_edit(ui, panel_x + 132, panel_y + 146, panel_w - 164,
                   masked, password_edit.cursor, password_edit.scroll,
                   active_admin_field == 1 ? LEONOS_UI_EDIT_FOCUSED : 0);
    leonos_ui_button(ui, panel_x + panel_w - 148, panel_y + panel_h - 54,
                     120, LEONOS_UI_BUTTON_H, T("Create", "创建"), 0);
    leonos_ui_text_clipped(ui, panel_x + 24, panel_y + panel_h - 48,
                           panel_w - 184, status_text,
                           LEONOS_UI_DARK, LEONOS_UI_LIGHT);
}

static void draw_oobe(struct leonos_ui_surface *ui)
{
    if (current_page == OOBE_PAGE_LICENSE_BROWSER) {
        browser_embed_draw(ui);
        return;
    }
    draw_header(ui);
    if (current_page == OOBE_PAGE_LICENSE) {
        draw_license_page(ui);
    } else {
        draw_admin_page(ui);
    }
}

static int activate_online(void)
{
    OOBE_LOG("[oobe.elf] online activation submit email_len=%d key_len=%d\n",
             (int)strlen(online_email), (int)strlen(online_key));
    int ret = leonos_license_activate_online(online_email, online_key,
                                             license_status_text,
                                             sizeof(license_status_text));
    OOBE_LOG("[oobe.elf] online activation ret=%d detail=%s\n",
             ret, license_status_text);
    if (ret == LEONOS_LICENSE_STATUS_NETWORK) {
        show_renew_dhcp = 1;
        OOBE_LOG_LINE("[oobe.elf] online activation marked network failure; showing Renew DHCP");
        return 0;
    }
    if (ret != LEONOS_LICENSE_STATUS_OK) {
        return 0;
    }
    license_ready = 1;
    show_renew_dhcp = 0;
    OOBE_LOG_LINE("[oobe.elf] online activation accepted");
    if (finish_if_complete(license_status_text, sizeof(license_status_text))) {
        return 1;
    }
    current_page = OOBE_PAGE_ADMIN;
    active_admin_field = 1;
    copy_text(status_text, sizeof(status_text),
              T("License activated. Create the first administrator.",
                "许可证已激活。请创建第一个管理员。"));
    return 0;
}

static int activate_offline(void)
{
    int ret = leonos_license_activate_offline(offline_email, offline_key,
                                              license_status_text,
                                              sizeof(license_status_text));
    if (ret != LEONOS_LICENSE_STATUS_OK) {
        return 0;
    }
    license_ready = 1;
    show_renew_dhcp = 0;
    if (finish_if_complete(license_status_text, sizeof(license_status_text))) {
        return 1;
    }
    current_page = OOBE_PAGE_ADMIN;
    active_admin_field = 1;
    copy_text(status_text, sizeof(status_text),
              T("License activated. Create the first administrator.",
                "许可证已激活。请创建第一个管理员。"));
    return 0;
}

static void close_license_browser(void)
{
    browser_embed_clear_exit();
    license_ready = (uint8_t)license_is_valid();
    current_page = license_ready ? OOBE_PAGE_ADMIN : OOBE_PAGE_LICENSE;
    if (current_page == OOBE_PAGE_ADMIN) {
        active_admin_field = 1;
    }
}

static void open_license_browser(void)
{
    char server[LEONOS_LICENSE_SERVER_URL_LEN];
    leonos_license_default_server(server, sizeof(server));
    if (!server[0]) {
        copy_text(license_status_text, sizeof(license_status_text),
                  T("License website is unavailable in this build.",
                    "此构建未提供许可证网站。"));
        return;
    }
    browser_embed_init(surface_w, surface_h, server);
    current_page = OOBE_PAGE_LICENSE_BROWSER;
}

static void renew_dhcp(void)
{
    struct leonos_net_dhcp dhcp;
    char msg[160];
    uint32_t pos = 0;
    dhcp = (struct leonos_net_dhcp){0};
    OOBE_LOG_LINE("[oobe.elf] DHCP renew requested from license page");
    int ret = leonos_net_dhcp_renew(8000U, &dhcp);
    OOBE_LOG("[oobe.elf] DHCP renew ret=%d status=%d ip=0x%x gateway=0x%x dns=0x%x\n",
             ret, (int)dhcp.status, dhcp.config.local_ip,
             dhcp.config.gateway_ip, dhcp.config.dns_ip);
    if (ret < 0 ||
        dhcp.status != LEONOS_NET_STATUS_OK) {
        msg[0] = 0;
        append_text(msg, &pos, sizeof(msg), "DHCP renew failed, status ");
        append_u32(msg, &pos, sizeof(msg), dhcp.status);
        copy_text(license_status_text, sizeof(license_status_text), msg);
        show_renew_dhcp = 1;
        return;
    }
    show_renew_dhcp = 0;
    copy_text(license_status_text, sizeof(license_status_text),
              T("DHCP renewed. Try online activation again.",
                "DHCP 已续租。请再次尝试在线激活。"));
}

static int create_admin(void)
{
    struct leonos_user_info user;
    int ret;
    if (!license_ready && !license_is_valid()) {
        current_page = OOBE_PAGE_LICENSE;
        copy_text(license_status_text, sizeof(license_status_text),
                  T("Activate LeonOS before creating the administrator.",
                    "请先激活 LeonOS，再创建管理员。"));
        return 0;
    }
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
    ret = leonos_auth_create_user(username, password, LEONOS_AUTH_ROLE_ADMIN, &user);
    OOBE_LOG("[oobe.elf] create administrator username=%s password_len=%u ret=%d\n",
             username, (unsigned)strlen(password), ret);
    if (ret < 0) {
        uint32_t pos = 0;
        status_text[0] = 0;
        append_text(status_text, &pos, sizeof(status_text),
                    T("Could not create administrator (error ",
                      "无法创建管理员（错误 "));
        if (ret < 0) {
            append_char(status_text, &pos, sizeof(status_text), '-');
            append_u32(status_text, &pos, sizeof(status_text), (uint32_t)(-(ret + 1)) + 1U);
        } else {
            append_u32(status_text, &pos, sizeof(status_text), (uint32_t)ret);
        }
        append_text(status_text, &pos, sizeof(status_text), T(").", "）。"));
        return 0;
    }
    if (!wait_for_created_admin()) {
        copy_text(status_text, sizeof(status_text),
                  T("Account was created, but is not ready yet. Try again.",
                    "账户已创建，但尚未就绪。请重试。"));
        return 0;
    }
    ret = login_created_admin(username, password, &user);
    OOBE_LOG("[oobe.elf] first administrator login username=%s ret=%d\n", username, ret);
    if (ret < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Created account, but sign-in failed.", "账户已创建，但登录失败。"));
        return 0;
    }
    if (write_completion_marker() < 0) {
        OOBE_LOG_LINE("[oobe.elf] administrator created; completion marker write failed");
    }
    return 1;
}

static int handle_license_click(int32_t x, int32_t y)
{
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_w;
    uint32_t panel_h;
    uint32_t edit_x;
    uint32_t edit_w;
    panel_geometry(720U, 470U, &panel_x, &panel_y, &panel_w, &panel_h);
    (void)panel_h;
    edit_x = panel_x + 150U;
    edit_w = panel_w > 182U ? panel_w - 182U : 120U;
    if (hit_rect_i(x, y, (int32_t)edit_x, (int32_t)(panel_y + 164),
                   (int32_t)edit_w, LEONOS_FONT_H + 8)) {
        active_license_field = LICENSE_FIELD_ONLINE_EMAIL;
        return 0;
    }
    if (hit_rect_i(x, y, (int32_t)edit_x, (int32_t)(panel_y + 204),
                   (int32_t)edit_w, LEONOS_FONT_H + 8)) {
        active_license_field = LICENSE_FIELD_ONLINE_KEY;
        return 0;
    }
    if (hit_rect_i(x, y, (int32_t)edit_x, (int32_t)(panel_y + 324),
                   (int32_t)edit_w, LEONOS_FONT_H + 8)) {
        active_license_field = LICENSE_FIELD_OFFLINE_EMAIL;
        return 0;
    }
    if (hit_rect_i(x, y, (int32_t)edit_x, (int32_t)(panel_y + 364),
                   (int32_t)edit_w, LEONOS_FONT_H + 8)) {
        active_license_field = LICENSE_FIELD_OFFLINE_KEY;
        return 0;
    }
    if (hit_rect_i(x, y, (int32_t)(panel_x + panel_w - 190),
                   (int32_t)(panel_y + 242), 162, LEONOS_UI_BUTTON_H)) {
        return activate_online();
    }
    if (hit_rect_i(x, y, (int32_t)(panel_x + 24),
                   (int32_t)(panel_y + 242), 162, LEONOS_UI_BUTTON_H)) {
        open_license_browser();
        return 0;
    }
    if (hit_rect_i(x, y, (int32_t)(panel_x + panel_w - 190),
                   (int32_t)(panel_y + 402), 162, LEONOS_UI_BUTTON_H)) {
        return activate_offline();
    }
    if (show_renew_dhcp &&
        hit_rect_i(x, y, (int32_t)(panel_x + 24), (int32_t)(panel_y + 402),
                   132, LEONOS_UI_BUTTON_H)) {
        renew_dhcp();
    }
    return 0;
}

static int handle_admin_click(int32_t x, int32_t y)
{
    uint32_t panel_x;
    uint32_t panel_y;
    uint32_t panel_w;
    uint32_t panel_h;
    panel_geometry(560U, 320U, &panel_x, &panel_y, &panel_w, &panel_h);
    if (hit_rect_i(x, y, (int32_t)(panel_x + 132),
                   (int32_t)(panel_y + 106), (int32_t)(panel_w - 164),
                   LEONOS_FONT_H + 8)) {
        active_admin_field = 0;
    } else if (hit_rect_i(x, y, (int32_t)(panel_x + 132),
                          (int32_t)(panel_y + 146), (int32_t)(panel_w - 164),
                          LEONOS_FONT_H + 8)) {
        active_admin_field = 1;
    } else if (hit_rect_i(x, y,
                          (int32_t)(panel_x + panel_w - 148),
                          (int32_t)(panel_y + panel_h - 54),
                          120, LEONOS_UI_BUTTON_H)) {
        return create_admin();
    }
    return 0;
}

static int handle_key_down(uint8_t keycode)
{
    if (current_page == OOBE_PAGE_LICENSE) {
        if (keycode == LEONOS_KEY_TAB) {
            active_license_field = (uint8_t)((active_license_field + 1U) % LICENSE_FIELD_COUNT);
            return 0;
        }
        if (keycode == LEONOS_KEY_ENTER) {
            if (active_license_field == LICENSE_FIELD_ONLINE_EMAIL ||
                active_license_field == LICENSE_FIELD_ONLINE_KEY) {
                return activate_online();
            }
            return activate_offline();
        }
        if (keycode != OOBE_KEY_ESCAPE) {
            (void)leonos_ui_edit_state_handle_key(license_edit_state(active_license_field),
                                                  keycode, 1);
        }
        return 0;
    }
    if (keycode == LEONOS_KEY_TAB) {
        active_admin_field = active_admin_field ? 0 : 1;
    } else if (keycode == LEONOS_KEY_ENTER) {
        return create_admin();
    } else if (keycode != OOBE_KEY_ESCAPE) {
        (void)leonos_ui_edit_state_handle_key(admin_edit_state(), keycode, 1);
    }
    return 0;
}

static void handle_key_up(uint8_t keycode)
{
    (void)leonos_ui_edit_state_handle_key(current_edit_state(), keycode, 0);
}

static void oobe_update_inputm_context(uint32_t window_id)
{
    struct leonos_inputm_context context = {
        .window_id = window_id,
        .flags = LEONOS_INPUTM_CONTEXT_FOCUSED,
    };
    if (current_page == OOBE_PAGE_ADMIN && active_admin_field == 1U) {
        context.flags |= LEONOS_INPUTM_CONTEXT_SECURE;
    }
    (void)leonos_inputm_set_context(&context);
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
            /* The terminal echoes the newline for ordinary fields. Secret
             * fields disable ECHO, so add their line ending explicitly. */
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

    /* OOBE is a child of the console PTY owner, so use fd-oriented termios
     * requests rather than the owner-only PTY management interface. */
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

static int tty_oobe_main(void)
{
    struct leonos_license_info license;
    struct leonos_user_info user;
    char email[LEONOS_LICENSE_EMAIL_LEN];
    char key[LEONOS_LICENSE_KEY_LEN];
    char account[LEONOS_AUTH_USERNAME_LEN];
    char password_input[LEONOS_AUTH_PASSWORD_LEN];
    puts("LeonOS first-run setup (TTY)");
    license = (struct leonos_license_info){0};
    if (leonos_license_required() &&
        (leonos_license_status(&license) < 0 ||
         license.status != LEONOS_LICENSE_STATUS_OK)) {
        if (!tty_read_line("License email: ", email, sizeof(email), 0) ||
            !tty_read_secret("License key: ", key, sizeof(key)) ||
            leonos_license_activate_online(email, key, license_status_text,
                                           sizeof(license_status_text)) != LEONOS_LICENSE_STATUS_OK) {
            puts("License activation failed.");
            return 1;
        }
    }
    if (admin_exists()) {
        (void)write_completion_marker();
        puts("Administrator already exists.");
        return 0;
    }
    copy_text(account, sizeof(account), "root");
    if (!tty_read_line("Administrator name [root]: ", account, sizeof(account), 0) ||
        !account[0]) {
        copy_text(account, sizeof(account), "root");
    }
    {
        uint32_t i;
        for (i = 0; account[i]; ++i) {
            if (!((account[i] >= 'a' && account[i] <= 'z') ||
                  (account[i] >= '0' && account[i] <= '9') || account[i] == '_')) {
                break;
            }
        }
        if (!account[0] || account[i]) {
            puts("Invalid administrator name.");
            return 1;
        }
    }
    if (!tty_read_secret("Administrator password: ", password_input,
                         sizeof(password_input)) || !password_input[0]) {
        puts("Password is required.");
        return 1;
    }
    if (leonos_auth_create_user(account, password_input,
                                LEONOS_AUTH_ROLE_ADMIN, &user) < 0 ||
        leonos_auth_login(account, password_input, &user) < 0 ||
        write_completion_marker() < 0) {
        puts("Administrator setup failed.");
        return 1;
    }
    puts("Administrator created. Setup complete.");
    return 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    if (isatty(STDIN_FILENO)) {
        return tty_oobe_main();
    }
    OOBE_LOG_LINE("[oobe.elf] starting first-run setup");
    license_ready = (uint8_t)license_is_valid();
    if (!leonos_license_required()) {
        copy_text(license_status_text, sizeof(license_status_text),
                  T("License validation is disabled for this build.",
                    "此构建已关闭许可证验证。"));
        copy_text(status_text, sizeof(status_text),
                  T("Create the first administrator.",
                    "请创建第一个管理员。"));
    }
    if (license_ready && admin_exists()) {
        (void)write_completion_marker();
        return 0;
    }
    current_page = license_ready ? OOBE_PAGE_ADMIN : OOBE_PAGE_LICENSE;
    if (current_page == OOBE_PAGE_ADMIN) {
        active_admin_field = 1;
    }
    update_surface_size_from_framebuffer();
    leonos_ui_edit_state_init(&username_edit, username, sizeof(username));
    leonos_ui_edit_state_init(&password_edit, password, sizeof(password));
    leonos_ui_edit_state_init(&online_email_edit, online_email, sizeof(online_email));
    leonos_ui_edit_state_init(&online_key_edit, online_key, sizeof(online_key));
    leonos_ui_edit_state_init(&offline_email_edit, offline_email, sizeof(offline_email));
    leonos_ui_edit_state_init(&offline_key_edit, offline_key, sizeof(offline_key));
    window_id = leonos_gui_create_app_window_ex("LeonOS Setup", "First-run setup",
                                                surface_w, surface_h,
                                                LEONOS_GUI_WINDOW_FULLSCREEN);
    if (window_id <= 0) {
        OOBE_LOG("[oobe.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, surface_w, surface_h, OOBE_MAX_W);
    for (;;) {
        draw_oobe(&ui);
        leonos_gui_present_window((uint32_t)window_id, surface_w, surface_h,
                                  OOBE_MAX_W, pixels);
        oobe_update_inputm_context((uint32_t)window_id);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, 20U) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                update_surface_size(event.width, event.height);
                leonos_ui_bind(&ui, pixels, surface_w, surface_h, OOBE_MAX_W);
                if (current_page == OOBE_PAGE_LICENSE_BROWSER) {
                    browser_embed_resize(surface_w, surface_h);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (current_page == OOBE_PAGE_LICENSE_BROWSER) {
                    if (event.pressed && event.keycode == OOBE_KEY_ESCAPE &&
                        !browser_embed_input_active()) {
                        close_license_browser();
                    } else {
                        browser_embed_handle_key(&event);
                    }
                    if (browser_embed_should_exit()) {
                        close_license_browser();
                    }
                    continue;
                }
                if (event.pressed) {
                    if (handle_key_down(event.keycode)) {
                        break;
                    }
                } else {
                    handle_key_up(event.keycode);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (current_page == OOBE_PAGE_LICENSE_BROWSER) {
                    browser_embed_handle_mouse_wheel(&event);
                    if (browser_embed_should_exit()) {
                        close_license_browser();
                    }
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                current_page == OOBE_PAGE_LICENSE_BROWSER) {
                browser_embed_handle_mouse_button(&event);
                if (browser_embed_should_exit()) {
                    close_license_browser();
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (current_page == OOBE_PAGE_LICENSE) {
                    if (handle_license_click(event.x, event.y)) {
                        break;
                    }
                } else if (handle_admin_click(event.x, event.y)) {
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
