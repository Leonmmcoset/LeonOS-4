#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define DISKMGR_W 760
#define DISKMGR_H 480
#define DISKMGR_MAX_W 1264
#define DISKMGR_MAX_H 746
#define DISKMGR_DETAIL_W 252
#define DISKMGR_RIGHT_MARGIN 22
#define DISKMGR_STATUS_H 28
#define DISKMGR_ROW_H 24
#define DISKMGR_LIST_TITLE_Y 14
#define DISKMGR_LIST_HEADER_Y 36
#define DISKMGR_LIST_ROW_Y 60
#define DISKMGR_DETAIL_Y 36
#define DISKMGR_DETAIL_H 186
#define DISKMGR_SUMMARY_Y 232
#define DISKMGR_LOWER_PANEL_Y 260
#define DISKMGR_LOWER_PANEL_H 132
#define DISKMGR_CONFIRM_EDIT_Y (DISKMGR_LOWER_PANEL_Y + 64)
#define DISKMGR_CONFIRM_BUTTON_Y (DISKMGR_LOWER_PANEL_Y + 98)
#define DISKMGR_KEY_ESCAPE 1U
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[DISKMGR_MAX_W * DISKMGR_MAX_H];
static struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
static char disk_id_text[LEONOS_INSTALL_MAX_DISKS][12];
static char disk_port_text[LEONOS_INSTALL_MAX_DISKS][12];
static char disk_size_text[LEONOS_INSTALL_MAX_DISKS][32];
static char disk_sector_text[LEONOS_INSTALL_MAX_DISKS][16];
static char disk_flags_text[LEONOS_INSTALL_MAX_DISKS][64];
static uint32_t disk_count;
static struct leonos_ui_listview_state disk_list;
static char status_text[128] = "Ready";
static char confirm_text[16];
static struct leonos_ui_edit_state confirm_edit;
static uint8_t confirm_open;
static uint8_t confirm_armed;
static uint32_t view_w = DISKMGR_W;
static uint32_t view_h = DISKMGR_H;

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1 < cap) {
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
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        append_char(buf, pos, cap, tmp[--n]);
    }
}

static void append_i32(char *buf, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        append_char(buf, pos, cap, '-');
        value = -value;
    }
    append_u64(buf, pos, cap, (uint32_t)value);
}

static void format_u32(char *buf, uint32_t cap, uint32_t value)
{
    uint32_t pos = 0;
    if (buf && cap) {
        buf[0] = 0;
    }
    append_u64(buf, &pos, cap, value);
}

static void format_size(char *buf, uint32_t cap, uint64_t bytes)
{
    uint32_t pos = 0;
    uint64_t mib = bytes / (1024ULL * 1024ULL);
    if (buf && cap) {
        buf[0] = 0;
    }
    append_u64(buf, &pos, cap, mib);
    append_text(buf, &pos, cap, " MiB");
    if (mib < 1) {
        append_text(buf, &pos, cap, " (");
        append_u64(buf, &pos, cap, bytes);
        append_text(buf, &pos, cap, " B)");
    }
}

static void format_flags(char *buf, uint32_t cap, uint32_t flags)
{
    uint32_t pos = 0;
    if (buf && cap) {
        buf[0] = 0;
    }
    if (flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT) {
        append_text(buf, &pos, cap, T("Boot root", "启动盘"));
    }
    if (flags & LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED) {
        if (pos) {
            append_text(buf, &pos, cap, ", ");
        }
        append_text(buf, &pos, cap, T("Target mounted", "目标已挂载"));
    }
    if (!pos) {
        append_text(buf, &pos, cap, T("Ready", "就绪"));
    }
}

static void set_ret_status(const char *prefix, int ret)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), prefix);
    append_text(status_text, &pos, sizeof(status_text), " ret=");
    append_i32(status_text, &pos, sizeof(status_text), ret);
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static uint32_t right_panel_x(void)
{
    return view_w > DISKMGR_DETAIL_W + DISKMGR_RIGHT_MARGIN
               ? view_w - DISKMGR_DETAIL_W - DISKMGR_RIGHT_MARGIN
               : 486;
}

static uint32_t disk_list_w(void)
{
    uint32_t right_x = right_panel_x();
    return right_x > 34 ? right_x - 34 : 452;
}

