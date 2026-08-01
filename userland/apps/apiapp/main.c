#include <leonos/api.h>
#include <leonos/admin.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <string.h>

#define T(en, zh) leonos_i18n((en), (zh))

#define WIZARD_W 480U
#define WIZARD_H 360U
#define DOWNLOAD_PATH_MAX LEONOS_FS_PATH_LEN
#define DOWNLOAD_STATUS_PATH_MAX 96U
#define DOWNLOAD_UPDATE_BYTES (64U * 1024U)
#define DOWNLOAD_UPDATE_MS 100U
#define INSTALL_UPDATE_BYTES (64U * 1024U)
#define INSTALL_UPDATE_MS 100U
#define API_INSTALL_LOG_PATH "0:/var/log/apiapp-install.log"
#define TASK_STATE_EXITED 3U
#define APIAPP_PATH "0:/system/apps/apiapp/apiapp.elf"

static uint32_t wizard_pixels[WIZARD_W * WIZARD_H];

struct install_state {
    char api_path[LEONOS_API_PATH_MAX];
    char install_path[LEONOS_API_PATH_MAX];
    uint32_t create_shortcut;
    struct leonos_api_info info;
    int step;
};

struct download_state {
    int window_id;
    int worker_pid;
    char output_path[DOWNLOAD_PATH_MAX];
    char status_path[DOWNLOAD_STATUS_PATH_MAX];
    char status[128];
    uint32_t received;
    uint32_t total;
    uint8_t cancelled;
};

struct download_worker_state {
    char status_path[DOWNLOAD_STATUS_PATH_MAX];
    uint32_t last_received;
    unsigned long last_update_ms;
};

struct install_worker_state {
    char status_path[DOWNLOAD_STATUS_PATH_MAX];
    uint32_t last_processed;
    uint32_t total;
    unsigned long last_update_ms;
};

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_text(char *dst, uint32_t *pos, uint32_t capacity,
                        const char *src)
{
    while (src && *src && *pos + 1U < capacity) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = 0;
}

static void append_u32(char *dst, uint32_t *pos, uint32_t capacity,
                       uint32_t value)
{
    char digits[12];
    uint32_t count = 0;
    if (value == 0) {
        if (*pos + 1U < capacity) {
            dst[(*pos)++] = '0';
            dst[*pos] = 0;
        }
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count && *pos + 1U < capacity) {
        dst[(*pos)++] = digits[--count];
    }
    dst[*pos] = 0;
}

static void download_path_for_user(char *dst, uint32_t capacity)
{
    struct leonos_user_info user;
    uint32_t pos = 0;
    dst[0] = 0;
    if (leonos_auth_current(&user) == 0 && user.home[0]) {
        append_text(dst, &pos, capacity, user.home);
        append_text(dst, &pos, capacity, "/downloads");
        return;
    }
    copy_text(dst, capacity, "0:/tmp");
}

static void build_download_path(char *dst, uint32_t capacity)
{
    char directory[LEONOS_FS_PATH_LEN];
    uint32_t pos = 0;
    download_path_for_user(directory, sizeof(directory));
    (void)mkdir(directory, 0);
    append_text(dst, &pos, capacity, directory);
    append_text(dst, &pos, capacity, "/app-");
    append_u32(dst, &pos, capacity, (uint32_t)getpid());
    append_text(dst, &pos, capacity, ".api");
}

static void build_download_status_path(char *dst, uint32_t capacity)
{
    uint32_t pos = 0;
    (void)mkdir("0:/tmp", 0);
    dst[0] = 0;
    append_text(dst, &pos, capacity, "0:/tmp/api_download_");
    append_u32(dst, &pos, capacity, (uint32_t)getpid());
    append_text(dst, &pos, capacity, ".status");
}

