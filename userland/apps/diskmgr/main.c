#include <leonos/admin.h>
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DISKMGR_W 900
#define DISKMGR_H 600
#define DISKMGR_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define DISKMGR_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define DISKMGR_STATUS_H 28
#define DISKMGR_ROW_H 24
#define DISKMGR_DISK_ROWS 3
#define DISKMGR_DETAIL_W 304
#define DISKMGR_RIGHT_MARGIN 16
#define DISKMGR_DISK_HEADER_Y 36
#define DISKMGR_DISK_ROW_Y 60
#define DISKMGR_PART_TITLE_Y 166
#define DISKMGR_PART_HEADER_Y 188
#define DISKMGR_PART_ROW_Y 212
#define DISKMGR_KEY_ESCAPE 1U
#define DISKMGR_ACTION_NONE 0U
#define DISKMGR_ACTION_FORMAT 1U
#define DISKMGR_ACTION_DELETE 2U
#define DISKMGR_ACTION_CREATE 3U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DISKMGR_MAX_W * DISKMGR_MAX_H];
static struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
static struct leonos_disk_partition partitions[LEONOS_DISK_MAX_PARTITIONS];
static char disk_id_text[LEONOS_INSTALL_MAX_DISKS][12];
static char disk_port_text[LEONOS_INSTALL_MAX_DISKS][12];
static char disk_size_text[LEONOS_INSTALL_MAX_DISKS][32];
static char disk_flags_text[LEONOS_INSTALL_MAX_DISKS][64];
static char part_index_text[LEONOS_DISK_MAX_PARTITIONS][12];
static char part_size_text[LEONOS_DISK_MAX_PARTITIONS][32];
static char part_range_text[LEONOS_DISK_MAX_PARTITIONS][48];
static char part_flags_text[LEONOS_DISK_MAX_PARTITIONS][80];
static uint32_t disk_count;
static uint32_t partition_count;
static struct leonos_ui_listview_state disk_list;
static struct leonos_ui_listview_state partition_list;
static char status_text[128] = "Ready";
static char confirm_text[16];
static char create_size_text[16] = "512";
static char create_label_text[LEONOS_DISK_PARTITION_NAME_LEN] = "Data";
static struct leonos_ui_edit_state confirm_edit;
static struct leonos_ui_edit_state create_size_edit;
static struct leonos_ui_edit_state create_label_edit;
static uint32_t selected_filesystem = LEONOS_DISK_FILESYSTEM_FAT32;
static uint32_t action_mode;
static uint8_t action_armed;
static uint8_t filesystem_dropdown_open;
static uint32_t view_w = DISKMGR_W;
static uint32_t view_h = DISKMGR_H;

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1u < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int text_eq(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1u < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_u64(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char digits[24];
    uint32_t count = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count) {
        append_char(buf, pos, cap, digits[--count]);
    }
}

static void append_i32(char *buf, uint32_t *pos, uint32_t cap, int value)
{
    if (value < 0) {
        append_char(buf, pos, cap, '-');
        append_u64(buf, pos, cap, (uint64_t)(-(int64_t)value));
        return;
    }
    append_u64(buf, pos, cap, (uint32_t)value);
}

static void format_u32(char *buf, uint32_t cap, uint32_t value)
{
    uint32_t pos = 0;
    if (cap) {
        buf[0] = 0;
    }
    append_u64(buf, &pos, cap, value);
}

static void format_size(char *buf, uint32_t cap, uint64_t bytes)
{
    uint32_t pos = 0;
    uint64_t mib = bytes / (1024ULL * 1024ULL);
    if (cap) {
        buf[0] = 0;
    }
    if (mib >= 1024u) {
        append_u64(buf, &pos, cap, mib / 1024u);
        append_text(buf, &pos, cap, " GiB");
    } else {
        append_u64(buf, &pos, cap, mib);
        append_text(buf, &pos, cap, " MiB");
    }
    if (mib == 0) {
        append_text(buf, &pos, cap, " (");
        append_u64(buf, &pos, cap, bytes);
        append_text(buf, &pos, cap, " B)");
    }
}

static void append_separator(char *buf, uint32_t *pos, uint32_t cap)
{
    if (*pos) {
        append_text(buf, pos, cap, ", ");
    }
}

static void format_disk_flags(char *buf, uint32_t cap, uint32_t flags)
{
    uint32_t pos = 0;
    if (cap) {
        buf[0] = 0;
    }
    if (flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT) {
        append_text(buf, &pos, cap, T("Boot disk", "启动磁盘"));
    }
    if (flags & LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED) {
        append_separator(buf, &pos, cap);
        append_text(buf, &pos, cap, T("Target mounted", "目标已挂载"));
    }
    if (!pos) {
        append_text(buf, &pos, cap, T("Ready", "就绪"));
    }
}

static const char *filesystem_label(uint32_t filesystem)
{
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        return "FAT32";
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_EXT2) {
        return "ext2";
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_ISO9660) {
        return "ISO 9660";
    }
    return T("Unknown", "未知");
}

