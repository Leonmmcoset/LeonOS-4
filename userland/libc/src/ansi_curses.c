/* Shared ANSI curses subset for LeonOS terminal applications. */
#include <ncurses.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <leonos/syscall.h>

struct leonos_curses_window {
    int rows;
    int columns;
    int y;
    int x;
    int cursor_y;
    int cursor_x;
    int nodelay_enabled;
    int scroll_enabled;
};

WINDOW *stdscr;
int LINES = 24;
int COLS = 80;
static int curses_active;
static struct termios saved_termios;
static int have_saved_termios;
static int pending_input = ERR;
#define LEONOS_CURSES_OUTPUT_CAP 4096U
static char output_buffer[LEONOS_CURSES_OUTPUT_CAP];
static size_t output_length;

static void flush_output(void)
{
    size_t offset = 0;
    while (offset < output_length) {
        long result = write(STDOUT_FILENO, output_buffer + offset,
                            output_length - offset);
        if (result <= 0) {
            if (offset) {
                memmove(output_buffer, output_buffer + offset,
                        output_length - offset);
                output_length -= offset;
            }
            return;
        }
        offset += (size_t)result;
    }
    output_length = 0;
}

static void emit(const char *text, size_t length)
{
    while (length) {
        size_t available = sizeof(output_buffer) - output_length;
        size_t chunk;
        if (!available) {
            flush_output();
            available = sizeof(output_buffer) - output_length;
            if (!available) {
                return;
            }
        }
        chunk = length < available ? length : available;
        memcpy(output_buffer + output_length, text, chunk);
        output_length += chunk;
        text += chunk;
        length -= chunk;
    }
}

static void emit_text(const char *text)
{
    emit(text, strlen(text));
}

static void move_cursor(WINDOW *window)
{
    char sequence[32];
    int y;
    int x;
    int length;
    if (!window) {
        return;
    }
    y = window->y + window->cursor_y + 1;
    x = window->x + window->cursor_x + 1;
    if (y < 1) {
        y = 1;
    }
    if (x < 1) {
        x = 1;
    }
    length = snprintf(sequence, sizeof(sequence), "\033[%d;%dH", y, x);
    if (length > 0) {
        emit(sequence, (size_t)length);
    }
}

static void refresh_size(void)
{
    struct winsize size;
    if (tcgetwinsize(STDIN_FILENO, &size) == 0) {
        if (size.ws_row > 0) {
            LINES = size.ws_row;
        }
        if (size.ws_col > 0) {
            COLS = size.ws_col;
        }
    }
}

WINDOW *initscr(void)
{
    if (!curses_active) {
        have_saved_termios = tcgetattr(STDIN_FILENO, &saved_termios) == 0;
    }
    refresh_size();
    if (!stdscr) {
        stdscr = newwin(LINES, COLS, 0, 0);
    } else {
        stdscr->rows = LINES;
        stdscr->columns = COLS;
    }
    if (!stdscr) {
        return 0;
    }
    curses_active = 1;
    emit_text("\033[?1049h\033[?25l\033[0m\033[2J\033[H");
    return stdscr;
}

int endwin(void)
{
    if (curses_active) {
        emit_text("\033[0m\033[?25h\033[?1049l");
        flush_output();
        if (have_saved_termios) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        }
        curses_active = 0;
    }
    return OK;
}

int isendwin(void)
{
    return !curses_active;
}

WINDOW *newwin(int rows, int columns, int y, int x)
{
    WINDOW *window = (WINDOW *)calloc(1, sizeof(*window));
    if (!window) {
        return 0;
    }
    if (rows <= 0) {
        rows = LINES - y;
    }
    if (columns <= 0) {
        columns = COLS - x;
    }
    window->rows = rows > 0 ? rows : 1;
    window->columns = columns > 0 ? columns : 1;
    window->y = y;
    window->x = x;
    return window;
}

int delwin(WINDOW *window)
{
    if (window && window != stdscr) {
        free(window);
    }
    return OK;
}

