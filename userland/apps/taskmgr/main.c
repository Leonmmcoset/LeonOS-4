#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TASKMGR_W 620
#define TASKMGR_H 300
#define TASKMGR_MAX_W 1264
#define TASKMGR_MAX_H 746
#define TASKMGR_DETAILS_W 430
#define TASKMGR_DETAILS_H 270
#define TASKMGR_STATUS_H 28
#define TASKMGR_VISIBLE_ROWS 7
#define TASKMGR_MENU_BAR_H 28
#define TASKMGR_MENU_ITEM_H (LEONOS_FONT_H + 8)
#define TASKMGR_CONTEXT_MENU_W 140
#define TASKMGR_CONTEXT_MENU_COUNT 3
#define TASKMGR_KEY_ESCAPE 1U
#define LEONOS_KEY_DELETE 83U
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    TASKMGR_ACTION_END = 1,
    TASKMGR_ACTION_DETAILS = 2,
    TASKMGR_ACTION_REFRESH = 3,
};

enum {
    TASKMGR_MENU_NONE = 0,
    TASKMGR_MENU_FILE = 1,
    TASKMGR_MENU_OPTIONS = 2,
};

static uint32_t pixels[TASKMGR_MAX_W * TASKMGR_MAX_H];
static uint32_t details_pixels[TASKMGR_DETAILS_W * TASKMGR_DETAILS_H];
static struct leonos_task_info tasks[LEONOS_TASK_MAX];
static uint32_t task_count;
static uint64_t task_tick;
static struct leonos_ui_listview_state task_list;
static uint8_t menu_open;
static uint8_t context_menu_active;
static uint8_t context_menu_animating;
static uint8_t context_menu_opening;
static unsigned long context_menu_anim_start;
static uint32_t context_menu_x;
static uint32_t context_menu_y;
static uint32_t view_w = TASKMGR_W;
static uint32_t view_h = TASKMGR_H;
static char status_text[96] = "Ready";

static uint32_t visible_rows(void)
{
    uint32_t h = view_h > 102 + TASKMGR_STATUS_H + 8 ? view_h - 102 - TASKMGR_STATUS_H - 8 : 24;
    uint32_t rows = h / 24;
    return rows ? rows : 1;
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
        set_status(T("No task selected", "未选择任务"));
        return;
    }
    if (!selected_task_killable()) {
        set_status(T("Cannot end protected or non-user task", "无法结束受保护或非用户任务"));
        return;
    }
    pid = task->pid;
    if (leonos_task_kill(pid) < 0) {
        set_status(T("End Task failed", "结束任务失败"));
        return;
    }
    set_status(T("Task ended", "任务已结束"));
    refresh_tasks();
}

static void build_context_menu_items(struct leonos_ui_context_menu_item *items)
{
    items[0] = (struct leonos_ui_context_menu_item){T("End Task", "结束任务"), TASKMGR_ACTION_END,
                                                    selected_task_killable() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){T("Details", "详细信息"), TASKMGR_ACTION_DETAILS,
                                                    selected_task() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){T("Refresh", "刷新"), TASKMGR_ACTION_REFRESH, 0};
}

static void details_add_line(struct leonos_ui_surface *ui, uint32_t y,
                             const char *label, const char *value)
{
    leonos_ui_text(ui, 18, y, label, LEONOS_UI_BLACK, LEONOS_UI_GRAY);
    leonos_ui_text_clipped(ui, 112, y, TASKMGR_DETAILS_W - 130,
                           value ? value : "", LEONOS_UI_BLACK, LEONOS_UI_GRAY);
}

