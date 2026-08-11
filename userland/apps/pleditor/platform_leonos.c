/* LeonOS terminal/PTY adapter for the upstream PL Editor core. */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <leonos/syscall.h>
#include <leonos/text.h>

#include "../../../third_party/pl_editor/src/platform.h"

static struct termios saved_termios;
static int terminal_active;
static uint32_t file_encoding = LEONOS_TEXT_ENCODING_UTF8;

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
    char *raw_buffer;
    uint32_t decoded_length;
    uint32_t replacements;
    uint32_t detected_encoding;
    int result;
    int close_result;
    if (!filename || !buffer || !length || !(file = fopen(filename, "rb"))) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    if ((uint64_t)size > ((uint64_t)UINT32_MAX - 1U) / 3U ||
        !(raw_buffer = malloc((size_t)size + 1U))) {
        fclose(file);
        return false;
    }
    read_length = fread(raw_buffer, 1, (size_t)size, file);
    close_result = fclose(file);
    if (read_length != (size_t)size || close_result != 0) {
        free(raw_buffer);
        return false;
    }
    if (leonos_text_detect_encoding(raw_buffer, (uint32_t)read_length,
                                    &detected_encoding) < 0 ||
        !(*buffer = malloc(read_length * 3U + 1U))) {
        free(raw_buffer);
        return false;
    }
    result = leonos_text_decode(raw_buffer, (uint32_t)read_length, detected_encoding,
                                *buffer, (uint32_t)(read_length * 3U),
                                &decoded_length, &replacements);
    free(raw_buffer);
    if (result < 0) {
        free(*buffer);
        *buffer = 0;
        return false;
    }
    (*buffer)[decoded_length] = '\0';
    *length = decoded_length;
    file_encoding = detected_encoding;
    return true;
}

bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t length)
{
    FILE *file;
    size_t written;
    char *encoded;
    uint32_t encoded_length;
    uint32_t replacements;
    int result;
    int close_result;
    if (!filename || (!buffer && length)) {
        return false;
    }
    if ((uint64_t)length > ((uint64_t)UINT32_MAX - 4U) / 2U ||
        !(encoded = malloc(length * 2U + 4U))) {
        return false;
    }
    result = leonos_text_encode(buffer, (uint32_t)length, file_encoding, encoded,
                                (uint32_t)(length * 2U + 4U),
                                &encoded_length, &replacements);
    if (result < 0 || replacements) {
        free(encoded);
        return false;
    }
    file = fopen(filename, "wb");
    if (!file) {
        free(encoded);
        return false;
    }
    written = fwrite(encoded, 1, encoded_length, file);
    free(encoded);
    close_result = fclose(file);
    return written == encoded_length && close_result == 0;
}
