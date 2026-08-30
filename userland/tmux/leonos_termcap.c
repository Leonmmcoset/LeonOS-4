/* Compact ANSI/xterm terminfo surface for tmux's terminal renderer. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <term.h>

struct leonos_terminal { int unused; };
static struct leonos_terminal terminal_instance;
TERMINAL *cur_term = &terminal_instance;

struct capability {
    const char *name;
    const char *value;
};

static const struct capability capabilities[] = {
    {"acsc", "lqkxmj"}, {"bel", "\a"}, {"blink", "\033[5m"},
    {"bold", "\033[1m"}, {"civis", "\033[?25l"},
    {"clear", "\033[H\033[2J"}, {"cnorm", "\033[?25h"},
    {"Cr", "\r"}, {"csr", "\033[%i%p1%d;%p2%dr"},
    {"cub", "\033[%p1%dD"}, {"cub1", "\b"},
    {"cud", "\033[%p1%dB"}, {"cud1", "\n"},
    {"cuf", "\033[%p1%dC"}, {"cuf1", "\033[C"},
    {"cup", "\033[%i%p1%d;%p2%dH"}, {"cuu", "\033[%p1%dA"},
    {"cuu1", "\033[A"}, {"cvvis", "\033[?25h"},
    {"dch", "\033[%p1%dP"}, {"dch1", "\033[P"},
    {"dim", "\033[2m"}, {"dl", "\033[%p1%dM"}, {"dl1", "\033[M"},
    {"ech", "\033[%p1%dX"}, {"ed", "\033[J"}, {"el", "\033[K"},
    {"el1", "\033[1K"}, {"enacs", ""}, {"home", "\033[H"},
    {"hpa", "\033[%i%p1%dG"}, {"ich", "\033[%p1%d@"},
    {"ich1", "\033[@"}, {"il", "\033[%p1%dL"}, {"il1", "\033[L"},
    {"indn", "\033[%p1%dS"}, {"invis", "\033[8m"},
    {"kcbt", "\033[Z"}, {"kcub1", "\033[D"}, {"kcud1", "\033[B"},
    {"kcuf1", "\033[C"}, {"kcuu1", "\033[A"}, {"kdch1", "\033[3~"},
    {"kend", "\033[F"}, {"kf1", "\033OP"}, {"kf2", "\033OQ"},
    {"kf3", "\033OR"}, {"kf4", "\033OS"}, {"kf5", "\033[15~"},
    {"kf6", "\033[17~"}, {"kf7", "\033[18~"}, {"kf8", "\033[19~"},
    {"kf9", "\033[20~"}, {"kf10", "\033[21~"}, {"khome", "\033[H"},
    {"kich1", "\033[2~"}, {"kind", "\033[B"}, {"knp", "\033[6~"},
    {"knxt", "\033[6~"}, {"kpp", "\033[5~"}, {"kprv", "\033[5~"},
    {"kri", "\033[A"}, {"op", "\033[39;49m"}, {"rev", "\033[7m"},
    {"ri", "\033[M"}, {"rin", "\033[%p1%dT"},
    {"rmacs", "\033[0m"}, {"rmcup", "\033[?1049l"},
    {"rmkx", "\033[?1l\033>"}, {"setab", "\033[48;5;%p1%dm"},
    {"setaf", "\033[38;5;%p1%dm"},
    {"setrgbb", "\033[48;2;%p1%d;%p2%d;%p3%dm"},
    {"setrgbf", "\033[38;2;%p1%d;%p2%d;%p3%dm"},
    {"sgr0", "\033[0m"}, {"sitm", "\033[3m"}, {"smacs", "\033(0"},
    {"smcup", "\033[?1049h"}, {"smkx", "\033[?1h\033="},
    {"smso", "\033[7m"}, {"smul", "\033[4m"}, {"vpa", "\033[%i%p1%dd"},
};

int setupterm(char *term, int fd, int *error)
{
    (void)term;
    (void)fd;
    cur_term = &terminal_instance;
    if (error) *error = 1;
    return OK;
}

int del_curterm(TERMINAL *term)
{
    (void)term;
    return OK;
}

char *tigetstr(char *name)
{
    if (!name) return (char *)-1;
    for (size_t i = 0; i < sizeof(capabilities) / sizeof(capabilities[0]); ++i) {
        if (strcmp(name, capabilities[i].name) == 0) return (char *)capabilities[i].value;
    }
    return (char *)-1;
}

int tigetnum(char *name)
{
    if (!name) return -2;
    if (strcmp(name, "colors") == 0) return 256;
    if (strcmp(name, "U8") == 0) return 1;
    return -1;
}

int tigetflag(char *name)
{
    if (!name) return -1;
    return strcmp(name, "am") == 0 || strcmp(name, "AX") == 0 ||
           strcmp(name, "bce") == 0 || strcmp(name, "Tc") == 0 ||
           strcmp(name, "RGB") == 0 || strcmp(name, "XT") == 0;
}

static char *expand(const char *capability, long parameters[9])
{
    static char output[256];
    long stack[16];
    size_t out = 0;
    int top = 0;
    while (capability && *capability && out + 1u < sizeof(output)) {
        if (*capability != '%') {
            output[out++] = *capability++;
            continue;
        }
        ++capability;
        switch (*capability++) {
        case '%': output[out++] = '%'; break;
        case 'i': ++parameters[0]; ++parameters[1]; break;
        case 'p':
            if (*capability >= '1' && *capability <= '9' && top < 16)
                stack[top++] = parameters[*capability++ - '1'];
            break;
        case '{': {
            long value = 0;
            while (*capability >= '0' && *capability <= '9')
                value = value * 10 + (*capability++ - '0');
            if (*capability == '}') ++capability;
            if (top < 16) stack[top++] = value;
            break;
        }
        case '+':
            if (top >= 2) {
                long right = stack[--top];
                stack[top - 1] += right;
            }
            break;
        case '-':
            if (top >= 2) {
                long right = stack[--top];
                stack[top - 1] -= right;
            }
            break;
        case 'd':
        case '2':
        case '3': {
            int width = capability[-1] == 'd' ? 0 : capability[-1] - '0';
            long value = top ? stack[--top] : 0;
            int written = width
                ? snprintf(output + out, sizeof(output) - out, "%0*ld", width, value)
                : snprintf(output + out, sizeof(output) - out, "%ld", value);
            if (written > 0) out += (size_t)written;
            break;
        }
        default: break;
        }
    }
    output[out < sizeof(output) ? out : sizeof(output) - 1u] = 0;
    return output;
}

char *tparm(char *capability, long p1, long p2, long p3, long p4, long p5,
            long p6, long p7, long p8, long p9)
{
    long parameters[9] = {p1, p2, p3, p4, p5, p6, p7, p8, p9};
    return expand(capability, parameters);
}

char *tiparm(const char *capability, ...)
{
    va_list arguments;
    long parameters[9] = {0};
    va_start(arguments, capability);
    for (size_t i = 0; i < 9; ++i) parameters[i] = va_arg(arguments, long);
    va_end(arguments);
    return expand(capability, parameters);
}
