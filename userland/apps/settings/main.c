#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/inputm.h>
#include <leonos/launch.h>
#include <leonos/license.h>
#include <leonos/psf_font.h>
#include <leonos/startup.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define SETTINGS_W 720
#define SETTINGS_H 470
#define SETTINGS_DROPDOWN_ROW_H 28
#define SETTINGS_MODE_COUNT 5
#define SETTINGS_SCALE_COUNT 3
#define SETTINGS_TAB_COUNT 7
#define SETTINGS_USER_ROWS 7
#define SETTINGS_ASSOC_ROWS 6
#define SETTINGS_SERVICE_ROWS 5
#define SETTINGS_WALLPAPER_MAX_W 1280U
#define SETTINGS_WALLPAPER_MAX_H 720U
#define SETTINGS_WALLPAPER_BMP_MAX_BYTES (SETTINGS_WALLPAPER_MAX_W * SETTINGS_WALLPAPER_MAX_H * 4U + 128U)
#define SETTINGS_DEFAULT_WALLPAPER_PATH "0:/system/resources/wallpaper-metro.bmp"
#define SETTINGS_TAB_Y 14
#define SETTINGS_BODY_Y 44
#define SETTINGS_SERVICES_PATH "0:/system/config/services.cfg"
#define SETTINGS_SERVICES_STATE_PATH "0:/var/run/services.state"
#define SETTINGS_SERVICES_CONFIG_MAX 512U
#define SETTINGS_INPUTM_CONFIG_MAX 2048U
#define SETTINGS_INPUTM_ROWS (LEONOS_INPUTM_MAX_PROVIDERS + 1U)
#define SETTINGS_INPUTM_OPTION_COUNT 4U
#define SETTINGS_INPUTM_OPTION_KEY_LEN 64U
#define SETTINGS_INPUTM_OPTION_LABEL_LEN 64U
#define SETTINGS_KEY_ESCAPE 1U
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    PAGE_DISPLAY = 0,
    PAGE_PERSONALIZATION = 1,
    PAGE_USERS = 2,
    PAGE_ASSOC = 3,
    PAGE_SERVICES = 4,
    PAGE_ACTIVATION = 5,
    PAGE_INPUT_METHODS = 6,
};

enum {
    DROP_NONE = 0,
    DROP_RESOLUTION = 1,
    DROP_SCALE = 2,
    DROP_LANGUAGE = 3,
    DROP_THEME = 4,
    DROP_METRO_COLOR = 5,
    DROP_WIN95_COLOR = 6,
    DROP_WALLPAPER_MODE = 7,
    DROP_INPUTM_HOTKEY = 8,
    DROP_INPUTM_STARTUP = 9,
};

static uint32_t pixels[SETTINGS_W * SETTINGS_H];
static struct leonos_display_state display_state;
static struct leonos_fb_capabilities framebuffer_caps;
static struct leonos_appearance_state appearance_state;
static struct leonos_user_info current_user;
static struct leonos_user_info users[LEONOS_AUTH_MAX_USERS];
static uint32_t user_count;
static uint32_t selected_user;
static uint8_t active_page;
static uint8_t active_drop;
static struct leonos_ui_tab_state settings_tabs;
static char status_text[160] = "Ready";
static char ntp_runtime_state[16] = "unknown";
static char ntp_runtime_detail[96] = "runtime state unavailable";

struct settings_inputm_entry {
    char id[LEONOS_INPUTM_ID_LEN];
    char path[LEONOS_FS_PATH_LEN];
    char settings_path[LEONOS_FS_PATH_LEN];
    char settings_app[LEONOS_FS_PATH_LEN];
    uint32_t config_index;
    uint32_t startup_mode;
    uint32_t order;
    uint8_t enabled;
};

struct settings_inputm_option {
    char key[SETTINGS_INPUTM_OPTION_KEY_LEN];
    char label[SETTINGS_INPUTM_OPTION_LABEL_LEN];
    char label_zh[SETTINGS_INPUTM_OPTION_LABEL_LEN];
    uint8_t value;
};

static struct settings_inputm_entry inputm_entries[SETTINGS_INPUTM_ROWS];
static uint32_t inputm_entry_count;
static uint32_t inputm_selected;
static char inputm_default[LEONOS_INPUTM_ID_LEN] = "en";
static char inputm_hotkey[16] = "win-space";
static struct settings_inputm_option inputm_options[SETTINGS_INPUTM_OPTION_COUNT];
static uint32_t inputm_option_count;

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

static uint16_t settings_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t settings_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t settings_read_le32s(const uint8_t *p)
{
    return (int32_t)settings_read_le32(p);
}

static const char *theme_color_label(uint32_t scheme)
{
    switch (scheme) {
    case LEONOS_UI_COLOR_SCHEME_TEAL:
        return T("Teal", "青色");
    case LEONOS_UI_COLOR_SCHEME_GREEN:
        return T("Green", "绿色");
    case LEONOS_UI_COLOR_SCHEME_PURPLE:
        return T("Purple", "紫色");
    case LEONOS_UI_COLOR_SCHEME_RED:
        return T("Red", "红色");
    case LEONOS_UI_COLOR_SCHEME_GRAPHITE:
        return T("Graphite", "石墨色");
    default:
        return T("Blue", "蓝色");
    }
}

static const char *wallpaper_mode_label(uint32_t mode)
{
    switch (mode) {
    case LEONOS_WALLPAPER_MODE_FIT:
        return T("Fit", "适应");
    case LEONOS_WALLPAPER_MODE_CENTER:
        return T("Center", "居中");
    case LEONOS_WALLPAPER_MODE_TILE:
        return T("Tile", "平铺");
    case LEONOS_WALLPAPER_MODE_STRETCH:
        return T("Stretch", "拉伸");
    default:
        return T("Fill", "填充");
    }
}

static void fill_theme_color_items(struct leonos_ui_dropdown_item *items)
{
    if (!items) {
        return;
    }
    for (uint32_t i = 0; i < LEONOS_UI_COLOR_SCHEME_COUNT; ++i) {
        items[i].label = theme_color_label(i);
        items[i].id = i;
        items[i].flags = 0;
    }
}

static void fill_wallpaper_mode_items(struct leonos_ui_dropdown_item *items)
{
    if (!items) {
        return;
    }
    for (uint32_t i = 0; i < LEONOS_WALLPAPER_MODE_COUNT; ++i) {
        items[i].label = wallpaper_mode_label(i);
        items[i].id = i;
        items[i].flags = 0;
    }
}

