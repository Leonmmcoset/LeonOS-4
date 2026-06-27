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
#define FILEMAN_KEY_ESCAPE 1U
#define FILEMAN_KEY_UP 72U
#define FILEMAN_KEY_DOWN 80U
#define OPEN_WITH_X 64
#define OPEN_WITH_Y 56
#define OPEN_WITH_W 432
#define OPEN_WITH_H 296
#define OPEN_WITH_ROW_H 34
#define FILEMAN_CONTEXT_MENU_W 176
#define FILEMAN_CONTEXT_MENU_COUNT 9
#define FILEMAN_DETAILS_W 430
#define FILEMAN_DETAILS_H 190

enum {
    FILEMAN_MENU_NONE = 0,
    FILEMAN_MENU_FILE = 1,
    FILEMAN_MENU_VIEW = 2,
};

enum {
    OPEN_WITH_MODE_TEMP = 0,
    OPEN_WITH_MODE_DEFAULT = 1,
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

static uint32_t pixels[FILEMAN_W * FILEMAN_H];
static uint32_t details_pixels[FILEMAN_DETAILS_W * FILEMAN_DETAILS_H];
static struct leonos_dir_entry entries[FILEMAN_MAX_ENTRIES];
static char current_path[LEONOS_FS_PATH_LEN] = "0:/";
static char status_text[96] = "Ready";
static char open_with_path[LEONOS_FS_PATH_LEN];
static char open_with_extension[16];
static char open_with_current_default[LEONOS_FS_PATH_LEN];
static char open_with_title[32];
static uint32_t entry_count;
static uint32_t open_with_app_count;
static const struct leonos_launch_assoc_app *open_with_apps;
static struct leonos_ui_listview_state file_list;
static struct leonos_ui_listview_state open_with_list;
static int32_t last_click_index = -1;
static unsigned long last_click_ms;
static uint8_t open_with_mode;
static uint8_t open_with_can_remember;
static uint8_t open_with_remember;
static uint8_t menu_open;
static uint8_t open_with_active;
static uint8_t context_menu_active;
static uint32_t context_menu_x;
static uint32_t context_menu_y;
static int32_t open_with_selected;

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

static uint32_t max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
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
    int32_t row;
    uint32_t index;
    if (!hit_rect_i(x, y, LIST_X + 2, LIST_Y + 30, LIST_W,
                    (int32_t)(LIST_VISIBLE_ROWS * ROW_H))) {
        return -1;
    }
    row = (y - (int32_t)(LIST_Y + 30)) / (int32_t)ROW_H;
    if (row < 0) {
        return -1;
    }
    index = file_list.scroll + (uint32_t)row;
    if (index >= entry_count) {
        return -1;
    }
    return (int)index;
}

static const struct leonos_launch_assoc_app *find_open_with_app(const char *program_path)
{
    uint32_t i;
    for (i = 0; i < open_with_app_count; ++i) {
        if (text_eq(open_with_apps[i].program_path, program_path)) {
            return &open_with_apps[i];
        }
    }
    return 0;
}

static int find_open_with_index(const char *program_path)
{
    uint32_t i;
    for (i = 0; i < open_with_app_count; ++i) {
        if (text_eq(open_with_apps[i].program_path, program_path)) {
            return (int)i;
        }
    }
    return -1;
}

static const char *open_with_app_label(const char *program_path, char *buf, uint32_t cap)
{
    const struct leonos_launch_assoc_app *app = find_open_with_app(program_path);
    if (app) {
        return app->name;
    }
    if (!program_path || !program_path[0]) {
        return "None";
    }
    copy_text(buf, cap, program_path);
    return buf;
}

