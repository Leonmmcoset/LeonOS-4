#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define FILEMAN_W 560
#define FILEMAN_H 360
#define FILEMAN_MAX_W 1264
#define FILEMAN_MAX_H 746
#define FILEMAN_MAX_ENTRIES LEONOS_FS_MAX_ENTRIES
#define TOOLBAR_Y 40
#define LIST_X 8
#define LIST_Y 82
#define ROW_H (LEONOS_FONT_H + 8)
#define STATUS_H 28
#define TREE_W 132
#define TREE_ROW_H 24
#define MENU_BAR_H 28
#define MENU_ITEM_H (LEONOS_FONT_H + 8)
#define FILEMAN_KEY_ESCAPE 1U
#define FILEMAN_KEY_UP 72U
#define FILEMAN_KEY_DOWN 80U
#define FILEMAN_CONTEXT_MENU_W 176
#define FILEMAN_CONTEXT_MENU_COUNT 9
#define FILEMAN_DETAILS_W 430
#define FILEMAN_DETAILS_H 190
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    FILEMAN_MENU_NONE = 0,
    FILEMAN_MENU_FILE = 1,
    FILEMAN_MENU_VIEW = 2,
};

enum {
    FILEMAN_ACTION_OPEN = 1,
    FILEMAN_ACTION_OPEN_WITH = 2,
    FILEMAN_ACTION_DEFAULT_PROGRAM = 3,
    FILEMAN_ACTION_DETAILS = 4,
    FILEMAN_ACTION_RENAME = 5,
    FILEMAN_ACTION_DELETE = 6,
    FILEMAN_ACTION_NEW_FOLDER = 7,
    FILEMAN_ACTION_UP = 8,
    FILEMAN_ACTION_REFRESH = 9,
};

static uint32_t pixels[FILEMAN_MAX_W * FILEMAN_MAX_H];
static uint32_t details_pixels[FILEMAN_DETAILS_W * FILEMAN_DETAILS_H];
static struct leonos_dir_entry entries[FILEMAN_MAX_ENTRIES];
static char current_path[LEONOS_FS_PATH_LEN] = "0:/";
static char status_text[96] = "Ready";
static uint32_t entry_count;
static struct leonos_ui_listview_state file_list;
static int32_t last_click_index = -1;
static unsigned long last_click_ms;
static uint8_t menu_open;
static uint8_t context_menu_active;
static uint8_t context_menu_animating;
static uint8_t context_menu_opening;
static unsigned long context_menu_anim_start;
static uint32_t context_menu_x;
static uint32_t context_menu_y;
static uint32_t view_w = FILEMAN_W;
static uint32_t view_h = FILEMAN_H;

struct fileman_layout {
    uint32_t tree_x;
    uint32_t tree_y;
    uint32_t tree_w;
    uint32_t tree_h;
    uint32_t list_x;
    uint32_t list_y;
    uint32_t list_w;
    uint32_t list_h;
    uint32_t rows_y;
    uint32_t rows_h;
    uint32_t visible_rows;
    uint32_t scrollbar_x;
    uint32_t scrollbar_h;
};

static struct fileman_layout current_layout(void)
{
    struct fileman_layout l;
    uint32_t content_h = view_h > LIST_Y + STATUS_H + 10 ? view_h - LIST_Y - STATUS_H - 10 : ROW_H * 2;
    l.tree_x = 8;
    l.tree_y = LIST_Y;
    l.tree_w = view_w > 430 ? TREE_W : 0;
    l.tree_h = content_h + 4;
    l.list_x = l.tree_w ? l.tree_x + l.tree_w + 8 : 8;
    l.list_y = LIST_Y;
    l.list_w = view_w > l.list_x + 34 ? view_w - l.list_x - 26 : 220;
    l.list_h = content_h + 4;
    l.rows_y = l.list_y + 30;
    l.rows_h = l.list_h > 34 ? l.list_h - 34 : ROW_H;
    l.visible_rows = l.rows_h / ROW_H;
    if (!l.visible_rows) {
        l.visible_rows = 1;
    }
    l.scrollbar_x = l.list_x + l.list_w + 2;
    l.scrollbar_h = l.list_h;
    return l;
}

