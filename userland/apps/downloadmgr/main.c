#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/http.h>
#include <leonos/i18n.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DOWNLOAD_W 680U
#define DOWNLOAD_H 300U
#define URL_X 84U
#define URL_Y 62U
#define URL_W 470U
#define BUTTON_X 568U
#define BUTTON_Y 62U
#define BUTTON_W 86U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DOWNLOAD_W * DOWNLOAD_H];
static char url_input[LEONOS_HTTP_URL_LEN] = "http://example.com/";
static char status_text[160] = "Ready";
static char target_path[LEONOS_FS_PATH_LEN] = "";
static char detail_text[192] = "Enter an http:// URL or open a download link from Browser.";
static char body[LEONOS_HTTP_BODY_MAX + 1U];
static char headers[LEONOS_HTTP_HEADER_MAX + 1U];
static struct leonos_ui_edit_state url_edit;
static uint32_t progress_value;
static uint8_t busy;
static uint8_t done;
static uint8_t failed;

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int starts_with_ignore_case(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (ascii_tolower(text[i]) != ascii_tolower(prefix[i])) {
            return 0;
        }
        ++i;
    }
    return 1;
}

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

static void path_parent(char *dst, uint32_t cap, const char *path)
{
    uint32_t len;
    copy_text(dst, cap, path);
    len = text_len(dst);
    while (len > 3U && dst[len - 1U] != '/') {
        dst[--len] = 0;
    }
    if (len > 3U) {
        dst[len - 1U] = 0;
    }
}

static const char *url_filename(const char *url)
{
    const char *base = url;
    const char *p = url;
    if (!url) {
        return "";
    }
    while (*p && *p != '?' && *p != '#') {
        if (*p == '/') {
            base = p + 1;
        }
        ++p;
    }
    return base;
}

static void sanitize_filename(char *dst, uint32_t cap, const char *src)
{
    uint32_t pos = 0;
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    while (src && src[i] && src[i] != '?' && src[i] != '#' && pos + 1U < cap) {
        char ch = src[i++];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_') {
            append_char(dst, &pos, cap, ch);
        } else {
            append_char(dst, &pos, cap, '_');
        }
    }
    if (!dst[0] || dst[0] == '.') {
        copy_text(dst, cap, "download.bin");
    }
}

static void build_download_dir(char *dst, uint32_t cap)
{
    struct leonos_user_info user;
    if (leonos_auth_current(&user) == 0 && user.home[0]) {
        uint32_t pos = 0;
        dst[0] = 0;
        append_text(dst, &pos, cap, user.home);
        append_text(dst, &pos, cap, "/downloads");
        return;
    }
    copy_text(dst, cap, "0:/tmp");
}

static void build_child_path(char *dst, uint32_t cap, const char *dir,
                             const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, cap, dir);
    if (dir && dir[0] && dir[text_len(dir) - 1U] != '/') {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, name);
}

static void build_numbered_name(char *dst, uint32_t cap, const char *name,
                                uint32_t number)
{
    uint32_t dot = 0;
    uint32_t len = text_len(name);
    uint32_t pos = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    if (!dot) {
        dot = len;
    }
    dst[0] = 0;
    for (uint32_t i = 0; i < dot; ++i) {
        append_char(dst, &pos, cap, name[i]);
    }
    append_text(dst, &pos, cap, "-");
    append_u32(dst, &pos, cap, number);
    for (uint32_t i = dot; i < len; ++i) {
        append_char(dst, &pos, cap, name[i]);
    }
}

