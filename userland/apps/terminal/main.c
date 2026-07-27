#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/pty.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>

#define TERMINAL_W 760U
#define TERMINAL_H 480U
#define TERMINAL_HISTORY_ROWS 160U
#define TERMINAL_LINE_CAP 128U
#define TERMINAL_MARGIN 14U
#define TERMINAL_HEADER_H 32U
#define TERMINAL_STATUS_H 24U
#define T(en, zh) leonos_i18n((en), (zh))

enum terminal_escape_state {
    TERMINAL_TEXT,
    TERMINAL_ESCAPE,
    TERMINAL_CSI,
};

struct terminal_line {
    char text[TERMINAL_LINE_CAP];
    uint16_t length;
    uint32_t color;
};

static uint32_t pixels[TERMINAL_W * TERMINAL_H];
static struct terminal_line history[TERMINAL_HISTORY_ROWS];
static uint32_t history_first;
static uint32_t history_count;
static uint32_t active_line;
static uint32_t pty_id;
static uint32_t text_color = 0x00d7e3f4U;
static uint8_t text_bright;
static uint8_t carriage_return_pending;
static enum terminal_escape_state escape_state;
static uint16_t csi_value;
static uint8_t csi_has_value;

static const uint32_t ansi_normal[8] = {
    0x00708090U, 0x00d06060U, 0x006fc77aU, 0x00d5b85aU,
    0x007f9ee8U, 0x00c38bd7U, 0x006bc8d8U, 0x00d7e3f4U,
};

static const uint32_t ansi_bright[8] = {
    0x0092a1b2U, 0x00ff8484U, 0x008de39aU, 0x00f0d37bU,
    0x009bb7ffU, 0x00e8a5ffU, 0x0088e8faU, 0x00ffffffU,
};

static void terminal_reset_style(void)
{
    text_color = 0x00d7e3f4U;
    text_bright = 0;
}

static void terminal_clear(void)
{
    history_first = 0;
    history_count = 1;
    active_line = 0;
    history[0].length = 0;
    history[0].text[0] = 0;
    history[0].color = text_color;
    carriage_return_pending = 0;
}

static struct terminal_line *terminal_current_line(void)
{
    return &history[active_line];
}

static void terminal_next_line(void)
{
    if (history_count < TERMINAL_HISTORY_ROWS) {
        active_line = (history_first + history_count) % TERMINAL_HISTORY_ROWS;
        ++history_count;
    } else {
        history_first = (history_first + 1U) % TERMINAL_HISTORY_ROWS;
        active_line = (history_first + history_count - 1U) % TERMINAL_HISTORY_ROWS;
    }
    history[active_line].length = 0;
    history[active_line].text[0] = 0;
    history[active_line].color = text_color;
}

static void terminal_put_visible(char value)
{
    struct terminal_line *line = terminal_current_line();
    if (line->length + 1U >= TERMINAL_LINE_CAP) {
        terminal_next_line();
        line = terminal_current_line();
    }
    line->text[line->length++] = value;
    line->text[line->length] = 0;
    line->color = text_color;
}

static void terminal_put_char(char value);

static void terminal_put_text(const char *text)
{
    while (text && *text) {
        terminal_put_char(*text++);
    }
}

static void terminal_apply_sgr(uint16_t code)
{
    if (code == 0) {
        terminal_reset_style();
    } else if (code == 1) {
        text_bright = 1;
    } else if (code == 22) {
        text_bright = 0;
    } else if (code == 39) {
        text_color = 0x00d7e3f4U;
    } else if (code >= 30 && code <= 37) {
        text_color = (text_bright ? ansi_bright : ansi_normal)[code - 30U];
    } else if (code >= 90 && code <= 97) {
        text_color = ansi_bright[code - 90U];
    }
}

static void terminal_finish_csi(char final)
{
    if (final == 'm') {
        terminal_apply_sgr(csi_has_value ? csi_value : 0);
    } else if (final == 'J' && csi_value == 2) {
        terminal_clear();
    } else if (final == 'K') {
        struct terminal_line *line = terminal_current_line();
        line->length = 0;
        line->text[0] = 0;
    }
    csi_value = 0;
    csi_has_value = 0;
    escape_state = TERMINAL_TEXT;
}

