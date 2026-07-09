#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define SERVICEMGR_W 780U
#define SERVICEMGR_H 430U
#define SERVICEMGR_ROWS 5U
#define SERVICEMGR_CONFIG_PATH "0:/etc/services.cfg"
#define SERVICEMGR_STATE_PATH "0:/var/run/services.state"
#define SERVICEMGR_COMMAND_PATH "0:/var/run/services.cmd"
#define SERVICEMGR_CONFIG_MAX 512U
#define SERVICEMGR_STATE_MAX 1024U
#define SERVICEMGR_ROW_Y 60U
#define SERVICEMGR_ROW_H 52U
#define T(en, zh) leonos_i18n((en), (zh))

struct service_row {
    const char *key;
    const char *name_en;
    const char *name_zh;
    const char *detail_en;
    const char *detail_zh;
    uint8_t enabled;
    uint8_t locked;
    char state[16];
    char state_detail[96];
    uint32_t pid;
};

static uint32_t pixels[SERVICEMGR_W * SERVICEMGR_H];
static struct leonos_user_info current_user;
static uint8_t can_manage;
static int32_t selected_row = 1;
static char status_text[180] = "Ready";
static unsigned long last_state_refresh_ms;

static struct service_row service_rows[SERVICEMGR_ROWS] = {
    {"desktop", "Desktop", "桌面",
     "Required shell and window manager.", "必需的外壳和窗口管理器。", 1, 1,
     "unknown", "runtime state unavailable", 0},
    {"dhcp", "DHCP", "DHCP",
     "Keeps trying DHCP when static fallback is active.", "静态回退时持续重试 DHCP。", 1, 0,
     "unknown", "runtime state unavailable", 0},
    {"network_icon", "Network icon", "网络图标",
     "Desktop taskbar network indicator.", "桌面任务栏网络指示器。", 1, 0,
     "unknown", "runtime state unavailable", 0},
    {"rtc_clock", "RTC clock", "RTC 时钟",
     "Desktop taskbar HH:MM:SS clock.", "桌面任务栏 HH:MM:SS 时钟。", 1, 0,
     "unknown", "runtime state unavailable", 0},
    {"ntp_sync", "Time sync", "网络校时",
     "Reserved until a kernel set-time ABI exists.", "等待内核设置时间 ABI 后启用。", 0, 0,
     "unknown", "runtime state unavailable", 0},
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

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int read_file_text(const char *path, char *buffer, uint32_t cap,
                          uint32_t *out_len)
{
    uint32_t len = 0;
    int fd;
    if (!buffer || cap == 0) {
        return -1;
    }
    buffer[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        if (out_len) {
            *out_len = 0;
        }
        return fd;
    }
    while (len + 1U < cap) {
        long got = read(fd, buffer + len, cap - len - 1U);
        if (got < 0) {
            close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    buffer[len] = 0;
    if (out_len) {
        *out_len = len;
    }
    return 0;
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

static int find_service(const char *key)
{
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        if (text_eq(key, service_rows[i].key)) {
            return (int)i;
        }
    }
    return -1;
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
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        if (!service_rows[i].locked) {
            service_rows[i].enabled = i == 4U ? 0 : 1;
        }
    }
    if (read_file_text(SERVICEMGR_CONFIG_PATH, cfg, sizeof(cfg), &len) < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Using default service policy", "正在使用默认服务策略"));
        return;
    }
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
    long wrote;
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
    wrote = write(fd, cfg, pos);
    close(fd);
    if (wrote < 0 || (uint32_t)wrote != pos) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not save service policy", "无法保存服务策略"));
    } else {
        copy_text(status_text, sizeof(status_text),
                  T("Service policy saved", "服务策略已保存"));
    }
}

static void reset_state(void)
{
    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        copy_text(service_rows[i].state, sizeof(service_rows[i].state), "unknown");
        copy_text(service_rows[i].state_detail,
                  sizeof(service_rows[i].state_detail),
                  "runtime state unavailable");
        service_rows[i].pid = 0;
    }
}

static void copy_segment(char *dst, uint32_t cap, const char *line,
                         uint32_t start, uint32_t end)
{
    uint32_t out = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (start < end && out + 1U < cap) {
        dst[out++] = line[start++];
    }
    dst[out] = 0;
}

static uint32_t parse_u32(const char *text)
{
    uint32_t value = 0;
    uint32_t i = 0;
    while (text && text[i] >= '0' && text[i] <= '9') {
        value = value * 10U + (uint32_t)(text[i] - '0');
        ++i;
    }
    return value;
}

