#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/auth.h>
#include <leonos/psf_font.h>
#include <leonos/startup.h>
#include <leonos/stdio.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TASKMGR_W 720
#define TASKMGR_H 560
#define TASKMGR_MAX_W LEONOS_GUI_MAX_WINDOW_WIDTH
#define TASKMGR_MAX_H LEONOS_GUI_MAX_WINDOW_HEIGHT
#define TASKMGR_DETAILS_W 430
#define TASKMGR_DETAILS_H 380
#define TASKMGR_STATUS_H 28
#define TASKMGR_MENU_BAR_H 28
#define TASKMGR_MENU_ITEM_H (LEONOS_FONT_H + 8)
#define TASKMGR_CONTEXT_MENU_W 140
#define TASKMGR_CONTEXT_MENU_COUNT 3
#define TASKMGR_STARTUP_USER_ROW_H 24U
#define TASKMGR_PERF_HISTORY 120U
#define TASKMGR_KEY_ESCAPE 1U
#define LEONOS_KEY_DELETE 83U
#define T(en, zh) leonos_i18n((en), (zh))

enum {
    TASKMGR_ACTION_END = 1,
    TASKMGR_ACTION_DETAILS = 2,
    TASKMGR_ACTION_REFRESH = 3,
    TASKMGR_ACTION_ABOUT = 4,
    TASKMGR_ACTION_PROCESSES = 5,
    TASKMGR_ACTION_PERFORMANCE = 6,
    TASKMGR_ACTION_STARTUP = 7,
};

enum {
    TASKMGR_MENU_NONE = 0,
    TASKMGR_MENU_FILE = 1,
    TASKMGR_MENU_OPTIONS = 2,
};

enum {
    TASKMGR_TAB_PROCESSES = 0,
    TASKMGR_TAB_PERFORMANCE = 1,
    TASKMGR_TAB_STARTUP = 2,
};

static uint32_t pixels[TASKMGR_MAX_W * TASKMGR_MAX_H];
static uint32_t details_pixels[TASKMGR_DETAILS_W * TASKMGR_DETAILS_H];
static struct leonos_task_info tasks[LEONOS_TASK_MAX];
static struct leonos_task_info previous_tasks[LEONOS_TASK_MAX];
static struct leonos_ui_treeview_item process_tree_items[LEONOS_TASK_MAX];
static const char *process_tree_cells[LEONOS_TASK_MAX][7];
static char process_tree_pid[LEONOS_TASK_MAX][16];
static char process_tree_cpu[LEONOS_TASK_MAX][16];
static char process_tree_memory[LEONOS_TASK_MAX][24];
static uint32_t task_count;
static uint32_t previous_task_count;
static uint64_t task_tick;
static uint64_t previous_task_tick;
static uint32_t task_cpu_percent[LEONOS_TASK_MAX];
static uint32_t previous_task_cpu_percent[LEONOS_TASK_MAX];
static struct leonos_perf_info perf_info;
static uint64_t last_busy_ticks;
static uint64_t last_idle_ticks;
static uint64_t last_cpu_busy_ticks[LEONOS_PERF_MAX_CPUS];
static uint64_t last_cpu_idle_ticks[LEONOS_PERF_MAX_CPUS];
static uint32_t cpu_percent_by_core[LEONOS_PERF_MAX_CPUS];
static uint8_t cpu_snapshot_valid;
static uint32_t cpu_percent;
static uint32_t mem_percent;
static uint8_t perf_valid;
static uint8_t perf_cpu_history[TASKMGR_PERF_HISTORY];
static uint8_t perf_mem_history[TASKMGR_PERF_HISTORY];
static uint32_t perf_history_head;
static uint32_t perf_history_count;
static struct leonos_ui_treeview_state process_tree;
static struct leonos_startup_entry startup_entries[LEONOS_STARTUP_MAX_ENTRIES];
static uint32_t startup_entry_count;
static struct leonos_ui_listview_state startup_list;
static struct leonos_user_info startup_users[LEONOS_AUTH_MAX_USERS];
static uint32_t startup_user_count;
static uint32_t startup_selected_uid;
static uint8_t startup_user_dropdown_open;
static uint32_t startup_user_dropdown_scroll;
static uint8_t active_tab = TASKMGR_TAB_PROCESSES;
static struct leonos_ui_tab_state taskmgr_tabs;
static uint8_t menu_open;
static uint8_t context_menu_active;
static uint8_t context_menu_animating;
static uint8_t context_menu_opening;
static unsigned long context_menu_anim_start;
static uint32_t context_menu_x;
static uint32_t context_menu_y;

static void taskmgr_tab_items(struct leonos_ui_tab_item items[3])
{
    items[0] = (struct leonos_ui_tab_item){T("Processes", "进程"), TASKMGR_TAB_PROCESSES, 0};
    items[1] = (struct leonos_ui_tab_item){T("Performance", "性能"), TASKMGR_TAB_PERFORMANCE, 0};
    items[2] = (struct leonos_ui_tab_item){T("Startup Apps", "启动应用"), TASKMGR_TAB_STARTUP, 0};
}
static uint32_t view_w = TASKMGR_W;
static uint32_t view_h = TASKMGR_H;
static char status_text[96] = "Ready";

static void perf_history_push(uint32_t cpu, uint32_t memory)
{
    if (cpu > 100U) {
        cpu = 100U;
    }
    if (memory > 100U) {
        memory = 100U;
    }
    perf_cpu_history[perf_history_head] = (uint8_t)cpu;
    perf_mem_history[perf_history_head] = (uint8_t)memory;
    perf_history_head = (perf_history_head + 1U) % TASKMGR_PERF_HISTORY;
    if (perf_history_count < TASKMGR_PERF_HISTORY) {
        ++perf_history_count;
    }
}

static uint32_t visible_rows(void)
{
    uint32_t h = view_h > 102 + TASKMGR_STATUS_H + 8 ? view_h - 102 - TASKMGR_STATUS_H - 8 : 24;
    uint32_t rows = h / 24;
    return rows ? rows : 1;
}

static uint32_t startup_visible_rows(void)
{
    uint32_t h = view_h > 142 + TASKMGR_STATUS_H + 8 ?
                     view_h - 142 - TASKMGR_STATUS_H - 8 : 24;
    uint32_t rows = h / 24;
    return rows ? rows : 1;
}

static uint32_t toolbar_tab_width(void)
{
    uint32_t available = view_w > 104 ? view_w - 104 : 0;
    return available > 340 ? 340 : available;
}

static uint32_t toolbar_action_x(void)
{
    return 104 + toolbar_tab_width() + 8;
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

static const char *task_user_name(const struct leonos_task_info *task)
{
    if (task && task->username[0]) {
        return task->username;
    }
    return task && task->uid ? T("Unknown", "未知") : T("System", "系统");
}

static const char *task_privilege_name(const struct leonos_task_info *task)
{
    if (!task || !task->uid) {
        return T("System", "系统");
    }
    if (task->flags & LEONOS_TASK_SNAPSHOT_FLAG_ELEVATED_ADMIN) {
        return T("Elevated", "已提升");
    }
    if (task->role == LEONOS_AUTH_ROLE_ADMIN) {
        return T("Admin", "管理员");
    }
    return T("Standard", "标准");
}

static int task_index_by_pid(const struct leonos_task_info *list,
                             uint32_t count, uint32_t pid)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

static void format_process_cpu(char *buf, uint32_t cap, uint32_t percent)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, percent);
    append_text(buf, &pos, cap, "%");
}

