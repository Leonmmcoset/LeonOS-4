#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/license.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define SETTINGS_W 720
#define SETTINGS_H 470
#define SETTINGS_DROPDOWN_ROW_H 28
#define SETTINGS_MODE_COUNT 5
#define SETTINGS_SCALE_COUNT 3
#define SETTINGS_TAB_COUNT 5
#define SETTINGS_USER_ROWS 7
#define SETTINGS_ASSOC_ROWS 6
#define SETTINGS_SERVICE_ROWS 5
#define SETTINGS_TAB_Y 14
#define SETTINGS_BODY_Y 44
#define SETTINGS_SERVICES_PATH "0:/etc/services.cfg"
#define SETTINGS_SERVICES_STATE_PATH "0:/var/run/services.state"
#define SETTINGS_SERVICES_CONFIG_MAX 512U
#define SETTINGS_KEY_ESCAPE 1U
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    PAGE_DISPLAY = 0,
    PAGE_USERS = 1,
    PAGE_ASSOC = 2,
    PAGE_SERVICES = 3,
    PAGE_ACTIVATION = 4,
};

enum {
    DROP_NONE = 0,
    DROP_RESOLUTION = 1,
    DROP_SCALE = 2,
    DROP_LANGUAGE = 3,
    DROP_THEME = 4,
};

static uint32_t pixels[SETTINGS_W * SETTINGS_H];
static struct leonos_display_state display_state;
static struct leonos_appearance_state appearance_state;
static struct leonos_user_info current_user;
static struct leonos_user_info users[LEONOS_AUTH_MAX_USERS];
static uint32_t user_count;
static uint32_t selected_user;
static uint8_t active_page;
static uint8_t active_drop;
static char status_text[160] = "Ready";
static char ntp_runtime_state[16] = "unknown";
static char ntp_runtime_detail[96] = "runtime state unavailable";

struct assoc_row {
    const char *extension;
    const char *description_en;
    const char *description_zh;
};

struct service_row {
    const char *key;
    const char *name_en;
    const char *name_zh;
    const char *detail_en;
    const char *detail_zh;
    uint8_t enabled;
    uint8_t locked;
};

static const char *mode_labels[SETTINGS_MODE_COUNT] = {
    "1920 x 1080",
    "1600 x 900",
    "1280 x 800",
    "1280 x 720",
    "1024 x 768",
};

static const uint32_t mode_widths[SETTINGS_MODE_COUNT] = {1920, 1600, 1280, 1280, 1024};
static const uint32_t mode_heights[SETTINGS_MODE_COUNT] = {1080, 900, 800, 720, 768};
static const uint32_t scale_values[SETTINGS_SCALE_COUNT] = {1, 2, 3};
static const char *scale_labels[SETTINGS_SCALE_COUNT] = {"1x", "2x", "3x"};
static const struct assoc_row assoc_rows[SETTINGS_ASSOC_ROWS] = {
    {".txt", "Text documents", "文本文档"},
    {".md", "Markdown notes", "Markdown 文档"},
    {".html", "HTML pages", "HTML 页面"},
    {".htm", "HTML pages", "HTML 页面"},
    {".bmp", "Bitmap images", "BMP 图片"},
    {".hlp", "Help files", "帮助文件"},
};
static struct service_row service_rows[SETTINGS_SERVICE_ROWS] = {
    {"desktop", "Desktop window service", "桌面窗口服务",
     "Required system shell. Cannot be disabled here.", "必需的系统外壳，不能在这里禁用。", 1, 1},
    {"dhcp", "DHCP auto connect", "DHCP 自动连接",
     "Boot and service runtime should request an address automatically.",
     "启动和服务运行时自动请求地址。", 1, 0},
    {"network_icon", "Taskbar network icon", "任务栏网络图标",
     "Show network state in the desktop taskbar.", "在桌面任务栏显示网络状态。", 1, 0},
    {"rtc_clock", "RTC taskbar clock", "RTC 任务栏时钟",
     "Show the hardware clock in the taskbar.", "在任务栏显示硬件时钟。", 1, 0},
    {"ntp_sync", "NTP time sync", "NTP 时间同步",
     "Synchronize the system clock through pool.ntp.org.",
     "通过 pool.ntp.org 同步系统时钟。", 0, 0},
};

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

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int text_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static const char *role_label(uint32_t role)
{
    return role == LEONOS_AUTH_ROLE_ADMIN ? T("Administrator", "管理员") : T("User", "用户");
}

static const char *program_label(const char *program)
{
    if (text_eq(program, "0:/userland/browser.elf")) {
        return T("Browser", "浏览器");
    }
    if (text_eq(program, "0:/userland/notepad.elf")) {
        return T("Notepad", "记事本");
    }
    if (text_eq(program, "0:/userland/imageview.elf")) {
        return T("Image Viewer", "图片查看器");
    }
    if (text_eq(program, "0:/userland/oshlp.elf")) {
        return T("Help Viewer", "帮助查看器");
    }
    if (text_eq(program, "0:/userland/terminal.elf")) {
        return T("Terminal", "终端");
    }
    if (text_eq(program, "0:/userland/run.elf")) {
        return T("Run", "运行");
    }
    return program && program[0] ? program : T("None", "无");
}