static int has_extension(const char *path)
{
    uint32_t i = 0;
    int seen_dot = 0;
    if (!path || !path[0]) {
        return 0;
    }
    while (path[i]) {
        if (path[i] == '/') {
            seen_dot = 0;
        } else if (path[i] == '.') {
            seen_dot = 1;
        }
        ++i;
    }
    return seen_dot;
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
        "Open", FILEMAN_ACTION_OPEN, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){
        "Open With...", FILEMAN_ACTION_OPEN_WITH, has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){
        "Default Program...", FILEMAN_ACTION_DEFAULT_PROGRAM,
        has_file ? 0 : LEONOS_UI_MENU_DISABLED};
    items[3] = (struct leonos_ui_context_menu_item){
        "Details", FILEMAN_ACTION_DETAILS, has_item ? 0 : LEONOS_UI_MENU_DISABLED};
    items[4] = (struct leonos_ui_context_menu_item){
        "Rename", FILEMAN_ACTION_RENAME, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[5] = (struct leonos_ui_context_menu_item){
        "Delete", FILEMAN_ACTION_DELETE, has_mutable ? 0 : LEONOS_UI_MENU_DISABLED};
    items[6] = (struct leonos_ui_context_menu_item){
        "", 0, LEONOS_UI_MENU_SEPARATOR};
    items[7] = (struct leonos_ui_context_menu_item){
        "New Folder", FILEMAN_ACTION_NEW_FOLDER, 0};
    items[8] = (struct leonos_ui_context_menu_item){
        "Refresh", FILEMAN_ACTION_REFRESH, 0};
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
    char size_text[32];
    char size_line[56];
    uint32_t pos = 0;
    int window_id;
    if (!selected_entry_valid()) {
        set_status("Select an item");
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    if (stat(path, &st) < 0) {
        set_status("Details stat failed");
        return;
    }
    size_text[0] = 0;
    size_line[0] = 0;
    append_size(size_text, &pos, sizeof(size_text), st.size);
    pos = 0;
    append_text(size_line, &pos, sizeof(size_line), size_text);
    append_text(size_line, &pos, sizeof(size_line), " bytes");

    window_id = leonos_gui_create_app_window_ex("Properties", path,
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
        leonos_ui_dialog(&ui, 0, 0, FILEMAN_DETAILS_W, FILEMAN_DETAILS_H, "Properties");
        details_add_line(&ui, 48, "Name:", entries[file_list.selected].name);
        details_add_line(&ui, 72, "Type:", entry_type_name(&entries[file_list.selected]));
        details_add_line(&ui, 96, "Path:", path);
        details_add_line(&ui, 120, "Size:", size_line);
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

static void set_open_with_state(const char *path, const char *title, uint8_t remember)
{
    copy_text(open_with_path, sizeof(open_with_path), path);
    open_with_apps = leonos_launch_assoc_apps(&open_with_app_count);
    open_with_selected = 0;
    open_with_mode = remember ? OPEN_WITH_MODE_DEFAULT : OPEN_WITH_MODE_TEMP;
    open_with_can_remember = has_extension(path);
    open_with_remember = remember && open_with_can_remember;
    copy_text(open_with_title, sizeof(open_with_title), title ? title : "Open With");
    open_with_extension[0] = 0;
    leonos_launch_get_extension_for_path(path, open_with_extension, sizeof(open_with_extension));
    leonos_ui_listview_state_init(&open_with_list, open_with_app_count > 4 ? 4 : open_with_app_count,
                                  OPEN_WITH_ROW_H);
    leonos_ui_listview_state_set_count(&open_with_list, open_with_app_count);
    open_with_list.scroll = 0;
    open_with_list.selected = -1;
    open_with_list.focused = 1;
    {
        const char *default_program = leonos_launch_resolve_default_app_for_path(path);
        copy_text(open_with_current_default, sizeof(open_with_current_default),
                  open_with_app_label(default_program, open_with_current_default,
                                      sizeof(open_with_current_default)));
        if (default_program) {
            int index = find_open_with_index(default_program);
            if (index >= 0) {
                open_with_selected = index;
            }
        }
    }
    if (open_with_selected < 0) {
        open_with_selected = 0;
    }
    open_with_list.selected = open_with_selected;
    open_with_active = 1;
    menu_open = FILEMAN_MENU_NONE;
    if (open_with_mode == OPEN_WITH_MODE_DEFAULT) {
        set_status("Choose a default program for this file type");
    } else {
        set_status("Choose a program to open this file");
    }
    printf("[fileman.elf] open-with request path=%s\n", open_with_path);
}

static void show_open_with_selected(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status("Select a file");
        return;
    }
    if (entries[file_list.selected].type != LEONOS_FS_TYPE_FILE) {
        set_status("Open With is for files");
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    set_open_with_state(path, "Open With", 0);
}

static void show_default_program_for_selected(void)
{
    char path[LEONOS_FS_PATH_LEN];
    if (file_list.selected < 0 || (uint32_t)file_list.selected >= entry_count) {
        set_status("Select a file");
        return;
    }
    if (entries[file_list.selected].type != LEONOS_FS_TYPE_FILE) {
        set_status("Default program is for files");
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    set_open_with_state(path, "Default Program", 1);
}

static int commit_open_with_selected(void)
{
    const struct leonos_launch_assoc_app *app;
    int pid = LEONOS_LAUNCH_ERR_NOT_FOUND;
    if (!open_with_active) {
        return 0;
    }
    if (open_with_list.selected < 0 || (uint32_t)open_with_list.selected >= open_with_app_count) {
        set_status("Choose a program");
        return LEONOS_LAUNCH_ERR_EMPTY;
    }

    app = &open_with_apps[open_with_list.selected];
    if (open_with_mode == OPEN_WITH_MODE_DEFAULT) {
        if (!open_with_extension[0]) {
            set_status("This file has no extension to remember");
            return LEONOS_LAUNCH_ERR_NO_ASSOCIATION;
        }
        pid = leonos_launch_set_extension_association(open_with_extension, app->program_path);
        if (pid < 0) {
            set_status_code("Save association failed ", pid);
            return pid;
        }
        {
            char buf[96];
            uint32_t pos = 0;
            buf[0] = 0;
            append_text(buf, &pos, sizeof(buf), "Default app set to ");
            append_text(buf, &pos, sizeof(buf), app->name);
            append_text(buf, &pos, sizeof(buf), " for ");
            append_text(buf, &pos, sizeof(buf), open_with_extension);
            set_status(buf);
        }
        open_with_active = 0;
        return pid;
    }

    if (open_with_remember && open_with_can_remember && open_with_extension[0]) {
        int ret = leonos_launch_set_extension_association(open_with_extension, app->program_path);
        if (ret < 0) {
            set_status_code("Save association failed ", ret);
            return ret;
        } else {
            copy_text(open_with_current_default, sizeof(open_with_current_default), app->name);
        }
    }

    pid = leonos_launch_file_with_app(open_with_path, app->program_path);
    printf("[fileman.elf] open-with path=%s app=%s pid=%d\n",
           open_with_path, app->name, pid);
    if (pid < 0) {
        set_status_code("Open With failed ", pid);
        return pid;
    }

    {
        char buf[96];
        uint32_t pos = 0;
        buf[0] = 0;
        append_text(buf, &pos, sizeof(buf), "Open With launched ");
        append_text(buf, &pos, sizeof(buf), app->name);
        append_text(buf, &pos, sizeof(buf), " pid ");
        append_dec(buf, &pos, sizeof(buf), (uint32_t)pid);
        set_status(buf);
    }
    open_with_active = 0;
    return pid;
}

static void cancel_open_with(void)
{
    open_with_active = 0;
    set_status("Open With canceled");
}

static void draw_open_with_dialog(struct leonos_ui_surface *ui)
{
    uint32_t list_x = OPEN_WITH_X + 16;
    uint32_t list_y = OPEN_WITH_Y + 170;
    uint32_t list_w = OPEN_WITH_W - 32;
    uint32_t list_h = open_with_list.visible_rows * OPEN_WITH_ROW_H + 8;
    uint32_t scrollbar_x = list_x + list_w - 18;
    uint32_t row_w = list_w - 26;
    char default_program[LEONOS_FS_PATH_LEN];
    const char *default_label = open_with_app_label(open_with_current_default,
                                                    default_program,
                                                    sizeof(default_program));
    leonos_ui_dialog(ui, OPEN_WITH_X, OPEN_WITH_Y, OPEN_WITH_W, OPEN_WITH_H, open_with_title);
    leonos_ui_text_clipped(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 44, OPEN_WITH_W - 32,
                           open_with_mode == OPEN_WITH_MODE_DEFAULT
                               ? "Choose a default program for this file type:"
                               : "Choose a program to open this file:",
                           LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 68, "File:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(ui, OPEN_WITH_X + 58, OPEN_WITH_Y + 64, OPEN_WITH_W - 74,
                   open_with_path, text_len(open_with_path), 0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 92, "Extension:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(ui, OPEN_WITH_X + 82, OPEN_WITH_Y + 88, 84,
                   open_with_extension[0] ? open_with_extension : "(none)",
                   text_len(open_with_extension[0] ? open_with_extension : "(none)"),
                   0, LEONOS_UI_EDIT_READONLY);
    leonos_ui_text(ui, OPEN_WITH_X + 180, OPEN_WITH_Y + 92, "Default:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_edit(ui, OPEN_WITH_X + 244, OPEN_WITH_Y + 88, OPEN_WITH_W - 260,
                   default_label, text_len(default_label), 0, LEONOS_UI_EDIT_READONLY);
    if (open_with_mode == OPEN_WITH_MODE_TEMP) {
        leonos_ui_checkbox(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 118,
                           "Always use this app",
                           open_with_can_remember ? open_with_remember : 0, 0);
    } else {
        leonos_ui_checkbox(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 118,
                           "Update default program",
                           1, LEONOS_UI_BUTTON_DISABLED);
    }
    leonos_ui_text(ui, OPEN_WITH_X + 16, OPEN_WITH_Y + 144, "Programs:",
                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_inset(ui, list_x, list_y, list_w, list_h, LEONOS_UI_WHITE);
    for (uint32_t row = 0; row < open_with_list.visible_rows; ++row) {
        uint32_t i = open_with_list.scroll + row;
        uint32_t row_x = list_x + 4;
        uint32_t row_y = list_y + 4 + row * OPEN_WITH_ROW_H;
        uint32_t selected;
        uint32_t bg;
        uint32_t fg;
        uint32_t detail_fg;
        if (i >= open_with_app_count) {
            break;
        }
        selected = open_with_list.selected == (int32_t)i;
        bg = selected ? LEONOS_UI_ACTIVE_TITLE : LEONOS_UI_WHITE;
        fg = selected ? LEONOS_UI_WHITE : LEONOS_UI_BLACK;
        detail_fg = selected ? LEONOS_UI_LIGHT : LEONOS_UI_DARK;
        leonos_ui_rect(ui, row_x, row_y, row_w, OPEN_WITH_ROW_H, bg);
        leonos_ui_text_clipped(ui, row_x + 8, row_y + 3, row_w - 16,
                               open_with_apps[i].name, fg, bg);
        leonos_ui_text_clipped(ui, row_x + 8, row_y + 18, row_w - 16,
                               open_with_apps[i].detail, detail_fg, bg);
    }
    leonos_ui_vscrollbar(ui, scrollbar_x, list_y, 18, list_h,
                         open_with_list.scroll,
                         max_u32(open_with_app_count, open_with_list.visible_rows),
                         open_with_list.visible_rows,
                         open_with_app_count <= open_with_list.visible_rows
                             ? LEONOS_UI_SCROLLBAR_DISABLED
                             : 0);
    leonos_ui_button(ui, OPEN_WITH_X + OPEN_WITH_W - 194, OPEN_WITH_Y + OPEN_WITH_H - 38,
                     96, LEONOS_UI_BUTTON_H,
                     open_with_mode == OPEN_WITH_MODE_DEFAULT ? "Set Default" : "Open", 0);
    leonos_ui_button(ui, OPEN_WITH_X + OPEN_WITH_W - 88, OPEN_WITH_Y + OPEN_WITH_H - 38,
                     72, LEONOS_UI_BUTTON_H, "Cancel", 0);
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
    context_menu_active = 0;
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
        uint32_t has_item = selected_entry_valid();
        uint32_t has_file = selected_entry_is_file();
        uint32_t has_mutable = selected_entry_is_mutable();
        leonos_ui_menu(ui, 8, MENU_BAR_H, 174, 242);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 132, "Open",
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 132, "Open With...",
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 132, "Default Program...",
                            has_file ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 132, "Details",
                            has_item ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 112, 132, "Rename",
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 138, 132, "Delete",
                            has_mutable ? 0 : LEONOS_UI_MENU_DISABLED);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 164, 132, "", LEONOS_UI_MENU_SEPARATOR);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 190, 132, "New Folder", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 216, 132, "Refresh", 0);
    } else if (menu_open == FILEMAN_MENU_VIEW) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, "Refresh", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, "Root", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, "About", 0);
    }
    if (context_menu_active) {
        struct leonos_ui_context_menu_item items[FILEMAN_CONTEXT_MENU_COUNT];
        build_context_menu_items(items, FILEMAN_CONTEXT_MENU_COUNT);
        leonos_ui_context_menu(ui, context_menu_x, context_menu_y, FILEMAN_CONTEXT_MENU_W,
                               items, FILEMAN_CONTEXT_MENU_COUNT);
    }
    if (open_with_active) {
        draw_open_with_dialog(ui);
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
        if (pid == LEONOS_LAUNCH_ERR_NO_ASSOCIATION) {
            set_open_with_state(path, "Open With", 0);
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
    if (!leonos_ui_show_input_dialog("New Folder", "Folder name:", name, sizeof(name))) {
        set_status("New folder canceled");
        return;
    }
    if (!name[0]) {
        set_status("Folder name is empty");
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
    set_status("Folder created");
}

static void rename_selected_entry(void)
{
    char old_path[LEONOS_FS_PATH_LEN];
    char new_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    int ret;
    if (!selected_entry_valid()) {
        set_status("Select an item");
        return;
    }
    copy_text(name, sizeof(name), entries[file_list.selected].name);
    if (!leonos_ui_show_input_dialog("Rename", "New name:", name, sizeof(name))) {
        set_status("Rename canceled");
        return;
    }
    if (!name[0]) {
        set_status("New name is empty");
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
    set_status("Renamed");
}

static void delete_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    char message[96];
    uint32_t pos = 0;
    int ret;
    if (!selected_entry_valid()) {
        set_status("Select an item");
        return;
    }
    message[0] = 0;
    append_text(message, &pos, sizeof(message), "Delete ");
    append_text(message, &pos, sizeof(message), entries[file_list.selected].name);
    append_char(message, &pos, sizeof(message), '?');
    if (!leonos_ui_show_confirm_dialog("Delete", message, 0)) {
        set_status("Delete canceled");
        return;
    }
    build_child_path(path, sizeof(path), entries[file_list.selected].name);
    ret = entries[file_list.selected].type == LEONOS_FS_TYPE_DIR ? rmdir(path) : unlink(path);
    if (ret < 0) {
        if (ret == -39) {
            set_status("Delete failed: directory not empty");
        } else {
            set_status_code("Delete failed ", ret);
        }
        return;
    }
    reload_dir();
    set_status("Deleted");
}

static void execute_action(uint32_t action)
{
    context_menu_active = 0;
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
            leonos_ui_show_message_box("File Manager", "Browse FAT32 files and launch apps.", "OK");
            return 1;
        }
        menu_open = FILEMAN_MENU_NONE;
        return 1;
    }
    return 0;
}

