#include <leonos/auth.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define SERVICEMGR_W 660U
#define SERVICEMGR_H 390U
#define SERVICEMGR_ROWS 5U
#define SERVICEMGR_CONFIG_PATH "0:/etc/services.cfg"
#define SERVICEMGR_CONFIG_MAX 512U
#define T(en, zh) leonos_i18n((en), (zh))

struct service_row {
    const char *key;
    const char *name_en;
    const char *name_zh;
    const char *detail_en;
    const char *detail_zh;
    uint8_t enabled;
    uint8_t locked;
};

static uint32_t pixels[SERVICEMGR_W * SERVICEMGR_H];
static struct leonos_user_info current_user;
static uint8_t can_manage;
static char status_text[160] = "Ready";
static struct service_row service_rows[SERVICEMGR_ROWS] = {
    {"desktop", "Desktop window service", "桌面窗口服务",
     "Required shell and window manager.", "必需的外壳和窗口管理器。", 1, 1},
    {"dhcp", "DHCP auto connect", "DHCP 自动连接",
     "Kernel tries DHCP during boot before keeping static fallback.", "内核启动时先尝试 DHCP，失败后保留静态回退。", 1, 0},
    {"network_icon", "Taskbar network icon", "任务栏网络图标",
     "Desktop shows the network state icon.", "桌面显示网络状态图标。", 1, 0},
    {"rtc_clock", "RTC taskbar clock", "RTC 任务栏时钟",
     "Desktop shows HH:MM:SS from RTC or uptime.", "桌面显示 RTC 或运行时间的 HH:MM:SS。", 1, 0},
    {"ntp_sync", "Network time sync", "网络时间同步",
     "Reserved switch for future NTP service.", "预留给未来 NTP 服务。", 0, 0},
};

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
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

