#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TASKMGR_W 620
#define TASKMGR_H 300
#define TASKMGR_STATUS_H 28
#define TASKMGR_VISIBLE_ROWS 7
#define TASKMGR_MENU_BAR_H 28
#define TASKMGR_MENU_ITEM_H (LEONOS_FONT_H + 8)
#define LEONOS_KEY_DELETE 83U

enum {
    TASKMGR_MENU_NONE = 0,
    TASKMGR_MENU_FILE = 1,
    TASKMGR_MENU_OPTIONS = 2,
};

static uint32_t pixels[TASKMGR_W * TASKMGR_H];
static struct leonos_task_info tasks[LEONOS_TASK_MAX];
static uint32_t task_count;
static uint64_t task_tick;
static struct leonos_ui_listview_state task_list;
static uint8_t menu_open;
static char status_text[96] = "Ready";

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

static void append_dec(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[20];
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

static void append_hex_fixed(char *buf, uint32_t *pos, uint32_t cap, uint64_t value, uint32_t digits)
{
    const char *hex = "0123456789abcdef";
    append_text(buf, pos, cap, "0x");
    for (int32_t shift = (int32_t)(digits * 4); shift > 0; shift -= 4) {
        append_char(buf, pos, cap, hex[(value >> (uint32_t)(shift - 4)) & 0xf]);
    }
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static const char *state_name(uint32_t state)
{
    switch (state) {
    case 0:
        return "ready";
    case 1:
        return "run";
    case 2:
        return "sleep";
    case 3:
        return "exit";
    default:
        return "?";
    }
}

static const char *kind_name(uint32_t kind)
{
    return kind == 1 ? "user" : "kern";
}

static void task_line(char *buf, uint32_t cap, const struct leonos_task_info *task)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, task->pid);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->parent_pid);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, state_name(task->state));
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, kind_name(task->kind));
    append_text(buf, &pos, cap, "  ");
    append_hex_fixed(buf, &pos, cap, task->cr3, 8);
    append_text(buf, &pos, cap, "  ");
    append_dec(buf, &pos, cap, task->wake_tick);
    append_text(buf, &pos, cap, "  ");
    append_text(buf, &pos, cap, task->name);
}

static void refresh_tasks(void)
{
    int count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &task_tick);
    task_count = count > 0 ? (uint32_t)count : 0;
    leonos_ui_listview_state_set_count(&task_list, task_count);
    if (task_list.selected < 0 && task_count) {
        task_list.selected = 0;
    }
}

static void set_status(const char *text)
{
    uint32_t i = 0;
    while (text && text[i] && i + 1 < sizeof(status_text)) {
        status_text[i] = text[i];
        ++i;
    }
    status_text[i] = 0;
}

static struct leonos_task_info *selected_task(void)
{
    if (task_list.selected < 0 || (uint32_t)task_list.selected >= task_count) {
        return 0;
    }
    return &tasks[task_list.selected];
}

static int selected_task_killable(void)
{
    struct leonos_task_info *task = selected_task();
    if (!task || task->pid == 0 || task->kind != 1 || task->state == 3 ||
        (task->flags & 1u) || task->pid == (uint32_t)getpid()) {
        return 0;
    }
    return 1;
}

static void kill_selected_task(void)
{
    struct leonos_task_info *task = selected_task();
    uint32_t pid;
    if (!task) {
        set_status("No task selected");
        return;
    }
    if (!selected_task_killable()) {
        set_status("Cannot end protected or non-user task");
        return;
    }
    pid = task->pid;
    if (leonos_task_kill(pid) < 0) {
        set_status("End Task failed");
        return;
    }
    set_status("Task ended");
    refresh_tasks();
}