static uint32_t disk_button_y(void)
{
    return view_h > DISKMGR_STATUS_H + 60 ? view_h - DISKMGR_STATUS_H - 60 : 392;
}

static uint32_t disk_list_h(void)
{
    uint32_t button_y = disk_button_y();
    return button_y > DISKMGR_LIST_ROW_Y + 18 ? button_y - DISKMGR_LIST_ROW_Y - 18 : 72;
}

static uint32_t disk_visible_rows(void)
{
    uint32_t rows = disk_list_h() / DISKMGR_ROW_H;
    return rows ? rows : 1;
}

static void update_disk_list_layout(void)
{
    disk_list.visible_rows = disk_visible_rows();
    leonos_ui_listview_state_set_count(&disk_list, disk_count);
}

static int selected_disk_index(void)
{
    if (disk_list.selected < 0 || (uint32_t)disk_list.selected >= disk_count) {
        return -1;
    }
    return disk_list.selected;
}

static void refresh_disks(void)
{
    uint32_t count = LEONOS_INSTALL_MAX_DISKS;
    int ret = leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &count);
    if (ret < 0) {
        disk_count = 0;
        disk_list.selected = -1;
        leonos_ui_listview_state_set_count(&disk_list, 0);
        set_ret_status(T("Disk refresh failed", "磁盘刷新失败"), ret);
        return;
    }

    disk_count = count;
    if (disk_count > LEONOS_INSTALL_MAX_DISKS) {
        disk_count = LEONOS_INSTALL_MAX_DISKS;
    }
    for (uint32_t i = 0; i < disk_count; ++i) {
        format_u32(disk_id_text[i], sizeof(disk_id_text[i]), disks[i].id);
        format_u32(disk_port_text[i], sizeof(disk_port_text[i]), disks[i].port);
        format_size(disk_size_text[i], sizeof(disk_size_text[i]),
                    disks[i].sector_count * (uint64_t)disks[i].sector_size);
        format_u32(disk_sector_text[i], sizeof(disk_sector_text[i]), disks[i].sector_size);
        format_flags(disk_flags_text[i], sizeof(disk_flags_text[i]), disks[i].flags);
    }
    update_disk_list_layout();
    if (disk_count && disk_list.selected < 0) {
        disk_list.selected = 0;
    }
    if ((uint32_t)disk_list.selected >= disk_count) {
        disk_list.selected = disk_count ? (int32_t)(disk_count - 1) : -1;
    }
    copy_text(status_text, sizeof(status_text), T("Disk list refreshed", "磁盘列表已刷新"));
}

static void reset_confirm(void)
{
    confirm_open = 0;
    confirm_armed = 0;
    confirm_text[0] = 0;
    leonos_ui_edit_state_init(&confirm_edit, confirm_text, sizeof(confirm_text));
}

static void open_format_confirm(void)
{
    if (selected_disk_index() < 0) {
        copy_text(status_text, sizeof(status_text), T("Select a disk first", "请先选择一个磁盘"));
        return;
    }
    confirm_open = 1;
    confirm_armed = 0;
    confirm_text[0] = 0;
    leonos_ui_edit_state_init(&confirm_edit, confirm_text, sizeof(confirm_text));
    confirm_edit.focused = 1;
    copy_text(status_text, sizeof(status_text), T("Type FORMAT to enable erase confirmation", "输入 FORMAT 启用擦除确认"));
}

static void do_mount_selected(void)
{
    int index = selected_disk_index();
    if (index < 0) {
        copy_text(status_text, sizeof(status_text), T("Select a disk first", "请先选择一个磁盘"));
        return;
    }
    int ret = leonos_install_mount_target(disks[index].id);
    if (ret < 0) {
        set_ret_status(T("Mount target failed", "挂载目标失败"), ret);
        return;
    }
    refresh_disks();
    copy_text(status_text, sizeof(status_text), T("Target mounted", "目标已挂载"));
}