static int validate_wallpaper_bmp(const char *path)
{
    uint8_t header[54];
    struct leonos_stat st;
    int fd;
    long got;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t height_signed;
    uint32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t row_stride;
    if (!path || !path[0]) {
        return 0;
    }
    if (stat(path, &st) < 0 || st.type != LEONOS_FS_TYPE_FILE ||
        st.size < sizeof(header) || st.size > SETTINGS_WALLPAPER_BMP_MAX_BYTES) {
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    got = read(fd, header, sizeof(header));
    close(fd);
    if (got < (long)sizeof(header) || header[0] != 'B' || header[1] != 'M') {
        return 0;
    }
    pixel_offset = settings_read_le32(header + 10);
    dib_size = settings_read_le32(header + 14);
    width = settings_read_le32s(header + 18);
    height_signed = settings_read_le32s(header + 22);
    planes = settings_read_le16(header + 26);
    bpp = settings_read_le16(header + 28);
    compression = settings_read_le32(header + 30);
    if (dib_size < 40 || width <= 0 || height_signed == 0 ||
        planes != 1 || (bpp != 24 && bpp != 32) || compression != 0) {
        return 0;
    }
    height = height_signed < 0
                 ? (uint32_t)(0u - (uint32_t)height_signed)
                 : (uint32_t)height_signed;
    if ((uint32_t)width > SETTINGS_WALLPAPER_MAX_W ||
        height > SETTINGS_WALLPAPER_MAX_H) {
        return 0;
    }
    row_stride = ((((uint32_t)width * bpp) + 31u) / 32u) * 4u;
    if ((uint64_t)pixel_offset + (uint64_t)row_stride * height > st.size) {
        return 0;
    }
    return 1;
}

static const char *role_label(uint32_t role)
{
    return role == LEONOS_AUTH_ROLE_ADMIN ? T("Administrator", "管理员") : T("User", "用户");
}

static const char *program_label(const char *program)
{
    if (text_eq(program, "0:/programs/browser/browser.elf")) {
        return T("Browser", "浏览器");
    }
    if (text_eq(program, "0:/programs/notepad/notepad.elf")) {
        return T("Notepad", "记事本");
    }
    if (text_eq(program, "0:/programs/imageview/imageview.elf")) {
        return T("Image Viewer", "图片查看器");
    }
    if (text_eq(program, "0:/programs/oshlp/oshlp.elf")) {
        return T("Help Viewer", "帮助查看器");
    }
    if (text_eq(program, "0:/system/apps/terminal/terminal.elf")) {
        return T("Terminal", "终端");
    }
    if (text_eq(program, "0:/system/apps/run/run.elf")) {
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
    uint64_t physical_width;
    uint64_t physical_height;
    uint64_t required_bytes;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t max_bytes;
    if (mode >= SETTINGS_MODE_COUNT || scale_index >= SETTINGS_SCALE_COUNT) {
        return 0;
    }
    scale = scale_values[scale_index];
    physical_width = (uint64_t)mode_widths[mode] * scale;
    physical_height = (uint64_t)mode_heights[mode] * scale;
    required_bytes = physical_width * physical_height * sizeof(uint32_t);
    max_width = framebuffer_caps.max_width ? framebuffer_caps.max_width : display_state.fb_width;
    max_height = framebuffer_caps.max_height ? framebuffer_caps.max_height : display_state.fb_height;
    max_bytes = framebuffer_caps.max_bytes
                    ? framebuffer_caps.max_bytes
                    : display_state.fb_width * display_state.fb_height * sizeof(uint32_t);
    return physical_width <= max_width && physical_height <= max_height &&
           required_bytes <= max_bytes;
}

static void refresh_display_state(void)
{
    int state_available = leonos_display_get_state(&display_state) > 0;
    if (leonos_fb_capabilities(&framebuffer_caps) < 0) {
        framebuffer_caps.bytes_per_pixel = 4;
        framebuffer_caps.capabilities = 0;
        framebuffer_caps.max_width = state_available ? display_state.fb_width : 1920;
        framebuffer_caps.max_height = state_available ? display_state.fb_height : 1080;
        framebuffer_caps.max_bytes = framebuffer_caps.max_width * framebuffer_caps.max_height *
                                    sizeof(uint32_t);
        framebuffer_caps.backend = LEONOS_FB_BACKEND_BOOT;
    }
    if (!state_available) {
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
        appearance_state.metro_color_scheme =
            leonos_ui_theme_color_scheme(LEONOS_UI_THEME_METRO);
        appearance_state.win95_color_scheme =
            leonos_ui_theme_color_scheme(LEONOS_UI_THEME_WIN95);
        appearance_state.wallpaper_mode = LEONOS_WALLPAPER_MODE_FILL;
        copy_text(appearance_state.wallpaper_path,
                  sizeof(appearance_state.wallpaper_path),
                  SETTINGS_DEFAULT_WALLPAPER_PATH);
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

static int inputm_config_path(char *path, uint32_t capacity)
{
    uint32_t home_len;
    const char *name = ".inputm.conf";
    if (!path || !capacity || !current_user.uid || !current_user.home[0]) {
        return 0;
    }
    home_len = text_len(current_user.home);
    if (home_len + 1U + text_len(name) >= capacity) {
        return 0;
    }
    copy_text(path, capacity, current_user.home);
    path[home_len] = '/';
    copy_text(path + home_len + 1U, capacity - home_len - 1U, name);
    return 1;
}

static int inputm_config_get(const char *config, const char *key,
                             char *value, uint32_t capacity)
{
    uint32_t key_len = text_len(key);
    uint32_t pos = 0;
    uint8_t found = 0;
    if (!config || !key || !key_len || !value || !capacity) {
        return 0;
    }
    value[0] = 0;
    while (config[pos]) {
        uint32_t start = pos;
        uint32_t end;
        uint32_t out = 0;
        uint8_t match = 1;
        while (config[pos] && config[pos] != '\n' && config[pos] != '\r') {
            ++pos;
        }
        end = pos;
        while (config[pos] == '\n' || config[pos] == '\r') {
            ++pos;
        }
        if (end <= start + key_len || config[start + key_len] != '=') {
            continue;
        }
        for (uint32_t i = 0; i < key_len; ++i) {
            if (config[start + i] != key[i]) {
                match = 0;
                break;
            }
        }
        if (!match) {
            continue;
        }
        start += key_len + 1U;
        while (start < end && out + 1U < capacity) {
            value[out++] = config[start++];
        }
        value[out] = 0;
        found = 1;
    }
    return found;
}

static uint32_t inputm_parse_u32(const char *text, uint32_t fallback)
{
    uint32_t value = 0;
    uint32_t digits = 0;
    while (text && *text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text - '0');
        ++digits;
        ++text;
    }
    return digits ? value : fallback;
}

static void inputm_provider_key(char *key, uint32_t capacity, uint32_t index,
                                const char *field)
{
    uint32_t pos = 0;
    key[0] = 0;
    append_text(key, &pos, capacity, "provider");
    append_dec(key, &pos, capacity, index);
    append_char(key, &pos, capacity, '_');
    append_text(key, &pos, capacity, field);
}

static int inputm_append_config(const char *key, const char *value)
{
    char path[LEONOS_FS_PATH_LEN];
    char line[LEONOS_FS_PATH_LEN + 80U];
    uint32_t pos = 0;
    int fd;
    long wrote;
    if (!key || !value || !inputm_config_path(path, sizeof(path))) {
        return 0;
    }
    line[0] = 0;
    append_char(line, &pos, sizeof(line), '\n');
    append_text(line, &pos, sizeof(line), key);
    append_char(line, &pos, sizeof(line), '=');
    append_text(line, &pos, sizeof(line), value);
    append_char(line, &pos, sizeof(line), '\n');
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_APPEND, 0);
    if (fd < 0) {
        return 0;
    }
    wrote = write(fd, line, pos);
    close(fd);
    if (wrote == (long)pos) {
        (void)leonos_inputm_notify_config(current_user.uid);
        return 1;
    }
    return 0;
}

static int inputm_key_is_safe(const char *key)
{
    uint32_t i = 0;
    if (!key || !key[0]) {
        return 0;
    }
    while (key[i]) {
        char ch = key[i++];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return 0;
        }
    }
    return i < SETTINGS_INPUTM_OPTION_KEY_LEN;
}

static int inputm_schema_value(const char *line, uint32_t length,
                               const char *field, char *out, uint32_t capacity)
{
    uint32_t field_len = text_len(field);
    uint32_t pos = 0;
    if (!line || !field || !out || capacity == 0 || length <= field_len ||
        line[field_len] != '=') {
        return 0;
    }
    for (uint32_t i = 0; i < field_len; ++i) {
        if (line[i] != field[i]) {
            return 0;
        }
    }
    while (field_len + 1U + pos < length && pos + 1U < capacity) {
        out[pos] = line[field_len + 1U + pos];
        ++pos;
    }
    out[pos] = 0;
    return 1;
}

static void inputm_add_schema_option(const char *config, const char *key,
                                     const char *type, const char *default_value,
                                     const char *label, const char *label_zh)
{
    struct settings_inputm_option *option;
    char value[16];
    if (inputm_option_count >= SETTINGS_INPUTM_OPTION_COUNT ||
        !inputm_key_is_safe(key) || !text_eq(type, "bool")) {
        return;
    }
    option = &inputm_options[inputm_option_count];
    *option = (struct settings_inputm_option){0};
    copy_text(option->key, sizeof(option->key), key);
    copy_text(option->label, sizeof(option->label), label && label[0] ? label : key);
    copy_text(option->label_zh, sizeof(option->label_zh), label_zh);
    if (inputm_config_get(config, key, value, sizeof(value))) {
        option->value = inputm_parse_u32(value, 0) ? 1 : 0;
    } else {
        option->value = inputm_parse_u32(default_value, 0) ? 1 : 0;
    }
    ++inputm_option_count;
}

static void inputm_load_extension_options(const char *config)
{
    struct settings_inputm_entry *entry;
    char schema[SETTINGS_INPUTM_CONFIG_MAX];
    char key[SETTINGS_INPUTM_OPTION_KEY_LEN] = {0};
    char type[16] = {0};
    char default_value[16] = {0};
    char label[SETTINGS_INPUTM_OPTION_LABEL_LEN] = {0};
    char label_zh[SETTINGS_INPUTM_OPTION_LABEL_LEN] = {0};
    uint8_t in_setting = 0;
    uint32_t pos = 0;
    int fd;
    long got;

    inputm_option_count = 0;
    entry = inputm_selected < inputm_entry_count ? &inputm_entries[inputm_selected] : 0;
    if (!entry || !entry->settings_path[0]) {
        return;
    }
    fd = open(entry->settings_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, schema, sizeof(schema) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    schema[got] = 0;
    while (schema[pos]) {
        uint32_t start = pos;
        uint32_t end;
        while (schema[pos] && schema[pos] != '\n' && schema[pos] != '\r') {
            ++pos;
        }
        end = pos;
        while (schema[pos] == '\n' || schema[pos] == '\r') {
            ++pos;
        }
        if (end - start == 9U &&
            schema[start] == '[' && schema[start + 1U] == 's' &&
            schema[start + 2U] == 'e' && schema[start + 3U] == 't' &&
            schema[start + 4U] == 't' && schema[start + 5U] == 'i' &&
            schema[start + 6U] == 'n' && schema[start + 7U] == 'g' &&
            schema[start + 8U] == ']') {
            if (in_setting) {
                inputm_add_schema_option(config, key, type, default_value, label, label_zh);
            }
            key[0] = 0;
            type[0] = 0;
            default_value[0] = 0;
            label[0] = 0;
            label_zh[0] = 0;
            in_setting = 1;
            continue;
        }
        if (!in_setting) {
            continue;
        }
        if (inputm_schema_value(schema + start, end - start, "key", key, sizeof(key)) ||
            inputm_schema_value(schema + start, end - start, "type", type, sizeof(type)) ||
            inputm_schema_value(schema + start, end - start, "default", default_value,
                                sizeof(default_value)) ||
            inputm_schema_value(schema + start, end - start, "label", label, sizeof(label))) {
            continue;
        }
        (void)inputm_schema_value(schema + start, end - start, "label_zh", label_zh,
                                  sizeof(label_zh));
    }
    if (in_setting) {
        inputm_add_schema_option(config, key, type, default_value, label, label_zh);
    }
}

static void inputm_sort_entries(void)
{
    for (uint32_t i = 1; i < inputm_entry_count; ++i) {
        for (uint32_t j = i + 1U; j < inputm_entry_count; ++j) {
            if (inputm_entries[j].order < inputm_entries[i].order) {
                struct settings_inputm_entry temp = inputm_entries[i];
                inputm_entries[i] = inputm_entries[j];
                inputm_entries[j] = temp;
            }
        }
    }
}

static void inputm_reload_extension_options(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char config[SETTINGS_INPUTM_CONFIG_MAX];
    int fd;
    long got;
    inputm_option_count = 0;
    if (!inputm_config_path(path, sizeof(path))) {
        return;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    config[got] = 0;
    inputm_load_extension_options(config);
}

static void inputm_load_settings(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char config[SETTINGS_INPUTM_CONFIG_MAX];
    char value[LEONOS_FS_PATH_LEN];
    int fd;
    long got;
    uint32_t configured = 0;
    inputm_entries[0] = (struct settings_inputm_entry){0};
    copy_text(inputm_entries[0].id, sizeof(inputm_entries[0].id), "en");
    inputm_entries[0].enabled = 1;
    inputm_entries[0].startup_mode = LEONOS_INPUTM_START_MANUAL;
    inputm_entry_count = 1;
    inputm_selected = 0;
    copy_text(inputm_default, sizeof(inputm_default), "en");
    copy_text(inputm_hotkey, sizeof(inputm_hotkey), "win-space");
    inputm_option_count = 0;
    if (!inputm_config_path(path, sizeof(path))) {
        return;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, config, sizeof(config) - 1U);
    close(fd);
    if (got <= 0) {
        return;
    }
    config[got] = 0;
    if (inputm_config_get(config, "default", value, sizeof(value)) && value[0]) {
        copy_text(inputm_default, sizeof(inputm_default), value);
    }
    if (inputm_config_get(config, "inputm_hotkey", value, sizeof(value)) && value[0]) {
        copy_text(inputm_hotkey, sizeof(inputm_hotkey), value);
    }
    if (inputm_config_get(config, "provider_count", value, sizeof(value))) {
        configured = inputm_parse_u32(value, 0);
    }
    if (configured > LEONOS_INPUTM_MAX_PROVIDERS) {
        configured = LEONOS_INPUTM_MAX_PROVIDERS;
    }
    for (uint32_t i = 0; i < configured && inputm_entry_count < SETTINGS_INPUTM_ROWS; ++i) {
        char key[48];
        struct settings_inputm_entry *entry = &inputm_entries[inputm_entry_count];
        inputm_provider_key(key, sizeof(key), i, "id");
        if (!inputm_config_get(config, key, value, sizeof(value)) || !value[0] ||
            text_eq(value, "en")) {
            continue;
        }
        *entry = (struct settings_inputm_entry){0};
        copy_text(entry->id, sizeof(entry->id), value);
        entry->config_index = i;
        entry->enabled = 1;
        entry->startup_mode = LEONOS_INPUTM_START_ON_DEMAND;
        entry->order = i + 1U;
        inputm_provider_key(key, sizeof(key), i, "path");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            copy_text(entry->path, sizeof(entry->path), value);
        }
        inputm_provider_key(key, sizeof(key), i, "settings");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            copy_text(entry->settings_path, sizeof(entry->settings_path), value);
        }
        inputm_provider_key(key, sizeof(key), i, "settings_app");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            copy_text(entry->settings_app, sizeof(entry->settings_app), value);
        }
        inputm_provider_key(key, sizeof(key), i, "enabled");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            entry->enabled = inputm_parse_u32(value, 1) ? 1 : 0;
        }
        inputm_provider_key(key, sizeof(key), i, "startup");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            uint32_t mode = inputm_parse_u32(value, LEONOS_INPUTM_START_ON_DEMAND);
            entry->startup_mode = mode <= LEONOS_INPUTM_START_ON_DEMAND ? mode :
                                  LEONOS_INPUTM_START_ON_DEMAND;
        }
        inputm_provider_key(key, sizeof(key), i, "order");
        if (inputm_config_get(config, key, value, sizeof(value))) {
            uint32_t order = inputm_parse_u32(value, i + 1U);
            entry->order = order ? order : i + 1U;
        }
        ++inputm_entry_count;
    }
    inputm_sort_entries();
    for (uint32_t i = 1; i < inputm_entry_count; ++i) {
        if (text_eq(inputm_entries[i].id, inputm_default)) {
            inputm_selected = i;
            break;
        }
    }
    inputm_load_extension_options(config);
}