static void copy_text(char *dst, uint32_t dst_len, const char *src)
{
    uint32_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (src) {
        while (i + 1 < dst_len && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
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

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int ends_with(const char *text, const char *suffix)
{
    uint32_t text_n = text_len(text);
    uint32_t suffix_n = text_len(suffix);
    if (suffix_n > text_n) {
        return 0;
    }
    return text_eq(text + text_n - suffix_n, suffix);
}

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (*pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    for (uint32_t i = 0; text && text[i]; ++i) {
        append_char(buf, pos, cap, text[i]);
    }
}

static void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[16];
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

static void append_size(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
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

static int is_root_path(const char *path)
{
    return text_eq(path, "0:/");
}

static int selected_entry_valid(void)
{
    return file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count;
}

static int selected_entry_is_file(void)
{
    return selected_entry_valid() && entries[file_list.selected].type == LEONOS_FS_TYPE_FILE;
}

static int selected_entry_is_mutable(void)
{
    return selected_entry_valid() && entries[file_list.selected].type != LEONOS_FS_TYPE_DEVICE;
}

static int list_index_at(int32_t x, int32_t y)
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

static void format_size_text(char *buf, uint32_t cap, uint64_t bytes)
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

static void set_status(const char *text)
{
    copy_text(status_text, sizeof(status_text), text);
}

static void set_status_code(const char *prefix, int value)
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

static void context_menu_set_active(uint8_t active)
{
    if (context_menu_active == active && !context_menu_animating) {
        return;
    }
    context_menu_active = active;
    context_menu_opening = active;
    context_menu_animating = 1;
    context_menu_anim_start = leonos_uptime_ms();
}

static void build_child_path(char *dst, uint32_t dst_len, const char *name)
{
    uint32_t pos = 0;
    dst[0] = 0;
    append_text(dst, &pos, dst_len, current_path);
    if (!is_root_path(current_path)) {
        append_char(dst, &pos, dst_len, '/');
    }
    append_text(dst, &pos, dst_len, name);
}

static void build_parent_path(char *dst, uint32_t dst_len)
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

static const char *entry_type_name(const struct leonos_dir_entry *entry)
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
    return "FILE";
}

static void build_context_menu_items(struct leonos_ui_context_menu_item *items,
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
        T("Details", "详细信息"), FILEMAN_ACTION_DETAILS, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[4] = (struct leonos_ui_context_menu_item){
        T("Rename", "重命名"), FILEMAN_ACTION_RENAME, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){
        T("Delete", "删除"), FILEMAN_ACTION_DELETE, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[6] = (struct leonos_ui_context_menu_item){
        "", 0, LEONOS_UI_MENU_SEPARATOR};
    items[7] = (struct leonos_ui_context_menu_item){
        T("New Folder", "新建文件夹"), FILEMAN_ACTION_NEW_FOLDER, 0};
    items[8] = (struct leonos_ui_context_menu_item){
        T("Refresh", "刷新"), FILEMAN_ACTION_REFRESH, 0};
}

static void details_add_line(struct leonos_ui_surface *ui, uint32_t y,
                             const char *label, const char *value)
{
    leonos_ui_text(ui, 18, y, label, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text_clipped(ui, 96, y, FILEMAN_DETAILS_W - 114,
                           value ? value : "", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
}

static void show_details_selected(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    struct leonos_stat st;
    char path[LEONOS_FS_PATH_LEN];
    char size_line[56];
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
    format_size_text(size_line, sizeof(size_line), st.size);

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
        leonos_ui_rect(&ui, 0, 0, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H, LEONOS_UI_GRAY);
        leonos_ui_dialog(&ui, 0, 0, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H, T("Properties", "属性"));
        details_add_line(&ui, 48, T("Name:", "名称:"), entries[file_list.selected].name);
        details_add_line(&ui, 72, T("Type:", "类型:"), entry_type_name(&entries[file_list.selected]));
        details_add_line(&ui, 96, T("Path:", "路径:"), path);
        details_add_line(&ui, 120, T("Size:", "大小:"), size_line);
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

static void show_open_with_for_path(const char *path, uint8_t set_default_only)
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

static void show_open_with_selected(void)
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

static void show_default_program_for_selected(void)
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

static int reload_dir(void)
{
    int fd = open(current_path, 0, 0);
    int ret = 0;
    uint32_t count = 0;
    if (fd < 0) {
        entry_count = 0;
        leonos_ui_listview_state_set_count(&file_list, 0);
        set_status_code("Open dir failed ", fd);
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
            set_status_code("Read dir failed ", ret);
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

static void draw_fileman(struct leonos_ui_surface *ui)
{
    struct fileman_layout l = current_layout();
    struct leonos_ui_list_column cols[] = {
        {T("Type", "类型"), 58},
        {T("Name", "名称"), l.list_w > 58 ? l.list_w - 58 : 120},
    };
    struct leonos_ui_tree_item tree_items[] = {
        {"0:/", 1, 0, LEONOS_UI_TREE_EXPANDED},
        {"system", 2, 1, LEONOS_UI_TREE_LEAF},
        {"fonts", 3, 2, LEONOS_UI_TREE_LEAF},
        {"resources", 4, 2, LEONOS_UI_TREE_LEAF},
        {"userland", 5, 1, LEONOS_UI_TREE_LEAF},
    };
    for (uint32_t i = 0; i < sizeof(tree_items) / sizeof(tree_items[0]); ++i) {
        const char *path = i == 0 ? "0:/" :
                           i == 1 ? "0:/system" :
                           i == 2 ? "0:/system/fonts" :
                           i == 3 ? "0:/system/resources" : "0:/userland";
        if (text_eq(current_path, path)) {
            tree_items[i].flags |= LEONOS_UI_TREE_SELECTED;
        }
    }
    file_list.visible_rows = l.visible_rows;
    leonos_ui_listview_state_set_count(&file_list, entry_count);
    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, view_w);
    leonos_ui_menubar_item(ui, 8, 0, 54, T("File", "文件"), menu_open == FILEMAN_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, T("View", "查看"), menu_open == FILEMAN_MENU_VIEW);

    leonos_ui_toolbar(ui, 0, 30, view_w, 42);
    leonos_ui_toolbar_button(ui, 8, TOOLBAR_Y, 54, T("Up", "上级"), 0);
    leonos_ui_toolbar_button(ui, 72, TOOLBAR_Y, 60, T("Open", "打开"), 0);
    leonos_ui_toolbar_button(ui, 142, TOOLBAR_Y, 76, T("Refresh", "刷新"), 0);
    leonos_ui_edit(ui, 230, TOOLBAR_Y, view_w > 238 ? view_w - 238 : 120,
                   current_path, text_len(current_path), 0, LEONOS_UI_EDIT_READONLY);

    if (l.tree_w) {
        leonos_ui_scroll_view_frame(ui, l.tree_x, l.tree_y, l.tree_w, l.tree_h);
        leonos_ui_tree(ui, l.tree_x + 2, l.tree_y + 4, l.tree_w - 4, tree_items,
                       sizeof(tree_items) / sizeof(tree_items[0]), TREE_ROW_H);
    }
    leonos_ui_scroll_view_frame(ui, l.list_x, l.list_y, l.list_w + 22, l.list_h);
    leonos_ui_listview_header(ui, l.list_x + 2, l.list_y + 2, l.list_w, cols, 2);
    for (uint32_t row = 0; row < file_list.visible_rows; ++row) {
        uint32_t i = file_list.scroll + row;
        const char *cells[2];
        if (i >= entry_count) {
            break;
        }
        cells[0] = entry_type_name(&entries[i]);
        cells[1] = entries[i].name;
        leonos_ui_listview_row(ui, l.list_x + 2, l.rows_y + row * ROW_H, l.list_w, cols, cells, 2,
                               file_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, l.scrollbar_x, l.list_y + 2, 18, l.scrollbar_h - 4,
                         file_list.scroll, entry_count > l.visible_rows ? entry_count : l.visible_rows,
                         l.visible_rows,
                         entry_count <= l.visible_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    leonos_ui_statusbar(ui, view_h - STATUS_H, STATUS_H, status_text);

    if (menu_open == FILEMAN_MENU_FILE) {
        uint32_t has_item = selected_entry_valid();
        uint32_t has_file = selected_entry_is_file();
        uint32_t has_mutable = selected_entry_is_mutable();
        leonos_ui_menu(ui, 8, MENU_BAR_H, 174, 242);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 132, T("Open", "打开"),
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 132, T("Open With...", "打开方式..."),
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 132, T("Default Program...", "默认程序..."),
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 132, T("Details", "详细信息"),
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 112, 132, T("Rename", "重命名"),
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 138, 132, T("Delete", "删除"),
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 164, 132, "", LEONOS_UI_MENU_SEPARATOR);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 190, 132, T("New Folder", "新建文件夹"), 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 216, 132, T("Refresh", "刷新"), 0);
    } else if (menu_open == FILEMAN_MENU_VIEW) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, T("Refresh", "刷新"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, T("Root", "根目录"), 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, T("About", "关于"), 0);
    }
    if (context_menu_active || context_menu_animating) {
        struct leonos_ui_context_menu_item items[FILEMAN_CONTEXT_MENU_COUNT];
        build_context_menu_items(items, FILEMAN_CONTEXT_MENU_COUNT);
        uint32_t progress = context_menu_animating
                                ? leonos_ui_anim_progress(leonos_uptime_ms(), context_menu_anim_start, 120)
                                : 1000;
        if (progress >= 1000) {
            context_menu_animating = 0;
            progress = context_menu_active ? 1000 : 0;
        } else if (!context_menu_opening) {
            progress = 1000 - progress;
        }
        leonos_ui_context_menu_animated(ui, context_menu_x, context_menu_y, FILEMAN_CONTEXT_MENU_W,
                                        items, FILEMAN_CONTEXT_MENU_COUNT, progress);
    }
}

static void open_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    int pid;
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status(T("Select an item", "请选择一个项目"));
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    if (entries[file_list.selected].type == LEONOS_FS_TYPE_DIR) {
        copy_text(current_path, sizeof(current_path), path);
        chdir(current_path);
        getcwd(current_path, sizeof(current_path));
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
        } else if (pid <= LEONOS_LAUNCH_ERR_EMPTY && pid >= LEONOS_LAUNCH_ERR_NO_ASSOCIATION) {
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

static void navigate_up(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (is_root_path(current_path)) {
        set_status("Already at root");
        return;
    }
    build_parent_path(path, sizeof(path));
    copy_text(current_path, sizeof(current_path), path);
    chdir(current_path);
    getcwd(current_path, sizeof(current_path));
    reload_dir();
}

static void navigate_root(void)
{
    copy_text(current_path, sizeof(current_path), "0:/");
    chdir(current_path);
    getcwd(current_path, sizeof(current_path));
    reload_dir();
}

static void create_new_folder(void)
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
        set_status_code("Create folder failed ", ret);
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

static void rename_selected_entry(void)
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
        set_status_code("Rename failed ", ret);
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

static void delete_selected_entry(void)
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
            set_status_code("Delete failed ", ret);
        }
        return;
    }
    reload_dir();
    set_status(T("Deleted", "已删除"));
}

static void execute_action(uint32_t action)
{
    context_menu_set_active(0);
    switch (action) {
    case FILEMAN_ACTION_OPEN:
        open_selected_entry();
        break;
    case FILEMAN_ACTION_OPEN_WITH:
        show_open_with_selected();
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

static int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == FILEMAN_MENU_FILE ? FILEMAN_MENU_NONE : FILEMAN_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 64, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == FILEMAN_MENU_VIEW ? FILEMAN_MENU_NONE : FILEMAN_MENU_VIEW;
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    if (menu_open == FILEMAN_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 8, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_valid()) {
                execute_action(FILEMAN_ACTION_OPEN);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 34, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_file()) {
                execute_action(FILEMAN_ACTION_OPEN_WITH);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 60, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_file()) {
                execute_action(FILEMAN_ACTION_DEFAULT_PROGRAM);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 86, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_valid()) {
                execute_action(FILEMAN_ACTION_DETAILS);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 112, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_mutable()) {
                execute_action(FILEMAN_ACTION_RENAME);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 138, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            if (selected_entry_is_mutable()) {
                execute_action(FILEMAN_ACTION_DELETE);
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 190, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            execute_action(FILEMAN_ACTION_NEW_FOLDER);
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 216, 132, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            execute_action(FILEMAN_ACTION_REFRESH);
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    if (menu_open == FILEMAN_MENU_VIEW) {
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            reload_dir();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            navigate_root();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            leonos_ui_show_message_box(T("File Manager", "文件资源管理器"), T("Browse FAT32 files and launch apps.", "浏览 FAT32 文件并启动应用。"), "OK");
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    return 0;
}

static int handle_context_menu_click(int32_t x, int32_t y)
{
    struct leonos_ui_context_menu_item items[FILEMAN_CONTEXT_MENU_COUNT];
    uint32_t action = 0;
    if (!context_menu_active) {
        return 0;
    }
    build_context_menu_items(items, FILEMAN_CONTEXT_MENU_COUNT);
    if (leonos_ui_context_menu_hit(x, y, context_menu_x, context_menu_y,
                                   FILEMAN_CONTEXT_MENU_W, items,
                                   FILEMAN_CONTEXT_MENU_COUNT, &action)) {
        context_menu_set_active(0);
        if (action) {
            execute_action(action);
        }
        return 1;
    }
    context_menu_set_active(0);
    return 0;
}

static void show_context_menu_at(int32_t x, int32_t y, int32_t target)
{
    uint32_t menu_h = leonos_ui_context_menu_height(FILEMAN_CONTEXT_MENU_COUNT);
    menu_open = FILEMAN_MENU_NONE;
    if (target >= 0 && (uint32_t)target < entry_count) {
        file_list.selected = target;
        if ((uint32_t)target < file_list.scroll) {
            file_list.scroll = (uint32_t)target;
        } else if ((uint32_t)target >= file_list.scroll + file_list.visible_rows) {
            file_list.scroll = (uint32_t)target - file_list.visible_rows + 1;
        }
    } else {
        file_list.selected = -1;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    context_menu_x = (uint32_t)x;
    context_menu_y = (uint32_t)y;
    if (context_menu_x + FILEMAN_CONTEXT_MENU_W > view_w) {
        context_menu_x = view_w > FILEMAN_CONTEXT_MENU_W ? view_w - FILEMAN_CONTEXT_MENU_W : 0;
    }
    if (context_menu_y + menu_h > view_h - STATUS_H) {
        context_menu_y = view_h - STATUS_H > menu_h
                             ? view_h - STATUS_H - menu_h
                             : 0;
    }
    context_menu_set_active(1);
}

static void handle_right_click(int32_t x, int32_t y)
{
    int32_t index = list_index_at(x, y);
    if (index >= 0) {
        set_status(entries[index].name);
    } else {
        set_status("Folder actions");
    }
    show_context_menu_at(x, y, index);
}

static void handle_click(int32_t x, int32_t y)
{
    struct fileman_layout l = current_layout();
    if (handle_context_menu_click(x, y)) {
        return;
    }
    if (handle_menu_click(x, y)) {
        context_menu_set_active(0);
        return;
    }
    menu_open = FILEMAN_MENU_NONE;
    context_menu_set_active(0);
    if (x >= 8 && x < 62 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        navigate_up();
        return;
    }
    if (x >= 72 && x < 132 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        open_selected_entry();
        return;
    }
    if (x >= 142 && x < 218 && y >= TOOLBAR_Y && y < TOOLBAR_Y + (int32_t)LEONOS_UI_BUTTON_H) {
        reload_dir();
        return;
    }
    if (l.tree_w && hit_rect_i(x, y, (int32_t)l.tree_x, (int32_t)l.tree_y,
                               (int32_t)l.tree_w, (int32_t)l.tree_h)) {
        struct leonos_ui_tree_item tree_items[] = {
            {"0:/", 1, 0, LEONOS_UI_TREE_EXPANDED},
            {"system", 2, 1, LEONOS_UI_TREE_LEAF},
            {"fonts", 3, 2, LEONOS_UI_TREE_LEAF},
            {"resources", 4, 2, LEONOS_UI_TREE_LEAF},
            {"userland", 5, 1, LEONOS_UI_TREE_LEAF},
        };
        uint32_t id = 0;
        if (leonos_ui_tree_hit(x, y, l.tree_x + 2, l.tree_y + 4, l.tree_w - 4,
                               tree_items, sizeof(tree_items) / sizeof(tree_items[0]),
                               TREE_ROW_H, &id)) {
            const char *path = id == 1 ? "0:/" :
                               id == 2 ? "0:/system" :
                               id == 3 ? "0:/system/fonts" :
                               id == 4 ? "0:/system/resources" :
                               id == 5 ? "0:/userland" : 0;
            if (path) {
                copy_text(current_path, sizeof(current_path), path);
                chdir(current_path);
                getcwd(current_path, sizeof(current_path));
                reload_dir();
            }
        }
        return;
    }
    if (x >= (int32_t)l.scrollbar_x && y >= (int32_t)(l.list_y + 2) &&
        y < (int32_t)(l.list_y + l.scrollbar_h)) {
        leonos_ui_vscrollbar_handle_mouse(&file_list.scroll,
                                          entry_count > l.visible_rows ? entry_count : l.visible_rows,
                                          l.visible_rows,
                                          l.scrollbar_x, l.list_y + 2, 18,
                                          l.scrollbar_h - 4,
                                          x, y);
        return;
    }
    {
        uint32_t activate = 0;
        int32_t before = file_list.selected;
        unsigned long now = leonos_uptime_ms();
        int32_t row = (y - (int32_t)l.rows_y) / (int32_t)ROW_H;
        uint32_t index;
        if (row < 0) {
            return;
        }
        index = file_list.scroll + (uint32_t)row;
        if (index >= entry_count) {
            last_click_index = -1;
            last_click_ms = 0;
            return;
        }
        if (!leonos_ui_listview_state_handle_mouse(&file_list, x, y, l.list_x + 2,
                                                   l.rows_y, l.list_w, &activate)) {
            return;
        }
        activate = before == file_list.selected &&
                   file_list.selected >= 0 &&
                   last_click_index == file_list.selected &&
                   now - last_click_ms <= 500;
        last_click_index = file_list.selected;
        last_click_ms = now;
        if (activate) {
            open_selected_entry();
        } else if (file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count) {
            set_status(entries[file_list.selected].name);
        }
    }
}

static void handle_key(uint8_t keycode)
{
    uint32_t activate = 0;
    if (leonos_ui_listview_state_handle_key(&file_list, keycode, &activate)) {
        if (activate) {
            open_selected_entry();
        } else if (file_list.selected >= 0 && (uint32_t)file_list.selected < entry_count) {
            set_status(entries[file_list.selected].name);
        }
    }
}

static int handle_wheel(int32_t x, int32_t y, int32_t wheel)
{
    struct fileman_layout l = current_layout();
    if (hit_rect_i(x, y, (int32_t)l.list_x, (int32_t)l.list_y,
                   (int32_t)(l.list_w + 24), (int32_t)l.list_h)) {
        return leonos_ui_listview_state_handle_wheel(&file_list, wheel);
    }
    return 0;
}

static void present_fileman(uint32_t window_id, struct leonos_ui_surface *ui)
{
    leonos_ui_bind(ui, pixels, view_w, view_h, FILEMAN_MAX_W);
    draw_fileman(ui);
    leonos_gui_present_window(window_id, view_w, view_h, FILEMAN_MAX_W, pixels);
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[fileman.elf] file manager starting");
    printf("[fileman.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window_ex(T("File Manager", "文件资源管理器"), T("LeonOS file browser", "LeonOS 文件浏览器"),
                                                FILEMAN_W, FILEMAN_H, 0);
    if (window_id <= 0) {
        printf("[fileman.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, view_w, view_h, FILEMAN_MAX_W);
    leonos_ui_listview_state_init(&file_list, current_layout().visible_rows, ROW_H);
    file_list.focused = 1;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        copy_text(current_path, sizeof(current_path), argv[1]);
    }
    chdir(current_path);
    getcwd(current_path, sizeof(current_path));
    reload_dir();
    present_fileman((uint32_t)window_id, &ui);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                if (event.buttons & 2u) {
                    handle_right_click(event.x, event.y);
                } else {
                    handle_click(event.x, event.y);
                }
                file_list.focused = 1;
                present_fileman((uint32_t)window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                handle_key(event.keycode);
                present_fileman((uint32_t)window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (handle_wheel(event.x, event.y, event.dy)) {
                    present_fileman((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width >= 320) {
                    view_w = event.width > FILEMAN_MAX_W ? FILEMAN_MAX_W : event.width;
                }
                if (event.height >= 240) {
                    view_h = event.height > FILEMAN_MAX_H ? FILEMAN_MAX_H : event.height;
                }
                file_list.visible_rows = current_layout().visible_rows;
                leonos_ui_listview_state_set_count(&file_list, entry_count);
                present_fileman((uint32_t)window_id, &ui);
            }
        } else if (context_menu_animating) {
            present_fileman((uint32_t)window_id, &ui);
            sleep_ms(10);
            continue;
        } else {
            sleep_ms(10);
        }
    }
}
