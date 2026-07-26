#include "desktop.h"

int start_menu_is_hidden_app(const char *name)
{
    return text_eq(name, "init.elf") ||
           text_eq(name, "desktop.elf") ||
           text_eq(name, "serviced.elf") ||
           text_eq(name, "oobe.elf") ||
           text_eq(name, "login.elf") ||
           text_eq(name, "shell.elf");
}

static void start_menu_add_package(const char *root, const char *package)
{
    char elf_name[LEONOS_FS_PATH_LEN];
    char path[LEONOS_FS_PATH_LEN];
    uint32_t pos = 0;
    struct leonos_stat st;
    if (!root || !package || !package[0] ||
        start_menu_app_count >= START_MENU_MAX_APPS) {
        return;
    }
    copy_text(elf_name, sizeof(elf_name), package);
    pos = 0;
    while (elf_name[pos]) {
        ++pos;
    }
    append_text(elf_name, &pos, sizeof(elf_name), ".elf");
    if (start_menu_is_hidden_app(elf_name)) {
        return;
    }
    copy_text(path, sizeof(path), root);
    pos = 0;
    while (path[pos]) {
        ++pos;
    }
    append_char(path, &pos, sizeof(path), '/');
    append_text(path, &pos, sizeof(path), package);
    append_char(path, &pos, sizeof(path), '/');
    append_text(path, &pos, sizeof(path), elf_name);
    if (stat(path, &st) < 0 || st.type != LEONOS_FS_TYPE_FILE) {
        return;
    }
    copy_app_label_from_elf(start_menu_app_labels[start_menu_app_count],
                            sizeof(start_menu_app_labels[start_menu_app_count]),
                            elf_name);
    copy_text(start_menu_app_paths[start_menu_app_count],
              sizeof(start_menu_app_paths[start_menu_app_count]), path);
    ++start_menu_app_count;
}

