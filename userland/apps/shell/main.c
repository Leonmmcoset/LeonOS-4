#include <leonos/pty.h>
#include <leonos/fs.h>
#include <leonos/launch.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

#define SHELL_LINE_MAX 120

static void put_chr(char ch)
{
    write(1, &ch, 1);
}

static void print_prompt(void)
{
    char cwd[LEONOS_FS_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) {
        write(1, "PS ?> ", 6);
        return;
    }
    write(1, "PS ", 3);
    write(1, cwd, strlen(cwd));
    write(1, "> ", 2);
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

static void print_file(const char *path)
{
    char buf[128];
    int fd = open(path, 0, 0);
    if (fd < 0) {
        printf("open %s => %d\n", path, fd);
        return;
    }
    for (;;) {
        long got = read(fd, buf, sizeof(buf));
        if (got < 0) {
            printf("read %s => %d\n", path, (int)got);
            break;
        }
        if (got == 0) {
            break;
        }
        write(1, buf, (size_t)got);
    }
    if (buf[0] != '\n') {
        put_chr('\n');
    }
    close(fd);
}

static void list_dir(const char *path)
{
    struct leonos_dir_entry entry;
    int fd = open(path && path[0] ? path : ".", 0, 0);
    if (fd < 0) {
        printf("open %s => %d\n", path && path[0] ? path : ".", fd);
        return;
    }
    while (leonos_readdir(fd, &entry) > 0) {
        printf("%s %s\n",
               entry.type == LEONOS_FS_TYPE_DIR ? "<DIR>" :
               (entry.type == LEONOS_FS_TYPE_DEVICE ? "<DEV>" : "<FILE>"),
               entry.name);
    }
    close(fd);
}

static void run_ansi_test(void)
{
    puts("\x1b[2J\x1b[HANSI test");
    puts("\x1b[31mred\x1b[0m \x1b[32mgreen\x1b[0m \x1b[34mblue\x1b[0m \x1b[93mbright yellow\x1b[0m");
    puts("\x1b[44;97mwhite on blue\x1b[0m \x1b[101;30mblack on bright red\x1b[0m");
    puts("line clear demo: keep this\x1b[10D\x1b[Kcleared");
    puts("\x1b[6;1Hcursor moved to row 6");
    puts("\x1b[8;1Hdone");
}

static void run_command(char *line)
{
    char *argv[LEONOS_LAUNCH_MAX_ARGS + 1];
    int argc = leonos_cmdline_split(line, argv, LEONOS_LAUNCH_MAX_ARGS + 1);
    if (argc == LEONOS_LAUNCH_ERR_EMPTY) {
        return;
    }
    if (argc < 0) {
        puts(leonos_launch_error_text(argc));
        return;
    }
    if (text_eq(argv[0], "help")) {
        puts("help clear ansi echo ps pwd cd ls cat hello uidemo taskmgr fileman terminal notepad calc run exit");
        return;
    }
    if (text_eq(argv[0], "ansi")) {
        run_ansi_test();
        return;
    }
    if (text_eq(argv[0], "clear")) {
        write(1, "\f", 1);
        return;
    }
    if (text_eq(argv[0], "ps")) {
        puts("shell is attached to PTY");
        return;
    }
    if (text_eq(argv[0], "pwd")) {
        char cwd[LEONOS_FS_PATH_LEN];
        if (getcwd(cwd, sizeof(cwd))) {
            puts(cwd);
        }
        return;
    }
    if (text_eq(argv[0], "cd")) {
        int ret = chdir(argc > 1 ? argv[1] : "0:/");
        printf("cd => %d\n", ret);
        return;
    }
    if (text_eq(argv[0], "ls")) {
        list_dir(argc > 1 ? argv[1] : ".");
        return;
    }
    if (text_eq(argv[0], "cat")) {
        if (argc < 2) {
            puts("cat needs a path");
            return;
        }
        print_file(argv[1]);
        return;
    }
    if (text_eq(argv[0], "exit")) {
        puts("shell exit");
        exit(0);
    }
    if (text_eq(argv[0], "echo")) {
        if (argc > 1) {
            puts(argv[1]);
        } else {
            puts("");
        }
        return;
    }

    {
        int pid = leonos_launch_argv(argv);
        if (pid >= 0) {
            printf("spawn %s => %d\n", argv[0], pid);
            return;
        }
        if (pid == LEONOS_LAUNCH_ERR_NOT_FOUND ||
            pid == LEONOS_LAUNCH_ERR_NO_ASSOCIATION ||
            pid == LEONOS_LAUNCH_ERR_TOO_MANY_ARGS ||
            pid == LEONOS_LAUNCH_ERR_UNCLOSED_QUOTE) {
            puts(leonos_launch_error_text(pid));
            return;
        }
        {
            char msg[160];
            size_t pos = 0;
            msg[0] = 0;
            append_text(msg, &pos, sizeof(msg), "launch failed: ");
            append_text(msg, &pos, sizeof(msg), argv[0]);
            puts(msg);
        }
    }
}

int main(int argc, char **argv, char **envp)
{
    char line[SHELL_LINE_MAX];
    size_t line_len = 0;
    int pty_id = leonos_pty_self();
    char ch;
    (void)envp;

    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        int ret = chdir(argv[1]);
        printf("[shell.elf] chdir argv[1]=%s => %d\n", argv[1], ret);
    }

    printf("[shell.elf] pid=%d pty=%d starting\n", getpid(), pty_id);
    puts("LeonOS Shell");
    puts("Type help for commands.");
    if (argc > 2 && argv[2] && argv[2][0]) {
        char startup[SHELL_LINE_MAX];
        size_t i = 0;
        while (argv[2][i] && i + 1 < sizeof(startup)) {
            startup[i] = argv[2][i];
            ++i;
        }
        startup[i] = 0;
        run_command(startup);
    }
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