static void build_install_status_path(char *dst, uint32_t capacity)
{
    uint32_t pos = 0;
    (void)mkdir("0:/tmp", 0);
    dst[0] = 0;
    append_text(dst, &pos, capacity, "0:/tmp/api_install_");
    append_u32(dst, &pos, capacity, (uint32_t)getpid());
    append_text(dst, &pos, capacity, ".status");
}

static void install_log(const char *message)
{
    int fd;
    long wrote;
    uint32_t len;
    if (!message) {
        return;
    }
    printf("[apiapp] %s\n", message);
    (void)mkdir("0:/var", 0);
    (void)mkdir("0:/var/log", 0);
    fd = open(API_INSTALL_LOG_PATH,
              LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_APPEND, 0);
    if (fd < 0) {
        return;
    }
    len = (uint32_t)strlen(message);
    wrote = write(fd, message, len);
    if (wrote == (long)len) {
        (void)write(fd, "\n", 1);
    }
    close(fd);
}

static void install_log_path(const char *label, const char *path)
{
    char line[LEONOS_FS_PATH_LEN + 48U];
    uint32_t pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), label);
    append_text(line, &pos, sizeof(line), path ? path : "(null)");
    install_log(line);
}

static void install_log_result(const char *label, int result)
{
    char line[96];
    uint32_t pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), label);
    append_u32(line, &pos, sizeof(line),
               result < 0 ? (uint32_t)(-(int64_t)result) : (uint32_t)result);
    install_log(line);
}

static void install_log_progress(uint32_t processed, uint32_t total)
{
    char line[96];
    uint32_t pos = 0;
    line[0] = 0;
    append_text(line, &pos, sizeof(line), "final progress: ");
    append_u32(line, &pos, sizeof(line), processed);
    append_text(line, &pos, sizeof(line), "/");
    append_u32(line, &pos, sizeof(line), total);
    install_log(line);
}

static void build_download_part_path(char *dst, uint32_t capacity,
                                     const char *output_path)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, capacity, output_path);
    append_text(dst, &pos, capacity, ".part");
}

static int write_download_status(const char *path, char state,
                                 uint32_t received, uint32_t total)
{
    char text[64];
    uint32_t pos = 0;
    int fd;
    long wrote;
    if (!path || !path[0]) {
        return -1;
    }
    text[pos++] = state;
    text[pos++] = ' ';
    text[pos] = 0;
    append_u32(text, &pos, sizeof(text), received);
    text[pos++] = ' ';
    text[pos] = 0;
    append_u32(text, &pos, sizeof(text), total);
    text[pos++] = '\n';
    text[pos] = 0;
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return -1;
    }
    wrote = write(fd, text, pos);
    close(fd);
    return wrote == (long)pos ? 0 : -1;
}

static uint32_t parse_u32(const char **text)
{
    uint32_t value = 0;
    while (text && *text && **text >= '0' && **text <= '9') {
        value = value * 10U + (uint32_t)(**text - '0');
        ++(*text);
    }
    return value;
}

static int read_download_status(const char *path, char *state,
                                uint32_t *received, uint32_t *total)
{
    char text[64];
    const char *cursor;
    int fd;
    long got;
    if (!path || !state || !received || !total) {
        return -1;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    got = read(fd, text, sizeof(text) - 1U);
    close(fd);
    if (got < 3) {
        return -1;
    }
    text[got] = 0;
    if (text[1] != ' ') {
        return -1;
    }
    *state = text[0];
    cursor = text + 2;
    *received = parse_u32(&cursor);
    if (*cursor != ' ') {
        return -1;
    }
    ++cursor;
    *total = parse_u32(&cursor);
    return 0;
}

static int download_worker_exited(int pid)
{
    struct leonos_task_info tasks[LEONOS_TASK_MAX];
    uint64_t tick;
    int count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &tick);
    (void)tick;
    if (count < 0) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if ((int)tasks[i].pid == pid) {
            return tasks[i].state == TASK_STATE_EXITED;
        }
    }
    return 1;
}