static void terminal_put_char(char value)
{
    uint8_t byte = (uint8_t)value;
    if (escape_state == TERMINAL_ESCAPE) {
        if (value == '[') {
            escape_state = TERMINAL_CSI;
            csi_value = 0;
            csi_has_value = 0;
        } else {
            escape_state = TERMINAL_TEXT;
        }
        return;
    }
    if (escape_state == TERMINAL_CSI) {
        if (byte >= '0' && byte <= '9') {
            csi_value = (uint16_t)(csi_value * 10U + byte - '0');
            csi_has_value = 1;
            return;
        }
        if (value == ';') {
            terminal_apply_sgr(csi_has_value ? csi_value : 0);
            csi_value = 0;
            csi_has_value = 0;
            return;
        }
        terminal_finish_csi(value);
        return;
    }
    if (carriage_return_pending && value != '\n') {
        struct terminal_line *line = terminal_current_line();
        line->length = 0;
        line->text[0] = 0;
        carriage_return_pending = 0;
    }
    if (byte == 27U) {
        escape_state = TERMINAL_ESCAPE;
    } else if (value == '\n') {
        carriage_return_pending = 0;
        terminal_next_line();
    } else if (value == '\r') {
        carriage_return_pending = 1;
    } else if (value == '\b') {
        struct terminal_line *line = terminal_current_line();
        if (line->length) {
            line->text[--line->length] = 0;
        }
    } else if (value == '\f') {
        terminal_clear();
    } else if (value == '\t') {
        for (uint32_t index = 0; index < 4; ++index) {
            terminal_put_visible(' ');
        }
    } else if (byte >= 32U) {
        terminal_put_visible(value);
    }
}

static uint32_t terminal_visible_rows(void)
{
    uint32_t body_h = TERMINAL_H - TERMINAL_HEADER_H - TERMINAL_STATUS_H - TERMINAL_MARGIN * 2U;
    return body_h / LEONOS_FONT_H;
}

static void terminal_draw(struct leonos_ui_surface *ui)
{
    uint32_t rows = terminal_visible_rows();
    uint32_t first_visible = history_count > rows ? history_count - rows : 0;
    uint32_t body_y = TERMINAL_HEADER_H + TERMINAL_MARGIN;
    uint32_t body_h = TERMINAL_H - TERMINAL_HEADER_H - TERMINAL_STATUS_H - TERMINAL_MARGIN;
    uint32_t active_visible = history_count ? history_count - 1U : 0;
    leonos_ui_rect(ui, 0, 0, TERMINAL_W, TERMINAL_H, 0x00101822U);
    leonos_ui_rect(ui, 0, 0, TERMINAL_W, TERMINAL_HEADER_H, 0x001b2b45U);
    leonos_ui_text(ui, TERMINAL_MARGIN, 8, T("LeonOS Terminal", "LeonOS 终端"),
                   LEONOS_UI_WHITE, 0x001b2b45U);
    leonos_ui_text_transparent_clipped(ui, TERMINAL_W - 236, 8, 220,
                                       T("PTY command session", "PTY 命令会话"), 0x00a6bddbU);
    leonos_ui_inset(ui, TERMINAL_MARGIN, body_y, TERMINAL_W - TERMINAL_MARGIN * 2U,
                    body_h, 0x000b1019U);
    for (uint32_t row = first_visible; row < history_count; ++row) {
        uint32_t line_index = (history_first + row) % TERMINAL_HISTORY_ROWS;
        uint32_t draw_y = body_y + 4U + (row - first_visible) * LEONOS_FONT_H;
        leonos_ui_text_clipped(ui, TERMINAL_MARGIN + 6U, draw_y,
                               TERMINAL_W - TERMINAL_MARGIN * 2U - 12U,
                               history[line_index].text, history[line_index].color,
                               0x000b1019U);
    }
    if (active_visible >= first_visible && active_visible < history_count &&
        (leonos_uptime_ms() / 450UL) % 2UL == 0UL) {
        uint32_t line_index = active_line;
        uint32_t cursor_x = TERMINAL_MARGIN + 6U +
                            leonos_ui_text_width(history[line_index].text);
        uint32_t cursor_y = body_y + 4U + (active_visible - first_visible) * LEONOS_FONT_H;
        leonos_ui_rect(ui, cursor_x, cursor_y + LEONOS_FONT_H - 2U, LEONOS_FONT_W, 2,
                       0x00d7e3f4U);
    }
    leonos_ui_rect(ui, 0, TERMINAL_H - TERMINAL_STATUS_H, TERMINAL_W,
                   TERMINAL_STATUS_H, 0x001b2b45U);
    leonos_ui_text(ui, TERMINAL_MARGIN, TERMINAL_H - TERMINAL_STATUS_H + 4,
                   T("Ctrl+U clears the current command", "Ctrl+U 清除当前命令"),
                   0x00a6bddbU, 0x001b2b45U);
}

