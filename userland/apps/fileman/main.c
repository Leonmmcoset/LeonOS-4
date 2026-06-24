#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/launch.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define FILEMAN_W 560
#define FILEMAN_H 360
#define FILEMAN_MAX_ENTRIES 24
#define TOOLBAR_Y 40
#define LIST_X 8
#define LIST_Y 82
#define LIST_W (FILEMAN_W - 34)
#define ROW_H (LEONOS_FONT_H + 8)
#define STATUS_H 28
#define LIST_VISIBLE_ROWS 9
#define MENU_BAR_H 28
#define MENU_ITEM_H (LEONOS_FONT_H + 8)

enum {
    FILEMAN_MENU_NONE = 0,
    FILEMAN_MENU_FILE = 1,
    FILEMAN_MENU_VIEW = 2,
};

static uint32_t pixels[FILEMAN_W * FILEMAN_H];
static struct leonos_dir_entry entries[FILEMAN_MAX_ENTRIES];
static char current_path[LEONOS_FS_PATH_LEN] = "0:/";
static char status_text[96] = "Ready";
static uint32_t entry_count;
static struct leonos_ui_listview_state file_list;
static int32_t last_click_index = -1;
static unsigned long last_click_ms;
static uint8_t menu_open;

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

static int is_root_path(const char *path)
{
    return text_eq(path, "0:/");
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
    last_click_index = -1;
    last_click_ms = 0;
    char buf[96];
    uint32_t pos = 0;
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), "Items ");
    append_dec(buf, &pos, sizeof(buf), entry_count);
    append_text(buf, &pos, sizeof(buf), " in ");
    append_text(buf, &pos, sizeof(buf), current_path);
    set_status(buf);
    printf("[fileman.elf] list path=%s count=%d\n", current_path, (int)count);
    return 0;
}

static void draw_fileman(struct leonos_ui_surface *ui)
{
    struct leonos_ui_list_column cols[] = {
        {"Type", 58},
        {"Name", LIST_W - 58},
    };
    leonos_ui_rect(ui, 0, 0, FILEMAN_W, FILEMAN_H, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, FILEMAN_W);
    leonos_ui_menubar_item(ui, 8, 0, 54, "File", menu_open == FILEMAN_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, "View", menu_open == FILEMAN_MENU_VIEW);

    leonos_ui_toolbar(ui, 0, 30, FILEMAN_W, 42);
    leonos_ui_toolbar_button(ui, 8, TOOLBAR_Y, 54, "Up", 0);
    leonos_ui_toolbar_button(ui, 72, TOOLBAR_Y, 60, "Open", 0);
    leonos_ui_toolbar_button(ui, 142, TOOLBAR_Y, 76, "Refresh", 0);
    leonos_ui_edit(ui, 230, TOOLBAR_Y, FILEMAN_W - 238, current_path, text_len(current_path), 0, LEONOS_UI_EDIT_READONLY);

    leonos_ui_scroll_view_frame(ui, LIST_X, LIST_Y, FILEMAN_W - 16, FILEMAN_H - LIST_Y - STATUS_H - 6);
    leonos_ui_listview_header(ui, LIST_X + 2, LIST_Y + 2, LIST_W, cols, 2);
    for (uint32_t row = 0; row < file_list.visible_rows; ++row) {
        uint32_t i = file_list.scroll + row;
        const char *cells[2];
        if (i >= entry_count) {
            break;
        }
        cells[0] = entry_type_name(&entries[i]);
        cells[1] = entries[i].name;
        leonos_ui_listview_row(ui, LIST_X + 2, LIST_Y + 30 + row * ROW_H, LIST_W, cols, cells, 2,
                               file_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, FILEMAN_W - 26, LIST_Y + 2, 18,
                         FILEMAN_H - LIST_Y - STATUS_H - 10,
                         file_list.scroll, entry_count > LIST_VISIBLE_ROWS ? entry_count : LIST_VISIBLE_ROWS,
                         LIST_VISIBLE_ROWS,
                         entry_count <= LIST_VISIBLE_ROWS ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    leonos_ui_statusbar(ui, FILEMAN_H - STATUS_H, STATUS_H, status_text);

    if (menu_open == FILEMAN_MENU_FILE) {
        leonos_ui_menu(ui, 8, MENU_BAR_H, 154, 112);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 116, "Open", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 116, "Up", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 116, "Refresh", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 116, "About", 0);
    } else if (menu_open == FILEMAN_MENU_VIEW) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, "Refresh", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, "Root", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, "About", 0);
    }
}

static void open_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    int pid;
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status("Select an item");
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
        if (pid <= LEONOS_LAUNCH_ERR_EMPTY && pid >= LEONOS_LAUNCH_ERR_NO_ASSOCIATION) {
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
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            open_selected_entry();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            navigate_up();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            reload_dir();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 86, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = FILEMAN_MENU_NONE;
            leonos_ui_show_message_box("File Manager", "Browse FAT32 files and launch apps.", "OK");
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
            leonos_ui_show_message_box("File Manager", "Double-click entries to open them.", "OK");
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    return 0;
}

static void handle_click(int32_t x, int32_t y)
{
    if (handle_menu_click(x, y)) {
        return;
    }
    menu_open = FILEMAN_MENU_NONE;
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
    if (x >= FILEMAN_W - 26 && y >= LIST_Y + 2 &&
        y < FILEMAN_H - STATUS_H - 8) {
        leonos_ui_vscrollbar_handle_mouse(&file_list.scroll,
                                          entry_count > LIST_VISIBLE_ROWS ? entry_count : LIST_VISIBLE_ROWS,
                                          LIST_VISIBLE_ROWS,
                                          FILEMAN_W - 26, LIST_Y + 2, 18,
                                          FILEMAN_H - LIST_Y - STATUS_H - 10,
                                          x, y);
        return;
    }
    {
        uint32_t activate = 0;
        int32_t before = file_list.selected;
        unsigned long now = leonos_uptime_ms();
        int32_t row = (y - (int32_t)(LIST_Y + 30)) / (int32_t)ROW_H;
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
        if (!leonos_ui_listview_state_handle_mouse(&file_list, x, y, LIST_X + 2,
                                                   LIST_Y + 30, LIST_W, &activate)) {
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

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[fileman.elf] file manager starting");
    printf("[fileman.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window_ex("File Manager", "LeonOS file browser",
                                                FILEMAN_W, FILEMAN_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[fileman.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, FILEMAN_W, FILEMAN_H, FILEMAN_W);
    leonos_ui_listview_state_init(&file_list, LIST_VISIBLE_ROWS, ROW_H);
    file_list.focused = 1;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        copy_text(current_path, sizeof(current_path), argv[1]);
    }
    chdir(current_path);
    getcwd(current_path, sizeof(current_path));
    reload_dir();
    draw_fileman(&ui);
    leonos_gui_present_window((uint32_t)window_id, FILEMAN_W, FILEMAN_H, FILEMAN_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1)) {
                handle_click(event.x, event.y);
                file_list.focused = 1;
                draw_fileman(&ui);
                leonos_gui_present_window((uint32_t)window_id, FILEMAN_W, FILEMAN_H, FILEMAN_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                handle_key(event.keycode);
                draw_fileman(&ui);
                leonos_gui_present_window((uint32_t)window_id, FILEMAN_W, FILEMAN_H, FILEMAN_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_fileman(&ui);
                leonos_gui_present_window((uint32_t)window_id, FILEMAN_W, FILEMAN_H, FILEMAN_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
}