static void format_process_memory(char *buf, uint32_t cap, uint32_t kib)
{
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    uint64_t bytes = (uint64_t)kib * 1024ULL;
    uint64_t unit_size = 1ULL;
    uint64_t whole;
    uint64_t fraction;
    uint32_t unit = 0;
    uint32_t pos = 0;

    while (unit + 1U < sizeof(units) / sizeof(units[0]) &&
           bytes >= unit_size * 1024ULL) {
        unit_size *= 1024ULL;
        ++unit;
    }
    whole = bytes / unit_size;
    /* Keep one useful decimal place for small non-integral values without
     * making the process list jump in width on every refresh. */
    fraction = ((bytes % unit_size) * 10ULL + unit_size / 2ULL) / unit_size;
    if (fraction == 10ULL) {
        ++whole;
        fraction = 0;
    }

    buf[0] = 0;
    append_dec(buf, &pos, cap, whole);
    if (unit != 0 && whole < 10ULL && fraction != 0) {
        append_char(buf, &pos, cap, '.');
        append_dec(buf, &pos, cap, fraction);
    }
    append_char(buf, &pos, cap, ' ');
    append_text(buf, &pos, cap, units[unit]);
}

static void rebuild_process_tree_items(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        uint32_t pos = 0;
        process_tree_pid[i][0] = 0;
        append_dec(process_tree_pid[i], &pos, sizeof(process_tree_pid[i]), tasks[i].pid);
        format_process_cpu(process_tree_cpu[i], sizeof(process_tree_cpu[i]), task_cpu_percent[i]);
        format_process_memory(process_tree_memory[i], sizeof(process_tree_memory[i]),
                              tasks[i].memory_kib);
        process_tree_cells[i][0] = tasks[i].name;
        process_tree_cells[i][1] = process_tree_pid[i];
        process_tree_cells[i][2] = process_tree_cpu[i];
        process_tree_cells[i][3] = process_tree_memory[i];
        process_tree_cells[i][4] = state_name(tasks[i].state);
        process_tree_cells[i][5] = task_user_name(&tasks[i]);
        process_tree_cells[i][6] = task_privilege_name(&tasks[i]);
        process_tree_items[i].id = tasks[i].pid;
        process_tree_items[i].parent_id = tasks[i].parent_pid;
        process_tree_items[i].cells = process_tree_cells[i];
        process_tree_items[i].flags = 0;
    }
    leonos_ui_treeview_state_sync(&process_tree, process_tree_items, task_count);
}

static void refresh_tasks(void)
{
    uint64_t next_tick = 0;
    uint64_t tick_delta = 0;
    uint64_t sample_total;
    int count;
    count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &next_tick);
    task_count = count > 0 ? (uint32_t)count : 0;
    /* cpu_ticks is charged by every CPU's local scheduling tick.  The BSP
     * wall-clock tick is not necessarily phase- or frequency-identical to
     * the AP LAPIC ticks, so `wall_ticks * cpu_count` can report a runnable
     * process as 0% on SMP.  Use the aggregate accounting denominator from
     * the same per-CPU sources that charged the task instead. */
    sample_total = perf_info.busy_ticks + perf_info.idle_ticks;
    if (previous_task_tick && sample_total > previous_task_tick) {
        tick_delta = sample_total - previous_task_tick;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        int previous = task_index_by_pid(previous_tasks, previous_task_count, tasks[i].pid);
        uint64_t used_ticks = 0;
        if (previous >= 0 && tasks[i].cpu_ticks >= previous_tasks[previous].cpu_ticks) {
            used_ticks = tasks[i].cpu_ticks - previous_tasks[previous].cpu_ticks;
        }
        if (tick_delta) {
            task_cpu_percent[i] = (uint32_t)((used_ticks * 100ULL) / tick_delta);
        } else if (previous >= 0) {
            task_cpu_percent[i] = previous_task_cpu_percent[previous];
        } else {
            task_cpu_percent[i] = 0;
        }
        if (task_cpu_percent[i] > 100U) {
            task_cpu_percent[i] = 100U;
        }
    }
    rebuild_process_tree_items();
    previous_task_count = task_count;
    previous_task_tick = sample_total;
    for (uint32_t i = 0; i < task_count; ++i) {
        previous_task_cpu_percent[i] = task_cpu_percent[i];
        previous_tasks[i] = tasks[i];
    }
    task_tick = sample_total;
}

static void refresh_startup_users(void)
{
    struct leonos_user_info current;
    uint32_t count = 0;
    current = (struct leonos_user_info){0};
    startup_user_count = 0;
    if (leonos_auth_current(&current) < 0 || !current.uid) {
        startup_selected_uid = 0;
        return;
    }
    if (current.role == LEONOS_AUTH_ROLE_ADMIN &&
        leonos_auth_list_users(startup_users, LEONOS_AUTH_MAX_USERS, 0, &count) == 0) {
        startup_user_count = count > LEONOS_AUTH_MAX_USERS ? LEONOS_AUTH_MAX_USERS : count;
    } else {
        startup_users[0] = current;
        startup_user_count = 1;
    }
    if (!startup_selected_uid) {
        startup_selected_uid = current.uid;
    }
    for (uint32_t i = 0; i < startup_user_count; ++i) {
        if (startup_users[i].uid == startup_selected_uid) {
            return;
        }
    }
    startup_selected_uid = startup_user_count ? startup_users[0].uid : 0;
}

static const char *startup_selected_username(void)
{
    for (uint32_t i = 0; i < startup_user_count; ++i) {
        if (startup_users[i].uid == startup_selected_uid) {
            return startup_users[i].username;
        }
    }
    return "";
}

static void refresh_startup_entries(void)
{
    uint32_t count = 0;
    if (!startup_selected_uid) {
        startup_entry_count = 0;
        return;
    }
    if (leonos_startup_list(startup_selected_uid, startup_entries,
                            LEONOS_STARTUP_MAX_ENTRIES, &count) < 0) {
        count = 0;
    }
    startup_entry_count = count > LEONOS_STARTUP_MAX_ENTRIES ?
                              LEONOS_STARTUP_MAX_ENTRIES : count;
    leonos_ui_listview_state_set_count(&startup_list, startup_entry_count);
    if (startup_list.selected < 0 && startup_entry_count) {
        startup_list.selected = 0;
    }
}