static const char *inputm_startup_label(uint32_t mode)
{
    if (mode == LEONOS_INPUTM_START_LOGIN) {
        return T("At sign-in", "登录时启动");
    }
    if (mode == LEONOS_INPUTM_START_ON_DEMAND) {
        return T("On demand", "按需启动");
    }
    return T("Manual", "手动启动");
}

static void request_display(uint32_t action, uint32_t mode, uint32_t scale)
{
    struct leonos_display_request request;
    request.action = action;
    request.mode_index = mode;
    request.scale_index = scale;
    (void)leonos_display_request(&request);
}

static void request_appearance_change(const char *ok_text)
{
    struct leonos_appearance_request request;
    request.theme = appearance_state.theme;
    request.metro_color_scheme = appearance_state.metro_color_scheme;
    request.win95_color_scheme = appearance_state.win95_color_scheme;
    request.wallpaper_mode = appearance_state.wallpaper_mode;
    copy_text(request.wallpaper_path, sizeof(request.wallpaper_path),
              appearance_state.wallpaper_path);
    if (leonos_appearance_request_theme(&request) > 0) {
        (void)leonos_ui_theme_set_appearance(request.theme,
                                             request.metro_color_scheme,
                                             request.win95_color_scheme);
        copy_text(status_text, sizeof(status_text),
                  ok_text ? ok_text : T("Personalization updated", "个性化设置已更新"));
    } else {
        copy_text(status_text, sizeof(status_text),
                  T("Could not apply personalization", "无法应用个性化设置"));
    }
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
    }
}

