#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TERM_W 640
#define TERM_H 360
#define TERM_COLS 78
#define TERM_ROWS 18
#define TERM_INPUT_MAX 64

static uint32_t pixels[TERM_W * TERM_H];
static char screen_chars[TERM_ROWS][TERM_COLS];
static uint32_t cursor_row;
static uint32_t cursor_col;
static uint32_t pty_id;
static char title_text[96];

static void clear_screen_chars(void)
{
    for (uint32_t y = 0; y < TERM_ROWS; ++y) {
        for (uint32_t x = 0; x < TERM_COLS; ++x) {
            screen_chars[y][x] = ' ';
        }
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void scroll_up(void)
{
    for (uint32_t y = 1; y < TERM_ROWS; ++y) {
        for (uint32_t x = 0; x < TERM_COLS; ++x) {
            screen_chars[y - 1][x] = screen_chars[y][x];
        }
    }
    for (uint32_t x = 0; x < TERM_COLS; ++x) {
        screen_chars[TERM_ROWS - 1][x] = ' ';
    }
    if (cursor_row) {
        --cursor_row;
    }
}

static void newline(void)
{
    cursor_col = 0;
    if (++cursor_row >= TERM_ROWS) {
        cursor_row = TERM_ROWS - 1;
        scroll_up();
    }
}

static void put_term_char(char ch)
{
    if (ch == '\f') {
        clear_screen_chars();
        return;
    }
    if (ch == '\r') {
        cursor_col = 0;
        return;
    }
    if (ch == '\n') {
        newline();
        return;
    }
    if (ch == '\b' || (unsigned char)ch == 127) {
        if (cursor_col) {
            --cursor_col;
            screen_chars[cursor_row][cursor_col] = ' ';
        }
        return;
    }
    if ((unsigned char)ch < 32) {
        return;
    }
    screen_chars[cursor_row][cursor_col] = ch;
    if (++cursor_col >= TERM_COLS) {
        newline();
    }
}

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

static void draw_terminal(struct leonos_ui_surface *ui)
{
    char line[TERM_COLS + 1];
    leonos_ui_rect(ui, 0, 0, TERM_W, TERM_H, LEONOS_UI_WHITE);
    leonos_ui_panel(ui, 8, 8, TERM_W - 16, TERM_H - 16, 0x00000000);
    leonos_ui_text(ui, 18, 16, title_text, LEONOS_UI_WHITE, 0x00000000);
    for (uint32_t row = 0; row < TERM_ROWS; ++row) {
        for (uint32_t col = 0; col < TERM_COLS; ++col) {
            line[col] = screen_chars[row][col];
        }
        line[TERM_COLS] = 0;
        leonos_ui_text(ui, 14, 40 + row * LEONOS_FONT_H, line, LEONOS_UI_WHITE, 0x00000000);
    }
    if (cursor_row < TERM_ROWS && cursor_col < TERM_COLS) {
        leonos_ui_rect(ui, 14 + cursor_col * LEONOS_FONT_W,
                       40 + cursor_row * LEONOS_FONT_H + LEONOS_FONT_H - 2,
                       LEONOS_FONT_W, 2, LEONOS_UI_WHITE);
    }
}

static int map_keycode(uint8_t keycode, uint8_t pressed, char *out)
{
    (void)pressed;
    switch (keycode) {
    case 2: *out = '1'; return 1;
    case 3: *out = '2'; return 1;
    case 4: *out = '3'; return 1;
    case 5: *out = '4'; return 1;
    case 6: *out = '5'; return 1;
    case 7: *out = '6'; return 1;
    case 8: *out = '7'; return 1;
    case 9: *out = '8'; return 1;
    case 10: *out = '9'; return 1;
    case 11: *out = '0'; return 1;
    case 12: *out = '-'; return 1;
    case 13: *out = '='; return 1;
    case 14: *out = '\b'; return 1;
    case 15: *out = '\t'; return 1;
    case 16: *out = 'q'; return 1;
    case 17: *out = 'w'; return 1;
    case 18: *out = 'e'; return 1;
    case 19: *out = 'r'; return 1;
    case 20: *out = 't'; return 1;
    case 21: *out = 'y'; return 1;
    case 22: *out = 'u'; return 1;
    case 23: *out = 'i'; return 1;
    case 24: *out = 'o'; return 1;
    case 25: *out = 'p'; return 1;
    case 26: *out = '['; return 1;
    case 27: *out = ']'; return 1;
    case 28: *out = '\n'; return 1;
    case 30: *out = 'a'; return 1;
    case 31: *out = 's'; return 1;
    case 32: *out = 'd'; return 1;
    case 33: *out = 'f'; return 1;
    case 34: *out = 'g'; return 1;
    case 35: *out = 'h'; return 1;
    case 36: *out = 'j'; return 1;
    case 37: *out = 'k'; return 1;
    case 38: *out = 'l'; return 1;
    case 39: *out = ';'; return 1;
    case 40: *out = '\''; return 1;
    case 41: *out = '`'; return 1;
    case 43: *out = '\\'; return 1;
    case 44: *out = 'z'; return 1;
    case 45: *out = 'x'; return 1;
    case 46: *out = 'c'; return 1;
    case 47: *out = 'v'; return 1;
    case 48: *out = 'b'; return 1;
    case 49: *out = 'n'; return 1;
    case 50: *out = 'm'; return 1;
    case 51: *out = ','; return 1;
    case 52: *out = '.'; return 1;
    case 53: *out = '/'; return 1;
    case 57: *out = ' '; return 1;
    default:
        return 0;
    }
}

static void pump_pty_output(void)
{
    char buf[256];
    int got;
    do {
        got = leonos_pty_read_output(pty_id, buf, sizeof(buf));
        if (got > 0) {
            for (int i = 0; i < got; ++i) {
                put_term_char(buf[i]);
            }
        }
    } while (got > 0);
}

int main(void)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    int shell_pid;

    puts("[terminal.elf] terminal starting");
    pty_id = (uint32_t)leonos_pty_create();
    printf("[terminal.elf] pid=%d pty=%d\n", getpid(), pty_id);
    if ((int)pty_id <= 0) {
        printf("[terminal.elf] pty create failed=%d\n", (int)pty_id);
        return 1;
    }

    window_id = leonos_gui_create_app_window("Terminal", "LeonOS terminal", TERM_W, TERM_H);
    if (window_id <= 0) {
        printf("[terminal.elf] create window failed=%d\n", window_id);
        return 1;
    }
    shell_pid = leonos_pty_spawn("0:/userland/shell.elf", pty_id);
    printf("[terminal.elf] spawn shell pid=%d\n", shell_pid);

    leonos_ui_bind(&ui, pixels, TERM_W, TERM_H, TERM_W);
    clear_screen_chars();
    copy_text(title_text, sizeof(title_text), "LeonOS Terminal");
    draw_terminal(&ui);
    leonos_gui_present_window((uint32_t)window_id, TERM_W, TERM_H, TERM_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_poll_app_event(&event) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                leonos_pty_write_input(pty_id, "exit\n", 5);
                sleep_ms(40);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                char ch;
                if (map_keycode(event.keycode, event.pressed, &ch)) {
                    leonos_pty_write_input(pty_id, &ch, 1);
                }
            }
        }
        pump_pty_output();
        draw_terminal(&ui);
        leonos_gui_present_window((uint32_t)window_id, TERM_W, TERM_H, TERM_W, pixels);
        sleep_ms(20);
    }
}
