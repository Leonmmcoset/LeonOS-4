#include "desktop.h"

/* Generated per image. Unknown (for example, post-install) programs remain
 * visible; only build-managed packages are listed here. */
#define START_MENU_ENTRY_POLICY_PATH "/system/config/desktop-entries.conf"
#define START_MENU_ENTRY_POLICY_BYTES 4096U
#define START_MENU_ENTRY_POLICY_MAX 96U

#define START_PANEL_MARGIN 8U
#define START_PANEL_HEADER_H 42U
#define START_PANEL_TAB_H 24U
#define START_PANEL_FOOTER_H 30U
#define START_PANEL_GAP 5U
#define START_SHORTCUT_COLUMNS 3U
#define START_SHORTCUT_H 42U
#define START_SHORTCUT_GAP 4U
#define START_LIST_TITLE_H 18U
#define START_DOC_ICON_PATH "/programs/oshlp/oshlp.bmp"

struct start_panel_layout {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct start_panel_content {
    uint32_t tabs_y;
    uint32_t search_y;
    uint32_t body_y;
    uint32_t body_h;
    uint32_t footer_y;
};

struct start_home_layout {
    uint32_t shortcuts_y;
    uint32_t shortcut_rows;
    uint32_t browse_y;
    uint32_t windows_title_y;
    uint32_t windows_y;
    uint32_t window_rows;
};

struct start_list_layout {
    uint32_t y;
    uint32_t h;
    uint32_t rows;
};

struct start_menu_result {
    const char *label;
    const char *path;
    uint8_t document;
};

static char start_menu_disabled_packages[START_MENU_ENTRY_POLICY_MAX][LEONOS_FS_PATH_LEN];
static uint32_t start_menu_disabled_package_count;
static int start_menu_kernel_debug_enabled(void)
{
    uint32_t flags = 0;
    return leonos_kernel_debug_get_state(&flags) == 0 &&
           (flags & LEONOS_KERNEL_DEBUG_STATE_ENABLED) != 0U;
}

static int start_menu_entry_policy_matches(const char *path)
{
    for (uint32_t index = 0; index < start_menu_disabled_package_count; ++index) {
        if (text_eq(path, start_menu_disabled_packages[index])) {
            return 1;
        }
    }
    return 0;
}

static void start_menu_load_entry_policy(void)
{
    char buffer[START_MENU_ENTRY_POLICY_BYTES + 1U];
    int fd;
    long got;
    uint32_t offset = 0;

    start_menu_disabled_package_count = 0;
    fd = open(START_MENU_ENTRY_POLICY_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    got = read(fd, buffer, START_MENU_ENTRY_POLICY_BYTES);
    close(fd);
    if (got <= 0) {
        return;
    }
    if ((uint32_t)got > START_MENU_ENTRY_POLICY_BYTES) {
        got = START_MENU_ENTRY_POLICY_BYTES;
    }
    buffer[got] = 0;
    while (offset < (uint32_t)got &&
           start_menu_disabled_package_count < START_MENU_ENTRY_POLICY_MAX) {
        uint32_t line_start = offset;
        uint32_t out = 0;
        while (offset < (uint32_t)got && buffer[offset] != '\n' && buffer[offset] != '\r') {
            ++offset;
        }
        if (offset - line_start > 5U &&
            buffer[line_start] == 'h' && buffer[line_start + 1U] == 'i' &&
            buffer[line_start + 2U] == 'd' && buffer[line_start + 3U] == 'e' &&
            buffer[line_start + 4U] == '=') {
            uint32_t value = line_start + 5U;
            while (value < offset && out + 1U < LEONOS_FS_PATH_LEN) {
                start_menu_disabled_packages[start_menu_disabled_package_count][out++] = buffer[value++];
            }
            start_menu_disabled_packages[start_menu_disabled_package_count][out] = 0;
            if (out) {
                ++start_menu_disabled_package_count;
            }
        }
        while (offset < (uint32_t)got && (buffer[offset] == '\n' || buffer[offset] == '\r')) {
            ++offset;
        }
    }
}

int start_menu_is_hidden_app(const char *name)
{
    return text_eq(name, "init.elf") ||
           text_eq(name, "desktop.elf") ||
           text_eq(name, "serviced.elf") ||
           text_eq(name, "oobe.elf") ||
           text_eq(name, "login.elf") ||
           text_eq(name, "sysconfdialog.elf") ||
           text_eq(name, "shell.elf");
}

static int start_menu_label_compare(const char *left, const char *right)
{
    uint32_t i = 0;
    while (left && right && left[i] && right[i]) {
        char a = lower_ascii(left[i]);
        char b = lower_ascii(right[i]);
        if (a != b) {
            return a < b ? -1 : 1;
        }
        ++i;
    }
    if (!left || !left[i]) {
        return right && right[i] ? -1 : 0;
    }
    return 1;
}

static void start_menu_sort_apps(void)
{
    for (uint32_t i = 0; i < start_menu_app_count; ++i) {
        for (uint32_t j = i + 1U; j < start_menu_app_count; ++j) {
            if (start_menu_label_compare(start_menu_app_labels[i], start_menu_app_labels[j]) > 0) {
                char label[sizeof(start_menu_app_labels[0])];
                char path[LEONOS_FS_PATH_LEN];
                uint32_t k;
                for (k = 0; k < sizeof(label); ++k) {
                    label[k] = start_menu_app_labels[i][k];
                    start_menu_app_labels[i][k] = start_menu_app_labels[j][k];
                    start_menu_app_labels[j][k] = label[k];
                }
                for (k = 0; k < LEONOS_FS_PATH_LEN; ++k) {
                    path[k] = start_menu_app_paths[i][k];
                    start_menu_app_paths[i][k] = start_menu_app_paths[j][k];
                    start_menu_app_paths[j][k] = path[k];
                }
            }
        }
    }
}

static void start_menu_sort_docs(void)
{
    for (uint32_t i = 0; i < start_menu_doc_count; ++i) {
        for (uint32_t j = i + 1U; j < start_menu_doc_count; ++j) {
            if (start_menu_label_compare(start_menu_doc_labels[i], start_menu_doc_labels[j]) > 0) {
                char label[sizeof(start_menu_doc_labels[0])];
                char path[LEONOS_FS_PATH_LEN];
                uint32_t k;
                for (k = 0; k < sizeof(label); ++k) {
                    label[k] = start_menu_doc_labels[i][k];
                    start_menu_doc_labels[i][k] = start_menu_doc_labels[j][k];
                    start_menu_doc_labels[j][k] = label[k];
                }
                for (k = 0; k < LEONOS_FS_PATH_LEN; ++k) {
                    path[k] = start_menu_doc_paths[i][k];
                    start_menu_doc_paths[i][k] = start_menu_doc_paths[j][k];
                    start_menu_doc_paths[j][k] = path[k];
                }
            }
        }
    }
}

static int start_menu_add_package(const char *root, const char *package)
{
    char elf_name[LEONOS_FS_PATH_LEN];
    char path[LEONOS_FS_PATH_LEN];
    uint32_t pos = 0;
    struct leonos_stat st;
    if (!root || !package || !package[0] ||
        start_menu_app_count >= START_MENU_MAX_APPS) {
        return 0;
    }
    copy_text(elf_name, sizeof(elf_name), package);
    while (elf_name[pos]) {
        ++pos;
    }
    append_text(elf_name, &pos, sizeof(elf_name), ".elf");
    if (start_menu_is_hidden_app(elf_name)) {
        return 0;
    }
    copy_text(path, sizeof(path), root);
    pos = 0;
    while (path[pos]) {
        ++pos;
    }
    append_char(path, &pos, sizeof(path), '/');
    append_text(path, &pos, sizeof(path), package);
    if (start_menu_entry_policy_matches(path)) {
        return 0;
    }
    append_char(path, &pos, sizeof(path), '/');
    append_text(path, &pos, sizeof(path), elf_name);
    {
        int ret = stat(path, &st);
        if (ret < 0) {
            return (ret == -LEONOS_EAGAIN || ret == -LEONOS_EIO) ? ret : 0;
        }
        if (st.type != LEONOS_FS_TYPE_FILE) {
            return 0;
        }
    }
    copy_app_label_from_elf(start_menu_app_labels[start_menu_app_count],
                            sizeof(start_menu_app_labels[start_menu_app_count]), elf_name);
    copy_text(start_menu_app_paths[start_menu_app_count],
              sizeof(start_menu_app_paths[start_menu_app_count]), path);
    ++start_menu_app_count;
    return 0;
}

static int start_menu_load_root(const char *root)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    int ret = leonos_list_dir(root, entries, LEONOS_FS_MAX_ENTRIES, &count);
    if (ret < 0) {
        return ret;
    }
    for (uint32_t i = 0; i < count && start_menu_app_count < START_MENU_MAX_APPS; ++i) {
        if (entries[i].type != LEONOS_FS_TYPE_DIR) {
            continue;
        }
        ret = start_menu_add_package(root, entries[i].name);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int start_menu_load_apps(void)
{
    int ret;
    start_menu_app_count = 0;
    start_menu_apps_loaded = 0;
    start_menu_load_entry_policy();
    ret = start_menu_load_root("/system/apps");
    if (ret < 0) {
        start_menu_app_count = 0;
        return ret;
    }
    ret = start_menu_load_root("/programs");
    if (ret < 0) {
        start_menu_app_count = 0;
        return ret;
    }
    start_menu_sort_apps();
    start_menu_apps_loaded = 1;
    return 0;
}

void start_menu_ensure_apps(void)
{
    unsigned long now = leonos_uptime_ms();
    if (!start_menu_apps_loaded && now >= start_menu_apps_retry_ms) {
        if (start_menu_load_apps() < 0) {
            start_menu_apps_retry_ms = now + 1000UL;
        }
    }
}

static void copy_hlp_filename_label(char *dst, uint32_t cap, const char *name)
{
    uint32_t len = 0;
    uint32_t out = 0;
    while (name && name[len]) {
        ++len;
    }
    if (len > 4U && text_ends_with(name, ".hlp")) {
        len -= 4U;
    }
    dst[0] = 0;
    while (name && out + 1U < cap && out < len) {
        dst[out] = name[out] == '_' ? ' ' : name[out];
        ++out;
    }
    dst[out] = 0;
    if (!dst[0]) {
        copy_text(dst, cap, leonos_i18n("Help", "帮助"));
    } else if (dst[0] >= 'a' && dst[0] <= 'z') {
        dst[0] = (char)(dst[0] - 'a' + 'A');
    }
}

static int line_key_matches(const char *line, const char *key)
{
    uint32_t i = 0;
    while (line && (*line == ' ' || *line == '\t')) {
        ++line;
    }
    while (key && key[i]) {
        if (!line || line[i] != key[i]) {
            return 0;
        }
        ++i;
    }
    return line && line[i] == ':';
}

static void copy_line_value(char *dst, uint32_t cap, const char *line)
{
    uint32_t pos = 0;
    while (line && *line && *line != ':') {
        ++line;
    }
    if (line && *line == ':') {
        ++line;
    }
    while (line && (*line == ' ' || *line == '\t')) {
        ++line;
    }
    dst[0] = 0;
    while (line && line[pos] && line[pos] != '\n' && line[pos] != '\r' &&
           pos + 1U < cap) {
        dst[pos] = line[pos];
        ++pos;
    }
    while (pos && (dst[pos - 1U] == ' ' || dst[pos - 1U] == '\t')) {
        --pos;
    }
    dst[pos] = 0;
}

static int read_hlp_menu_title(const char *path, char *dst, uint32_t cap)
{
    char buf[2049];
    char fallback[48];
    const char *wanted = leonos_i18n_language() == LEONOS_LANG_ZH ? "title.zh" : "title.en";
    const char *other = leonos_i18n_language() == LEONOS_LANG_ZH ? "title.en" : "title.zh";
    int fd = open(path, LEONOS_O_RDONLY, 0);
    long got;
    uint32_t pos = 0;
    if (fd < 0) {
        return -1;
    }
    got = read(fd, buf, sizeof(buf) - 1U);
    close(fd);
    if (got <= 0) {
        return -1;
    }
    buf[(uint32_t)got] = 0;
    fallback[0] = 0;
    while (pos < (uint32_t)got) {
        char *line = buf + pos;
        while (pos < (uint32_t)got && buf[pos] != '\n' && buf[pos] != '\r') {
            ++pos;
        }
        if (pos < (uint32_t)got) {
            buf[pos++] = 0;
        }
        while (pos < (uint32_t)got && (buf[pos] == '\n' || buf[pos] == '\r')) {
            ++pos;
        }
        if (line_key_matches(line, wanted)) {
            copy_line_value(dst, cap, line);
            return dst[0] ? 0 : -1;
        }
        if (!fallback[0] && line_key_matches(line, other)) {
            copy_line_value(fallback, sizeof(fallback), line);
        }
        if (line[0] == '%' && line[1] == '%' && line[2] == 'D' &&
            line[3] == 'O' && line[4] == 'C') {
            break;
        }
    }
    if (fallback[0]) {
        copy_text(dst, cap, fallback);
        return 0;
    }
    return -1;
}

void start_menu_load_docs(void)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    start_menu_doc_count = 0;
    start_menu_docs_loaded = 0;
    if (leonos_list_dir("/docs", entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < count && start_menu_doc_count < START_MENU_MAX_DOCS; ++i) {
        uint32_t pos = 0;
        if (entries[i].type != LEONOS_FS_TYPE_FILE || !text_ends_with(entries[i].name, ".hlp")) {
            continue;
        }
        copy_text(start_menu_doc_paths[start_menu_doc_count],
                  sizeof(start_menu_doc_paths[start_menu_doc_count]), "/docs/");
        while (start_menu_doc_paths[start_menu_doc_count][pos]) {
            ++pos;
        }
        append_text(start_menu_doc_paths[start_menu_doc_count], &pos,
                    sizeof(start_menu_doc_paths[start_menu_doc_count]), entries[i].name);
        if (read_hlp_menu_title(start_menu_doc_paths[start_menu_doc_count],
                                start_menu_doc_labels[start_menu_doc_count],
                                sizeof(start_menu_doc_labels[start_menu_doc_count])) < 0) {
            copy_hlp_filename_label(start_menu_doc_labels[start_menu_doc_count],
                                    sizeof(start_menu_doc_labels[start_menu_doc_count]),
                                    entries[i].name);
        }
        ++start_menu_doc_count;
    }
    start_menu_sort_docs();
    start_menu_docs_loaded = 1;
}

void start_menu_ensure_docs(void)
{
    unsigned long now = leonos_uptime_ms();
    if (!start_menu_docs_loaded && now >= start_menu_docs_retry_ms) {
        start_menu_load_docs();
        if (!start_menu_docs_loaded) {
            start_menu_docs_retry_ms = now + 1000UL;
        }
    }
}

static int start_menu_contains(const char *text, const char *query)
{
    if (!query || !query[0]) {
        return 1;
    }
    for (uint32_t i = 0; text && text[i]; ++i) {
        uint32_t j = 0;
        while (query[j] && text[i + j] && lower_ascii(text[i + j]) == lower_ascii(query[j])) {
            ++j;
        }
        if (!query[j]) {
            return 1;
        }
    }
    return 0;
}

static int start_menu_app_matches(uint32_t index)
{
    return index < start_menu_app_count &&
           (start_menu_contains(start_menu_app_labels[index], start_menu_query) ||
            start_menu_contains(start_menu_app_paths[index], start_menu_query));
}

static int start_menu_doc_matches(uint32_t index)
{
    return index < start_menu_doc_count &&
           (start_menu_contains(start_menu_doc_labels[index], start_menu_query) ||
            start_menu_contains(start_menu_doc_paths[index], start_menu_query));
}

uint32_t start_menu_filtered_app_count(void)
{
    uint32_t count = 0;
    start_menu_ensure_apps();
    for (uint32_t i = 0; i < start_menu_app_count; ++i) {
        if (start_menu_app_matches(i)) {
            ++count;
        }
    }
    return count;
}

uint32_t start_menu_filtered_app_index(uint32_t filtered_index)
{
    uint32_t count = 0;
    start_menu_ensure_apps();
    for (uint32_t i = 0; i < start_menu_app_count; ++i) {
        if (!start_menu_app_matches(i)) {
            continue;
        }
        if (count == filtered_index) {
            return i;
        }
        ++count;
    }
    return start_menu_app_count;
}

static uint32_t start_menu_filtered_doc_count(void)
{
    uint32_t count = 0;
    start_menu_ensure_docs();
    for (uint32_t i = 0; i < start_menu_doc_count; ++i) {
        if (start_menu_doc_matches(i)) {
            ++count;
        }
    }
    return count;
}

static uint32_t start_menu_filtered_doc_index(uint32_t filtered_index)
{
    uint32_t count = 0;
    start_menu_ensure_docs();
    for (uint32_t i = 0; i < start_menu_doc_count; ++i) {
        if (!start_menu_doc_matches(i)) {
            continue;
        }
        if (count == filtered_index) {
            return i;
        }
        ++count;
    }
    return start_menu_doc_count;
}

static uint8_t start_menu_effective_view(void)
{
    return start_menu_query[0] ? START_MENU_VIEW_SEARCH : start_menu_view;
}

static uint32_t start_menu_result_count(uint8_t view)
{
    if (view == START_MENU_VIEW_APPS) {
        return start_menu_filtered_app_count();
    }
    if (view == START_MENU_VIEW_DOCUMENTS) {
        return start_menu_filtered_doc_count();
    }
    if (view == START_MENU_VIEW_SEARCH) {
        return start_menu_filtered_app_count() + start_menu_filtered_doc_count();
    }
    return 0;
}

static int start_menu_result_at(uint8_t view, uint32_t index, struct start_menu_result *out)
{
    uint32_t app_count;
    uint32_t actual;
    if (!out) {
        return 0;
    }
    if (view == START_MENU_VIEW_APPS || view == START_MENU_VIEW_SEARCH) {
        app_count = start_menu_filtered_app_count();
        if (index < app_count) {
            actual = start_menu_filtered_app_index(index);
            if (actual < start_menu_app_count) {
                out->label = start_menu_app_labels[actual];
                out->path = start_menu_app_paths[actual];
                out->document = 0;
                return 1;
            }
        }
        if (view == START_MENU_VIEW_APPS) {
            return 0;
        }
        index -= app_count;
    }
    if (view == START_MENU_VIEW_DOCUMENTS || view == START_MENU_VIEW_SEARCH) {
        actual = start_menu_filtered_doc_index(index);
        if (actual < start_menu_doc_count) {
            out->label = start_menu_doc_labels[actual];
            out->path = start_menu_doc_paths[actual];
            out->document = 1;
            return 1;
        }
    }
    return 0;
}

static struct start_panel_layout start_menu_panel_layout(void)
{
    struct start_panel_layout panel;
    uint32_t top = taskbar_y();
    uint32_t available_h = top > START_PANEL_MARGIN ? top - START_PANEL_MARGIN : top;
    panel.w = fb_w() > START_PANEL_MARGIN * 2U ? fb_w() - START_PANEL_MARGIN * 2U : fb_w();
    if (panel.w > START_MENU_W) {
        panel.w = START_MENU_W;
    }
    panel.h = available_h < START_MENU_MAX_H ? available_h : START_MENU_MAX_H;
    panel.x = START_PANEL_MARGIN;
    panel.y = top > panel.h ? top - panel.h : 0;
    return panel;
}

static struct start_panel_content start_menu_content_layout(const struct start_panel_layout *panel)
{
    struct start_panel_content content;
    uint32_t after_header = panel->y + START_PANEL_HEADER_H + START_PANEL_GAP;
    content.tabs_y = after_header;
    content.search_y = content.tabs_y + START_PANEL_TAB_H + START_PANEL_GAP;
    content.body_y = content.search_y + START_MENU_SEARCH_H + START_PANEL_GAP + 2U;
    content.footer_y = panel->y + panel->h > START_PANEL_FOOTER_H + START_PANEL_GAP
                           ? panel->y + panel->h - START_PANEL_FOOTER_H - START_PANEL_GAP
                           : panel->y;
    content.body_h = content.footer_y > content.body_y ? content.footer_y - content.body_y : 0;
    return content;
}

static struct start_home_layout start_menu_home_layout(const struct start_panel_content *content)
{
    struct start_home_layout home;
    uint32_t after_shortcuts;
    home.shortcut_rows = content->body_h >= 190U ? 2U : 1U;
    home.shortcuts_y = content->body_y + START_LIST_TITLE_H;
    after_shortcuts = home.shortcuts_y + home.shortcut_rows *
                      (START_SHORTCUT_H + START_SHORTCUT_GAP);
    home.browse_y = after_shortcuts + 2U;
    home.windows_title_y = home.browse_y + START_MENU_ITEM_H + 4U;
    home.windows_y = home.windows_title_y + START_LIST_TITLE_H;
    home.window_rows = content->footer_y > home.windows_y
                           ? (content->footer_y - home.windows_y) / START_MENU_ITEM_H
                           : 0;
    return home;
}

static struct start_list_layout start_menu_list_layout(const struct start_panel_content *content)
{
    struct start_list_layout list;
    list.y = content->body_y + START_LIST_TITLE_H;
    list.h = content->body_h > START_LIST_TITLE_H ? content->body_h - START_LIST_TITLE_H : 0;
    list.rows = list.h / START_MENU_ITEM_H;
    return list;
}

static void start_menu_set_view(uint8_t view)
{
    start_menu_view = view;
    start_menu_scroll = 0;
    start_menu_selected = 0;
    full_redraw_pending = 1;
}

static uint32_t start_menu_minimized_count(void)
{
    uint32_t count = 0;
    for (uint8_t i = 0; i < MAX_WINDOWS; ++i) {
        if (windows[i].visible && windows[i].minimized) {
            ++count;
        }
    }
    return count;
}

static int start_menu_minimized_window(uint32_t wanted)
{
    uint32_t count = 0;
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        if (!windows[id].visible || !windows[id].minimized) {
            continue;
        }
        if (count++ == wanted) {
            return id;
        }
    }
    return -1;
}

static const char *start_menu_shortcut_label(uint32_t index)
{
    switch (index) {
    case 0: return leonos_i18n("File Manager", "文件管理器");
    case 1: return leonos_i18n("Terminal", "终端");
    case 2: return leonos_i18n("Settings", "设置");
    case 3: return leonos_i18n("Run", "运行");
    case 4: return leonos_i18n("Task Manager", "任务管理器");
    default: return "";
    }
}

static const char *start_menu_shortcut_path(uint32_t index)
{
    static const char *const paths[] = {
        "/system/apps/fileman/fileman.elf",
        "/system/apps/terminal/terminal.elf",
        "/system/apps/settings/settings.elf",
        "/system/apps/run/run.elf",
        "/system/apps/taskmgr/taskmgr.elf",
    };
    return index < sizeof(paths) / sizeof(paths[0]) ? paths[index] : 0;
}

static void start_menu_draw_header(const struct start_panel_layout *panel)
{
    struct leonos_user_info user = {0};
    const char *session = leonos_i18n("Desktop session", "桌面会话");
    uint32_t header_w = panel->w > 2U ? panel->w - 2U : panel->w;
    leonos_ui_rect(&ui, panel->x + 1U, panel->y + 1U, header_w,
                   START_PANEL_HEADER_H, LEONOS_UI_ACTIVE_TITLE);
    leonos_ui_text(&ui, panel->x + 12U, panel->y + 7U, "LeonOS 4",
                   LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
    if (leonos_auth_current(&user) == 0 && user.uid && user.username[0]) {
        session = user.username;
    }
    leonos_ui_text_clipped(&ui, panel->x + 12U, panel->y + 24U,
                           panel->w > 24U ? panel->w - 24U : 0U, session,
                           LEONOS_UI_WHITE, LEONOS_UI_ACTIVE_TITLE);
}

static void start_menu_draw_tabs(const struct start_panel_layout *panel,
                                 const struct start_panel_content *content)
{
    uint32_t x = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t tab_w = width / 3U;
    const char *labels[3] = {
        leonos_i18n("Home", "主页"),
        leonos_i18n("All apps", "所有应用"),
        leonos_i18n("Documents", "文档"),
    };
    for (uint32_t i = 0; i < 3U; ++i) {
        uint32_t item_w = i == 2U ? width - tab_w * 2U : tab_w;
        uint32_t flags = start_menu_view == i ? LEONOS_UI_BUTTON_ACTIVE : 0;
        leonos_ui_button(&ui, x, content->tabs_y, item_w, START_PANEL_TAB_H, labels[i], flags);
        x += item_w;
    }
}

static void start_menu_draw_search(const struct start_panel_layout *panel,
                                   const struct start_panel_content *content)
{
    uint32_t x = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    leonos_ui_text_field(&ui, x, content->search_y, width, start_menu_query,
                         LEONOS_UI_EDIT_FOCUSED);
    if (!start_menu_query[0]) {
        leonos_ui_text_clipped(&ui, x + 7U, content->search_y + 5U,
                               width > 14U ? width - 14U : 0U,
                               leonos_i18n("Search apps and documents", "搜索应用和文档"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
}

static void start_menu_draw_shortcut(const struct start_panel_layout *panel, uint32_t x,
                                     uint32_t y, uint32_t width, uint32_t index)
{
    char icon_path[LEONOS_FS_PATH_LEN];
    const char *path = start_menu_shortcut_path(index);
    (void)panel;
    leonos_ui_button(&ui, x, y, width, START_SHORTCUT_H, "", 0);
    desktop_icon_path_for_app(path, icon_path, sizeof(icon_path));
    draw_app_icon(icon_path, (int)x + 7, (int)y + 13);
    leonos_ui_text_clipped(&ui, x + 29U, y + 13U, width > 35U ? width - 35U : 0U,
                           start_menu_shortcut_label(index), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
}

static void start_menu_draw_home(const struct start_panel_layout *panel,
                                 const struct start_panel_content *content)
{
    struct start_home_layout home = start_menu_home_layout(content);
    uint32_t content_x = panel->x + START_PANEL_MARGIN;
    uint32_t content_w = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t tile_w = content_w > START_SHORTCUT_GAP * 2U
                          ? (content_w - START_SHORTCUT_GAP * 2U) / START_SHORTCUT_COLUMNS : 0U;
    uint32_t minimized = start_menu_minimized_count();
    uint32_t shown = 0;
    leonos_ui_text(&ui, content_x, content->body_y,
                   leonos_i18n("Quick access", "快速访问"), LEONOS_UI_DARK, LEONOS_UI_GRAY);
    for (uint32_t index = 0; index < 5U; ++index) {
        uint32_t row = index / START_SHORTCUT_COLUMNS;
        uint32_t col = index % START_SHORTCUT_COLUMNS;
        if (row >= home.shortcut_rows) {
            break;
        }
        start_menu_draw_shortcut(panel,
                                 content_x + col * (tile_w + START_SHORTCUT_GAP),
                                 home.shortcuts_y + row * (START_SHORTCUT_H + START_SHORTCUT_GAP),
                                 tile_w, index);
    }
    leonos_ui_button(&ui, content_x, home.browse_y,
                     content_w > START_SHORTCUT_GAP ? (content_w - START_SHORTCUT_GAP) / 2U : 0U,
                     START_MENU_ITEM_H, leonos_i18n("All applications", "所有应用"), 0);
    leonos_ui_button(&ui, content_x + (content_w + START_SHORTCUT_GAP) / 2U, home.browse_y,
                     content_w > START_SHORTCUT_GAP ? (content_w - START_SHORTCUT_GAP) / 2U : 0U,
                     START_MENU_ITEM_H, leonos_i18n("Documents", "文档"), 0);
    if (!home.window_rows) {
        return;
    }
    leonos_ui_text(&ui, content_x, home.windows_title_y,
                   minimized ? leonos_i18n("Minimized windows", "最小化窗口")
                             : leonos_i18n("No minimized windows", "没有最小化窗口"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < minimized && shown < home.window_rows; ++i) {
        int id = start_menu_minimized_window(i);
        uint32_t y = home.windows_y + shown * START_MENU_ITEM_H;
        if (id < 0) {
            break;
        }
        draw_app_icon(windows[id].icon_path, (int)content_x + 6, (int)y + 5);
        leonos_ui_menu_item(&ui, content_x + 29U, y,
                            content_w > 35U ? content_w - 35U : 0U,
                            windows[id].title ? windows[id].title : leonos_i18n("Window", "窗口"), 0);
        ++shown;
    }
}

static const char *start_menu_list_title(uint8_t view)
{
    if (view == START_MENU_VIEW_SEARCH) {
        return leonos_i18n("Search results", "搜索结果");
    }
    if (view == START_MENU_VIEW_DOCUMENTS) {
        return leonos_i18n("Documents", "文档");
    }
    return leonos_i18n("All applications", "所有应用");
}

static void start_menu_normalize_list(uint8_t view, uint32_t rows)
{
    uint32_t count = start_menu_result_count(view);
    if (!count) {
        start_menu_scroll = 0;
        start_menu_selected = 0;
        return;
    }
    if (start_menu_selected >= count) {
        start_menu_selected = count - 1U;
    }
    if (!rows) {
        start_menu_scroll = 0;
        return;
    }
    if (start_menu_scroll >= count) {
        start_menu_scroll = count - 1U;
    }
    if (start_menu_selected < start_menu_scroll) {
        start_menu_scroll = start_menu_selected;
    } else if (start_menu_selected >= start_menu_scroll + rows) {
        start_menu_scroll = start_menu_selected - rows + 1U;
    }
}

static void start_menu_draw_results(const struct start_panel_layout *panel,
                                    const struct start_panel_content *content, uint8_t view)
{
    struct start_list_layout list = start_menu_list_layout(content);
    uint32_t content_x = panel->x + START_PANEL_MARGIN;
    uint32_t content_w = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t count = start_menu_result_count(view);
    start_menu_normalize_list(view, list.rows);
    leonos_ui_text(&ui, content_x, content->body_y, start_menu_list_title(view),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    if (!count) {
        leonos_ui_menu_item(&ui, content_x + 5U, list.y,
                            content_w > 10U ? content_w - 10U : 0U,
                            leonos_i18n("Nothing found", "没有找到内容"),
                            LEONOS_UI_MENU_DISABLED);
        return;
    }
    for (uint32_t visible = 0; visible < list.rows; ++visible) {
        struct start_menu_result result;
        uint32_t index = start_menu_scroll + visible;
        uint32_t row_y = list.y + visible * START_MENU_ITEM_H;
        char icon_path[LEONOS_FS_PATH_LEN];
        if (index >= count || !start_menu_result_at(view, index, &result)) {
            break;
        }
        if (result.document) {
            copy_text(icon_path, sizeof(icon_path), START_DOC_ICON_PATH);
        } else {
            desktop_icon_path_for_app(result.path, icon_path, sizeof(icon_path));
        }
        draw_app_icon(icon_path, (int)content_x + 6, (int)row_y + 5);
        leonos_ui_menu_item(&ui, content_x + 29U, row_y,
                            content_w > 35U ? content_w - 35U : 0U, result.label,
                            index == start_menu_selected ? LEONOS_UI_MENU_SELECTED : 0);
    }
}

static void start_menu_draw_power(const struct start_panel_layout *panel,
                                  const struct start_panel_content *content)
{
    uint32_t x = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t y = content->body_y + START_LIST_TITLE_H;
    leonos_ui_text(&ui, x, content->body_y, leonos_i18n("Power", "电源"),
                   LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_button(&ui, x, y, width, START_MENU_ITEM_H,
                     leonos_i18n("Restart", "重启"), 0);
    y += START_MENU_ITEM_H + START_PANEL_GAP;
    if (start_menu_kernel_debug_enabled()) {
        leonos_ui_button(&ui, x, y, width, START_MENU_ITEM_H,
                         leonos_i18n("Restart into kernel debugger", "重启并进入内核调试工具"), 0);
        y += START_MENU_ITEM_H + START_PANEL_GAP;
    }
    leonos_ui_button(&ui, x, y, width, START_MENU_ITEM_H,
                     leonos_i18n("Shut down", "关机"), 0);
    y += START_MENU_ITEM_H + START_PANEL_GAP;
    leonos_ui_button(&ui, x, y, width, START_MENU_ITEM_H,
                     leonos_i18n("Back", "返回"), 0);
}

static void start_menu_draw_footer(const struct start_panel_layout *panel,
                                   const struct start_panel_content *content)
{
    uint32_t x = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t power_w = 92U;
    uint32_t session_w = width > power_w + START_PANEL_GAP ? width - power_w - START_PANEL_GAP : 0U;
    if (desktop_session_logged_in()) {
        leonos_ui_button(&ui, x, content->footer_y, session_w, START_PANEL_FOOTER_H,
                         leonos_i18n("Sign out", "注销"), 0);
    } else {
        leonos_ui_button(&ui, x, content->footer_y, session_w, START_PANEL_FOOTER_H,
                         leonos_i18n("Session", "会话"), LEONOS_UI_BUTTON_DISABLED);
    }
    leonos_ui_button(&ui, x + session_w + START_PANEL_GAP, content->footer_y,
                     power_w, START_PANEL_FOOTER_H,
                     start_menu_view == START_MENU_VIEW_POWER ? leonos_i18n("Back", "返回")
                                                               : leonos_i18n("Power", "电源"),
                     start_menu_view == START_MENU_VIEW_POWER ? LEONOS_UI_BUTTON_ACTIVE : 0);
}

void draw_start_menu(void)
{
    struct start_panel_layout panel;
    struct start_panel_content content;
    uint32_t progress;
    uint32_t visible_h;
    if (!start_menu_open && !start_menu_animating) {
        return;
    }
    panel = start_menu_panel_layout();
    progress = start_menu_progress();
    visible_h = (panel.h * progress + 99U) / 100U;
    if (visible_h < panel.h) {
        uint32_t visible_y = taskbar_y() > visible_h ? taskbar_y() - visible_h : 0U;
        leonos_ui_panel(&ui, panel.x, visible_y, panel.w, visible_h, LEONOS_UI_GRAY);
        return;
    }
    leonos_ui_panel(&ui, panel.x, panel.y, panel.w, panel.h, LEONOS_UI_GRAY);
    if (panel.h < START_PANEL_HEADER_H + START_PANEL_TAB_H + START_MENU_SEARCH_H + START_PANEL_FOOTER_H) {
        return;
    }
    content = start_menu_content_layout(&panel);
    start_menu_draw_header(&panel);
    start_menu_draw_tabs(&panel, &content);
    start_menu_draw_search(&panel, &content);
    if (start_menu_effective_view() == START_MENU_VIEW_HOME) {
        start_menu_draw_home(&panel, &content);
    } else if (start_menu_effective_view() == START_MENU_VIEW_POWER) {
        start_menu_draw_power(&panel, &content);
    } else {
        start_menu_draw_results(&panel, &content, start_menu_effective_view());
    }
    start_menu_draw_footer(&panel, &content);
}

static void start_menu_launch_result(uint8_t view, uint32_t index)
{
    struct start_menu_result result;
    if (!start_menu_result_at(view, index, &result)) {
        return;
    }
    if (result.document) {
        spawn_help_path(result.path);
    } else {
        spawn_program_path(result.path);
    }
    start_menu_set_open(0);
}

int start_menu_handle_key(uint8_t keycode, uint8_t pressed)
{
    char ch;
    uint32_t len = 0;
    uint8_t view;
    struct start_panel_layout panel;
    struct start_panel_content content;
    struct start_list_layout list;
    if (!start_menu_open || start_menu_animating) {
        return 0;
    }
    if (!pressed) {
        return 1;
    }
    if (keycode == LEONOS_KEY_ESCAPE) {
        if (start_menu_query[0]) {
            start_menu_query[0] = 0;
            start_menu_scroll = 0;
            start_menu_selected = 0;
        } else if (start_menu_view != START_MENU_VIEW_HOME) {
            start_menu_set_view(START_MENU_VIEW_HOME);
        } else {
            start_menu_set_open(0);
        }
        full_redraw_pending = 1;
        return 1;
    }
    if (keycode == LEONOS_KEY_TAB) {
        if (start_menu_query[0]) {
            start_menu_query[0] = 0;
        }
        start_menu_set_view((uint8_t)((start_menu_view + 1U) % 3U));
        return 1;
    }
    if (keycode == LEONOS_KEY_BACKSPACE) {
        while (start_menu_query[len]) {
            ++len;
        }
        if (len) {
            start_menu_query[len - 1U] = 0;
            start_menu_scroll = 0;
            start_menu_selected = 0;
        } else if (start_menu_view != START_MENU_VIEW_HOME) {
            start_menu_set_view(START_MENU_VIEW_HOME);
        }
        full_redraw_pending = 1;
        return 1;
    }
    view = start_menu_effective_view();
    if (view == START_MENU_VIEW_APPS || view == START_MENU_VIEW_DOCUMENTS ||
        view == START_MENU_VIEW_SEARCH) {
        uint32_t count = start_menu_result_count(view);
        panel = start_menu_panel_layout();
        content = start_menu_content_layout(&panel);
        list = start_menu_list_layout(&content);
        if (keycode == LEONOS_KEY_UP && count && start_menu_selected) {
            --start_menu_selected;
        } else if (keycode == LEONOS_KEY_DOWN && count && start_menu_selected + 1U < count) {
            ++start_menu_selected;
        } else if (keycode == LEONOS_KEY_HOME && count) {
            start_menu_selected = 0;
        } else if (keycode == LEONOS_KEY_END && count) {
            start_menu_selected = count - 1U;
        } else if (keycode == LEONOS_KEY_PAGE_UP && count) {
            start_menu_selected = start_menu_selected > list.rows ? start_menu_selected - list.rows : 0;
        } else if (keycode == LEONOS_KEY_PAGE_DOWN && count) {
            uint32_t next = start_menu_selected + list.rows;
            start_menu_selected = next < count ? next : count - 1U;
        } else if (keycode == LEONOS_KEY_ENTER && count) {
            start_menu_launch_result(view, start_menu_selected);
            return 1;
        } else {
            goto text_input;
        }
        start_menu_normalize_list(view, list.rows);
        full_redraw_pending = 1;
        return 1;
    }
    if (view == START_MENU_VIEW_POWER && keycode == LEONOS_KEY_ENTER) {
        return 1;
    }

text_input:
    if (leonos_ui_keycode_to_char_shift(keycode,
                                        desktop_left_shift_down || desktop_right_shift_down,
                                        &ch)) {
        while (start_menu_query[len]) {
            ++len;
        }
        if (len + 1U < sizeof(start_menu_query)) {
            start_menu_query[len] = ch;
            start_menu_query[len + 1U] = 0;
            start_menu_scroll = 0;
            start_menu_selected = 0;
            full_redraw_pending = 1;
        }
    }
    return 1;
}

int start_menu_hit_test(uint32_t x, uint32_t y)
{
    struct start_panel_layout panel;
    uint32_t progress;
    uint32_t visible_h;
    uint32_t visible_y;
    if (!start_menu_open && !start_menu_animating) {
        return 0;
    }
    panel = start_menu_panel_layout();
    progress = start_menu_progress();
    visible_h = (panel.h * progress + 99U) / 100U;
    visible_y = taskbar_y() > visible_h ? taskbar_y() - visible_h : 0U;
    return hit_rect(x, y, (int)panel.x, (int)visible_y, panel.w, visible_h);
}

static int start_menu_hit_tab(uint32_t x, uint32_t y, const struct start_panel_layout *panel,
                              const struct start_panel_content *content, uint8_t *view)
{
    uint32_t left = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t tab_w = width / 3U;
    if (!hit_rect(x, y, (int)left, (int)content->tabs_y, width, START_PANEL_TAB_H)) {
        return 0;
    }
    if (x - left < tab_w) {
        *view = START_MENU_VIEW_HOME;
    } else if (x - left < tab_w * 2U) {
        *view = START_MENU_VIEW_APPS;
    } else {
        *view = START_MENU_VIEW_DOCUMENTS;
    }
    return 1;
}

static void start_menu_handle_home_click(uint32_t x, uint32_t y,
                                         const struct start_panel_layout *panel,
                                         const struct start_panel_content *content)
{
    struct start_home_layout home = start_menu_home_layout(content);
    uint32_t content_x = panel->x + START_PANEL_MARGIN;
    uint32_t content_w = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t tile_w = content_w > START_SHORTCUT_GAP * 2U
                          ? (content_w - START_SHORTCUT_GAP * 2U) / START_SHORTCUT_COLUMNS : 0U;
    if (tile_w && hit_rect(x, y, (int)content_x, (int)home.shortcuts_y, content_w,
                 home.shortcut_rows * (START_SHORTCUT_H + START_SHORTCUT_GAP))) {
        uint32_t row = (y - home.shortcuts_y) / (START_SHORTCUT_H + START_SHORTCUT_GAP);
        uint32_t col = (x - content_x) / (tile_w + START_SHORTCUT_GAP);
        uint32_t index = row * START_SHORTCUT_COLUMNS + col;
        if (tile_w && col < START_SHORTCUT_COLUMNS && index < 5U &&
            (x - content_x) % (tile_w + START_SHORTCUT_GAP) < tile_w &&
            (y - home.shortcuts_y) % (START_SHORTCUT_H + START_SHORTCUT_GAP) < START_SHORTCUT_H) {
            spawn_program_path(start_menu_shortcut_path(index));
            start_menu_set_open(0);
        }
        return;
    }
    if (hit_rect(x, y, (int)content_x, (int)home.browse_y, content_w, START_MENU_ITEM_H)) {
        if (x - content_x < content_w / 2U) {
            start_menu_set_view(START_MENU_VIEW_APPS);
        } else {
            start_menu_set_view(START_MENU_VIEW_DOCUMENTS);
        }
        return;
    }
    if (hit_rect(x, y, (int)content_x, (int)home.windows_y, content_w,
                 home.window_rows * START_MENU_ITEM_H)) {
        uint32_t row = (y - home.windows_y) / START_MENU_ITEM_H;
        int id = start_menu_minimized_window(row);
        if (id >= 0) {
            restore_window((uint8_t)id);
            start_menu_set_open(0);
        }
    }
}

static void start_menu_handle_power_click(uint32_t x, uint32_t y,
                                          const struct start_panel_layout *panel,
                                          const struct start_panel_content *content)
{
    uint32_t left = panel->x + START_PANEL_MARGIN;
    uint32_t width = panel->w > START_PANEL_MARGIN * 2U ? panel->w - START_PANEL_MARGIN * 2U : 0U;
    uint32_t first_y = content->body_y + START_LIST_TITLE_H;
    if (!hit_rect(x, y, (int)left, (int)first_y, width, START_MENU_ITEM_H)) {
        first_y += START_MENU_ITEM_H + START_PANEL_GAP;
        if (start_menu_kernel_debug_enabled()) {
            if (hit_rect(x, y, (int)left, (int)first_y, width, START_MENU_ITEM_H)) {
                start_menu_set_open(0);
                if (leonos_kernel_debug_arm_next_boot() == 0) {
                    leonos_system_reboot();
                } else {
                    desktop_show_message(leonos_i18n("Kernel debugger", "内核调试工具"),
                                         leonos_i18n("Could not arm the next debug boot.",
                                                     "无法设置下一次调试启动。"));
                }
                return;
            }
            first_y += START_MENU_ITEM_H + START_PANEL_GAP;
        }
        if (hit_rect(x, y, (int)left, (int)first_y, width, START_MENU_ITEM_H)) {
            start_menu_set_open(0);
            desktop_request_power_confirm(POWER_CONFIRM_SHUTDOWN);
        } else {
            first_y += START_MENU_ITEM_H + START_PANEL_GAP;
            if (hit_rect(x, y, (int)left, (int)first_y, width, START_MENU_ITEM_H)) {
                start_menu_set_view(START_MENU_VIEW_HOME);
            }
        }
        return;
    }
    start_menu_set_open(0);
    desktop_request_power_confirm(POWER_CONFIRM_REBOOT);
}

void start_menu_handle_click(uint32_t x, uint32_t y)
{
    struct start_panel_layout panel;
    struct start_panel_content content;
    uint8_t tab_view;
    uint8_t view;
    if (start_menu_animating) {
        return;
    }
    panel = start_menu_panel_layout();
    if (!hit_rect(x, y, (int)panel.x, (int)panel.y, panel.w, panel.h)) {
        start_menu_set_open(0);
        return;
    }
    content = start_menu_content_layout(&panel);
    if (start_menu_hit_tab(x, y, &panel, &content, &tab_view)) {
        start_menu_query[0] = 0;
        start_menu_set_view(tab_view);
        return;
    }
    if (hit_rect(x, y, (int)(panel.x + START_PANEL_MARGIN), (int)content.search_y,
                 panel.w > START_PANEL_MARGIN * 2U ? panel.w - START_PANEL_MARGIN * 2U : 0U,
                 START_MENU_SEARCH_H)) {
        return;
    }
    if (hit_rect(x, y, (int)(panel.x + START_PANEL_MARGIN), (int)content.footer_y,
                 panel.w > START_PANEL_MARGIN * 2U ? panel.w - START_PANEL_MARGIN * 2U : 0U,
                 START_PANEL_FOOTER_H)) {
        uint32_t width = panel.w > START_PANEL_MARGIN * 2U ? panel.w - START_PANEL_MARGIN * 2U : 0U;
        uint32_t power_w = 92U;
        uint32_t session_w = width > power_w + START_PANEL_GAP ? width - power_w - START_PANEL_GAP : 0U;
        if (x < panel.x + START_PANEL_MARGIN + session_w) {
            if (desktop_session_logged_in()) {
                desktop_logout();
            }
        } else if (start_menu_view == START_MENU_VIEW_POWER) {
            start_menu_set_view(START_MENU_VIEW_HOME);
        } else {
            start_menu_query[0] = 0;
            start_menu_set_view(START_MENU_VIEW_POWER);
        }
        return;
    }
    view = start_menu_effective_view();
    if (view == START_MENU_VIEW_HOME) {
        start_menu_handle_home_click(x, y, &panel, &content);
        return;
    }
    if (view == START_MENU_VIEW_POWER) {
        start_menu_handle_power_click(x, y, &panel, &content);
        return;
    }
    {
        struct start_list_layout list = start_menu_list_layout(&content);
        uint32_t count = start_menu_result_count(view);
        if (hit_rect(x, y, (int)(panel.x + START_PANEL_MARGIN), (int)list.y,
                     panel.w > START_PANEL_MARGIN * 2U ? panel.w - START_PANEL_MARGIN * 2U : 0U,
                     list.rows * START_MENU_ITEM_H)) {
            uint32_t index = start_menu_scroll + (y - list.y) / START_MENU_ITEM_H;
            if (index < count) {
                start_menu_selected = index;
                start_menu_launch_result(view, index);
            }
        }
    }
}

int start_menu_handle_wheel(uint32_t x, uint32_t y, int32_t wheel)
{
    struct start_panel_layout panel;
    struct start_panel_content content;
    struct start_list_layout list;
    uint8_t view;
    uint32_t count;
    uint32_t steps;
    if (!start_menu_open || start_menu_animating || wheel == 0) {
        return 0;
    }
    view = start_menu_effective_view();
    if (view != START_MENU_VIEW_APPS && view != START_MENU_VIEW_DOCUMENTS &&
        view != START_MENU_VIEW_SEARCH) {
        return 0;
    }
    panel = start_menu_panel_layout();
    content = start_menu_content_layout(&panel);
    list = start_menu_list_layout(&content);
    count = start_menu_result_count(view);
    if (!list.rows || count <= list.rows ||
        !hit_rect(x, y, (int)(panel.x + START_PANEL_MARGIN), (int)list.y,
                  panel.w > START_PANEL_MARGIN * 2U ? panel.w - START_PANEL_MARGIN * 2U : 0U,
                  list.rows * START_MENU_ITEM_H)) {
        return 0;
    }
    steps = wheel < 0 ? (uint32_t)(-wheel) : (uint32_t)wheel;
    if (wheel > 0) {
        start_menu_scroll = start_menu_scroll > steps ? start_menu_scroll - steps : 0;
    } else {
        uint32_t max_scroll = count - list.rows;
        start_menu_scroll = start_menu_scroll + steps < max_scroll
                              ? start_menu_scroll + steps : max_scroll;
    }
    full_redraw_pending = 1;
    return 1;
}