static void format_partition_flags(char *buf, uint32_t cap, uint32_t flags,
                                   const char *mount_path)
{
    uint32_t pos = 0;
    if (cap) {
        buf[0] = 0;
    }
    if (flags & LEONOS_DISK_PARTITION_FLAG_ESP) {
        append_text(buf, &pos, cap, "ESP");
    }
    if (flags & LEONOS_DISK_PARTITION_FLAG_BOOT_ROOT) {
        append_separator(buf, &pos, cap);
        append_text(buf, &pos, cap, T("Boot disk", "启动磁盘"));
    }
    if (flags & LEONOS_DISK_PARTITION_FLAG_TARGET_MOUNTED) {
        append_separator(buf, &pos, cap);
        append_text(buf, &pos, cap, T("Mounted target", "已挂载目标"));
    }
    if (flags & LEONOS_DISK_PARTITION_FLAG_MOUNTED) {
        append_separator(buf, &pos, cap);
        append_text(buf, &pos, cap, T("Mounted ", "已挂载 "));
        append_text(buf, &pos, cap, mount_path);
    }
    if (flags & LEONOS_DISK_PARTITION_FLAG_PROTECTED) {
        append_separator(buf, &pos, cap);
        append_text(buf, &pos, cap, T("Protected", "受保护"));
    }
    if (!pos) {
        append_text(buf, &pos, cap, T("Data", "数据"));
    }
}

static void format_partition_range(char *buf, uint32_t cap,
                                   const struct leonos_disk_partition *partition)
{
    uint32_t pos = 0;
    if (cap) {
        buf[0] = 0;
    }
    if (!partition || partition->sector_count == 0) {
        append_char(buf, &pos, cap, '-');
        return;
    }
    append_u64(buf, &pos, cap, partition->first_lba);
    append_text(buf, &pos, cap, " - ");
    append_u64(buf, &pos, cap, partition->first_lba + partition->sector_count - 1u);
}

static void set_ret_status(const char *prefix, int ret)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), prefix);
    append_text(status_text, &pos, sizeof(status_text), " ret=");
    append_i32(status_text, &pos, sizeof(status_text), ret);
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t width, int32_t height)
{
    return x >= rx && y >= ry && x < rx + width && y < ry + height;
}

static uint32_t detail_x(void)
{
    return view_w > DISKMGR_DETAIL_W + DISKMGR_RIGHT_MARGIN
               ? view_w - DISKMGR_DETAIL_W - DISKMGR_RIGHT_MARGIN
               : DISKMGR_W - DISKMGR_DETAIL_W - DISKMGR_RIGHT_MARGIN;
}

static uint32_t disk_list_width(void)
{
    uint32_t x = detail_x();
    return x > 34u ? x - 34u : 400u;
}

static uint32_t action_y(void)
{
    return view_h > DISKMGR_STATUS_H + 142u
               ? view_h - DISKMGR_STATUS_H - 142u
               : DISKMGR_H - DISKMGR_STATUS_H - 142u;
}

static uint32_t partition_list_height(void)
{
    uint32_t actions = action_y();
    return actions > DISKMGR_PART_ROW_Y + 16u ? actions - DISKMGR_PART_ROW_Y - 16u : DISKMGR_ROW_H;
}

static uint32_t partition_visible_rows(void)
{
    uint32_t rows = partition_list_height() / DISKMGR_ROW_H;
    return rows ? rows : 1u;
}

static uint32_t action_panel_y(void)
{
    return action_y() + 38u;
}

static uint32_t action_panel_height(void)
{
    uint32_t y = action_panel_y();
    uint32_t bottom = view_h > DISKMGR_STATUS_H + 8u ? view_h - DISKMGR_STATUS_H - 8u : y;
    return bottom > y ? bottom - y : 88u;
}

static void update_layout(void)
{
    disk_list.visible_rows = DISKMGR_DISK_ROWS;
    partition_list.visible_rows = partition_visible_rows();
    leonos_ui_listview_state_set_count(&disk_list, disk_count);
    leonos_ui_listview_state_set_count(&partition_list, partition_count);
}

static int selected_disk_index(void)
{
    if (disk_list.selected < 0 || (uint32_t)disk_list.selected >= disk_count) {
        return -1;
    }
    return disk_list.selected;
}

static int selected_partition_index(void)
{
    if (partition_list.selected < 0 || (uint32_t)partition_list.selected >= partition_count) {
        return -1;
    }
    return partition_list.selected;
}

static int selected_disk_mutable(void)
{
    int index = selected_disk_index();
    return index >= 0 && !(disks[index].flags &
                           (LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT |
                            LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED));
}

static int selected_partition_mutable(void)
{
    int index = selected_partition_index();
    return index >= 0 && !(partitions[index].flags &
                            (LEONOS_DISK_PARTITION_FLAG_PROTECTED |
                             LEONOS_DISK_PARTITION_FLAG_MOUNTED)) &&
           selected_disk_mutable();
}

static int selected_partition_mountable(void)
{
    int index = selected_partition_index();
    return index >= 0 && selected_disk_mutable() &&
           !(partitions[index].flags &
             (LEONOS_DISK_PARTITION_FLAG_PROTECTED | LEONOS_DISK_PARTITION_FLAG_MOUNTED)) &&
           (partitions[index].filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ||
            partitions[index].filesystem == LEONOS_DISK_FILESYSTEM_EXT2);
}

static int selected_partition_unmountable(void)
{
    int index = selected_partition_index();
    return index >= 0 && (partitions[index].flags & LEONOS_DISK_PARTITION_FLAG_MOUNTED) != 0;
}