static int choose_target_path(char *dst, uint32_t cap, const char *url)
{
    char dir[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    char numbered[LEONOS_FS_NAME_LEN];
    struct leonos_stat st;
    build_download_dir(dir, sizeof(dir));
    (void)mkdir(dir, 0);
    sanitize_filename(name, sizeof(name), url_filename(url));
    for (uint32_t i = 0; i < 100U; ++i) {
        if (i == 0) {
            build_child_path(dst, cap, dir, name);
        } else {
            build_numbered_name(numbered, sizeof(numbered), name, i + 1U);
            build_child_path(dst, cap, dir, numbered);
        }
        if (stat(dst, &st) < 0) {
            return 0;
        }
    }
    return -1;
}

static const char *net_status_name(uint32_t status)
{
    switch (status) {
    case LEONOS_NET_STATUS_OK:
        return T("OK", "成功");
    case LEONOS_NET_STATUS_TCP_TIMEOUT:
        return T("TCP timeout", "TCP 超时");
    case LEONOS_NET_STATUS_DNS_FAILED:
        return T("DNS failed", "DNS 失败");
    case LEONOS_NET_STATUS_DNS_NO_ANSWER:
        return T("No DNS answer", "没有 DNS 应答");
    case LEONOS_NET_STATUS_PROTOCOL_UNSUPPORTED:
        return T("Protocol unsupported", "协议不支持");
    default:
        return T("Network failed", "网络失败");
    }
}

static void set_detail_done(const struct leonos_http_response *response)
{
    uint32_t pos = 0;
    detail_text[0] = 0;
    append_text(detail_text, &pos, sizeof(detail_text), T("Saved ", "已保存 "));
    append_u32(detail_text, &pos, sizeof(detail_text), response->body_len);
    append_text(detail_text, &pos, sizeof(detail_text), T(" bytes to ", " 字节到 "));
    append_text(detail_text, &pos, sizeof(detail_text), target_path);
}

static int save_body(const char *path, const char *data, uint32_t len)
{
    int fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    long wrote;
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, data, len);
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    return (uint32_t)wrote == len ? 0 : -1;
}

static void perform_download(void)
{
    struct leonos_http_response response;
    int ret;
    busy = 1;
    done = 0;
    failed = 0;
    progress_value = 10;
    copy_text(status_text, sizeof(status_text), T("Connecting...", "正在连接..."));
    copy_text(detail_text, sizeof(detail_text), url_input);
    body[0] = 0;
    headers[0] = 0;
    if (!starts_with_ignore_case(url_input, "http://")) {
        failed = 1;
        busy = 0;
        progress_value = 0;
        copy_text(status_text, sizeof(status_text),
                  T("Only http:// downloads are supported.", "目前只支持 http:// 下载。"));
        return;
    }
    ret = leonos_http_get(url_input, LEONOS_HTTP_DEFAULT_TIMEOUT_MS,
                          body, sizeof(body), headers, sizeof(headers),
                          &response);
    progress_value = 72;
    if (ret < 0 || response.net_status != LEONOS_NET_STATUS_OK) {
        failed = 1;
        busy = 0;
        copy_text(status_text, sizeof(status_text),
                  ret < 0 ? T("HTTP client failed", "HTTP 客户端失败")
                          : net_status_name(response.net_status));
        return;
    }
    if (response.http_status < 200U || response.http_status >= 300U) {
        uint32_t pos = 0;
        failed = 1;
        busy = 0;
        status_text[0] = 0;
        append_text(status_text, &pos, sizeof(status_text), T("HTTP status ", "HTTP 状态 "));
        append_u32(status_text, &pos, sizeof(status_text), response.http_status);
        return;
    }
    if (response.flags & LEONOS_HTTP_FLAG_TRUNCATED) {
        failed = 1;
        busy = 0;
        copy_text(status_text, sizeof(status_text),
                  T("Download is too large for v1 client.", "文件超过 v1 下载缓冲限制。"));
        return;
    }
    if (choose_target_path(target_path, sizeof(target_path),
                           response.final_url[0] ? response.final_url : url_input) < 0) {
        failed = 1;
        busy = 0;
        copy_text(status_text, sizeof(status_text),
                  T("Could not choose target file.", "无法选择目标文件。"));
        return;
    }
    path_parent(detail_text, sizeof(detail_text), target_path);
    ret = save_body(target_path, body, response.body_len);
    if (ret < 0) {
        failed = 1;
        busy = 0;
        copy_text(status_text, sizeof(status_text),
                  T("Could not save file.", "无法保存文件。"));
        return;
    }
    progress_value = 100;
    done = 1;
    busy = 0;
    copy_text(status_text, sizeof(status_text), T("Download complete", "下载完成"));
    set_detail_done(&response);
}

