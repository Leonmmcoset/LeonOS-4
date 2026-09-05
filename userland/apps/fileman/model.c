#include "fileman.h"

int is_root_path(const char *path)
{
    return path && path[0] == '/' && path[1] == 0;
}

int selected_entry_valid(void)
{
    return file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count;
}

int selected_entry_is_file(void)
{
    return selected_entry_valid() && entries[file_list.selected].type == LEONOS_FS_TYPE_FILE;
}

int selected_entry_is_mutable(void)
{
    return selected_entry_valid() && entries[file_list.selected].type != LEONOS_FS_TYPE_DEVICE;
}

int fileman_entry_marked(uint32_t index)
{
    return index < 64U && (selected_mask & (1ULL << index)) != 0;
}

uint32_t fileman_selected_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < entry_count && i < 64U; ++i) {
        if (fileman_entry_marked(i)) {
            ++count;
        }
    }
    return count;
}

void fileman_toggle_selected(void)
{
    if (!selected_entry_valid() || file_list.selected >= 64) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    selected_mask ^= 1ULL << (uint32_t)file_list.selected;
    set_status(fileman_entry_marked((uint32_t)file_list.selected)
                   ? T("Item marked", "已标记项目")
                   : T("Item unmarked", "已取消标记项目"));
}

void fileman_select_all(void)
{
    selected_mask = entry_count >= 64U ? UINT64_MAX :
                    (entry_count ? (1ULL << entry_count) - 1ULL : 0);
    set_status(T("All items marked", "已标记所有项目"));
}

void fileman_clear_selection(void)
{
    selected_mask = 0;
    set_status(T("Marks cleared", "已清除标记"));
}

int fileman_is_recycle_dir(void)
{
    char recycle[LEONOS_FS_PATH_LEN];
    if (!home_path[0] && !refresh_home_path()) {
        return 0;
    }
    build_path_join(recycle, sizeof(recycle), home_path, "recycle-bin");
    return text_eq(current_path, recycle);
}

int fileman_entry_is_hidden(const struct leonos_dir_entry *entry)
{
    return entry && entry->name[0] == '.';
}

static void fileman_settings_path(char *path, uint32_t capacity)
{
    struct leonos_user_info user;
    if (!path || capacity == 0) {
        return;
    }
    path[0] = 0;
    if (leonos_auth_current(&user) == 0 && user.uid && user.home[0]) {
        build_path_join(path, capacity, user.home, ".fileman.conf");
        return;
    }
    (void)mkdir("/var", 0);
    build_path_join(path, capacity, "/var", ".fileman.conf");
}

static uint8_t fileman_settings_show_hidden_value(const char *config)
{
    static const char key[] = "show_hidden=";
    uint32_t pos = 0;
    while (config && config[pos]) {
        uint32_t start = pos;
        uint32_t key_pos = 0;
        while (config[pos] && config[pos] != '\n' && config[pos] != '\r') {
            ++pos;
        }
        while (key[key_pos] && start + key_pos < pos &&
               config[start + key_pos] == key[key_pos]) {
            ++key_pos;
        }
        if (!key[key_pos] && start + key_pos < pos) {
            char value = config[start + key_pos];
            return value == '1' || value == 'y' || value == 'Y' ||
                   value == 't' || value == 'T';
        }
        while (config[pos] == '\n' || config[pos] == '\r') {
            ++pos;
        }
    }
    return 0;
}

void fileman_settings_load(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char config[128];
    uint32_t length = 0;
    int fd;
    fileman_show_hidden = 0;
    fileman_settings_path(path, sizeof(path));
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return;
    }
    while (length + 1u < sizeof(config)) {
        long got = read(fd, config + length, sizeof(config) - length - 1u);
        if (got <= 0) {
            break;
        }
        length += (uint32_t)got;
    }
    close(fd);
    config[length] = 0;
    fileman_show_hidden = fileman_settings_show_hidden_value(config);
}

static int fileman_settings_save(uint8_t show_hidden)
{
    char path[LEONOS_FS_PATH_LEN];
    static const char enabled[] = "version=1\nshow_hidden=1\n";
    static const char disabled[] = "version=1\nshow_hidden=0\n";
    const char *config = show_hidden ? enabled : disabled;
    uint32_t length = text_len(config);
    int fd;
    fileman_settings_path(path, sizeof(path));
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    if (write(fd, config, length) != (long)length) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

void fileman_open_settings(void)
{
    fileman_settings_show_hidden = fileman_show_hidden;
    fileman_settings_open = 1;
    menu_open = FILEMAN_MENU_NONE;
    context_menu_set_active(0);
}

void fileman_cancel_settings(void)
{
    fileman_settings_open = 0;
}

void fileman_apply_settings(void)
{
    int ret = fileman_settings_save(fileman_settings_show_hidden);
    if (ret < 0) {
        set_status_error(T("Could not save file manager settings ",
                           "无法保存文件资源管理器设置 "), ret);
        return;
    }
    fileman_show_hidden = fileman_settings_show_hidden;
    fileman_settings_open = 0;
    fileman_tree_reset();
    (void)reload_dir();
    set_status(T("File Manager settings saved", "文件资源管理器设置已保存"));
}

void fileman_settings_dialog_rect(struct leonos_ui_rect *out)
{
    uint32_t width = view_w > 16u ? view_w - 16u : view_w;
    uint32_t height = view_h > 16u ? view_h - 16u : view_h;
    if (!out) {
        return;
    }
    if (width > FILEMAN_SETTINGS_DIALOG_W) {
        width = FILEMAN_SETTINGS_DIALOG_W;
    }
    if (height > FILEMAN_SETTINGS_DIALOG_H) {
        height = FILEMAN_SETTINGS_DIALOG_H;
    }
    out->w = width;
    out->h = height;
    out->x = (int32_t)(view_w > width ? (view_w - width) / 2u : 0u);
    out->y = (int32_t)(view_h > height ? (view_h - height) / 2u : 0u);
}

int fileman_handle_settings_click(int32_t x, int32_t y)
{
    struct leonos_ui_rect rect;
    uint32_t button_y;
    if (!fileman_settings_open) {
        return 0;
    }
    fileman_settings_dialog_rect(&rect);
    button_y = (uint32_t)rect.y + rect.h - 34u;
    if (hit_rect_i(x, y, rect.x + 18, rect.y + 52,
                   (int32_t)(rect.w > 36u ? rect.w - 36u : rect.w), 30)) {
        fileman_settings_show_hidden = fileman_settings_show_hidden ? 0 : 1;
        return 1;
    }
    if (hit_rect_i(x, y, rect.x + (int32_t)rect.w - 168, (int32_t)button_y,
                   72, LEONOS_UI_BUTTON_H)) {
        fileman_apply_settings();
        return 1;
    }
    if (hit_rect_i(x, y, rect.x + (int32_t)rect.w - 88, (int32_t)button_y,
                   72, LEONOS_UI_BUTTON_H)) {
        fileman_cancel_settings();
        return 1;
    }
    return 1;
}

int fileman_handle_settings_key(uint8_t keycode)
{
    if (!fileman_settings_open) {
        return 0;
    }
    if (keycode == FILEMAN_KEY_ESCAPE) {
        fileman_cancel_settings();
    } else if (keycode == LEONOS_KEY_ENTER) {
        fileman_apply_settings();
    }
    return 1;
}

int list_index_at(int32_t x, int32_t y)
{
    struct fileman_layout l = current_layout();
    int32_t row;
    uint32_t index;
    if (!hit_rect_i(x, y, (int32_t)(l.list_x + 2), (int32_t)l.rows_y,
                    (int32_t)l.list_w, (int32_t)(l.visible_rows * ROW_H))) {
        return -1;
    }
    row = (y - (int32_t)l.rows_y) / (int32_t)ROW_H;
    if (row < 0) {
        return -1;
    }
    index = file_list.scroll + (uint32_t)row;
    if (index >= entry_count) {
        return -1;
    }
    return (int)index;
}

void format_size_text(char *buf, uint32_t cap, uint64_t bytes)
{
    static const char *units[] = {"Byte", "KB", "MB", "GB", "TB"};
    uint64_t whole = bytes;
    uint64_t frac = 0;
    uint32_t unit = 0;
    uint32_t pos = 0;
    while (whole >= 1024 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        frac = ((whole % 1024) * 100) / 1024;
        whole /= 1024;
        ++unit;
    }
    buf[0] = 0;
    append_size(buf, &pos, cap, whole);
    if (unit > 0 && frac > 0) {
        append_char(buf, &pos, cap, '.');
        append_char(buf, &pos, cap, (char)('0' + frac / 10));
        append_char(buf, &pos, cap, (char)('0' + frac % 10));
    }
    append_char(buf, &pos, cap, ' ');
    append_text(buf, &pos, cap, units[unit]);
    if (unit == 0 && bytes != 1) {
        append_char(buf, &pos, cap, 's');
    }
    if (unit > 0) {
        append_text(buf, &pos, cap, " (");
        append_size(buf, &pos, cap, bytes);
        append_text(buf, &pos, cap, " bytes)");
    }
}

void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
}

