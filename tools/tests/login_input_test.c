#include <assert.h>
#include <errno.h>
#include <string.h>
#define main login_application_main
#include "../../userland/apps/login/main.c"
#undef main

static const char *input_text;
static size_t input_position;
static int interrupt_read, restore_error, termios_calls;

ssize_t __wrap_read(int fd, void *buffer, size_t length)
{
    assert(fd == 0 && length == 1);
    if (interrupt_read) { interrupt_read = 0; errno = EINTR; return -1; }
    if (!input_text[input_position]) return 0;
    *(char *)buffer = input_text[input_position++];
    return 1;
}

ssize_t __wrap_write(int fd, const void *buffer, size_t length)
{
    assert(fd == 1);
    (void)buffer;
    return (ssize_t)length;
}

int __wrap_tcgetattr(int fd, struct termios *attributes)
{
    assert(fd == 0);
    memset(attributes, 0, sizeof(*attributes));
    attributes->c_lflag = ECHO | ECHONL | ICANON;
    return 0;
}

int __wrap_tcsetattr(int fd, int action, const struct termios *attributes)
{
    assert(fd == 0 && action == TCSANOW);
    ++termios_calls;
    if (termios_calls == 1) assert(!(attributes->c_lflag & (ECHO | ECHONL)));
    else {
        assert(attributes->c_lflag & ECHO);
        if (restore_error) { errno = EIO; return -1; }
    }
    return 0;
}

static void feed(const char *text)
{
    input_text = text;
    input_position = 0;
    termios_calls = 0;
}

int main(void)
{
    char output[LEONOS_AUTH_PASSWORD_LEN], overlong[140];
    feed("U!lower\n");
    interrupt_read = 1;
    assert(tty_read_secret(NULL, output, sizeof(output)) == 1);
    assert(!strcmp(output, "U!lower") && termios_calls == 2);
    for (unsigned i = 0; i < 32; ++i) memcpy(overlong + i * 4, "\xf0\x9f\x94\x91", 4);
    strcpy(overlong + 128, "x\nnext\n");
    feed(overlong);
    assert(tty_read_secret(NULL, output, sizeof(output)) == 0 && errno == EOVERFLOW);
    assert(tty_read_line(NULL, output, sizeof(output), 0) == 1);
    assert(!strcmp(output, "next"));
    feed("r\n");
    restore_error = 1;
    assert(tty_read_secret(NULL, output, sizeof(output)) == 0 && errno == EIO);
    puts("login input: EINTR, complete-line rejection, and terminal restoration PASS");
    return 0;
}