static const char *license_status_label(uint32_t status)
{
    switch (status) {
    case LEONOS_LICENSE_STATUS_OK:
        return T("Activated", "已激活");
    case LEONOS_LICENSE_STATUS_MISSING:
        return T("Not activated", "未激活");
    case LEONOS_LICENSE_STATUS_INVALID:
        return T("Invalid activation", "激活无效");
    case LEONOS_LICENSE_STATUS_NETWORK:
        return T("Network failure", "网络失败");
    case LEONOS_LICENSE_STATUS_CLOCK:
        return T("Clock failure", "时钟失败");
    case LEONOS_LICENSE_STATUS_DENIED:
        return T("Activation denied", "激活被拒绝");
    default:
        return T("Unknown", "未知");
    }
}

static const char *license_mode_label(const char *mode)
{
    if (text_eq(mode, "online")) {
        return T("Online", "在线");
    }
    if (text_eq(mode, "offline")) {
        return T("Offline", "离线");
    }
    return T("None", "无");
}

static const char *value_or_dash(const char *value)
{
    return value && value[0] ? value : "-";
}

static void draw_field(struct leonos_ui_surface *ui, int32_t y,
                       const char *label, const char *value)
{
    leonos_ui_text(ui, 50, y, label, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 176, y, SETTINGS_W - 226,
                           value_or_dash(value), LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static void fake_path_for_extension(char *dst, uint32_t cap, const char *ext)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, "0:/sample");
    append_text(dst, &pos, cap, ext);
}

static int service_line_matches(const char *line, uint32_t len,
                                const char *key, uint8_t *value)
{
    uint32_t key_len = text_len(key);
    uint32_t pos = 0;
    if (!line || !key || !value || len <= key_len || line[key_len] != '=') {
        return 0;
    }
    while (pos < key_len) {
        if (line[pos] != key[pos]) {
            return 0;
        }
        ++pos;
    }
    *value = line[key_len + 1U] == '1' ||
             line[key_len + 1U] == 'y' ||
             line[key_len + 1U] == 'Y';
    return 1;
}

