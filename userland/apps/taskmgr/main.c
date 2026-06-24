#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TASKMGR_W 620
#define TASKMGR_H 300

static uint32_t pixels[TASKMGR_W * TASKMGR_H];
static struct leonos_task_info tasks[LEONOS_TASK_MAX];
static uint32_t task_count;
static uint64_t task_tick;

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

static void draw_taskmgr(struct leonos_ui_surface *ui)
{
    char line[128];
    uint32_t pos = 0;
    uint32_t rows;

    leonos_ui_rect(ui, 0, 0, TASKMGR_W, TASKMGR_H, LEONOS_UI_WHITE);
    line[0] = 0;
    append_text(line, &pos, sizeof(line), "tick=");
    append_dec(line, &pos, sizeof(line), task_tick);
    append_text(line, &pos, sizeof(line), " tasks=");
    append_dec(line, &pos, sizeof(line), task_count);
    leonos_ui_text(ui, 10, 10, line, LEONOS_UI_BLACK, LEONOS_UI_WHITE);

    leonos_ui_list_header(ui, 8, 34, TASKMGR_W - 16, "PID PPID STATE KIND CR3        WAKE NAME");
    rows = task_count;
    if (rows > 10) {
        rows = 10;
    }
    for (uint32_t i = 0; i < rows; ++i) {
        task_line(line, sizeof(line), &tasks[i]);
        leonos_ui_list_row(ui, 8, 64 + i * 24, TASKMGR_W - 16, line, 0);
    }
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    unsigned long last_refresh = 0;
    int window_id;

    puts("[taskmgr.elf] task manager starting");
    printf("[taskmgr.elf] pid=%d creating GUI window\n", getpid());
    window_id = leonos_gui_create_app_window("Task Manager", "Task snapshot", TASKMGR_W, TASKMGR_H);
    if (window_id <= 0) {
        printf("[taskmgr.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, TASKMGR_W, TASKMGR_H, TASKMGR_W);
    for (;;) {
        unsigned long now = leonos_uptime_ms();
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            event.window_id = (uint32_t)window_id;
        }
        if (now - last_refresh >= 500) {
            int count = leonos_task_snapshot(tasks, LEONOS_TASK_MAX, &task_tick);
            task_count = count > 0 ? (uint32_t)count : 0;
            draw_taskmgr(&ui);
            leonos_gui_present_window((uint32_t)window_id, TASKMGR_W, TASKMGR_H, TASKMGR_W, pixels);
            last_refresh = now;
        }
        sleep_ms(20);
    }
}