static void draw_downloadmgr(struct leonos_ui_surface *ui)
{
    uint32_t state_color = failed ? 0x00b03030U : (done ? 0x00108040U : LEONOS_UI_DARK);
    leonos_ui_rect(ui, 0, 0, DOWNLOAD_W, DOWNLOAD_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, DOWNLOAD_W, DOWNLOAD_H,
                     T("Download Manager", "下载管理器"));
    leonos_ui_text(ui, 24, 38,
                   T("HTTP downloads are saved to the current user's Downloads folder.",
                     "HTTP 下载会保存到当前用户的 Downloads 文件夹。"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, URL_Y + 4U, T("URL:", "地址:"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, URL_X, URL_Y, URL_W, &url_edit, busy ? LEONOS_UI_EDIT_DISABLED : 0);
    leonos_ui_button(ui, BUTTON_X, BUTTON_Y, BUTTON_W, LEONOS_UI_BUTTON_H,
                     T("Download", "下载"), busy ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_text(ui, 24, 112, T("Progress", "进度"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_progress(ui, 104, 108, DOWNLOAD_W - 142, 22,
                       progress_value, 100);
    leonos_ui_text_clipped(ui, 24, 154, DOWNLOAD_W - 48,
                           status_text, state_color, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, 24, 182, DOWNLOAD_W - 48,
                           detail_text, LEONOS_UI_DARK, LEONOS_UI_WHITE);
    if (target_path[0]) {
        leonos_ui_text_clipped(ui, 24, 212, DOWNLOAD_W - 48,
                               target_path, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    }
    leonos_ui_statusbar(ui, DOWNLOAD_H - 28, 28, status_text);
}

static void present(int window_id, struct leonos_ui_surface *ui)
{
    draw_downloadmgr(ui);
    leonos_gui_present_window((uint32_t)window_id, DOWNLOAD_W, DOWNLOAD_H,
                              DOWNLOAD_W, pixels);
}

static int hit_rect(int32_t px, int32_t py, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    return px >= (int32_t)x && py >= (int32_t)y &&
           px < (int32_t)(x + w) && py < (int32_t)(y + h);
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    uint8_t auto_start = 0;
    (void)envp;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        copy_text(url_input, sizeof(url_input), argv[1]);
        auto_start = 1;
    }
    window_id = leonos_gui_create_app_window_ex(T("Download Manager", "下载管理器"),
                                                T("HTTP downloads", "HTTP 下载"),
                                                DOWNLOAD_W, DOWNLOAD_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[downloadmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, DOWNLOAD_W, DOWNLOAD_H, DOWNLOAD_W);
    leonos_ui_edit_state_init(&url_edit, url_input, sizeof(url_input));
    url_edit.focused = !auto_start;
    present(window_id, &ui);
    if (auto_start) {
        perform_download();
        present(window_id, &ui);
    }
    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON &&
                (event.buttons & 1U)) {
                if (leonos_ui_edit_state_handle_mouse(&url_edit, event.x, event.y,
                                                      URL_X, URL_Y, URL_W,
                                                      event.buttons)) {
                    present(window_id, &ui);
                    continue;
                }
                if (!busy && hit_rect(event.x, event.y, BUTTON_X, BUTTON_Y,
                                      BUTTON_W, LEONOS_UI_BUTTON_H)) {
                    perform_download();
                    present(window_id, &ui);
                    continue;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (event.pressed && event.keycode == 1U) {
                    return 0;
                }
                if (event.pressed && event.keycode == LEONOS_KEY_ENTER && !busy) {
                    perform_download();
                    present(window_id, &ui);
                } else if (leonos_ui_edit_state_handle_key(&url_edit,
                                                           event.keycode,
                                                           event.pressed)) {
                    present(window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_FOCUS ||
                event.type == LEONOS_GUI_APP_EVENT_RESIZE) {
                present(window_id, &ui);
            }
        } else {
            sleep_ms(10);
        }
    }
}