void set_status_code(const char *prefix, int value)
{
    char buf[96];
    uint32_t pos = 0;
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), prefix);
    if (value < 0) {
        append_char(buf, &pos, sizeof(buf), '-');
        value = -value;
    }
    append_dec(buf, &pos, sizeof(buf), (uint32_t)value);
    set_status(buf);
}

int permission_error(int value)
{
    return value == -LEONOS_EPERM || value == -LEONOS_EACCES;
}

void set_status_error(const char *prefix, int value)
{
    if (permission_error(value)) {
        set_status(T("Permission denied", "权限被拒绝"));
    } else {
        set_status_code(prefix, value);
    }
}

int refresh_home_path(void)
{
    struct leonos_user_info user;
    home_path[0] = 0;
    if (leonos_auth_current(&user) == 0 && user.uid && user.home[0]) {
        copy_text(home_path, sizeof(home_path), user.home);
        return 1;
    }
    return 0;
}

void context_menu_set_active(uint8_t active)
{
    if (context_menu_active == active && !context_menu_animating) {
        return;
    }
    context_menu_active = active;
    context_menu_opening = active;
    context_menu_animating = 1;
    context_menu_anim_start = leonos_uptime_ms();
}

void build_child_path(char *dst, uint32_t dst_len, const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, dst_len, current_path);
    if (!is_root_path(current_path)) {
        append_char(dst, &pos, dst_len, '/');
    }
    append_text(dst, &pos, dst_len, name);
}

void build_path_join(char *dst, uint32_t dst_len, const char *parent, const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, dst_len, parent);
    if (!is_root_path(parent)) {
        append_char(dst, &pos, dst_len, '/');
    }
    append_text(dst, &pos, dst_len, name);
}

void build_parent_path(char *dst, uint32_t dst_len)
{
    uint32_t len;
    copy_text(dst, dst_len, current_path);
    if (is_root_path(dst)) {
        return;
    }
    len = text_len(dst);
    while (len > 1 && dst[len - 1] != '/') {
        dst[--len] = 0;
    }
    if (len > 1) {
        dst[len - 1] = 0;
    }
}

const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path) {
        return "";
    }
    for (uint32_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1;
        }
    }
    return base ? base : "";
}

const char *entry_type_name(const struct leonos_dir_entry *entry)
{
    if (!entry) {
        return "FILE";
    }
    if (entry->type == LEONOS_FS_TYPE_DIR) {
        return "DIR ";
    }
    if (entry->type == LEONOS_FS_TYPE_DEVICE) {
        return "DEV ";
    }
    if (ends_with(entry->name, ".elf")) {
        return "ELF ";
    }
    if (ends_with(entry->name, ".lnk")) {
        return "LNK ";
    }
    return "FILE";
}

