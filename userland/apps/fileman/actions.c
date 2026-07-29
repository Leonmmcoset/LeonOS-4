#include "fileman.h"

void open_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    int pid;
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    if (entries[file_list.selected].type == LEONOS_FS_TYPE_DIR) {
        navigate_to_path(path);
        return;
    }
    if (ends_with(entries[file_list.selected].name, ".tar")) {
        extract_tar_with_path(path);
        reload_dir();
        return;
    }
    {
        char *argv[] = {path, 0};
        pid = leonos_launch_argv(argv);
    }
    if (pid < 0) {
        if (pid == LEONOS_LAUNCH_ERR_NO_ASSOCIATION) {
            show_open_with_for_path(path, 0);
        } else if (pid <= LEONOS_LAUNCH_ERR_EMPTY && pid >= LEONOS_LAUNCH_ERR_EXISTS) {
            set_status(leonos_launch_error_text(pid));
        } else {
            set_status_code("Launch failed ", pid);
        }
    } else {
        char buf[96];
        uint32_t pos = 0;
        buf[0] = 0;
        append_text(buf, &pos, sizeof(buf), "Launched pid ");
        append_dec(buf, &pos, sizeof(buf), (uint32_t)pid);
        append_text(buf, &pos, sizeof(buf), " from ");
        append_text(buf, &pos, sizeof(buf), entries[file_list.selected].name);
        set_status(buf);
    }
    printf("[fileman.elf] open path=%s pid=%d\n", path, pid);
}

void navigate_up(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (is_root_path(current_path)) {
        set_status("Already at root");
        return;
    }
    build_parent_path(path, sizeof(path));
    copy_text(current_path, sizeof(current_path), path);
    navigate_to_path(current_path);
}

void navigate_root(void)
{
    navigate_to_path("0:/");
}

void create_new_folder(void)
{
    char name[LEONOS_FS_NAME_LEN] = "New Folder";
    char path[LEONOS_FS_PATH_LEN];
    int ret;
    if (!leonos_ui_show_input_dialog(T("New Folder", "新建文件夹"), T("Folder name:", "文件夹名称:"), name, sizeof(name))) {
        set_status(T("New folder canceled", "已取消新建文件夹"));
        return;
    }
    if (!name[0]) {
        set_status(T("Folder name is empty", "文件夹名称为空"));
        return;
    }
    build_child_path(path, sizeof(path), name);
    ret = mkdir(path, 0);
    if (ret < 0) {
        set_status_error("Create folder failed ", ret);
        return;
    }
    reload_dir();
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (text_eq(entries[i].name, name)) {
            file_list.selected = (int32_t)i;
            if (i >= file_list.visible_rows) {
                file_list.scroll = i - file_list.visible_rows + 1;
            }
            break;
        }
    }
    set_status(T("Folder created", "文件夹已创建"));
}

void create_shortcut_for_selected(void)
{
    char target_path[LEONOS_FS_PATH_LEN];
    char dest_dir[LEONOS_FS_PATH_LEN];
    char shortcut_path[LEONOS_FS_PATH_LEN];
    const char *created_name;
    int to_desktop;
    int ret;
    if (!selected_entry_is_file()) {
        set_status(T("Select a file", "请选择一个文件"));
        return;
    }
    build_child_path(target_path, sizeof(target_path), entries[file_list.selected].name);
    to_desktop = leonos_ui_show_confirm_dialog(
        T("Create Shortcut", "创建快捷方式"),
        T("Place shortcut on Desktop? No creates it here.",
          "是否放到桌面？选择“否”则放在当前目录。"),
        1);
    if (to_desktop) {
        if (!home_path[0]) {
            refresh_home_path();
        }
        if (!home_path[0]) {
            set_status(T("Desktop folder unavailable", "桌面文件夹不可用"));
            return;
        }
        build_path_join(dest_dir, sizeof(dest_dir), home_path, "desktop");
    } else {
        copy_text(dest_dir, sizeof(dest_dir), current_path);
    }
    ret = leonos_launch_create_shortcut_in_dir(dest_dir, target_path,
                                               shortcut_path, sizeof(shortcut_path));
    if (ret < 0) {
        if (ret <= LEONOS_LAUNCH_ERR_EMPTY && ret >= LEONOS_LAUNCH_ERR_EXISTS) {
            set_status(leonos_launch_error_text(ret));
        } else {
            set_status_error("Create shortcut failed ", ret);
        }
        return;
    }
    if (text_eq(dest_dir, current_path)) {
        reload_dir();
        created_name = path_basename(shortcut_path);
        for (uint32_t i = 0; i < entry_count; ++i) {
            if (text_eq(entries[i].name, created_name)) {
                file_list.selected = (int32_t)i;
                if (i < file_list.scroll) {
                    file_list.scroll = i;
                } else if (i >= file_list.scroll + file_list.visible_rows) {
                    file_list.scroll = i - file_list.visible_rows + 1;
                }
                break;
            }
        }
    }
    set_status(T("Shortcut created", "快捷方式已创建"));
}

