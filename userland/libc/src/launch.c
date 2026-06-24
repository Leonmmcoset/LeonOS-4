#include <leonos/fs.h>
#include <leonos/launch.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static int ends_with(const char *text, const char *suffix)
{
    uint32_t text_n = text_len(text);
    uint32_t suffix_n = text_len(suffix);
    if (suffix_n > text_n) {
        return 0;
    }
    return text_eq(text + text_n - suffix_n, suffix);
}

static int is_text_file_path(const char *path)
{
    return ends_with(path, ".txt") ||
           ends_with(path, ".md") ||
           ends_with(path, ".log") ||
           ends_with(path, ".cfg") ||
           ends_with(path, ".conf") ||
           ends_with(path, ".ini");
}

int leonos_cmdline_split(char *line, char *argv[], uint32_t max_args)
{
    uint32_t argc = 0;
    char quote = 0;
    char *src;
    char *dst;
    if (!line || !argv || max_args == 0) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    src = line;
    while (*src) {
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
            ++src;
        }
        if (!*src) {
            break;
        }
        if (argc + 1 >= max_args) {
            argv[0] = 0;
            return LEONOS_LAUNCH_ERR_TOO_MANY_ARGS;
        }
        argv[argc++] = src;
        dst = src;
        quote = 0;
        while (*src) {
            if (quote) {
                if (*src == quote) {
                    quote = 0;
                    ++src;
                    continue;
                }
                *dst++ = *src++;
                continue;
            }
            if (*src == '\'' || *src == '"') {
                quote = *src++;
                continue;
            }
            if (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
                break;
            }
            *dst++ = *src++;
        }
        if (quote) {
            argv[0] = 0;
            return LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE;
        }
        *dst = 0;
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
            *src++ = 0;
        }
    }
    if (argc == 0) {
        argv[0] = 0;
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    argv[argc] = 0;
    return (int)argc;
}

int leonos_launch_argv(char *argv[])
{
    struct leonos_stat st;
    char *path;
    int pid;
    if (!argv || !argv[0] || !argv[0][0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    path = argv[0];
    if (text_eq(path, "hello")) {
        path = "0:/userland/hello.elf";
        argv[0] = path;
    } else if (text_eq(path, "uidemo")) {
        path = "0:/userland/uidemo.elf";
        argv[0] = path;
    } else if (text_eq(path, "taskmgr")) {
        path = "0:/userland/taskmgr.elf";
        argv[0] = path;
    } else if (text_eq(path, "fileman")) {
        path = "0:/userland/fileman.elf";
        argv[0] = path;
    } else if (text_eq(path, "terminal")) {
        path = "0:/userland/terminal.elf";
        argv[0] = path;
    } else if (text_eq(path, "notepad")) {
        path = "0:/userland/notepad.elf";
        argv[0] = path;
    } else if (text_eq(path, "calc")) {
        path = "0:/userland/calc.elf";
        argv[0] = path;
    } else if (text_eq(path, "run")) {
        path = "0:/userland/run.elf";
        argv[0] = path;
    }

    if (stat(path, &st) < 0) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    if (st.type == LEONOS_FS_TYPE_DIR) {
        char *dir_argv[3];
        dir_argv[0] = "0:/userland/fileman.elf";
        dir_argv[1] = path;
        dir_argv[2] = 0;
        pid = execve(dir_argv[0], dir_argv, 0);
        return pid < 0 ? pid : pid;
    }
    if (is_text_file_path(path)) {
        char *note_argv[3];
        note_argv[0] = "0:/userland/notepad.elf";
        note_argv[1] = path;
        note_argv[2] = 0;
        pid = execve(note_argv[0], note_argv, 0);
        return pid < 0 ? pid : pid;
    }
    if (ends_with(path, ".elf")) {
        pid = execve(path, argv, 0);
        return pid < 0 ? pid : pid;
    }
    return LEONOS_LAUNCH_ERR_NO_ASSOCIATION;
}

int leonos_launch_command_line(char *line, char *argv[], uint32_t max_args)
{
    int argc = leonos_cmdline_split(line, argv, max_args);
    if (argc < 0) {
        return argc;
    }
    return leonos_launch_argv(argv);
}

const char *leonos_launch_error_text(int code)
{
    switch (code) {
    case LEONOS_LAUNCH_ERR_EMPTY:
        return "Command line is empty";
    case LEONOS_LAUNCH_ERR_TOO_MANY_ARGS:
        return "Too many arguments";
    case LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE:
        return "Missing closing quote";
    case LEONOS_LAUNCH_ERR_NOT_FOUND:
        return "Program or path not found";
    case LEONOS_LAUNCH_ERR_NO_ASSOCIATION:
        return "No file association for this item";
    default:
        return "Launch failed";
    }
}