static void load_services_config(void)
{
    char cfg[SETTINGS_SERVICES_CONFIG_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    int fd = open(SETTINGS_SERVICES_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    while (len + 1U < sizeof(cfg)) {
        long got = read(fd, cfg + len, sizeof(cfg) - len - 1U);
        if (got < 0) {
            close(fd);
            return;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    cfg[len] = 0;
    while (pos < len) {
        uint32_t start = pos;
        uint32_t line_len;
        while (pos < len && cfg[pos] != '\n' && cfg[pos] != '\r') {
            ++pos;
        }
        line_len = pos - start;
        while (pos < len && (cfg[pos] == '\n' || cfg[pos] == '\r')) {
            ++pos;
        }
        for (uint32_t i = 0; i < SETTINGS_SERVICE_ROWS; ++i) {
            uint8_t value = 0;
            if (!service_rows[i].locked &&
                service_line_matches(cfg + start, line_len,
                                     service_rows[i].key, &value)) {
                service_rows[i].enabled = value;
            }
        }
    }
}

static void refresh_ntp_runtime_state(void)
{
    char state[1024];
    uint32_t len = 0;
    uint32_t pos = 0;
    int fd = open(SETTINGS_SERVICES_STATE_PATH, LEONOS_O_RDONLY, 0);
    copy_text(ntp_runtime_state, sizeof(ntp_runtime_state), "unknown");
    copy_text(ntp_runtime_detail, sizeof(ntp_runtime_detail),
              T("runtime state unavailable", "运行状态不可用"));
    if (fd < 0) {
        return;
    }
    while (len + 1U < sizeof(state)) {
        long got = read(fd, state + len, sizeof(state) - len - 1U);
        if (got <= 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    state[len] = 0;
    while (pos < len) {
        uint32_t start = pos;
        uint32_t first = len;
        uint32_t second = len;
        uint32_t third = len;
        while (pos < len && state[pos] != '\n' && state[pos] != '\r') {
            if (state[pos] == '|') {
                if (first == len) {
                    first = pos;
                } else if (second == len) {
                    second = pos;
                } else if (third == len) {
                    third = pos;
                }
            }
            ++pos;
        }
        if (first > start && second < len && third < len &&
            first - start == 8U && state[start] == 'n' && state[start + 1U] == 't' &&
            state[start + 2U] == 'p' && state[start + 3U] == '_' &&
            state[start + 4U] == 's' && state[start + 5U] == 'y' &&
            state[start + 6U] == 'n' && state[start + 7U] == 'c') {
            uint32_t state_len = second - first - 1U;
            uint32_t detail_len = pos - third - 1U;
            if (state_len >= sizeof(ntp_runtime_state)) {
                state_len = sizeof(ntp_runtime_state) - 1U;
            }
            if (detail_len >= sizeof(ntp_runtime_detail)) {
                detail_len = sizeof(ntp_runtime_detail) - 1U;
            }
            for (uint32_t i = 0; i < state_len; ++i) {
                ntp_runtime_state[i] = state[first + 1U + i];
            }
            ntp_runtime_state[state_len] = 0;
            for (uint32_t i = 0; i < detail_len; ++i) {
                ntp_runtime_detail[i] = state[third + 1U + i];
            }
            ntp_runtime_detail[detail_len] = 0;
            return;
        }
        while (pos < len && (state[pos] == '\n' || state[pos] == '\r')) {
            ++pos;
        }
    }
}

static void save_services_config(void)
{
    char cfg[SETTINGS_SERVICES_CONFIG_MAX];
    uint32_t pos = 0;
    int fd;
    if (current_user.role != LEONOS_AUTH_ROLE_ADMIN) {
        copy_text(status_text, sizeof(status_text),
                  T("Administrator rights required", "需要管理员权限"));
        return;
    }
    cfg[0] = 0;
    append_text(cfg, &pos, sizeof(cfg), "# LeonOS service startup settings\n");
    for (uint32_t i = 0; i < SETTINGS_SERVICE_ROWS; ++i) {
        append_text(cfg, &pos, sizeof(cfg), service_rows[i].key);
        append_char(cfg, &pos, sizeof(cfg), '=');
        append_char(cfg, &pos, sizeof(cfg), service_rows[i].enabled ? '1' : '0');
        append_char(cfg, &pos, sizeof(cfg), '\n');
    }
    fd = open(SETTINGS_SERVICES_PATH,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save services.", "无法保存服务设置。"));
        return;
    }
    if (write(fd, cfg, pos) < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save services.", "无法保存服务设置。"));
    } else {
        copy_text(status_text, sizeof(status_text),
                  T("Service settings saved", "服务设置已保存"));
    }
    close(fd);
}

static int mode_supported(uint32_t mode, uint32_t scale_index)
{
    uint32_t scale;
    if (mode >= SETTINGS_MODE_COUNT || scale_index >= SETTINGS_SCALE_COUNT) {
        return 0;
    }
    scale = scale_values[scale_index];
    return mode_widths[mode] * scale <= display_state.fb_width &&
           mode_heights[mode] * scale <= display_state.fb_height;
}

static void refresh_display_state(void)
{
    if (leonos_display_get_state(&display_state) <= 0) {
        display_state.fb_width = 1920;
        display_state.fb_height = 1080;
        display_state.logical_width = 1280;
        display_state.logical_height = 800;
        display_state.scale = 1;
        display_state.mode_index = 2;
        display_state.scale_index = 0;
        display_state.pending_confirm = 0;
        display_state.confirm_remaining_ms = 0;
    }
}

static void refresh_appearance_state(void)
{
    if (leonos_appearance_get_state(&appearance_state) <= 0) {
        appearance_state.theme = leonos_ui_theme();
    }
}

static void refresh_users(void)
{
    uint32_t count = 0;
    current_user = (struct leonos_user_info){0};
    (void)leonos_auth_current(&current_user);
    if (current_user.role == LEONOS_AUTH_ROLE_ADMIN) {
        (void)leonos_auth_list_users(users, LEONOS_AUTH_MAX_USERS, 1, &count);
    } else if (current_user.uid) {
        users[0] = current_user;
        count = 1;
    }
    user_count = count > LEONOS_AUTH_MAX_USERS ? LEONOS_AUTH_MAX_USERS : count;
    if (selected_user >= user_count) {
        selected_user = user_count ? user_count - 1 : 0;
    }
}

static void request_display(uint32_t action, uint32_t mode, uint32_t scale)
{
    struct leonos_display_request request;
    request.action = action;
    request.mode_index = mode;
    request.scale_index = scale;
    (void)leonos_display_request(&request);
}

static const char *mode_label(void)
{
    return display_state.mode_index < SETTINGS_MODE_COUNT
               ? mode_labels[display_state.mode_index]
               : mode_labels[0];
}

static const char *scale_label(void)
{
    return display_state.scale_index < SETTINGS_SCALE_COUNT
               ? scale_labels[display_state.scale_index]
               : scale_labels[0];
}

static const char *language_label(void)
{
    return leonos_i18n_language() == LEONOS_LANG_ZH ? "中文" : "English";
}

static const char *theme_label(void)
{
    return appearance_state.theme == LEONOS_UI_THEME_WIN95 ? "Win95" : "Metro";
}

static void draw_display_page(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    struct leonos_ui_dropdown_item mode_items[SETTINGS_MODE_COUNT];
    struct leonos_ui_dropdown_item scale_items[SETTINGS_SCALE_COUNT];
    struct leonos_ui_dropdown_item lang_items[2];
    struct leonos_ui_dropdown_item theme_items[2];
    for (uint32_t i = 0; i < SETTINGS_MODE_COUNT; ++i) {
        mode_items[i].label = mode_labels[i];
        mode_items[i].id = i;
        mode_items[i].flags = mode_supported(i, display_state.scale_index) ? 0 : LEONOS_UI_MENU_DISABLED;
    }
    for (uint32_t i = 0; i < SETTINGS_SCALE_COUNT; ++i) {
        scale_items[i].label = scale_labels[i];
        scale_items[i].id = i;
        scale_items[i].flags = mode_supported(display_state.mode_index, i) ? 0 : LEONOS_UI_MENU_DISABLED;
    }
    lang_items[0] = (struct leonos_ui_dropdown_item){"English", LEONOS_LANG_EN, 0};
    lang_items[1] = (struct leonos_ui_dropdown_item){"中文", LEONOS_LANG_ZH, 0};
    theme_items[0] = (struct leonos_ui_dropdown_item){"Metro", LEONOS_UI_THEME_METRO, 0};
    theme_items[1] = (struct leonos_ui_dropdown_item){"Win95", LEONOS_UI_THEME_WIN95, 0};

    append_text(line, &pos, sizeof(line), T("Framebuffer ", "帧缓冲 "));
    append_dec(line, &pos, sizeof(line), display_state.fb_width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), display_state.fb_height);
    append_text(line, &pos, sizeof(line), "  ");
    append_text(line, &pos, sizeof(line), T("Desktop ", "桌面 "));
    append_dec(line, &pos, sizeof(line), display_state.logical_width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), display_state.logical_height);
    leonos_ui_text_clipped(ui, 34, 64, SETTINGS_W - 68, line, LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 44, 104, T("Resolution", "分辨率"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 98, 190, mode_label(), active_drop == DROP_RESOLUTION, 0);
    leonos_ui_text(ui, 44, 144, T("Scale", "缩放"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 138, 190, scale_label(), active_drop == DROP_SCALE, 0);
    leonos_ui_slider(ui, 370, 138, 150, LEONOS_UI_BUTTON_H,
                     display_state.scale_index,
                     SETTINGS_SCALE_COUNT > 1 ? SETTINGS_SCALE_COUNT - 1 : 1,
                     0);
    leonos_ui_text(ui, 44, 184, T("Language", "语言"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 178, 190, language_label(), active_drop == DROP_LANGUAGE, 0);
    leonos_ui_text(ui, 44, 224, T("System style", "系统样式"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 218, 190, theme_label(), active_drop == DROP_THEME,
                        current_user.role == LEONOS_AUTH_ROLE_ADMIN ? 0 : LEONOS_UI_BUTTON_DISABLED);
    if (current_user.role != LEONOS_AUTH_ROLE_ADMIN) {
        leonos_ui_text(ui, 360, 224, T("Administrator only", "仅管理员可更改"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    if (display_state.pending_confirm) {
        uint32_t seconds = (display_state.confirm_remaining_ms + 999) / 1000;
        pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), T("Keep these display settings? Reverting in ",
                                                "保留这些显示设置？将在 "));
        append_dec(line, &pos, sizeof(line), seconds);
        append_text(line, &pos, sizeof(line), T("s", " 秒后还原"));
        leonos_ui_panel(ui, 44, 272, SETTINGS_W - 88, 64, LEONOS_UI_LIGHT);
        leonos_ui_text_clipped(ui, 54, 284, SETTINGS_W - 108, line, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
        leonos_ui_button(ui, 54, 306, 82, LEONOS_UI_BUTTON_H, T("Keep", "保留"), 0);
        leonos_ui_button(ui, 146, 306, 82, LEONOS_UI_BUTTON_H, T("Revert", "还原"), 0);
    }
    if (active_drop == DROP_LANGUAGE) {
        leonos_ui_dropdown(ui, 160, 202, 190, lang_items, 2,
                           (uint32_t)leonos_i18n_language(), SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_SCALE) {
        leonos_ui_dropdown(ui, 160, 162, 190, scale_items, SETTINGS_SCALE_COUNT,
                           display_state.scale_index, SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_RESOLUTION) {
        leonos_ui_dropdown(ui, 160, 122, 190, mode_items, SETTINGS_MODE_COUNT,
                           display_state.mode_index, SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_THEME) {
        leonos_ui_dropdown(ui, 160, 242, 190, theme_items, 2,
                           appearance_state.theme, SETTINGS_DROPDOWN_ROW_H, 1000);
    }
}

static void draw_users_page(struct leonos_ui_surface *ui)
{
    const struct leonos_ui_list_column cols[] = {
        {T("User", "用户"), 180},
        {T("Role", "权限"), 130},
        {T("State", "状态"), 120},
    };
    char state[32];
    leonos_ui_text(ui, 34, 64,
                   current_user.role == LEONOS_AUTH_ROLE_ADMIN
                       ? T("Administrators can create and manage local accounts.", "管理员可以创建和管理本地账户。")
                       : T("You can change your password.", "你可以修改自己的密码。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 34, 98, 430, cols, 3);
    for (uint32_t i = 0; i < user_count && i < SETTINGS_USER_ROWS; ++i) {
        const char *cells[3];
        copy_text(state, sizeof(state),
                  (users[i].flags & LEONOS_AUTH_USER_DISABLED)
                      ? T("Disabled", "已禁用")
                      : T("Enabled", "已启用"));
        cells[0] = users[i].username;
        cells[1] = role_label(users[i].role);
        cells[2] = state;
        leonos_ui_listview_row(ui, 34, 126 + i * 28, 430, cols, cells, 3,
                               i == selected_user ? LEONOS_UI_MENU_SELECTED : 0);
    }
    if (current_user.role == LEONOS_AUTH_ROLE_ADMIN) {
        leonos_ui_button(ui, 484, 98, 104, LEONOS_UI_BUTTON_H, T("New User", "新建用户"), 0);
        leonos_ui_button(ui, 484, 130, 104, LEONOS_UI_BUTTON_H, T("New Admin", "新建管理员"), 0);
        leonos_ui_button(ui, 484, 178, 104, LEONOS_UI_BUTTON_H,
                         users[selected_user].flags & LEONOS_AUTH_USER_DISABLED
                             ? T("Enable", "启用")
                             : T("Disable", "禁用"),
                         user_count ? 0 : LEONOS_UI_BUTTON_DISABLED);
        leonos_ui_button(ui, 484, 210, 104, LEONOS_UI_BUTTON_H,
                         users[selected_user].role == LEONOS_AUTH_ROLE_ADMIN
                             ? T("Make User", "改为用户")
                             : T("Make Admin", "改为管理员"),
                         user_count ? 0 : LEONOS_UI_BUTTON_DISABLED);
        leonos_ui_button(ui, 484, 258, 104, LEONOS_UI_BUTTON_H, T("Reset Pass", "重置密码"),
                         user_count ? 0 : LEONOS_UI_BUTTON_DISABLED);
    }
    leonos_ui_button(ui, 34, 324, 150, LEONOS_UI_BUTTON_H, T("Change Password", "修改密码"),
                     current_user.uid ? 0 : LEONOS_UI_BUTTON_DISABLED);
}

static void draw_assoc_page(struct leonos_ui_surface *ui)
{
    const struct leonos_ui_list_column cols[] = {
        {T("Extension", "扩展名"), 82},
        {T("Type", "类型"), 170},
        {T("Default app", "默认应用"), 160},
    };
    leonos_ui_text(ui, 34, 64,
                   T("Choose the default app used by File Manager and desktop shortcuts.",
                     "选择文件资源管理器和桌面快捷方式打开文件时使用的默认应用。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 34, 96, 412, cols, 3);
    for (uint32_t i = 0; i < SETTINGS_ASSOC_ROWS; ++i) {
        char fake[LEONOS_FS_PATH_LEN];
        const char *program;
        const char *cells[3];
        uint32_t y = 124 + i * 40;
        fake_path_for_extension(fake, sizeof(fake), assoc_rows[i].extension);
        program = leonos_launch_resolve_default_app_for_path(fake);
        cells[0] = assoc_rows[i].extension;
        cells[1] = T(assoc_rows[i].description_en, assoc_rows[i].description_zh);
        cells[2] = program_label(program);
        leonos_ui_listview_row(ui, 34, y, 412, cols, cells, 3, 0);
        leonos_ui_button(ui, 450, y + 2, 64, LEONOS_UI_BUTTON_H,
                         T("Browser", "浏览器"),
                         text_eq(program, "0:/userland/browser.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 520, y + 2, 70, LEONOS_UI_BUTTON_H,
                         T("Notepad", "记事本"),
                         text_eq(program, "0:/userland/notepad.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 596, y + 2, 58, LEONOS_UI_BUTTON_H,
                         T("Image", "图片"),
                         text_eq(program, "0:/userland/imageview.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 660, y + 2, 50, LEONOS_UI_BUTTON_H,
                         T("Help", "帮助"),
                         text_eq(program, "0:/userland/oshlp.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
    }
}

static void draw_services_page(struct leonos_ui_surface *ui)
{
    leonos_ui_text(ui, 34, 64,
                   T("Startup policy is saved here; Service Manager shows runtime state.",
                     "这里保存启动策略；服务管理器显示运行状态。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < SETTINGS_SERVICE_ROWS; ++i) {
        uint32_t y = 98 + i * 48;
        uint32_t flags = (service_rows[i].locked ||
                          current_user.role != LEONOS_AUTH_ROLE_ADMIN)
                             ? LEONOS_UI_BUTTON_DISABLED
                             : 0;
        leonos_ui_panel(ui, 34, y, SETTINGS_W - 68, 40, LEONOS_UI_WHITE);
        leonos_ui_checkbox(ui, 44, y + 9,
                           T(service_rows[i].name_en, service_rows[i].name_zh),
                           service_rows[i].enabled, flags);
        leonos_ui_text_clipped(ui, 274, y + 11, SETTINGS_W - 318,
                               i == 4U ? ntp_runtime_detail :
                                         T(service_rows[i].detail_en, service_rows[i].detail_zh),
                               service_rows[i].locked ? LEONOS_UI_DARK : LEONOS_UI_BLACK,
                               LEONOS_UI_WHITE);
    }
    leonos_ui_text(ui, 34, 346, T("NTP runtime state:", "NTP 运行状态:"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text_clipped(ui, 174, 346, SETTINGS_W - 208, ntp_runtime_state,
                           text_eq(ntp_runtime_state, "failed") ? 0x00b03030U :
                           text_eq(ntp_runtime_state, "running") ? 0x00108040U :
                           LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_button(ui, 34, SETTINGS_H - 66, 108, LEONOS_UI_BUTTON_H,
                     T("Save", "保存"),
                     current_user.role == LEONOS_AUTH_ROLE_ADMIN
                         ? 0
                         : LEONOS_UI_BUTTON_DISABLED);
}

static void draw_activation_page(struct leonos_ui_surface *ui)
{
    struct leonos_license_info info;
    const char *status;
    uint32_t required;
    uint32_t ok;
    required = (uint32_t)leonos_license_required();
    if (!required) {
        info = (struct leonos_license_info){0};
        copy_text(info.detail, sizeof(info.detail), T("License validation is disabled for this build.",
                                                      "此构建已关闭许可证验证。"));
        status = T("Not required", "不需要验证");
        ok = 1;
    } else if (leonos_license_status(&info) < 0) {
        info = (struct leonos_license_info){0};
        info.status = LEONOS_LICENSE_STATUS_INVALID;
        copy_text(info.detail, sizeof(info.detail), "license status unavailable");
        status = license_status_label(info.status);
        ok = 0;
    } else {
        status = license_status_label(info.status);
        ok = info.status == LEONOS_LICENSE_STATUS_OK;
    }
    leonos_ui_text(ui, 34, 64, T("Activation", "激活"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_panel(ui, 34, 94, SETTINGS_W - 68, 52,
                    ok ? LEONOS_UI_WHITE : LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 50, 110, T("Computer", "此计算机"),
                   LEONOS_UI_BLACK, ok ? LEONOS_UI_WHITE : LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, 176, 110, SETTINGS_W - 226,
                           status,
                           ok ? LEONOS_UI_BLACK : LEONOS_UI_DARK,
                           ok ? LEONOS_UI_WHITE : LEONOS_UI_LIGHT);
    draw_field(ui, 170, T("Activation mode", "激活方式"),
               !required ? T("Not required", "不需要验证") :
               ok ? license_mode_label(info.mode) : "-");
    draw_field(ui, 202, T("Machine ID", "机器码"), info.install_id);
    draw_field(ui, 234, T("Email hash", "邮箱哈希"), info.email_hash);
    draw_field(ui, 266, T("Detail", "详情"), info.detail);
    draw_field(ui, 298, T("License file", "许可证文件"), "0:/etc/license.dat");
}

static void draw_settings(struct leonos_ui_surface *ui)
{
    const char *tabs[] = {
        T("Display", "显示"),
        T("Users", "用户"),
        T("File Types", "文件类型"),
        T("Services", "服务"),
        T("Activation", "激活"),
    };
    leonos_ui_rect(ui, 0, 0, SETTINGS_W, SETTINGS_H, LEONOS_UI_GRAY);
    leonos_ui_tabs(ui, 18, SETTINGS_TAB_Y, SETTINGS_W - 36, tabs, SETTINGS_TAB_COUNT, active_page);
    leonos_ui_tab_body(ui, 18, SETTINGS_BODY_Y, SETTINGS_W - 36, SETTINGS_H - 84);
    if (active_page == PAGE_DISPLAY) {
        draw_display_page(ui);
    } else if (active_page == PAGE_USERS) {
        draw_users_page(ui);
    } else if (active_page == PAGE_ASSOC) {
        draw_assoc_page(ui);
    } else if (active_page == PAGE_SERVICES) {
        draw_services_page(ui);
    } else {
        draw_activation_page(ui);
    }
    leonos_ui_statusbar(ui, SETTINGS_H - 28, 28, status_text);
}

static int handle_open_dropdown_hit(int32_t x, int32_t y)
{
    uint32_t id = 0;
    struct leonos_ui_dropdown_item mode_items[SETTINGS_MODE_COUNT];
    struct leonos_ui_dropdown_item scale_items[SETTINGS_SCALE_COUNT];
    struct leonos_ui_dropdown_item lang_items[2];
    struct leonos_ui_dropdown_item theme_items[2];
    for (uint32_t i = 0; i < SETTINGS_MODE_COUNT; ++i) {
        mode_items[i].label = mode_labels[i];
        mode_items[i].id = i;
        mode_items[i].flags = mode_supported(i, display_state.scale_index) ? 0 : LEONOS_UI_MENU_DISABLED;
    }
    for (uint32_t i = 0; i < SETTINGS_SCALE_COUNT; ++i) {
        scale_items[i].label = scale_labels[i];
        scale_items[i].id = i;
        scale_items[i].flags = mode_supported(display_state.mode_index, i) ? 0 : LEONOS_UI_MENU_DISABLED;
    }
    lang_items[0] = (struct leonos_ui_dropdown_item){"English", LEONOS_LANG_EN, 0};
    lang_items[1] = (struct leonos_ui_dropdown_item){"中文", LEONOS_LANG_ZH, 0};
    theme_items[0] = (struct leonos_ui_dropdown_item){"Metro", LEONOS_UI_THEME_METRO, 0};
    theme_items[1] = (struct leonos_ui_dropdown_item){"Win95", LEONOS_UI_THEME_WIN95, 0};
    if (active_drop == DROP_RESOLUTION &&
        leonos_ui_dropdown_hit(x, y, 160, 122, 190, mode_items, SETTINGS_MODE_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id < SETTINGS_MODE_COUNT && mode_supported(id, display_state.scale_index)) {
            request_display(LEONOS_DISPLAY_REQUEST_APPLY, id, display_state.scale_index);
            copy_text(status_text, sizeof(status_text), T("Resolution changed", "分辨率已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_SCALE &&
        leonos_ui_dropdown_hit(x, y, 160, 162, 190, scale_items, SETTINGS_SCALE_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id < SETTINGS_SCALE_COUNT && mode_supported(display_state.mode_index, id)) {
            request_display(LEONOS_DISPLAY_REQUEST_APPLY, display_state.mode_index, id);
            copy_text(status_text, sizeof(status_text), T("Scale changed", "缩放已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_LANGUAGE &&
        leonos_ui_dropdown_hit(x, y, 160, 202, 190, lang_items, 2,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (id == LEONOS_LANG_EN || id == LEONOS_LANG_ZH) {
            (void)leonos_i18n_set_language((int)id);
            request_display(LEONOS_DISPLAY_REQUEST_REFRESH, display_state.mode_index,
                            display_state.scale_index);
            copy_text(status_text, sizeof(status_text), T("Language changed", "语言已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_THEME &&
        leonos_ui_dropdown_hit(x, y, 160, 242, 190, theme_items, 2,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (current_user.role == LEONOS_AUTH_ROLE_ADMIN &&
            (id == LEONOS_UI_THEME_METRO || id == LEONOS_UI_THEME_WIN95)) {
            struct leonos_appearance_request request = {.theme = id};
            if (leonos_appearance_request_theme(&request) > 0) {
                leonos_ui_theme_set(id);
                appearance_state.theme = id;
                copy_text(status_text, sizeof(status_text), T("System style changed", "系统样式已更改"));
            } else {
                copy_text(status_text, sizeof(status_text), T("Could not change system style", "无法更改系统样式"));
            }
        }
        return 1;
    }
    return 0;
}

static void create_user_dialog(uint32_t role)
{
    char name[LEONOS_AUTH_USERNAME_LEN] = "";
    char pass[LEONOS_AUTH_PASSWORD_LEN] = "";
    struct leonos_user_info user;
    if (leonos_ui_show_input_dialog(T("Create user", "创建用户"), T("Username", "用户名"),
                                    name, sizeof(name)) <= 0) {
        return;
    }
    if (leonos_ui_show_input_dialog(T("Create user", "创建用户"), T("Password", "密码"),
                                    pass, sizeof(pass)) <= 0) {
        return;
    }
    if (leonos_auth_create_user(name, pass, role, &user) == 0) {
        copy_text(status_text, sizeof(status_text), T("User created", "用户已创建"));
        refresh_users();
    } else {
        copy_text(status_text, sizeof(status_text), T("Could not create user", "无法创建用户"));
    }
}

static void reset_password_dialog(uint32_t uid)
{
    char pass[LEONOS_AUTH_PASSWORD_LEN] = "";
    if (leonos_ui_show_input_dialog(T("Reset password", "重置密码"), T("New password", "新密码"),
                                    pass, sizeof(pass)) <= 0) {
        return;
    }
    if (leonos_auth_change_password(uid, "", pass) == 0) {
        copy_text(status_text, sizeof(status_text), T("Password reset", "密码已重置"));
    } else {
        copy_text(status_text, sizeof(status_text), T("Password reset failed", "重置密码失败"));
    }
}

static void change_my_password(void)
{
    char old_pass[LEONOS_AUTH_PASSWORD_LEN] = "";
    char new_pass[LEONOS_AUTH_PASSWORD_LEN] = "";
    if (!current_user.uid) {
        return;
    }
    if (leonos_ui_show_input_dialog(T("Change password", "修改密码"), T("Old password", "旧密码"),
                                    old_pass, sizeof(old_pass)) <= 0) {
        return;
    }
    if (leonos_ui_show_input_dialog(T("Change password", "修改密码"), T("New password", "新密码"),
                                    new_pass, sizeof(new_pass)) <= 0) {
        return;
    }
    if (leonos_auth_change_password(current_user.uid, old_pass, new_pass) == 0) {
        copy_text(status_text, sizeof(status_text), T("Password changed", "密码已修改"));
    } else {
        copy_text(status_text, sizeof(status_text), T("Password change failed", "修改密码失败"));
    }
}

static void handle_users_click(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < user_count && i < SETTINGS_USER_ROWS; ++i) {
        if (hit_rect_i(x, y, 34, 126 + (int32_t)i * 28, 430, 28)) {
            selected_user = i;
            return;
        }
    }
    if (current_user.role == LEONOS_AUTH_ROLE_ADMIN) {
        if (hit_rect_i(x, y, 484, 98, 104, LEONOS_UI_BUTTON_H)) {
            create_user_dialog(LEONOS_AUTH_ROLE_USER);
        } else if (hit_rect_i(x, y, 484, 130, 104, LEONOS_UI_BUTTON_H)) {
            create_user_dialog(LEONOS_AUTH_ROLE_ADMIN);
        } else if (user_count && hit_rect_i(x, y, 484, 178, 104, LEONOS_UI_BUTTON_H)) {
            uint32_t flags = users[selected_user].flags ^ LEONOS_AUTH_USER_DISABLED;
            if (leonos_auth_update_user(users[selected_user].uid, LEONOS_AUTH_UPDATE_FLAGS,
                                        users[selected_user].role, flags) == 0) {
                copy_text(status_text, sizeof(status_text), T("User state updated", "用户状态已更新"));
            } else {
                copy_text(status_text, sizeof(status_text), T("User state change denied", "用户状态更改被拒绝"));
            }
            refresh_users();
        } else if (user_count && hit_rect_i(x, y, 484, 210, 104, LEONOS_UI_BUTTON_H)) {
            uint32_t role = users[selected_user].role == LEONOS_AUTH_ROLE_ADMIN
                                ? LEONOS_AUTH_ROLE_USER
                                : LEONOS_AUTH_ROLE_ADMIN;
            if (leonos_auth_update_user(users[selected_user].uid, LEONOS_AUTH_UPDATE_ROLE,
                                        role, users[selected_user].flags) == 0) {
                copy_text(status_text, sizeof(status_text), T("Role updated", "权限已更新"));
            } else {
                copy_text(status_text, sizeof(status_text), T("Role change denied", "权限更改被拒绝"));
            }
            refresh_users();
        } else if (user_count && hit_rect_i(x, y, 484, 258, 104, LEONOS_UI_BUTTON_H)) {
            reset_password_dialog(users[selected_user].uid);
        }
    }
    if (hit_rect_i(x, y, 34, 324, 150, LEONOS_UI_BUTTON_H)) {
        change_my_password();
    }
}

static void handle_display_click(int32_t x, int32_t y)
{
    if (active_drop && handle_open_dropdown_hit(x, y)) {
        return;
    }
    active_drop = DROP_NONE;
    if (hit_rect_i(x, y, 160, 98, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_RESOLUTION;
        return;
    }
    if (hit_rect_i(x, y, 160, 138, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_SCALE;
        return;
    }
    if (hit_rect_i(x, y, 370, 138, 150, LEONOS_UI_BUTTON_H)) {
        uint32_t next = display_state.scale_index;
        if (leonos_ui_slider_handle_mouse(&next,
                                          SETTINGS_SCALE_COUNT > 1 ? SETTINGS_SCALE_COUNT - 1 : 1,
                                          370, 138, 150, LEONOS_UI_BUTTON_H,
                                          x, y) &&
            next < SETTINGS_SCALE_COUNT &&
            mode_supported(display_state.mode_index, next)) {
            request_display(LEONOS_DISPLAY_REQUEST_APPLY,
                            display_state.mode_index, next);
            copy_text(status_text, sizeof(status_text), T("Scale changed", "缩放已更改"));
        }
        return;
    }
    if (hit_rect_i(x, y, 160, 178, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_LANGUAGE;
        return;
    }
    if (current_user.role == LEONOS_AUTH_ROLE_ADMIN &&
        hit_rect_i(x, y, 160, 218, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_THEME;
        return;
    }
    if (display_state.pending_confirm && hit_rect_i(x, y, 54, 306, 82, LEONOS_UI_BUTTON_H)) {
        request_display(LEONOS_DISPLAY_REQUEST_KEEP, display_state.mode_index, display_state.scale_index);
        copy_text(status_text, sizeof(status_text), T("Display settings saved", "显示设置已保存"));
        return;
    }
    if (display_state.pending_confirm && hit_rect_i(x, y, 146, 306, 82, LEONOS_UI_BUTTON_H)) {
        request_display(LEONOS_DISPLAY_REQUEST_REVERT, display_state.mode_index, display_state.scale_index);
        copy_text(status_text, sizeof(status_text), T("Display settings reverted", "显示设置已还原"));
    }
}

static void set_assoc_for_row(uint32_t row, const char *program)
{
    int ret;
    if (row >= SETTINGS_ASSOC_ROWS) {
        return;
    }
    ret = leonos_launch_set_extension_association(assoc_rows[row].extension,
                                                  program);
    if (ret == 0) {
        copy_text(status_text, sizeof(status_text),
                  T("File association saved", "文件关联已保存"));
    } else {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save file association", "无法保存文件关联"));
    }
}

static void handle_assoc_click(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < SETTINGS_ASSOC_ROWS; ++i) {
        int32_t row_y = 124 + (int32_t)i * 40;
        if (hit_rect_i(x, y, 450, row_y + 2, 64, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/userland/browser.elf");
            return;
        }
        if (hit_rect_i(x, y, 520, row_y + 2, 70, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/userland/notepad.elf");
            return;
        }
        if (hit_rect_i(x, y, 596, row_y + 2, 58, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/userland/imageview.elf");
            return;
        }
        if (hit_rect_i(x, y, 660, row_y + 2, 50, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/userland/oshlp.elf");
            return;
        }
    }
}

static void handle_services_click(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < SETTINGS_SERVICE_ROWS; ++i) {
        int32_t row_y = 98 + (int32_t)i * 48;
        if (current_user.role == LEONOS_AUTH_ROLE_ADMIN &&
            !service_rows[i].locked &&
            hit_rect_i(x, y, 44, row_y + 4, 220, 32)) {
            service_rows[i].enabled = service_rows[i].enabled ? 0 : 1;
            copy_text(status_text, sizeof(status_text),
                      T("Service setting changed", "服务设置已更改"));
            return;
        }
    }
    if (hit_rect_i(x, y, 34, SETTINGS_H - 66, 108, LEONOS_UI_BUTTON_H)) {
        save_services_config();
    }
}

static void handle_click(int32_t x, int32_t y)
{
    const char *tabs[] = {
        T("Display", "显示"),
        T("Users", "用户"),
        T("File Types", "文件类型"),
        T("Services", "服务"),
        T("Activation", "激活"),
    };
    int tab = leonos_ui_tabs_hit(x, y, 18, SETTINGS_TAB_Y, SETTINGS_W - 36, tabs, SETTINGS_TAB_COUNT);
    if (tab >= 0) {
        active_page = (uint8_t)tab;
        active_drop = DROP_NONE;
        return;
    }
    if (active_page == PAGE_DISPLAY) {
        handle_display_click(x, y);
    } else if (active_page == PAGE_USERS) {
        handle_users_click(x, y);
    } else if (active_page == PAGE_ASSOC) {
        handle_assoc_click(x, y);
    } else if (active_page == PAGE_SERVICES) {
        handle_services_click(x, y);
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    unsigned long last_refresh = 0;
    puts("[settings.elf] settings starting");
    window_id = leonos_gui_create_app_window_ex(T("Settings", "设置"),
                                                T("System settings", "系统设置"),
                                                SETTINGS_W, SETTINGS_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[settings.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, SETTINGS_W, SETTINGS_H, SETTINGS_W);
    refresh_display_state();
    refresh_appearance_state();
    refresh_users();
    load_services_config();
    refresh_ntp_runtime_state();
    draw_settings(&ui);
    leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_THEME_CHANGED) {
                (void)leonos_ui_theme_set((uint32_t)event.x);
                appearance_state.theme = (uint32_t)event.x;
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                handle_click(event.x, event.y);
                refresh_display_state();
                refresh_appearance_state();
                refresh_users();
                refresh_ntp_runtime_state();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed &&
                event.keycode == SETTINGS_KEY_ESCAPE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS || event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                refresh_display_state();
                refresh_appearance_state();
                refresh_users();
                refresh_ntp_runtime_state();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
            }
        } else {
            unsigned long now = leonos_uptime_ms();
            if (now - last_refresh >= 250) {
                refresh_display_state();
                refresh_appearance_state();
                refresh_users();
                draw_settings(&ui);
                leonos_gui_present_window((uint32_t)window_id, SETTINGS_W, SETTINGS_H, SETTINGS_W, pixels);
                last_refresh = now;
            }
            sleep_ms(20);
        }
    }
}