static void refresh_startup(void)
{
    refresh_startup_users();
    refresh_startup_entries();
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

static void refresh_performance(void)
{
    struct leonos_perf_info next;
    uint64_t old_total;
    uint64_t new_total;
    uint64_t delta_total;
    uint64_t delta_busy;
    uint32_t cpu_count;
    if (leonos_perf_info(&next) < 0) {
        perf_valid = 0;
        set_status(T("Performance data unavailable", "性能数据不可用"));
        return;
    }
    old_total = last_busy_ticks + last_idle_ticks;
    new_total = next.busy_ticks + next.idle_ticks;
    if (old_total && new_total > old_total) {
        delta_total = new_total - old_total;
        delta_busy = next.busy_ticks >= last_busy_ticks ? next.busy_ticks - last_busy_ticks : 0;
        cpu_percent = delta_total ? (uint32_t)((delta_busy * 100ULL) / delta_total) : 0;
    } else if (!cpu_snapshot_valid) {
        cpu_percent = new_total ? (uint32_t)((next.busy_ticks * 100ULL) / new_total) : 0;
    }
    if (cpu_percent > 100) {
        cpu_percent = 100;
    }
    if (next.total_memory_kib && next.total_memory_kib >= next.free_memory_kib) {
        uint64_t used = next.total_memory_kib - next.free_memory_kib;
        mem_percent = (uint32_t)((used * 100ULL) / next.total_memory_kib);
    } else {
        mem_percent = 0;
    }
    if (mem_percent > 100) {
        mem_percent = 100;
    }
    /* CPU and memory values are recorded from the same kernel snapshot so
     * both plots advance together on every performance refresh. */
    perf_history_push(cpu_percent, mem_percent);
    cpu_count = next.cpu_count;
    if (cpu_count > LEONOS_PERF_MAX_CPUS) {
        cpu_count = LEONOS_PERF_MAX_CPUS;
    }
    for (uint32_t i = 0; i < LEONOS_PERF_MAX_CPUS; ++i) {
        uint64_t old_cpu_total = last_cpu_busy_ticks[i] + last_cpu_idle_ticks[i];
        uint64_t new_cpu_total = next.cpus[i].busy_ticks + next.cpus[i].idle_ticks;
        uint64_t cpu_delta_total;
        uint64_t cpu_delta_busy;
        if (i >= cpu_count || !next.cpus[i].online) {
            cpu_percent_by_core[i] = 0;
        } else if (cpu_snapshot_valid && old_cpu_total && new_cpu_total > old_cpu_total) {
            cpu_delta_total = new_cpu_total - old_cpu_total;
            cpu_delta_busy = next.cpus[i].busy_ticks >= last_cpu_busy_ticks[i]
                                 ? next.cpus[i].busy_ticks - last_cpu_busy_ticks[i] : 0;
            cpu_percent_by_core[i] = cpu_delta_total
                                         ? (uint32_t)((cpu_delta_busy * 100ULL) / cpu_delta_total)
                                         : 0;
        } else if (!cpu_snapshot_valid) {
            cpu_percent_by_core[i] = new_cpu_total
                                         ? (uint32_t)((next.cpus[i].busy_ticks * 100ULL) /
                                                      new_cpu_total)
                                         : 0;
        }
        if (cpu_percent_by_core[i] > 100) {
            cpu_percent_by_core[i] = 100;
        }
        last_cpu_busy_ticks[i] = next.cpus[i].busy_ticks;
        last_cpu_idle_ticks[i] = next.cpus[i].idle_ticks;
    }
    perf_info = next;
    last_busy_ticks = next.busy_ticks;
    last_idle_ticks = next.idle_ticks;
    cpu_snapshot_valid = 1;
    perf_valid = 1;
}

static void refresh_all(void)
{
    refresh_performance();
    refresh_tasks();
    if (active_tab == TASKMGR_TAB_STARTUP) {
        refresh_startup();
    }
}

static struct leonos_task_info *selected_task(void)
{
    int index;
    if (active_tab != TASKMGR_TAB_PROCESSES) {
        return 0;
    }
    if (!process_tree.has_selection) {
        return 0;
    }
    index = task_index_by_pid(tasks, task_count, process_tree.selected_id);
    return index >= 0 ? &tasks[index] : 0;
}

static struct leonos_startup_entry *selected_startup_entry(void)
{
    if (active_tab != TASKMGR_TAB_STARTUP || startup_list.selected < 0 ||
        (uint32_t)startup_list.selected >= startup_entry_count) {
        return 0;
    }
    return &startup_entries[startup_list.selected];
}

static void toggle_selected_startup_entry(void)
{
    struct leonos_startup_entry *entry = selected_startup_entry();
    if (!entry) {
        set_status(T("No startup app selected", "未选择启动应用"));
        return;
    }
    if (leonos_startup_set_enabled(startup_selected_uid, entry->id, !entry->enabled) < 0) {
        set_status(T("Could not change startup app", "无法更改启动应用"));
        return;
    }
    set_status(entry->enabled ? T("Startup app disabled", "启动应用已禁用")
                              : T("Startup app enabled", "启动应用已启用"));
    refresh_startup_entries();
}

static void remove_selected_startup_entry(void)
{
    struct leonos_startup_entry *entry = selected_startup_entry();
    if (!entry) {
        set_status(T("No startup app selected", "未选择启动应用"));
        return;
    }
    if (!leonos_ui_show_confirm_dialog(T("Remove Startup App", "删除启动应用"),
                                       T("Remove the selected startup app?", "删除选中的启动应用？"), 0)) {
        return;
    }
    if (leonos_startup_remove(startup_selected_uid, entry->id) < 0) {
        set_status(T("Could not remove startup app", "无法删除启动应用"));
        return;
    }
    set_status(T("Startup app removed", "启动应用已删除"));
    refresh_startup_entries();
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
    refresh_all();
}

static void build_context_menu_items(struct leonos_ui_context_menu_item *items)
{
    items[0] = (struct leonos_ui_context_menu_item){T("End Task", "结束任务"), TASKMGR_ACTION_END,
                                                    selected_task_killable() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[1] = (struct leonos_ui_context_menu_item){T("Details", "详细信息"), TASKMGR_ACTION_DETAILS,
                                                    selected_task() ? 0 : LEONOS_UI_MENU_DISABLED};
    items[2] = (struct leonos_ui_context_menu_item){T("Refresh", "刷新"), TASKMGR_ACTION_REFRESH, 0};
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
    char cpu_ticks[24];
    char memory[24];
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
    pos = 0;
    cpu_ticks[0] = 0;
    append_dec(cpu_ticks, &pos, sizeof(cpu_ticks), snapshot.cpu_ticks);
    append_text(cpu_ticks, &pos, sizeof(cpu_ticks), " ticks");
    format_process_memory(memory, sizeof(memory), snapshot.memory_kib);

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
        struct leonos_ui_property_item props[] = {
            {T("Name:", "名称:"), snapshot.name, 0},
            {"PID:", pid, 0},
            {T("Parent PID:", "父 PID:"), ppid, 0},
            {T("State:", "状态:"), state_name(snapshot.state), 0},
            {T("Kind:", "类型:"), kind_name(snapshot.kind), 0},
            {T("User:", "用户:"), task_user_name(&snapshot), 0},
            {T("Privileges:", "权限:"), task_privilege_name(&snapshot), 0},
            {T("CPU time:", "CPU 时间:"), cpu_ticks, 0},
            {T("Memory:", "内存:"), memory, 0},
            {"CR3:", cr3, 0},
            {"Entry:", entry, 0},
            {T("Wake tick:", "唤醒 tick:"), wake, 0},
        };
        leonos_ui_rect(&ui, 0, 0, TASKMGR_DETAILS_W, TASKMGR_DETAILS_H,
                       LEONOS_UI_GRAY);
        leonos_ui_property_grid(&ui, 16, 16, TASKMGR_DETAILS_W - 32,
                                props, sizeof(props) / sizeof(props[0]),
                                110, 23);
        leonos_ui_button(&ui, TASKMGR_DETAILS_W - 90, TASKMGR_DETAILS_H - 38,
                         72, LEONOS_UI_BUTTON_H, "OK", 0);
        leonos_gui_present_window((uint32_t)window_id, TASKMGR_DETAILS_W,
                                  TASKMGR_DETAILS_H, TASKMGR_DETAILS_W,
                                  details_pixels);
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, LEONOS_GUI_IDLE_WAIT_MS) > 0) {
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
        refresh_all();
    }
}