static void do_format_selected(void)
{
    int index = selected_disk_index();
    if (index < 0) {
        reset_confirm();
        copy_text(status_text, sizeof(status_text), T("Select a disk first", "请先选择一个磁盘"));
        return;
    }
    if (!text_eq(confirm_text, "FORMAT")) {
        confirm_armed = 0;
        copy_text(status_text, sizeof(status_text), T("Type FORMAT exactly", "必须精确输入 FORMAT"));
        return;
    }
    if (!confirm_armed) {
        confirm_armed = 1;
        copy_text(status_text, sizeof(status_text), T("Click again to erase the selected disk", "再次点击以擦除选中磁盘"));
        return;
    }
    int ret = leonos_install_format_esp(disks[index].id);
    if (ret < 0) {
        set_ret_status(T("Format ESP failed", "格式化 ESP 失败"), ret);
        confirm_armed = 0;
        return;
    }
    reset_confirm();
    refresh_disks();
    copy_text(status_text, sizeof(status_text), T("Format complete; target is not mounted", "格式化完成；目标盘未自动挂载"));
}

static void draw_confirmation(struct leonos_ui_surface *ui)
{
    int index = selected_disk_index();
    uint32_t right_x = right_panel_x();
    leonos_ui_groupbox(ui, right_x, DISKMGR_LOWER_PANEL_Y, DISKMGR_DETAIL_W,
                       DISKMGR_LOWER_PANEL_H, T("Format confirmation", "格式化确认"));
    leonos_ui_text_clipped(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 24, DISKMGR_DETAIL_W - 32,
                           T("This will erase and recreate an ESP.", "这会擦除并重建 ESP。"),
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    if (index >= 0) {
        char line[96];
        uint32_t pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), "Disk ");
        append_u64(line, &pos, sizeof(line), disks[index].id);
        append_text(line, &pos, sizeof(line), " ");
        append_text(line, &pos, sizeof(line), disks[index].name);
        leonos_ui_text_clipped(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 44,
                               DISKMGR_DETAIL_W - 32, line,
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }
    leonos_ui_text(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 68, T("Confirm:", "确认:"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_edit_state_draw(ui, right_x + 86, DISKMGR_CONFIRM_EDIT_Y, 150, &confirm_edit, 0);
    leonos_ui_button(ui, right_x + 14, DISKMGR_CONFIRM_BUTTON_Y, 134, LEONOS_UI_BUTTON_H,
                     confirm_armed ? T("Click again", "再次点击") : T("Format Now", "立即格式化"),
                     text_eq(confirm_text, "FORMAT") ? 0 : LEONOS_UI_BUTTON_DISABLED);
    leonos_ui_button(ui, right_x + 158, DISKMGR_CONFIRM_BUTTON_Y, 78, LEONOS_UI_BUTTON_H,
                     T("Cancel", "取消"), 0);
}

static void draw_diskmgr(struct leonos_ui_surface *ui)
{
    static const struct leonos_ui_list_column cols[] = {
        { "ID", 42 },
        { "Name", 150 },
        { "Port", 54 },
        { "Size", 106 },
        { "Sector", 74 },
        { "Status", 164 },
    };
    char selected_line[96];
    uint32_t pos = 0;
    uint32_t right_x = right_panel_x();
    uint32_t list_w = disk_list_w();
    uint32_t list_h = disk_list_h();
    uint32_t button_y = disk_button_y();
    update_disk_list_layout();

    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_GRAY);
    leonos_ui_text(ui, 16, DISKMGR_LIST_TITLE_Y, T("Disks", "磁盘"), LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_listview_header(ui, 16, DISKMGR_LIST_HEADER_Y, list_w, cols, sizeof(cols) / sizeof(cols[0]));
    for (uint32_t row = 0; row < disk_list.visible_rows; ++row) {
        uint32_t i = disk_list.scroll + row;
        uint32_t y = DISKMGR_LIST_ROW_Y + row * DISKMGR_ROW_H;
        if (i >= disk_count) {
            leonos_ui_list_row(ui, 16, y, list_w, "", 0);
            continue;
        }
        const char *cells[] = {
            disk_id_text[i],
            disks[i].name,
            disk_port_text[i],
            disk_size_text[i],
            disk_sector_text[i],
            disk_flags_text[i],
        };
        leonos_ui_listview_row(ui, 16, y, list_w, cols, cells,
                               sizeof(cols) / sizeof(cols[0]),
                               (int32_t)i == disk_list.selected ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, 18 + list_w, DISKMGR_LIST_ROW_Y, 18, list_h,
                         disk_list.scroll, disk_count, disk_list.visible_rows,
                         disk_count <= disk_list.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    leonos_ui_button(ui, 16, button_y, 90, LEONOS_UI_BUTTON_H, T("Refresh", "刷新"), 0);
    leonos_ui_button(ui, 116, button_y, 112, LEONOS_UI_BUTTON_H, T("Format ESP", "格式化 ESP"),
                     selected_disk_index() < 0 ? LEONOS_UI_BUTTON_DISABLED : 0);
    leonos_ui_button(ui, 238, button_y, 118, LEONOS_UI_BUTTON_H, T("Mount Target", "挂载目标"),
                     selected_disk_index() < 0 ? LEONOS_UI_BUTTON_DISABLED : 0);

    leonos_ui_groupbox(ui, right_x, DISKMGR_DETAIL_Y, DISKMGR_DETAIL_W, DISKMGR_DETAIL_H,
                       T("Selected disk", "选中磁盘"));
    int index = selected_disk_index();
    if (index >= 0) {
        struct leonos_ui_property_item props[] = {
            {"ID:", disk_id_text[index], 0},
            {T("Name:", "名称:"), disks[index].name, 0},
            {T("Port:", "端口:"), disk_port_text[index], 0},
            {T("Size:", "容量:"), disk_size_text[index], 0},
            {T("Sector:", "扇区:"), disk_sector_text[index], 0},
            {T("Status:", "状态:"), disk_flags_text[index], 0},
        };
        leonos_ui_property_grid(ui, right_x + 10, DISKMGR_DETAIL_Y + 22,
                                DISKMGR_DETAIL_W > 20 ? DISKMGR_DETAIL_W - 20 : DISKMGR_DETAIL_W,
                                props, sizeof(props) / sizeof(props[0]),
                                74, 24);
    } else {
        leonos_ui_text(ui, right_x + 14, DISKMGR_DETAIL_Y + 30, T("No disk selected.", "未选择磁盘。"),
                       LEONOS_UI_DARK, LEONOS_UI_WHITE);
    }

    selected_line[0] = 0;
    append_text(selected_line, &pos, sizeof(selected_line), T("Detected disks: ", "检测到磁盘: "));
    append_u64(selected_line, &pos, sizeof(selected_line), disk_count);
    leonos_ui_text(ui, right_x + 14, DISKMGR_SUMMARY_Y, selected_line, LEONOS_UI_DARK,
                   LEONOS_UI_WHITE);
    if (confirm_open) {
        draw_confirmation(ui);
    } else {
        leonos_ui_groupbox(ui, right_x, DISKMGR_LOWER_PANEL_Y, DISKMGR_DETAIL_W,
                           DISKMGR_LOWER_PANEL_H, T("Safety", "安全"));
        leonos_ui_text_clipped(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 26,
                               DISKMGR_DETAIL_W - 32,
                               T("Formatting is allowed for every disk.", "所有磁盘都允许格式化。"),
                               LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 48,
                               DISKMGR_DETAIL_W - 32,
                               T("Type FORMAT and click twice to erase.", "输入 FORMAT 并点击两次才会擦除。"),
                               LEONOS_UI_DARK, LEONOS_UI_WHITE);
        leonos_ui_text_clipped(ui, right_x + 14, DISKMGR_LOWER_PANEL_Y + 80,
                               DISKMGR_DETAIL_W - 32,
                               T("Mounting is manual after formatting.", "格式化后需要手动挂载。"),
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

static int handle_mouse(uint32_t window_id, struct leonos_ui_surface *ui,
                        const struct leonos_gui_app_event *event)
{
    if (!(event->buttons & 1u)) {
        return 0;
    }
    uint32_t right_x = right_panel_x();
    uint32_t list_w = disk_list_w();
    uint32_t list_h = disk_list_h();
    uint32_t button_y = disk_button_y();
    if (confirm_open &&
        leonos_ui_edit_state_handle_mouse(&confirm_edit, event->x, event->y,
                                          right_x + 86, DISKMGR_CONFIRM_EDIT_Y, 150,
                                          event->buttons)) {
        confirm_armed = 0;
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 16, button_y, 90, LEONOS_UI_BUTTON_H)) {
        refresh_disks();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 116, button_y, 112, LEONOS_UI_BUTTON_H)) {
        open_format_confirm();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (hit_rect_i(event->x, event->y, 238, button_y, 118, LEONOS_UI_BUTTON_H)) {
        do_mount_selected();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (confirm_open && hit_rect_i(event->x, event->y, right_x + 14, DISKMGR_CONFIRM_BUTTON_Y,
                                   134, LEONOS_UI_BUTTON_H)) {
        do_format_selected();
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (confirm_open && hit_rect_i(event->x, event->y, right_x + 158, DISKMGR_CONFIRM_BUTTON_Y,
                                   78, LEONOS_UI_BUTTON_H)) {
        reset_confirm();
        copy_text(status_text, sizeof(status_text), T("Format cancelled", "已取消格式化"));
        present_diskmgr(window_id, ui);
        return 1;
    }
    if (event->x >= (int32_t)(18 + list_w) && event->y >= DISKMGR_LIST_ROW_Y &&
        event->y < (int32_t)(DISKMGR_LIST_ROW_Y + list_h)) {
        if (leonos_ui_vscrollbar_handle_mouse(&disk_list.scroll,
                                              disk_count > disk_list.visible_rows ? disk_count : disk_list.visible_rows,
                                              disk_list.visible_rows,
                                              18 + list_w, DISKMGR_LIST_ROW_Y, 18, list_h, event->x, event->y)) {
            present_diskmgr(window_id, ui);
            return 1;
        }
    } else {
        uint32_t activate = 0;
        if (leonos_ui_listview_state_handle_mouse(&disk_list, event->x, event->y,
                                                  16, DISKMGR_LIST_ROW_Y, list_w, &activate)) {
            confirm_armed = 0;
            present_diskmgr(window_id, ui);
            return 1;
        }
    }
    return 0;
}

static int handle_key(uint32_t window_id, struct leonos_ui_surface *ui,
                      const struct leonos_gui_app_event *event)
{
    if (event->type != LEONOS_GUI_APP_EVENT_KEY_DOWN &&
        event->type != LEONOS_GUI_APP_EVENT_KEY_UP) {
        return 0;
    }
    if (event->pressed && event->keycode == DISKMGR_KEY_ESCAPE) {
        if (confirm_open) {
            reset_confirm();
            copy_text(status_text, sizeof(status_text), T("Format cancelled", "已取消格式化"));
            present_diskmgr(window_id, ui);
            return 1;
        }
        return -1;
    }
    if (confirm_open) {
        if (leonos_ui_edit_state_handle_key(&confirm_edit, event->keycode, event->pressed)) {
            confirm_armed = 0;
            present_diskmgr(window_id, ui);
            return 1;
        }
        if (event->pressed && event->keycode == LEONOS_KEY_ENTER) {
            do_format_selected();
            present_diskmgr(window_id, ui);
            return 1;
        }
    } else if (event->type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
        uint32_t activate = 0;
        if (leonos_ui_listview_state_handle_key(&disk_list, event->keycode, &activate)) {
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
                                                T("Manage disks", "管理磁盘"),
                                                DISKMGR_W, DISKMGR_H, 0);
    if (window_id <= 0) {
        printf("[diskmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, view_w, view_h, DISKMGR_MAX_W);
    leonos_ui_listview_state_init(&disk_list, disk_visible_rows(), DISKMGR_ROW_H);
    disk_list.focused = 1;
    reset_confirm();
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
                if (leonos_ui_listview_state_handle_wheel(&disk_list, event.dy)) {
                    present_diskmgr((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
                event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                int key_result = handle_key((uint32_t)window_id, &ui, &event);
                if (key_result < 0) {
                    return 0;
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width >= DISKMGR_W) {
                    view_w = event.width > DISKMGR_MAX_W ? DISKMGR_MAX_W : event.width;
                }
                if (event.height >= DISKMGR_H) {
                    view_h = event.height > DISKMGR_MAX_H ? DISKMGR_MAX_H : event.height;
                }
                update_disk_list_layout();
                present_diskmgr((uint32_t)window_id, &ui);
            }
        }
        sleep_ms(20);
    }
}