static void draw_personalization_page(struct leonos_ui_surface *ui)
{
    struct leonos_ui_dropdown_item theme_items[2];
    struct leonos_ui_dropdown_item metro_items[LEONOS_UI_COLOR_SCHEME_COUNT];
    struct leonos_ui_dropdown_item win95_items[LEONOS_UI_COLOR_SCHEME_COUNT];
    struct leonos_ui_dropdown_item wallpaper_items[LEONOS_WALLPAPER_MODE_COUNT];
    uint32_t disabled = current_user.uid ? 0 : LEONOS_UI_BUTTON_DISABLED;
    theme_items[0] = (struct leonos_ui_dropdown_item){"Metro", LEONOS_UI_THEME_METRO, 0};
    theme_items[1] = (struct leonos_ui_dropdown_item){"Win95", LEONOS_UI_THEME_WIN95, 0};
    fill_theme_color_items(metro_items);
    fill_theme_color_items(win95_items);
    fill_wallpaper_mode_items(wallpaper_items);

    leonos_ui_text(ui, 34, 64,
                   T("Personalization is saved for the current user. Metro and Win95 keep separate colors.",
                     "个性化设置按当前用户保存。Metro 和 Win95 保留各自独立的颜色。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);

    leonos_ui_text(ui, 44, 104, T("Theme style", "主题样式"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 98, 190, theme_label(),
                       active_drop == DROP_THEME, disabled);

    leonos_ui_text(ui, 44, 144, T("Metro color", "Metro 颜色"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 138, 190,
                       theme_color_label(appearance_state.metro_color_scheme),
                       active_drop == DROP_METRO_COLOR, disabled);
    leonos_ui_rect(ui, 364, 141, 28, 18,
                   leonos_ui_theme_scheme_accent(LEONOS_UI_THEME_METRO,
                                                 appearance_state.metro_color_scheme));

    leonos_ui_text(ui, 44, 184, T("Win95 color", "Win95 颜色"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 178, 190,
                       theme_color_label(appearance_state.win95_color_scheme),
                       active_drop == DROP_WIN95_COLOR, disabled);
    leonos_ui_rect(ui, 364, 181, 28, 18,
                   leonos_ui_theme_scheme_accent(LEONOS_UI_THEME_WIN95,
                                                 appearance_state.win95_color_scheme));

    leonos_ui_text(ui, 44, 224, T("Wallpaper", "壁纸"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text_field(ui, 160, 218, 318, appearance_state.wallpaper_path,
                         LEONOS_UI_EDIT_READONLY | disabled);
    leonos_ui_button(ui, 486, 218, 82, LEONOS_UI_BUTTON_H,
                     T("Browse", "浏览"), disabled);
    leonos_ui_button(ui, 576, 218, 82, LEONOS_UI_BUTTON_H,
                     T("Default", "默认"), disabled);

    leonos_ui_text(ui, 44, 264, T("Display mode", "显示方式"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 160, 258, 190,
                       wallpaper_mode_label(appearance_state.wallpaper_mode),
                       active_drop == DROP_WALLPAPER_MODE, disabled);
    leonos_ui_text(ui, 360, 264,
                   T("BMP only, up to 1280 x 720.", "仅 BMP，最大 1280 x 720。"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);

    if (!current_user.uid) {
        leonos_ui_text(ui, 44, 318,
                       T("Sign in to change personalization.", "登录后才能更改个性化设置。"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }

    if (active_drop == DROP_THEME) {
        leonos_ui_dropdown(ui, 160, 122, 190, theme_items, 2,
                           appearance_state.theme, SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_METRO_COLOR) {
        leonos_ui_dropdown(ui, 160, 162, 190, metro_items,
                           LEONOS_UI_COLOR_SCHEME_COUNT,
                           appearance_state.metro_color_scheme,
                           SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_WIN95_COLOR) {
        leonos_ui_dropdown(ui, 160, 202, 190, win95_items,
                           LEONOS_UI_COLOR_SCHEME_COUNT,
                           appearance_state.win95_color_scheme,
                           SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_WALLPAPER_MODE) {
        leonos_ui_dropdown(ui, 160, 282, 190, wallpaper_items,
                           LEONOS_WALLPAPER_MODE_COUNT,
                           appearance_state.wallpaper_mode,
                           SETTINGS_DROPDOWN_ROW_H, 1000);
    }
}

static void draw_input_methods_page(struct leonos_ui_surface *ui)
{
    const struct leonos_ui_list_column cols[] = {
        {T("Input method", "输入法"), 220},
        {T("Enabled", "启用"), 90},
        {T("Startup", "启动方式"), 150},
    };
    struct leonos_ui_dropdown_item startup_items[3] = {
        {T("Manual", "手动"), LEONOS_INPUTM_START_MANUAL, 0},
        {T("At sign-in", "登录时"), LEONOS_INPUTM_START_LOGIN, 0},
        {T("On demand", "按需"), LEONOS_INPUTM_START_ON_DEMAND, 0},
    };
    struct leonos_ui_dropdown_item hotkey_items[2] = {
        {"Win + Space", 0, 0},
        {"Alt + Shift", 1, 0},
    };
    struct settings_inputm_entry *selected =
        inputm_selected < inputm_entry_count ? &inputm_entries[inputm_selected] : 0;
    leonos_ui_text(ui, 34, 64,
                   T("Input methods and learning data are isolated for the current user.",
                     "输入法和学习数据按当前用户隔离保存。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 34, 90, 460, cols, 3);
    for (uint32_t i = 0; i < inputm_entry_count && i < 5U; ++i) {
        const char *cells[3];
        cells[0] = inputm_entries[i].id;
        cells[1] = inputm_entries[i].enabled ? T("Yes", "是") : T("No", "否");
        cells[2] = inputm_startup_label(inputm_entries[i].startup_mode);
        leonos_ui_listview_row(ui, 34, 118 + i * 27U, 460, cols, cells, 3,
                               i == inputm_selected ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_button(ui, 510, 92, 156, LEONOS_UI_BUTTON_H,
                     selected && selected->enabled ? T("Disable", "禁用") :
                                                     T("Enable", "启用"),
                     !selected || !current_user.uid || inputm_selected == 0 ?
                         LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, 510, 126, 156, LEONOS_UI_BUTTON_H,
                     T("Use as default", "设为默认"),
                     !selected || !selected->enabled || !current_user.uid ?
                         LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_combobox(ui, 510, 160, 156,
                        selected ? inputm_startup_label(selected->startup_mode) : "-",
                        active_drop == DROP_INPUTM_STARTUP,
                        !selected || !current_user.uid || inputm_selected == 0 ?
                            LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, 510, 194, 74, LEONOS_UI_BUTTON_H,
                     T("Move up", "上移"),
                     !selected || !current_user.uid || inputm_selected <= 1U ?
                         LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, 592, 194, 74, LEONOS_UI_BUTTON_H,
                     T("Move down", "下移"),
                     !selected || !current_user.uid || inputm_selected == 0 ||
                         inputm_selected + 1U >= inputm_entry_count ?
                         LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_text(ui, 44, 266, T("Switch shortcut", "切换快捷键"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 170, 260, 180,
                        text_eq(inputm_hotkey, "alt-shift") ? "Alt + Shift" : "Win + Space",
                        active_drop == DROP_INPUTM_HOTKEY,
                        current_user.uid ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_text(ui, 372, 266, T("Candidates", "候选框"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 470, 266, T("System overlay", "系统覆盖层"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
    if (inputm_option_count) {
        for (uint32_t i = 0; i < inputm_option_count; ++i) {
            char label[SETTINGS_INPUTM_OPTION_LABEL_LEN + 12U];
            uint32_t pos = 0;
            const char *base = T(inputm_options[i].label,
                                 inputm_options[i].label_zh[0] ?
                                     inputm_options[i].label_zh : inputm_options[i].label);
            label[0] = 0;
            append_text(label, &pos, sizeof(label), base);
            append_text(label, &pos, sizeof(label), inputm_options[i].value ? ": On" : ": Off");
            leonos_ui_button(ui, 44U + i * 150U, 304, 140, LEONOS_UI_BUTTON_H,
                             label, current_user.uid ?
                                        (inputm_options[i].value ? LEONOS_UI_BUTTON_PRESSED : 0) :
                                        LEONOS_UI_BUTTON_DISABLED);
        }
    } else {
        leonos_ui_text(ui, 44, 314, T("This input method has no configurable options.",
                                      "此输入法没有可配置选项。"),
                       LEONOS_UI_DARK, LEONOS_UI_GRAY);
    }
    leonos_ui_button(ui, 510, 344, 156, LEONOS_UI_BUTTON_H,
                     selected && selected->settings_app[0] ?
                         T("Open provider settings", "打开输入法设置") :
                         T("Update dictionary", "更新词库"),
                     !selected ||
                         (!(selected->settings_app[0]) &&
                          (!text_eq(selected->id, "oschinpt") || !selected->path[0])) ?
                         LEONOS_UI_BUTTON_DISABLED : 0);
    if (active_drop == DROP_INPUTM_STARTUP && selected) {
        leonos_ui_dropdown(ui, 510, 184, 156, startup_items, 3,
                           selected->startup_mode, SETTINGS_DROPDOWN_ROW_H, 1000);
    } else if (active_drop == DROP_INPUTM_HOTKEY) {
        leonos_ui_dropdown(ui, 170, 284, 180, hotkey_items, 2,
                           text_eq(inputm_hotkey, "alt-shift") ? 1U : 0U,
                           SETTINGS_DROPDOWN_ROW_H, 1000);
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
                         text_eq(program, "0:/programs/browser/browser.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 520, y + 2, 70, LEONOS_UI_BUTTON_H,
                         T("Notepad", "记事本"),
                         text_eq(program, "0:/programs/notepad/notepad.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 596, y + 2, 58, LEONOS_UI_BUTTON_H,
                         T("Image", "图片"),
                         text_eq(program, "0:/programs/imageview/imageview.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
        leonos_ui_button(ui, 660, y + 2, 50, LEONOS_UI_BUTTON_H,
                         T("Help", "帮助"),
                         text_eq(program, "0:/programs/oshlp/oshlp.elf") ? LEONOS_UI_BUTTON_PRESSED : 0);
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
    draw_field(ui, 298, T("License file", "许可证文件"), "0:/system/state/license.dat");
}

static void draw_settings(struct leonos_ui_surface *ui)
{
    struct leonos_ui_tab_item tabs[] = {
        {T("Display", "显示"), PAGE_DISPLAY, 0},
        {T("Personalize", "个性化"), PAGE_PERSONALIZATION, 0},
        {T("Users", "用户"), PAGE_USERS, 0},
        {T("File Types", "文件类型"), PAGE_ASSOC, 0},
        {T("Services", "服务"), PAGE_SERVICES, 0},
        {T("Activation", "激活"), PAGE_ACTIVATION, 0},
        {T("Input", "输入法"), PAGE_INPUT_METHODS, 0},
    };
    leonos_ui_rect(ui, 0, 0, SETTINGS_W, SETTINGS_H, LEONOS_UI_GRAY);
    settings_tabs.selected_id = active_page;
    leonos_ui_tab_control(ui, 18, SETTINGS_TAB_Y, SETTINGS_W - 36, tabs,
                          SETTINGS_TAB_COUNT, &settings_tabs);
    leonos_ui_tab_body(ui, 18, SETTINGS_BODY_Y, SETTINGS_W - 36, SETTINGS_H - 84);
    if (active_page == PAGE_DISPLAY) {
        draw_display_page(ui);
    } else if (active_page == PAGE_PERSONALIZATION) {
        draw_personalization_page(ui);
    } else if (active_page == PAGE_USERS) {
        draw_users_page(ui);
    } else if (active_page == PAGE_ASSOC) {
        draw_assoc_page(ui);
    } else if (active_page == PAGE_SERVICES) {
        draw_services_page(ui);
    } else if (active_page == PAGE_INPUT_METHODS) {
        draw_input_methods_page(ui);
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
    struct leonos_ui_dropdown_item metro_items[LEONOS_UI_COLOR_SCHEME_COUNT];
    struct leonos_ui_dropdown_item win95_items[LEONOS_UI_COLOR_SCHEME_COUNT];
    struct leonos_ui_dropdown_item wallpaper_items[LEONOS_WALLPAPER_MODE_COUNT];
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
    fill_theme_color_items(metro_items);
    fill_theme_color_items(win95_items);
    fill_wallpaper_mode_items(wallpaper_items);
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
        leonos_ui_dropdown_hit(x, y, 160, 122, 190, theme_items, 2,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (current_user.uid &&
            (id == LEONOS_UI_THEME_METRO || id == LEONOS_UI_THEME_WIN95)) {
            appearance_state.theme = id;
            request_appearance_change(T("Theme style changed", "主题样式已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_METRO_COLOR &&
        leonos_ui_dropdown_hit(x, y, 160, 162, 190, metro_items,
                               LEONOS_UI_COLOR_SCHEME_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (current_user.uid && id < LEONOS_UI_COLOR_SCHEME_COUNT) {
            appearance_state.metro_color_scheme = id;
            request_appearance_change(T("Metro color changed", "Metro 颜色已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_WIN95_COLOR &&
        leonos_ui_dropdown_hit(x, y, 160, 202, 190, win95_items,
                               LEONOS_UI_COLOR_SCHEME_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (current_user.uid && id < LEONOS_UI_COLOR_SCHEME_COUNT) {
            appearance_state.win95_color_scheme = id;
            request_appearance_change(T("Win95 color changed", "Win95 颜色已更改"));
        }
        return 1;
    }
    if (active_drop == DROP_WALLPAPER_MODE &&
        leonos_ui_dropdown_hit(x, y, 160, 282, 190, wallpaper_items,
                               LEONOS_WALLPAPER_MODE_COUNT,
                               SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
        active_drop = DROP_NONE;
        if (current_user.uid && id < LEONOS_WALLPAPER_MODE_COUNT) {
            appearance_state.wallpaper_mode = id;
            request_appearance_change(T("Wallpaper mode changed", "壁纸显示方式已更改"));
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
    if (leonos_ui_show_password_dialog(T("Create user", "创建用户"), T("Password", "密码"),
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
    if (leonos_ui_show_password_dialog(T("Reset password", "重置密码"), T("New password", "新密码"),
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
    if (leonos_ui_show_password_dialog(T("Change password", "修改密码"), T("Old password", "旧密码"),
                                       old_pass, sizeof(old_pass)) <= 0) {
        return;
    }
    if (leonos_ui_show_password_dialog(T("Change password", "修改密码"), T("New password", "新密码"),
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

static void choose_wallpaper_dialog(void)
{
    char path[LEONOS_FS_PATH_LEN];
    copy_text(path, sizeof(path),
              appearance_state.wallpaper_path[0]
                  ? appearance_state.wallpaper_path
                  : SETTINGS_DEFAULT_WALLPAPER_PATH);
    if (leonos_ui_show_open_dialog(T("Choose wallpaper", "选择壁纸"),
                                   path, sizeof(path),
                                   T("Bitmap (*.bmp)", "BMP 图片 (*.bmp)"),
                                   ".bmp") <= 0) {
        return;
    }
    if (!validate_wallpaper_bmp(path)) {
        copy_text(status_text, sizeof(status_text),
                  T("Wallpaper BMP must be uncompressed 24/32-bit and no larger than 1280 x 720.",
                    "壁纸 BMP 必须是未压缩 24/32 位，且不超过 1280 x 720。"));
        return;
    }
    copy_text(appearance_state.wallpaper_path,
              sizeof(appearance_state.wallpaper_path), path);
    request_appearance_change(T("Wallpaper changed", "壁纸已更改"));
}

static void handle_personalization_click(int32_t x, int32_t y)
{
    if (active_drop && handle_open_dropdown_hit(x, y)) {
        return;
    }
    active_drop = DROP_NONE;
    if (!current_user.uid) {
        copy_text(status_text, sizeof(status_text),
                  T("Sign in to change personalization.", "登录后才能更改个性化设置。"));
        return;
    }
    if (hit_rect_i(x, y, 160, 98, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_THEME;
        return;
    }
    if (hit_rect_i(x, y, 160, 138, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_METRO_COLOR;
        return;
    }
    if (hit_rect_i(x, y, 160, 178, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_WIN95_COLOR;
        return;
    }
    if (hit_rect_i(x, y, 486, 218, 82, LEONOS_UI_BUTTON_H)) {
        choose_wallpaper_dialog();
        return;
    }
    if (hit_rect_i(x, y, 576, 218, 82, LEONOS_UI_BUTTON_H)) {
        copy_text(appearance_state.wallpaper_path,
                  sizeof(appearance_state.wallpaper_path),
                  SETTINGS_DEFAULT_WALLPAPER_PATH);
        request_appearance_change(T("Default wallpaper restored", "已恢复默认壁纸"));
        return;
    }
    if (hit_rect_i(x, y, 160, 258, 190, LEONOS_FONT_H + 8)) {
        active_drop = DROP_WALLPAPER_MODE;
        return;
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
            set_assoc_for_row(i, "0:/programs/browser/browser.elf");
            return;
        }
        if (hit_rect_i(x, y, 520, row_y + 2, 70, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/programs/notepad/notepad.elf");
            return;
        }
        if (hit_rect_i(x, y, 596, row_y + 2, 58, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/programs/imageview/imageview.elf");
            return;
        }
        if (hit_rect_i(x, y, 660, row_y + 2, 50, LEONOS_UI_BUTTON_H)) {
            set_assoc_for_row(i, "0:/programs/oshlp/oshlp.elf");
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

static void inputm_set_status(int ok, const char *success, const char *failure)
{
    copy_text(status_text, sizeof(status_text), ok ? success : failure);
}

static void inputm_request_login_start(const struct settings_inputm_entry *entry)
{
    struct leonos_startup_command command = {0};
    uint32_t request_id = 0;
    if (!entry || !entry->path[0] || text_len(entry->path) >= sizeof(command.path)) {
        return;
    }
    copy_text(command.path, sizeof(command.path), entry->path);
    if (leonos_startup_request(&command, &request_id) == 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Login startup approval requested", "已请求登录启动权限"));
    }
}

static void handle_input_methods_click(int32_t x, int32_t y)
{
    struct settings_inputm_entry *entry =
        inputm_selected < inputm_entry_count ? &inputm_entries[inputm_selected] : 0;
    if (active_drop == DROP_INPUTM_STARTUP && entry) {
        struct leonos_ui_dropdown_item items[3] = {
            {T("Manual", "手动"), LEONOS_INPUTM_START_MANUAL, 0},
            {T("At sign-in", "登录时"), LEONOS_INPUTM_START_LOGIN, 0},
            {T("On demand", "按需"), LEONOS_INPUTM_START_ON_DEMAND, 0},
        };
        uint32_t id = 0;
        if (leonos_ui_dropdown_hit(x, y, 510, 184, 156, items, 3,
                                   SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
            char key[48];
            char value[4];
            active_drop = DROP_NONE;
            if (id <= LEONOS_INPUTM_START_ON_DEMAND) {
                entry->startup_mode = id;
                inputm_provider_key(key, sizeof(key), entry->config_index, "startup");
                value[0] = (char)('0' + id);
                value[1] = 0;
                inputm_set_status(inputm_append_config(key, value),
                                  T("Startup behavior saved", "启动方式已保存"),
                                  T("Could not save input method", "无法保存输入法设置"));
                if (id == LEONOS_INPUTM_START_LOGIN) {
                    inputm_request_login_start(entry);
                }
            }
            return;
        }
    }
    if (active_drop == DROP_INPUTM_HOTKEY) {
        struct leonos_ui_dropdown_item items[2] = {
            {"Win + Space", 0, 0},
            {"Alt + Shift", 1, 0},
        };
        uint32_t id = 0;
        if (leonos_ui_dropdown_hit(x, y, 170, 284, 180, items, 2,
                                   SETTINGS_DROPDOWN_ROW_H, 1000, &id)) {
            active_drop = DROP_NONE;
            copy_text(inputm_hotkey, sizeof(inputm_hotkey),
                      id == 1U ? "alt-shift" : "win-space");
            inputm_set_status(inputm_append_config("inputm_hotkey", inputm_hotkey),
                              T("Input shortcut changed", "输入法快捷键已更改"),
                              T("Could not save input method", "无法保存输入法设置"));
            return;
        }
    }
    active_drop = DROP_NONE;
    if (!current_user.uid) {
        copy_text(status_text, sizeof(status_text),
                  T("Sign in to change input methods", "登录后才能更改输入法"));
        return;
    }
    if (y >= 118 && y < 118 + (int32_t)(inputm_entry_count * 27U) &&
        x >= 34 && x < 494) {
        inputm_selected = (uint32_t)(y - 118) / 27U;
        inputm_reload_extension_options();
        return;
    }
    entry = inputm_selected < inputm_entry_count ? &inputm_entries[inputm_selected] : 0;
    if (!entry) {
        return;
    }
    if (hit_rect_i(x, y, 510, 92, 156, LEONOS_UI_BUTTON_H) && inputm_selected != 0) {
        char key[48];
        entry->enabled = entry->enabled ? 0 : 1;
        inputm_provider_key(key, sizeof(key), entry->config_index, "enabled");
        inputm_set_status(inputm_append_config(key, entry->enabled ? "1" : "0"),
                          entry->enabled ? T("Input method enabled", "输入法已启用") :
                                           T("Input method disabled", "输入法已禁用"),
                          T("Could not save input method", "无法保存输入法设置"));
        if (!entry->enabled && text_eq(inputm_default, entry->id)) {
            copy_text(inputm_default, sizeof(inputm_default), "en");
            (void)inputm_append_config("default", "en");
        }
        if (!entry->enabled) {
            (void)leonos_inputm_set_active(current_user.uid, "en");
        }
        return;
    }
    if (hit_rect_i(x, y, 510, 126, 156, LEONOS_UI_BUTTON_H) && entry->enabled) {
        copy_text(inputm_default, sizeof(inputm_default), entry->id);
        inputm_set_status(inputm_append_config("default", entry->id),
                          T("Default input method saved", "默认输入法已保存"),
                          T("Could not save input method", "无法保存输入法设置"));
        return;
    }
    if (hit_rect_i(x, y, 510, 160, 156, LEONOS_UI_BUTTON_H) && inputm_selected != 0) {
        active_drop = DROP_INPUTM_STARTUP;
        return;
    }
    if (hit_rect_i(x, y, 510, 194, 74, LEONOS_UI_BUTTON_H) && inputm_selected > 1U) {
        struct settings_inputm_entry *previous = &inputm_entries[inputm_selected - 1U];
        char key[48];
        char value[12];
        uint32_t pos = 0;
        uint32_t order = entry->order;
        entry->order = previous->order;
        previous->order = order;
        inputm_provider_key(key, sizeof(key), entry->config_index, "order");
        value[0] = 0;
        append_dec(value, &pos, sizeof(value), entry->order);
        if (!inputm_append_config(key, value)) {
            copy_text(status_text, sizeof(status_text),
                      T("Could not save input method order", "无法保存输入法顺序"));
            return;
        }
        inputm_provider_key(key, sizeof(key), previous->config_index, "order");
        pos = 0;
        value[0] = 0;
        append_dec(value, &pos, sizeof(value), previous->order);
        if (!inputm_append_config(key, value)) {
            copy_text(status_text, sizeof(status_text),
                      T("Could not save input method order", "无法保存输入法顺序"));
            return;
        }
        inputm_sort_entries();
        --inputm_selected;
        inputm_reload_extension_options();
        copy_text(status_text, sizeof(status_text), T("Input method moved", "已调整输入法顺序"));
        return;
    }
    if (hit_rect_i(x, y, 592, 194, 74, LEONOS_UI_BUTTON_H) && inputm_selected != 0 &&
        inputm_selected + 1U < inputm_entry_count) {
        struct settings_inputm_entry *next = &inputm_entries[inputm_selected + 1U];
        char key[48];
        char value[12];
        uint32_t pos = 0;
        uint32_t order = entry->order;
        entry->order = next->order;
        next->order = order;
        inputm_provider_key(key, sizeof(key), entry->config_index, "order");
        value[0] = 0;
        append_dec(value, &pos, sizeof(value), entry->order);
        if (!inputm_append_config(key, value)) {
            copy_text(status_text, sizeof(status_text),
                      T("Could not save input method order", "无法保存输入法顺序"));
            return;
        }
        inputm_provider_key(key, sizeof(key), next->config_index, "order");
        pos = 0;
        value[0] = 0;
        append_dec(value, &pos, sizeof(value), next->order);
        if (!inputm_append_config(key, value)) {
            copy_text(status_text, sizeof(status_text),
                      T("Could not save input method order", "无法保存输入法顺序"));
            return;
        }
        inputm_sort_entries();
        ++inputm_selected;
        inputm_reload_extension_options();
        copy_text(status_text, sizeof(status_text), T("Input method moved", "已调整输入法顺序"));
        return;
    }
    if (hit_rect_i(x, y, 170, 260, 180, LEONOS_UI_BUTTON_H)) {
        active_drop = DROP_INPUTM_HOTKEY;
        return;
    }
    for (uint32_t i = 0; i < inputm_option_count; ++i) {
        if (hit_rect_i(x, y, 44 + (int32_t)i * 150, 304, 140, LEONOS_UI_BUTTON_H)) {
            inputm_options[i].value = inputm_options[i].value ? 0 : 1;
            inputm_set_status(inputm_append_config(inputm_options[i].key,
                                                   inputm_options[i].value ? "1" : "0"),
                              T("Input method setting changed", "输入法设置已更改"),
                              T("Could not save input method", "无法保存输入法设置"));
            return;
        }
    }
    if (hit_rect_i(x, y, 510, 344, 156, LEONOS_UI_BUTTON_H) && entry->settings_app[0]) {
        char *argv[2];
        argv[0] = entry->settings_app;
        argv[1] = 0;
        inputm_set_status(leonos_spawn_argv(entry->settings_app, argv) > 0,
                          T("Provider settings started", "已启动输入法设置"),
                          T("Could not start provider settings", "无法启动输入法设置"));
        return;
    }
    if (hit_rect_i(x, y, 510, 344, 156, LEONOS_UI_BUTTON_H) &&
        text_eq(entry->id, "oschinpt") && entry->path[0]) {
        char *argv[3];
        argv[0] = entry->path;
        argv[1] = "--update";
        argv[2] = 0;
        inputm_set_status(leonos_spawn_argv(entry->path, argv) > 0,
                          T("Dictionary update started", "词库更新已开始"),
                          T("Could not start dictionary update", "无法启动词库更新"));
    }
}

static void handle_click(int32_t x, int32_t y)
{
    struct leonos_ui_tab_item tabs[] = {
        {T("Display", "显示"), PAGE_DISPLAY, 0},
        {T("Personalize", "个性化"), PAGE_PERSONALIZATION, 0},
        {T("Users", "用户"), PAGE_USERS, 0},
        {T("File Types", "文件类型"), PAGE_ASSOC, 0},
        {T("Services", "服务"), PAGE_SERVICES, 0},
        {T("Activation", "激活"), PAGE_ACTIVATION, 0},
        {T("Input", "输入法"), PAGE_INPUT_METHODS, 0},
    };
    if (leonos_ui_tab_control_handle_mouse(&settings_tabs, x, y, 18,
                                           SETTINGS_TAB_Y, SETTINGS_W - 36,
                                           tabs, SETTINGS_TAB_COUNT)) {
        active_page = (uint8_t)settings_tabs.selected_id;
        active_drop = DROP_NONE;
        return;
    }
    if (active_page == PAGE_DISPLAY) {
        handle_display_click(x, y);
    } else if (active_page == PAGE_PERSONALIZATION) {
        handle_personalization_click(x, y);
    } else if (active_page == PAGE_USERS) {
        handle_users_click(x, y);
    } else if (active_page == PAGE_ASSOC) {
        handle_assoc_click(x, y);
    } else if (active_page == PAGE_SERVICES) {
        handle_services_click(x, y);
    } else if (active_page == PAGE_INPUT_METHODS) {
        handle_input_methods_click(x, y);
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    unsigned long last_refresh = 0;
    puts("[settings.elf] settings starting");
    leonos_ui_tab_state_init(&settings_tabs, PAGE_DISPLAY);
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
    inputm_load_settings();
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
                (void)leonos_ui_theme_set_appearance((uint32_t)event.x,
                                                     (uint32_t)event.y,
                                                     (uint32_t)event.dx);
                refresh_appearance_state();
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
                inputm_load_settings();
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
