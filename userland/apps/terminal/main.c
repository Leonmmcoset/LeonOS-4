#include <leonos/gui.h>
#include <leonos/i18n.h>
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
#define TERM_TEXT_X 14
#define TERM_TEXT_Y 40
#define TERM_DEFAULT_FG 0x00ffffffu
#define TERM_DEFAULT_BG 0x00000000u
#define TERM_CSI_MAX_PARAMS 8
#define T(en, zh) leonos_i18n((en), (zh))

static uint32_t pixels[TERM_W * TERM_H];
static char screen_chars[TERM_ROWS][TERM_COLS];
static uint32_t screen_fg[TERM_ROWS][TERM_COLS];
static uint32_t screen_bg[TERM_ROWS][TERM_COLS];
static uint32_t cursor_row;
static uint32_t cursor_col;
static uint32_t pty_id;
static char title_text[96];
static uint32_t term_fg = TERM_DEFAULT_FG;
static uint32_t term_bg = TERM_DEFAULT_BG;
static uint8_t term_bold;

enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
};

static enum ansi_state ansi_parser_state;
static int csi_params[TERM_CSI_MAX_PARAMS];
static uint32_t csi_count;
static int csi_value;
static uint8_t csi_has_value;

static const uint32_t ansi_colors[8] = {
    0x00000000u, 0x00aa0000u, 0x0000aa00u, 0x00aa5500u,
    0x000000aaU, 0x00aa00aau, 0x0000aaaau, 0x00aaaaaau,
};

static const uint32_t ansi_bright_colors[8] = {
    0x00555555u, 0x00ff5555u, 0x0055ff55u, 0x00ffff55u,
    0x005555ffu, 0x00ff55ffu, 0x0055ffffu, 0x00ffffffu,
};

static void reset_attrs(void)
{
    term_fg = TERM_DEFAULT_FG;
    term_bg = TERM_DEFAULT_BG;
    term_bold = 0;
}

static void set_cell(uint32_t row, uint32_t col, char ch, uint32_t fg, uint32_t bg)
{
    if (row >= TERM_ROWS || col >= TERM_COLS) {
        return;
    }
    screen_chars[row][col] = ch;
    screen_fg[row][col] = fg;
    screen_bg[row][col] = bg;
}

static void clear_cell(uint32_t row, uint32_t col)
{
    set_cell(row, col, ' ', term_fg, term_bg);
}

static void clamp_cursor(void)
{
    if (cursor_row >= TERM_ROWS) {
        cursor_row = TERM_ROWS - 1;
    }
    if (cursor_col >= TERM_COLS) {
        cursor_col = TERM_COLS - 1;
    }
}