static void start_menu_load_root(const char *root)
{
    struct leonos_dir_entry entries[LEONOS_FS_MAX_ENTRIES];
    uint32_t count = 0;
    if (leonos_list_dir(root, entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < count && start_menu_app_count < START_MENU_MAX_APPS; ++i) {
        if (entries[i].type != LEONOS_FS_TYPE_DIR) {
            continue;
        }
        start_menu_add_package(root, entries[i].name);
    }
}

void start_menu_load_apps(void)
{
    start_menu_app_count = 0;
    start_menu_apps_loaded = 1;
    start_menu_load_root("0:/system/apps");
    start_menu_load_root("0:/programs");
}

void start_menu_ensure_apps(void)
{
    if (!start_menu_apps_loaded) {
        start_menu_load_apps();
    }
}

static void copy_hlp_filename_label(char *dst, uint32_t cap, const char *name)
{
    uint32_t len = 0;
    uint32_t out = 0;
    while (name && name[len]) {
        ++len;
    }
    if (len > 4 && text_ends_with(name, ".hlp")) {
        len -= 4;
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
        if (line[0] == '%' && line[1] == '%' && line[2] == 'D' && line[3] == 'O' && line[4] == 'C') {
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
    start_menu_docs_loaded = 1;
    if (leonos_list_dir("0:/docs", entries, LEONOS_FS_MAX_ENTRIES, &count) < 0) {
        return;
    }
    for (uint32_t i = 0; i < count && start_menu_doc_count < START_MENU_MAX_DOCS; ++i) {
        uint32_t pos = 0;
        if (entries[i].type != LEONOS_FS_TYPE_FILE ||
            !text_ends_with(entries[i].name, ".hlp")) {
            continue;
        }
        copy_text(start_menu_doc_paths[start_menu_doc_count],
                  sizeof(start_menu_doc_paths[start_menu_doc_count]),
                  "0:/docs/");
        while (start_menu_doc_paths[start_menu_doc_count][pos]) {
            ++pos;
        }
        append_text(start_menu_doc_paths[start_menu_doc_count], &pos,
                    sizeof(start_menu_doc_paths[start_menu_doc_count]),
                    entries[i].name);
        if (read_hlp_menu_title(start_menu_doc_paths[start_menu_doc_count],
                                start_menu_doc_labels[start_menu_doc_count],
                                sizeof(start_menu_doc_labels[start_menu_doc_count])) < 0) {
            copy_hlp_filename_label(start_menu_doc_labels[start_menu_doc_count],
                                    sizeof(start_menu_doc_labels[start_menu_doc_count]),
                                    entries[i].name);
        }
        ++start_menu_doc_count;
    }
}

void start_menu_ensure_docs(void)
{
    if (!start_menu_docs_loaded) {
        start_menu_load_docs();
    }
}

static int start_menu_contains(const char *text, const char *query)
{
    if (!query || !query[0]) {
        return 1;
    }
    for (uint32_t i = 0; text && text[i]; ++i) {
        uint32_t j = 0;
        while (query[j] && text[i + j] &&
               lower_ascii(text[i + j]) == lower_ascii(query[j])) {
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

uint32_t build_start_menu_items(struct start_menu_item *items, uint32_t cap)
{
    uint32_t count = 0;
#define ADD_ITEM(label_, type_, win_, path_) do { \
        if (count < cap) { \
            items[count++] = (struct start_menu_item){label_, type_, win_, path_}; \
        } \
    } while (0)
    ADD_ITEM(leonos_i18n("Desktop Server", "桌面服务"), START_ACTION_RESTORE, 0, 0);
    ADD_ITEM(leonos_i18n("Settings", "设置"), START_ACTION_SPAWN_ONCE, 0,
             "0:/system/apps/settings/settings.elf");
    ADD_ITEM("", START_ACTION_SEPARATOR, 0, 0);
    start_menu_ensure_apps();
    ADD_ITEM(leonos_i18n("Programs >", "程序 >"), START_ACTION_PROGRAMS, 0, 0);
    start_menu_ensure_docs();
    ADD_ITEM(leonos_i18n("Documents >", "文档 >"), START_ACTION_DOCUMENTS, 0, 0);
    uint32_t before_minimized = count;
    for (int zi = MAX_WINDOWS - 1; zi >= 0; --zi) {
        uint8_t id = z_order[zi];
        if (windows[id].visible && windows[id].minimized && count < cap) {
            if (count == before_minimized) {
                ADD_ITEM("", START_ACTION_SEPARATOR, 0, 0);
            }
            items[count++] = (struct start_menu_item){
                windows[id].title ? windows[id].title : leonos_i18n("Window", "窗口"),
                START_ACTION_RESTORE,
                id,
                0,
            };
        }
    }
    ADD_ITEM("", START_ACTION_SEPARATOR, 0, 0);
    if (desktop_session_logged_in()) {
        ADD_ITEM(leonos_i18n("Log Out", "注销"), START_ACTION_LOGOUT, 0, 0);
    }
    ADD_ITEM(leonos_i18n("Restart", "重启"), START_ACTION_REBOOT, 0, 0);
    ADD_ITEM(leonos_i18n("Shut Down", "关机"), START_ACTION_SHUTDOWN, 0, 0);
#undef ADD_ITEM
    return count;
}

uint32_t start_menu_height_for_count(uint32_t count)
{
    uint32_t h = 16 + count * START_MENU_ITEM_H;
    if (h > START_MENU_MAX_H) {
        h = START_MENU_MAX_H;
    }
    if (h < 80) {
        h = 80;
    }
    return h;
}

struct start_menu_layout start_menu_layout_for_count(uint32_t count)
{
    struct start_menu_layout layout;
    uint32_t progress = start_menu_progress();
    layout.x = 6;
    layout.w = START_MENU_W;
    layout.full_h = start_menu_height_for_count(count);
    layout.visible_h = (layout.full_h * progress + 99) / 100;
    if (layout.visible_h < 12 && (start_menu_open || start_menu_animating)) {
        layout.visible_h = 12;
    }
    layout.y = taskbar_y() > layout.visible_h ? taskbar_y() - layout.visible_h : 0;
    layout.visible_start = layout.full_h > layout.visible_h ? layout.full_h - layout.visible_h : 0;
    return layout;
}

static struct start_programs_layout start_submenu_layout_for_menu(struct start_menu_layout menu,
                                                                  uint32_t item_count,
                                                                  uint32_t item_w,
                                                                  uint32_t *scroll,
                                                                  uint32_t search_h)
{
    struct start_programs_layout layout;
    uint32_t max_h = taskbar_y() > 12 ? taskbar_y() - 12 : START_MENU_MAX_H;
    uint32_t available_right;
    uint32_t available_left;
    uint32_t max_cols;
    uint32_t rows;
    uint32_t chrome_h = 16 + search_h;
    uint32_t item_area_h = max_h > chrome_h ? max_h - chrome_h : START_MENU_ITEM_H;
    rows = item_area_h / START_MENU_ITEM_H;
    if (rows == 0) {
        rows = 1;
    }
    if (item_count && rows > item_count) {
        rows = item_count;
    }
    if (rows == 0) {
        rows = 1;
    }
    layout.cols = item_count > rows ? (item_count + rows - 1) / rows : 1;
    if (layout.cols == 0) {
        layout.cols = 1;
    }
    available_right = fb_w() > menu.x + menu.w ? fb_w() - (menu.x + menu.w) + 2 : 0;
    available_left = menu.x + 2;
    max_cols = available_right > available_left ? available_right : available_left;
    max_cols = max_cols / item_w;
    if (max_cols == 0) {
        max_cols = 1;
    }
    if (layout.cols > max_cols) {
        layout.cols = max_cols;
        rows = (item_count + layout.cols - 1) / layout.cols;
        if (rows == 0) {
            rows = 1;
        }
        if (rows * START_MENU_ITEM_H + chrome_h > max_h) {
            rows = item_area_h / START_MENU_ITEM_H;
        }
        if (rows == 0) {
            rows = 1;
        }
    }
    layout.rows = rows;
    layout.w = layout.cols * item_w;
    layout.h = chrome_h + rows * START_MENU_ITEM_H;
    layout.visible_count = layout.rows * layout.cols;
    if (layout.visible_count == 0) {
        layout.visible_count = 1;
    }
    if (scroll && *scroll >= item_count) {
        *scroll = item_count ? item_count - 1U : 0U;
    }
    if (scroll && *scroll + layout.visible_count > item_count) {
        *scroll = item_count > layout.visible_count
                      ? item_count - layout.visible_count
                      : 0U;
    }
    layout.x = menu.x + menu.w - 2;
    if (layout.x + layout.w > fb_w()) {
        layout.x = menu.x > layout.w ? menu.x - layout.w + 2 : 0;
    }
    layout.y = menu.y;
    if (layout.y + layout.h > taskbar_y()) {
        layout.y = taskbar_y() > layout.h ? taskbar_y() - layout.h : 0;
    }
    return layout;
}

struct start_programs_layout start_programs_layout_for_menu(struct start_menu_layout menu)
{
    return start_submenu_layout_for_menu(menu, start_menu_filtered_app_count(),
                                         START_PROGRAMS_W,
                                         &start_menu_programs_scroll,
                                         START_MENU_SEARCH_H);
}

struct start_programs_layout start_docs_layout_for_menu(struct start_menu_layout menu)
{
    start_menu_ensure_docs();
    return start_submenu_layout_for_menu(menu, start_menu_doc_count,
                                         START_DOCS_W,
                                         &start_menu_docs_scroll, 0);
}

void draw_start_programs_menu(struct start_menu_layout menu)
{
    uint32_t item_count;
    struct start_programs_layout layout;
    uint32_t search_y;
    if (!start_menu_programs_open) {
        return;
    }
    item_count = start_menu_filtered_app_count();
    layout = start_programs_layout_for_menu(menu);
    leonos_ui_menu(&ui, layout.x, layout.y, layout.w, layout.h);
    search_y = layout.y + 8;
    leonos_ui_text_field(&ui, layout.x + 8, search_y, layout.w - 16,
                         start_menu_query, LEONOS_UI_EDIT_FOCUSED);
    if (!start_menu_query[0]) {
        leonos_ui_text_clipped(&ui, layout.x + 15, search_y + 5, layout.w - 30,
                               leonos_i18n("Search applications", "搜索应用"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    if (!item_count) {
        leonos_ui_menu_item(&ui, layout.x + 34, search_y + START_MENU_SEARCH_H,
                            START_PROGRAMS_W - 44, leonos_i18n("No programs", "没有程序"),
                            LEONOS_UI_MENU_DISABLED);
        return;
    }
    for (uint32_t visible = 0; visible < layout.visible_count; ++visible) {
        uint32_t filtered_index = start_menu_programs_scroll + visible;
        uint32_t app_index = start_menu_filtered_app_index(filtered_index);
        uint32_t col = visible / layout.rows;
        uint32_t row = visible % layout.rows;
        uint32_t item_x = layout.x + 34 + col * START_PROGRAMS_W;
        uint32_t item_y = search_y + START_MENU_SEARCH_H + row * START_MENU_ITEM_H;
        char icon_path[LEONOS_FS_PATH_LEN];
        if (app_index >= start_menu_app_count) {
            break;
        }
        desktop_icon_path_for_app(start_menu_app_paths[app_index], icon_path, sizeof(icon_path));
        draw_app_icon(icon_path, (int)item_x - 22, (int)item_y + 4);
        leonos_ui_menu_item(&ui, item_x, item_y, START_PROGRAMS_W - 44,
                            start_menu_app_labels[app_index], 0);
    }
}

void draw_start_docs_menu(struct start_menu_layout menu)
{
    if (!start_menu_docs_open) {
        return;
    }
    struct start_programs_layout layout = start_docs_layout_for_menu(menu);
    leonos_ui_menu(&ui, layout.x, layout.y, layout.w, layout.h);
    if (!start_menu_doc_count) {
        leonos_ui_menu_item(&ui, layout.x + 34, layout.y + 8,
                            START_DOCS_W - 44, leonos_i18n("No documents", "没有文档"),
                            LEONOS_UI_MENU_DISABLED);
        return;
    }
    for (uint32_t visible = 0; visible < layout.visible_count; ++visible) {
        uint32_t i = start_menu_docs_scroll + visible;
        uint32_t col = visible / layout.rows;
        uint32_t row = visible % layout.rows;
        uint32_t item_x = layout.x + 34 + col * START_DOCS_W;
        uint32_t item_y = layout.y + 8 + row * START_MENU_ITEM_H;
        if (i >= start_menu_doc_count) {
            break;
        }
        draw_app_icon("0:/programs/oshlp/oshlp.bmp", (int)item_x - 22, (int)item_y + 4);
        leonos_ui_menu_item(&ui, item_x, item_y, START_DOCS_W - 44,
                            start_menu_doc_labels[i], 0);
    }
}

int start_menu_handle_key(uint8_t keycode, uint8_t pressed)
{
    char ch;
    uint32_t len = 0;
    if (!start_menu_open || start_menu_animating) {
        return 0;
    }
    if (!pressed) {
        return 1;
    }
    if (keycode == 1) {
        start_menu_set_open(0);
        return 1;
    }
    if (keycode == LEONOS_KEY_ENTER && start_menu_query[0]) {
        uint32_t app_index = start_menu_filtered_app_index(0);
        if (app_index < start_menu_app_count) {
            spawn_program_path(start_menu_app_paths[app_index]);
            start_menu_set_open(0);
        }
        return 1;
    }
    if (keycode == LEONOS_KEY_BACKSPACE) {
        while (start_menu_query[len]) {
            ++len;
        }
        if (len) {
            start_menu_query[len - 1U] = 0;
            start_menu_programs_scroll = 0;
            full_redraw_pending = 1;
        }
        return 1;
    }
    if (leonos_ui_keycode_to_char_shift(keycode,
                                        desktop_left_shift_down || desktop_right_shift_down,
                                        &ch)) {
        while (start_menu_query[len]) {
            ++len;
        }
        if (len + 1U < sizeof(start_menu_query)) {
            start_menu_query[len] = ch;
            start_menu_query[len + 1U] = 0;
            start_menu_programs_open = 1;
            start_menu_docs_open = 0;
            start_menu_programs_scroll = 0;
            full_redraw_pending = 1;
        }
        return 1;
    }
    return 1;
}

void draw_start_menu(void)
{
    if (!start_menu_open && !start_menu_animating) {
        return;
    }
    struct start_menu_item items[START_MENU_MAX_ITEMS];
    uint32_t count = build_start_menu_items(items, START_MENU_MAX_ITEMS);
    struct start_menu_layout layout = start_menu_layout_for_count(count);
    leonos_ui_menu(&ui, layout.x, layout.y, layout.w, layout.visible_h);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item_y_full = 8 + i * START_MENU_ITEM_H;
        if (item_y_full + START_MENU_ITEM_H <= layout.visible_start) {
            continue;
        }
        int item_y = (int)layout.y + (int)item_y_full - (int)layout.visible_start;
        if (item_y < (int)layout.y + 4 ||
            item_y + START_MENU_ITEM_H > (int)layout.y + (int)layout.visible_h) {
            continue;
        }
        if (items[i].type == START_ACTION_SEPARATOR) {
            leonos_ui_menu_item(&ui, layout.x + 34, (uint32_t)item_y + 8,
                                layout.w - 52,
                                "", LEONOS_UI_MENU_SEPARATOR);
        } else {
            char icon_path[LEONOS_FS_PATH_LEN];
            icon_path[0] = 0;
            if (items[i].type == START_ACTION_RESTORE && items[i].window_id < MAX_WINDOWS) {
                copy_text(icon_path, sizeof(icon_path), windows[items[i].window_id].icon_path);
            } else if (items[i].path) {
                desktop_icon_path_for_app(items[i].path, icon_path, sizeof(icon_path));
            }
            draw_app_icon(icon_path, (int)layout.x + 10, item_y + 4);
            leonos_ui_menu_item(&ui, layout.x + 34, (uint32_t)item_y,
                                layout.w - 52,
                                items[i].label, 0);
        }
    }
    draw_start_programs_menu(layout);
    draw_start_docs_menu(layout);
}
