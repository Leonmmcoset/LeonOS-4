#include "desktop.h"

#define DESKTOP_ITEM_LABEL_LINES 2
#define DESKTOP_SHORTCUT_MAX_BYTES 384U

static uint32_t desktop_text_len(const char *text)
{
    uint32_t len = 0;
    while (text && text[len]) {
        ++len;
    }
    return len;
}

static int desktop_utf8_cont(uint8_t byte)
{
    return (byte & 0xc0u) == 0x80u;
}

static uint32_t desktop_decode_utf8(const char *text, uint32_t len,
                                    uint32_t off, uint32_t *byte_len)
{
    const uint8_t *s = (const uint8_t *)text;
    uint8_t b0;
    if (byte_len) {
        *byte_len = 1;
    }
    if (!text || off >= len) {
        return 0xfffdu;
    }
    b0 = s[off];
    if (b0 < 0x80u) {
        return b0;
    }
    if (b0 < 0xc2u) {
        return 0xfffdu;
    }
    if (b0 < 0xe0u) {
        if (off + 1u >= len || !desktop_utf8_cont(s[off + 1u])) {
            return 0xfffdu;
        }
        if (byte_len) {
            *byte_len = 2;
        }
        return ((uint32_t)(b0 & 0x1fu) << 6) | (uint32_t)(s[off + 1u] & 0x3fu);
    }
    if (b0 < 0xf0u) {
        uint8_t b1;
        uint8_t b2;
        if (off + 2u >= len) {
            return 0xfffdu;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        if (!desktop_utf8_cont(b1) || !desktop_utf8_cont(b2)) {
            return 0xfffdu;
        }
        if (byte_len) {
            *byte_len = 3;
        }
        return ((uint32_t)(b0 & 0x0fu) << 12) |
               ((uint32_t)(b1 & 0x3fu) << 6) |
               (uint32_t)(b2 & 0x3fu);
    }
    if (b0 < 0xf5u) {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        if (off + 3u >= len) {
            return 0xfffdu;
        }
        b1 = s[off + 1u];
        b2 = s[off + 2u];
        b3 = s[off + 3u];
        if (!desktop_utf8_cont(b1) || !desktop_utf8_cont(b2) ||
            !desktop_utf8_cont(b3)) {
            return 0xfffdu;
        }
        if (byte_len) {
            *byte_len = 4;
        }
        return ((uint32_t)(b0 & 0x07u) << 18) |
               ((uint32_t)(b1 & 0x3fu) << 12) |
               ((uint32_t)(b2 & 0x3fu) << 6) |
               (uint32_t)(b3 & 0x3fu);
    }
    return 0xfffdu;
}

static int desktop_is_wide_codepoint(uint32_t cp)
{
    return (cp >= 0x1100u && cp <= 0x115fu) ||
           cp == 0x2329u || cp == 0x232au ||
           (cp >= 0x2e80u && cp <= 0xa4cfu) ||
           (cp >= 0xac00u && cp <= 0xd7a3u) ||
           (cp >= 0xf900u && cp <= 0xfaffu) ||
           (cp >= 0x20000u && cp <= 0x3fffdu) ||
           (cp >= 0xfe10u && cp <= 0xfe19u) ||
           (cp >= 0xfe30u && cp <= 0xfe6fu) ||
           (cp >= 0xff00u && cp <= 0xff60u) ||
           (cp >= 0xffe0u && cp <= 0xffe6u);
}

static uint32_t desktop_label_cell_width(uint32_t cp)
{
    if (cp == 0 || cp == '\n' || cp == '\r') {
        return 0;
    }
    if (cp == '\t') {
        return 4;
    }
    return desktop_is_wide_codepoint(cp) ? 2u : 1u;
}

static int desktop_path_is_root(const char *path)
{
    return text_eq(path, "/");
}

static void desktop_build_child_path(char *dst, uint32_t cap,
                                     const char *parent, const char *name)
{
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    append_text(dst, &pos, cap, parent);
    if (!desktop_path_is_root(parent)) {
        append_char(dst, &pos, cap, '/');
    }
    append_text(dst, &pos, cap, name);
}

static int desktop_shortcut_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int desktop_shortcut_line_starts_with(const char *line, uint32_t len,
                                             const char *prefix)
{
    uint32_t i = 0;
    while (prefix && prefix[i]) {
        if (i >= len || line[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return i > 0;
}

static int desktop_parse_shortcut_target(const char *buffer, uint32_t len,
                                         char *target, uint32_t cap)
{
    uint32_t pos = 0;
    if (!buffer || !target || cap == 0) {
        return -22;
    }
    target[0] = 0;
    while (pos < len) {
        uint32_t start = pos;
        uint32_t line_len;
        uint32_t text_start;
        uint32_t text_end;
        uint32_t out = 0;
        while (pos < len && buffer[pos] != '\n' && buffer[pos] != '\r') {
            ++pos;
        }
        line_len = pos - start;
        while (pos < len && (buffer[pos] == '\n' || buffer[pos] == '\r')) {
            ++pos;
        }
        text_start = start;
        text_end = start + line_len;
        while (text_start < text_end && desktop_shortcut_is_space(buffer[text_start])) {
            ++text_start;
        }
        while (text_end > text_start && desktop_shortcut_is_space(buffer[text_end - 1])) {
            --text_end;
        }
        if (text_start >= text_end || buffer[text_start] == '#') {
            continue;
        }
        if (desktop_shortcut_line_starts_with(buffer + text_start,
                                              text_end - text_start, "target=")) {
            text_start += 7;
        }
        while (text_start < text_end && out + 1 < cap) {
            target[out++] = buffer[text_start++];
        }
        target[out] = 0;
        return target[0] ? 0 : -22;
    }
    return -22;
}

static int desktop_read_shortcut_target(const char *shortcut_path, char *target, uint32_t cap)
{
    char buffer[DESKTOP_SHORTCUT_MAX_BYTES];
    int fd;
    long got;
    uint32_t len = 0;
    if (!shortcut_path || !target || cap == 0) {
        return -22;
    }
    target[0] = 0;
    fd = open(shortcut_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    while (len + 1 < sizeof(buffer)) {
        got = read(fd, buffer + len, sizeof(buffer) - len - 1);
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
    return desktop_parse_shortcut_target(buffer, len, target, cap);
}

static uint32_t desktop_grid_rows(void)
{
    uint32_t bottom = taskbar_y();
    if (bottom <= DESKTOP_ITEM_GRID_Y) {
        return 0;
    }
    return (bottom - DESKTOP_ITEM_GRID_Y) / DESKTOP_ITEM_CELL_H;
}

static uint32_t desktop_grid_cols(void)
{
    if (fb_w() <= DESKTOP_ITEM_GRID_X) {
        return 0;
    }
    return (fb_w() - DESKTOP_ITEM_GRID_X) / DESKTOP_ITEM_CELL_W;
}

static uint32_t desktop_visible_item_count(void)
{
    uint32_t rows = desktop_grid_rows();
    uint32_t cols = desktop_grid_cols();
    uint32_t limit = rows * cols;
    if (!rows || !cols) {
        return 0;
    }
    return desktop_item_count < limit ? desktop_item_count : limit;
}

static struct rect desktop_item_rect(uint32_t index)
{
    uint32_t rows = desktop_grid_rows();
    uint32_t row = rows ? index % rows : 0;
    uint32_t col = rows ? index / rows : 0;
    return rect_make((int)(DESKTOP_ITEM_GRID_X + col * DESKTOP_ITEM_CELL_W),
                     (int)(DESKTOP_ITEM_GRID_Y + row * DESKTOP_ITEM_CELL_H),
                     DESKTOP_ITEM_CELL_W, DESKTOP_ITEM_CELL_H);
}

static int32_t desktop_item_at(uint32_t x, uint32_t y)
{
    uint32_t visible = desktop_visible_item_count();
    for (uint32_t i = 0; i < visible; ++i) {
        struct rect r = desktop_item_rect(i);
        if (hit_rect(x, y, r.x, r.y, (uint32_t)r.w, (uint32_t)r.h)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static void desktop_icon_path_for_target(const char *path, char *dst, uint32_t dst_len)
{
    struct leonos_stat st;
    const char *app_path;
    if (!dst || !dst_len) {
        return;
    }
    dst[0] = 0;
    if (!path || !path[0]) {
        return;
    }
    if (text_ends_with(path, ".elf")) {
        desktop_icon_path_for_app(path, dst, dst_len);
        return;
    }
    if (stat(path, &st) == 0 && st.type == LEONOS_FS_TYPE_DIR) {
        desktop_icon_path_for_app("/system/apps/fileman/fileman.elf", dst, dst_len);
        return;
    }
    app_path = leonos_launch_resolve_default_app_for_path(path);
    if (app_path && app_path[0]) {
        desktop_icon_path_for_app(app_path, dst, dst_len);
    }
}

static void desktop_icon_path_for_entry(const struct leonos_dir_entry *entry,
                                        const char *path, char *dst, uint32_t dst_len)
{
    char target[LEONOS_FS_PATH_LEN];
    if (!dst || !dst_len) {
        return;
    }
    dst[0] = 0;
    if (entry && entry->type == LEONOS_FS_TYPE_DIR) {
        desktop_icon_path_for_target(path, dst, dst_len);
        return;
    }
    if (entry && entry->type == LEONOS_FS_TYPE_FILE &&
        text_ends_with(entry->name, ".lnk") &&
        desktop_read_shortcut_target(path, target, sizeof(target)) == 0) {
        desktop_icon_path_for_target(target, dst, dst_len);
        return;
    }
    if (entry && entry->type == LEONOS_FS_TYPE_FILE) {
        desktop_icon_path_for_target(path, dst, dst_len);
    }
}

static void desktop_copy_item_label(char *dst, uint32_t dst_len,
                                    const struct leonos_dir_entry *entry)
{
    uint32_t len;
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = 0;
    if (!entry || !entry->name[0]) {
        return;
    }
    len = desktop_text_len(entry->name);
    if (entry->type == LEONOS_FS_TYPE_FILE &&
        text_ends_with(entry->name, ".lnk") && len > 4U) {
        len -= 4U;
    }
    if (len >= dst_len) {
        len = dst_len - 1U;
    }
    for (uint32_t i = 0; i < len; ++i) {
        dst[i] = entry->name[i];
    }
    dst[len] = 0;
}

static uint32_t desktop_label_next_line(const char *text, uint32_t len,
                                        uint32_t start, uint32_t max_cells)
{
    uint32_t pos = start;
    uint32_t cells = 0;
    if (!max_cells) {
        max_cells = 1;
    }
    while (pos < len) {
        uint32_t byte_len = 1;
        uint32_t cp = desktop_decode_utf8(text, len, pos, &byte_len);
        uint32_t cw = desktop_label_cell_width(cp);
        if (!cw) {
            cw = 1;
        }
        if (cells && cells + cw > max_cells) {
            break;
        }
        cells += cw;
        pos += byte_len ? byte_len : 1u;
        if (cells >= max_cells) {
            break;
        }
    }
    return pos > start ? pos : (start < len ? start + 1 : start);
}

static uint32_t desktop_label_offset_for_cells(const char *text, uint32_t len,
                                               uint32_t start, uint32_t max_cells)
{
    uint32_t pos = start;
    uint32_t cells = 0;
    while (text && pos < len) {
        uint32_t byte_len = 1;
        uint32_t cp = desktop_decode_utf8(text, len, pos, &byte_len);
        uint32_t cw = desktop_label_cell_width(cp);
        if (!cw) {
            cw = 1;
        }
        if (cells + cw > max_cells) {
            break;
        }
        cells += cw;
        pos += byte_len ? byte_len : 1u;
    }
    return pos;
}

static void desktop_copy_label_line(char *dst, uint32_t dst_len,
                                    const char *text, uint32_t start,
                                    uint32_t end, uint32_t max_cells,
                                    uint8_t ellipsis)
{
    uint32_t pos = 0;
    uint32_t src = start;
    if (!dst || !dst_len) {
        return;
    }
    dst[0] = 0;
    if (ellipsis) {
        end = max_cells > 3
                  ? desktop_label_offset_for_cells(text, end, start, max_cells - 3)
                  : start;
    }
    while (text && src < end && pos + 1 < dst_len) {
        dst[pos++] = text[src++];
    }
    if (ellipsis && dst_len > 4) {
        while (pos + 4 > dst_len && pos > 0) {
            --pos;
        }
        dst[pos++] = '.';
        dst[pos++] = '.';
        dst[pos++] = '.';
    }
    dst[pos < dst_len ? pos : dst_len - 1] = 0;
}

static uint32_t desktop_label_visible_lines(const char *text, uint32_t label_w)
{
    uint32_t len = desktop_text_len(text);
    uint32_t pos = 0;
    uint32_t max_cells = leonos_ui_text_fit_chars(label_w);
    uint32_t lines = 0;
    if (!len) {
        return 1;
    }
    while (pos < len && lines < DESKTOP_ITEM_LABEL_LINES) {
        uint32_t next = desktop_label_next_line(text, len, pos, max_cells);
        pos = next > pos ? next : pos + 1;
        ++lines;
    }
    return lines ? lines : 1;
}

static void desktop_draw_centered_label_line(const char *text, uint32_t x, uint32_t y,
                                             uint32_t w, uint32_t fg, uint32_t bg)
{
    uint32_t text_w = leonos_ui_text_width(text ? text : "");
    uint32_t draw_x = x;
    uint32_t draw_w = w;
    if (text_w < w) {
        uint32_t inset = (w - text_w) / 2;
        draw_x = x + inset;
        draw_w = w > inset ? w - inset : w;
    }
    if (wallpaper_loaded && bg == LEONOS_UI_DESKTOP) {
        text_draw_transparent_i((int)draw_x + 1, (int)y + 1, text ? text : "",
                                0x00101d32u);
        text_draw_transparent_i((int)draw_x, (int)y, text ? text : "", fg);
        return;
    }
    leonos_ui_text_clipped(&ui, draw_x, y, draw_w, text ? text : "", fg, bg);
}

static void desktop_draw_item_label(const char *text, uint32_t x, uint32_t y,
                                    uint32_t w, int selected)
{
    uint32_t len = desktop_text_len(text);
    uint32_t pos = 0;
    uint32_t line = 0;
    uint32_t max_cells = leonos_ui_text_fit_chars(w);
    uint32_t fg = LEONOS_UI_WHITE;
    uint32_t bg = selected
                      ? LEONOS_UI_ACTIVE_TITLE
                      : LEONOS_UI_DESKTOP;
    if (!max_cells) {
        max_cells = 1;
    }
    if (!len) {
        desktop_draw_centered_label_line("", x, y, w, fg, bg);
        return;
    }
    while (pos < len && line < DESKTOP_ITEM_LABEL_LINES) {
        char label[LEONOS_FS_NAME_LEN + 4];
        uint32_t next = desktop_label_next_line(text, len, pos, max_cells);
        uint8_t ellipsis = (line + 1 == DESKTOP_ITEM_LABEL_LINES && next < len) ? 1 : 0;
        desktop_copy_label_line(label, sizeof(label), text, pos, next, max_cells, ellipsis);
        desktop_draw_centered_label_line(label, x, y + line * LEONOS_FONT_H, w, fg, bg);
        pos = next > pos ? next : pos + 1;
        ++line;
    }
}

static void desktop_append_signed(char *buf, uint32_t *pos, uint32_t cap, int value)
{
    if (value < 0) {
        append_char(buf, pos, cap, '-');
        value = -value;
    }
    append_dec(buf, pos, cap, (uint32_t)value);
}

static int desktop_permission_error(int value)
{
    return value == -LEONOS_EPERM || value == -LEONOS_EACCES;
}

static const char *desktop_launch_error_text(int code)
{
    if (desktop_permission_error(code)) {
        return leonos_i18n("Permission denied", "权限被拒绝");
    }
    switch (code) {
    case LEONOS_LAUNCH_ERR_EMPTY:
        return leonos_i18n("No item selected.", "未选择项目。");
    case LEONOS_LAUNCH_ERR_TOO_MANY_ARGS:
        return leonos_i18n("Too many launch arguments.", "启动参数过多。");
    case LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE:
        return leonos_i18n("Launch command has an unfinished quote.", "启动命令存在未闭合引号。");
    case LEONOS_LAUNCH_ERR_NOT_FOUND:
        return leonos_i18n("Program or path not found.", "程序或路径不存在。");
    case LEONOS_LAUNCH_ERR_NO_ASSOCIATION:
        return leonos_i18n("No file association for this item.", "此项目没有默认打开方式。");
    case LEONOS_LAUNCH_ERR_INVALID_SHORTCUT:
        return leonos_i18n("Invalid shortcut.", "快捷方式无效。");
    case LEONOS_LAUNCH_ERR_SHORTCUT_LOOP:
        return leonos_i18n("Shortcut loop detected.", "检测到快捷方式循环。");
    case LEONOS_LAUNCH_ERR_EXISTS:
        return leonos_i18n("Shortcut already exists.", "快捷方式已存在。");
    case LEONOS_LAUNCH_ERR_ALREADY_RUNNING:
        return leonos_i18n("Desktop is already running.", "桌面已在运行。");
    default:
        return 0;
    }
}

static void desktop_show_error_code(const char *title, const char *prefix, int code)
{
    const char *launch_error = desktop_launch_error_text(code);
    char buf[DESKTOP_MESSAGE_TEXT_LEN];
    uint32_t pos = 0;
    if (launch_error) {
        desktop_show_message(title, launch_error);
        return;
    }
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), prefix ? prefix : leonos_i18n("Operation failed", "操作失败"));
    append_text(buf, &pos, sizeof(buf), " ret=");
    desktop_append_signed(buf, &pos, sizeof(buf), code);
    desktop_show_message(title, buf);
}

static void desktop_context_menu_set_active(uint8_t active)
{
    desktop_context_menu_active = active;
    desktop_context_menu_opening = active;
    desktop_context_menu_animating = active;
    desktop_context_menu_anim_start = leonos_uptime_ms();
    full_redraw_pending = 1;
}

static void desktop_build_context_menu_items(struct leonos_ui_context_menu_item *items)
{
    items[0] = (struct leonos_ui_context_menu_item){
        leonos_i18n("Refresh", "刷新"), DESKTOP_CONTEXT_ACTION_REFRESH, 0};
    items[1] = (struct leonos_ui_context_menu_item){
        leonos_i18n("Open Desktop Folder", "打开桌面文件夹"),
        DESKTOP_CONTEXT_ACTION_OPEN_FOLDER, 0};
    items[2] = (struct leonos_ui_context_menu_item){
        leonos_i18n("Create Shortcut", "创建快捷方式"),
        DESKTOP_CONTEXT_ACTION_CREATE_SHORTCUT, 0};
}

static void desktop_show_context_menu(uint32_t x, uint32_t y)
{
    uint32_t menu_h = leonos_ui_context_menu_height(DESKTOP_CONTEXT_MENU_COUNT);
    desktop_context_menu_x = x;
    desktop_context_menu_y = y;
    if (desktop_context_menu_x + DESKTOP_CONTEXT_MENU_W > fb_w()) {
        desktop_context_menu_x = fb_w() > DESKTOP_CONTEXT_MENU_W
                                     ? fb_w() - DESKTOP_CONTEXT_MENU_W
                                     : 0;
    }
    if (desktop_context_menu_y + menu_h > taskbar_y()) {
        desktop_context_menu_y = taskbar_y() > menu_h ? taskbar_y() - menu_h : 0;
    }
    start_menu_set_open(0);
    desktop_context_menu_set_active(1);
}

static void desktop_open_path(const char *path)
{
    int pid;
    char *argv[2];
    if (!path || !path[0]) {
        desktop_show_message(leonos_i18n("Desktop", "桌面"),
                             leonos_i18n("No item selected.", "未选择项目。"));
        return;
    }
    argv[0] = (char *)path;
    argv[1] = 0;
    pid = leonos_launch_argv(argv);
    if (pid < 0) {
        desktop_show_error_code(leonos_i18n("Open Failed", "打开失败"),
                                leonos_i18n("Open failed", "打开失败"), pid);
    }
}

static void desktop_open_folder(void)
{
    int ret = 0;
    if (!desktop_folder_path[0]) {
        ret = desktop_refresh_items();
    }
    if (ret < 0 || !desktop_folder_path[0]) {
        desktop_show_error_code(leonos_i18n("Desktop", "桌面"),
                                leonos_i18n("Cannot open Desktop folder", "无法打开桌面文件夹"),
                                ret < 0 ? ret : -LEONOS_EACCES);
        return;
    }
    desktop_open_path(desktop_folder_path);
}

static void desktop_open_shortcut_input(void)
{
    desktop_shortcut_input_active = 1;
    desktop_shortcut_shift_down = 0;
    desktop_shortcut_target[0] = 0;
    desktop_message_active = 0;
    desktop_context_menu_active = 0;
    desktop_context_menu_animating = 0;
    start_menu_set_open(0);
    full_redraw_pending = 1;
}

static void desktop_append_shortcut_input_char(char ch)
{
    uint32_t len = desktop_text_len(desktop_shortcut_target);
    if (len + 1 >= sizeof(desktop_shortcut_target)) {
        return;
    }
    desktop_shortcut_target[len] = ch;
    desktop_shortcut_target[len + 1] = 0;
    full_redraw_pending = 1;
}

static void desktop_create_shortcut_from_input(void)
{
    char shortcut_path[LEONOS_FS_PATH_LEN];
    int ret;
    if (!desktop_shortcut_target[0]) {
        desktop_shortcut_input_active = 0;
        desktop_show_message(leonos_i18n("Create Shortcut", "创建快捷方式"),
                             leonos_i18n("Target path is empty.", "目标路径为空。"));
        return;
    }
    if (!desktop_folder_path[0]) {
        ret = desktop_refresh_items();
        if (ret < 0 || !desktop_folder_path[0]) {
            desktop_shortcut_input_active = 0;
            desktop_show_error_code(leonos_i18n("Create Shortcut", "创建快捷方式"),
                                    leonos_i18n("Cannot open Desktop folder", "无法打开桌面文件夹"),
                                    ret < 0 ? ret : -LEONOS_EACCES);
            return;
        }
    }
    ret = leonos_launch_create_shortcut_in_dir(desktop_folder_path,
                                               desktop_shortcut_target,
                                               shortcut_path, sizeof(shortcut_path));
    if (ret < 0) {
        desktop_shortcut_input_active = 0;
        desktop_show_error_code(leonos_i18n("Create Shortcut", "创建快捷方式"),
                                leonos_i18n("Create shortcut failed", "创建快捷方式失败"),
                                ret);
        return;
    }
    desktop_shortcut_input_active = 0;
    desktop_shortcut_target[0] = 0;
    (void)desktop_refresh_items();
    full_redraw_pending = 1;
}

static void desktop_run_context_action(uint32_t action)
{
    int ret;
    if (action == DESKTOP_CONTEXT_ACTION_REFRESH) {
        ret = desktop_refresh_items();
        if (ret < 0) {
            desktop_show_error_code(leonos_i18n("Desktop", "桌面"),
                                    leonos_i18n("Refresh failed", "刷新失败"), ret);
        }
        full_redraw_pending = 1;
    } else if (action == DESKTOP_CONTEXT_ACTION_OPEN_FOLDER) {
        desktop_open_folder();
    } else if (action == DESKTOP_CONTEXT_ACTION_CREATE_SHORTCUT) {
        desktop_open_shortcut_input();
    }
}

void desktop_items_clear(void)
{
    desktop_item_count = 0;
    desktop_folder_path[0] = 0;
    desktop_items_ready = 0;
    desktop_selected_item = -1;
    desktop_last_click_item = -1;
    desktop_last_click_ms = 0;
    desktop_context_menu_active = 0;
    desktop_context_menu_animating = 0;
    desktop_context_menu_opening = 0;
}

int desktop_refresh_items(void)
{
    struct leonos_user_info user;
    struct leonos_dir_entry entry;
    int fd;
    int ret;
    int auth_ret;
    uint32_t count = 0;

    desktop_items_clear();
    user = (struct leonos_user_info){0};
    auth_ret = leonos_auth_current(&user);
    if (auth_ret < 0) {
        return auth_ret;
    }
    if (!user.uid || !user.home[0]) {
        return -LEONOS_EACCES;
    }
    desktop_build_child_path(desktop_folder_path, sizeof(desktop_folder_path),
                             user.home, "desktop");
    fd = open(desktop_folder_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    for (;;) {
        if (count >= DESKTOP_ITEM_MAX) {
            break;
        }
        ret = leonos_readdir(fd, &entry);
        if (ret < 0) {
            close(fd);
            desktop_items_clear();
            return ret;
        }
        if (ret == 0) {
            break;
        }
        /* Desktop follows the usual dotfile convention: keep private files
         * and directories addressable through File Manager, but never turn
         * them (including ext2's . and .. entries) into desktop icons. */
        if (entry.name[0] == '.') {
            continue;
        }
        desktop_items[count].entry = entry;
        desktop_copy_item_label(desktop_items[count].label,
                                sizeof(desktop_items[count].label), &entry);
        desktop_build_child_path(desktop_items[count].path,
                                 sizeof(desktop_items[count].path),
                                 desktop_folder_path, entry.name);
        desktop_icon_path_for_entry(&entry, desktop_items[count].path,
                                    desktop_items[count].icon_path,
                                    sizeof(desktop_items[count].icon_path));
        ++count;
    }
    close(fd);
    desktop_item_count = count;
    desktop_items_ready = 1;
    desktop_items_retry_ms = 0;
    return 0;
}

void draw_desktop_items(struct rect dirty)
{
    uint32_t visible = desktop_visible_item_count();
    for (uint32_t i = 0; i < visible; ++i) {
        struct rect r = desktop_item_rect(i);
        int selected = desktop_selected_item == (int32_t)i;
        int icon_x = r.x + (r.w - APP_ICON_LARGE_W) / 2;
        int icon_y = r.y + 8;
        int label_y = r.y + 48;
        uint32_t label_w = r.w > 8 ? (uint32_t)r.w - 8 : 0;
        uint32_t label_lines = desktop_label_visible_lines(desktop_items[i].label, label_w);
        if (!rect_intersects(dirty, r)) {
            continue;
        }
        if (selected) {
            rect_fill_i(r.x + 4, label_y - 2, r.w - 8,
                        (int)(label_lines * LEONOS_FONT_H + 4),
                        LEONOS_UI_ACTIVE_TITLE);
        }
        draw_app_icon_large(desktop_items[i].icon_path, icon_x, icon_y);
        desktop_draw_item_label(desktop_items[i].label,
                                (uint32_t)r.x + 4, (uint32_t)label_y,
                                label_w, selected);
    }
}

void draw_desktop_context_menu(void)
{
    struct leonos_ui_context_menu_item items[DESKTOP_CONTEXT_MENU_COUNT];
    uint32_t progress = 1000;
    if (!desktop_context_menu_active && !desktop_context_menu_animating) {
        return;
    }
    desktop_build_context_menu_items(items);
    if (desktop_context_menu_animating) {
        progress = leonos_ui_anim_progress(leonos_uptime_ms(),
                                           desktop_context_menu_anim_start, 120);
        if (progress >= 1000) {
            desktop_context_menu_animating = 0;
            progress = desktop_context_menu_active ? 1000 : 0;
        }
    }
    leonos_ui_context_menu_animated(&ui, desktop_context_menu_x, desktop_context_menu_y,
                                    DESKTOP_CONTEXT_MENU_W, items,
                                    DESKTOP_CONTEXT_MENU_COUNT, progress);
}

void desktop_show_message(const char *title, const char *message)
{
    copy_text(desktop_message_title, sizeof(desktop_message_title),
              title ? title : leonos_i18n("Desktop", "桌面"));
    copy_text(desktop_message_text, sizeof(desktop_message_text),
              message ? message : "");
    desktop_message_active = 1;
    desktop_shortcut_input_active = 0;
    desktop_context_menu_active = 0;
    desktop_context_menu_animating = 0;
    start_menu_set_open(0);
    full_redraw_pending = 1;
}

void draw_desktop_message(void)
{
    uint32_t x;
    uint32_t y;
    if (!desktop_message_active) {
        return;
    }
    x = fb_w() > DESKTOP_MESSAGE_W ? (fb_w() - DESKTOP_MESSAGE_W) / 2 : 0;
    y = fb_h() > DESKTOP_MESSAGE_H ? (fb_h() - DESKTOP_MESSAGE_H) / 2 : 0;
    rect_fill_i((int)x + 5, (int)y + 5, DESKTOP_MESSAGE_W, DESKTOP_MESSAGE_H, 0x00404040);
    leonos_ui_dialog(&ui, x, y, DESKTOP_MESSAGE_W, DESKTOP_MESSAGE_H, desktop_message_title);
    leonos_ui_text_clipped(&ui, x + 20, y + 50, DESKTOP_MESSAGE_W - 40,
                           desktop_message_text, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_button(&ui, x + DESKTOP_MESSAGE_W / 2 - 36,
                     y + DESKTOP_MESSAGE_H - 38, 72, LEONOS_UI_BUTTON_H, "OK", 0);
}

int desktop_handle_message_click(uint32_t x, uint32_t y)
{
    uint32_t dialog_x;
    uint32_t dialog_y;
    if (!desktop_message_active) {
        return 0;
    }
    dialog_x = fb_w() > DESKTOP_MESSAGE_W ? (fb_w() - DESKTOP_MESSAGE_W) / 2 : 0;
    dialog_y = fb_h() > DESKTOP_MESSAGE_H ? (fb_h() - DESKTOP_MESSAGE_H) / 2 : 0;
    if (hit_rect(x, y, (int)dialog_x + DESKTOP_MESSAGE_W / 2 - 36,
                 (int)dialog_y + DESKTOP_MESSAGE_H - 38, 72, LEONOS_UI_BUTTON_H) ||
        !hit_rect(x, y, (int)dialog_x, (int)dialog_y,
                  DESKTOP_MESSAGE_W, DESKTOP_MESSAGE_H)) {
        desktop_message_active = 0;
        full_redraw_pending = 1;
    }
    return 1;
}

int desktop_handle_message_key(uint8_t keycode, uint8_t pressed)
{
    if (!desktop_message_active) {
        return 0;
    }
    if (pressed && (keycode == LEONOS_KEY_ENTER || keycode == 1)) {
        desktop_message_active = 0;
        full_redraw_pending = 1;
    }
    return 1;
}

void draw_desktop_shortcut_input(void)
{
    uint32_t x;
    uint32_t y;
    uint32_t input_w;
    uint32_t text_len;
    uint32_t visible_chars;
    uint32_t scroll = 0;
    if (!desktop_shortcut_input_active) {
        return;
    }
    x = fb_w() > DESKTOP_SHORTCUT_INPUT_W
            ? (fb_w() - DESKTOP_SHORTCUT_INPUT_W) / 2
            : 0;
    y = fb_h() > DESKTOP_SHORTCUT_INPUT_H
            ? (fb_h() - DESKTOP_SHORTCUT_INPUT_H) / 2
            : 0;
    input_w = DESKTOP_SHORTCUT_INPUT_W > 40 ? DESKTOP_SHORTCUT_INPUT_W - 40 : DESKTOP_SHORTCUT_INPUT_W;
    text_len = desktop_text_len(desktop_shortcut_target);
    visible_chars = input_w > 8 ? (input_w - 8) / LEONOS_FONT_W : 0;
    if (visible_chars && text_len > visible_chars) {
        scroll = text_len - visible_chars;
    }
    rect_fill_i((int)x + 5, (int)y + 5,
                DESKTOP_SHORTCUT_INPUT_W, DESKTOP_SHORTCUT_INPUT_H, 0x00404040);
    leonos_ui_dialog(&ui, x, y, DESKTOP_SHORTCUT_INPUT_W,
                     DESKTOP_SHORTCUT_INPUT_H,
                     leonos_i18n("Create Shortcut", "创建快捷方式"));
    leonos_ui_text(&ui, x + 20, y + 48,
                   leonos_i18n("Target path:", "目标路径:"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(&ui, x + 20, y + 72, input_w,
                   desktop_shortcut_target, text_len, scroll,
                   LEONOS_UI_EDIT_FOCUSED);
    leonos_ui_button(&ui, x + DESKTOP_SHORTCUT_INPUT_W - 168,
                     y + DESKTOP_SHORTCUT_INPUT_H - 38,
                     72, LEONOS_UI_BUTTON_H,
                     leonos_i18n("OK", "确定"), 0);
    leonos_ui_button(&ui, x + DESKTOP_SHORTCUT_INPUT_W - 88,
                     y + DESKTOP_SHORTCUT_INPUT_H - 38,
                     72, LEONOS_UI_BUTTON_H,
                     leonos_i18n("Cancel", "取消"), 0);
}

int desktop_handle_shortcut_input_click(uint32_t x, uint32_t y)
{
    uint32_t dialog_x;
    uint32_t dialog_y;
    if (!desktop_shortcut_input_active) {
        return 0;
    }
    dialog_x = fb_w() > DESKTOP_SHORTCUT_INPUT_W
                   ? (fb_w() - DESKTOP_SHORTCUT_INPUT_W) / 2
                   : 0;
    dialog_y = fb_h() > DESKTOP_SHORTCUT_INPUT_H
                   ? (fb_h() - DESKTOP_SHORTCUT_INPUT_H) / 2
                   : 0;
    if (hit_rect(x, y, (int)dialog_x + DESKTOP_SHORTCUT_INPUT_W - 168,
                 (int)dialog_y + DESKTOP_SHORTCUT_INPUT_H - 38,
                 72, LEONOS_UI_BUTTON_H)) {
        desktop_create_shortcut_from_input();
        return 1;
    }
    if (hit_rect(x, y, (int)dialog_x + DESKTOP_SHORTCUT_INPUT_W - 88,
                 (int)dialog_y + DESKTOP_SHORTCUT_INPUT_H - 38,
                 72, LEONOS_UI_BUTTON_H)) {
        desktop_shortcut_input_active = 0;
        desktop_shortcut_target[0] = 0;
        full_redraw_pending = 1;
        return 1;
    }
    return 1;
}

int desktop_handle_shortcut_input_key(uint8_t keycode, uint8_t pressed)
{
    char ch;
    uint32_t len;
    if (!desktop_shortcut_input_active) {
        return 0;
    }
    if (keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT) {
        desktop_shortcut_shift_down = pressed ? 1 : 0;
        return 1;
    }
    if (!pressed) {
        return 1;
    }
    if (keycode == LEONOS_KEY_ENTER) {
        desktop_create_shortcut_from_input();
        return 1;
    }
    if (keycode == 1) {
        desktop_shortcut_input_active = 0;
        desktop_shortcut_target[0] = 0;
        full_redraw_pending = 1;
        return 1;
    }
    if (keycode == LEONOS_KEY_BACKSPACE) {
        len = desktop_text_len(desktop_shortcut_target);
        if (len) {
            desktop_shortcut_target[len - 1] = 0;
            full_redraw_pending = 1;
        }
        return 1;
    }
    if (leonos_ui_keycode_to_char_shift(keycode, desktop_shortcut_shift_down, &ch) &&
        ch >= 32 && ch != 127) {
        desktop_append_shortcut_input_char(ch);
    }
    return 1;
}

int desktop_handle_context_menu_click(uint32_t x, uint32_t y)
{
    struct leonos_ui_context_menu_item items[DESKTOP_CONTEXT_MENU_COUNT];
    uint32_t action = 0;
    if (!desktop_context_menu_active) {
        return 0;
    }
    desktop_build_context_menu_items(items);
    if (leonos_ui_context_menu_hit((int32_t)x, (int32_t)y,
                                   desktop_context_menu_x, desktop_context_menu_y,
                                   DESKTOP_CONTEXT_MENU_W, items,
                                   DESKTOP_CONTEXT_MENU_COUNT, &action)) {
        desktop_context_menu_set_active(0);
        if (action) {
            desktop_run_context_action(action);
        }
        return 1;
    }
    desktop_context_menu_set_active(0);
    return 1;
}

int desktop_handle_background_click(uint32_t x, uint32_t y)
{
    int32_t index = desktop_item_at(x, y);
    unsigned long now = leonos_uptime_ms();
    if (index >= 0) {
        if (desktop_selected_item == index &&
            desktop_last_click_item == index &&
            now - desktop_last_click_ms <= DESKTOP_ITEM_DOUBLE_CLICK_MS) {
            desktop_last_click_item = -1;
            desktop_last_click_ms = 0;
            desktop_open_path(desktop_items[index].path);
        } else {
            desktop_selected_item = index;
            desktop_last_click_item = index;
            desktop_last_click_ms = now;
        }
        full_redraw_pending = 1;
        return 1;
    }
    if (desktop_selected_item >= 0) {
        desktop_selected_item = -1;
        desktop_last_click_item = -1;
        full_redraw_pending = 1;
        return 1;
    }
    return 0;
}

int desktop_handle_background_right_click(uint32_t x, uint32_t y)
{
    int32_t index;
    if (y >= taskbar_y()) {
        return 0;
    }
    index = desktop_item_at(x, y);
    desktop_selected_item = index;
    desktop_last_click_item = -1;
    desktop_show_context_menu(x, y);
    return 1;
}