static void clear_screen_chars(void)
{
    for (uint32_t y = 0; y < TERM_ROWS; ++y) {
        for (uint32_t x = 0; x < TERM_COLS; ++x) {
            clear_cell(y, x);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
}

static void clear_line_range(uint32_t row, uint32_t from, uint32_t to)
{
    if (row >= TERM_ROWS || from >= TERM_COLS) {
        return;
    }
    if (to >= TERM_COLS) {
        to = TERM_COLS - 1;
    }
    for (uint32_t x = from; x <= to; ++x) {
        clear_cell(row, x);
    }
}

static void scroll_up(void)
{
    for (uint32_t y = 1; y < TERM_ROWS; ++y) {
        for (uint32_t x = 0; x < TERM_COLS; ++x) {
            screen_chars[y - 1][x] = screen_chars[y][x];
            screen_fg[y - 1][x] = screen_fg[y][x];
            screen_bg[y - 1][x] = screen_bg[y][x];
        }
    }
    for (uint32_t x = 0; x < TERM_COLS; ++x) {
        clear_cell(TERM_ROWS - 1, x);
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

static void execute_sgr(void)
{
    uint32_t count = csi_count;
    if (csi_has_value && count < TERM_CSI_MAX_PARAMS) {
        csi_params[count++] = csi_value;
    }
    if (count == 0) {
        reset_attrs();
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        int p = csi_params[i];
        if (p == 0) {
            reset_attrs();
        } else if (p == 1) {
            term_bold = 1;
        } else if (p == 22) {
            term_bold = 0;
        } else if (p == 39) {
            term_fg = TERM_DEFAULT_FG;
        } else if (p == 49) {
            term_bg = TERM_DEFAULT_BG;
        } else if (p >= 30 && p <= 37) {
            term_fg = term_bold ? ansi_bright_colors[p - 30] : ansi_colors[p - 30];
        } else if (p >= 40 && p <= 47) {
            term_bg = ansi_colors[p - 40];
        } else if (p >= 90 && p <= 97) {
            term_fg = ansi_bright_colors[p - 90];
        } else if (p >= 100 && p <= 107) {
            term_bg = ansi_bright_colors[p - 100];
        }
    }
}

static int csi_param_or(uint32_t index, int fallback)
{
    uint32_t count = csi_count;
    if (csi_has_value && count < TERM_CSI_MAX_PARAMS) {
        csi_params[count++] = csi_value;
    }
    if (index >= count || csi_params[index] == 0) {
        return fallback;
    }
    return csi_params[index];
}

static void execute_csi(char final)
{
    int n;
    switch (final) {
    case 'm':
        execute_sgr();
        break;
    case 'H':
    case 'f':
        n = csi_param_or(0, 1);
        cursor_row = n > 0 ? (uint32_t)(n - 1) : 0;
        n = csi_param_or(1, 1);
        cursor_col = n > 0 ? (uint32_t)(n - 1) : 0;
        clamp_cursor();
        break;
    case 'A':
        n = csi_param_or(0, 1);
        cursor_row = cursor_row > (uint32_t)n ? cursor_row - (uint32_t)n : 0;
        break;
    case 'B':
        n = csi_param_or(0, 1);
        cursor_row += (uint32_t)n;
        clamp_cursor();
        break;
    case 'C':
        n = csi_param_or(0, 1);
        cursor_col += (uint32_t)n;
        clamp_cursor();
        break;
    case 'D':
        n = csi_param_or(0, 1);
        cursor_col = cursor_col > (uint32_t)n ? cursor_col - (uint32_t)n : 0;
        break;
    case 'J':
        n = csi_param_or(0, 0);
        if (n == 2) {
            clear_screen_chars();
        } else if (n == 1) {
            for (uint32_t row = 0; row < cursor_row; ++row) {
                clear_line_range(row, 0, TERM_COLS - 1);
            }
            clear_line_range(cursor_row, 0, cursor_col);
        } else {
            clear_line_range(cursor_row, cursor_col, TERM_COLS - 1);
            for (uint32_t row = cursor_row + 1; row < TERM_ROWS; ++row) {
                clear_line_range(row, 0, TERM_COLS - 1);
            }
        }
        break;
    case 'K':
        n = csi_param_or(0, 0);
        if (n == 2) {
            clear_line_range(cursor_row, 0, TERM_COLS - 1);
        } else if (n == 1) {
            clear_line_range(cursor_row, 0, cursor_col);
        } else {
            clear_line_range(cursor_row, cursor_col, TERM_COLS - 1);
        }
        break;
    default:
        break;
    }
}

static void reset_csi(void)
{
    for (uint32_t i = 0; i < TERM_CSI_MAX_PARAMS; ++i) {
        csi_params[i] = 0;
    }
    csi_count = 0;
    csi_value = 0;
    csi_has_value = 0;
}

static void put_plain_char(char ch)
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
            clear_cell(cursor_row, cursor_col);
        }
        return;
    }
    if ((unsigned char)ch < 32) {
        return;
    }
    set_cell(cursor_row, cursor_col, ch, term_fg, term_bg);
    if (++cursor_col >= TERM_COLS) {
        newline();
    }
}

static void put_term_char(char ch)
{
    unsigned char uch = (unsigned char)ch;
    if (ansi_parser_state == ANSI_ESC) {
        if (ch == '[') {
            reset_csi();
            ansi_parser_state = ANSI_CSI;
            return;
        }
        ansi_parser_state = ANSI_NORMAL;
        return;
    }
    if (ansi_parser_state == ANSI_CSI) {
        if (uch >= '0' && uch <= '9') {
            csi_value = csi_value * 10 + (int)(uch - '0');
            csi_has_value = 1;
            return;
        }
        if (ch == ';') {
            if (csi_count < TERM_CSI_MAX_PARAMS) {
                csi_params[csi_count++] = csi_has_value ? csi_value : 0;
            }
            csi_value = 0;
            csi_has_value = 0;
            return;
        }
        if (uch >= 0x40 && uch <= 0x7e) {
            execute_csi(ch);
            ansi_parser_state = ANSI_NORMAL;
            return;
        }
        return;
    }
    if (uch == 27) {
        ansi_parser_state = ANSI_ESC;
        return;
    }
    put_plain_char(ch);
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
    leonos_ui_rect(ui, 0, 0, TERM_W, TERM_H, LEONOS_UI_WHITE);
    leonos_ui_panel(ui, 8, 8, TERM_W - 16, TERM_H - 16, 0x00000000);
    leonos_ui_text(ui, 18, 16, title_text, LEONOS_UI_WHITE, 0x00000000);
    for (uint32_t row = 0; row < TERM_ROWS; ++row) {
        for (uint32_t col = 0; col < TERM_COLS; ++col) {
            char cell[2];
            cell[0] = screen_chars[row][col];
            cell[1] = 0;
            leonos_ui_text(ui, TERM_TEXT_X + col * LEONOS_FONT_W,
                           TERM_TEXT_Y + row * LEONOS_FONT_H,
                           cell, screen_fg[row][col], screen_bg[row][col]);
        }
    }
    if (cursor_row < TERM_ROWS && cursor_col < TERM_COLS) {
        leonos_ui_rect(ui, TERM_TEXT_X + cursor_col * LEONOS_FONT_W,
                       TERM_TEXT_Y + cursor_row * LEONOS_FONT_H + LEONOS_FONT_H - 2,
                       LEONOS_FONT_W, 2, LEONOS_UI_WHITE);
    }
}

static int map_keycode(uint8_t keycode, uint8_t pressed, char *out)
{
    static uint8_t shift_down;
    if (keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT) {
        shift_down = pressed ? 1 : 0;
        return 0;
    }
    if (!pressed) {
        return 0;
    }
    return leonos_ui_keycode_to_char_shift(keycode, shift_down, out);
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

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    int window_id;
    int shell_pid;
    char *shell_argv[4];
    (void)envp;

    puts("[terminal.elf] terminal starting");
    pty_id = (uint32_t)leonos_pty_create();
    printf("[terminal.elf] pid=%d pty=%d\n", getpid(), pty_id);
    if ((int)pty_id <= 0) {
        printf("[terminal.elf] pty create failed=%d\n", (int)pty_id);
        return 1;
    }

    window_id = leonos_gui_create_app_window_ex(T("Terminal", "终端"), T("LeonOS terminal", "LeonOS 终端"),
                                                TERM_W, TERM_H, LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("[terminal.elf] create window failed=%d\n", window_id);
        return 1;
    }
    shell_argv[0] = "0:/userland/shell.elf";
    shell_argv[1] = (argc > 1 && argv && argv[1] && argv[1][0]) ? argv[1] : 0;
    shell_argv[2] = (argc > 2 && argv && argv[2] && argv[2][0]) ? argv[2] : 0;
    shell_argv[3] = 0;
    shell_pid = leonos_pty_spawn_argv("0:/userland/shell.elf", pty_id, shell_argv, 0);
    printf("[terminal.elf] spawn shell pid=%d\n", shell_pid);

    leonos_ui_bind(&ui, pixels, TERM_W, TERM_H, TERM_W);
    reset_attrs();
    clear_screen_chars();
    copy_text(title_text, sizeof(title_text), T("LeonOS Terminal", "LeonOS 终端"));
    draw_terminal(&ui);
    leonos_gui_present_window((uint32_t)window_id, TERM_W, TERM_H, TERM_W, pixels);

    for (;;) {
        event.window_id = (uint32_t)window_id;
        while (leonos_gui_wait_app_event(&event, 20U) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                leonos_pty_write_input(pty_id, "exit\n", 5);
                sleep_ms(40);
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN || event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
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
