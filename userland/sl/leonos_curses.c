/* LeonOS ANSI terminal compatibility layer for upstream sl. */
#include <curses.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <leonos/syscall.h>

struct leonos_sl_window {
    int rows;
    int columns;
    int nodelay_enabled;
    int scroll_enabled;
};

WINDOW *stdscr;
int LINES = 24;
int COLS = 80;

static struct leonos_sl_window root_window;
static struct termios saved_termios;
static int have_saved_termios;
static int curses_active;

#define LEONOS_SL_OUTPUT_CAP 16384U
static char output_buffer[LEONOS_SL_OUTPUT_CAP];
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

static void refresh_size(void)
{
    struct winsize size;
    if (tcgetwinsize(STDIN_FILENO, &size) == 0) {
        if (size.ws_row > 0) {
            LINES = (int)size.ws_row;
        }
        if (size.ws_col > 0) {
            COLS = (int)size.ws_col;
        }
    }
    if (LINES < 1) LINES = 1;
    if (COLS < 1) COLS = 1;
}

WINDOW *initscr(void)
{
    refresh_size();
    root_window.rows = LINES;
    root_window.columns = COLS;
    root_window.nodelay_enabled = 0;
    root_window.scroll_enabled = 0;
    stdscr = &root_window;
    have_saved_termios = tcgetattr(STDIN_FILENO, &saved_termios) == 0;
    curses_active = 1;
    emit_text("\033[?25l\033[0m\033[2J\033[H");
    flush_output();
    return stdscr;
}

int endwin(void)
{
    if (!curses_active) {
        return OK;
    }
    emit_text("\033[0m\033[?25h");
    flush_output();
    if (have_saved_termios) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    }
    curses_active = 0;
    return OK;
}

int nodelay(WINDOW *window, int enabled)
{
    if (window) {
        window->nodelay_enabled = enabled != 0;
    }
    return OK;
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

int leaveok(WINDOW *window, int enabled)
{
    (void)window;
    (void)enabled;
    return OK;
}

int scrollok(WINDOW *window, int enabled)
{
    if (window) {
        window->scroll_enabled = enabled != 0;
    }
    return OK;
}

int curs_set(int visibility)
{
    emit_text(visibility ? "\033[?25h" : "\033[?25l");
    return OK;
}

int mvaddch(int y, int x, chtype character)
{
    char sequence[32];
    int length;
    if (!stdscr || y < 0 || y >= LINES || x < 0 || x >= COLS) {
        return ERR;
    }
    length = snprintf(sequence, sizeof(sequence), "\033[%d;%dH", y + 1, x + 1);
    if (length <= 0) {
        return ERR;
    }
    emit(sequence, (size_t)length);
    sequence[0] = (char)(character & 0xffU);
    emit(sequence, 1);
    return OK;
}

int refresh(void)
{
    flush_output();
    return OK;
}

int usleep(unsigned int microseconds)
{
    unsigned long milliseconds = ((unsigned long)microseconds + 999UL) / 1000UL;
    return sleep_ms(milliseconds);
}

int getch(void)
{
    /* sl only polls for input to match curses; it never consumes it. */
    return ERR;
}

int mvcur(int old_y, int old_x, int new_y, int new_x)
{
    char sequence[32];
    int length;
    (void)old_y;
    (void)old_x;
    if (new_y < 0) new_y = 0;
    if (new_x < 0) new_x = 0;
    length = snprintf(sequence, sizeof(sequence), "\033[%d;%dH", new_y + 1, new_x + 1);
    if (length > 0) {
        emit(sequence, (size_t)length);
    }
    return OK;
}
