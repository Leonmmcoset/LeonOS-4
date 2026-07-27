#include "fileman.h"

int is_root_path(const char *path)
{
    return text_eq(path, "0:/");
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
    if (!text_eq(parent, "0:/")) {
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
    while (len > 3 && dst[len - 1] != '/') {
        dst[--len] = 0;
    }
    if (len > 3) {
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
        if (stat(child, &st) < 0) {
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
    if (stat(path, &st) < 0) {
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

uint32_t build_tree_items(struct leonos_ui_tree_item *items, uint32_t cap)
{
    uint32_t count = 0;
    if (!items || cap < 5) {
        return 0;
    }
    items[count++] = (struct leonos_ui_tree_item){"0:/", 1, 0, LEONOS_UI_TREE_EXPANDED};
    if (home_path[0] && count < cap) {
        items[count++] = (struct leonos_ui_tree_item){T("Home", "主页"), 2, 1, LEONOS_UI_TREE_LEAF};
    }
    items[count++] = (struct leonos_ui_tree_item){"system", 3, 1, LEONOS_UI_TREE_LEAF};
    items[count++] = (struct leonos_ui_tree_item){"fonts", 4, 2, LEONOS_UI_TREE_LEAF};
    items[count++] = (struct leonos_ui_tree_item){"resources", 5, 2, LEONOS_UI_TREE_LEAF};
    items[count++] = (struct leonos_ui_tree_item){"programs", 6, 1, LEONOS_UI_TREE_LEAF};
    return count;
}

const char *tree_path_for_id(uint32_t id)
{
    switch (id) {
    case 1:
        return "0:/";
    case 2:
        return home_path[0] ? home_path : 0;
    case 3:
        return "0:/system";
    case 4:
        return "0:/system/fonts";
    case 5:
        return "0:/system/resources";
    case 6:
        return "0:/programs";
    default:
        return 0;
    }
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
        set_status_error("Open dir failed ", ret);
        return ret;
    }
    copy_text(current_path, sizeof(current_path), target);
    getcwd(current_path, sizeof(current_path));
    return reload_dir();
}
