#include <leonos/gui.h>
#include <leonos/i18n.h>
#include <leonos/pty.h>
#include <leonos/psf_font.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdio.h>
#include <termios.h>

#define TERMINAL_W 760U
#define TERMINAL_H 480U
#define TERMINAL_HISTORY_ROWS 160U
#define TERMINAL_MARGIN 14U
#define TERMINAL_HEADER_H 32U
#define TERMINAL_STATUS_H 24U
#define TERMINAL_BODY_X (TERMINAL_MARGIN + 6U)
#define TERMINAL_BODY_Y (TERMINAL_HEADER_H + TERMINAL_MARGIN)
#define TERMINAL_BODY_W (TERMINAL_W - TERMINAL_MARGIN * 2U)
#define TERMINAL_BODY_H (TERMINAL_H - TERMINAL_HEADER_H - TERMINAL_STATUS_H - TERMINAL_MARGIN * 2U)
#define TERMINAL_COLUMNS ((TERMINAL_BODY_W - 12U) / LEONOS_FONT_W)
#define TERMINAL_CSI_PARAM_CAP 12U
/* Keep a complete typical curses redraw in one terminal frame.  Smaller
 * batches make full-screen programs redraw and present the window repeatedly. */
#define TERMINAL_OUTPUT_BUDGET 8192U
#define TERMINAL_CELL_CONTINUATION 0xffffffffU
#define T(en, zh) leonos_i18n((en), (zh))

enum terminal_escape_state {
    TERMINAL_TEXT,
    TERMINAL_ESCAPE,
    TERMINAL_CSI,
};

struct terminal_cell {
    uint32_t codepoint;
    uint32_t foreground;
    uint32_t background;
};

struct terminal_line {
    struct terminal_cell cells[TERMINAL_COLUMNS];
};

struct terminal_screen_snapshot {
    struct terminal_line lines[TERMINAL_HISTORY_ROWS];
    uint32_t history_first;
    uint32_t history_count;
    uint32_t active_line;
    uint32_t cursor_column;
    uint32_t saved_line;
    uint32_t saved_column;
    uint32_t text_foreground;
    uint32_t text_background;
    int8_t foreground_index;
    int8_t background_index;
    uint8_t text_bright;
    uint8_t text_dim;
    uint8_t text_inverse;
    uint8_t background_bright;
    uint8_t cursor_visible;
    uint8_t saved_cursor_valid;
};

static uint32_t pixels[TERMINAL_W * TERMINAL_H];
static struct terminal_line history[TERMINAL_HISTORY_ROWS];
static struct terminal_screen_snapshot normal_screen;
static uint32_t history_first;
static uint32_t history_count;
static uint32_t active_line;
static uint32_t cursor_column;
static uint32_t saved_line;
static uint32_t saved_column;
static uint32_t pty_id;
static uint32_t text_foreground = 0x00d7e3f4U;
static uint32_t text_background = 0x000b1019U;
static int8_t foreground_index = -1;
static int8_t background_index = -1;
static uint8_t text_bright;
static uint8_t text_dim;
static uint8_t text_inverse;
static uint8_t background_bright;
static uint8_t cursor_visible = 1;
static uint8_t saved_cursor_valid;
static enum terminal_escape_state escape_state;
static uint16_t csi_params[TERMINAL_CSI_PARAM_CAP];
static uint8_t csi_param_count;
static uint8_t csi_private;
static uint8_t utf8_bytes[4];
static uint8_t utf8_length;
static uint8_t utf8_expected;
static uint8_t alternate_screen_active;

static uint32_t terminal_visible_rows(void);
static void terminal_flush_utf8(void);
static int terminal_write_input(const char *buffer, uint32_t length);

static const uint32_t ansi_normal[8] = {
    0x00708090U, 0x00d06060U, 0x006fc77aU, 0x00d5b85aU,
    0x007f9ee8U, 0x00c38bd7U, 0x006bc8d8U, 0x00d7e3f4U,
};

static const uint32_t ansi_bright[8] = {
    0x0092a1b2U, 0x00ff8484U, 0x008de39aU, 0x00f0d37bU,
    0x009bb7ffU, 0x00e8a5ffU, 0x0088e8faU, 0x00ffffffU,
};

