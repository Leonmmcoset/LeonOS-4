/* ANSI termcap adapter for upstream less on the LeonOS PTY. */
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

static char termcap_storage[128];
static char cursor_template[] = "\033[%i%d;%dH";

static char *copy_capability(const char *value, char **area)
{
    size_t length;
    char *result;
    if (!value) {
        return 0;
    }
    if (!area || !*area) {
        return (char *)value;
    }
    length = strlen(value) + 1U;
    result = *area;
    memcpy(result, value, length);
    *area += length;
    return result;
}

int tgetent(char *buffer, const char *term)
{
    (void)buffer;
    (void)term;
    return 1;
}

int tgetflag(const char *name)
{
    if (!name) {
        return 0;
    }
    return strcmp(name, "am") == 0 || strcmp(name, "bs") == 0;
}

int tgetnum(const char *name)
{
    struct winsize size;
    int fd;
    if (!name || (strcmp(name, "li") != 0 && strcmp(name, "co") != 0)) {
        return -1;
    }
    for (fd = 2; fd >= 0; --fd) {
        if (ioctl(fd, TIOCGWINSZ, &size) == 0) {
            if (strcmp(name, "li") == 0 && size.ws_row > 0) {
                return size.ws_row;
            }
            if (strcmp(name, "co") == 0 && size.ws_col > 0) {
                return size.ws_col;
            }
        }
    }
    return -1;
}

char *tgetstr(const char *name, char **area)
{
    const char *value = 0;
    if (!name) {
        return 0;
    }
    if (strcmp(name, "cm") == 0) value = cursor_template;
    else if (strcmp(name, "cl") == 0) value = "\033[H\033[2J";
    else if (strcmp(name, "cd") == 0) value = "\033[J";
    else if (strcmp(name, "ce") == 0) value = "\033[K";
    else if (strcmp(name, "ho") == 0) value = "\033[H";
    else if (strcmp(name, "cr") == 0) value = "\r";
    else if (strcmp(name, "so") == 0) value = "\033[7m";
    else if (strcmp(name, "se") == 0) value = "\033[27m";
    else if (strcmp(name, "us") == 0) value = "\033[4m";
    else if (strcmp(name, "ue") == 0) value = "\033[24m";
    else if (strcmp(name, "md") == 0) value = "\033[1m";
    else if (strcmp(name, "me") == 0) value = "\033[0m";
    else if (strcmp(name, "mb") == 0) value = "\033[5m";
    else if (strcmp(name, "vb") == 0) value = "\a";
    else if (strcmp(name, "kr") == 0) value = "\033[C";
    else if (strcmp(name, "kl") == 0) value = "\033[D";
    else if (strcmp(name, "ku") == 0) value = "\033[A";
    else if (strcmp(name, "kd") == 0) value = "\033[B";
    else if (strcmp(name, "kP") == 0) value = "\033[5~";
    else if (strcmp(name, "kN") == 0) value = "\033[6~";
    else if (strcmp(name, "kh") == 0) value = "\033[H";
    else if (strcmp(name, "@7") == 0) value = "\033[F";
    else if (strcmp(name, "kD") == 0) value = "\033[3~";
    else if (strcmp(name, "k1") == 0) value = "\033OP";
    else if (strcmp(name, "kb") == 0) value = "\177";
    else if (strcmp(name, "pc") == 0) value = "";
    if (!value) {
        return 0;
    }
    if (area && *area) {
        return copy_capability(value, area);
    }
    strncpy(termcap_storage, value, sizeof(termcap_storage) - 1U);
    termcap_storage[sizeof(termcap_storage) - 1U] = 0;
    return termcap_storage;
}

char *tgoto(const char *capability, int column, int row)
{
    static char result[48];
    (void)capability;
    snprintf(result, sizeof(result), "\033[%d;%dH", row + 1, column + 1);
    return result;
}

int tputs(const char *string, int affected_lines, int (*putc_function)(int))
{
    (void)affected_lines;
    if (!string || !putc_function) {
        return -1;
    }
    while (*string) {
        if (putc_function((unsigned char)*string++) == EOF) {
            return -1;
        }
    }
    return 0;
}
