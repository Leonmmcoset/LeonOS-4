#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define CJKTEST_W 720
#define CJKTEST_H 430

static uint32_t pixels[CJKTEST_W * CJKTEST_H];
static char status_line[160] = "准备测试中文显示";
static char text_buffer[512] =
    "中文显示测试\n"
    "你好，LeonOS 4。\n"
    "中文标点：，。！？；：《》【】（）\n"
    "宽度测试：ASCII=1 cell，中文=2 cells。\n"
    "文件名测试会创建 0:/测试目录/你好.txt\n";
static struct leonos_ui_text_area_state text_state;

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (!dst || !pos || *pos + 1 >= cap) {
        return;
    }
    dst[*pos] = ch;
    ++(*pos);
    dst[*pos] = 0;
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        append_char(dst, pos, cap, *text++);
    }
}

static void append_int(char *dst, uint32_t *pos, uint32_t cap, int value)
{
    char tmp[16];
    uint32_t n = 0;
    unsigned int v;
    if (value < 0) {
        append_char(dst, pos, cap, '-');
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }
    if (v == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static void run_file_test(void)
{
    const char *dir = "0:/测试目录";
    const char *path = "0:/测试目录/你好.txt";
    const char *content = "你好，LeonOS 4。中文文件名和 UTF-8 内容测试。\n";
    struct leonos_stat st;
    struct leonos_dir_entry entry;
    uint32_t pos = 0;
    int mkdir_ret = mkdir(dir, 0);
    int fd = open(path, LEONOS_O_CREAT | LEONOS_O_TRUNC | LEONOS_O_WRONLY, 0);
    int write_ret = -1;
    int stat_ret;
    int readdir_seen = 0;

    if (fd >= 0) {
        write_ret = (int)write(fd, content, strlen(content));
        close(fd);
    }
    stat_ret = stat(path, &st);
    fd = open(dir, LEONOS_O_RDONLY, 0);
    if (fd >= 0) {
        while (leonos_readdir(fd, &entry) > 0) {
            printf("[cjktest.elf] readdir name=%s type=%d\n", entry.name, (int)entry.type);
            if (entry.name[0]) {
                readdir_seen = 1;
            }
        }
        close(fd);
    }

    append_text(status_line, &pos, sizeof(status_line), "mkdir=");
    append_int(status_line, &pos, sizeof(status_line), mkdir_ret);
    append_text(status_line, &pos, sizeof(status_line), " write=");
    append_int(status_line, &pos, sizeof(status_line), write_ret);
    append_text(status_line, &pos, sizeof(status_line), " stat=");
    append_int(status_line, &pos, sizeof(status_line), stat_ret);
    append_text(status_line, &pos, sizeof(status_line), " list=");
    append_text(status_line, &pos, sizeof(status_line), readdir_seen ? "OK" : "EMPTY");

    printf("[cjktest.elf] unicode file test %s\n", status_line);
}

static void draw(struct leonos_ui_surface *ui)
{
    uint32_t sample_w;
    leonos_ui_rect(ui, 0, 0, CJKTEST_W, CJKTEST_H, LEONOS_UI_WHITE);
    leonos_ui_window(ui, 8, 8, CJKTEST_W - 16, CJKTEST_H - 16,
                     "中文显示测试", LEONOS_UI_WINDOW_ACTIVE | LEONOS_UI_WINDOW_NO_RESIZE, 0);

    leonos_ui_text(ui, 28, 48, "你好，LeonOS 4。中文显示测试。", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 28, 74, "标点：，。！？；：《》【】（）", LEONOS_UI_BLACK, LEONOS_UI_WHITE);
    leonos_ui_text(ui, 28, 100, "ASCII ABC 123  |  中文宽字符  |  mixed 混排", LEONOS_UI_BLACK, LEONOS_UI_WHITE);

    sample_w = leonos_ui_text_width("你好，LeonOS 4。");
    leonos_ui_text(ui, 28, 132, "leonos_ui_text_width(\"你好，LeonOS 4。\"):", LEONOS_UI_DARK, LEONOS_UI_WHITE);
    leonos_ui_progress(ui, 338, 130, 220, 18, sample_w, 220);

    leonos_ui_groupbox(ui, 28, 164, 664, 170, "UTF-8 文本域");
    leonos_ui_text_area_state_draw(ui, 42, 190, 636, 118, &text_state, LEONOS_UI_EDIT_READONLY);

    leonos_ui_statusbar(ui, CJKTEST_H - 36, 28, status_line);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;

    puts("[cjktest.elf] CJK display test starting");
    run_file_test();

    window_id = leonos_gui_create_app_window_ex("中文显示测试", "UTF-8 / CJK font test",
                                                CJKTEST_W, CJKTEST_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[cjktest.elf] create window failed=%d\n", window_id);
        return 1;
    }

    leonos_ui_bind(&ui, pixels, CJKTEST_W, CJKTEST_H, CJKTEST_W);
    leonos_ui_text_area_state_init(&text_state, text_buffer, sizeof(text_buffer));
    text_state.readonly = 1;

    draw(&ui);
    leonos_gui_present_window((uint32_t)window_id, CJKTEST_W, CJKTEST_H, CJKTEST_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_RESIZE ||
                event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                draw(&ui);
                leonos_gui_present_window((uint32_t)window_id, CJKTEST_W, CJKTEST_H, CJKTEST_W, pixels);
            }
        } else {
            sleep_ms(10);
        }
    }
    return 0;
}