static int terminal_arg_eq(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static uint32_t terminal_dim_color(uint32_t color)
{
    return (color & 0x00fefefeU) >> 1U;
}

static uint32_t terminal_style_foreground(void)
{
    uint32_t color = foreground_index < 0 ? text_foreground :
                     (text_bright ? ansi_bright : ansi_normal)[(uint8_t)foreground_index];
    if (text_dim) {
        color = terminal_dim_color(color);
    }
    if (text_inverse) {
        return background_index < 0 ? text_background :
               (background_bright ? ansi_bright : ansi_normal)[(uint8_t)background_index];
    }
    return color;
}

static uint32_t terminal_style_background(void)
{
    uint32_t color = background_index < 0 ? text_background :
                     (background_bright ? ansi_bright : ansi_normal)[(uint8_t)background_index];
    if (!text_inverse) {
        return color;
    }
    color = foreground_index < 0 ? text_foreground :
            (text_bright ? ansi_bright : ansi_normal)[(uint8_t)foreground_index];
    return text_dim ? terminal_dim_color(color) : color;
}

static void terminal_reset_style(void)
{
    text_foreground = 0x00d7e3f4U;
    text_background = 0x000b1019U;
    foreground_index = -1;
    background_index = -1;
    text_bright = 0;
    text_dim = 0;
    text_inverse = 0;
    background_bright = 0;
}

static void terminal_save_normal_screen(void)
{
    uint32_t row;
    for (row = 0; row < TERMINAL_HISTORY_ROWS; ++row) {
        normal_screen.lines[row] = history[row];
    }
    normal_screen.history_first = history_first;
    normal_screen.history_count = history_count;
    normal_screen.active_line = active_line;
    normal_screen.cursor_column = cursor_column;
    normal_screen.saved_line = saved_line;
    normal_screen.saved_column = saved_column;
    normal_screen.text_foreground = text_foreground;
    normal_screen.text_background = text_background;
    normal_screen.foreground_index = foreground_index;
    normal_screen.background_index = background_index;
    normal_screen.text_bright = text_bright;
    normal_screen.text_dim = text_dim;
    normal_screen.text_inverse = text_inverse;
    normal_screen.background_bright = background_bright;
    normal_screen.cursor_visible = cursor_visible;
    normal_screen.saved_cursor_valid = saved_cursor_valid;
}

static void terminal_restore_normal_screen(void)
{
    uint32_t row;
    for (row = 0; row < TERMINAL_HISTORY_ROWS; ++row) {
        history[row] = normal_screen.lines[row];
    }
    history_first = normal_screen.history_first;
    history_count = normal_screen.history_count;
    active_line = normal_screen.active_line;
    cursor_column = normal_screen.cursor_column;
    saved_line = normal_screen.saved_line;
    saved_column = normal_screen.saved_column;
    text_foreground = normal_screen.text_foreground;
    text_background = normal_screen.text_background;
    foreground_index = normal_screen.foreground_index;
    background_index = normal_screen.background_index;
    text_bright = normal_screen.text_bright;
    text_dim = normal_screen.text_dim;
    text_inverse = normal_screen.text_inverse;
    background_bright = normal_screen.background_bright;
    cursor_visible = normal_screen.cursor_visible;
    saved_cursor_valid = normal_screen.saved_cursor_valid;
}

static void terminal_blank_cell(struct terminal_cell *cell)
{
    cell->codepoint = 0;
    cell->foreground = terminal_style_foreground();
    cell->background = terminal_style_background();
}

static void terminal_reset_line(struct terminal_line *line)
{
    uint32_t column;
    for (column = 0; column < TERMINAL_COLUMNS; ++column) {
        terminal_blank_cell(&line->cells[column]);
    }
}

static uint32_t terminal_logical_active(void)
{
    uint32_t row;
    for (row = 0; row < history_count; ++row) {
        if ((history_first + row) % TERMINAL_HISTORY_ROWS == active_line) {
            return row;
        }
    }
    return history_count ? history_count - 1U : 0;
}

static void terminal_select_logical(uint32_t row)
{
    if (history_count == 0) {
        return;
    }
    if (row >= history_count) {
        row = history_count - 1U;
    }
    active_line = (history_first + row) % TERMINAL_HISTORY_ROWS;
}

static struct terminal_line *terminal_current_line(void)
{
    return &history[active_line];
}

static void terminal_append_line(void)
{
    if (history_count < TERMINAL_HISTORY_ROWS) {
        active_line = (history_first + history_count) % TERMINAL_HISTORY_ROWS;
        ++history_count;
    } else {
        if (saved_cursor_valid && saved_line == history_first) {
            saved_cursor_valid = 0;
        }
        history_first = (history_first + 1U) % TERMINAL_HISTORY_ROWS;
        active_line = (history_first + history_count - 1U) % TERMINAL_HISTORY_ROWS;
    }
    cursor_column = 0;
    terminal_reset_line(terminal_current_line());
}

static void terminal_newline(void)
{
    uint32_t row = terminal_logical_active();
    if (row + 1U < history_count) {
        terminal_select_logical(row + 1U);
        cursor_column = 0;
        return;
    }
    terminal_append_line();
}

static void terminal_clear(void)
{
    uint32_t row;
    history_first = 0;
    history_count = 1;
    active_line = 0;
    cursor_column = 0;
    saved_cursor_valid = 0;
    for (row = 0; row < TERMINAL_HISTORY_ROWS; ++row) {
        terminal_reset_line(&history[row]);
    }
}

static void terminal_enter_alternate_screen(void)
{
    if (alternate_screen_active) {
        return;
    }
    terminal_flush_utf8();
    terminal_save_normal_screen();
    alternate_screen_active = 1;
    terminal_reset_style();
    terminal_clear();
}

static void terminal_leave_alternate_screen(void)
{
    if (!alternate_screen_active) {
        return;
    }
    terminal_flush_utf8();
    terminal_restore_normal_screen();
    alternate_screen_active = 0;
}

static uint32_t terminal_codepoint_width(uint32_t codepoint)
{
    if (codepoint >= 0x1100U &&
        (codepoint <= 0x115fU ||
         (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) ||
         (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
         (codepoint >= 0xf900U && codepoint <= 0xfaffU) ||
         (codepoint >= 0xfe10U && codepoint <= 0xfe6fU) ||
         (codepoint >= 0xff01U && codepoint <= 0xff60U) ||
         (codepoint >= 0xffe0U && codepoint <= 0xffe6U))) {
        return 2;
    }
    return 1;
}

static void terminal_prepare_cell(struct terminal_line *line, uint32_t column)
{
    if (column >= TERMINAL_COLUMNS) {
        return;
    }
    if (line->cells[column].codepoint == TERMINAL_CELL_CONTINUATION && column > 0) {
        terminal_blank_cell(&line->cells[column - 1U]);
    }
    if (column + 1U < TERMINAL_COLUMNS &&
        line->cells[column + 1U].codepoint == TERMINAL_CELL_CONTINUATION) {
        terminal_blank_cell(&line->cells[column + 1U]);
    }
    terminal_blank_cell(&line->cells[column]);
}

static void terminal_put_codepoint(uint32_t codepoint)
{
    struct terminal_line *line;
    uint32_t width = terminal_codepoint_width(codepoint);
    uint32_t column;
    if (width > TERMINAL_COLUMNS) {
        codepoint = '?';
        width = 1;
    }
    if (cursor_column >= TERMINAL_COLUMNS || cursor_column + width > TERMINAL_COLUMNS) {
        terminal_newline();
    }
    line = terminal_current_line();
    terminal_prepare_cell(line, cursor_column);
    line->cells[cursor_column].codepoint = codepoint;
    line->cells[cursor_column].foreground = terminal_style_foreground();
    line->cells[cursor_column].background = terminal_style_background();
    for (column = 1; column < width; ++column) {
        terminal_prepare_cell(line, cursor_column + column);
        line->cells[cursor_column + column].codepoint = TERMINAL_CELL_CONTINUATION;
        line->cells[cursor_column + column].foreground = line->cells[cursor_column].foreground;
        line->cells[cursor_column + column].background = line->cells[cursor_column].background;
    }
    cursor_column += width;
}

static void terminal_flush_utf8(void)
{
    if (utf8_length) {
        terminal_put_codepoint('?');
    }
    utf8_length = 0;
    utf8_expected = 0;
}

static void terminal_put_utf8_byte(uint8_t byte)
{
    uint32_t codepoint;
    if (byte < 0x80U) {
        terminal_flush_utf8();
        terminal_put_codepoint(byte);
        return;
    }
    if (utf8_length == 0) {
        if (byte >= 0xc2U && byte <= 0xdfU) {
            utf8_expected = 2;
        } else if (byte >= 0xe0U && byte <= 0xefU) {
            utf8_expected = 3;
        } else if (byte >= 0xf0U && byte <= 0xf4U) {
            utf8_expected = 4;
        } else {
            terminal_put_codepoint('?');
            return;
        }
        utf8_bytes[0] = byte;
        utf8_length = 1;
        return;
    }
    if ((byte & 0xc0U) != 0x80U) {
        terminal_flush_utf8();
        terminal_put_utf8_byte(byte);
        return;
    }
    utf8_bytes[utf8_length++] = byte;
    if (utf8_length != utf8_expected) {
        return;
    }
    if (utf8_expected == 2) {
        codepoint = ((uint32_t)(utf8_bytes[0] & 0x1fU) << 6U) |
                    (uint32_t)(utf8_bytes[1] & 0x3fU);
    } else if (utf8_expected == 3) {
        codepoint = ((uint32_t)(utf8_bytes[0] & 0x0fU) << 12U) |
                    ((uint32_t)(utf8_bytes[1] & 0x3fU) << 6U) |
                    (uint32_t)(utf8_bytes[2] & 0x3fU);
    } else {
        codepoint = ((uint32_t)(utf8_bytes[0] & 0x07U) << 18U) |
                    ((uint32_t)(utf8_bytes[1] & 0x3fU) << 12U) |
                    ((uint32_t)(utf8_bytes[2] & 0x3fU) << 6U) |
                    (uint32_t)(utf8_bytes[3] & 0x3fU);
    }
    utf8_length = 0;
    utf8_expected = 0;
    if (codepoint >= 0x110000U || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
        codepoint = '?';
    }
    terminal_put_codepoint(codepoint);
}

static void terminal_move_up(uint32_t amount)
{
    uint32_t row = terminal_logical_active();
    terminal_select_logical(row > amount ? row - amount : 0);
}

static void terminal_move_down(uint32_t amount)
{
    uint32_t row = terminal_logical_active();
    uint32_t target = row + amount;
    while (target >= history_count) {
        terminal_append_line();
    }
    terminal_select_logical(target);
}

static void terminal_move_home(uint32_t row, uint32_t column)
{
    uint32_t visible_rows = TERMINAL_BODY_H / LEONOS_FONT_H;
    uint32_t first_visible = history_count > visible_rows ? history_count - visible_rows : 0;
    uint32_t target;
    if (row == 0) {
        row = 1;
    }
    if (column == 0) {
        column = 1;
    }
    if (row > visible_rows) {
        row = visible_rows;
    }
    target = first_visible + row - 1U;
    while (target >= history_count) {
        terminal_append_line();
    }
    terminal_select_logical(target);
    cursor_column = column - 1U;
    if (cursor_column >= TERMINAL_COLUMNS) {
        cursor_column = TERMINAL_COLUMNS - 1U;
    }
}

static void terminal_erase_line(uint32_t mode)
{
    struct terminal_line *line = terminal_current_line();
    uint32_t first = 0;
    uint32_t last = TERMINAL_COLUMNS;
    uint32_t column;
    if (mode == 0) {
        first = cursor_column;
    } else if (mode == 1) {
        last = cursor_column < TERMINAL_COLUMNS ? cursor_column + 1U : TERMINAL_COLUMNS;
    }
    if (first > TERMINAL_COLUMNS) {
        first = TERMINAL_COLUMNS;
    }
    for (column = first; column < last; ++column) {
        terminal_blank_cell(&line->cells[column]);
    }
}

static void terminal_erase_display(uint32_t mode)
{
    uint32_t row = terminal_logical_active();
    uint32_t index;
    if (mode == 2) {
        terminal_clear();
        return;
    }
    if (mode == 0) {
        terminal_erase_line(0);
        for (index = row + 1U; index < history_count; ++index) {
            terminal_reset_line(&history[(history_first + index) % TERMINAL_HISTORY_ROWS]);
        }
    } else if (mode == 1) {
        terminal_erase_line(1);
        for (index = 0; index < row; ++index) {
            terminal_reset_line(&history[(history_first + index) % TERMINAL_HISTORY_ROWS]);
        }
    }
}

static void terminal_save_cursor(void)
{
    saved_line = active_line;
    saved_column = cursor_column;
    saved_cursor_valid = 1;
}

static void terminal_restore_cursor(void)
{
    uint32_t row;
    if (!saved_cursor_valid) {
        return;
    }
    for (row = 0; row < history_count; ++row) {
        if ((history_first + row) % TERMINAL_HISTORY_ROWS == saved_line) {
            active_line = saved_line;
            cursor_column = saved_column < TERMINAL_COLUMNS ? saved_column : TERMINAL_COLUMNS;
            return;
        }
    }
    saved_cursor_valid = 0;
}

static void terminal_apply_sgr(uint16_t code)
{
    if (code == 0) {
        terminal_reset_style();
    } else if (code == 1) {
        text_bright = 1;
        text_dim = 0;
    } else if (code == 2) {
        text_dim = 1;
        text_bright = 0;
    } else if (code == 7) {
        text_inverse = 1;
    } else if (code == 22) {
        text_bright = 0;
        text_dim = 0;
    } else if (code == 27) {
        text_inverse = 0;
    } else if (code == 39) {
        foreground_index = -1;
    } else if (code == 49) {
        background_index = -1;
    } else if (code >= 30 && code <= 37) {
        foreground_index = (int8_t)(code - 30U);
    } else if (code >= 40 && code <= 47) {
        background_index = (int8_t)(code - 40U);
        background_bright = 0;
    } else if (code >= 90 && code <= 97) {
        foreground_index = (int8_t)(code - 90U);
        text_bright = 1;
    } else if (code >= 100 && code <= 107) {
        background_index = (int8_t)(code - 100U);
        background_bright = 1;
    }
}

static void terminal_csi_begin(void)
{
    uint32_t index;
    for (index = 0; index < TERMINAL_CSI_PARAM_CAP; ++index) {
        csi_params[index] = 0;
    }
    csi_param_count = 1;
    csi_private = 0;
}

static uint16_t terminal_csi_param(uint32_t index, uint16_t default_value)
{
    if (index >= csi_param_count || csi_params[index] == 0) {
        return default_value;
    }
    return csi_params[index];
}

static void terminal_finish_csi(char final)
{
    uint16_t amount = terminal_csi_param(0, 1);
    struct terminal_line *line;
    uint32_t column;
    if (final == 'm') {
        uint32_t index;
        for (index = 0; index < csi_param_count; ++index) {
            terminal_apply_sgr(csi_params[index]);
        }
    } else if (final == 'A') {
        terminal_move_up(amount);
    } else if (final == 'B') {
        terminal_move_down(amount);
    } else if (final == 'C') {
        cursor_column += amount;
        if (cursor_column > TERMINAL_COLUMNS) {
            cursor_column = TERMINAL_COLUMNS;
        }
    } else if (final == 'D') {
        cursor_column = cursor_column > amount ? cursor_column - amount : 0;
    } else if (final == 'E') {
        terminal_move_down(amount);
        cursor_column = 0;
    } else if (final == 'F') {
        terminal_move_up(amount);
        cursor_column = 0;
    } else if (final == 'a') {
        cursor_column += amount;
        if (cursor_column > TERMINAL_COLUMNS) {
            cursor_column = TERMINAL_COLUMNS;
        }
    } else if (final == 'G') {
        cursor_column = amount - 1U;
        if (cursor_column >= TERMINAL_COLUMNS) {
            cursor_column = TERMINAL_COLUMNS - 1U;
        }
    } else if (final == 'H' || final == 'f') {
        terminal_move_home(terminal_csi_param(0, 1), terminal_csi_param(1, 1));
    } else if (final == 'J') {
        terminal_erase_display(csi_params[0]);
    } else if (final == 'K') {
        terminal_erase_line(csi_params[0]);
    } else if (final == '@') {
        line = terminal_current_line();
        if (amount > TERMINAL_COLUMNS - cursor_column) {
            amount = (uint16_t)(TERMINAL_COLUMNS - cursor_column);
        }
        for (column = TERMINAL_COLUMNS; column-- > cursor_column + amount;) {
            line->cells[column] = line->cells[column - amount];
        }
        for (column = cursor_column; column < cursor_column + amount; ++column) {
            terminal_blank_cell(&line->cells[column]);
        }
    } else if (final == 'P') {
        line = terminal_current_line();
        if (amount > TERMINAL_COLUMNS - cursor_column) {
            amount = (uint16_t)(TERMINAL_COLUMNS - cursor_column);
        }
        for (column = cursor_column; column + amount < TERMINAL_COLUMNS; ++column) {
            line->cells[column] = line->cells[column + amount];
        }
        for (; column < TERMINAL_COLUMNS; ++column) {
            terminal_blank_cell(&line->cells[column]);
        }
    } else if (final == 'X') {
        line = terminal_current_line();
        if (amount > TERMINAL_COLUMNS - cursor_column) {
            amount = (uint16_t)(TERMINAL_COLUMNS - cursor_column);
        }
        for (column = cursor_column; column < cursor_column + amount; ++column) {
            terminal_blank_cell(&line->cells[column]);
        }
    } else if (final == 'd') {
        terminal_move_home(amount, 1);
    } else if (final == 'n' && csi_params[0] == 6) {
        char response[32];
        uint32_t row = terminal_logical_active();
        uint32_t visible_rows = terminal_visible_rows();
        uint32_t first_visible = history_count > visible_rows ? history_count - visible_rows : 0;
        int length;
        row = row >= first_visible ? row - first_visible + 1U : 1U;
        length = snprintf(response, sizeof(response), "\033[%u;%uR", row,
                          cursor_column + 1U);
        if (length > 0) {
            (void)leonos_pty_write_input(pty_id, response, (uint32_t)length);
        }
    } else if (final == 's') {
        terminal_save_cursor();
    } else if (final == 'u') {
        terminal_restore_cursor();
    } else if ((final == 'h' || final == 'l') && csi_private && csi_params[0] == 1049) {
        if (final == 'h') {
            terminal_enter_alternate_screen();
        } else {
            terminal_leave_alternate_screen();
        }
    } else if ((final == 'h' || final == 'l') && csi_private && csi_params[0] == 25) {
        cursor_visible = final == 'h';
    }
    escape_state = TERMINAL_TEXT;
}

static void terminal_put_char(char value)
{
    uint8_t byte = (uint8_t)value;
    if (escape_state == TERMINAL_ESCAPE) {
        if (value == '[') {
            escape_state = TERMINAL_CSI;
            terminal_csi_begin();
        } else {
            if (value == '7') {
                terminal_save_cursor();
            } else if (value == '8') {
                terminal_restore_cursor();
            } else if (value == 'c') {
                terminal_reset_style();
                terminal_clear();
                cursor_visible = 1;
            } else if (value == 'D') {
                terminal_newline();
            }
            escape_state = TERMINAL_TEXT;
        }
        return;
    }
    if (escape_state == TERMINAL_CSI) {
        if (byte >= '0' && byte <= '9') {
            uint16_t *value_slot = &csi_params[csi_param_count - 1U];
            if (*value_slot < 6553U) {
                *value_slot = (uint16_t)(*value_slot * 10U + byte - '0');
            }
            return;
        }
        if (byte == ';') {
            if (csi_param_count < TERMINAL_CSI_PARAM_CAP) {
                ++csi_param_count;
            }
            return;
        }
        if (byte == '?' && csi_param_count == 1U && csi_params[0] == 0) {
            csi_private = 1;
            return;
        }
        terminal_finish_csi(value);
        return;
    }
    if (byte == 27U) {
        terminal_flush_utf8();
        escape_state = TERMINAL_ESCAPE;
    } else if (value == '\n') {
        terminal_flush_utf8();
        terminal_newline();
    } else if (value == '\r') {
        terminal_flush_utf8();
        cursor_column = 0;
    } else if (value == '\b') {
        terminal_flush_utf8();
        if (cursor_column) {
            --cursor_column;
        }
    } else if (value == '\f') {
        terminal_flush_utf8();
        terminal_clear();
    } else if (value == '\t') {
        uint32_t next_stop;
        terminal_flush_utf8();
        next_stop = (cursor_column + 4U) & ~3U;
        while (cursor_column < next_stop) {
            terminal_put_codepoint(' ');
        }
    } else if (byte >= 32U) {
        terminal_put_utf8_byte(byte);
    }
}

static void terminal_put_text(const char *text)
{
    while (text && *text) {
        terminal_put_char(*text++);
    }
}

static uint32_t terminal_visible_rows(void)
{
    return TERMINAL_BODY_H / LEONOS_FONT_H;
}

static void terminal_draw_monospace_glyph(struct leonos_ui_surface *ui,
                                          uint32_t x, uint32_t y,
                                          uint32_t codepoint, uint32_t width,
                                          uint32_t foreground,
                                          uint32_t background)
{
    const uint8_t *glyph;
    if (!ui || !ui->pixels || codepoint > 0xffU || width != 1U ||
        x + LEONOS_FONT_W > ui->width || y + LEONOS_FONT_H > ui->height) {
        leonos_ui_codepoint(ui, x, y, codepoint, width, foreground, background);
        return;
    }

    /* The embedded PSF font is an 8x16 bitmap, so every terminal cell has
     * the same advance regardless of the active desktop UI theme. */
    glyph = leonos_psf_glyph((char)codepoint);
    for (uint32_t row = 0; row < LEONOS_FONT_H; ++row) {
        uint8_t bits = glyph[row];
        uint32_t *pixels_row = ui->pixels + (uint64_t)(y + row) * ui->stride + x;
        for (uint32_t column = 0; column < LEONOS_FONT_W; ++column) {
            if (bits & (uint8_t)(0x80U >> column)) {
                pixels_row[column] = foreground;
            }
        }
    }
}

static void terminal_draw_line(struct leonos_ui_surface *ui, uint32_t y,
                               const struct terminal_line *line)
{
    uint32_t column;
    for (column = 0; column < TERMINAL_COLUMNS; ++column) {
        const struct terminal_cell *cell = &line->cells[column];
        uint32_t width = 1;
        uint32_t x = TERMINAL_BODY_X + column * LEONOS_FONT_W;
        if (cell->codepoint == TERMINAL_CELL_CONTINUATION) {
            continue;
        }
        if (column + 1U < TERMINAL_COLUMNS &&
            line->cells[column + 1U].codepoint == TERMINAL_CELL_CONTINUATION) {
            width = 2;
        }
        leonos_ui_rect(ui, x, y, width * LEONOS_FONT_W, LEONOS_FONT_H,
                       cell->background);
        if (cell->codepoint) {
            terminal_draw_monospace_glyph(ui, x, y, cell->codepoint, width,
                                          cell->foreground, cell->background);
        }
    }
}

static void terminal_draw(struct leonos_ui_surface *ui)
{
    uint32_t rows = terminal_visible_rows();
    uint32_t first_visible = history_count > rows ? history_count - rows : 0;
    uint32_t active_visible = terminal_logical_active();
    uint32_t row;
    leonos_ui_rect(ui, 0, 0, TERMINAL_W, TERMINAL_H, 0x00101822U);
    leonos_ui_rect(ui, 0, 0, TERMINAL_W, TERMINAL_HEADER_H, 0x001b2b45U);
    leonos_ui_text(ui, TERMINAL_MARGIN, 8, T("LeonOS Terminal", "LeonOS 终端"),
                   LEONOS_UI_WHITE, 0x001b2b45U);
    leonos_ui_text_transparent_clipped(ui, TERMINAL_W - 236, 8, 220,
                                       T("PTY command session", "PTY 命令会话"), 0x00a6bddbU);
    leonos_ui_inset(ui, TERMINAL_MARGIN, TERMINAL_BODY_Y, TERMINAL_BODY_W,
                    TERMINAL_BODY_H, 0x000b1019U);
    for (row = 0; row < rows && first_visible + row < history_count; ++row) {
        uint32_t line_index = (history_first + first_visible + row) % TERMINAL_HISTORY_ROWS;
        terminal_draw_line(ui, TERMINAL_BODY_Y + 4U + row * LEONOS_FONT_H,
                           &history[line_index]);
    }
    if (cursor_visible && active_visible >= first_visible &&
        active_visible < first_visible + rows &&
        (leonos_uptime_ms() / 450UL) % 2UL == 0UL) {
        uint32_t cursor_x = TERMINAL_BODY_X +
                            (cursor_column < TERMINAL_COLUMNS ? cursor_column : TERMINAL_COLUMNS - 1U) * LEONOS_FONT_W;
        uint32_t cursor_y = TERMINAL_BODY_Y + 4U +
                            (active_visible - first_visible) * LEONOS_FONT_H;
        leonos_ui_rect(ui, cursor_x, cursor_y + LEONOS_FONT_H - 2U,
                       LEONOS_FONT_W, 2, terminal_style_foreground());
    }
    leonos_ui_rect(ui, 0, TERMINAL_H - TERMINAL_STATUS_H, TERMINAL_W,
                   TERMINAL_STATUS_H, 0x001b2b45U);
    leonos_ui_text(ui, TERMINAL_MARGIN, TERMINAL_H - TERMINAL_STATUS_H + 4,
                   T("Ctrl+U clears the current command", "Ctrl+U 清除当前命令"),
                   0x00a6bddbU, 0x001b2b45U);
}

static int terminal_pump_output(void)
{
    char buffer[1024];
    uint32_t consumed = 0;
    int changed = 0;
    int received;
    do {
        uint32_t request = TERMINAL_OUTPUT_BUDGET - consumed;
        if (request > sizeof(buffer)) {
            request = sizeof(buffer);
        }
        received = leonos_pty_read_output(pty_id, buffer, request);
        if (received > 0) {
            int index;
            for (index = 0; index < received; ++index) {
                terminal_put_char(buffer[index]);
            }
            consumed += (uint32_t)received;
            changed = 1;
        }
    } while (received > 0 && consumed < TERMINAL_OUTPUT_BUDGET);
    return changed;
}

static int terminal_pty_echo_enabled(void)
{
    struct leonos_pty_termios termios;
    if (leonos_pty_get_termios(pty_id, &termios) < 0) {
        return 1;
    }
    return (termios.c_lflag & ECHO) != 0;
}

static char terminal_pty_echo_character(char character)
{
    struct leonos_pty_termios termios;
    if (character == '\r' &&
        leonos_pty_get_termios(pty_id, &termios) == 0 &&
        (termios.c_iflag & LEONOS_PTY_IFLAG_ICRNL)) {
        return '\n';
    }
    return character;
}

static int terminal_write_input(const char *buffer, uint32_t length)
{
    return leonos_pty_write_input(pty_id, buffer, length) == (int)length;
}

static int terminal_control_character(char character, uint8_t ctrl, char *out)
{
    if (!out) {
        return 0;
    }
    if (!ctrl) {
        *out = character;
        return 1;
    }
    if (character >= 'a' && character <= 'z') {
        *out = (char)(character - 'a' + 1);
        return 1;
    }
    if (character >= 'A' && character <= 'Z') {
        *out = (char)(character - 'A' + 1);
        return 1;
    }
    switch (character) {
    case '@': *out = 0; return 1;
    case '[': *out = 27; return 1;
    case '\\': *out = 28; return 1;
    case ']': *out = 29; return 1;
    case '^': *out = 30; return 1;
    case '_': *out = 31; return 1;
    default: return 0;
    }
}

static int terminal_send_key(uint8_t keycode, uint8_t pressed,
                             uint8_t *shift_down, uint8_t *ctrl_down,
                             uint8_t *alt_down)
{
    char character;
    char sequence[8];
    uint32_t sequence_length = 0;
    int local_echo;
    if (keycode == LEONOS_KEY_LEFT_SHIFT || keycode == LEONOS_KEY_RIGHT_SHIFT) {
        *shift_down = pressed ? 1 : 0;
        return 0;
    }
    if (keycode == LEONOS_KEY_LEFT_CTRL || keycode == LEONOS_KEY_RIGHT_CTRL) {
        *ctrl_down = pressed ? 1 : 0;
        return 0;
    }
    if (keycode == LEONOS_KEY_LEFT_ALT || keycode == LEONOS_KEY_RIGHT_ALT) {
        *alt_down = pressed ? 1 : 0;
        return 0;
    }
    if (!pressed) {
        return 0;
    }
    if (keycode == LEONOS_KEY_ESCAPE) {
        sequence[0] = '\033';
        sequence_length = 1;
    } else if (keycode == LEONOS_KEY_UP) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'A'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_DOWN) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'B'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_RIGHT) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'C'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_LEFT) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'D'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_HOME) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'H'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_END) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = 'F'; sequence_length = 3;
    } else if (keycode == LEONOS_KEY_INSERT) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = '2'; sequence[3] = '~'; sequence_length = 4;
    } else if (keycode == LEONOS_KEY_DELETE) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = '3'; sequence[3] = '~'; sequence_length = 4;
    } else if (keycode == LEONOS_KEY_PAGE_UP) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = '5'; sequence[3] = '~'; sequence_length = 4;
    } else if (keycode == LEONOS_KEY_PAGE_DOWN) {
        sequence[0] = '\033'; sequence[1] = '['; sequence[2] = '6'; sequence[3] = '~'; sequence_length = 4;
    } else if (keycode == LEONOS_KEY_ENTER) {
        /* A terminal Enter key emits CR.  Canonical PTYs translate it to LF
         * for shell input, while raw-mode programs such as nano receive ^M. */
        character = '\r';
    } else if (keycode == LEONOS_KEY_BACKSPACE) {
        character = '\b';
    } else if (keycode == LEONOS_KEY_TAB) {
        character = '\t';
    } else if (!leonos_ui_keycode_to_char_shift(keycode, *shift_down, &character)) {
        return 0;
    } else if (!terminal_control_character(character, *ctrl_down, &character)) {
        return 0;
    }
    if (sequence_length) {
        if (*ctrl_down && sequence_length == 3U && sequence[2] >= 'A' && sequence[2] <= 'D') {
            sequence[2] = (char)(sequence[2] - 'A' + 'A');
            sequence[1] = '[';
            /* BusyBox read_key understands the standard Ctrl-arrow form. */
            sequence[2] = '1'; sequence[3] = ';'; sequence[4] = '5';
            sequence[5] = (char)(keycode == LEONOS_KEY_UP ? 'A' :
                                  keycode == LEONOS_KEY_DOWN ? 'B' :
                                  keycode == LEONOS_KEY_RIGHT ? 'C' : 'D');
            sequence_length = 6;
        }
        if (!terminal_write_input(sequence, sequence_length)) {
            return 0;
        }
        return 1;
    }
    if (*alt_down) {
        char alt_sequence[2] = {'\033', character};
        if (!terminal_write_input(alt_sequence, sizeof(alt_sequence))) {
            return 0;
        }
        return 1;
    }
    if (!terminal_write_input(&character, 1)) {
        return 0;
    }
    local_echo = terminal_pty_echo_enabled();
    if (local_echo) {
        terminal_put_char(terminal_pty_echo_character(character));
    }
    return 1;
}