static void refresh_partitions(void)
{
    int disk_index = selected_disk_index();
    uint32_t count = LEONOS_DISK_MAX_PARTITIONS;
    int ret;
    partition_count = 0;
    partition_list.selected = -1;
    partition_list.scroll = 0;
    if (disk_index < 0) {
        leonos_ui_listview_state_set_count(&partition_list, 0);
        return;
    }
    ret = leonos_disk_list_partitions(disks[disk_index].id, partitions,
                                      LEONOS_DISK_MAX_PARTITIONS, &count);
    if (ret < 0) {
        set_ret_status(T("Partition refresh failed", "分区刷新失败"), ret);
        leonos_ui_listview_state_set_count(&partition_list, 0);
        return;
    }
    partition_count = count > LEONOS_DISK_MAX_PARTITIONS ? LEONOS_DISK_MAX_PARTITIONS : count;
    for (uint32_t i = 0; i < partition_count; ++i) {
        format_u32(part_index_text[i], sizeof(part_index_text[i]), partitions[i].index + 1u);
        format_size(part_size_text[i], sizeof(part_size_text[i]), partitions[i].sector_count * 512ULL);
        format_partition_range(part_range_text[i], sizeof(part_range_text[i]), &partitions[i]);
        format_partition_flags(part_flags_text[i], sizeof(part_flags_text[i]),
                               partitions[i].flags, partitions[i].mount_path);
    }
    update_layout();
    if (partition_count) {
        partition_list.selected = 0;
    }
}

static void refresh_disks(void)
{
    uint32_t previous_id = UINT32_MAX;
    uint32_t count = LEONOS_INSTALL_MAX_DISKS;
    int old_index = selected_disk_index();
    int ret;
    if (old_index >= 0) {
        previous_id = disks[old_index].id;
    }
    ret = leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &count);
    if (ret < 0) {
        disk_count = 0;
        disk_list.selected = -1;
        partition_count = 0;
        partition_list.selected = -1;
        update_layout();
        set_ret_status(T("Disk refresh failed", "磁盘刷新失败"), ret);
        return;
    }
    disk_count = count > LEONOS_INSTALL_MAX_DISKS ? LEONOS_INSTALL_MAX_DISKS : count;
    disk_list.selected = -1;
    for (uint32_t i = 0; i < disk_count; ++i) {
        format_u32(disk_id_text[i], sizeof(disk_id_text[i]), disks[i].id);
        format_u32(disk_port_text[i], sizeof(disk_port_text[i]), disks[i].port);
        format_size(disk_size_text[i], sizeof(disk_size_text[i]),
                    disks[i].sector_count * (uint64_t)disks[i].sector_size);
        format_disk_flags(disk_flags_text[i], sizeof(disk_flags_text[i]), disks[i].flags);
        if (disks[i].id == previous_id) {
            disk_list.selected = (int32_t)i;
        }
    }
    if (disk_count && disk_list.selected < 0) {
        disk_list.selected = 0;
    }
    update_layout();
    refresh_partitions();
    copy_text(status_text, sizeof(status_text), T("Disk list refreshed", "磁盘列表已刷新"));
}

static void reset_action(void)
{
    action_mode = DISKMGR_ACTION_NONE;
    action_armed = 0;
    filesystem_dropdown_open = 0;
    confirm_text[0] = 0;
    leonos_ui_edit_state_init(&confirm_edit, confirm_text, sizeof(confirm_text));
    leonos_ui_edit_state_init(&create_size_edit, create_size_text, sizeof(create_size_text));
    leonos_ui_edit_state_init(&create_label_edit, create_label_text, sizeof(create_label_text));
}

static const char *action_token(void)
{
    if (action_mode == DISKMGR_ACTION_FORMAT) {
        return "FORMAT";
    }
    if (action_mode == DISKMGR_ACTION_DELETE) {
        return "DELETE";
    }
    return "CREATE";
}

static int parse_size_mib(const char *text, uint32_t *out_value)
{
    uint64_t value = 0;
    if (!text || !text[0] || !out_value) {
        return -1;
    }
    for (uint32_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9' || value > 429496729ULL) {
            return -1;
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
    }
    if (value == 0 || value > UINT32_MAX) {
        return -1;
    }
    *out_value = (uint32_t)value;
    return 0;
}

static void open_action(uint32_t mode)
{
    int part_index = selected_partition_index();
    if ((mode == DISKMGR_ACTION_CREATE && !selected_disk_mutable()) ||
        (mode != DISKMGR_ACTION_CREATE && !selected_partition_mutable())) {
        copy_text(status_text, sizeof(status_text),
                  T("Select an unprotected non-boot disk partition", "请选择未受保护的非启动磁盘分区"));
        return;
    }
    if (!leonos_admin_elevate()) {
        copy_text(status_text, sizeof(status_text), T("Administrator approval is required", "需要管理员授权"));
        return;
    }
    reset_action();
    action_mode = mode;
    if (mode == DISKMGR_ACTION_FORMAT && part_index >= 0 &&
        partitions[part_index].filesystem == LEONOS_DISK_FILESYSTEM_EXT2) {
        selected_filesystem = LEONOS_DISK_FILESYSTEM_EXT2;
    } else if (mode == DISKMGR_ACTION_CREATE) {
        selected_filesystem = LEONOS_DISK_FILESYSTEM_EXT2;
        copy_text(create_size_text, sizeof(create_size_text), "512");
        copy_text(create_label_text, sizeof(create_label_text), "Data");
        leonos_ui_edit_state_init(&create_size_edit, create_size_text, sizeof(create_size_text));
        leonos_ui_edit_state_init(&create_label_edit, create_label_text, sizeof(create_label_text));
        create_size_edit.focused = 1;
    } else {
        selected_filesystem = LEONOS_DISK_FILESYSTEM_FAT32;
    }
    confirm_edit.focused = mode != DISKMGR_ACTION_CREATE;
    copy_text(status_text, sizeof(status_text),
              T("Type the confirmation word and click Apply twice", "输入确认词后点击两次应用"));
}

