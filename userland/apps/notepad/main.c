#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define NOTEPAD_W 720
#define NOTEPAD_H 460
#define NOTEPAD_TEXT_CAP 32768
#define STATUS_CAP 128
#define PATH_CAP LEONOS_FS_PATH_LEN
#define HEADER_X 10
#define HEADER_Y 38
#define PATH_Y 56
#define VIEW_X 10
#define VIEW_Y 82
#define VIEW_W (NOTEPAD_W - 38)
#define VIEW_H (NOTEPAD_H - 120)
#define STATUS_H 28
#define MENU_BAR_H 28
#define MENU_ITEM_H (LEONOS_FONT_H + 8)
#define UNTITLED_NAME "Untitled"

enum {
    NOTEPAD_MENU_NONE = 0,
    NOTEPAD_MENU_FILE = 1,
    NOTEPAD_MENU_EDIT = 2,
    NOTEPAD_MENU_VIEW = 3,
};

static uint32_t pixels[NOTEPAD_W * NOTEPAD_H];
static char file_path[PATH_CAP] = UNTITLED_NAME;
static char status_text[STATUS_CAP] = "Open a text file from File Manager or Run";
static char text_data[NOTEPAD_TEXT_CAP];
static uint8_t truncated;
static uint8_t menu_open;
static uint8_t document_dirty;
static uint32_t saved_hash;
static struct leonos_ui_text_area_state document;

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

static void append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (!buf || !pos || *pos + 1 >= cap) {
        return;
    }
    buf[*pos] = ch;
    ++(*pos);
    buf[*pos] = 0;
}

static void append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(buf, pos, cap, *text++);
    }
}

static void append_u32(char *buf, uint32_t *pos, uint32_t cap, uint32_t value)
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

static int text_equals(const char *a, const char *b)
{
    uint32_t i = 0;
    for (;;) {
        char ca = a ? a[i] : 0;
        char cb = b ? b[i] : 0;
        if (ca != cb) {
            return 0;
        }
        if (ca == 0) {
            return 1;
        }
        ++i;
    }
}

static int is_untitled_path(const char *path)
{
    return !path || !path[0] || text_equals(path, UNTITLED_NAME);
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static uint32_t visible_rows(void)
{
    return VIEW_H / LEONOS_FONT_H;
}

static uint32_t document_hash(void)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < document.length; ++i) {
        hash ^= (uint8_t)text_data[i];
        hash *= 16777619u;
    }
    hash ^= document.length;
    hash *= 16777619u;
    return hash;
}

static void rebuild_status(void)
{
    uint32_t pos = 0;
    leonos_ui_text_area_state_sync(&document, VIEW_W);
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), "Lines ");
    append_u32(status_text, &pos, sizeof(status_text), document.line_count);
    append_text(status_text, &pos, sizeof(status_text), "  Bytes ");
    append_u32(status_text, &pos, sizeof(status_text), document.length);
    if (document_dirty) {
        append_text(status_text, &pos, sizeof(status_text), "  Modified");
    }
    if (truncated) {
        append_text(status_text, &pos, sizeof(status_text), "  Truncated");
    }
    if (document.length == 0) {
        append_text(status_text, &pos, sizeof(status_text), "  Empty");
    }
}

static void clamp_scroll(void)
{
    uint32_t rows = visible_rows();
    leonos_ui_text_area_state_sync(&document, VIEW_W);
    if (rows == 0) {
        document.scroll_line = 0;
        return;
    }
    if (document.line_count <= rows) {
        document.scroll_line = 0;
        return;
    }
    if (document.scroll_line + rows > document.line_count) {
        document.scroll_line = document.line_count - rows;
    }
}

static void set_error_status(const char *prefix, int value)
{
    uint32_t pos = 0;
    status_text[0] = 0;
    append_text(status_text, &pos, sizeof(status_text), prefix);
    if (value < 0) {
        append_char(status_text, &pos, sizeof(status_text), '-');
        value = -value;
    }
    append_u32(status_text, &pos, sizeof(status_text), (uint32_t)value);
}

static void mark_document_clean(void)
{
    saved_hash = document_hash();
    document_dirty = 0;
    rebuild_status();
}