static void draw_taskmgr(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    uint32_t rows;
    struct leonos_ui_list_column cols[] = {
        {"PID", 44},
        {"PPID", 50},
        {"STATE", 58},
        {"KIND", 52},
        {"CR3", 104},
        {"WAKE", 70},
        {"NAME", TASKMGR_W - 16 - 44 - 50 - 58 - 52 - 104 - 70},
    };

    leonos_ui_rect(ui, 0, 0, TASKMGR_W, TASKMGR_H, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, TASKMGR_W);
    leonos_ui_menubar_item(ui, 8, 0, 64, "File", menu_open == TASKMGR_MENU_FILE);
    leonos_ui_menubar_item(ui, 74, 0, 80, "Options", menu_open == TASKMGR_MENU_OPTIONS);
    leonos_ui_toolbar(ui, 0, 28, TASKMGR_W, 36);
    leonos_ui_toolbar_button(ui, 8, 34, 88, "Refresh", 0);
    leonos_ui_toolbar_button(ui, 104, 34, 94, "Processes", LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_toolbar_button(ui, 206, 34, 86, "End Task",
                             selected_task_killable() ? 0 : LEONOS_UI_BUTTON_DISABLED);

    line[0] = 0;
    append_text(line, &pos, sizeof(line), "tick=");
    append_dec(line, &pos, sizeof(line), task_tick);
    append_text(line, &pos, sizeof(line), " tasks=");
    append_dec(line, &pos, sizeof(line), task_count);
    leonos_ui_text(ui, 306, 40, line, LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    leonos_ui_scroll_view_frame(ui, 8, 72, TASKMGR_W - 16, TASKMGR_H - 72 - TASKMGR_STATUS_H - 4);
    leonos_ui_listview_header(ui, 10, 74, TASKMGR_W - 38, cols, 7);
    rows = task_count > task_list.visible_rows ? task_list.visible_rows : task_count;
    for (uint32_t row = 0; row < rows; ++row) {
        uint32_t i = task_list.scroll + row;
        char pid[16];
        char ppid[16];
        char cr3[24];
        char wake[24];
        uint32_t p = 0;
        const char *cells[7];
        if (i >= task_count) {
            break;
        }
        pid[0] = 0;
        append_dec(pid, &p, sizeof(pid), tasks[i].pid);
        p = 0;
        ppid[0] = 0;
        append_dec(ppid, &p, sizeof(ppid), tasks[i].parent_pid);
        p = 0;
        cr3[0] = 0;
        append_hex_fixed(cr3, &p, sizeof(cr3), tasks[i].cr3, 8);
        p = 0;
        wake[0] = 0;
        append_dec(wake, &p, sizeof(wake), tasks[i].wake_tick);
        cells[0] = pid;
        cells[1] = ppid;
        cells[2] = state_name(tasks[i].state);
        cells[3] = kind_name(tasks[i].kind);
        cells[4] = cr3;
        cells[5] = wake;
        cells[6] = tasks[i].name;
        task_line(line, sizeof(line), &tasks[i]);
        leonos_ui_listview_row(ui, 10, 102 + row * 24, TASKMGR_W - 38, cols, cells, 7,
                               task_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, TASKMGR_W - 26, 74, 18, TASKMGR_H - 104,
                         task_list.scroll, task_count > TASKMGR_VISIBLE_ROWS ? task_count : TASKMGR_VISIBLE_ROWS,
                         TASKMGR_VISIBLE_ROWS,
                         task_count <= TASKMGR_VISIBLE_ROWS ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_statusbar(ui, TASKMGR_H - TASKMGR_STATUS_H, TASKMGR_STATUS_H, status_text);

    if (menu_open == TASKMGR_MENU_FILE) {
        leonos_ui_menu(ui, 8, TASKMGR_MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 8, 116, "Refresh", 0);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 34, 116, "End Task",
                            selected_task_killable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 60, 116, "About", 0);
    } else if (menu_open == TASKMGR_MENU_OPTIONS) {
        leonos_ui_menu(ui, 74, TASKMGR_MENU_BAR_H, 178, 60);
        leonos_ui_menu_item(ui, 108, TASKMGR_MENU_BAR_H + 8, 140, "Processes", LEONOS_UI_MENU_SELECTED);
        leonos_ui_menu_item(ui, 108, TASKMGR_MENU_BAR_H + 34, 140, "About", 0);
    }
}

static int handle_menu_click(int32_t x, int32_t y)
{
    if (y >= 0 && y < (int32_t)TASKMGR_MENU_BAR_H) {
        if (hit_rect_i(x, y, 8, 0, 64, (int32_t)TASKMGR_MENU_BAR_H)) {
            menu_open = menu_open == TASKMGR_MENU_FILE ? TASKMGR_MENU_NONE : TASKMGR_MENU_FILE;
            return 1;
        }
        if (hit_rect_i(x, y, 74, 0, 80, (int32_t)TASKMGR_MENU_BAR_H)) {
            menu_open = menu_open == TASKMGR_MENU_OPTIONS ? TASKMGR_MENU_NONE : TASKMGR_MENU_OPTIONS;
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    if (menu_open == TASKMGR_MENU_FILE) {
        if (hit_rect_i(x, y, 42, (int32_t)TASKMGR_MENU_BAR_H + 8, 116,
                       (int32_t)TASKMGR_MENU_ITEM_H)) {
            menu_open = TASKMGR_MENU_NONE;
            refresh_tasks();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)TASKMGR_MENU_BAR_H + 34, 116,
                       (int32_t)TASKMGR_MENU_ITEM_H)) {
            menu_open = TASKMGR_MENU_NONE;
            kill_selected_task();
            return 1;
        }
        if (hit_rect_i(x, y, 42, (int32_t)TASKMGR_MENU_BAR_H + 60, 116,
                       (int32_t)TASKMGR_MENU_ITEM_H)) {
            menu_open = TASKMGR_MENU_NONE;
            leonos_ui_show_message_box("Task Manager", "Live task snapshot from the scheduler.", "OK");
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    if (menu_open == TASKMGR_MENU_OPTIONS) {
        if (hit_rect_i(x, y, 108, (int32_t)TASKMGR_MENU_BAR_H + 8, 140,
                       (int32_t)TASKMGR_MENU_ITEM_H)) {
            menu_open = TASKMGR_MENU_NONE;
            refresh_tasks();
            return 1;
        }
        if (hit_rect_i(x, y, 108, (int32_t)TASKMGR_MENU_BAR_H + 34, 140,
                       (int32_t)TASKMGR_MENU_ITEM_H)) {
            menu_open = TASKMGR_MENU_NONE;
            leonos_ui_show_message_box("Task Manager", "Shows runnable, sleeping, and exited tasks.", "OK");
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    return 0;
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    unsigned long last_refresh = 0;
    int window_id;

    puts("[taskmgr.elf] task manager starting");
    printf("[taskmgr.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window_ex("Task Manager", "Task snapshot",
                                                TASKMGR_W, TASKMGR_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[taskmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, TASKMGR_W, TASKMGR_H, TASKMGR_W);
    leonos_ui_listview_state_init(&task_list, TASKMGR_VISIBLE_ROWS, 24);
    task_list.focused = 1;
    for (;;) {
        unsigned long now = leonos_uptime_ms();
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                if (handle_menu_click(event.x, event.y)) {
                    draw_taskmgr(&ui);
                    leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
                    continue;
                }
                menu_open = TASKMGR_MENU_NONE;
                if (hit_rect_i(event.x, event.y, 206, 34, 86, LEONOS_UI_BUTTON_H)) {
                    kill_selected_task();
                    draw_taskmgr(&ui);
                    leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
                    continue;
                }
                if (event.x >= (int32_t)(TASKMGR_W - 26) && event.y >= 74 &&
                    event.y < (int32_t)(TASKMGR_H - TASKMGR_STATUS_H)) {
                    leonos_ui_vscrollbar_handle_mouse(&task_list.scroll,
                                                      task_count > TASKMGR_VISIBLE_ROWS ? task_count : TASKMGR_VISIBLE_ROWS,
                                                      TASKMGR_VISIBLE_ROWS,
                                                      TASKMGR_W - 26, 74, 18, TASKMGR_H - 104,
                                                      event.x, event.y);
                } else {
                    uint32_t activate = 0;
                    leonos_ui_listview_state_handle_mouse(&task_list, event.x, event.y,
                                                          10, 102, TASKMGR_W - 38, &activate);
                    (void)activate;
                }
                task_list.focused = 1;
                draw_taskmgr(&ui);
                leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                menu_open = TASKMGR_MENU_NONE;
                uint32_t activate = 0;
                if (event.keycode == LEONOS_KEY_DELETE) {
                    kill_selected_task();
                    draw_taskmgr(&ui);
                    leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
                    continue;
                }
                if (leonos_ui_listview_state_handle_key(&task_list, event.keycode, &activate)) {
                    draw_taskmgr(&ui);
                    leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
                }
            }
            event.window_id = (uint32_t)window_id;
        }
        if (now - last_refresh >= 500) {
            refresh_tasks();
            draw_taskmgr(&ui);
            leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
            last_refresh = now;
        }
        sleep_ms(20);
    }
}