static void show_task_details(void)
{
    struct leonos_task_info *task = selected_task();
    struct leonos_task_info snapshot;
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    char pid[24];
    char ppid[24];
    char cr3[24];
    char entry[24];
    char wake[24];
    uint32_t pos;
    int window_id;
    if (!task) {
        set_status(T("No task selected", "未选择任务"));
        return;
    }
    snapshot = *task;
    pos = 0;
    pid[0] = 0;
    append_dec(pid, &pos, sizeof(pid), snapshot.pid);
    pos = 0;
    ppid[0] = 0;
    append_dec(ppid, &pos, sizeof(ppid), snapshot.parent_pid);
    pos = 0;
    cr3[0] = 0;
    append_hex_fixed(cr3, &pos, sizeof(cr3), snapshot.cr3, 12);
    pos = 0;
    entry[0] = 0;
    append_hex_fixed(entry, &pos, sizeof(entry), snapshot.entry, 12);
    pos = 0;
    wake[0] = 0;
    append_dec(wake, &pos, sizeof(wake), snapshot.wake_tick);

    window_id = leonos_gui_create_app_window_ex(T("Task Details", "任务详细信息"), snapshot.name,
                                                TASKMGR_DETAILS_W, TASKMGR_DETAILS_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        set_status(T("Details failed", "详细信息打开失败"));
        return;
    }
    leonos_ui_bind(&ui, details_pixels, TASKMGR_DETAILS_W, TASKMGR_DETAILS_H,
                   TASKMGR_DETAILS_W);
    for (;;) {
        leonos_ui_rect(&ui, 0, 0, TASKMGR_DETAILS_W, TASKMGR_DETAILS_H,
                       LEONOS_UI_GRAY);
        leonos_ui_dialog(&ui, 0, 0, TASKMGR_DETAILS_W, TASKMGR_DETAILS_H,
                         T("Task Details", "任务详细信息"));
        details_add_line(&ui, 44, T("Name:", "名称:"), snapshot.name);
        details_add_line(&ui, 66, "PID:", pid);
        details_add_line(&ui, 88, T("Parent PID:", "父 PID:"), ppid);
        details_add_line(&ui, 110, T("State:", "状态:"), state_name(snapshot.state));
        details_add_line(&ui, 132, T("Kind:", "类型:"), kind_name(snapshot.kind));
        details_add_line(&ui, 154, "CR3:", cr3);
        details_add_line(&ui, 176, "Entry:", entry);
        details_add_line(&ui, 198, T("Wake tick:", "唤醒 tick:"), wake);
        leonos_ui_button(&ui, TASKMGR_DETAILS_W - 90, TASKMGR_DETAILS_H - 38,
                         72, LEONOS_UI_BUTTON_H, "OK", 0);
        leonos_gui_present_window((uint32_t)window_id, TASKMGR_DETAILS_W,
                                  TASKMGR_DETAILS_H, TASKMGR_DETAILS_W,
                                  details_pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN &&
                (event.keycode == LEONOS_KEY_ENTER || event.keycode == TASKMGR_KEY_ESCAPE)) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u) &&
                hit_rect_i(event.x, event.y, TASKMGR_DETAILS_W - 90,
                           TASKMGR_DETAILS_H - 38, 72,
                           (int32_t)LEONOS_UI_BUTTON_H)) {
                break;
            }
        } else {
            sleep_ms(10);
        }
    }
    leonos_gui_destroy_app_window((uint32_t)window_id);
}

static void execute_context_action(uint32_t action)
{
    context_menu_set_active(0);
    if (action == TASKMGR_ACTION_END) {
        kill_selected_task();
    } else if (action == TASKMGR_ACTION_DETAILS) {
        show_task_details();
    } else if (action == TASKMGR_ACTION_REFRESH) {
        refresh_tasks();
    }
}