void build_context_menu_items(struct leonos_ui_context_menu_item *items,
                                      uint32_t count)
{
    uint32_t has_item = selected_entry_valid();
    uint32_t has_file = selected_entry_is_file();
    uint32_t has_mutable = selected_entry_is_mutable();
    if (!items || count < FILEMAN_CONTEXT_MENU_COUNT) {
        return;
    }
    items[0] = (struct leonos_ui_context_menu_item){
        T("Open", "打开"), FILEMAN_ACTION_OPEN, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){
        T("Open With...", "打开方式..."), FILEMAN_ACTION_OPEN_WITH, has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){
        T("Copy", "复制"), FILEMAN_ACTION_COPY, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){
        T("Cut", "剪切"), FILEMAN_ACTION_CUT, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[4] = (struct leonos_ui_context_menu_item){
        T("Paste", "粘贴"), FILEMAN_ACTION_PASTE,
        fileman_clipboard_available() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){
        fileman_entry_marked((uint32_t)(file_list.selected < 0 ? 0 : file_list.selected))
            ? T("Unmark", "取消标记") : T("Mark for Batch", "标记为批量操作"),
        FILEMAN_ACTION_TOGGLE_MARK, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[6] = (struct leonos_ui_context_menu_item){"", 0, LEONOS_UI_MENU_SEPARATOR};
    items[7] = (struct leonos_ui_context_menu_item){
        T("Rename", "重命名"), FILEMAN_ACTION_RENAME,
        has_mutable && fileman_selected_count() <= 1U ? 0 : LEONOS_UI_MENU_DISABLED};
    items[8] = (struct leonos_ui_context_menu_item){
        T("Move to Recycle Bin", "移到回收站"), FILEMAN_ACTION_RECYCLE,
        has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[9] = (struct leonos_ui_context_menu_item){
        T("Delete Permanently", "永久删除"), FILEMAN_ACTION_DELETE_PERMANENT,
        has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[10] = (struct leonos_ui_context_menu_item){
        T("Details", "详细信息"), FILEMAN_ACTION_DETAILS,
        has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[11] = (struct leonos_ui_context_menu_item){
        T("Refresh", "刷新"), FILEMAN_ACTION_REFRESH, 0};
    items[12] = (struct leonos_ui_context_menu_item){"", 0, LEONOS_UI_MENU_SEPARATOR};
    items[13] = (struct leonos_ui_context_menu_item){
        has_file && ends_with(entries[file_list.selected].name, ".tar")
            ? T("Extract tar", "解压tar")
            : T("Compress to .tar", "压缩为tar"),
        has_file && ends_with(entries[file_list.selected].name, ".tar")
            ? FILEMAN_ACTION_EXTRACT_TAR
            : FILEMAN_ACTION_COMPRESS_TAR,
        has_item ? 0 : LEONOS_UI_MENU_DISABLED};
}

void build_file_menu_items(struct leonos_ui_context_menu_item *items, uint32_t count)
{
    uint32_t has_item = selected_entry_valid();
    uint32_t has_file = selected_entry_is_file();
    uint32_t has_mutable = selected_entry_is_mutable();
    if (!items || count < FILEMAN_FILE_MENU_COUNT) {
        return;
    }
    items[0] = (struct leonos_ui_context_menu_item){T("Open", "打开"), FILEMAN_ACTION_OPEN,
                                                     has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){T("Open With...", "打开方式..."), FILEMAN_ACTION_OPEN_WITH,
                                                     has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){T("Default Program...", "默认程序..."), FILEMAN_ACTION_DEFAULT_PROGRAM,
                                                     has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){T("Create Shortcut", "创建快捷方式"), FILEMAN_ACTION_CREATE_SHORTCUT,
                                                     has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[4] = (struct leonos_ui_context_menu_item){T("Details", "详细信息"), FILEMAN_ACTION_DETAILS,
                                                     has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){"", 0, LEONOS_UI_MENU_SEPARATOR};
    items[6] = (struct leonos_ui_context_menu_item){T("Rename", "重命名"), FILEMAN_ACTION_RENAME,
                                                     has_mutable && fileman_selected_count() <= 1U ? 0 : LEONOS_UI_MENU_DISABLED};
    items[7] = (struct leonos_ui_context_menu_item){T("New Folder", "新建文件夹"), FILEMAN_ACTION_NEW_FOLDER, 0};
    items[8] = (struct leonos_ui_context_menu_item){T("Refresh", "刷新"), FILEMAN_ACTION_REFRESH, 0};
}

void build_edit_menu_items(struct leonos_ui_context_menu_item *items, uint32_t count)
{
    uint32_t has_mutable = selected_entry_is_mutable();
    if (!items || count < FILEMAN_EDIT_MENU_COUNT) {
        return;
    }
    items[0] = (struct leonos_ui_context_menu_item){T("Copy", "复制"), FILEMAN_ACTION_COPY,
                                                     has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){T("Cut", "剪切"), FILEMAN_ACTION_CUT,
                                                     has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){T("Paste", "粘贴"), FILEMAN_ACTION_PASTE,
                                                     fileman_clipboard_available() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){"", 0, LEONOS_UI_MENU_SEPARATOR};
    items[4] = (struct leonos_ui_context_menu_item){
        fileman_entry_marked((uint32_t)(file_list.selected < 0 ? 0 : file_list.selected))
            ? T("Unmark", "取消标记") : T("Mark for Batch", "标记为批量操作"),
        FILEMAN_ACTION_TOGGLE_MARK, selected_entry_valid() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){T("Mark All", "标记全部"), FILEMAN_ACTION_SELECT_ALL,
                                                     entry_count ? 0 : LEONOS_UI_MENU_DISABLED};
    items[6] = (struct leonos_ui_context_menu_item){T("Clear Marks", "清除标记"), FILEMAN_ACTION_CLEAR_SELECTION,
                                                     fileman_selected_count() ? 0 : LEONOS_UI_MENU_DISABLED};
}

void build_recycle_menu_items(struct leonos_ui_context_menu_item *items, uint32_t count)
{
    uint32_t has_item = selected_entry_valid();
    uint32_t has_mutable = selected_entry_is_mutable();
    uint32_t recycle = fileman_is_recycle_dir();
    if (!items || count < FILEMAN_RECYCLE_MENU_COUNT) {
        return;
    }
    items[0] = (struct leonos_ui_context_menu_item){T("Move to Recycle Bin", "移到回收站"), FILEMAN_ACTION_RECYCLE,
                                                     !recycle && has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){T("Restore", "还原"), FILEMAN_ACTION_RESTORE,
                                                     recycle && has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){T("Delete Permanently", "永久删除"), FILEMAN_ACTION_DELETE_PERMANENT,
                                                     has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){T("Empty Recycle Bin", "清空回收站"), FILEMAN_ACTION_EMPTY_RECYCLE,
                                                     recycle && entry_count ? 0 : LEONOS_UI_MENU_DISABLED};
}

void format_contains_text(char *buf, uint32_t cap, const struct folder_size_info *info)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, info ? info->files : 0);
    append_text(buf, &pos, cap, T(" files, ", " 个文件, "));
    append_dec(buf, &pos, cap, info ? info->folders : 0);
    append_text(buf, &pos, cap, T(" folders", " 个文件夹"));
    if (info && info->partial) {
        append_text(buf, &pos, cap, T(" (partial)", " (部分)"));
    }
}

int accumulate_folder_size(const char *path, struct folder_size_info *info, uint32_t depth)
{
    struct leonos_dir_entry entry;
    int fd;
    int ret;
    if (!path || !info) {
        return -1;
    }
    if (depth >= FILEMAN_FOLDER_SIZE_MAX_DEPTH ||
        info->visited >= FILEMAN_FOLDER_SIZE_MAX_ITEMS) {
        info->partial = 1;
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        info->partial = 1;
        return fd;
    }
    for (;;) {
        char child[LEONOS_FS_PATH_LEN];
        struct leonos_stat st;
        ret = leonos_readdir(fd, &entry);
        if (ret < 0) {
            info->partial = 1;
            close(fd);
            return ret;
        }
        if (ret == 0) {
            break;
        }
        if (info->visited >= FILEMAN_FOLDER_SIZE_MAX_ITEMS) {
            info->partial = 1;
            break;
        }
        ++info->visited;
        build_path_join(child, sizeof(child), path, entry.name);
        if (leonos_stat_legacy(child, &st) < 0) {
            info->partial = 1;
            continue;
        }
        if (st.type == LEONOS_FS_TYPE_DIR) {
            ++info->folders;
            (void)accumulate_folder_size(child, info, depth + 1);
        } else if (st.type == LEONOS_FS_TYPE_FILE) {
            ++info->files;
            info->bytes += st.size;
        }
    }
    close(fd);
    return 0;
}

static const uint32_t acl_principals[] = {
    LEONOS_FS_ACL_PRINCIPAL_OWNER,
    LEONOS_FS_ACL_PRINCIPAL_SYSTEM,
    LEONOS_FS_ACL_PRINCIPAL_ADMINISTRATORS,
    LEONOS_FS_ACL_PRINCIPAL_USERS,
    LEONOS_FS_ACL_PRINCIPAL_EVERYONE,
};

static const char *acl_principal_label(uint32_t principal)
{
    switch (principal) {
    case LEONOS_FS_ACL_PRINCIPAL_OWNER:
        return T("Owner", "所有者");
    case LEONOS_FS_ACL_PRINCIPAL_SYSTEM:
        return "System";
    case LEONOS_FS_ACL_PRINCIPAL_ADMINISTRATORS:
        return T("Administrators", "管理员");
    case LEONOS_FS_ACL_PRINCIPAL_USERS:
        return T("Users", "用户");
    case LEONOS_FS_ACL_PRINCIPAL_EVERYONE:
        return "Everyone";
    default:
        return "?";
    }
}

static int acl_find_ace(struct leonos_fs_acl *acl, uint32_t principal)
{
    for (uint32_t i = 0; acl && i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
        if (acl->aces[i].principal == principal) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t acl_permissions_for(const struct leonos_fs_acl *acl,
                                    uint32_t principal);

static void acl_toggle_permission(struct leonos_fs_acl *acl, uint32_t principal,
                                  uint32_t perm)
{
    int idx = acl_find_ace(acl, principal);
    uint32_t current = acl_permissions_for(acl, principal);
    if (!acl) {
        return;
    }
    if (current & perm) {
        for (uint32_t i = 0; i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
            if (acl->aces[i].principal == principal) {
                acl->aces[i].flags = 0;
                acl->aces[i].permissions &= ~perm;
            }
        }
        acl->flags &= ~(LEONOS_FS_ACL_FLAG_CORRUPT | LEONOS_FS_ACL_FLAG_SYNTHETIC);
        return;
    }
    if (idx < 0) {
        if (acl->ace_count >= LEONOS_FS_ACL_MAX_ACE) {
            return;
        }
        idx = (int)acl->ace_count++;
        acl->aces[idx] = (struct leonos_fs_acl_ace){
            .principal = principal,
            .flags = 0,
            .permissions = 0,
            .reserved = 0,
        };
    }
    acl->aces[idx].flags = 0;
    acl->aces[idx].permissions |= perm;
    acl->aces[idx].permissions &= LEONOS_FS_PERM_FULL;
    acl->flags &= ~(LEONOS_FS_ACL_FLAG_CORRUPT | LEONOS_FS_ACL_FLAG_SYNTHETIC);
}

static void acl_compact(struct leonos_fs_acl *acl)
{
    uint32_t out = 0;
    if (!acl) {
        return;
    }
    for (uint32_t i = 0; i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
        if ((acl->aces[i].permissions & LEONOS_FS_PERM_FULL) == 0) {
            continue;
        }
        acl->aces[out++] = acl->aces[i];
    }
    acl->ace_count = out;
}

static uint32_t acl_permissions_for(const struct leonos_fs_acl *acl,
                                    uint32_t principal)
{
    uint32_t permissions = 0;
    if (!acl) {
        return 0;
    }
    for (uint32_t i = 0; i < acl->ace_count && i < LEONOS_FS_ACL_MAX_ACE; ++i) {
        if (acl->aces[i].principal == principal) {
            permissions |= acl->aces[i].permissions;
        }
    }
    return permissions & LEONOS_FS_PERM_FULL;
}

static void acl_owner_text(uint32_t uid, char *buf, uint32_t cap)
{
    struct leonos_user_info users[LEONOS_AUTH_MAX_USERS];
    uint32_t count = 0;
    uint32_t pos = 0;
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = 0;
    if (uid == 0) {
        copy_text(buf, cap, "System");
        return;
    }
    if (leonos_auth_list_users(users, LEONOS_AUTH_MAX_USERS, 0, &count) == 0) {
        for (uint32_t i = 0; i < count; ++i) {
            if (users[i].uid == uid) {
                copy_text(buf, cap, users[i].username);
                return;
            }
        }
    }
    append_text(buf, &pos, cap, "uid ");
    append_dec(buf, &pos, cap, uid);
}

static void draw_acl_security_page(struct leonos_ui_surface *ui,
                                   const struct leonos_fs_acl *acl,
                                   const char *message)
{
    static const uint32_t perms[] = {
        LEONOS_FS_PERM_READ,
        LEONOS_FS_PERM_WRITE,
        LEONOS_FS_PERM_EXEC,
        LEONOS_FS_PERM_DELETE,
        LEONOS_FS_PERM_MANAGE,
    };
    static const char *perm_labels[] = {"R", "W", "X", "D", "M"};
    char owner[48];
    char state[96];
    uint32_t pos = 0;
    acl_owner_text(acl ? acl->owner_uid : 0, owner, sizeof(owner));
    state[0] = 0;
    append_text(state, &pos, sizeof(state), T("Owner: ", "所有者: "));
    append_text(state, &pos, sizeof(state), owner);
    if (acl && (acl->flags & LEONOS_FS_ACL_FLAG_SYNTHETIC)) {
        append_text(state, &pos, sizeof(state), T("  inherited/default", "  继承/默认"));
    }
    if (acl && (acl->flags & LEONOS_FS_ACL_FLAG_CORRUPT)) {
        append_text(state, &pos, sizeof(state), T("  corrupt", "  已损坏"));
    }
    leonos_ui_text(ui, 24, 50, state, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 24, 74, T("Principal", "主体"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 180, 74, T("Allow", "允许"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    for (uint32_t i = 0; i < 5; ++i) {
        leonos_ui_text(ui, 174 + i * 28, 94, perm_labels[i], LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    }
    for (uint32_t r = 0; r < 5; ++r) {
        uint32_t y = 118 + r * 30;
        uint32_t allow = acl_permissions_for(acl, acl_principals[r]);
        leonos_ui_text_clipped(ui, 24, y + 3, 136,
                               acl_principal_label(acl_principals[r]),
                               LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        for (uint32_t p = 0; p < 5; ++p) {
            leonos_ui_checkbox(ui, 172 + p * 28, y, "", (allow & perms[p]) != 0, 0);
        }
    }
    leonos_ui_text_clipped(ui, 24, 274, 330, message ? message : "",
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_button(ui, 24, FILEMAN_DETAILS_H - 38, 120, LEONOS_UI_BUTTON_H,
                     T("Take Owner", "接管所有权"), 0);
    leonos_ui_button(ui, 152, FILEMAN_DETAILS_H - 38, 88, LEONOS_UI_BUTTON_H,
                     T("Repair", "修复"), 0);
    leonos_ui_button(ui, 368, FILEMAN_DETAILS_H - 38, 82, LEONOS_UI_BUTTON_H,
                     T("Save", "保存"), 0);
}

static int acl_security_hit(struct leonos_fs_acl *acl, int32_t x, int32_t y)
{
    static const uint32_t perms[] = {
        LEONOS_FS_PERM_READ,
        LEONOS_FS_PERM_WRITE,
        LEONOS_FS_PERM_EXEC,
        LEONOS_FS_PERM_DELETE,
        LEONOS_FS_PERM_MANAGE,
    };
    for (uint32_t r = 0; r < 5; ++r) {
        uint32_t row_y = 118 + r * 30;
        for (uint32_t p = 0; p < 5; ++p) {
            if (hit_rect_i(x, y, 172 + (int32_t)p * 28, (int32_t)row_y, 18, 18)) {
                acl_toggle_permission(acl, acl_principals[r], perms[p]);
                return 1;
            }
        }
    }
    return 0;
}

void show_details_selected(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    struct leonos_stat st;
    struct leonos_fs_acl acl;
    char path[LEONOS_FS_PATH_LEN];
    char size_line[56];
    char contains_line[72];
    char acl_message[96];
    struct folder_size_info folder_info = {0};
    uint32_t active_tab = 0;
    struct leonos_ui_tab_state details_tabs;
    int acl_loaded;
    int window_id;
    if (!selected_entry_valid()) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    if (leonos_stat_legacy(path, &st) < 0) {
        set_status("Details stat failed");
        return;
    }
    if (st.type == LEONOS_FS_TYPE_DIR) {
        (void)accumulate_folder_size(path, &folder_info, 0);
        format_size_text(size_line, sizeof(size_line), folder_info.bytes);
        format_contains_text(contains_line, sizeof(contains_line), &folder_info);
    } else {
        format_size_text(size_line, sizeof(size_line), st.size);
        contains_line[0] = 0;
    }
    acl = (struct leonos_fs_acl){0};
    acl_message[0] = 0;
    acl_loaded = leonos_fs_acl_get(path, &acl);
    if (acl_loaded < 0) {
        set_status_error("ACL load failed ", acl_loaded);
        copy_text(acl_message, sizeof(acl_message), T("Could not load permissions", "无法加载权限"));
    }

    window_id = leonos_gui_create_app_window_ex(T("Properties", "属性"), path,
                                                FILEMAN_DETAILS_W, FILEMAN_DETAILS_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        set_status_code("Details failed ", window_id);
        return;
    }
    leonos_ui_bind(&ui, details_pixels, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H,
                   FILEMAN_DETAILS_W);
    leonos_ui_tab_state_init(&details_tabs, active_tab);
    for (;;) {
        struct leonos_ui_tab_item tabs[] = {
            {T("General", "常规"), 0, 0},
            {T("Security", "安全"), 1, 0},
        };
        struct leonos_ui_property_item props[5];
        uint32_t prop_count = 4;
        props[0] = (struct leonos_ui_property_item){T("Name:", "名称:"), entries[file_list.selected].name, 0};
        props[1] = (struct leonos_ui_property_item){T("Type:", "类型:"), entry_type_name(&entries[file_list.selected]), 0};
        props[2] = (struct leonos_ui_property_item){T("Path:", "路径:"), path, 0};
        props[3] = (struct leonos_ui_property_item){T("Size:", "大小:"), size_line, 0};
        if (st.type == LEONOS_FS_TYPE_DIR) {
            props[prop_count++] = (struct leonos_ui_property_item){T("Contains:", "包含:"), contains_line, 0};
        }
        leonos_ui_rect(&ui, 0, 0, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H, LEONOS_UI_GRAY);
        details_tabs.selected_id = active_tab;
        leonos_ui_tab_control(&ui, 16, 10, FILEMAN_DETAILS_W - 32, tabs, 2,
                              &details_tabs);
        leonos_ui_tab_body(&ui, 16, 36, FILEMAN_DETAILS_W - 32, FILEMAN_DETAILS_H - 84);
        if (active_tab == 0) {
            leonos_ui_property_grid(&ui, 28, 56, FILEMAN_DETAILS_W - 56,
                                    props, prop_count, 86, 24);
        } else if (acl_loaded == 0) {
            draw_acl_security_page(&ui, &acl, acl_message);
        } else {
            leonos_ui_text(&ui, 28, 56, T("Permission information is unavailable.",
                                          "权限信息不可用。"),
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
            leonos_ui_text_clipped(&ui, 28, 84, FILEMAN_DETAILS_W - 56,
                                   acl_message, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
            leonos_ui_button(&ui, 152, FILEMAN_DETAILS_H - 38, 88, LEONOS_UI_BUTTON_H,
                             T("Repair", "修复"), 0);
        }
        leonos_ui_button(&ui, FILEMAN_DETAILS_W - 90, FILEMAN_DETAILS_H - 38,
                         72, LEONOS_UI_BUTTON_H, "OK", 0);
        leonos_gui_present_window((uint32_t)window_id, FILEMAN_DETAILS_W,
                                  FILEMAN_DETAILS_H, FILEMAN_DETAILS_W,
                                  details_pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                (event.keycode == LEONOS_KEY_ENTER || event.keycode == FILEMAN_KEY_ESCAPE)) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (leonos_ui_tab_control_handle_mouse(&details_tabs, event.x, event.y,
                                                       16, 10, FILEMAN_DETAILS_W - 32,
                                                       tabs, 2)) {
                    active_tab = details_tabs.selected_id;
                    continue;
                }
                if (hit_rect_i(event.x, event.y, FILEMAN_DETAILS_W - 90,
                               FILEMAN_DETAILS_H - 38, 72,
                               (int32_t)LEONOS_UI_BUTTON_H)) {
                    break;
                }
                if (active_tab == 1) {
                    if (acl_loaded == 0 && acl_security_hit(&acl, event.x, event.y)) {
                        copy_text(acl_message, sizeof(acl_message),
                                  T("Unsaved changes", "有未保存的更改"));
                        continue;
                    }
                    if (hit_rect_i(event.x, event.y, 24, FILEMAN_DETAILS_H - 38,
                                   120, (int32_t)LEONOS_UI_BUTTON_H)) {
                        int ret = leonos_fs_acl_take_ownership(path, &acl);
                        acl_loaded = ret;
                        copy_text(acl_message, sizeof(acl_message),
                                  ret == 0 ? T("Ownership updated", "所有权已更新")
                                           : T("Take ownership failed", "接管所有权失败"));
                        continue;
                    }
                    if (hit_rect_i(event.x, event.y, 152, FILEMAN_DETAILS_H - 38,
                                   88, (int32_t)LEONOS_UI_BUTTON_H)) {
                        int ret = leonos_fs_acl_repair(path, &acl);
                        acl_loaded = ret;
                        copy_text(acl_message, sizeof(acl_message),
                                  ret == 0 ? T("Permissions repaired", "权限已修复")
                                           : T("Repair failed", "修复失败"));
                        continue;
                    }
                    if (acl_loaded == 0 &&
                        hit_rect_i(event.x, event.y, 368, FILEMAN_DETAILS_H - 38,
                                   82, (int32_t)LEONOS_UI_BUTTON_H)) {
                        int ret;
                        acl.version = LEONOS_FS_ACL_VERSION;
                        acl_compact(&acl);
                        ret = leonos_fs_acl_set(path, &acl);
                        if (ret == 0) {
                            acl_loaded = leonos_fs_acl_get(path, &acl);
                        }
                        copy_text(acl_message, sizeof(acl_message),
                                  ret == 0 ? T("Permissions saved", "权限已保存")
                                           : T("Save failed", "保存失败"));
                        continue;
                    }
                }
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
}

void show_open_with_for_path(const char *path, uint8_t set_default_only)
{
    char program[LEONOS_FS_PATH_LEN];
    char extension[16];
    uint32_t remember = set_default_only ? 1 : 0;
    uint32_t flags = set_default_only ? LEONOS_UI_OPEN_WITH_SET_DEFAULT : 0;
    int ret;
    int pid;
    program[0] = 0;
    extension[0] = 0;
    menu_open = FILEMAN_MENU_NONE;
    context_menu_set_active(0);
    ret = leonos_ui_show_open_with_dialog(set_default_only ? T("Default Program", "默认程序") : T("Open With", "打开方式"),
                                          path, program, sizeof(program),
                                          &remember, flags);
    if (ret < 0) {
        set_status_code("Open With failed ", ret);
        return;
    }
    if (ret == 0) {
        set_status(T("Open With canceled", "已取消打开方式"));
        return;
    }
    if (set_default_only || remember) {
        int assoc_ret;
        if (!leonos_launch_get_extension_for_path(path, extension, sizeof(extension))) {
            if (set_default_only) {
                set_status("This file has no extension to remember");
                return;
            }
        } else {
            assoc_ret = leonos_launch_set_extension_association(extension, program);
            if (assoc_ret < 0) {
                set_status_code("Save association failed ", assoc_ret);
                return;
            }
            if (set_default_only) {
                char buf[96];
                uint32_t pos = 0;
                buf[0] = 0;
                append_text(buf, &pos, sizeof(buf), "Default app set for ");
                append_text(buf, &pos, sizeof(buf), extension);
                set_status(buf);
                return;
            }
        }
    }

    pid = leonos_launch_file_with_app(path, program);
    printf("[fileman.elf] open-with path=%s app=%s pid=%d\n", path, program, pid);
    if (pid < 0) {
        set_status_code("Open With failed ", pid);
        return;
    }
    {
        char buf[96];
        uint32_t pos = 0;
        buf[0] = 0;
        append_text(buf, &pos, sizeof(buf), "Open With launched pid ");
        append_dec(buf, &pos, sizeof(buf), (uint32_t)pid);
        set_status(buf);
    }
}

void show_open_with_selected(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status(T("Select a file", "请选择一个文件"));
        return;
    }
    if (entries[file_list.selected].type != LEONOS_FS_TYPE_FILE) {
        set_status(T("Open With is for files", "打开方式仅用于文件"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    show_open_with_for_path(path, 0);
}

void show_default_program_for_selected(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status(T("Select a file", "请选择一个文件"));
        return;
    }
    if (entries[file_list.selected].type != LEONOS_FS_TYPE_FILE) {
        set_status(T("Default program is for files", "默认程序仅用于文件"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    show_open_with_for_path(path, 1);
}

static char sort_fold_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static int compare_entry_names(const char *left, const char *right)
{
    uint32_t index = 0;
    while (left[index] && right[index]) {
        char a = sort_fold_ascii(left[index]);
        char b = sort_fold_ascii(right[index]);
        if (a != b) {
            return (uint8_t)a < (uint8_t)b ? -1 : 1;
        }
        ++index;
    }
    if (left[index]) {
        return 1;
    }
    if (right[index]) {
        return -1;
    }
    return 0;
}

static int compare_entries(const struct leonos_dir_entry *left,
                           const struct leonos_dir_entry *right)
{
    uint32_t left_is_dir = left->type == LEONOS_FS_TYPE_DIR;
    uint32_t right_is_dir = right->type == LEONOS_FS_TYPE_DIR;
    if (left_is_dir != right_is_dir) {
        return left_is_dir ? -1 : 1;
    }
    return compare_entry_names(left->name, right->name);
}

static void sort_directory_entries(uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i) {
        struct leonos_dir_entry entry = entries[i];
        uint32_t slot = i;
        while (slot && compare_entries(&entry, &entries[slot - 1U]) < 0) {
            entries[slot] = entries[slot - 1U];
            --slot;
        }
        entries[slot] = entry;
    }
}

int reload_dir(void)
{
    int fd = open(current_path, 0, 0);
    int ret = 0;
    uint32_t count = 0;
    if (fd < 0) {
        entry_count = 0;
        leonos_ui_listview_state_set_count(&file_list, 0);
        set_status_error("Open dir failed ", fd);
        printf("[fileman.elf] list path=%s open=%d\n", current_path, fd);
        return fd;
    }
    leonos_ui_listview_state_set_count(&file_list, 0);
    while (count < FILEMAN_MAX_ENTRIES) {
        struct leonos_dir_entry entry;
        ret = leonos_readdir(fd, &entry);
        if (ret < 0) {
            close(fd);
            entry_count = 0;
            leonos_ui_listview_state_set_count(&file_list, 0);
            set_status_error("Read dir failed ", ret);
            printf("[fileman.elf] list path=%s readdir=%d\n", current_path, ret);
            return ret;
        }
        if (ret == 0) {
            break;
        }
        if (fileman_is_recycle_dir() && text_eq(entry.name, ".leon-recycle-map")) {
            continue;
        }
        if (!fileman_show_hidden && fileman_entry_is_hidden(&entry)) {
            continue;
        }
        entries[count] = entry;
        ++count;
    }
    close(fd);
    sort_directory_entries(count);
    entry_count = count;
    selected_mask = 0;
    leonos_ui_listview_state_set_count(&file_list, entry_count);
    file_list.selected = entry_count ? 0 : -1;
    file_list.scroll = 0;
    context_menu_set_active(0);
    last_click_index = -1;
    last_click_ms = 0;
    char buf[96];
    uint32_t pos = 0;
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), T("Items ", "项目 "));
    append_dec(buf, &pos, sizeof(buf), entry_count);
    append_text(buf, &pos, sizeof(buf), " in ");
    append_text(buf, &pos, sizeof(buf), current_path);
    set_status(buf);
    printf("[fileman.elf] list path=%s count=%d\n", current_path, (int)count);
    return 0;
}

static int tree_compare_nodes(const struct fileman_tree_node *left,
                              const struct fileman_tree_node *right)
{
    return compare_entry_names(left->label, right->label);
}

static int tree_node_index_for_id(uint32_t id)
{
    for (uint32_t i = 0; i < fileman_tree_node_count; ++i) {
        if (fileman_tree_nodes[i].used && fileman_tree_nodes[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int tree_node_index_for_path(const char *path)
{
    for (uint32_t i = 0; i < fileman_tree_node_count; ++i) {
        if (fileman_tree_nodes[i].used && text_eq(fileman_tree_nodes[i].path, path)) {
            return (int)i;
        }
    }
    return -1;
}

static int tree_dir_has_children(const char *path)
{
    struct leonos_dir_entry entry;
    int fd;
    int ret;
    if (!path) {
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    for (;;) {
        ret = leonos_readdir(fd, &entry);
        if (ret <= 0) {
            break;
        }
        if (entry.type == LEONOS_FS_TYPE_DIR &&
            (fileman_show_hidden || !fileman_entry_is_hidden(&entry))) {
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}

static void tree_add_node(const char *path, const char *label, uint32_t id,
                          uint32_t parent_id)
{
    struct fileman_tree_node *node;
    if (fileman_tree_node_count >= FILEMAN_TREE_MAX_NODES) {
        return;
    }
    node = &fileman_tree_nodes[fileman_tree_node_count++];
    node->used = 1;
    node->id = id;
    node->parent_id = parent_id;
    copy_text(node->path, sizeof(node->path), path);
    copy_text(node->label, sizeof(node->label), label);
    node->has_children = tree_dir_has_children(path) ? 1 : 0;
}

void fileman_tree_reset(void)
{
    for (uint32_t i = 0; i < FILEMAN_TREE_MAX_NODES; ++i) {
        fileman_tree_nodes[i] = (struct fileman_tree_node){0};
    }
    fileman_tree_node_count = 0;
    fileman_tree_next_id = 11;
    fileman_tree_scroll = 0;
    tree_add_node("/", "/", 1, 0);
}

/** Refresh the root tree so new /mnt and /media mounts become visible. */
static void fileman_tree_refresh_mounts(void)
{
    fileman_tree_reset();
}

static void tree_init_if_needed(void)
{
    if (fileman_tree_node_count == 0) {
        fileman_tree_reset();
    }
}

static void tree_load_children(uint32_t node_index)
{
    struct fileman_tree_node *node;
    struct leonos_dir_entry entry;
    uint32_t first_child;
    uint32_t added;
    int fd;
    int ret;
    if (node_index >= fileman_tree_node_count) {
        return;
    }
    node = &fileman_tree_nodes[node_index];
    if (!node->used || node->loaded) {
        return;
    }
    node->loaded = 1;
    first_child = fileman_tree_node_count;
    fd = open(node->path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        node->has_children = 0;
        return;
    }
    while (fileman_tree_node_count < FILEMAN_TREE_MAX_NODES) {
        char child_path[LEONOS_FS_PATH_LEN];
        ret = leonos_readdir(fd, &entry);
        if (ret <= 0) {
            break;
        }
        if (entry.type != LEONOS_FS_TYPE_DIR) {
            continue;
        }
        if (!fileman_show_hidden && fileman_entry_is_hidden(&entry)) {
            continue;
        }
        build_path_join(child_path, sizeof(child_path), node->path, entry.name);
        tree_add_node(child_path, entry.name, fileman_tree_next_id++, node->id);
    }
    close(fd);
    added = fileman_tree_node_count - first_child;
    if (added == 0) {
        node->has_children = 0;
        return;
    }
    for (uint32_t i = first_child + 1; i < fileman_tree_node_count; ++i) {
        struct fileman_tree_node child = fileman_tree_nodes[i];
        uint32_t slot = i;
        while (slot > first_child &&
               tree_compare_nodes(&child, &fileman_tree_nodes[slot - 1]) < 0) {
            fileman_tree_nodes[slot] = fileman_tree_nodes[slot - 1];
            --slot;
        }
        fileman_tree_nodes[slot] = child;
    }
}

static int tree_ensure_path(const char *path)
{
    char partial[LEONOS_FS_PATH_LEN];
    uint32_t pos;
    int index;
    if (!path || path[0] != '/') {
        return -1;
    }
    partial[0] = '/';
    partial[1] = 0;
    index = tree_node_index_for_path(partial);
    if (index < 0) {
        return -1;
    }
    pos = 1;
    while (path[pos]) {
        uint32_t end = pos;
        uint32_t partial_len;
        while (path[end] && path[end] != '/') {
            ++end;
        }
        if (end == pos) {
            pos = path[end] == '/' ? end + 1 : end;
            continue;
        }
        partial_len = text_len(partial);
        if (!is_root_path(partial)) {
            append_char(partial, &partial_len, sizeof(partial), '/');
        }
        while (pos < end) {
            append_char(partial, &partial_len, sizeof(partial), path[pos++]);
        }
        tree_load_children((uint32_t)index);
        index = tree_node_index_for_path(partial);
        if (index < 0) {
            return -1;
        }
        pos = path[end] == '/' ? end + 1 : end;
    }
    return index;
}

static void tree_expand_path(const char *path)
{
    int index;
    tree_init_if_needed();
    index = tree_ensure_path(path);
    while (index >= 0) {
        struct fileman_tree_node *node = &fileman_tree_nodes[index];
        if (node->has_children) {
            tree_load_children((uint32_t)index);
            if (node->has_children) {
                node->expanded = 1;
            }
        }
        index = node->parent_id ? tree_node_index_for_id(node->parent_id) : -1;
    }
}

static void tree_collapse_path(const char *path)
{
    int index;
    tree_init_if_needed();
    index = tree_ensure_path(path);
    if (index >= 0) {
        fileman_tree_nodes[index].expanded = 0;
    }
}

static void tree_scroll_to_path(const char *path)
{
    struct leonos_ui_tree_item items[FILEMAN_TREE_MAX_NODES];
    struct fileman_layout layout;
    uint32_t count;
    uint32_t visible_rows;
    if (!path) {
        return;
    }
    count = build_tree_items(items, sizeof(items) / sizeof(items[0]));
    layout = current_layout();
    visible_rows = fileman_tree_visible_rows(&layout);
    for (uint32_t i = 0; i < count; ++i) {
        const char *item_path = tree_path_for_id(items[i].id);
        if (!text_eq(path, item_path)) {
            continue;
        }
        if (i < fileman_tree_scroll) {
            fileman_tree_scroll = i;
        } else if (i >= fileman_tree_scroll + visible_rows) {
            fileman_tree_scroll = i - visible_rows + 1;
        }
        return;
    }
}

static int tree_path_is_direct_child(const char *parent, const char *child)
{
    uint32_t parent_len;
    uint32_t child_pos;
    if (!parent || !child || !parent[0] || !child[0] || text_eq(parent, child)) {
        return 0;
    }
    parent_len = text_len(parent);
    for (uint32_t i = 0; i < parent_len; ++i) {
        if (parent[i] != child[i]) {
            return 0;
        }
    }
    child_pos = parent_len;
    if (!is_root_path(parent)) {
        if (child[child_pos++] != '/') {
            return 0;
        }
    }
    if (!child[child_pos]) {
        return 0;
    }
    while (child[child_pos]) {
        if (child[child_pos++] == '/') {
            return 0;
        }
    }
    return 1;
}

static void fileman_tree_sync_navigation(const char *old_path, const char *new_path)
{
    if (tree_path_is_direct_child(old_path, new_path)) {
        tree_expand_path(new_path);
        tree_scroll_to_path(new_path);
    } else if (tree_path_is_direct_child(new_path, old_path)) {
        tree_collapse_path(old_path);
        tree_scroll_to_path(new_path);
    }
}

static void tree_append_visible(struct leonos_ui_tree_item *items, uint32_t cap,
                                uint32_t *count, uint32_t node_index,
                                uint32_t depth)
{
    struct fileman_tree_node *node;
    if (!items || !count || node_index >= fileman_tree_node_count || *count >= cap) {
        return;
    }
    node = &fileman_tree_nodes[node_index];
    if (!node->used) {
        return;
    }
    items[(*count)++] = (struct leonos_ui_tree_item){
        node->label, node->id, depth,
        node->has_children ? (node->expanded ? LEONOS_UI_TREE_EXPANDED : 0)
                           : LEONOS_UI_TREE_LEAF};
    if (!node->expanded) {
        return;
    }
    for (uint32_t i = 0; i < fileman_tree_node_count && *count < cap; ++i) {
        if (fileman_tree_nodes[i].used && fileman_tree_nodes[i].parent_id == node->id) {
            tree_append_visible(items, cap, count, i, depth + 1);
        }
    }
}

uint32_t build_tree_items(struct leonos_ui_tree_item *items, uint32_t cap)
{
    uint32_t count = 0;
    tree_init_if_needed();
    fileman_tree_refresh_mounts();
    if (!items || cap == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < fileman_tree_node_count && count < cap; ++i) {
        if (fileman_tree_nodes[i].used && fileman_tree_nodes[i].parent_id == 0) {
            tree_append_visible(items, cap, &count, i, 0);
        }
    }
    return count;
}

const char *tree_path_for_id(uint32_t id)
{
    int index;
    tree_init_if_needed();
    index = tree_node_index_for_id(id);
    return index >= 0 ? fileman_tree_nodes[index].path : 0;
}

int fileman_tree_toggle(uint32_t id)
{
    int index;
    tree_init_if_needed();
    index = tree_node_index_for_id(id);
    if (index < 0 || !fileman_tree_nodes[index].has_children) {
        return 0;
    }
    if (!fileman_tree_nodes[index].expanded) {
        tree_load_children((uint32_t)index);
        if (!fileman_tree_nodes[index].has_children) {
            return 0;
        }
    }
    fileman_tree_nodes[index].expanded = fileman_tree_nodes[index].expanded ? 0 : 1;
    return 1;
}

uint32_t fileman_tree_visible_rows(const struct fileman_layout *layout)
{
    uint32_t height;
    if (!layout || layout->tree_h <= 8) {
        return 1;
    }
    height = layout->tree_h - 8;
    return height / TREE_ROW_H ? height / TREE_ROW_H : 1;
}

int navigate_to_path(const char *path)
{
    char old_path[LEONOS_FS_PATH_LEN];
    char target[LEONOS_FS_PATH_LEN];
    int ret;
    if (!path || !path[0]) {
        return -1;
    }
    copy_text(old_path, sizeof(old_path), current_path);
    copy_text(target, sizeof(target), path);
    ret = chdir(target);
    if (ret < 0) {
        copy_text(current_path, sizeof(current_path), old_path);
        address_edit_sync_path();
        set_status_error("Open dir failed ", ret);
        return ret;
    }
    copy_text(current_path, sizeof(current_path), target);
    getcwd(current_path, sizeof(current_path));
    ret = reload_dir();
    if (ret == 0) {
        fileman_tree_sync_navigation(old_path, current_path);
    }
    address_edit_sync_path();
    return ret;
}

void address_edit_sync_path(void)
{
    if (!address_edit.buffer) {
        return;
    }
    copy_text(address_input, sizeof(address_input), current_path);
    leonos_ui_edit_state_sync(&address_edit);
    address_edit.focused = 0;
    address_edit.selecting = 0;
}
