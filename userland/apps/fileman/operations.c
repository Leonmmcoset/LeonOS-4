#include "fileman.h"

#define FILEMAN_CLIPBOARD_MAX FILEMAN_MAX_ENTRIES
#define FILEMAN_COPY_BUFFER_SIZE 1024U
#define FILEMAN_COPY_MAX_DEPTH 16U
#define FILEMAN_RECYCLE_MAP ".leon-recycle-map"

static char clipboard_paths[FILEMAN_CLIPBOARD_MAX][LEONOS_FS_PATH_LEN];
static uint32_t clipboard_count;
static uint8_t clipboard_cut;
static char recycle_map[4096];

static void operation_set(uint32_t percent, const char *text)
{
    fileman_operation_active = 1;
    fileman_operation_percent = percent > 100U ? 100U : percent;
    copy_text(fileman_operation_text, sizeof(fileman_operation_text), text);
    fileman_present_progress();
}

void fileman_present_progress(void)
{
    if (fileman_window_id) {
        present_fileman(fileman_window_id, &fileman_ui);
    }
}

static void operation_finish(const char *text)
{
    fileman_operation_percent = 100;
    copy_text(fileman_operation_text, sizeof(fileman_operation_text), text);
    fileman_present_progress();
    fileman_operation_active = 0;
    set_status(text);
}

static int build_recycle_dir(char *dst, uint32_t cap)
{
    if (!home_path[0] && !refresh_home_path()) {
        return -1;
    }
    build_path_join(dst, cap, home_path, "recycle-bin");
    return mkdir(dst, 0) < 0 && leonos_stat_legacy(dst, &(struct leonos_stat){0}) < 0 ? -1 : 0;
}

static void build_path_in_dir(char *dst, uint32_t cap, const char *dir,
                              const char *name)
{
    build_path_join(dst, cap, dir, name);
}

static void parent_path_of(char *dst, uint32_t cap, const char *path)
{
    uint32_t len;
    copy_text(dst, cap, path);
    len = text_len(dst);
    while (len > 1U && dst[len - 1U] != '/') {
        dst[--len] = 0;
    }
    if (len > 1U) {
        dst[len - 1U] = 0;
    }
}

static int path_is_same_or_child(const char *parent, const char *path)
{
    uint32_t i = 0;
    while (parent[i] && path[i] && parent[i] == path[i]) {
        ++i;
    }
    return !parent[i] && (!path[i] || path[i] == '/');
}

