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
        T("Default Program...", "默认程序..."), FILEMAN_ACTION_DEFAULT_PROGRAM,
        has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){
        T("Create Shortcut", "创建快捷方式"), FILEMAN_ACTION_CREATE_SHORTCUT,
        has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[4] = (struct leonos_ui_context_menu_item){
        T("Details", "详细信息"), FILEMAN_ACTION_DETAILS, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){
        T("Rename", "重命名"), FILEMAN_ACTION_RENAME, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[6] = (struct leonos_ui_context_menu_item){
        T("Delete", "删除"), FILEMAN_ACTION_DELETE, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[7] = (struct leonos_ui_context_menu_item){
        "", 0, LEONOS_UI_MENU_SEPARATOR};
    items[8] = (struct leonos_ui_context_menu_item){
        T("New Folder", "新建文件夹"), FILEMAN_ACTION_NEW_FOLDER, 0};
    items[9] = (struct leonos_ui_context_menu_item){
        T("Refresh", "刷新"), FILEMAN_ACTION_REFRESH, 0};
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

void show_details_selected(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    struct leonos_stat st;
    char path[LEONOS_FS_PATH_LEN];
    char size_line[56];
    char contains_line[72];
    struct folder_size_info folder_info = {0};
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

    window_id = leonos_gui_create_app_window_ex(T("Properties", "属性"), path,
                                                FILEMAN_DETAILS_W, FILEMAN_DETAILS_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        set_status_code("Details failed ", window_id);
        return;
    }
    leonos_ui_bind(&ui, details_pixels, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H,
                   FILEMAN_DETAILS_W);
    for (;;) {
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
        leonos_ui_dialog(&ui, 0, 0, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H, T("Properties", "属性"));
        leonos_ui_property_grid(&ui, 16, 46, FILEMAN_DETAILS_W - 32,
                                props, prop_count, 86, 24);
        leonos_ui_button(&ui, FILEMAN_DETAILS_W - 90, FILEMAN_DETAILS_H - 38,
                         72, LEONOS_UI_BUTTON_H, "OK", 0);
        leonos_gui_present_window((uint32_t)window_id, FILEMAN_DETAILS_W,
                                  FILEMAN_DETAILS_H, FILEMAN_DETAILS_W,
                                  details_pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                (event.keycode == LEONOS_KEY_ENTER || event.keycode == FILEMAN_KEY_ESCAPE)) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u) &&
                hit_rect_i(event.x, event.y, FILEMAN_DETAILS_W - 90, FILEMAN_DETAILS_H - 38,
                           72, (int32_t)LEONOS_UI_BUTTON_H)) {
                break;
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
        ret = leonos_readdir(fd, &entries[count]);
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
        ++count;
    }
    close(fd);
    entry_count = count;
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
    items[count++] = (struct leonos_ui_tree_item){"userland", 6, 1, LEONOS_UI_TREE_LEAF};
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
        return "0:/userland";
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