int keypad(WINDOW *window, int enabled)
{
    (void)window;
    (void)enabled;
    return OK;
}

int nodelay(WINDOW *window, int enabled)
{
    if (window) {
        window->nodelay_enabled = enabled != 0;
    }
    return OK;
}

int scrollok(WINDOW *window, int enabled)
{
    if (window) {
        window->scroll_enabled = enabled != 0;
    }
    return OK;
}

int raw(void)
{
    struct termios mode;
    if (tcgetattr(STDIN_FILENO, &mode) != 0) {
        return ERR;
    }
    mode.c_lflag &= (tcflag_t)~(ICANON | ISIG);
    mode.c_iflag &= (tcflag_t)~(ICRNL | INLCR);
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSANOW, &mode) == 0 ? OK : ERR;
}

int noecho(void)
{
    struct termios mode;
    if (tcgetattr(STDIN_FILENO, &mode) != 0) {
        return ERR;
    }
    mode.c_lflag &= (tcflag_t)~ECHO;
    return tcsetattr(STDIN_FILENO, TCSANOW, &mode) == 0 ? OK : ERR;
}

int halfdelay(int tenths)
{
    (void)tenths;
    return raw();
}

int napms(int milliseconds)
{
    if (milliseconds < 0) {
        return ERR;
    }
    flush_output();
    return sleep_ms((unsigned long)milliseconds) < 0 ? ERR : OK;
}

int nonl(void)
{
    return OK;
}

int typeahead(int fd)
{
    (void)fd;
    return OK;
}

int leaveok(WINDOW *window, int enabled)
{
    (void)window;
    (void)enabled;
    return OK;
}

int curs_set(int visibility)
{
    emit_text(visibility ? "\033[?25h" : "\033[?25l");
    return OK;
}

int beep(void)
{
    emit_text("\a");
    return OK;
}

int start_color(void) { return OK; }
int use_default_colors(void) { return OK; }
int init_pair(short pair, short foreground, short background)
{
    (void)pair;
    (void)foreground;
    (void)background;
    return OK;
}

int wattron(WINDOW *window, chtype attributes)
{
    (void)window;
    if (attributes & A_REVERSE) {
        emit_text("\033[7m");
    }
    if (attributes & A_BOLD) {
        emit_text("\033[1m");
    }
    if (attributes & A_ITALIC) {
        emit_text("\033[3m");
    }
    return OK;
}

int wattroff(WINDOW *window, chtype attributes)
{
    (void)window;
    (void)attributes;
    emit_text("\033[0m");
    return OK;
}

int wmove(WINDOW *window, int y, int x)
{
    if (!window) {
        return ERR;
    }
    window->cursor_y = y < 0 ? 0 : y;
    window->cursor_x = x < 0 ? 0 : x;
    move_cursor(window);
    return OK;
}

int waddch(WINDOW *window, const chtype character)
{
    char value = (char)character;
    if (!window) {
        return ERR;
    }
    if (value == '\n') {
        emit_text("\r\n");
        window->cursor_y++;
        window->cursor_x = 0;
    } else if (value == '\r') {
        emit_text("\r");
        window->cursor_x = 0;
    } else {
        emit(&value, 1);
        window->cursor_x++;
    }
    return OK;
}

int waddnstr(WINDOW *window, const char *text, int length)
{
    int index = 0;
    if (!window || !text) {
        return ERR;
    }
    while (text[index] && (length < 0 || index < length)) {
        (void)waddch(window, (unsigned char)text[index]);
        ++index;
    }
    return OK;
}

int waddstr(WINDOW *window, const char *text)
{
    return waddnstr(window, text, -1);
}

int mvwaddch(WINDOW *window, int y, int x, const chtype character)
{
    return wmove(window, y, x) == OK ? waddch(window, character) : ERR;
}

int mvwaddnstr(WINDOW *window, int y, int x, const char *text, int length)
{
    return wmove(window, y, x) == OK ? waddnstr(window, text, length) : ERR;
}