static void run_action(void)
{
    int disk_index = selected_disk_index();
    int part_index = selected_partition_index();
    int ret;
    if (!text_eq(confirm_text, action_token())) {
        action_armed = 0;
        copy_text(status_text, sizeof(status_text), T("Confirmation word does not match", "确认词不匹配"));
        return;
    }
    if (!action_armed) {
        action_armed = 1;
        copy_text(status_text, sizeof(status_text), T("Click Apply again to continue", "再次点击应用以继续"));
        return;
    }
    if (disk_index < 0 || (action_mode != DISKMGR_ACTION_CREATE && part_index < 0)) {
        reset_action();
        copy_text(status_text, sizeof(status_text), T("Selection changed; operation cancelled", "选择已变更；操作已取消"));
        return;
    }
    if (action_mode == DISKMGR_ACTION_FORMAT) {
        struct leonos_disk_partition_format request = {
            .disk_id = disks[disk_index].id,
            .partition_index = partitions[part_index].index,
            .filesystem = selected_filesystem,
            .reserved = 0,
        };
        ret = leonos_disk_format_partition(&request);
        if (ret < 0) {
            set_ret_status(T("Partition format failed", "分区格式化失败"), ret);
            action_armed = 0;
            return;
        }
        reset_action();
        refresh_disks();
        copy_text(status_text, sizeof(status_text), T("Partition formatted", "分区格式化完成"));
        return;
    }
    if (action_mode == DISKMGR_ACTION_DELETE) {
        struct leonos_disk_partition_delete request = {
            .disk_id = disks[disk_index].id,
            .partition_index = partitions[part_index].index,
            .reserved0 = 0,
            .reserved1 = 0,
        };
        ret = leonos_disk_delete_partition(&request);
        if (ret < 0) {
            set_ret_status(T("Partition deletion failed", "删除分区失败"), ret);
            action_armed = 0;
            return;
        }
        reset_action();
        refresh_disks();
        copy_text(status_text, sizeof(status_text), T("Partition entry deleted", "分区条目已删除"));
        return;
    }
    {
        struct leonos_disk_partition_create request = {
            .disk_id = disks[disk_index].id,
            .filesystem = selected_filesystem,
            .size_mib = 0,
            .reserved = 0,
        };
        if (parse_size_mib(create_size_text, &request.size_mib) < 0) {
            action_armed = 0;
            copy_text(status_text, sizeof(status_text), T("Enter a valid size in MiB", "请输入有效的 MiB 大小"));
            return;
        }
        copy_text(request.name, sizeof(request.name), create_label_text);
        ret = leonos_disk_create_partition(&request);
        if (ret < 0) {
            set_ret_status(T("Partition creation failed", "创建分区失败"), ret);
            action_armed = 0;
            return;
        }
    }
    reset_action();
    refresh_disks();
    copy_text(status_text, sizeof(status_text), T("Partition created and formatted", "分区已创建并格式化"));
}

static void mount_selected_partition(void)
{
    int disk_index = selected_disk_index();
    int part_index = selected_partition_index();
    char mount_path[LEONOS_FS_PATH_LEN];
    int ret;
    uint32_t pos = 0;
    if (disk_index < 0 || part_index < 0 || !selected_partition_mountable()) {
        copy_text(status_text, sizeof(status_text),
                  T("Select an unmounted FAT32 or ext2 data partition",
                    "请选择未挂载的 FAT32 或 ext2 数据分区"));
        return;
    }
    if (!leonos_admin_elevate()) {
        copy_text(status_text, sizeof(status_text),
                  T("Administrator approval is required", "需要管理员授权"));
        return;
    }
    ret = leonos_disk_mount_partition(disks[disk_index].id, partitions[part_index].index,
                                      mount_path, sizeof(mount_path));
    if (ret < 0) {
        set_ret_status(T("Partition mount failed", "分区挂载失败"), ret);
        return;
    }
    refresh_disks();
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), T("Mounted as ", "已挂载为 "));
    append_text(status_text, &pos, sizeof(status_text), mount_path);
}

