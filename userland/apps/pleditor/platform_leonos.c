/* LeonOS terminal/PTY adapter for the upstream PL Editor core. */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <leonos/syscall.h>

#include "../../../third_party/pl_editor/src/platform.h"

static struct termios saved_termios;
static int terminal_active;

static int read_byte(unsigned char *value, unsigned long timeout_ms)
{
    unsigned long waited = 0;
    for (;;) {
        long result = read(STDIN_FILENO, value, 1);
        if (result == 1) {
            return 1;
        }
        if (result < 0) {
            return -1;
        }
        if (timeout_ms && waited >= timeout_ms) {
            return 0;
        }
        (void)sleep_ms(1);
        ++waited;
    }
}

static void write_all(const char *text, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        long result = write(STDOUT_FILENO, text + offset, length - offset);
        if (result <= 0) {
            return;
        }
        offset += (size_t)result;
    }
}

bool pleditor_platform_init(void)
{
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return false;
    }
    raw = saved_termios;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INLCR | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return false;
    }
    terminal_active = 1;
    write_all("\033[?1049h\033[?25l\033[0m", 18);
    return true;
}

void pleditor_platform_cleanup(void)
{
    if (terminal_active) {
        write_all("\033[0m\033[?25h\033[?1049l", 18);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        terminal_active = 0;
    }
}

bool pleditor_platform_get_size(int *rows, int *cols)
{
    struct winsize size;
    if (!rows || !cols || tcgetwinsize(STDOUT_FILENO, &size) != 0 ||
        size.ws_row < 3 || size.ws_col == 0) {
        return false;
    }
    *rows = size.ws_row;
    *cols = size.ws_col;
    return true;
}

int pleditor_platform_read_key(void)
{
    unsigned char value;
    unsigned char sequence[3];
    int result = read_byte(&value, 0);
    if (result != 1) {
        return PLEDITOR_KEY_ERR;
    }
    if (value != PLEDITOR_KEY_ESC) {
        return value;
    }
    if (read_byte(&sequence[0], 30) != 1 || read_byte(&sequence[1], 30) != 1) {
        return PLEDITOR_KEY_ESC;
    }
    if (sequence[0] == '[') {
        if (sequence[1] >= '0' && sequence[1] <= '9') {
            if (read_byte(&sequence[2], 30) != 1 || sequence[2] != '~') {
                return PLEDITOR_KEY_ESC;
            }
            switch (sequence[1]) {
            case '1': case '7': return PLEDITOR_HOME_KEY;
            case '3': return PLEDITOR_DEL_KEY;
            case '4': case '8': return PLEDITOR_END_KEY;
            case '5': return PLEDITOR_PAGE_UP;
            case '6': return PLEDITOR_PAGE_DOWN;
            default: return PLEDITOR_KEY_ESC;
            }
        }
        switch (sequence[1]) {
        case 'A': return PLEDITOR_ARROW_UP;
        case 'B': return PLEDITOR_ARROW_DOWN;
        case 'C': return PLEDITOR_ARROW_RIGHT;
        case 'D': return PLEDITOR_ARROW_LEFT;
        case 'H': return PLEDITOR_HOME_KEY;
        case 'F': return PLEDITOR_END_KEY;
        default: return PLEDITOR_KEY_ESC;
        }
    }
    if (sequence[0] == 'O') {
        if (sequence[1] == 'H') {
            return PLEDITOR_HOME_KEY;
        }
        if (sequence[1] == 'F') {
            return PLEDITOR_END_KEY;
        }
    }
    return PLEDITOR_KEY_ESC;
}

void pleditor_platform_write(const char *text, size_t length)
{
    if (text && length) {
        write_all(text, length);
    }
}

bool pleditor_platform_read_file(const char *filename, char **buffer, size_t *length)
{
    FILE *file;
    long size;
    size_t read_length;
    if (!filename || !buffer || !length || !(file = fopen(filename, "rb"))) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    *buffer = malloc((size_t)size + 1U);
    if (!*buffer) {
        fclose(file);
        return false;
    }
    read_length = fread(*buffer, 1, (size_t)size, file);
    if (read_length != (size_t)size || fclose(file) != 0) {
        free(*buffer);
        *buffer = 0;
        return false;
    }
    (*buffer)[read_length] = '\0';
    *length = read_length;
    return true;
}

bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t length)
{
    FILE *file;
    size_t written;
    if (!filename || (!buffer && length) || !(file = fopen(filename, "wb"))) {
        return false;
    }
    written = fwrite(buffer, 1, length, file);
    return written == length && fclose(file) == 0;
}
