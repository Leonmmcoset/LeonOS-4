#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <leonos/ui.h>

#define BUGTEST_W 620
#define BUGTEST_H 430
#define TEST_COUNT 18U

static uint32_t pixels[BUGTEST_W * BUGTEST_H];
static char status_text[128] = "Click Safe Suite first. Destructive tests may freeze or panic the kernel.";
static uint32_t selected_test;
static uint32_t ran_mask;
static uint32_t pass_mask;
static uint32_t fail_mask;
static uint32_t safe_ran_count;
static uint32_t safe_pass_count;
static uint32_t safe_fail_count;

enum test_kind {
    TEST_SAFE = 1,
    TEST_DESTRUCTIVE = 2,
};

struct bug_test {
    const char *name;
    const char *desc;
    enum test_kind kind;
    int (*run)(void);
};

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

static uint32_t str_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static void append_text(char *dst, uint32_t cap, uint32_t *pos, const char *text)
{
    while (text && *text && *pos + 1 < cap) {
        dst[(*pos)++] = *text++;
    }
    if (cap) {
        dst[*pos < cap ? *pos : cap - 1] = 0;
    }
}

static void append_dec(char *dst, uint32_t cap, uint32_t *pos, int value)
{
    char tmp[16];
    uint32_t n = 0;
    uint32_t v;
    if (value < 0) {
        append_text(dst, cap, pos, "-");
        v = (uint32_t)(-value);
    } else {
        v = (uint32_t)value;
    }
    if (v == 0) {
        append_text(dst, cap, pos, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) {
        char ch[2] = {tmp[--n], 0};
        append_text(dst, cap, pos, ch);
    }
}

static void set_status_result(const char *name, int ret)
{
    uint32_t pos = 0;
    copy_text(status_text, sizeof(status_text), "");
    append_text(status_text, sizeof(status_text), &pos, name);
    append_text(status_text, sizeof(status_text), &pos, " returned ");
    append_dec(status_text, sizeof(status_text), &pos, ret);
}

static void set_suite_summary(void)
{
    uint32_t pos = 0;
    copy_text(status_text, sizeof(status_text), "");
    append_text(status_text, sizeof(status_text), &pos, "Safe suite report: ");
    append_dec(status_text, sizeof(status_text), &pos, (int)safe_pass_count);
    append_text(status_text, sizeof(status_text), &pos, " passed, ");
    append_dec(status_text, sizeof(status_text), &pos, (int)safe_fail_count);
    append_text(status_text, sizeof(status_text), &pos, " failed, ");
    append_dec(status_text, sizeof(status_text), &pos, (int)safe_ran_count);
    append_text(status_text, sizeof(status_text), &pos, " total.");
}

static int nonfatal_result(int ret)
{
    return ret < 0 || ret == 0 ? 1 : 0;
}

static int safe_read_bad_dst(void)
{
    return nonfatal_result((int)read(0, (void *)0x200000ULL, 16));
}

static int safe_write_bad_src(void)
{
    return nonfatal_result((int)write(1, (const void *)0x200000ULL, 16));
}

static int safe_open_bad_path(void)
{
    return nonfatal_result(open((const char *)0x200000ULL, LEONOS_O_RDONLY, 0));
}

static int safe_stat_bad_out(void)
{
    return nonfatal_result(stat("0:/etc/leonos.conf", (struct leonos_stat *)0x200000ULL));
}

static int safe_getcwd_bad_out(void)
{
    return getcwd((char *)0x200000ULL, 32) == 0;
}

static int safe_list_dir_bad_entries(void)
{
    struct leonos_dir_list query = {
        .path = "0:/userland",
        .capacity = 4,
        .count = 0,
        .entries = (struct leonos_dir_entry *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_IOCTL_LIST_DIR, &query));
}

static int safe_system_info_bad_out(void)
{
    return nonfatal_result(ioctl(3, LEONOS_IOCTL_SYSTEM_INFO, (void *)0x200000ULL));
}

static int safe_gui_event_bad_out(void)
{
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_EVENT, (void *)0x200000ULL));
}

static int safe_create_window_bad_title(void)
{
    struct leonos_gui_create cmd = {
        .width = 120,
        .height = 80,
        .title = (const char *)0x200000ULL,
        .text = "bad title",
        .flags = 0,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_CREATE_WINDOW, &cmd));
}

static int safe_fb_text_bad_string(void)
{
    struct leonos_fb_text cmd = {
        .x = 0,
        .y = 0,
        .fg = LEONOS_UI_WHITE,
        .bg = LEONOS_UI_BLACK,
        .text = (const char *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_FB_TEXT, &cmd));
}

