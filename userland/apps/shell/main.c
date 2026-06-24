#include <leonos/pty.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

#define SHELL_LINE_MAX 120

static void print_prompt(void)
{
    write(1, "PS 0:/> ", 8);
}

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

static void trim_newline(char *line)
{
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = 0;
    }
}

static void append_text(char *buf, size_t *pos, size_t cap, const char *text)
{
    while (text && *text && *pos + 1 < cap) {
        buf[(*pos)++] = *text++;
    }
    buf[*pos] = 0;
}

static void run_command(const char *line)
{
    if (text_eq(line, "")) {
        return;
    }
    if (text_eq(line, "help")) {
        puts("help  clear  echo  ps  hello  uidemo  taskmgr  fileman  exit");
        return;
    }
    if (text_eq(line, "clear")) {
        write(1, "\f", 1);
        return;
    }
    if (text_eq(line, "ps")) {
        puts("shell is attached to PTY");
        return;
    }
    if (text_eq(line, "hello")) {
        int pid = execve("0:/userland/hello.elf", 0, 0);
        printf("spawn hello => %d\n", pid);
        return;
    }
    if (text_eq(line, "uidemo")) {
        int pid = execve("0:/userland/uidemo.elf", 0, 0);
        printf("spawn uidemo => %d\n", pid);
        return;
    }
    if (text_eq(line, "taskmgr")) {
        int pid = execve("0:/userland/taskmgr.elf", 0, 0);
        printf("spawn taskmgr => %d\n", pid);
        return;
    }
    if (text_eq(line, "fileman")) {
        int pid = execve("0:/userland/fileman.elf", 0, 0);
        printf("spawn fileman => %d\n", pid);
        return;
    }
    if (text_eq(line, "exit")) {
        puts("shell exit");
        exit(0);
    }
    if (line[0] == 'e' && line[1] == 'c' && line[2] == 'h' && line[3] == 'o' && line[4] == ' ') {
        puts(line + 5);
        return;
    }

    {
        char msg[160];
        size_t pos = 0;
        msg[0] = 0;
        append_text(msg, &pos, sizeof(msg), "unknown command: ");
        append_text(msg, &pos, sizeof(msg), line);
        puts(msg);
    }
}

int main(void)
{
    char line[SHELL_LINE_MAX];
    size_t line_len = 0;
    int pty_id = leonos_pty_self();
    char ch;

    printf("[shell.elf] pid=%d pty=%d starting\n", getpid(), pty_id);
    puts("LeonOS Shell");
    puts("Type help for commands.");
    print_prompt();

    for (;;) {
        long got = read(0, &ch, 1);
        if (got <= 0) {
            sleep_ms(10);
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            write(1, "\n", 1);
            line[line_len] = 0;
            trim_newline(line);
            run_command(line);
            line_len = 0;
            line[0] = 0;
            print_prompt();
            continue;
        }
        if (ch == '\b' || (unsigned char)ch == 127) {
            if (line_len) {
                --line_len;
                line[line_len] = 0;
                write(1, "\b \b", 3);
            }
            continue;
        }
        if ((unsigned char)ch < 32) {
            continue;
        }
        if (line_len + 1 < sizeof(line)) {
            line[line_len++] = ch;
            line[line_len] = 0;
            write(1, &ch, 1);
        }
    }
}