void rename_selected_entry(void)
{
    char old_path[LEONOS_FS_PATH_LEN];
    char new_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    int ret;
    if (!selected_entry_valid()) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    copy_text(name, sizeof(name), entries[file_list.selected].name);
    if (!leonos_ui_show_input_dialog(T("Rename", "重命名"), T("New name:", "新名称:"), name, sizeof(name))) {
        set_status(T("Rename canceled", "已取消重命名"));
        return;
    }
    if (!name[0]) {
        set_status(T("New name is empty", "新名称为空"));
        return;
    }
    build_child_path(old_path, sizeof(old_path), entries[file_list.selected].name);
    build_child_path(new_path, sizeof(new_path), name);
    ret = rename(old_path, new_path);
    if (ret < 0) {
        set_status_error("Rename failed ", ret);
        return;
    }
    reload_dir();
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (text_eq(entries[i].name, name)) {
            file_list.selected = (int32_t)i;
            if (i >= file_list.scroll + file_list.visible_rows) {
                file_list.scroll = i - file_list.visible_rows + 1;
            }
            break;
        }
    }
    set_status(T("Renamed", "已重命名"));
}

void delete_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char message[96];
    uint32_t pos = 0;
    int ret;
    if (!selected_entry_valid()) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    message[0] = 0;
    append_text(message, &pos, sizeof(message), T("Delete ", "删除 "));
    append_text(message, &pos, sizeof(message), entries[file_list.selected].name);
    append_char(message, &pos, sizeof(message), '?');
    if (!leonos_ui_show_confirm_dialog(T("Delete", "删除"), message, 0)) {
        set_status(T("Delete canceled", "已取消删除"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    ret = entries[file_list.selected].type == LEONOS_FS_TYPE_DIR ? rmdir(path) : unlink(path);
    if (ret < 0) {
        if (ret == -39) {
            set_status(T("Delete failed: directory not empty", "删除失败：目录非空"));
        } else {
            set_status_error("Delete failed ", ret);
        }
        return;
    }
    reload_dir();
    set_status(T("Deleted", "已删除"));
}

void execute_action(uint32_t action)
{
    context_menu_set_active(0);
    switch (action) {
    case FILEMAN_ACTION_OPEN:
        open_selected_entry();
        break;
    case FILEMAN_ACTION_OPEN_WITH:
        show_open_with_selected();
        break;
    case FILEMAN_ACTION_CREATE_SHORTCUT:
        create_shortcut_for_selected();
        break;
    case FILEMAN_ACTION_DEFAULT_PROGRAM:
        show_default_program_for_selected();
        break;
    case FILEMAN_ACTION_DETAILS:
        show_details_selected();
        break;
    case FILEMAN_ACTION_RENAME:
        rename_selected_entry();
        break;
    case FILEMAN_ACTION_DELETE:
        delete_selected_entry();
        break;
    case FILEMAN_ACTION_COPY:
        copy_selected_entries(0);
        break;
    case FILEMAN_ACTION_CUT:
        copy_selected_entries(1);
        break;
    case FILEMAN_ACTION_PASTE:
        paste_clipboard();
        break;
    case FILEMAN_ACTION_TOGGLE_MARK:
        fileman_toggle_selected();
        break;
    case FILEMAN_ACTION_SELECT_ALL:
        fileman_select_all();
        break;
    case FILEMAN_ACTION_CLEAR_SELECTION:
        fileman_clear_selection();
        break;
    case FILEMAN_ACTION_RECYCLE:
        recycle_selected_entries();
        break;
    case FILEMAN_ACTION_DELETE_PERMANENT:
        permanent_delete_selected_entries();
        break;
    case FILEMAN_ACTION_RESTORE:
        restore_selected_entry();
        break;
    case FILEMAN_ACTION_EMPTY_RECYCLE:
        empty_recycle_bin();
        break;
    case FILEMAN_ACTION_EXTRACT_TAR:
        extract_tar_selected();
        break;
    case FILEMAN_ACTION_COMPRESS_TAR:
        compress_selected_to_tar();
        break;
    case FILEMAN_ACTION_NEW_FOLDER:
        create_new_folder();
        break;
    case FILEMAN_ACTION_UP:
        navigate_up();
        break;
    case FILEMAN_ACTION_REFRESH:
        reload_dir();
        break;
    default:
        break;
    }
}

void extract_tar_with_path(const char *tar_path)
{
    char dest_dir[LEONOS_FS_PATH_LEN];
    uint32_t plen;
    const char *ext;
    if (!tar_path || !tar_path[0]) {
        return;
    }
    copy_text(dest_dir, sizeof(dest_dir), tar_path);
    plen = (uint32_t)text_len(dest_dir);
    ext = 0;
    if (plen > 4U && dest_dir[plen - 4U] == '.') {
        ext = dest_dir + plen - 4U;
    }
    if (ext && text_eq(ext, ".tar")) {
        dest_dir[plen - 4U] = 0;
    }
    if (mkdir(dest_dir, 0) < 0) {
        struct leonos_stat st;
        if (stat(dest_dir, &st) != 0 || st.type != LEONOS_FS_TYPE_DIR) {
            set_status_code("Extract mkdir failed ", -1);
            return;
        }
    }
    if (!leonos_tar_extract_all(tar_path, dest_dir)) {
        set_status(T("Tar extract failed", "Tar解压失败"));
        return;
    }
    set_status(T("Tar extracted successfully", "Tar解压成功"));
}

void extract_tar_selected(void)
{
    char tar_path[LEONOS_FS_PATH_LEN];
    if (!selected_entry_valid()) {
        set_status(T("Select a tar file", "请选择一个tar文件"));
        return;
    }
    build_child_path(tar_path, sizeof(tar_path),
                     entries[file_list.selected].name);
    fileman_operation_active = 1;
    fileman_operation_percent = 0;
    copy_text(fileman_operation_text, sizeof(fileman_operation_text),
              T("Extracting tar...", "正在解压tar..."));
    fileman_present_progress();
    extract_tar_with_path(tar_path);
    fileman_operation_active = 0;
    reload_dir();
}

void compress_selected_to_tar(void)
{
    char tar_path[LEONOS_FS_PATH_LEN];
    char src_path[LEONOS_FS_PATH_LEN];
    uint32_t count;
    uint32_t i;
    uint32_t has_any = 0;
    uint32_t packed_count = 0;
    int failed = 0;
    char base_name[LEONOS_FS_NAME_LEN];
    int tar_fd;
    count = fileman_selected_count();
    if (count == 0) {
        if (!selected_entry_valid()) {
            set_status(T("Mark files first", "请先标记文件"));
            return;
        }
        count = 1;
    }
    if (count == 1 && selected_entry_valid()) {
        copy_text(base_name, sizeof(base_name),
                  entries[file_list.selected].name);
    } else {
        copy_text(base_name, sizeof(base_name), "archive");
    }
    {
        uint32_t nlen = (uint32_t)text_len(base_name);
        uint32_t clen = (uint32_t)text_len(current_path);
        if (clen + nlen + 6U >= sizeof(tar_path)) {
            set_status(T("Path too long", "路径过长"));
            return;
        }
        memcpy(tar_path, current_path, clen);
        tar_path[clen] = '/';
        memcpy(tar_path + clen + 1U, base_name, nlen);
        memcpy(tar_path + clen + 1U + nlen, ".tar", 5U);
    }
    tar_fd = open(tar_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (tar_fd < 0) {
        set_status(T("Cannot create tar", "无法创建tar"));
        return;
    }
    fileman_operation_active = 1;
    fileman_operation_percent = 0;
    copy_text(fileman_operation_text, sizeof(fileman_operation_text),
              T("Compressing to tar...", "正在压缩为tar..."));
    fileman_present_progress();
    for (i = 0; i < entry_count && i < FILEMAN_MAX_ENTRIES; ++i) {
        if (count > 1 && !fileman_entry_marked(i)) {
            continue;
        }
        if (count == 1 && (uint32_t)file_list.selected != i) {
            continue;
        }
        build_child_path(src_path, sizeof(src_path), entries[i].name);
        if (entries[i].type == LEONOS_FS_TYPE_DIR) {
            if (!leonos_tar_pack_dir_append(tar_fd, src_path)) {
                set_status_code("Tar pack dir failed on ", -1);
                failed = 1;
                break;
            }
        } else {
            if (!leonos_tar_pack_file_append(tar_fd, src_path,
                                              entries[i].name)) {
                set_status_code("Tar pack file failed on ", -1);
                failed = 1;
                break;
            }
        }
        has_any = 1;
        ++packed_count;
        fileman_operation_percent =
            (uint32_t)(((uint64_t)packed_count * 100U) / count);
    }
    if (failed) {
        close(tar_fd);
        unlink(tar_path);
        fileman_operation_active = 0;
        reload_dir();
        return;
    }
    if (has_any) {
        if (!leonos_tar_finalize(tar_fd)) {
            close(tar_fd);
            unlink(tar_path);
            fileman_operation_active = 0;
            set_status(T("Tar finalize failed", "Tar收尾失败"));
            reload_dir();
            return;
        }
        close(tar_fd);
        set_status(T("Tar created successfully", "Tar创建成功"));
    } else {
        close(tar_fd);
        unlink(tar_path);
        set_status(T("No files compressed", "没有文件被压缩"));
    }
    fileman_operation_active = 0;
    reload_dir();
}