static int safe_fb_blit_bad_pixels(void)
{
    struct leonos_fb_blit cmd = {
        .x = 0,
        .y = 0,
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = (const uint32_t *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_FB_BLIT, &cmd));
}

static int safe_present_bad_pixels(void)
{
    struct leonos_gui_present cmd = {
        .window_id = 0x12345678U,
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = (const uint32_t *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_PRESENT_WINDOW, &cmd));
}

static int safe_fetch_bad_pixels(void)
{
    struct leonos_gui_fetch cmd = {
        .window_id = 0x12345678U,
        .capacity_width = 8,
        .capacity_height = 8,
        .stride = 8,
        .out_width = 0,
        .out_height = 0,
        .pixels = (uint32_t *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_FETCH_WINDOW, &cmd));
}

static int safe_window_event_bad_out(void)
{
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_WINDOW_EVENT, (void *)0x200000ULL));
}

static int safe_send_window_event_bad_src(void)
{
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT, (void *)0x200000ULL));
}

static int safe_task_snapshot_bad_tasks(void)
{
    struct leonos_task_snapshot snapshot = {
        .capacity = 4,
        .count = 0,
        .tick = 0,
        .tasks = (struct leonos_task_info *)0x200000ULL,
    };
    return nonfatal_result(ioctl(3, LEONOS_GUI_IOCTL_TASKS, &snapshot));
}

static int safe_pty_bad_buffer(void)
{
    int pty = leonos_pty_create();
    struct leonos_pty_io io;
    if (pty <= 0) {
        return 0;
    }
    io.pty_id = (uint32_t)pty;
    io.length = 8;
    io.buffer = (char *)0x200000ULL;
    return nonfatal_result(ioctl(3, LEONOS_PTY_IOCTL_READ_OUTPUT, &io));
}

static int destructive_null_write(void)
{
    volatile uint64_t *p = (volatile uint64_t *)0;
    *p = 0x1122334455667788ULL;
    return 0;
}

static int destructive_kernel_read(void)
{
    volatile uint64_t value = *(volatile uint64_t *)0xffffffff80000000ULL;
    return (int)value;
}

static int destructive_bad_instruction(void)
{
    __asm__ volatile(".byte 0x0f, 0x0b");
    return 0;
}

static const struct bug_test tests[TEST_COUNT] = {
    {"read bad dst", "read(fd=0) into unmapped user pointer 0x200000", TEST_SAFE, safe_read_bad_dst},
    {"write bad src", "write(fd=1) from unmapped user pointer 0x200000", TEST_SAFE, safe_write_bad_src},
    {"open bad path", "open() with path pointer outside user range", TEST_SAFE, safe_open_bad_path},
    {"stat bad out", "stat() writes result to bad user pointer", TEST_SAFE, safe_stat_bad_out},
    {"getcwd bad out", "getcwd() writes cwd to bad user pointer", TEST_SAFE, safe_getcwd_bad_out},
    {"listdir bad entries", "LIST_DIR writes entries to bad pointer", TEST_SAFE, safe_list_dir_bad_entries},
    {"system info bad out", "SYSTEM_INFO writes struct to bad pointer", TEST_SAFE, safe_system_info_bad_out},
    {"input event bad out", "GUI EVENT writes input event to bad pointer", TEST_SAFE, safe_gui_event_bad_out},
    {"create bad title", "CREATE_WINDOW copies title from bad pointer", TEST_SAFE, safe_create_window_bad_title},
    {"fb text bad string", "FB_TEXT uses text pointer outside user range", TEST_SAFE, safe_fb_text_bad_string},
    {"fb blit bad pixels", "FB_BLIT reads pixels from bad pointer", TEST_SAFE, safe_fb_blit_bad_pixels},
    {"present bad pixels", "PRESENT_WINDOW reads pixels from bad pointer", TEST_SAFE, safe_present_bad_pixels},
    {"fetch bad pixels", "FETCH_WINDOW writes pixels to bad pointer", TEST_SAFE, safe_fetch_bad_pixels},
    {"window event bad out", "WINDOW_EVENT writes app event to bad pointer", TEST_SAFE, safe_window_event_bad_out},
    {"send event bad src", "SEND_WINDOW_EVENT reads event from bad pointer", TEST_SAFE, safe_send_window_event_bad_src},
    {"task list bad tasks", "TASKS writes snapshot array to bad pointer", TEST_SAFE, safe_task_snapshot_bad_tasks},
    {"pty bad buffer", "PTY read output writes to bad pointer", TEST_SAFE, safe_pty_bad_buffer},
    {"FAULT null write", "destructive: write to address 0x0 from Ring-3", TEST_DESTRUCTIVE, destructive_null_write},
};