int mvwaddstr(WINDOW *window, int y, int x, const char *text)
{
    return mvwaddnstr(window, y, x, text, -1);
}

static int print_into(WINDOW *window, const char *format, va_list args)
{
    char buffer[1024];
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    if (length < 0) {
        return ERR;
    }
    if ((size_t)length >= sizeof(buffer)) {
        length = (int)sizeof(buffer) - 1;
    }
    return waddnstr(window, buffer, length);
}

int wprintw(WINDOW *window, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = print_into(window, format, args);
    va_end(args);
    return result;
}

int mvwprintw(WINDOW *window, int y, int x, const char *format, ...)
{
    int result;
    va_list args;
    if (wmove(window, y, x) != OK) {
        return ERR;
    }
    va_start(args, format);
    result = print_into(window, format, args);
    va_end(args);
    return result;
}

int wclrtoeol(WINDOW *window)
{
    if (!window) {
        return ERR;
    }
    move_cursor(window);
    emit_text("\033[K");
    return OK;
}

int werase(WINDOW *window)
{
    int line;
    if (!window) {
        return ERR;
    }
    for (line = 0; line < window->rows; ++line) {
        (void)wmove(window, line, 0);
        emit_text("\033[K");
    }
    (void)wmove(window, 0, 0);
    return OK;
}

int wrefresh(WINDOW *window)
{
    (void)window;
    flush_output();
    return OK;
}

int wnoutrefresh(WINDOW *window)
{
    (void)window;
    return OK;
}

int doupdate(void)
{
    flush_output();
    return OK;
}

int wredrawln(WINDOW *window, int begin, int count)
{
    (void)window;
    (void)begin;
    (void)count;
    return OK;
}

int wscrl(WINDOW *window, int lines)
{
    char sequence[24];
    int length;
    if (!window || !lines) {
        return OK;
    }
    length = snprintf(sequence, sizeof(sequence), "\033[%d%c", lines < 0 ? -lines : lines,
                      lines < 0 ? 'T' : 'S');
    if (length > 0) {
        emit(sequence, (size_t)length);
    }
    return OK;
}

int wgetch(WINDOW *window)
{
    unsigned char value;
    long result;
    if (!window) {
        return ERR;
    }
    flush_output();
    if (pending_input != ERR) {
        int input = pending_input;
        pending_input = ERR;
        return input;
    }

    /* libc's read() deliberately waits for PTY input so ordinary terminal
     * reads have blocking semantics.  Curses nodelay mode is different: nano
     * uses it to probe for bytes already queued after the first key.  Bypass
     * that blocking wrapper and perform exactly one kernel read here. */
    if (window->nodelay_enabled) {
        result = syscall3(SYS_read, STDIN_FILENO, (long)&value, 1);
        return result == 1 ? value : ERR;
    }

    do {
        result = read(STDIN_FILENO, &value, 1);
        if (result == 1) {
            return value;
        }
        /* LeonOS PTYs report an empty input queue as a zero-byte read rather
         * than blocking in the kernel.  Nano expects blocking curses reads;
         * sleep for one scheduler tick instead of spinning millions of times. */
        if (result == 0) {
            (void)sleep_ms(1);
        }
    } while (result == 0);
    return ERR;
}

int ungetch(int input)
{
    if (pending_input != ERR) {
        return ERR;
    }
    pending_input = input;
    return OK;
}

int key_defined(const char *definition)
{
    (void)definition;
    return 0;
}

char *tgetstr(const char *name, char **area)
{
    (void)name;
    (void)area;
    return 0;
}

int mvaddch(int y, int x, chtype character)
{
    return mvwaddch(stdscr, y, x, character);
}

int refresh(void)
{
    return wrefresh(stdscr);
}

int getch(void)
{
    return wgetch(stdscr);
}

int mvcur(int old_y, int old_x, int new_y, int new_x)
{
    (void)old_y;
    (void)old_x;
    return wmove(stdscr, new_y, new_x);
}