static void draw_taskmgr(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    uint32_t rows;
    uint32_t list_w = view_w > 38 ? view_w - 38 : 320;
    uint32_t list_h = view_h > 72 + TASKMGR_STATUS_H + 4 ? view_h - 72 - TASKMGR_STATUS_H - 4 : 80;
    uint32_t vis_rows = visible_rows();
    struct leonos_ui_list_column cols[] = {
        {"PID", 44},
        {"PPID", 50},
        {"STATE", 58},
        {"KIND", 52},
        {"CR3", 104},
        {"WAKE", 70},
        {"NAME", list_w > 44 + 50 + 58 + 52 + 104 + 70 ?
                     list_w - 44 - 50 - 58 - 52 - 104 - 70 : 80},
    };
    task_list.visible_rows = vis_rows;
    leonos_ui_listview_state_set_count(&task_list, task_count);

    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_menubar(ui, 0, 0, view_w);
    leonos_ui_menubar_item(ui, 8, 0, 64, T("File", "文件"), menu_open == TASKMGR_MENU_FILE);
    leonos_ui_menubar_item(ui, 74, 0, 80, T("Options", "选项"), menu_open == TASKMGR_MENU_OPTIONS);
    leonos_ui_toolbar(ui, 0, 28, view_w, 36);
    leonos_ui_toolbar_button(ui, 8, 34, 88, T("Refresh", "刷新"), 0);
    leonos_ui_toolbar_button(ui, 104, 34, 94, T("Processes", "进程"), LEONOS_UI_BUTTON_PRESSED);
    leonos_ui_toolbar_button(ui, 206, 34, 86, T("End Task", "结束任务"),
                             selected_task_killable() ? 0 : LEONOS_UI_BUTTON_DISABLED);

    line[0] = 0;
    append_text(line, &pos, sizeof(line), "tick=");
    append_dec(line, &pos, sizeof(line), task_tick);
    append_text(line, &pos, sizeof(line), " tasks=");
    append_dec(line, &pos, sizeof(line), task_count);
    leonos_ui_text(ui, 306, 40, line, LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    leonos_ui_scroll_view_frame(ui, 8, 72, view_w - 16, list_h);
    leonos_ui_listview_header(ui, 10, 74, list_w, cols, 7);
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
        leonos_ui_listview_row(ui, 10, 102 + row * 24, list_w, cols, cells, 7,
                               task_list.selected == (int32_t)i ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, view_w - 26, 74, 18, view_h > 104 ? view_h - 104 : 24,
                         task_list.scroll, task_count > vis_rows ? task_count : vis_rows,
                         vis_rows,
                         task_count <= vis_rows ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    leonos_ui_statusbar(ui, view_h - TASKMGR_STATUS_H, TASKMGR_STATUS_H, status_text);

    if (menu_open == TASKMGR_MENU_FILE) {
        leonos_ui_menu(ui, 8, TASKMGR_MENU_BAR_H, 154, 86);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 8, 116, T("Refresh", "刷新"), 0);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 34, 116, T("End Task", "结束任务"),
                            selected_task_killable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
        leonos_ui_menu_item(ui, 42, TASKMGR_MENU_BAR_H + 60, 116, T("About", "关于"), 0);
    } else if (menu_open == TASKMGR_MENU_OPTIONS) {
        leonos_ui_menu(ui, 74, TASKMGR_MENU_BAR_H, 178, 60);
        leonos_ui_menu_item(ui, 108, TASKMGR_MENU_BAR_H + 8, 140, T("Processes", "进程"), LEONOS_UI_MENU_SELECTED);
        leonos_ui_menu_item(ui, 108, TASKMGR_MENU_BAR_H + 34, 140, T("About", "关于"), 0);
    }
    if (context_menu_active || context_menu_animating) {
        struct leonos_ui_context_menu_item items[TASKMGR_CONTEXT_MENU_COUNT];
        uint32_t progress = context_menu_animating
                                ? leonos_ui_anim_progress(leonos_uptime_ms(), context_menu_anim_start, 120)
                                : 1000;
        if (progress >= 1000) {
            context_menu_animating = 0;
            progress = context_menu_active ? 1000 : 0;
        } else if (!context_menu_opening) {
            progress = 1000 - progress;
        }
        build_context_menu_items(items);
        leonos_ui_context_menu_animated(ui, context_menu_x, context_menu_y,
                                        TASKMGR_CONTEXT_MENU_W, items,
                                        TASKMGR_CONTEXT_MENU_COUNT, progress);
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
            leonos_ui_show_message_box(T("Task Manager", "任务管理器"), T("Live task snapshot from the scheduler.", "来自调度器的实时任务快照。"), "OK");
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
            leonos_ui_show_message_box(T("Task Manager", "任务管理器"), T("Shows runnable, sleeping, and exited tasks.", "显示可运行、睡眠和已退出任务。"), "OK");
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    return 0;
}

static int handle_context_menu_click(int32_t x, int32_t y)
{
    struct leonos_ui_context_menu_item items[TASKMGR_CONTEXT_MENU_COUNT];
    uint32_t action = 0;
    if (!context_menu_active) {
        return 0;
    }
    build_context_menu_items(items);
    if (leonos_ui_context_menu_hit(x, y, context_menu_x, context_menu_y,
                                   TASKMGR_CONTEXT_MENU_W, items,
                                   TASKMGR_CONTEXT_MENU_COUNT, &action)) {
        if (action) {
            execute_context_action(action);
        } else {
            context_menu_set_active(0);
        }
        return 1;
    }
    context_menu_set_active(0);
    return 0;
}

static void show_context_menu_at(int32_t x, int32_t y)
{
    uint32_t menu_h = leonos_ui_context_menu_height(TASKMGR_CONTEXT_MENU_COUNT);
    menu_open = TASKMGR_MENU_NONE;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    context_menu_x = (uint32_t)x;
    context_menu_y = (uint32_t)y;
    if (context_menu_x + TASKMGR_CONTEXT_MENU_W > view_w) {
        context_menu_x = view_w > TASKMGR_CONTEXT_MENU_W ? view_w - TASKMGR_CONTEXT_MENU_W : 0;
    }
    if (context_menu_y + menu_h > view_h - TASKMGR_STATUS_H) {
        context_menu_y = view_h - TASKMGR_STATUS_H > menu_h
                             ? view_h - TASKMGR_STATUS_H - menu_h
                             : 0;
    }
    context_menu_set_active(1);
}

static void present_taskmgr(uint32_t window_id, struct leonos_ui_surface *ui)
{
    leonos_ui_bind(ui, pixels, view_w, view_h, TASKMGR_MAX_W);
    draw_taskmgr(ui);
    leonos_gui_present_window(window_id, view_w, view_h, TASKMGR_MAX_W, pixels);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    unsigned long last_refresh = 0;
    int window_id;

    puts("[taskmgr.elf] task manager starting");
    printf("[taskmgr.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window_ex(T("Task Manager", "任务管理器"), T("Task snapshot", "任务快照"),
                                                TASKMGR_W, TASKMGR_H, 0);
    if (window_id <= 0) {
        printf("[taskmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, view_w, view_h, TASKMGR_MAX_W);
    leonos_ui_listview_state_init(&task_list, visible_rows(), 24);
    task_list.focused = 1;
    for (;;) {
        unsigned long now = leonos_uptime_ms();
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                if (event.buttons & 2u) {
                    uint32_t activate = 0;
                    leonos_ui_listview_state_handle_mouse(&task_list, event.x, event.y,
                                                          10, 102, view_w > 38 ? view_w - 38 : 320,
                                                          &activate);
                    show_context_menu_at(event.x, event.y);
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (handle_context_menu_click(event.x, event.y)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (handle_menu_click(event.x, event.y)) {
                    context_menu_set_active(0);
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                menu_open = TASKMGR_MENU_NONE;
                context_menu_set_active(0);
                if (hit_rect_i(event.x, event.y, 206, 34, 86, LEONOS_UI_BUTTON_H)) {
                    kill_selected_task();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (event.x >= (int32_t)(view_w - 26) && event.y >= 74 &&
                    event.y < (int32_t)(view_h - TASKMGR_STATUS_H)) {
                    leonos_ui_vscrollbar_handle_mouse(&task_list.scroll,
                                                      task_count > visible_rows() ? task_count : visible_rows(),
                                                      visible_rows(),
                                                      view_w - 26, 74, 18, view_h > 104 ? view_h - 104 : 24,
                                                      event.x, event.y);
                } else {
                    uint32_t activate = 0;
                    leonos_ui_listview_state_handle_mouse(&task_list, event.x, event.y,
                                                          10, 102, view_w > 38 ? view_w - 38 : 320, &activate);
                    (void)activate;
                }
                task_list.focused = 1;
                present_taskmgr((uint32_t)window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (leonos_ui_listview_state_handle_wheel(&task_list, event.dy)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                menu_open = TASKMGR_MENU_NONE;
                context_menu_set_active(0);
                uint32_t activate = 0;
                if (event.keycode == LEONOS_KEY_DELETE) {
                    kill_selected_task();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (leonos_ui_listview_state_handle_key(&task_list, event.keycode, &activate)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE || event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                if (event.width >= 360) {
                    view_w = event.width > TASKMGR_MAX_W ? TASKMGR_MAX_W : event.width;
                }
                if (event.height >= 220) {
                    view_h = event.height > TASKMGR_MAX_H ? TASKMGR_MAX_H : event.height;
                }
                task_list.visible_rows = visible_rows();
                leonos_ui_listview_state_set_count(&task_list, task_count);
                present_taskmgr((uint32_t)window_id, &ui);
            }
            event.window_id = (uint32_t)window_id;
        }
        if (now - last_refresh >= 500) {
            refresh_tasks();
            present_taskmgr((uint32_t)window_id, &ui);
            last_refresh = now;
        } else if (context_menu_animating) {
            present_taskmgr((uint32_t)window_id, &ui);
        }
        sleep_ms(20);
    }
}