static int handle_open_with_click(int32_t x, int32_t y)
{
    int32_t list_x = OPEN_WITH_X + 16;
    int32_t list_y = OPEN_WITH_Y + 170;
    int32_t list_w = OPEN_WITH_W - 32;
    int32_t list_h = (int32_t)(open_with_list.visible_rows * OPEN_WITH_ROW_H + 8);
    int32_t scrollbar_x = list_x + list_w - 18;
    uint32_t activated = 0;
    if (!open_with_active) {
        return 0;
    }
    if (hit_rect_i(x, y, OPEN_WITH_X + OPEN_WITH_W - 194, OPEN_WITH_Y + OPEN_WITH_H - 38,
                   96, (int32_t)LEONOS_UI_BUTTON_H)) {
        commit_open_with_selected();
        return 1;
    }
    if (hit_rect_i(x, y, OPEN_WITH_X + OPEN_WITH_W - 88, OPEN_WITH_Y + OPEN_WITH_H - 38,
                   72, (int32_t)LEONOS_UI_BUTTON_H)) {
        cancel_open_with();
        return 1;
    }
    if (open_with_mode == OPEN_WITH_MODE_TEMP &&
        open_with_can_remember &&
        hit_rect_i(x, y, OPEN_WITH_X + 16, OPEN_WITH_Y + 118, 180, (int32_t)LEONOS_FONT_H + 8)) {
        open_with_remember = open_with_remember ? 0 : 1;
        return 1;
    }
    if (hit_rect_i(x, y, scrollbar_x, list_y, 18, list_h)) {
        leonos_ui_vscrollbar_handle_mouse(&open_with_list.scroll,
                                          max_u32(open_with_app_count, open_with_list.visible_rows),
                                          open_with_list.visible_rows,
                                          (uint32_t)scrollbar_x, (uint32_t)list_y, 18, (uint32_t)list_h,
                                          x, y);
        return 1;
    }
    if (hit_rect_i(x, y, list_x, list_y, list_w, list_h)) {
        if (leonos_ui_listview_state_handle_mouse(&open_with_list, x, y,
                                                  (uint32_t)(list_x + 4),
                                                  (uint32_t)(list_y + 4),
                                                  (uint32_t)(list_w - 26),
                                                  &activated)) {
            open_with_selected = open_with_list.selected;
            if (activated) {
                commit_open_with_selected();
            }
        }
        return 1;
    }
    return 1;
}