static void build_copy_name(char *dst, uint32_t cap, const char *name,
                            uint32_t serial)
{
    uint32_t pos = 0;
    uint32_t dot = 0;
    uint32_t len = text_len(name);
    for (uint32_t i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    if (!dot) {
        dot = len;
    }
    for (uint32_t i = 0; i < dot; ++i) {
        append_char(dst, &pos, cap, name[i]);
    }
    append_text(dst, &pos, cap, "-copy-");
    append_dec(dst, &pos, cap, serial);
    for (uint32_t i = dot; i < len; ++i) {
        append_char(dst, &pos, cap, name[i]);
    }
}

static int choose_target_path(const char *dir, const char *name,
                              char *dst, uint32_t cap)
{
    struct leonos_stat st;
    char candidate[LEONOS_FS_NAME_LEN];
    build_path_in_dir(dst, cap, dir, name);
    if (leonos_stat_legacy(dst, &st) < 0) {
        return 0;
    }
    if (leonos_ui_show_confirm_dialog(T("File Conflict", "文件冲突"),
                                      T("The destination exists. Replace it? Choose No to save with another name.",
                                        "目标已存在。要替换它吗？选择“否”将使用其他名称保存。"),
                                      0)) {
        return 1;
    }
    copy_text(candidate, sizeof(candidate), name);
    for (uint32_t serial = 2; serial < 100U; ++serial) {
        build_copy_name(candidate, sizeof(candidate), name, serial);
        build_path_in_dir(dst, cap, dir, candidate);
        if (leonos_stat_legacy(dst, &st) < 0) {
            return 0;
        }
    }
    return -1;
}

static int choose_free_target_path(const char *dir, const char *name,
                                   char *dst, uint32_t cap)
{
    struct leonos_stat st;
    char candidate[LEONOS_FS_NAME_LEN];
    build_path_in_dir(dst, cap, dir, name);
    if (leonos_stat_legacy(dst, &st) < 0) {
        return 0;
    }
    for (uint32_t serial = 2; serial < 100U; ++serial) {
        build_copy_name(candidate, sizeof(candidate), name, serial);
        build_path_in_dir(dst, cap, dir, candidate);
        if (leonos_stat_legacy(dst, &st) < 0) {
            return 0;
        }
    }
    return -1;
}

static int remove_tree(const char *path, uint32_t depth)
{
    struct leonos_stat st;
    int fd;
    int ret;
    if (depth > FILEMAN_COPY_MAX_DEPTH || leonos_stat_legacy(path, &st) < 0) {
        return -1;
    }
    if (st.type != LEONOS_FS_TYPE_DIR) {
        return unlink(path);
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    for (;;) {
        struct leonos_dir_entry entry;
        char child[LEONOS_FS_PATH_LEN];
        ret = leonos_readdir(fd, &entry);
        if (ret <= 0) {
            break;
        }
        build_path_in_dir(child, sizeof(child), path, entry.name);
        ret = remove_tree(child, depth + 1U);
        if (ret < 0) {
            close(fd);
            return ret;
        }
    }
    close(fd);
    return ret < 0 ? ret : rmdir(path);
}

static int copy_file(const char *src, const char *dst, uint64_t total,
                     uint64_t *done, uint32_t base_percent,
                     uint32_t span_percent)
{
    char buffer[FILEMAN_COPY_BUFFER_SIZE];
    int in = open(src, LEONOS_O_RDONLY, 0);
    int out;
    if (in < 0) {
        return in;
    }
    out = open(dst, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (out < 0) {
        close(in);
        return out;
    }
    for (;;) {
        long got = read(in, buffer, sizeof(buffer));
        if (got < 0) {
            close(in);
            close(out);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        if (write(out, buffer, (uint32_t)got) != got) {
            close(in);
            close(out);
            return -1;
        }
        *done += (uint32_t)got;
        if (total) {
            uint32_t progress = base_percent +
                (uint32_t)((*done * span_percent) / total);
            operation_set(progress, T("Copying files...", "正在复制文件..."));
        }
    }
    close(in);
    close(out);
    return 0;
}

static int copy_tree(const char *src, const char *dst, uint64_t total,
                     uint64_t *done, uint32_t base_percent,
                     uint32_t span_percent, uint32_t depth)
{
    struct leonos_stat st;
    int ret;
    if (depth > FILEMAN_COPY_MAX_DEPTH || leonos_stat_legacy(src, &st) < 0) {
        return -1;
    }
    if (st.type != LEONOS_FS_TYPE_DIR) {
        return copy_file(src, dst, total, done, base_percent, span_percent);
    }
    ret = mkdir(dst, 0);
    if (ret < 0 && leonos_stat_legacy(dst, &(struct leonos_stat){0}) < 0) {
        return ret;
    }
    {
        int fd = open(src, LEONOS_O_RDONLY, 0);
        if (fd < 0) {
            return fd;
        }
        for (;;) {
            struct leonos_dir_entry entry;
            char child_src[LEONOS_FS_PATH_LEN];
            char child_dst[LEONOS_FS_PATH_LEN];
            ret = leonos_readdir(fd, &entry);
            if (ret <= 0) {
                break;
            }
            build_path_in_dir(child_src, sizeof(child_src), src, entry.name);
            build_path_in_dir(child_dst, sizeof(child_dst), dst, entry.name);
            ret = copy_tree(child_src, child_dst, total, done, base_percent,
                            span_percent, depth + 1U);
            if (ret < 0) {
                close(fd);
                return ret;
            }
        }
        close(fd);
    }
    return ret < 0 ? ret : 0;
}

static uint64_t path_bytes(const char *path)
{
    struct leonos_stat st;
    struct folder_size_info info = {0};
    if (leonos_stat_legacy(path, &st) < 0) {
        return 0;
    }
    if (st.type == LEONOS_FS_TYPE_DIR) {
        (void)accumulate_folder_size(path, &info, 0);
        return info.bytes;
    }
    return st.size;
}

int fileman_clipboard_available(void)
{
    return clipboard_count != 0;
}

void copy_selected_entries(uint8_t cut)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < entry_count && i < FILEMAN_CLIPBOARD_MAX; ++i) {
        if (fileman_entry_marked(i) ||
            (!fileman_selected_count() && i == (uint32_t)(file_list.selected < 0 ? entry_count : file_list.selected))) {
            if (fileman_entry_is_device(i)) {
                continue;
            }
            build_child_path(clipboard_paths[count], sizeof(clipboard_paths[count]),
                             entries[i].name);
            ++count;
        }
    }
    clipboard_count = count;
    clipboard_cut = cut;
    set_status(count ? (cut ? T("Items cut. Open a folder and paste.", "项目已剪切。打开一个文件夹后粘贴。")
                            : T("Items copied. Open a folder and paste.", "项目已复制。打开一个文件夹后粘贴。"))
                     : T("Select an item", "请选择一个项目"));
}

void paste_clipboard(void)
{
    uint64_t total = 0;
    uint64_t done = 0;
    uint32_t completed = 0;
    uint32_t failed = 0;
    uint8_t was_cut = clipboard_cut;
    if (!clipboard_count) {
        set_status(T("Clipboard is empty", "剪贴板为空"));
        return;
    }
    for (uint32_t i = 0; i < clipboard_count; ++i) {
        total += path_bytes(clipboard_paths[i]);
    }
    operation_set(0, was_cut ? T("Moving files...", "正在移动文件...")
                             : T("Copying files...", "正在复制文件..."));
    for (uint32_t i = 0; i < clipboard_count; ++i) {
        char target[LEONOS_FS_PATH_LEN];
        char source_parent[LEONOS_FS_PATH_LEN];
        const char *name = path_basename(clipboard_paths[i]);
        int conflict;
        int ret;
        parent_path_of(source_parent, sizeof(source_parent), clipboard_paths[i]);
        if (text_eq(source_parent, current_path)) {
            if (was_cut || choose_free_target_path(current_path, name, target,
                                                   sizeof(target)) != 0) {
                ++failed;
                continue;
            }
            conflict = 0;
        } else {
            if (path_is_same_or_child(clipboard_paths[i], current_path)) {
                ++failed;
                continue;
            }
            conflict = choose_target_path(current_path, name, target, sizeof(target));
        }
        if (conflict < 0) {
            ++failed;
            continue;
        }
        if (conflict > 0) {
            ret = remove_tree(target, 0);
            if (ret < 0) {
                ++failed;
                continue;
            }
        }
        if (was_cut) {
            ret = rename(clipboard_paths[i], target);
            if (ret == 0) {
                done += path_bytes(target);
            } else {
                ret = copy_tree(clipboard_paths[i], target, total, &done, 0, 100, 0);
                if (ret == 0) {
                    ret = remove_tree(clipboard_paths[i], 0);
                }
            }
        } else {
            ret = copy_tree(clipboard_paths[i], target, total, &done, 0, 100, 0);
        }
        if (ret < 0) {
            ++failed;
        } else {
            ++completed;
        }
        operation_set(total ? (uint32_t)((done * 100ULL) / total)
                            : ((i + 1U) * 100U) / clipboard_count,
                      was_cut ? T("Moving files...", "正在移动文件...")
                              : T("Copying files...", "正在复制文件..."));
    }
    if (was_cut && !failed) {
        clipboard_count = 0;
        clipboard_cut = 0;
    }
    reload_dir();
    selected_mask = 0;
    if (failed) {
        char status[160];
        uint32_t pos = 0;
        status[0] = 0;
        append_dec(status, &pos, sizeof(status), completed);
        append_text(status, &pos, sizeof(status), T(" item(s) completed; ", " 个项目完成；"));
        append_dec(status, &pos, sizeof(status), failed);
        append_text(status, &pos, sizeof(status), T(" failed", " 个失败"));
        operation_finish(status);
    } else {
        operation_finish(was_cut ? T("Move complete", "移动完成")
                                 : T("Copy complete", "复制完成"));
    }
}

static int recycle_map_load(const char *dir)
{
    char path[LEONOS_FS_PATH_LEN];
    uint32_t len = 0;
    int fd;
    build_path_in_dir(path, sizeof(path), dir, FILEMAN_RECYCLE_MAP);
    recycle_map[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    while (len + 1U < sizeof(recycle_map)) {
        long got = read(fd, recycle_map + len, sizeof(recycle_map) - len - 1U);
        if (got <= 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    recycle_map[len] = 0;
    return 0;
}

static void recycle_map_save(const char *dir)
{
    char path[LEONOS_FS_PATH_LEN];
    int fd;
    build_path_in_dir(path, sizeof(path), dir, FILEMAN_RECYCLE_MAP);
    fd = open(path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd >= 0) {
        (void)write(fd, recycle_map, text_len(recycle_map));
        close(fd);
    }
}

static void recycle_map_append(const char *name, const char *origin)
{
    uint32_t pos = text_len(recycle_map);
    append_text(recycle_map, &pos, sizeof(recycle_map), name);
    append_char(recycle_map, &pos, sizeof(recycle_map), '\t');
    append_text(recycle_map, &pos, sizeof(recycle_map), origin);
    append_char(recycle_map, &pos, sizeof(recycle_map), '\n');
}

static int recycle_map_origin(const char *name, char *origin, uint32_t cap)
{
    for (uint32_t pos = 0; recycle_map[pos];) {
        uint32_t start = pos;
        uint32_t tab = 0;
        while (recycle_map[pos] && recycle_map[pos] != '\n' &&
               recycle_map[pos] != '\r') {
            if (recycle_map[pos] == '\t') {
                tab = pos;
            }
            ++pos;
        }
        if (tab > start && (uint32_t)(tab - start) == text_len(name)) {
            uint32_t i = 0;
            while (i < tab - start && recycle_map[start + i] == name[i]) {
                ++i;
            }
            if (i == tab - start) {
                uint32_t out = 0;
                for (uint32_t at = tab + 1U; at < pos && out + 1U < cap; ++at) {
                    origin[out++] = recycle_map[at];
                }
                origin[out] = 0;
                return origin[0] != 0;
            }
        }
        while (recycle_map[pos] == '\r' || recycle_map[pos] == '\n') {
            ++pos;
        }
    }
    return 0;
}

static void recycle_map_remove(const char *name)
{
    char next[sizeof(recycle_map)];
    uint32_t out = 0;
    for (uint32_t pos = 0; recycle_map[pos];) {
        uint32_t start = pos;
        uint32_t end = pos;
        uint32_t tab = 0;
        while (recycle_map[end] && recycle_map[end] != '\n' &&
               recycle_map[end] != '\r') {
            if (recycle_map[end] == '\t') {
                tab = end;
            }
            ++end;
        }
        uint32_t same = tab > start && (uint32_t)(tab - start) == text_len(name);
        for (uint32_t i = 0; same && i < tab - start; ++i) {
            if (recycle_map[start + i] != name[i]) {
                same = 0;
            }
        }
        if (!same) {
            for (uint32_t i = start; i < end; ++i) {
                append_char(next, &out, sizeof(next), recycle_map[i]);
            }
            append_char(next, &out, sizeof(next), '\n');
        }
        pos = end;
        while (recycle_map[pos] == '\r' || recycle_map[pos] == '\n') {
            ++pos;
        }
    }
    copy_text(recycle_map, sizeof(recycle_map), next);
}

void recycle_selected_entries(void)
{
    char recycle[LEONOS_FS_PATH_LEN];
    uint32_t moved = 0;
    if (fileman_is_recycle_dir()) {
        set_status(T("Items are already in the Recycle Bin", "项目已经在回收站中"));
        return;
    }
    if (build_recycle_dir(recycle, sizeof(recycle)) < 0) {
        set_status(T("Recycle Bin is unavailable", "回收站不可用"));
        return;
    }
    recycle_map_load(recycle);
    operation_set(0, T("Moving items to Recycle Bin...", "正在将项目移到回收站..."));
    for (uint32_t i = 0; i < entry_count; ++i) {
        char src[LEONOS_FS_PATH_LEN];
        char dst[LEONOS_FS_PATH_LEN];
        int selected = fileman_entry_marked(i) ||
            (!fileman_selected_count() && i == (uint32_t)file_list.selected);
        if (!selected || fileman_entry_is_device(i)) {
            continue;
        }
        build_child_path(src, sizeof(src), entries[i].name);
        if (choose_free_target_path(recycle, entries[i].name, dst, sizeof(dst)) != 0) {
            continue;
        }
        if (rename(src, dst) == 0) {
            recycle_map_append(path_basename(dst), src);
            ++moved;
        }
        operation_set((i + 1U) * 100U / (entry_count ? entry_count : 1U),
                      T("Moving items to Recycle Bin...", "正在将项目移到回收站..."));
    }
    recycle_map_save(recycle);
    reload_dir();
    selected_mask = 0;
    operation_finish(moved ? T("Moved to Recycle Bin", "已移到回收站")
                           : T("No items moved", "没有移动项目"));
}

void restore_selected_entry(void)
{
    char recycle[LEONOS_FS_PATH_LEN];
    char src[LEONOS_FS_PATH_LEN];
    char origin[LEONOS_FS_PATH_LEN];
    if (!fileman_is_recycle_dir() || !selected_entry_valid()) {
        set_status(T("Select an item in Recycle Bin", "请在回收站中选择一个项目"));
        return;
    }
    build_recycle_dir(recycle, sizeof(recycle));
    recycle_map_load(recycle);
    if (!recycle_map_origin(entries[file_list.selected].name, origin, sizeof(origin))) {
        set_status(T("Original location is unavailable", "原始位置不可用"));
        return;
    }
    build_child_path(src, sizeof(src), entries[file_list.selected].name);
    if (leonos_stat_legacy(origin, &(struct leonos_stat){0}) == 0 &&
        !leonos_ui_show_confirm_dialog(T("Restore Conflict", "还原冲突"),
                                       T("Original path exists. Replace it?", "原始路径已存在。要替换它吗？"), 0)) {
        return;
    }
    if (leonos_stat_legacy(origin, &(struct leonos_stat){0}) == 0 && remove_tree(origin, 0) < 0) {
        set_status(T("Could not replace original item", "无法替换原始项目"));
        return;
    }
    if (rename(src, origin) < 0) {
        set_status(T("Restore failed", "还原失败"));
        return;
    }
    recycle_map_remove(entries[file_list.selected].name);
    recycle_map_save(recycle);
    reload_dir();
    operation_finish(T("Item restored", "项目已还原"));
}

void empty_recycle_bin(void)
{
    uint32_t removed = 0;
    if (!fileman_is_recycle_dir()) {
        return;
    }
    if (!leonos_ui_show_confirm_dialog(T("Empty Recycle Bin", "清空回收站"),
                                       T("Delete all Recycle Bin items permanently?", "要永久删除回收站中的所有项目吗？"), 0)) {
        return;
    }
    operation_set(0, T("Emptying Recycle Bin...", "正在清空回收站..."));
    for (uint32_t i = 0; i < entry_count; ++i) {
        char path[LEONOS_FS_PATH_LEN];
        if (text_eq(entries[i].name, FILEMAN_RECYCLE_MAP)) {
            continue;
        }
        build_child_path(path, sizeof(path), entries[i].name);
        if (remove_tree(path, 0) == 0) {
            ++removed;
        }
        operation_set((i + 1U) * 100U / (entry_count ? entry_count : 1U),
                      T("Emptying Recycle Bin...", "正在清空回收站..."));
    }
    recycle_map[0] = 0;
    recycle_map_save(current_path);
    reload_dir();
    operation_finish(removed ? T("Recycle Bin emptied", "回收站已清空")
                             : T("Recycle Bin is empty", "回收站为空"));
}

void permanent_delete_selected_entries(void)
{
    uint32_t removed = 0;
    uint32_t failed = 0;
    if (!selected_entry_valid()) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    if (!leonos_ui_show_confirm_dialog(T("Delete Permanently", "永久删除"),
                                       T("Selected items cannot be restored. Continue?", "选中的项目将无法恢复。要继续吗？"), 0)) {
        return;
    }
    operation_set(0, T("Deleting items...", "正在删除项目..."));
    for (uint32_t i = 0; i < entry_count; ++i) {
        char path[LEONOS_FS_PATH_LEN];
        int selected = fileman_entry_marked(i) ||
            (!fileman_selected_count() && i == (uint32_t)file_list.selected);
        if (!selected || fileman_entry_is_device(i)) {
            continue;
        }
        build_child_path(path, sizeof(path), entries[i].name);
        if (remove_tree(path, 0) == 0) {
            ++removed;
        } else {
            ++failed;
        }
        operation_set((i + 1U) * 100U / (entry_count ? entry_count : 1U),
                      T("Deleting items...", "正在删除项目..."));
    }
    reload_dir();
    selected_mask = 0;
    if (failed) {
        operation_finish(T("Some items could not be deleted", "部分项目无法删除"));
    } else {
        operation_finish(removed ? T("Items deleted permanently", "项目已永久删除")
                                 : T("No items deleted", "没有删除项目"));
    }
}