static void refresh_document_dirty(void)
{
    document_dirty = document_hash() != saved_hash;
    rebuild_status();
}

static void clear_document_contents(void)
{
    document.length = 0;
    document.cursor = 0;
    document.scroll_line = 0;
    text_data[0] = 0;
    truncated = 0;
    leonos_ui_text_area_state_sync(&document, VIEW_W);
}

static void begin_new_document(void)
{
    copy_text(file_path, sizeof(file_path), UNTITLED_NAME);
    clear_document_contents();
    mark_document_clean();
}

static int load_document(const char *path)
{
    struct leonos_stat st;
    int fd;
    int ret;
    clear_document_contents();
    copy_text(file_path, sizeof(file_path), path && path[0] ? path : UNTITLED_NAME);
    if (!path || !path[0]) {
        mark_document_clean();
        return 0;
    }
    ret = stat(path, &st);
    if (ret < 0) {
        set_error_status("stat failed ", ret);
        saved_hash = document_hash();
        document_dirty = 0;
        return ret;
    }
    if (st.type != LEONOS_FS_TYPE_FILE) {
        copy_text(status_text, sizeof(status_text), "Selected path is not a file");
        saved_hash = document_hash();
        document_dirty = 0;
        return -1;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        set_error_status("open failed ", fd);
        saved_hash = document_hash();
        document_dirty = 0;
        return fd;
    }
    for (;;) {
        long got;
        uint32_t free_bytes = sizeof(text_data) - document.length - 1;
        if (free_bytes == 0) {
            truncated = 1;
            break;
        }
        got = read(fd, text_data + document.length, free_bytes);
        if (got < 0) {
            close(fd);
            set_error_status("read failed ", (int)got);
            saved_hash = document_hash();
            document_dirty = 0;
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        document.length += (uint32_t)got;
        text_data[document.length] = 0;
        if ((uint32_t)got == free_bytes) {
            truncated = 1;
            break;
        }
    }
    close(fd);
    document.cursor = 0;
    document.scroll_line = 0;
    leonos_ui_text_area_state_sync(&document, VIEW_W);
    clamp_scroll();
    mark_document_clean();
    printf("[notepad.elf] open path=%s bytes=%d lines=%d truncated=%d\n",
           file_path, (int)document.length, (int)document.line_count, (int)truncated);
    return 0;
}

static int save_document_to_path(const char *path)
{
    int fd;
    uint32_t flags;
    long wrote;
    if (!path || !path[0]) {
        return 0;
    }
    flags = LEONOS_O_WRONLY | LEONOS_O_CREAT;
    if (!document.length) {
        flags |= LEONOS_O_TRUNC;
    }
    fd = open(path, flags, 0);
    if (fd < 0) {
        set_error_status("save open failed ", fd);
        return 0;
    }
    wrote = 0;
    if (document.length) {
        wrote = write(fd, text_data, document.length);
        if (wrote < 0) {
            close(fd);
            set_error_status("save write failed ", (int)wrote);
            return 0;
        }
        if ((uint32_t)wrote != document.length) {
            close(fd);
            copy_text(status_text, sizeof(status_text), "save write incomplete");
            return 0;
        }
    }
    close(fd);
    copy_text(file_path, sizeof(file_path), path);
    mark_document_clean();
    printf("[notepad.elf] saved path=%s bytes=%d\n", file_path, (int)document.length);
    return 1;
}

static int save_document_as(void)
{
    char path[PATH_CAP];
    if (is_untitled_path(file_path)) {
        path[0] = 0;
    } else {
        copy_text(path, sizeof(path), file_path);
    }
    if (leonos_ui_show_save_dialog_ex("Save As", path, sizeof(path),
                                      "Text files (*.txt)",
                                      ".txt") <= 0) {
        return 0;
    }
    if (!path[0]) {
        return 0;
    }
    return save_document_to_path(path);
}

static int open_document_via_dialog(void)
{
    char path[PATH_CAP];
    path[0] = 0;
    if (leonos_ui_show_open_dialog("Open", path, sizeof(path),
                                   "Text files (*.txt)",
                                   ".txt") <= 0) {
        return 0;
    }
    if (!path[0]) {
        return 0;
    }
    return load_document(path) == 0;
}

static int save_document(void)
{
    if (is_untitled_path(file_path)) {
        return save_document_as();
    }
    return save_document_to_path(file_path);
}

static int confirm_dirty_action(const char *message)
{
    if (!document_dirty) {
        return 1;
    }
    if (!leonos_ui_show_confirm_dialog("Notepad", message, 1)) {
        return 1;
    }
    return save_document() && !document_dirty;
}

static void draw_notepad(struct leonos_ui_surface *ui)
{
    uint32_t rows = visible_rows();
    char path_label[PATH_CAP + 4];
    uint32_t path_pos = 0;
    leonos_ui_text_area_state_sync(&document, VIEW_W);
    path_label[0] = 0;
    append_text(path_label, &path_pos, sizeof(path_label), file_path);
    if (document_dirty) {
        append_text(path_label, &path_pos, sizeof(path_label), " *");
    }
    leonos_ui_rect(ui, 0, 0, NOTEPAD_W, NOTEPAD_H, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, NOTEPAD_W);
    leonos_ui_menubar_item(ui, 8, 0, 54, "File", menu_open == NOTEPAD_MENU_FILE);
    leonos_ui_menubar_item(ui, 64, 0, 54, "Edit", menu_open == NOTEPAD_MENU_EDIT);
    leonos_ui_menubar_item(ui, 120, 0, 54, "View", menu_open == NOTEPAD_MENU_VIEW);
    leonos_ui_toolbar(ui, 0, 28, NOTEPAD_W, 26);
    leonos_ui_toolbar_button(ui, 8, 30, 64, "Open", 0);
    leonos_ui_toolbar_button(ui, 78, 30, 64, "Save",
                             document_dirty ? LEONOS_UI_TOOLBAR_BUTTON_ACTIVE : 0);
    leonos_ui_text_clipped(ui, 154, 36, NOTEPAD_W - 164, path_label, LEONOS_UI_DARK, LEONOS_UI_GRAY);
    leonos_ui_text_area_state_draw(ui, VIEW_X, VIEW_Y, VIEW_W, VIEW_H, &document, 0);
    leonos_ui_vscrollbar(ui, NOTEPAD_W - 26, VIEW_Y, 18, VIEW_H, document.scroll_line,
                         document.line_count > 0 ? document.line_count : 1, rows,
                         document.line_count <= rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_statusbar(ui, NOTEPAD_H - STATUS_H, STATUS_H, status_text);

    if (menu_open == NOTEPAD_MENU_FILE) {
        leonos_ui_menu(ui, 8, MENU_BAR_H, 154, 112);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 8, 116, "Open", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 34, 116, "New", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 60, 116, "Save", 0);
        leonos_ui_menu_item(ui, 42, MENU_BAR_H + 86, 116, "Save As", 0);
    } else if (menu_open == NOTEPAD_MENU_EDIT) {
        leonos_ui_menu(ui, 64, MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 8, 116, "Clear", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 34, 116, "Home", 0);
        leonos_ui_menu_item(ui, 98, MENU_BAR_H + 60, 116, "End", 0);
    } else if (menu_open == NOTEPAD_MENU_VIEW) {
        leonos_ui_menu(ui, 120, MENU_BAR_H, 154, 60);
        leonos_ui_menu_item(ui, 154, MENU_BAR_H + 8, 116, "Top", 0);
        leonos_ui_menu_item(ui, 154, MENU_BAR_H + 34, 116, "About", 0);
    }
}

static int handle_toolbar_click(int32_t x, int32_t y)
{
    if (hit_rect_i(x, y, 78, 30, 64, (int32_t)LEONOS_UI_BUTTON_H)) {
        menu_open = NOTEPAD_MENU_NONE;
        save_document();
        return 1;
    }
    if (hit_rect_i(x, y, 8, 30, 64, (int32_t)LEONOS_UI_BUTTON_H)) {
        menu_open = NOTEPAD_MENU_NONE;
        open_document_via_dialog();
        return 1;
    }
    return 0;
}

static int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_FILE ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 64, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_EDIT ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_EDIT;
            return 1;
        }
        if (hit_rect_i(x, y, 120, 0, 54, (int32_t)MENU_BAR_H)) {
            menu_open = menu_open == NOTEPAD_MENU_VIEW ? NOTEPAD_MENU_NONE : NOTEPAD_MENU_VIEW;
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (hit_rect_i(x, y, 78, 30, 64, (int32_t)LEONOS_UI_BUTTON_H)) {
        return handle_toolbar_click(x, y);
    }
    if (menu_open == NOTEPAD_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (open_document_via_dialog()) {
                rebuild_status();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (confirm_dirty_action("Save changes before creating a new file?")) {
                begin_new_document();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            save_document();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)MENU_BAR_H + 86, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            save_document_as();
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (menu_open == NOTEPAD_MENU_EDIT) {
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            if (confirm_dirty_action("Save changes before clearing this file?")) {
                clear_document_contents();
                refresh_document_dirty();
            }
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.cursor = 0;
            document.scroll_line = 0;
            rebuild_status();
            return 1;
        }
        if (hit_rect_i(x, y, 98, (int32_t)MENU_BAR_H + 60, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.cursor = document.length;
            clamp_scroll();
            rebuild_status();
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    if (menu_open == NOTEPAD_MENU_VIEW) {
        if (hit_rect_i(x, y, 154, (int32_t)MENU_BAR_H + 8, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            document.scroll_line = 0;
            return 1;
        }
        if (hit_rect_i(x, y, 154, (int32_t)MENU_BAR_H + 34, 116, (int32_t)MENU_ITEM_H)) {
            menu_open = NOTEPAD_MENU_NONE;
            leonos_ui_show_message_box("Notepad", "Open files from File Manager or Run.", "OK");
            return 1;
        }
        menu_open = NOTEPAD_MENU_NONE;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    (void)envp;

    puts("[notepad.elf] notepad starting");
    window_id = leonos_gui_create_app_window_ex("Notepad", "LeonOS text viewer",
                                                NOTEPAD_W, NOTEPAD_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[notepad.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W);
    leonos_ui_text_area_state_init(&document, text_data, sizeof(text_data));
    document.focused = 1;
    document.readonly = 0;
    begin_new_document();
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        load_document(argv[1]);
    }
    draw_notepad(&ui);
    leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                if (confirm_dirty_action("Save changes before closing?")) {
                    return 0;
                }
                draw_notepad(&ui);
                leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON) {
                if (event.buttons & 1u) {
                    if (handle_menu_click(event.x, event.y)) {
                        draw_notepad(&ui);
                        leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
                        continue;
                    }
                    menu_open = NOTEPAD_MENU_NONE;
                    if (handle_toolbar_click(event.x, event.y)) {
                        draw_notepad(&ui);
                        leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
                        continue;
                    }
                    {
                        uint32_t before = document.scroll_line;
                        if (event.x >= (int32_t)(NOTEPAD_W - 26) && event.y >= VIEW_Y &&
                            event.y < (int32_t)(VIEW_Y + VIEW_H)) {
                            leonos_ui_vscrollbar_handle_mouse(&document.scroll_line,
                                                              document.line_count, visible_rows(),
                                                              NOTEPAD_W - 26, VIEW_Y, 18, VIEW_H,
                                                              event.x, event.y);
                        } else {
                            leonos_ui_text_area_state_handle_mouse(&document, event.x, event.y,
                                                                   VIEW_X, VIEW_Y, VIEW_W, VIEW_H,
                                                                   event.buttons);
                        }
                        if (before != document.scroll_line || document.focused) {
                            draw_notepad(&ui);
                            leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
                        }
                    }
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                uint32_t before_hash = document_hash();
                menu_open = NOTEPAD_MENU_NONE;
                if (leonos_ui_text_area_state_handle_key(&document, event.keycode, event.pressed, VIEW_W, VIEW_H)) {
                    clamp_scroll();
                    if (before_hash != document_hash()) {
                        refresh_document_dirty();
                    } else {
                        rebuild_status();
                    }
                    draw_notepad(&ui);
                    leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
                }
                continue;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE || event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_notepad(&ui);
                leonos_gui_present_window((uint32_t)window_id, NOTEPAD_W, NOTEPAD_H, NOTEPAD_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
}