static void append_text(char *dst, uint32_t *pos, uint32_t cap,
                        const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static int service_line_matches(const char *line, uint32_t len,
                                const char *key, uint8_t *value)
{
    uint32_t key_len = text_len(key);
    if (!line || !key || !value || key_len == 0 || len <= key_len ||
        line[key_len] != '=') {
        return 0;
    }
    for (uint32_t i = 0; i < key_len; ++i) {
        if (line[i] != key[i]) {
            return 0;
        }
    }
    *value = line[key_len + 1U] == '1' ||
             line[key_len + 1U] == 'y' ||
             line[key_len + 1U] == 'Y';
    return 1;
}

static void refresh_user(void)
{
    current_user = (struct leonos_user_info){0};
    can_manage = 0;
    if (leonos_auth_current(&current_user) == 0 &&
        current_user.role == LEONOS_AUTH_ROLE_ADMIN) {
        can_manage = 1;
    }
}

static void load_config(void)
{
    char cfg[SERVICEMGR_CONFIG_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    int fd = open(SERVICEMGR_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        if (!service_rows[i].locked) {
            service_rows[i].enabled = i == 4U ? 0 : 1;
        }
    }
    if (fd < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Using default service policy", "正在使用默认服务策略"));
        return;
    }
    while (len + 1U < sizeof(cfg)) {
        long got = read(fd, cfg + len, sizeof(cfg) - len - 1U);
        if (got < 0) {
            close(fd);
            copy_text(status_text, sizeof(status_text),
                      T("Could not read service policy", "无法读取服务策略"));
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
        for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
            uint8_t value = 0;
            if (!service_rows[i].locked &&
                service_line_matches(cfg + start, line_len,
                                     service_rows[i].key, &value)) {
                service_rows[i].enabled = value;
            }
        }
    }
    copy_text(status_text, sizeof(status_text),
              T("Service policy loaded", "服务策略已加载"));
}

static void save_config(void)
{
    char cfg[SERVICEMGR_CONFIG_MAX];
    uint32_t pos = 0;
    int fd;
    if (!can_manage) {
        copy_text(status_text, sizeof(status_text),
                  T("Administrator rights required", "需要管理员权限"));
        return;
    }
    cfg[0] = 0;
    append_text(cfg, &pos, sizeof(cfg), "# LeonOS service startup settings\n");
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        append_text(cfg, &pos, sizeof(cfg), service_rows[i].key);
        append_char(cfg, &pos, sizeof(cfg), '=');
        append_char(cfg, &pos, sizeof(cfg), service_rows[i].enabled ? '1' : '0');
        append_char(cfg, &pos, sizeof(cfg), '\n');
    }
    fd = open(SERVICEMGR_CONFIG_PATH,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save service policy", "无法保存服务策略"));
        return;
    }
    if (write(fd, cfg, pos) < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save service policy", "无法保存服务策略"));
    } else {
        copy_text(status_text, sizeof(status_text),
                  T("Service policy saved", "服务策略已保存"));
    }
    close(fd);
}

static void draw_servicemgr(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, SERVICEMGR_W, SERVICEMGR_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, SERVICEMGR_W, SERVICEMGR_H,
                     T("Service Manager", "服务管理器"));
    leonos_ui_text(ui, 24, 40,
                   can_manage
                       ? T("Manage startup services and desktop service switches.",
                           "管理启动服务和桌面服务开关。")
                       : T("You can view service settings. Administrator rights are required to save.",
                           "你可以查看服务设置，保存需要管理员权限。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        uint32_t y = 74U + i * 46U;
        uint32_t flags = (!can_manage || service_rows[i].locked)
                             ? LEONOS_UI_BUTTON_DISABLED
                             : 0;
        leonos_ui_panel(ui, 24, y, SERVICEMGR_W - 48U, 38U, LEONOS_UI_WHITE);
        leonos_ui_checkbox(ui, 36, y + 8U,
                           T(service_rows[i].name_en, service_rows[i].name_zh),
                           service_rows[i].enabled, flags);
        leonos_ui_text_clipped(ui, 276, y + 10U, SERVICEMGR_W - 310U,
                               T(service_rows[i].detail_en, service_rows[i].detail_zh),
                               flags ? LEONOS_UI_DARK : LEONOS_UI_BLACK,
                               LEONOS_UI_WHITE);
    }
    leonos_ui_button(ui, 24, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Refresh", "刷新"), 0);
    leonos_ui_button(ui, 126, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Save", "保存"),
                     can_manage ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_statusbar(ui, SERVICEMGR_H - 28U, 28U, status_text);
}

static void present(int window_id, struct leonos_ui_surface *ui)
{
    draw_servicemgr(ui);
    leonos_gui_present_window((uint32_t)window_id, SERVICEMGR_W,
                              SERVICEMGR_H, SERVICEMGR_W, pixels);
}

static int hit_rect(int32_t px, int32_t py, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

static void handle_click(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        uint32_t row_y = 74U + i * 46U;
        if (can_manage && !service_rows[i].locked &&
            hit_rect(x, y, 36, row_y + 4U, 230U, 30U)) {
            service_rows[i].enabled = service_rows[i].enabled ? 0 : 1;
            copy_text(status_text, sizeof(status_text),
                      T("Service setting changed", "服务设置已更改"));
            return;
        }
    }
    if (hit_rect(x, y, 24, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H)) {
        load_config();
    } else if (hit_rect(x, y, 126, SERVICEMGR_H - 66U,
                        92U, LEONOS_UI_BUTTON_H)) {
        save_config();
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[servicemgr.elf] service manager starting");
    refresh_user();
    load_config();
    window_id = leonos_gui_create_app_window_ex(T("Service Manager", "服务管理器"),
                                                T("Startup services", "启动服务"),
                                                SERVICEMGR_W, SERVICEMGR_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[servicemgr.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, SERVICEMGR_W, SERVICEMGR_H, SERVICEMGR_W);
    present(window_id, &ui);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1U)) {
                handle_click(event.x, event.y);
                present(window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                event.pressed && event.keycode == 1U) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                refresh_user();
                present(window_id, &ui);
            }
        } else {
            sleep_ms(20);
        }
    }
}