static void format_percent(char *buf, uint32_t cap, uint32_t value)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, value);
    append_text(buf, &pos, cap, "%");
}

static void format_kib(char *buf, uint32_t cap, uint64_t kib)
{
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, kib);
    append_text(buf, &pos, cap, " KiB");
}

static void format_uptime(char *buf, uint32_t cap, uint64_t ms)
{
    uint64_t seconds = ms / 1000ULL;
    uint64_t hours = seconds / 3600ULL;
    uint64_t minutes = (seconds / 60ULL) % 60ULL;
    uint64_t secs = seconds % 60ULL;
    uint32_t pos = 0;
    buf[0] = 0;
    append_dec(buf, &pos, cap, hours);
    append_text(buf, &pos, cap, ":");
    if (minutes < 10) {
        append_text(buf, &pos, cap, "0");
    }
    append_dec(buf, &pos, cap, minutes);
    append_text(buf, &pos, cap, ":");
    if (secs < 10) {
        append_text(buf, &pos, cap, "0");
    }
    append_dec(buf, &pos, cap, secs);
}

static void draw_perf_text_line(struct leonos_ui_surface *ui, uint32_t x, uint32_t y,
                                const char *label, const char *value)
{
    leonos_ui_text(ui, x, y, label, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, x + 150, y, view_w > x + 174 ? view_w - x - 174 : 80,
                           value ? value : "", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
}

static void draw_perf_segment(struct leonos_ui_surface *ui, int32_t x0, int32_t y0,
                              int32_t x1, int32_t y1, uint32_t color)
{
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            leonos_ui_pixel(ui, (uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            int32_t twice = error * 2;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }
}

static void draw_perf_graph(struct leonos_ui_surface *ui, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, const char *title,
                            const uint8_t *history, uint32_t color)
{
    uint32_t plot_x;
    uint32_t plot_y;
    uint32_t plot_w;
    uint32_t plot_h;
    uint32_t grid_color = leonos_ui_color(LEONOS_UI_COLOR_BORDER);
    uint32_t muted_color = leonos_ui_color(LEONOS_UI_COLOR_MUTED);
    uint32_t count;
    if (w < 72U || h < 48U) {
        return;
    }
    leonos_ui_text_clipped(ui, x + 8U, y + 6U, w - 16U, title,
                           LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    plot_x = x + 8U;
    plot_y = y + 26U;
    plot_w = w - 16U;
    plot_h = h - 34U;
    leonos_ui_inset(ui, plot_x, plot_y, plot_w, plot_h, LEONOS_UI_WHITE);
    if (plot_w < 4U || plot_h < 4U) {
        return;
    }
    for (uint32_t step = 1U; step < 4U; ++step) {
        uint32_t gy = plot_y + ((plot_h - 1U) * step) / 4U;
        leonos_ui_rect(ui, plot_x + 1U, gy, plot_w - 2U, 1U, grid_color);
    }
    leonos_ui_text_clipped(ui, plot_x + 3U, plot_y + 2U, 28U, "100%",
                           muted_color, LEONOS_UI_WHITE);
    leonos_ui_text_clipped(ui, plot_x + 3U,
                           plot_y + plot_h > 14U ? plot_y + plot_h - 14U : plot_y,
                           28U, "0%", muted_color, LEONOS_UI_WHITE);

    count = perf_history_count;
    if (!count || !history) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t history_index = (perf_history_head + TASKMGR_PERF_HISTORY - count + i) %
                                  TASKMGR_PERF_HISTORY;
        /* Use the complete history width as the time axis.  While the ring
         * fills, samples enter at the right; after it fills, advancing the
         * head shifts every point one slot to the left like Task Manager. */
        uint32_t slot = TASKMGR_PERF_HISTORY - count + i;
        uint32_t px = plot_x + 1U +
                      (slot * (plot_w - 3U)) / (TASKMGR_PERF_HISTORY - 1U);
        uint32_t value = history[history_index] > 100U ? 100U : history[history_index];
        uint32_t py = plot_y + plot_h - 2U -
                      (value * (plot_h - 3U)) / 100U;
        if (i) {
            uint32_t previous_index = (perf_history_head + TASKMGR_PERF_HISTORY - count + i - 1U) %
                                       TASKMGR_PERF_HISTORY;
            uint32_t previous_value = history[previous_index] > 100U ? 100U : history[previous_index];
            uint32_t previous_slot = TASKMGR_PERF_HISTORY - count + i - 1U;
            uint32_t previous_x = plot_x + 1U +
                                  (previous_slot * (plot_w - 3U)) /
                                      (TASKMGR_PERF_HISTORY - 1U);
            uint32_t previous_y = plot_y + plot_h - 2U -
                                  (previous_value * (plot_h - 3U)) / 100U;
            draw_perf_segment(ui, (int32_t)previous_x, (int32_t)previous_y,
                              (int32_t)px, (int32_t)py, color);
        }
        leonos_ui_pixel(ui, px, py, color);
    }
}

static void draw_performance(struct leonos_ui_surface *ui)
{
    char value[64];
    char value2[64];
    char core_label[24];
    uint32_t y = 80U;
    uint32_t content_w = view_w > 40U ? view_w - 40U : 1U;
    uint32_t graph_gap = 12U;
    uint32_t graph_w;
    uint32_t graph_h;
    uint32_t graph_bottom;
    uint32_t cpu_count = perf_info.cpu_count;
    uint32_t columns;
    uint32_t rows;
    uint32_t cpu_start_y;
    uint64_t used_kib = perf_info.total_memory_kib >= perf_info.free_memory_kib
                            ? perf_info.total_memory_kib - perf_info.free_memory_kib
                            : 0;
    leonos_ui_panel(ui, 8, 72, view_w > 16 ? view_w - 16 : view_w,
                    view_h > 112 ? view_h - 108 : 96, LEONOS_UI_WHITE);
    if (!perf_valid) {
        leonos_ui_text(ui, 24, y + 20, T("Performance data unavailable", "性能数据不可用"),
                       LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        return;
    }

    /* Keep the plots large enough to read, but let short/narrow windows use a
     * compact stacked layout instead of drawing outside the client surface. */
    graph_w = content_w > graph_gap ? (content_w - graph_gap) / 2U : content_w;
    if (graph_w < 190U) {
        graph_w = content_w;
        if (view_h > 480U) {
            graph_h = 110U;
        } else if (view_h > 320U) {
            graph_h = 80U;
        } else {
            uint32_t available = view_h > 150U + TASKMGR_STATUS_H
                                     ? view_h - 150U - TASKMGR_STATUS_H : 88U;
            graph_h = available / 2U > 44U ? available / 2U : 44U;
        }
        draw_perf_graph(ui, 20U, y, graph_w, graph_h,
                        T("CPU Usage", "CPU 占用"), perf_cpu_history,
                        leonos_ui_color(LEONOS_UI_COLOR_ACCENT));
        draw_perf_graph(ui, 20U, y + graph_h + 8U, graph_w, graph_h,
                        T("Memory Usage", "内存占用"), perf_mem_history,
                        leonos_ui_color(LEONOS_UI_COLOR_TEXT));
        graph_bottom = y + graph_h * 2U + 8U;
    } else {
        graph_h = view_h > 700U ? 160U : (view_h > 420U ? 120U : 96U);
        draw_perf_graph(ui, 20U, y, graph_w, graph_h,
                        T("CPU Usage", "CPU 占用"), perf_cpu_history,
                        leonos_ui_color(LEONOS_UI_COLOR_ACCENT));
        draw_perf_graph(ui, 20U + graph_w + graph_gap, y, graph_w, graph_h,
                        T("Memory Usage", "内存占用"), perf_mem_history,
                        leonos_ui_color(LEONOS_UI_COLOR_TEXT));
        graph_bottom = y + graph_h;
    }

    y = graph_bottom + 10U;
    leonos_ui_text(ui, 24, y, T("Current", "当前"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    format_percent(value, sizeof(value), cpu_percent);
    leonos_ui_text(ui, 94, y, T("CPU ", "CPU "), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 132, y, value, LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    format_percent(value, sizeof(value), mem_percent);
    leonos_ui_text(ui, 202, y, T("RAM ", "RAM "), LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 246, y, value, LEONOS_UI_BLACK, LEONOS_UI_WHITE);

    y += 30U;
    if (cpu_count > LEONOS_PERF_MAX_CPUS) {
        cpu_count = LEONOS_PERF_MAX_CPUS;
    }
    columns = cpu_count > 16U ? 4U : (cpu_count > 8U ? 3U : 2U);
    if (cpu_count == 1U) {
        columns = 1U;
    }
    if (content_w < 340U) {
        columns = 1U;
    }
    rows = cpu_count ? (cpu_count + columns - 1U) / columns : 1U;
    leonos_ui_text(ui, 24, y, T("Per-core usage", "每核占用"),
                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    cpu_start_y = y + 22U;
    {
        uint32_t available_w = content_w;
        uint32_t column_w = columns ? available_w / columns : available_w;
        for (uint32_t i = 0; i < cpu_count; ++i) {
            uint32_t column = i % columns;
            uint32_t row = i / columns;
            uint32_t x = 24U + column * column_w;
            uint32_t row_y = cpu_start_y + row * 30U;
            uint32_t progress_x = x + 48U;
            uint32_t progress_w = column_w > 104U ? column_w - 104U : 24U;
            uint32_t percent_x = x + column_w > 48U ? x + column_w - 48U : x;
            uint32_t pos = 0;
            core_label[0] = 0;
            append_text(core_label, &pos, sizeof(core_label), "CPU ");
            append_dec(core_label, &pos, sizeof(core_label), i);
            leonos_ui_text_clipped(ui, x, row_y + 2U, 46U, core_label,
                                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
            leonos_ui_progress(ui, progress_x, row_y, progress_w, 18U,
                               cpu_percent_by_core[i], 100U);
            format_percent(value, sizeof(value), cpu_percent_by_core[i]);
            leonos_ui_text_clipped(ui, percent_x, row_y + 2U, 46U, value,
                                   LEONOS_UI_BLACK, LEONOS_UI_WHITE);
        }
    }
    y = cpu_start_y + rows * 30U + 8U;

    format_kib(value, sizeof(value), perf_info.total_memory_kib);
    draw_perf_text_line(ui, 24U, y, T("Total memory:", "总内存:"), value);
    y += 22U;
    format_kib(value, sizeof(value), used_kib);
    draw_perf_text_line(ui, 24U, y, T("Used memory:", "已用内存:"), value);
    y += 22U;
    format_kib(value, sizeof(value), perf_info.free_memory_kib);
    draw_perf_text_line(ui, 24U, y, T("Free memory:", "可用内存:"), value);
    y += 22U;
    format_uptime(value, sizeof(value), perf_info.uptime_ms);
    draw_perf_text_line(ui, 24U, y, T("Uptime:", "运行时间:"), value);

    y += 30U;
    {
        uint32_t pos = 0U;
        value[0] = 0;
        append_text(value, &pos, sizeof(value), T("Tasks ", "任务 "));
        append_dec(value, &pos, sizeof(value), perf_info.task_count);
        append_text(value, &pos, sizeof(value), " / ");
        append_text(value, &pos, sizeof(value), T("Run ", "运行 "));
        append_dec(value, &pos, sizeof(value), perf_info.running_tasks);
    }
    {
        uint32_t pos = 0U;
        value2[0] = 0;
        append_text(value2, &pos, sizeof(value2), T("Ready ", "就绪 "));
        append_dec(value2, &pos, sizeof(value2), perf_info.ready_tasks);
        append_text(value2, &pos, sizeof(value2), " / ");
        append_text(value2, &pos, sizeof(value2), T("Sleep ", "睡眠 "));
        append_dec(value2, &pos, sizeof(value2), perf_info.sleeping_tasks);
    }
    draw_perf_text_line(ui, 24U, y, value, value2);
}

static uint32_t startup_dropdown_rows(void)
{
    uint32_t available = view_h > 160 + TASKMGR_STATUS_H ?
                             view_h - 160 - TASKMGR_STATUS_H : 1;
    uint32_t rows = available / TASKMGR_STARTUP_USER_ROW_H;
    if (rows == 0) {
        rows = 1;
    }
    return rows > 12 ? 12 : rows;
}

static void startup_command_line(char *text, uint32_t cap,
                                 const struct leonos_startup_command *command)
{
    uint32_t pos = 0;
    text[0] = 0;
    append_text(text, &pos, cap, command->path);
    for (uint32_t i = 0; i < command->argc; ++i) {
        append_text(text, &pos, cap, " ");
        append_text(text, &pos, cap, command->args[i]);
    }
}

static void draw_startup(struct leonos_ui_surface *ui)
{
    uint32_t list_w = view_w > 38 ? view_w - 38 : 320;
    uint32_t list_h = view_h > 112 + TASKMGR_STATUS_H + 4 ?
                          view_h - 112 - TASKMGR_STATUS_H - 4 : 80;
    uint32_t scroll_h = list_h > 2 ? list_h - 2 : 24;
    uint32_t rows = startup_entry_count > startup_list.visible_rows ?
                        startup_list.visible_rows : startup_entry_count;
    struct leonos_ui_list_column cols[] = {
        {T("STATUS", "状态"), 88},
        {T("COMMAND", "命令"), list_w > 88 ? list_w - 88 : 80},
    };

    startup_list.visible_rows = startup_visible_rows();
    leonos_ui_listview_state_set_count(&startup_list, startup_entry_count);
    leonos_ui_panel(ui, 8, 72, view_w > 16 ? view_w - 16 : view_w,
                    view_h > 112 ? view_h - 108 : 96, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 24, 84, T("User", "用户"), LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_combobox(ui, 70, 78, 180, startup_selected_username(),
                        startup_user_dropdown_open, 0);
    leonos_ui_scroll_view_frame(ui, 8, 112, view_w - 16, list_h);
    leonos_ui_listview_header(ui, 10, 114, list_w, cols, 2);
    for (uint32_t row = 0; row < rows; ++row) {
        uint32_t i = startup_list.scroll + row;
        const char *cells[2];
        char command[512];
        if (i >= startup_entry_count) {
            break;
        }
        cells[0] = startup_entries[i].enabled ? T("Enabled", "已启用") :
                                                T("Disabled", "已禁用");
        startup_command_line(command, sizeof(command), &startup_entries[i].command);
        cells[1] = command;
        leonos_ui_listview_row(ui, 10, 142 + row * 24, list_w, cols, cells, 2,
                               startup_list.selected == (int32_t)i
                                   ? LEONOS_UI_MENU_SELECTED : 0);
    }
    leonos_ui_vscrollbar(ui, view_w - 26, 114, 18, scroll_h,
                         startup_list.scroll,
                         startup_entry_count > startup_list.visible_rows
                             ? startup_entry_count : startup_list.visible_rows,
                         startup_list.visible_rows,
                         startup_entry_count <= startup_list.visible_rows
                             ? LEONOS_UI_SCROLLBAR_DISABLED : 0);

    if (startup_user_dropdown_open) {
        uint32_t visible = startup_dropdown_rows();
        uint32_t rows_to_draw = startup_user_count > startup_user_dropdown_scroll
                                    ? startup_user_count - startup_user_dropdown_scroll : 0;
        uint32_t height;
        if (rows_to_draw > visible) {
            rows_to_draw = visible;
        }
        height = rows_to_draw * TASKMGR_STARTUP_USER_ROW_H;
        leonos_ui_menu(ui, 70, 102, 180, height);
        for (uint32_t row = 0; row < rows_to_draw; ++row) {
            uint32_t i = startup_user_dropdown_scroll + row;
            leonos_ui_menu_item(ui, 72, 103 + row * TASKMGR_STARTUP_USER_ROW_H,
                                156, startup_users[i].username,
                                startup_users[i].uid == startup_selected_uid
                                    ? LEONOS_UI_MENU_SELECTED : 0);
        }
        if (startup_user_count > visible) {
            leonos_ui_vscrollbar(ui, 232, 102, 16, height,
                                 startup_user_dropdown_scroll, startup_user_count,
                                 visible, 0);
        }
    }
}

static void draw_taskmgr(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    uint32_t list_w = view_w > 38 ? view_w - 38 : 320;
    uint32_t list_h = view_h > 72 + TASKMGR_STATUS_H + 4 ? view_h - 72 - TASKMGR_STATUS_H - 4 : 80;
    uint32_t vis_rows = visible_rows();
    struct leonos_ui_list_column cols[] = {
        {T("PROCESS", "进程"), list_w > 382 ? list_w - 382 : 80},
        {"PID", 44},
        {T("CPU", "CPU"), 48},
        {T("MEM", "内存"), 64},
        {T("STATE", "状态"), 58},
        {T("USER", "用户"), 80},
        {T("PRIV", "权限"), 88},
    };
    struct leonos_ui_menubar_item menu_items[] = {
        {T("File", "文件"), TASKMGR_MENU_FILE, 64, 0},
        {T("Options", "选项"), TASKMGR_MENU_OPTIONS, 80, 0},
    };
    struct leonos_ui_tab_item tabs[3];
    uint32_t tab_w;
    uint32_t action_x;
    taskmgr_tab_items(tabs);
    leonos_ui_treeview_state_set_viewport(&process_tree, vis_rows);

    leonos_ui_rect(ui, 0, 0, view_w, view_h, LEONOS_UI_WHITE);
    leonos_ui_menubar_draw(ui, 0, 0, view_w, menu_items,
                           sizeof(menu_items) / sizeof(menu_items[0]),
                           menu_open);
    leonos_ui_toolbar(ui, 0, 28, view_w, 36);
    leonos_ui_toolbar_button(ui, 8, 34, 88, T("Refresh", "刷新"), 0);
    taskmgr_tabs.selected_id = active_tab;
    tab_w = toolbar_tab_width();
    action_x = toolbar_action_x();
    leonos_ui_tab_control(ui, 104, 34, tab_w, tabs, 3, &taskmgr_tabs);
    if (active_tab == TASKMGR_TAB_PROCESSES && action_x + 86 <= view_w) {
        leonos_ui_toolbar_button(ui, action_x, 34, 86, T("End Task", "结束任务"),
                                 selected_task_killable() ? 0 : LEONOS_UI_BUTTON_DISABLED);
    } else if (active_tab == TASKMGR_TAB_STARTUP) {
        struct leonos_startup_entry *entry = selected_startup_entry();
        if (action_x + 92 <= view_w) {
            leonos_ui_toolbar_button(ui, action_x, 34, 92,
                                     entry && entry->enabled ? T("Disable", "禁用") :
                                                               T("Enable", "启用"),
                                     entry ? 0 : LEONOS_UI_BUTTON_DISABLED);
        }
        if (action_x + 192 <= view_w) {
            leonos_ui_toolbar_button(ui, action_x + 100, 34, 86, T("Remove", "删除"),
                                     entry ? 0 : LEONOS_UI_BUTTON_DISABLED);
        }
    }

    if (active_tab == TASKMGR_TAB_PROCESSES) {
        line[0] = 0;
        append_text(line, &pos, sizeof(line), "tick=");
        append_dec(line, &pos, sizeof(line), task_tick);
        append_text(line, &pos, sizeof(line), " tasks=");
        append_dec(line, &pos, sizeof(line), task_count);
        uint32_t tick_x = action_x + 86 <= view_w ? action_x + 94 : action_x;
        if (tick_x + 64 < view_w) {
            leonos_ui_text_clipped(ui, tick_x, 40, view_w - tick_x - 8, line,
                                   LEONOS_UI_BLACK, LEONOS_UI_GRAY);
        }

        leonos_ui_scroll_view_frame(ui, 8, 72, view_w - 16, list_h);
        leonos_ui_treeview(ui, 10, 74, list_w, cols, 7,
                            process_tree_items, task_count, &process_tree);
        leonos_ui_vscrollbar(ui, view_w - 26, 74, 18, view_h > 104 ? view_h - 104 : 24,
                             process_tree.scroll,
                             process_tree.visible_count > process_tree.visible_rows
                                 ? process_tree.visible_count : process_tree.visible_rows,
                             process_tree.visible_rows,
                             process_tree.visible_count <= process_tree.visible_rows
                                 ? LEONOS_UI_SCROLLBAR_DISABLED : 0);
    } else if (active_tab == TASKMGR_TAB_PERFORMANCE) {
        draw_performance(ui);
    } else {
        draw_startup(ui);
    }
    leonos_ui_statusbar(ui, view_h - TASKMGR_STATUS_H, TASKMGR_STATUS_H, status_text);

    if (menu_open == TASKMGR_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Refresh", "刷新"), TASKMGR_ACTION_REFRESH, 0},
            {T("End Task", "结束任务"), TASKMGR_ACTION_END,
             selected_task_killable() ? 0 : LEONOS_UI_MENU_DISABLED},
            {T("About", "关于"), TASKMGR_ACTION_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, menu_items,
                                    sizeof(menu_items) / sizeof(menu_items[0]),
                                    TASKMGR_MENU_FILE, &r);
        leonos_ui_menu_popup(ui, (uint32_t)r.x, TASKMGR_MENU_BAR_H, 154,
                             items, sizeof(items) / sizeof(items[0]), 0);
    } else if (menu_open == TASKMGR_MENU_OPTIONS) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Processes", "进程"), TASKMGR_ACTION_PROCESSES, 0},
            {T("Performance", "性能"), TASKMGR_ACTION_PERFORMANCE, 0},
            {T("Startup Apps", "启动应用"), TASKMGR_ACTION_STARTUP, 0},
            {T("About", "关于"), TASKMGR_ACTION_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, menu_items,
                                    sizeof(menu_items) / sizeof(menu_items[0]),
                                    TASKMGR_MENU_OPTIONS, &r);
        leonos_ui_menu_popup(ui, (uint32_t)r.x, TASKMGR_MENU_BAR_H, 178,
                              items, sizeof(items) / sizeof(items[0]),
                              active_tab == TASKMGR_TAB_PROCESSES
                                  ? TASKMGR_ACTION_PROCESSES
                                  : active_tab == TASKMGR_TAB_PERFORMANCE
                                        ? TASKMGR_ACTION_PERFORMANCE
                                        : TASKMGR_ACTION_STARTUP);
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
    struct leonos_ui_menubar_item menu_items[] = {
        {T("File", "文件"), TASKMGR_MENU_FILE, 64, 0},
        {T("Options", "选项"), TASKMGR_MENU_OPTIONS, 80, 0},
    };
    uint32_t action = 0;
    if (leonos_ui_menubar_hit(x, y, 0, 0, menu_items,
                              sizeof(menu_items) / sizeof(menu_items[0]),
                              &action)) {
        if (action) {
            menu_open = menu_open == action ? TASKMGR_MENU_NONE : (uint8_t)action;
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    if (menu_open == TASKMGR_MENU_FILE) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Refresh", "刷新"), TASKMGR_ACTION_REFRESH, 0},
            {T("End Task", "结束任务"), TASKMGR_ACTION_END,
             selected_task_killable() ? 0 : LEONOS_UI_MENU_DISABLED},
            {T("About", "关于"), TASKMGR_ACTION_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, menu_items,
                                    sizeof(menu_items) / sizeof(menu_items[0]),
                                    TASKMGR_MENU_FILE, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x,
                                     TASKMGR_MENU_BAR_H, 154,
                                     items, sizeof(items) / sizeof(items[0]),
                                     &action)) {
            menu_open = TASKMGR_MENU_NONE;
            if (action == TASKMGR_ACTION_REFRESH) {
                refresh_all();
            } else if (action == TASKMGR_ACTION_END) {
                kill_selected_task();
            } else if (action == TASKMGR_ACTION_ABOUT) {
                leonos_ui_show_message_box(T("Task Manager", "任务管理器"), T("Live task snapshot from the scheduler.", "来自调度器的实时任务快照。"), "OK");
            }
            return 1;
        }
        menu_open = TASKMGR_MENU_NONE;
        return 1;
    }
    if (menu_open == TASKMGR_MENU_OPTIONS) {
        struct leonos_ui_context_menu_item items[] = {
            {T("Processes", "进程"), TASKMGR_ACTION_PROCESSES, 0},
            {T("Performance", "性能"), TASKMGR_ACTION_PERFORMANCE, 0},
            {T("Startup Apps", "启动应用"), TASKMGR_ACTION_STARTUP, 0},
            {T("About", "关于"), TASKMGR_ACTION_ABOUT, 0},
        };
        struct leonos_ui_rect r;
        leonos_ui_menubar_item_rect(0, 0, menu_items,
                                    sizeof(menu_items) / sizeof(menu_items[0]),
                                    TASKMGR_MENU_OPTIONS, &r);
        if (leonos_ui_menu_popup_hit(x, y, (uint32_t)r.x,
                                     TASKMGR_MENU_BAR_H, 178,
                                     items, sizeof(items) / sizeof(items[0]),
                                     &action)) {
            menu_open = TASKMGR_MENU_NONE;
            if (action == TASKMGR_ACTION_PROCESSES) {
                active_tab = TASKMGR_TAB_PROCESSES;
                taskmgr_tabs.selected_id = active_tab;
                refresh_all();
            } else if (action == TASKMGR_ACTION_PERFORMANCE) {
                active_tab = TASKMGR_TAB_PERFORMANCE;
                taskmgr_tabs.selected_id = active_tab;
                refresh_all();
            } else if (action == TASKMGR_ACTION_STARTUP) {
                active_tab = TASKMGR_TAB_STARTUP;
                taskmgr_tabs.selected_id = active_tab;
                refresh_all();
            } else if (action == TASKMGR_ACTION_ABOUT) {
                leonos_ui_show_message_box(T("Task Manager", "任务管理器"), T("Shows runnable, sleeping, and exited tasks.", "显示可运行、睡眠和已退出任务。"), "OK");
            }
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
    leonos_ui_treeview_state_init(&process_tree, visible_rows(), 24);
    leonos_ui_listview_state_init(&startup_list, startup_visible_rows(), 24);
    leonos_ui_tab_state_init(&taskmgr_tabs, TASKMGR_TAB_PROCESSES);
    process_tree.focused = 1;
    for (;;) {
        unsigned long now = leonos_uptime_ms();
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event,
                                         context_menu_animating ? 20U : LEONOS_GUI_IDLE_WAIT_MS) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 3u)) {
                if (event.buttons & 2u) {
                    if (active_tab == TASKMGR_TAB_PROCESSES) {
                        uint32_t activate = 0;
                        leonos_ui_treeview_state_handle_mouse(&process_tree,
                                                              process_tree_items, task_count,
                                                              event.x, event.y, 10, 102,
                                                              view_w > 38 ? view_w - 38 : 320,
                                                              &activate);
                        show_context_menu_at(event.x, event.y);
                    }
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
                if (hit_rect_i(event.x, event.y, 8, 34, 88, LEONOS_UI_BUTTON_H)) {
                    refresh_all();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                {
                    struct leonos_ui_tab_item tabs[3];
                    uint32_t tab_w = toolbar_tab_width();
                    taskmgr_tab_items(tabs);
                    if (leonos_ui_tab_control_handle_mouse(&taskmgr_tabs, event.x, event.y,
                                                           104, 34, tab_w, tabs, 3)) {
                        active_tab = (uint8_t)taskmgr_tabs.selected_id;
                        refresh_all();
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                }
                uint32_t action_x = toolbar_action_x();
                if (active_tab == TASKMGR_TAB_PROCESSES &&
                    action_x + 86 <= view_w &&
                    hit_rect_i(event.x, event.y, (int32_t)action_x, 34, 86, LEONOS_UI_BUTTON_H)) {
                    kill_selected_task();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (active_tab == TASKMGR_TAB_STARTUP) {
                    uint32_t dropdown_rows = startup_dropdown_rows();
                    uint32_t dropdown_h = dropdown_rows * TASKMGR_STARTUP_USER_ROW_H;
                    if (startup_user_dropdown_open &&
                        hit_rect_i(event.x, event.y, 70, 102, 160, (int32_t)dropdown_h)) {
                        uint32_t row = ((uint32_t)event.y - 102U) / TASKMGR_STARTUP_USER_ROW_H;
                        uint32_t index = startup_user_dropdown_scroll + row;
                        if (index < startup_user_count) {
                            startup_selected_uid = startup_users[index].uid;
                            startup_user_dropdown_open = 0;
                            startup_user_dropdown_scroll = 0;
                            refresh_startup_entries();
                        }
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                    if (startup_user_dropdown_open && startup_user_count > dropdown_rows &&
                        hit_rect_i(event.x, event.y, 232, 102, 16, (int32_t)dropdown_h)) {
                        leonos_ui_vscrollbar_handle_mouse(&startup_user_dropdown_scroll,
                                                          startup_user_count, dropdown_rows,
                                                          232, 102, 16, dropdown_h,
                                                          event.x, event.y);
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                    if (hit_rect_i(event.x, event.y, 70, 78, 180, LEONOS_UI_BUTTON_H)) {
                        startup_user_dropdown_open = startup_user_dropdown_open ? 0U : 1U;
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                    startup_user_dropdown_open = 0;
                    if (action_x + 92 <= view_w &&
                        hit_rect_i(event.x, event.y, (int32_t)action_x, 34, 92, LEONOS_UI_BUTTON_H)) {
                        toggle_selected_startup_entry();
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                    if (action_x + 192 <= view_w &&
                        hit_rect_i(event.x, event.y, (int32_t)action_x + 100, 34, 86,
                                   LEONOS_UI_BUTTON_H)) {
                        remove_selected_startup_entry();
                        present_taskmgr((uint32_t)window_id, &ui);
                        continue;
                    }
                    if (event.x >= (int32_t)(view_w - 26) && event.y >= 114 &&
                        event.y < (int32_t)(view_h - TASKMGR_STATUS_H)) {
                        leonos_ui_vscrollbar_handle_mouse(&startup_list.scroll,
                                                          startup_entry_count > startup_visible_rows()
                                                              ? startup_entry_count : startup_visible_rows(),
                                                          startup_visible_rows(),
                                                          view_w - 26, 114, 18,
                                                          view_h > 112 + TASKMGR_STATUS_H + 6
                                                              ? view_h - 112 - TASKMGR_STATUS_H - 6 : 24,
                                                          event.x, event.y);
                    } else {
                        uint32_t activate = 0;
                        leonos_ui_listview_state_handle_mouse(&startup_list, event.x, event.y,
                                                              10, 142,
                                                              view_w > 38 ? view_w - 38 : 320,
                                                              &activate);
                    }
                    startup_list.focused = 1;
                }
                if (active_tab == TASKMGR_TAB_PROCESSES &&
                    event.x >= (int32_t)(view_w - 26) && event.y >= 74 &&
                    event.y < (int32_t)(view_h - TASKMGR_STATUS_H)) {
                    leonos_ui_vscrollbar_handle_mouse(&process_tree.scroll,
                                                      process_tree.visible_count > process_tree.visible_rows
                                                          ? process_tree.visible_count
                                                          : process_tree.visible_rows,
                                                      visible_rows(),
                                                      view_w - 26, 74, 18, view_h > 104 ? view_h - 104 : 24,
                                                      event.x, event.y);
                } else if (active_tab == TASKMGR_TAB_PROCESSES) {
                    uint32_t activate = 0;
                    leonos_ui_treeview_state_handle_mouse(&process_tree,
                                                          process_tree_items, task_count,
                                                          event.x, event.y, 10, 102,
                                                          view_w > 38 ? view_w - 38 : 320,
                                                          &activate);
                    (void)activate;
                }
                process_tree.focused = 1;
                present_taskmgr((uint32_t)window_id, &ui);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_WHEEL) {
                if (active_tab == TASKMGR_TAB_PROCESSES &&
                    leonos_ui_treeview_state_handle_wheel(&process_tree, event.dy)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                } else if (active_tab == TASKMGR_TAB_STARTUP && startup_user_dropdown_open &&
                           startup_user_count > startup_dropdown_rows()) {
                    leonos_ui_vscrollbar_handle_wheel(&startup_user_dropdown_scroll,
                                                       startup_user_count, startup_dropdown_rows(),
                                                       event.dy);
                    present_taskmgr((uint32_t)window_id, &ui);
                } else if (active_tab == TASKMGR_TAB_STARTUP &&
                           leonos_ui_listview_state_handle_wheel(&startup_list, event.dy)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                }
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                menu_open = TASKMGR_MENU_NONE;
                context_menu_set_active(0);
                uint32_t activate = 0;
                if (active_tab == TASKMGR_TAB_PROCESSES && event.keycode == LEONOS_KEY_DELETE) {
                    kill_selected_task();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (active_tab == TASKMGR_TAB_STARTUP && event.keycode == LEONOS_KEY_DELETE) {
                    remove_selected_startup_entry();
                    present_taskmgr((uint32_t)window_id, &ui);
                    continue;
                }
                if (active_tab == TASKMGR_TAB_PROCESSES &&
                    leonos_ui_treeview_state_handle_key(&process_tree, process_tree_items,
                                                        task_count, event.keycode, &activate)) {
                    present_taskmgr((uint32_t)window_id, &ui);
                } else if (active_tab == TASKMGR_TAB_STARTUP &&
                           leonos_ui_listview_state_handle_key(&startup_list, event.keycode, &activate)) {
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
                leonos_ui_treeview_state_set_viewport(&process_tree, visible_rows());
                leonos_ui_treeview_state_sync(&process_tree, process_tree_items, task_count);
                startup_list.visible_rows = startup_visible_rows();
                leonos_ui_listview_state_set_count(&startup_list, startup_entry_count);
                present_taskmgr((uint32_t)window_id, &ui);
            }
            event.window_id = (uint32_t)window_id;
        }
        if (now - last_refresh >= 500) {
            refresh_all();
            present_taskmgr((uint32_t)window_id, &ui);
            last_refresh = now;
        } else if (context_menu_animating) {
            present_taskmgr((uint32_t)window_id, &ui);
        }
        sleep_ms(20);
    }
}