static int handle_open_with_key(uint8_t keycode)
{
    if (!open_with_active) {
        return 0;
    }
    if (keycode == LEONOS_KEY_ENTER) {
        commit_open_with_selected();
        return 1;
    }
    if (keycode == FILEMAN_KEY_ESCAPE) {
        cancel_open_with();
        return 1;
    }
    if (keycode == FILEMAN_KEY_UP) {
        uint32_t activated = 0;
        if (leonos_ui_listview_state_handle_key(&open_with_list, keycode, &activated)) {
            open_with_selected = open_with_list.selected;
        }
        return 1;
    }
    if (keycode == FILEMAN_KEY_DOWN) {
        uint32_t activated = 0;
        if (leonos_ui_listview_state_handle_key(&open_with_list, keycode, &activated)) {
            open_with_selected = open_with_list.selected;
        }
        return 1;
    }
    if (keycode == LEONOS_KEY_SPACE && open_with_mode == OPEN_WITH_MODE_TEMP && open_with_can_remember) {
        open_with_remember = open_with_remember ? 0 : 1;
        return 1;
    }
    return 1;
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
        context_menu_active = 0;
        if (action) {
            execute_action(action);
        }
        return 1;
    }
    context_menu_active = 0;
    return 0;
}

static void show_context_menu_at(int32_t x, int32_t y, int32_t target)
{
    uint32_t menu_h = leonos_ui_context_menu_height(FILEMAN_CONTEXT_MENU_COUNT);
    menu_open = FILEMAN_MENU_NONE;
    open_with_active = 0;
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
    if (context_menu_x + FILEMAN_CONTEXT_MENU_W > FILEMAN_W) {
        context_menu_x = FILEMAN_W - FILEMAN_CONTEXT_MENU_W;
    }
    if (context_menu_y + menu_h > FILEMAN_H - STATUS_H) {
        context_menu_y = FILEMAN_H - STATUS_H > menu_h
                             ? FILEMAN_H - STATUS_H - menu_h
                             : 0;
    }
    context_menu_active = 1;
}

static void handle_right_click(int32_t x, int32_t y)
{
    int32_t index = list_index_at(x, y);
    if (open_with_active) {
        return;
    }
    if (index >= 0) {
        set_status(entries[index].name);
    } else {
        set_status("Folder actions");
    }
    show_context_menu_at(x, y, index);
}

static void handle_click(int32_t x, int32_t y)
{
    if (handle_open_with_click(x, y)) {
        return;
    }
    if (handle_context_menu_click(x, y)) {
        return;
    }
    if (handle_menu_click(x, y)) {
        context_menu_active = 0;
        return;
    }
    menu_open = FILEMAN_MENU_NONE;
    context_menu_active = 0;
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
    if (handle_open_with_key(keycode)) {
        return;
    }
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
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                if (event.buttons & 2u) {
                    handle_right_click(event.x, event.y);
                } else {
                    handle_click(event.x, event.y);
                }
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
