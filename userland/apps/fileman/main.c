#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define FILEMAN_W 560
#define FILEMAN_H 360
#define FILEMAN_MAX_ENTRIES 24
#define TOOLBAR_Y 40
#define LIST_X 8
#define LIST_Y 78
#define LIST_W (FILEMAN_W - 16)
#define ROW_H (LEONOS_FONT_H + 8)

static uint32_t pixels[FILEMAN_W * FILEMAN_H];
static struct leonos_dir_entry entries[FILEMAN_MAX_ENTRIES];
static char current_path[LEONOS_FS_PATH_LEN] = "0:/";
static char status_text[96] = "Ready";
static uint32_t entry_count;
static int32_t selected_index = -1;

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
    uint32_t count = 0;
    int ret = leonos_list_dir(current_path, entries, FILEMAN_MAX_ENTRIES, &count);
    if (ret < 0) {
        entry_count = 0;
        selected_index = -1;
        set_status_code("List failed ", ret);
        printf("[fileman.elf] list path=%s ret=%d\n", current_path, ret);
        return ret;
    }
    entry_count = count;
    selected_index = -1;
    char buf[96];
    uint32_t pos = 0;
    buf[0] = 0;
    append_text(buf, &pos, sizeof(buf), "Items ");
    append_dec(buf, &pos, sizeof(buf), entry_count);
    append_text(buf, &pos, sizeof(buf), " in ");
    append_text(buf, &pos, sizeof(buf), current_path);
    set_status(buf);
    printf("[fileman.elf] list path=%s count=%d\n", current_path, ret);
    return ret;
}

static void draw_fileman(struct leonos_ui_surface *ui)
{
    char line[96];
    leonos_ui_rect(ui, 0, 0, FILEMAN_W, FILEMAN_H, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 10, 10, "LeonOS File Manager", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 10, 28, "Real GUI app over LeonOS directory listing", LEONOS_UI_DARK, LEONOS_UI_WHITE);

    leonos_ui_button(ui, 8, TOOLBAR_Y, 54, LEONOS_UI_BUTTON_H, "Up", 0);
    leonos_ui_button(ui, 72, TOOLBAR_Y, 60, LEONOS_UI_BUTTON_H, "Open", 0);
    leonos_ui_button(ui, 142, TOOLBAR_Y, 76, LEONOS_UI_BUTTON_H, "Refresh", 0);
    leonos_ui_text_field(ui, 230, TOOLBAR_Y, FILEMAN_W - 238, current_path, 0);

    leonos_ui_list_header(ui, LIST_X, LIST_Y, LIST_W, "Type Name");
    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t pos = 0;
        line[0] = 0;
        append_text(line, &pos, sizeof(line), entry_type_name(&entries[i]));
        append_text(line, &pos, sizeof(line), " ");
        append_text(line, &pos, sizeof(line), entries[i].name);
        leonos_ui_list_row(ui, LIST_X, LIST_Y + 28 + i * ROW_H, LIST_W, line,
                           selected_index == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }

    leonos_ui_panel(ui, 8, FILEMAN_H - 34, FILEMAN_W - 16, 22, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 12, FILEMAN_H - 31, status_text, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
}

static void open_selected_entry(void)
{
    char path[LEONOS_FS_PATH_LEN];
    int pid;
    if (selected_index < 0 || (uint32_t)selected_index >= entry_count) {
        set_status("Select an item");
        return;
    }
    build_child_path(path, sizeof(path), entries[selected_index].name);
    if (entries[selected_index].type == LEONOS_FS_TYPE_DIR) {
        copy_text(current_path, sizeof(current_path), path);
        reload_dir();
        return;
    }
    if (!ends_with(entries[selected_index].name, ".elf")) {
        set_status("Open only supports directories and ELF programs");
        return;
    }
    pid = execve(path, 0, 0);
    if (pid < 0) {
        set_status_code("Launch failed ", pid);
    } else {
        char buf[96];
        uint32_t pos = 0;
        buf[0] = 0;
        append_text(buf, &pos, sizeof(buf), "Launched pid ");
        append_dec(buf, &pos, sizeof(buf), (uint32_t)pid);
        append_text(buf, &pos, sizeof(buf), " from ");
        append_text(buf, &pos, sizeof(buf), entries[selected_index].name);
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
    reload_dir();
}

static void handle_click(int32_t x, int32_t y)
{
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
    if (x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y + 28) {
        selected_index = -1;
        return;
    }
    uint32_t row = (uint32_t)(y - (LIST_Y + 28)) / ROW_H;
    if (row >= entry_count) {
        selected_index = -1;
        return;
    }
    if (selected_index == (int32_t)row) {
        open_selected_entry();
    } else {
        selected_index = (int32_t)row;
        set_status(entries[row].name);
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    puts("[fileman.elf] file manager starting");
    printf("[fileman.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window("File Manager", "LeonOS file browser", FILEMAN_W, FILEMAN_H);
    if (window_id <= 0) {
        printf("[fileman.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, FILEMAN_W, FILEMAN_H, FILEMAN_W);
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