static void parse_state_line(const char *line, uint32_t len)
{
    char key[32];
    char state[16];
    char pid_text[16];
    uint32_t a = 0;
    uint32_t b;
    uint32_t c;
    int index;
    if (!line || len == 0 || line[0] == '#') {
        return;
    }
    while (a < len && line[a] != '|') {
        ++a;
    }
    if (a >= len) {
        return;
    }
    b = a + 1U;
    while (b < len && line[b] != '|') {
        ++b;
    }
    if (b >= len) {
        return;
    }
    c = b + 1U;
    while (c < len && line[c] != '|') {
        ++c;
    }
    if (c >= len) {
        return;
    }
    copy_segment(key, sizeof(key), line, 0, a);
    index = find_service(key);
    if (index < 0) {
        return;
    }
    copy_segment(state, sizeof(state), line, a + 1U, b);
    copy_segment(pid_text, sizeof(pid_text), line, b + 1U, c);
    copy_text(service_rows[index].state, sizeof(service_rows[index].state), state);
    service_rows[index].pid = parse_u32(pid_text);
    copy_segment(service_rows[index].state_detail,
                 sizeof(service_rows[index].state_detail), line, c + 1U, len);
}

static void load_state(uint8_t quiet)
{
    char state[SERVICEMGR_STATE_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    reset_state();
    if (read_file_text(SERVICEMGR_STATE_PATH, state, sizeof(state), &len) < 0) {
        if (!quiet) {
            copy_text(status_text, sizeof(status_text),
                      T("Service runtime state unavailable",
                        "服务运行状态不可用"));
        }
        return;
    }
    while (pos < len) {
        uint32_t start = pos;
        while (pos < len && state[pos] != '\n' && state[pos] != '\r') {
            ++pos;
        }
        parse_state_line(state + start, pos - start);
        while (pos < len && (state[pos] == '\n' || state[pos] == '\r')) {
            ++pos;
        }
    }
    if (!quiet) {
        copy_text(status_text, sizeof(status_text),
                  T("Runtime state refreshed", "运行状态已刷新"));
    }
}

static void write_command(const char *action, uint32_t row)
{
    char cmd[64];
    uint32_t pos = 0;
    int fd;
    long wrote;
    if (!can_manage) {
        copy_text(status_text, sizeof(status_text),
                  T("Administrator rights required", "需要管理员权限"));
        return;
    }
    if (row >= SERVICEMGR_ROWS || service_rows[row].locked) {
        copy_text(status_text, sizeof(status_text),
                  T("This service is protected", "该服务受保护"));
        return;
    }
    cmd[0] = 0;
    append_text(cmd, &pos, sizeof(cmd), action);
    append_char(cmd, &pos, sizeof(cmd), ' ');
    append_text(cmd, &pos, sizeof(cmd), service_rows[row].key);
    append_char(cmd, &pos, sizeof(cmd), '\n');
    (void)mkdir("0:/var", 0);
    (void)mkdir("0:/var/run", 0);
    fd = open(SERVICEMGR_COMMAND_PATH,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not queue service command", "无法写入服务命令"));
        return;
    }
    wrote = write(fd, cmd, pos);
    close(fd);
    if (wrote < 0 || (uint32_t)wrote != pos) {
        copy_text(status_text, sizeof(status_text),
                  T("Could not queue service command", "无法写入服务命令"));
        return;
    }
    if (text_eq(action, "stop")) {
        service_rows[row].enabled = 0;
    } else {
        service_rows[row].enabled = 1;
    }
    save_config();
    copy_text(status_text, sizeof(status_text),
              T("Service command queued", "服务命令已提交"));
}

static const char *localized_state(const char *state)
{
    if (text_eq(state, "running")) {
        return T("Running", "运行中");
    }
    if (text_eq(state, "stopped")) {
        return T("Stopped", "已停止");
    }
    if (text_eq(state, "failed")) {
        return T("Failed", "失败");
    }
    if (text_eq(state, "starting")) {
        return T("Starting", "启动中");
    }
    return T("Unknown", "未知");
}

static void draw_servicemgr(struct leonos_ui_surface *ui)
{
    leonos_ui_rect(ui, 0, 0, SERVICEMGR_W, SERVICEMGR_H, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, 16,
                   can_manage
                       ? T("View service runtime state and control startup services.",
                           "查看服务运行状态并控制启动服务。")
                       : T("Runtime state is visible. Administrator rights are required to control services.",
                           "可以查看运行状态，控制服务需要管理员权限。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 36, 42, T("Service", "服务"), LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 300, 42, T("State", "状态"), LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 408, 42, "PID", LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 462, 42, T("Detail", "详情"), LEONOS_UI_DARK, LEONOS_UI_GRAY);

    for (uint32_t i = 0; i < SERVICEMGR_ROWS; ++i) {
        char pid_text[16];
        uint32_t pos = 0;
        uint32_t y = SERVICEMGR_ROW_Y + i * SERVICEMGR_ROW_H;
        uint32_t flags = (!can_manage || service_rows[i].locked)
                             ? LEONOS_UI_BUTTON_DISABLED
                             : 0;
        pid_text[0] = 0;
        append_u32(pid_text, &pos, sizeof(pid_text), service_rows[i].pid);
        leonos_ui_panel(ui, 24, y, SERVICEMGR_W - 48U, 44U,
                        selected_row == (int32_t)i ? 0x00e7f0ff : LEONOS_UI_WHITE);
        leonos_ui_checkbox(ui, 36, y + 12U,
                           T(service_rows[i].name_en, service_rows[i].name_zh),
                           service_rows[i].enabled, flags);
        leonos_ui_text_clipped(ui, 156, y + 14U, 126,
                               T(service_rows[i].detail_en, service_rows[i].detail_zh),
                               LEONOS_UI_DARK,
                               selected_row == (int32_t)i ? 0x00e7f0ff : LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 300, y + 14U, 92,
                               localized_state(service_rows[i].state),
                               text_eq(service_rows[i].state, "failed")
                                   ? 0x00c00000
                                   : LEONOS_UI_BLACK,
                               selected_row == (int32_t)i ? 0x00e7f0ff : LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 408, y + 14U, 42, pid_text,
                               LEONOS_UI_DARK,
                               selected_row == (int32_t)i ? 0x00e7f0ff : LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 462, y + 14U, SERVICEMGR_W - 498U,
                               service_rows[i].state_detail,
                               LEONOS_UI_BLACK,
                               selected_row == (int32_t)i ? 0x00e7f0ff : LEONOS_UI_WHITE);
    }
    leonos_ui_button(ui, 24, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Refresh", "刷新"), 0);
    leonos_ui_button(ui, 126, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Save", "保存"),
                     can_manage ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 240, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Start", "启动"),
                     can_manage && selected_row >= 0 &&
                             !service_rows[selected_row].locked
                         ? 0
                         : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 342, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H,
                     T("Stop", "停止"),
                     can_manage && selected_row >= 0 &&
                             !service_rows[selected_row].locked
                         ? 0
                         : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 444, SERVICEMGR_H - 66U, 104U, LEONOS_UI_BUTTON_H,
                     T("Restart", "重启"),
                     can_manage && selected_row >= 0 &&
                             !service_rows[selected_row].locked
                         ? 0
                         : LEONOS_UI_BUTTON_DISABLED);
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
        uint32_t row_y = SERVICEMGR_ROW_Y + i * SERVICEMGR_ROW_H;
        if (hit_rect(x, y, 24, row_y, SERVICEMGR_W - 48U, 44U)) {
            selected_row = (int32_t)i;
        }
        if (can_manage && !service_rows[i].locked &&
            hit_rect(x, y, 36, row_y + 8U, 110U, 30U)) {
            selected_row = (int32_t)i;
            service_rows[i].enabled = service_rows[i].enabled ? 0 : 1;
            copy_text(status_text, sizeof(status_text),
                      T("Service setting changed", "服务设置已更改"));
            return;
        }
    }
    if (hit_rect(x, y, 24, SERVICEMGR_H - 66U, 92U, LEONOS_UI_BUTTON_H)) {
        load_config();
        load_state(0);
    } else if (hit_rect(x, y, 126, SERVICEMGR_H - 66U,
                        92U, LEONOS_UI_BUTTON_H)) {
        save_config();
    } else if (hit_rect(x, y, 240, SERVICEMGR_H - 66U,
                        92U, LEONOS_UI_BUTTON_H) &&
               selected_row >= 0) {
        write_command("start", (uint32_t)selected_row);
    } else if (hit_rect(x, y, 342, SERVICEMGR_H - 66U,
                        92U, LEONOS_UI_BUTTON_H) &&
               selected_row >= 0) {
        write_command("stop", (uint32_t)selected_row);
    } else if (hit_rect(x, y, 444, SERVICEMGR_H - 66U,
                        104U, LEONOS_UI_BUTTON_H) &&
               selected_row >= 0) {
        write_command("restart", (uint32_t)selected_row);
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
    load_state(1);
    window_id = leonos_gui_create_app_window_ex(T("Service Manager", "服务管理器"),
                                                T("Services", "服务"),
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
                load_state(1);
                present(window_id, &ui);
            }
        } else {
            unsigned long now = leonos_uptime_ms();
            if (now - last_state_refresh_ms >= 1000UL) {
                last_state_refresh_ms = now;
                load_state(1);
                present(window_id, &ui);
            }
            sleep_ms(20);
        }
    }
}