static void unmount_selected_partition(void)
{
    int disk_index = selected_disk_index();
    int part_index = selected_partition_index();
    int ret;
    if (disk_index < 0 || part_index < 0 || !selected_partition_unmountable()) {
        copy_text(status_text, sizeof(status_text),
                  T("Select a mounted data partition", "请选择已挂载的数据分区"));
        return;
    }
    if (!leonos_admin_elevate()) {
        copy_text(status_text, sizeof(status_text),
                  T("Administrator approval is required", "需要管理员授权"));
        return;
    }
    ret = leonos_disk_unmount_partition(disks[disk_index].id, partitions[part_index].index);
    if (ret < 0) {
        if (ret == -LEONOS_EBUSY) {
            copy_text(status_text, sizeof(status_text),
                      T("Unmount blocked: close files and leave the mount first",
                        "卸载被阻止：请先关闭文件并离开该挂载点"));
        } else {
            set_ret_status(T("Partition unmount failed", "分区卸载失败"), ret);
        }
        return;
    }
    refresh_disks();
    copy_text(status_text, sizeof(status_text), T("Partition unmounted", "分区已卸载"));
}

static void draw_disk_details(struct leonos_ui_surface *ui)
{
    int index = selected_disk_index();
    uint32_t x = detail_x();
    leonos_ui_groupbox(ui, x, 18, DISKMGR_DETAIL_W, 138, T("Selected disk", "选中磁盘"));
    if (index >= 0) {
        struct leonos_ui_property_item props[] = {
            {"ID:", disk_id_text[index], 0},
            {T("Name:", "名称:"), disks[index].name, 0},
            {T("Port:", "端口:"), disk_port_text[index], 0},
            {T("Capacity:", "容量:"), disk_size_text[index], 0},
            {T("Status:", "状态:"), disk_flags_text[index], 0},
        };
        leonos_ui_property_grid(ui, x + 10, 40, DISKMGR_DETAIL_W - 20u, props,
                                sizeof(props) / sizeof(props[0]), 72, 21);
    } else {
        leonos_ui_text(ui, x + 14, 46, T("No disk selected.", "未选择磁盘。"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
}

static void draw_action_panel(struct leonos_ui_surface *ui)
{
    static const struct leonos_ui_dropdown_item filesystem_items[] = {
        {"FAT32", LEONOS_DISK_FILESYSTEM_FAT32, 0},
        {"ext2", LEONOS_DISK_FILESYSTEM_EXT2, 0},
    };
    uint32_t y = action_panel_y();
    uint32_t height = action_panel_height();
    uint32_t confirm_x = action_mode == DISKMGR_ACTION_CREATE ? 606u : 332u;
    uint32_t edit_x = confirm_x + 70u;
    leonos_ui_groupbox(ui, 16, y, view_w > 32u ? view_w - 32u : 1u, height,
                       action_mode == DISKMGR_ACTION_FORMAT ? T("Format partition", "格式化分区") :
                       action_mode == DISKMGR_ACTION_DELETE ? T("Delete partition", "删除分区") :
                       T("Create partition", "创建分区"));
    if (action_mode == DISKMGR_ACTION_DELETE) {
        leonos_ui_text_clipped(ui, 30, y + 22, view_w > 60u ? view_w - 60u : 1u,
                               T("This removes the GPT entry. Existing data is not securely erased.",
                                 "这会移除 GPT 条目，原有数据不会被安全擦除。"),
                               LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    } else {
        leonos_ui_text(ui, 30, y + 22, T("File system:", "文件系统:"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_combobox(ui, 118, y + 16, 136, filesystem_label(selected_filesystem),
                            filesystem_dropdown_open, 0);
        if (action_mode == DISKMGR_ACTION_CREATE) {
            leonos_ui_text(ui, 274, y + 22, T("Size MiB:", "大小 MiB:"),
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
            leonos_ui_edit_state_draw(ui, 346, y + 16, 92, &create_size_edit, 0);
            leonos_ui_text(ui, 454, y + 22, T("Label:", "卷标:"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
            leonos_ui_edit_state_draw(ui, 508, y + 16, 82, &create_label_edit, 0);
        }
    }
    leonos_ui_text(ui, confirm_x, y + 22, T("Confirm:", "确认:"), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, edit_x, y + 16, 112, &confirm_edit, 0);
    leonos_ui_button(ui, 30, y + 60, 122, LEONOS_UI_BUTTON_H,
                     action_armed ? T("Apply again", "再次应用") : T("Apply", "应用"),
                     text_eq(confirm_text, action_token()) ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 162, y + 60, 82, LEONOS_UI_BUTTON_H, T("Cancel", "取消"), 0);
    if (filesystem_dropdown_open) {
        leonos_ui_dropdown(ui, 118, y + 40, 136, filesystem_items,
                           sizeof(filesystem_items) / sizeof(filesystem_items[0]),
                           selected_filesystem, DISKMGR_ROW_H, 1000);
    }
}

static void draw_diskmgr(struct leonos_ui_surface *ui)
{
    const struct leonos_ui_list_column disk_columns[] = {
        {"ID", 38},
        {T("Name", "名称"), 126},
        {T("Port", "端口"), 48},
        {T("Capacity", "容量"), 94},
        {T("Status", "状态"), 0},
    };
    const struct leonos_ui_list_column partition_columns[] = {
        {"#", 42},
        {T("Name", "名称"), 178},
        {T("File system", "文件系统"), 94},
        {T("Size", "大小"), 110},
        {"LBA", 164},
        {T("Status", "状态"), 0},
    };
    uint32_t disk_w = disk_list_width();
    uint32_t part_w = view_w > 32u ? view_w - 32u : 1u;
    uint32_t part_h = partition_list_height();
    uint32_t controls_y = action_y();
    char summary[128];
    uint32_t pos = 0;
    update_layout();
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 16, 14, T("Disks", "磁盘"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 16, DISKMGR_DISK_HEADER_Y, disk_w, disk_columns,
                              sizeof(disk_columns) / sizeof(disk_columns[0]));
    for (uint32_t row = 0; row < disk_list.visible_rows; ++row) {
        uint32_t index = disk_list.scroll + row;
        uint32_t y = DISKMGR_DISK_ROW_Y + row * DISKMGR_ROW_H;
        if (index >= disk_count) {
            leonos_ui_list_row(ui, 16, y, disk_w, "", 0);
            continue;
        }
        {
            const char *cells[] = {
                disk_id_text[index], disks[index].name, disk_port_text[index],
                disk_size_text[index], disk_flags_text[index],
            };
            leonos_ui_listview_row(ui, 16, y, disk_w, disk_columns, cells,
                                   sizeof(disk_columns) / sizeof(disk_columns[0]),
                                   (int32_t)index == disk_list.selected ? LEONOS_UI_MENU_SELECTED : 0);
        }
    }
    leonos_ui_vscrollbar(ui, 18 + disk_w, DISKMGR_DISK_ROW_Y, 18,
                         DISKMGR_DISK_ROWS * DISKMGR_ROW_H, disk_list.scroll,
                         disk_count, disk_list.visible_rows,
                         disk_count <= disk_list.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    draw_disk_details(ui);

    leonos_ui_text(ui, 16, DISKMGR_PART_TITLE_Y, T("Partitions", "分区"),
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 16, DISKMGR_PART_HEADER_Y, part_w, partition_columns,
                              sizeof(partition_columns) / sizeof(partition_columns[0]));
    for (uint32_t row = 0; row < partition_list.visible_rows; ++row) {
        uint32_t index = partition_list.scroll + row;
        uint32_t y = DISKMGR_PART_ROW_Y + row * DISKMGR_ROW_H;
        if (index >= partition_count) {
            leonos_ui_list_row(ui, 16, y, part_w, "", 0);
            continue;
        }
        {
            const char *cells[] = {
                part_index_text[index], partitions[index].name[0] ? partitions[index].name : "-",
                filesystem_label(partitions[index].filesystem), part_size_text[index],
                part_range_text[index], part_flags_text[index],
            };
            leonos_ui_listview_row(ui, 16, y, part_w, partition_columns, cells,
                                   sizeof(partition_columns) / sizeof(partition_columns[0]),
                                   (int32_t)index == partition_list.selected ? LEONOS_UI_MENU_SELECTED : 0);
        }
    }
    leonos_ui_vscrollbar(ui, 18 + part_w, DISKMGR_PART_ROW_Y, 18, part_h,
                         partition_list.scroll, partition_count, partition_list.visible_rows,
                         partition_count <= partition_list.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    leonos_ui_button(ui, 16, controls_y, 86, LEONOS_UI_BUTTON_H, T("Refresh", "刷新"), 0);
    leonos_ui_button(ui, 112, controls_y, 112, LEONOS_UI_BUTTON_H,
                     T("New partition", "新建分区"),
                     selected_disk_mutable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 234, controls_y, 118, LEONOS_UI_BUTTON_H,
                     T("Format partition", "格式化分区"),
                     selected_partition_mutable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 362, controls_y, 118, LEONOS_UI_BUTTON_H,
                     T("Delete partition", "删除分区"),
                     selected_partition_mutable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 490, controls_y, 88, LEONOS_UI_BUTTON_H,
                     T("Mount", "挂载"),
                     selected_partition_mountable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, 588, controls_y, 104, LEONOS_UI_BUTTON_H,
                     T("Unmount", "卸载"),
                     selected_partition_unmountable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    summary[0] = 0;
    append_text(summary, &pos, sizeof(summary), T("Disks: ", "磁盘: "));
    append_u64(summary, &pos, sizeof(summary), disk_count);
    append_text(summary, &pos, sizeof(summary), T("   Partitions: ", "   分区: "));
    append_u64(summary, &pos, sizeof(summary), partition_count);
    leonos_ui_text_clipped(ui, 706, controls_y + 6,
                           view_w > 722u ? view_w - 722u : 1u,
                           summary, LEONOS_UI_DARK, LEONOS_UI_GRAY);

    if (action_mode != DISKMGR_ACTION_NONE) {
        draw_action_panel(ui);
    } else {
        uint32_t y = action_panel_y();
        leonos_ui_groupbox(ui, 16, y, view_w > 32u ? view_w - 32u : 1u, action_panel_height(),
                           T("Partition safety", "分区安全"));
        leonos_ui_text_clipped(ui, 30, y + 22, view_w > 60u ? view_w - 60u : 1u,
                               T("Mount FAT32 or ext2 data partitions at stable /mnt paths.",
                                 "可将 FAT32 或 ext2 数据分区挂载到稳定的 /mnt 路径。"),
                               LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, 30, y + 46, view_w > 60u ? view_w - 60u : 1u,
                               T("Unmount requires administrator approval and no task may use the mount.",
                                 "卸载需要管理员授权，且不能有进程正在使用该挂载点。"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    leonos_ui_statusbar(ui, view_h - DISKMGR_STATUS_H, DISKMGR_STATUS_H, status_text);
}

static void present_diskmgr(uint32_t window_id, struct leonos_ui_surface *ui)
{
    leonos_ui_bind(ui, pixels, view_w, view_h, DISKMGR_MAX_W);
    draw_diskmgr(ui);
    leonos_gui_present_window(window_id, view_w, view_h, DISKMGR_MAX_W, pixels);
}

static int select_filesystem_from_dropdown(int32_t x, int32_t y)
{
    static const struct leonos_ui_dropdown_item filesystem_items[] = {
        {"FAT32", LEONOS_DISK_FILESYSTEM_FAT32, 0},
        {"ext2", LEONOS_DISK_FILESYSTEM_EXT2, 0},
    };
    uint32_t selected = 0;
    if (!filesystem_dropdown_open) {
        return 0;
    }
    if (!leonos_ui_dropdown_hit(x, y, 118, action_panel_y() + 40u, 136,
                                filesystem_items, sizeof(filesystem_items) / sizeof(filesystem_items[0]),
                                DISKMGR_ROW_H, 1000, &selected)) {
        return 0;
    }
    if (selected == LEONOS_DISK_FILESYSTEM_FAT32 || selected == LEONOS_DISK_FILESYSTEM_EXT2) {
        selected_filesystem = selected;
    }
    filesystem_dropdown_open = 0;
    action_armed = 0;
    return 1;
}

static int handle_mouse(uint32_t window_id, struct leonos_ui_surface *ui,
                        const struct leonos_gui_app_event *event)
{
    uint32_t disk_w = disk_list_width();
    uint32_t part_w = view_w > 32u ? view_w - 32u : 1u;
    uint32_t part_h = partition_list_height();
    uint32_t controls_y = action_y();
    uint32_t panel_y = action_panel_y();
    if (!(event->buttons & 1u)) {
        return 0;
    }
    if (action_mode != DISKMGR_ACTION_NONE && select_filesystem_from_dropdown(event->x, event->y)) {
        present_diskmgr(window_id, ui);
        return 1;
    }
    if ((action_mode == DISKMGR_ACTION_FORMAT || action_mode == DISKMGR_ACTION_CREATE) &&
        hit_rect_i(event->x, event->y, 118, panel_y + 16u, 136, LEONOS_UI_BUTTON_H)) {
        filesystem_dropdown_open = filesystem_dropdown_open ? 0 : 1;
        action_armed = 0;
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (action_mode != DISKMGR_ACTION_NONE &&
        leonos_ui_edit_state_handle_mouse(&confirm_edit, event->x, event->y,
                                          action_mode == DISKMGR_ACTION_CREATE ? 676u : 402u,
                                          panel_y + 16u, 112, event->buttons)) {
        action_armed = 0;
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (action_mode == DISKMGR_ACTION_CREATE &&
        leonos_ui_edit_state_handle_mouse(&create_size_edit, event->x, event->y,
                                          346, panel_y + 16u, 92, event->buttons)) {
        action_armed = 0;
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (action_mode == DISKMGR_ACTION_CREATE &&
        leonos_ui_edit_state_handle_mouse(&create_label_edit, event->x, event->y,
                                          508, panel_y + 16u, 82, event->buttons)) {
        action_armed = 0;
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 16, controls_y, 86, LEONOS_UI_BUTTON_H)) {
        reset_action();
        refresh_disks();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 112, controls_y, 112, LEONOS_UI_BUTTON_H)) {
        open_action(DISKMGR_ACTION_CREATE);
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 234, controls_y, 118, LEONOS_UI_BUTTON_H)) {
        open_action(DISKMGR_ACTION_FORMAT);
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 362, controls_y, 118, LEONOS_UI_BUTTON_H)) {
        open_action(DISKMGR_ACTION_DELETE);
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 490, controls_y, 88, LEONOS_UI_BUTTON_H)) {
        mount_selected_partition();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 588, controls_y, 104, LEONOS_UI_BUTTON_H)) {
        unmount_selected_partition();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (action_mode != DISKMGR_ACTION_NONE &&
        hit_rect_i(event->x, event->y, 30, panel_y + 60u, 122, LEONOS_UI_BUTTON_H)) {
        run_action();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (action_mode != DISKMGR_ACTION_NONE &&
        hit_rect_i(event->x, event->y, 162, panel_y + 60u, 82, LEONOS_UI_BUTTON_H)) {
        reset_action();
        copy_text(status_text, sizeof(status_text), T("Operation cancelled", "操作已取消"));
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (event->x >= (int32_t)(18u + disk_w) && event->y >= DISKMGR_DISK_ROW_Y &&
        event->y < (int32_t)(DISKMGR_DISK_ROW_Y + DISKMGR_DISK_ROWS * DISKMGR_ROW_H)) {
        if (leonos_ui_vscrollbar_handle_mouse(&disk_list.scroll,
                                              disk_count > disk_list.visible_rows ? disk_count : disk_list.visible_rows,
                                              disk_list.visible_rows, 18 + disk_w,
                                              DISKMGR_DISK_ROW_Y, 18,
                                              DISKMGR_DISK_ROWS * DISKMGR_ROW_H, event->x, event->y)) {
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    if (event->x >= 16 && event->x < (int32_t)(16u + disk_w) &&
        event->y >= DISKMGR_DISK_ROW_Y &&
        event->y < (int32_t)(DISKMGR_DISK_ROW_Y + DISKMGR_DISK_ROWS * DISKMGR_ROW_H)) {
        int old = selected_disk_index();
        uint32_t activate = 0;
        if (leonos_ui_listview_state_handle_mouse(&disk_list, event->x, event->y,
                                                  16, DISKMGR_DISK_ROW_Y, disk_w, &activate)) {
            partition_list.focused = 0;
            if (selected_disk_index() != old) {
                reset_action();
                refresh_partitions();
            }
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    if (event->x >= (int32_t)(18u + part_w) && event->y >= DISKMGR_PART_ROW_Y &&
        event->y < (int32_t)(DISKMGR_PART_ROW_Y + part_h)) {
        if (leonos_ui_vscrollbar_handle_mouse(&partition_list.scroll,
                                              partition_count > partition_list.visible_rows ? partition_count : partition_list.visible_rows,
                                              partition_list.visible_rows, 18 + part_w,
                                              DISKMGR_PART_ROW_Y, 18, part_h, event->x, event->y)) {
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    if (event->x >= 16 && event->x < (int32_t)(16u + part_w) &&
        event->y >= DISKMGR_PART_ROW_Y && event->y < (int32_t)(DISKMGR_PART_ROW_Y + part_h)) {
        uint32_t activate = 0;
        if (leonos_ui_listview_state_handle_mouse(&partition_list, event->x, event->y,
                                                  16, DISKMGR_PART_ROW_Y, part_w, &activate)) {
            disk_list.focused = 0;
            action_armed = 0;
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    return 0;
}

static int handle_key(uint32_t window_id, struct leonos_ui_surface *ui,
                      const struct leonos_gui_app_event *event)
{
    if (event->type != LEONOS_GUI_APP_EVENT_KEY_DOWN && event->type != LEONOS_GUI_APP_EVENT_KEY_UP) {
        return 0;
    }
    if (event->pressed && event->keycode == DISKMGR_KEY_ESCAPE) {
        if (action_mode != DISKMGR_ACTION_NONE) {
            reset_action();
            copy_text(status_text, sizeof(status_text), T("Operation cancelled", "操作已取消"));
            present_diskmgr(window_id, ui);
            return 1;
        }
        return -1;
    }
    if (action_mode != DISKMGR_ACTION_NONE) {
        int changed = leonos_ui_edit_state_handle_key(&confirm_edit, event->keycode, event->pressed);
        if (action_mode == DISKMGR_ACTION_CREATE) {
            changed |= leonos_ui_edit_state_handle_key(&create_size_edit, event->keycode, event->pressed);
            changed |= leonos_ui_edit_state_handle_key(&create_label_edit, event->keycode, event->pressed);
        }
        if (changed) {
            action_armed = 0;
            present_diskmgr(window_id, ui);
            return 1;
        }
        if (event->pressed && event->keycode == LEONOS_KEY_ENTER) {
            run_action();
            present_diskmgr(window_id, ui);
            return 1;
        }
        return 0;
    }
    if (event->type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
        uint32_t activate = 0;
        if (disk_list.focused && leonos_ui_listview_state_handle_key(&disk_list, event->keycode, &activate)) {
            refresh_partitions();
            present_diskmgr(window_id, ui);
            return 1;
        }
        if (partition_list.focused &&
            leonos_ui_listview_state_handle_key(&partition_list, event->keycode, &activate)) {
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    puts("[diskmgr.elf] disk manager starting");
    window_id = leonos_gui_create_app_window_ex(T("Disk Manager", "磁盘管理器"),
                                                T("Manage GPT partitions", "管理 GPT 分区"),
                                                DISKMGR_W, DISKMGR_H, 0);
    if (window_id <= 0) {
        printf("[diskmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, view_w, view_h, DISKMGR_MAX_W);
    leonos_ui_listview_state_init(&disk_list, DISKMGR_DISK_ROWS, DISKMGR_ROW_H);
    leonos_ui_listview_state_init(&partition_list, partition_visible_rows(), DISKMGR_ROW_H);
    disk_list.focused = 1;
    reset_action();
    refresh_disks();
    present_diskmgr((uint32_t)window_id, &ui);
    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                handle_mouse((uint32_t)window_id, &ui, &event);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                int changed = 0;
                if (event.y >= DISKMGR_PART_ROW_Y &&
                    event.y < (int32_t)(DISKMGR_PART_ROW_Y + partition_list_height())) {
                    changed = leonos_ui_listview_state_handle_wheel(&partition_list, event.dy);
                } else if (event.y >= DISKMGR_DISK_ROW_Y &&
                           event.y < (int32_t)(DISKMGR_DISK_ROW_Y +
                                               DISKMGR_DISK_ROWS * DISKMGR_ROW_H)) {
                    changed = leonos_ui_listview_state_handle_wheel(&disk_list, event.dy);
                }
                if (changed) {
                    present_diskmgr((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                if (handle_key((uint32_t)window_id, &ui, &event) < 0) {
                    return 0;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE || event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width >= DISKMGR_W) {
                    view_w = event.width > DISKMGR_MAX_W ? DISKMGR_MAX_W : event.width;
                }
                if (event.height >= DISKMGR_H) {
                    view_h = event.height > DISKMGR_MAX_H ? DISKMGR_MAX_H : event.height;
                }
                update_layout();
                present_diskmgr((uint32_t)window_id, &ui);
            }
        }
        sleep_ms(20);
    }
}