static const struct bug_test extra_destructive_tests[] = {
    {"FAULT kernel read", "destructive: read kernel address from Ring-3", TEST_DESTRUCTIVE, destructive_kernel_read},
    {"FAULT ud2", "destructive: execute invalid instruction", TEST_DESTRUCTIVE, destructive_bad_instruction},
};

static uint32_t row_y(uint32_t index)
{
    return 62 + index * 18;
}

static void draw_status_mark(struct leonos_ui_surface *ui, uint32_t index, uint32_t y)
{
    uint32_t bit = 1u << index;
    const char *mark = "-";
    uint32_t fg = LEONOS_UI_DARK;
    if (pass_mask & bit) {
        mark = "OK";
        fg = 0x00008000u;
    } else if (fail_mask & bit) {
        mark = "FAIL";
        fg = 0x00800000u;
    } else if (ran_mask & bit) {
        mark = "RUN";
        fg = 0x00000080u;
    }
    leonos_ui_text(ui, 18, y, mark, fg, LEONOS_UI_WHITE);
}

static void draw_bugtest(struct leonos_ui_surface *ui)
{
    char line[96];
    uint32_t pos;
    leonos_ui_rect(ui, 0, 0, BUGTEST_W, BUGTEST_H, LEONOS_UI_WHITE);
    leonos_ui_dialog(ui, 0, 0, BUGTEST_W, BUGTEST_H, "Kernel Bug Test");
    leonos_ui_text(ui, 18, 38, "Safe tests should return errors. Fault tests may crash current kernel.", LEONOS_UI_BLACK, LEONOS_UI_GRAY);

    for (uint32_t i = 0; i < TEST_COUNT; ++i) {
        uint32_t y = row_y(i);
        uint32_t bg = i == selected_test ? LEONOS_UI_LIGHT : LEONOS_UI_WHITE;
        leonos_ui_rect(ui, 12, y - 2, 316, 17, bg);
        draw_status_mark(ui, i, y);
        leonos_ui_text_clipped(ui, 62, y, 250, tests[i].name,
                               tests[i].kind == TEST_DESTRUCTIVE ? 0x00800000u : LEONOS_UI_BLACK,
                               bg);
    }

    leonos_ui_panel(ui, 344, 58, 258, 174, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 358, 76, tests[selected_test].name, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, 358, 104, 220, tests[selected_test].desc, LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, 358, 126, 220, tests[selected_test].desc + (str_len(tests[selected_test].desc) > 32 ? 32 : str_len(tests[selected_test].desc)), LEONOS_UI_DARK, LEONOS_UI_LIGHT);
    leonos_ui_button(ui, 358, 160, 108, LEONOS_UI_BUTTON_H, "Run Test", 0);
    leonos_ui_button(ui, 478, 160, 108, LEONOS_UI_BUTTON_H, "Safe Suite", 0);
    leonos_ui_button(ui, 358, 194, 108, LEONOS_UI_BUTTON_H, extra_destructive_tests[0].name, 0);
    leonos_ui_button(ui, 478, 194, 108, LEONOS_UI_BUTTON_H, extra_destructive_tests[1].name, 0);

    leonos_ui_panel(ui, 344, 244, 258, 126, LEONOS_UI_LIGHT);
    leonos_ui_text(ui, 358, 260, "Report", LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    pos = 0;
    line[0] = 0;
    append_text(line, sizeof(line), &pos, "Safe ran ");
    append_dec(line, sizeof(line), &pos, (int)safe_ran_count);
    append_text(line, sizeof(line), &pos, " / ");
    append_dec(line, sizeof(line), &pos, (int)(TEST_COUNT - 1));
    leonos_ui_text(ui, 358, 284, line, LEONOS_UI_BLACK, LEONOS_UI_LIGHT);
    pos = 0;
    line[0] = 0;
    append_text(line, sizeof(line), &pos, "Pass ");
    append_dec(line, sizeof(line), &pos, (int)safe_pass_count);
    append_text(line, sizeof(line), &pos, "  Fail ");
    append_dec(line, sizeof(line), &pos, (int)safe_fail_count);
    leonos_ui_text(ui, 358, 306, line, safe_fail_count ? 0x00800000u : 0x00008000u,
                   LEONOS_UI_LIGHT);
    leonos_ui_text_clipped(ui, 358, 330, 226,
                           safe_ran_count ? status_text : "No report yet. Run Safe Suite.",
                           safe_fail_count ? 0x00800000u : LEONOS_UI_DARK,
                           LEONOS_UI_LIGHT);

    leonos_ui_statusbar(ui, BUGTEST_H - 28, 28, status_text);
}

static int hit_rect_i(int32_t x, int32_t y, int32_t rx, int32_t ry,
                      int32_t rw, int32_t rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void run_test_index(uint32_t index)
{
    int ret;
    uint32_t bit;
    if (index >= TEST_COUNT) {
        return;
    }
    bit = 1u << index;
    ran_mask |= bit;
    printf("[bugtest.elf] RUN %u %s\n", index, tests[index].name);
    ret = tests[index].run();
    set_status_result(tests[index].name, ret);
    if (ret) {
        pass_mask |= bit;
        fail_mask &= ~bit;
        if (tests[index].kind == TEST_SAFE) {
            ++safe_pass_count;
        }
        printf("[bugtest.elf] PASS %u %s\n", index, tests[index].name);
    } else {
        fail_mask |= bit;
        pass_mask &= ~bit;
        if (tests[index].kind == TEST_SAFE) {
            ++safe_fail_count;
        }
        printf("[bugtest.elf] FAIL %u %s\n", index, tests[index].name);
    }
    if (tests[index].kind == TEST_SAFE) {
        ++safe_ran_count;
    }
}

static void run_extra_destructive(uint32_t index)
{
    if (index >= sizeof(extra_destructive_tests) / sizeof(extra_destructive_tests[0])) {
        return;
    }
    copy_text(status_text, sizeof(status_text), extra_destructive_tests[index].name);
    printf("[bugtest.elf] DESTRUCTIVE %s\n", extra_destructive_tests[index].name);
    (void)extra_destructive_tests[index].run();
}

static void run_safe_suite(void)
{
    safe_ran_count = 0;
    safe_pass_count = 0;
    safe_fail_count = 0;
    for (uint32_t i = 0; i < TEST_COUNT; ++i) {
        if (tests[i].kind != TEST_SAFE) {
            continue;
        }
        run_test_index(i);
        sched_yield();
    }
    set_suite_summary();
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    puts("[bugtest.elf] kernel bug test app starting");
    window_id = leonos_gui_create_app_window_ex("Kernel Bug Test", "Kernel crash probe",
                                                BUGTEST_W, BUGTEST_H, 0);
    if (window_id <= 0) {
        printf("[bugtest.elf] create window failed=%d\n", window_id);
        return 1;
    }
    leonos_ui_bind(&ui, pixels, BUGTEST_W, BUGTEST_H, BUGTEST_W);
    draw_bugtest(&ui);
    leonos_gui_present_window((uint32_t)window_id, BUGTEST_W, BUGTEST_H, BUGTEST_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_MOUSE_BUTTON && (event.buttons & 1u)) {
                for (uint32_t i = 0; i < TEST_COUNT; ++i) {
                    if (hit_rect_i(event.x, event.y, 12, (int32_t)row_y(i) - 3, 316, 18)) {
                        selected_test = i;
                    }
                }
                if (hit_rect_i(event.x, event.y, 358, 160, 108, LEONOS_UI_BUTTON_H)) {
                    run_test_index(selected_test);
                } else if (hit_rect_i(event.x, event.y, 478, 160, 108, LEONOS_UI_BUTTON_H)) {
                    run_safe_suite();
                } else if (hit_rect_i(event.x, event.y, 358, 194, 108, LEONOS_UI_BUTTON_H)) {
                    run_extra_destructive(0);
                } else if (hit_rect_i(event.x, event.y, 478, 194, 108, LEONOS_UI_BUTTON_H)) {
                    run_extra_destructive(1);
                }
                draw_bugtest(&ui);
                leonos_gui_present_window((uint32_t)window_id, BUGTEST_W, BUGTEST_H, BUGTEST_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN && event.pressed) {
                if (event.keycode == LEONOS_KEY_ENTER) {
                    run_test_index(selected_test);
                } else if (event.keycode == LEONOS_KEY_TAB) {
                    selected_test = (selected_test + 1) % TEST_COUNT;
                } else if (event.keycode == 1) {
                    return 0;
                }
                draw_bugtest(&ui);
                leonos_gui_present_window((uint32_t)window_id, BUGTEST_W, BUGTEST_H, BUGTEST_W, pixels);
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw_bugtest(&ui);
                leonos_gui_present_window((uint32_t)window_id, BUGTEST_W, BUGTEST_H, BUGTEST_W, pixels);
            }
        }
        sleep_ms(10);
    }
}