static void cleanup_download(const struct download_state *state)
{
    char part_path[DOWNLOAD_PATH_MAX];
    if (!state) {
        return;
    }
    build_download_part_path(part_path, sizeof(part_path), state->output_path);
    unlink(part_path);
    unlink(state->status_path);
}

static int hit(int32_t x, int32_t y, uint32_t rx, uint32_t ry,
               uint32_t rw, uint32_t rh)
{
    return x >= (int32_t)rx && y >= (int32_t)ry &&
           x < (int32_t)(rx + rw) && y < (int32_t)(ry + rh);
}

static void draw_download_page(struct leonos_ui_surface *ui,
                               const struct download_state *state)
{
    uint32_t percent = state->total
                           ? (uint32_t)(((uint64_t)state->received * 100U) /
                                        state->total)
                           : 0U;
    leonos_ui_rect(ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 24, T("Downloading Application", "正在下载应用"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 64, state->status, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    if (state->total) {
        leonos_ui_progress(ui, 32, 100, WIZARD_W - 64U, 24, percent, 100);
    } else {
        leonos_ui_activity_bar(ui, 32, 107, WIZARD_W - 64U, 10,
                               (uint32_t)(leonos_uptime_ms() % 1000UL));
    }
    leonos_ui_text(ui, 32, 144,
                   T("Downloaded", "已下载"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    {
        char detail[96];
        uint32_t pos = 0;
        detail[0] = 0;
        append_u32(detail, &pos, sizeof(detail), state->received);
        append_text(detail, &pos, sizeof(detail), T(" bytes", " 字节"));
        if (state->total) {
            append_text(detail, &pos, sizeof(detail), T(" of ", " / "));
            append_u32(detail, &pos, sizeof(detail), state->total);
        }
        leonos_ui_text(ui, 32, 168, detail, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_button(ui, WIZARD_W - 120U, WIZARD_H - 52U, 88U,
                      LEONOS_UI_BUTTON_H, T("Cancel", "取消"), 0);
}

static void draw_install_progress_page(struct leonos_ui_surface *ui,
                                       const struct download_state *state)
{
    uint32_t percent = state->total
                           ? (uint32_t)(((uint64_t)state->received * 100U) /
                                        state->total)
                           : 0U;
    char detail[96];
    uint32_t pos = 0;
    leonos_ui_rect(ui, 0, 0, WIZARD_W, WIZARD_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 24, T("Installing Application", "正在安装应用"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 64, state->status, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    if (state->total) {
        leonos_ui_progress(ui, 32, 100, WIZARD_W - 64U, 24, percent, 100);
    } else {
        leonos_ui_activity_bar(ui, 32, 107, WIZARD_W - 64U, 10,
                               (uint32_t)(leonos_uptime_ms() % 1000UL));
    }
    detail[0] = 0;
    append_u32(detail, &pos, sizeof(detail), state->received);
    append_text(detail, &pos, sizeof(detail), T(" bytes processed", " 字节已处理"));
    if (state->total) {
        append_text(detail, &pos, sizeof(detail), T(" of ", " / "));
        append_u32(detail, &pos, sizeof(detail), state->total);
    }
    leonos_ui_text(ui, 32, 144, detail, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 32, 184,
                   T("Please keep this window open until installation completes.",
                     "请保持此窗口打开，直到安装完成。"),
                   LEONOS_UI_DARK, LEONOS_UI_WHITE);
}

static int download_worker_progress(uint32_t received, uint32_t total,
                                    void *context)
{
    struct download_worker_state *state = (struct download_worker_state *)context;
    unsigned long now;
    if (!state) {
        return -1;
    }
    now = leonos_uptime_ms();
    if (received && received - state->last_received < DOWNLOAD_UPDATE_BYTES &&
        now - state->last_update_ms < DOWNLOAD_UPDATE_MS) {
        return 0;
    }
    state->last_received = received;
    state->last_update_ms = now;
    return write_download_status(state->status_path, 'R', received, total);
}

static int install_worker_progress(uint32_t processed, uint32_t total,
                                   void *context)
{
    struct install_worker_state *state = (struct install_worker_state *)context;
    unsigned long now;
    if (!state) {
        return -1;
    }
    now = leonos_uptime_ms();
    if (processed && processed - state->last_processed < INSTALL_UPDATE_BYTES &&
        now - state->last_update_ms < INSTALL_UPDATE_MS) {
        return 0;
    }
    state->last_processed = processed;
    state->total = total;
    state->last_update_ms = now;
    return write_download_status(state->status_path, 'R', processed, total);
}

static int starts_with_ignore_case(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        char a = text[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int run_download_worker(const char *url, const char *output_path,
                               const char *status_path)
{
    struct download_worker_state state;
    struct leonos_http_response response;
    int ret;
    memset(&state, 0, sizeof(state));
    copy_text(state.status_path, sizeof(state.status_path), status_path);
    if (write_download_status(state.status_path, 'R', 0, 0) < 0) {
        return 1;
    }
    ret = leonos_http_download(url, output_path, LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                               download_worker_progress, &state, &response);
    if (ret == 0 && response.net_status == LEONOS_NET_STATUS_OK &&
        response.http_status >= 200U && response.http_status < 300U) {
        (void)write_download_status(state.status_path, 'D', response.body_len,
                                    response.content_length);
        return 0;
    }
    (void)write_download_status(state.status_path, 'F', state.last_received, 0);
    return 1;
}

static int run_install_worker(const char *api_path, const char *install_path,
                              uint32_t create_shortcut,
                              const char *status_path)
{
    struct install_worker_state state;
    int ret;
    memset(&state, 0, sizeof(state));
    install_log("install worker started");
    install_log_path("api path: ", api_path);
    install_log_path("install path: ", install_path);
    install_log_path("status path: ", status_path);
    copy_text(state.status_path, sizeof(state.status_path), status_path);
    for (;;) {
        char status_state;
        uint32_t processed;
        uint32_t total;
        if (read_download_status(state.status_path, &status_state, &processed,
                                 &total) == 0 && status_state == 'A') {
            break;
        }
        sleep_ms(10);
    }
    /* The parent delegated elevation before publishing the authorization state. */
    if (write_download_status(state.status_path, 'R', 0, 0) < 0) {
        install_log("failed to publish running status");
        return 1;
    }
    install_log("calling leonos_api_install_with_progress");
    ret = leonos_api_install_with_progress(api_path, install_path,
                                            create_shortcut,
                                            install_worker_progress, &state);
    install_log_progress(state.last_processed, state.total);
    install_log_result("leonos_api_install_with_progress result: ", ret);
    (void)write_download_status(state.status_path, ret ? 'D' : 'F',
                                 state.last_processed, state.total);
    install_log(ret ? "install worker completed" : "install worker failed");
    return ret ? 0 : 1;
}

static int install_api_with_progress(int window_id, const char *api_path,
                                     const char *install_path,
                                     uint32_t create_shortcut)
{
    struct download_state state;
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    char status_state = 0;
    char *argv[7];
    int worker_status;
    uint8_t install_complete = 0;
    if (!leonos_admin_elevate()) {
        install_log("parent failed to elevate for installation");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    state.window_id = window_id;
    build_install_status_path(state.status_path, sizeof(state.status_path));
    install_log("installation requested");
    install_log_path("api path: ", api_path);
    install_log_path("install path: ", install_path);
    install_log_path("status path: ", state.status_path);
    unlink(state.status_path);
    copy_text(state.status, sizeof(state.status),
              T("Preparing installation...", "正在准备安装..."));
    leonos_ui_bind(&ui, wizard_pixels, WIZARD_W, WIZARD_H, WIZARD_W);
    argv[0] = APIAPP_PATH;
    argv[1] = "--install-worker";
    argv[2] = (char *)api_path;
    argv[3] = (char *)install_path;
    argv[4] = create_shortcut ? "1" : "0";
    argv[5] = state.status_path;
    argv[6] = 0;
    state.worker_pid = execve(APIAPP_PATH, argv, 0);
    if (state.worker_pid < 0) {
        install_log_result("failed to spawn install worker: ", state.worker_pid);
        return 0;
    }
    if (leonos_auth_delegate_elevation((uint32_t)state.worker_pid) < 0 ||
        write_download_status(state.status_path, 'A', 0, 0) < 0) {
        install_log("failed to authorize install worker");
        (void)leonos_task_kill((uint32_t)state.worker_pid);
        unlink(state.status_path);
        return 0;
    }
    for (;;) {
        uint32_t processed;
        uint32_t total;
        if (read_download_status(state.status_path, &status_state, &processed,
                                 &total) == 0) {
            state.received = processed;
            state.total = total;
            if (status_state == 'R') {
                copy_text(state.status, sizeof(state.status),
                          T("Installing...", "正在安装..."));
            } else if (status_state == 'D') {
                install_complete = 1;
                copy_text(state.status, sizeof(state.status),
                          T("Finalizing installation...", "正在完成安装..."));
            } else if (status_state == 'F') {
                copy_text(state.status, sizeof(state.status),
                          T("Installation failed", "安装失败"));
            }
        }
        if (download_worker_exited(state.worker_pid)) {
            worker_status = 0;
            (void)wait4(state.worker_pid, &worker_status, 0, 0);
            install_log_result("install worker exit status: ", worker_status);
            install_log(install_complete ? "installation completed" :
                                          "installation failed before completion");
            unlink(state.status_path);
            return install_complete;
        }
        draw_install_progress_page(&ui, &state);
        leonos_gui_present_window((uint32_t)window_id, WIZARD_W, WIZARD_H,
                                  WIZARD_W, wizard_pixels);
        event.window_id = (uint32_t)window_id;
        (void)leonos_gui_wait_app_event(&event, 50U);
    }
}

static int wait_for_download_close(struct download_state *state,
                                   struct leonos_ui_surface *ui)
{
    struct leonos_gui_app_event event;
    for (;;) {
        draw_download_page(ui, state);
        leonos_gui_present_window((uint32_t)state->window_id, WIZARD_W,
                                  WIZARD_H, WIZARD_W, wizard_pixels);
        event.window_id = (uint32_t)state->window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0 &&
            (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
             (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
              (event.buttons & 1U) &&
              hit(event.x, event.y, WIZARD_W - 120U, WIZARD_H - 52U,
                  88U, LEONOS_UI_BUTTON_H)))) {
            return 0;
        }
    }
}

static int download_api(const char *url, char *api_path, uint32_t capacity)
{
    struct download_state state;
    struct leonos_gui_app_event event;
    struct leonos_ui_surface ui;
    char status_state = 0;
    char *argv[6];
    int worker_status;
    uint8_t download_complete = 0;
    memset(&state, 0, sizeof(state));
    build_download_path(state.output_path, sizeof(state.output_path));
    build_download_status_path(state.status_path, sizeof(state.status_path));
    if (!state.output_path[0] || strlen(state.output_path) >= capacity) {
        return 0;
    }
    unlink(state.status_path);
    state.window_id = leonos_gui_create_app_window_ex(
        T("API Installer", "API 安装程序"),
        T("Downloading application", "正在下载应用"),
        WIZARD_W, WIZARD_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (state.window_id <= 0) {
        return 0;
    }
    copy_text(state.status, sizeof(state.status),
              T("Preparing download...", "正在准备下载..."));
    leonos_ui_bind(&ui, wizard_pixels, WIZARD_W, WIZARD_H, WIZARD_W);
    draw_download_page(&ui, &state);
    leonos_gui_present_window((uint32_t)state.window_id, WIZARD_W, WIZARD_H,
                              WIZARD_W, wizard_pixels);
    argv[0] = APIAPP_PATH;
    argv[1] = "--download-worker";
    argv[2] = (char *)url;
    argv[3] = state.output_path;
    argv[4] = state.status_path;
    argv[5] = 0;
    state.worker_pid = execve(APIAPP_PATH, argv, 0);
    if (state.worker_pid < 0) {
        copy_text(state.status, sizeof(state.status),
                  T("Could not start download", "无法启动下载"));
        (void)wait_for_download_close(&state, &ui);
        leonos_gui_destroy_app_window((uint32_t)state.window_id);
        return 0;
    }

    for (;;) {
        uint32_t received;
        uint32_t total;
        if (read_download_status(state.status_path, &status_state, &received,
                                 &total) == 0) {
            state.received = received;
            state.total = total;
            if (status_state == 'R') {
                copy_text(state.status, sizeof(state.status),
                          total ? T("Downloading...", "正在下载...")
                                : T("Connecting...", "正在连接..."));
            } else if (status_state == 'D') {
                download_complete = 1;
                copy_text(state.status, sizeof(state.status),
                          T("Finalizing download...", "正在完成下载..."));
            } else if (status_state == 'F') {
                copy_text(state.status, sizeof(state.status),
                          T("Download failed", "下载失败"));
            }
        }
        if (download_worker_exited(state.worker_pid)) {
            worker_status = 0;
            (void)wait4(state.worker_pid, &worker_status, 0, 0);
            state.worker_pid = 0;
            if (download_complete) {
                copy_text(api_path, capacity, state.output_path);
                unlink(state.status_path);
                leonos_gui_destroy_app_window((uint32_t)state.window_id);
                return 1;
            }
            break;
        }
        draw_download_page(&ui, &state);
        leonos_gui_present_window((uint32_t)state.window_id, WIZARD_W,
                                  WIZARD_H, WIZARD_W, wizard_pixels);
        event.window_id = (uint32_t)state.window_id;
        if (leonos_gui_wait_app_event(&event, 50U) > 0 &&
            (event.type == LEONOS_GUI_APP_EVENT_CLOSE ||
             (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
              (event.buttons & 1U) &&
              hit(event.x, event.y, WIZARD_W - 120U, WIZARD_H - 52U,
                  88U, LEONOS_UI_BUTTON_H)))) {
            state.cancelled = 1;
            break;
        }
    }
    if (state.worker_pid > 0) {
        if (state.cancelled) {
            (void)leonos_task_kill((uint32_t)state.worker_pid);
        }
    }
    cleanup_download(&state);
    copy_text(state.status, sizeof(state.status),
              state.cancelled ? T("Download cancelled", "下载已取消")
                              : T("Download failed", "下载失败"));
    (void)wait_for_download_close(&state, &ui);
    leonos_gui_destroy_app_window((uint32_t)state.window_id);
    return 0;
}

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
                        result = install_api_with_progress(
                            window_id, state.api_path, state.install_path,
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
    char downloaded_path[LEONOS_API_PATH_MAX];
    if (argc == 5 && argv && argv[1] && argv[2] && argv[3] && argv[4] &&
        strcmp(argv[1], "--download-worker") == 0) {
        return run_download_worker(argv[2], argv[3], argv[4]);
    }
    if (argc == 6 && argv && argv[1] && argv[2] && argv[3] && argv[4] &&
        argv[5] && strcmp(argv[1], "--install-worker") == 0) {
        return run_install_worker(argv[2], argv[3],
                                  strcmp(argv[4], "1") == 0, argv[5]);
    }
    if (argc < 2 || !argv[1] || !argv[1][0]) {
        return 1;
    }
    if (starts_with_ignore_case(argv[1], "http://") ||
        starts_with_ignore_case(argv[1], "https://")) {
        if (!download_api(argv[1], downloaded_path, sizeof(downloaded_path))) {
            return 1;
        }
        return run_wizard(downloaded_path);
    }
    return run_wizard(argv[1]);
}