int main(int argc, char **argv, char **envp)
{
    struct leonos_ui_surface ui;
    struct leonos_gui_app_event event;
    char *shell_argv[4];
    uint8_t shift_down = 0;
    uint8_t ctrl_down = 0;
    uint8_t alt_down = 0;
    uint8_t cursor_phase = 2;
    int window_id;
    int shell_result;
    (void)envp;
    pty_id = (uint32_t)leonos_pty_create();
    if ((int)pty_id <= 0) {
        printf("terminal: PTY creation failed (%d)\n", (int)pty_id);
        return 1;
    }
    {
        struct leonos_pty_winsize winsize = {
            .ws_row = (uint16_t)terminal_visible_rows(),
            .ws_col = (uint16_t)TERMINAL_COLUMNS,
        };
        (void)leonos_pty_set_winsize(pty_id, &winsize);
    }
    window_id = leonos_gui_create_app_window_ex(T("Terminal", "终端"),
                                                T("PTY command terminal", "PTY 命令终端"),
                                                TERMINAL_W, TERMINAL_H,
                                                LEONOS_GUI_WINDOW_NO_RESIZE);
    if (window_id <= 0) {
        printf("terminal: window creation failed (%d)\n", window_id);
        return 1;
    }
    if (argc > 2 && terminal_arg_eq(argv[1], "--run") && argv[2] && argv[2][0]) {
        shell_result = leonos_pty_spawn_argv(argv[2], pty_id, &argv[2], 0);
    } else {
        shell_argv[0] = "0:/programs/busybox/busybox.elf";
        shell_argv[1] = "sh";
        shell_argv[2] = 0;
        shell_argv[3] = 0;
        shell_result = leonos_pty_spawn_argv(shell_argv[0], pty_id, shell_argv, 0);
    }
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
                redraw |= terminal_send_key(event.keycode, 1, &shift_down,
                                            &ctrl_down, &alt_down);
            } else if (event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
                (void)terminal_send_key(event.keycode, 0, &shift_down,
                                        &ctrl_down, &alt_down);
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