static int terminal_pump_output(void)
{
    char buffer[256];
    int changed = 0;
    int received;
    do {
        received = leonos_pty_read_output(pty_id, buffer, sizeof(buffer));
        if (received > 0) {
            for (int index = 0; index < received; ++index) {
                terminal_put_char(buffer[index]);
            }
            changed = 1;
        }
    } while (received > 0);
    return changed;
}

static int terminal_send_key(uint8_t keycode, uint8_t pressed, uint8_t *shift_down)
{
    char character;
    if (keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT) {
        *shift_down = pressed ? 1 : 0;
        return 0;
    }
    if (!pressed) {
        return 0;
    }
    if (keycode == LEONOS_KEY_ENTER) {
        character = '\n';
    } else if (keycode == LEONOS_KEY_BACKSPACE) {
        character = '\b';
    } else if (keycode == LEONOS_KEY_TAB) {
        character = '\t';
    } else if (!leonos_ui_keycode_to_char_shift(keycode, *shift_down, &character)) {
        return 0;
    }
    return leonos_pty_write_input(pty_id, &character, 1) > 0;
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    char *shell_argv[4];
    uint8_t shift_down = 0;
    uint8_t cursor_phase = 2;
    int window_id;
    int shell_result;
    (void)envp;
    pty_id = (uint32_t)leonos_pty_create();
    if ((int)pty_id <= 0) {
        printf("terminal: PTY creation failed (%d)\n", (int)pty_id);
        return 1;
    }
    window_id = leonos_gui_create_app_window_ex(T("Terminal", "终端"),
                                                T("PTY command terminal", "PTY 命令终端"),
                                                TERMINAL_W, TERMINAL_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("terminal: window creation failed (%d)\n", window_id);
        return 1;
    }
    shell_argv[0] = "0:/system/apps/shell/shell.elf";
    shell_argv[1] = argc > 1 && argv[1] && argv[1][0] ? argv[1] : 0;
    shell_argv[2] = argc > 2 && argv[2] && argv[2][0] ? argv[2] : 0;
    shell_argv[3] = 0;
    shell_result = leonos_pty_spawn_argv(shell_argv[0], pty_id, shell_argv, 0);
    leonos_ui_bind(&ui, pixels, TERMINAL_W, TERMINAL_H, TERMINAL_W);
    terminal_reset_style();
    terminal_clear();
    if (shell_result < 0) {
        terminal_put_text("! shell start failed");
    }
    terminal_draw(&ui);
    leonos_gui_present_window((uint32_t)window_id, TERMINAL_W, TERMINAL_H,
                              TERMINAL_W, pixels);
    for (;;) {
        int redraw = terminal_pump_output();
        uint8_t next_cursor_phase = (uint8_t)((leonos_uptime_ms() / 450UL) % 2UL);
        if (next_cursor_phase != cursor_phase) {
            cursor_phase = next_cursor_phase;
            redraw = 1;
        }
        event.window_id = (uint32_t)window_id;
        if (leonos_gui_wait_app_event(&event, 40U) > 0) {
            if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
                return 0;
            }
            if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN) {
                redraw |= terminal_send_key(event.keycode, 1, &shift_down);
            } else if (event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                (void)terminal_send_key(event.keycode, 0, &shift_down);
            } else if (event.type == LEONOS_GUI_APP_EVENT_THEME_CHANGED ||
                       event.type == LEONOS_GUI_APP_EVENT_FOCUS) {
                redraw = 1;
            }
        }
        if (redraw) {
            terminal_draw(&ui);
            leonos_gui_present_window((uint32_t)window_id, TERMINAL_W, TERMINAL_H,
                                      TERMINAL_W, pixels);
        }
    }
}
