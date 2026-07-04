#include <leonos/fs.h>
#include <leonos/launch.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>

#define LEONOS_ASSOC_CONFIG_PATH "0:/etc/fileassoc.cfg"
#define LEONOS_ASSOC_CONFIG_MAX 1024U

struct builtin_program {
    const char *name;
    const char *path;
};

struct builtin_assoc {
    const char *extension;
    const char *program_path;
};

static const struct builtin_program builtin_programs[] = {
    {"hello", "0:/userland/hello.elf"},
    {"uidemo", "0:/userland/uidemo.elf"},
    {"taskmgr", "0:/userland/taskmgr.elf"},
    {"fileman", "0:/userland/fileman.elf"},
    {"terminal", "0:/userland/terminal.elf"},
    {"notepad", "0:/userland/notepad.elf"},
    {"calc", "0:/userland/calc.elf"},
    {"run", "0:/userland/run.elf"},
    {"osver", "0:/userland/osver.elf"},
    {"memtest", "0:/userland/memtest.elf"},
    {"diskmgr", "0:/userland/diskmgr.elf"},
    {"devmgr", "0:/userland/devmgr.elf"},
};

static const struct builtin_assoc builtin_assocs[] = {
    {".txt", "0:/userland/notepad.elf"},
    {".md", "0:/userland/notepad.elf"},
    {".log", "0:/userland/notepad.elf"},
    {".cfg", "0:/userland/notepad.elf"},
    {".conf", "0:/userland/notepad.elf"},
    {".ini", "0:/userland/notepad.elf"},
};

static const struct leonos_launch_assoc_app assoc_apps[] = {
    {"Notepad", "Open the file as text", "0:/userland/notepad.elf", LEONOS_LAUNCH_ASSOC_MODE_EXEC},
    {"Terminal", "Run cat in a terminal window", "0:/userland/terminal.elf", LEONOS_LAUNCH_ASSOC_MODE_TERMINAL_CAT},
    {"Run", "Pass the path to the Run dialog", "0:/userland/run.elf", LEONOS_LAUNCH_ASSOC_MODE_EXEC},
};

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
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

static char ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int text_eq_ignore_case(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && ascii_tolower(*a) == ascii_tolower(*b)) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || capacity == 0) {
        return;
    }
    if (src) {
        while (i + 1 < capacity && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t capacity, char ch)
{
    if (!dst || !pos || *pos + 1 >= capacity) {
        return;
    }
    dst[*pos] = ch;
    ++(*pos);
    dst[*pos] = 0;
}

static void append_text(char *dst, uint32_t *pos, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    while (src && src[i]) {
        append_char(dst, pos, capacity, src[i]);
        ++i;
    }
}

static int ends_with_ignore_case(const char *text, const char *suffix)
{
    uint32_t text_n = text_len(text);
    uint32_t suffix_n = text_len(suffix);
    if (suffix_n > text_n) {
        return 0;
    }
    return text_eq_ignore_case(text + text_n - suffix_n, suffix);
}

static int is_space_char(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void build_parent_path(char *dst, uint32_t capacity, const char *path)
{
    uint32_t len;
    copy_text(dst, capacity, path);
    len = text_len(dst);
    while (len > 3 && dst[len - 1] != '/') {
        dst[--len] = 0;
    }
    if (len > 3) {
        dst[len - 1] = 0;
    } else {
        copy_text(dst, capacity, "0:/");
    }
}

static void build_cat_command(char *dst, uint32_t capacity, const char *path)
{
    uint32_t pos = 0;
    if (!dst || capacity == 0) {
        return;
    }
    dst[0] = 0;
    append_text(dst, &pos, capacity, "cat \"");
    append_text(dst, &pos, capacity, path);
    append_char(dst, &pos, capacity, '"');
}

static const struct leonos_launch_assoc_app *find_assoc_app(const char *program_path)
{
    uint32_t count = (uint32_t)(sizeof(assoc_apps) / sizeof(assoc_apps[0]));
    for (uint32_t i = 0; i < count; ++i) {
        if (text_eq(assoc_apps[i].program_path, program_path)) {
            return &assoc_apps[i];
        }
    }
    return 0;
}

static int normalize_extension(const char *extension, char *buffer, uint32_t capacity)
{
    uint32_t i = 0;
    uint32_t pos = 0;
    if (!buffer || capacity == 0) {
        return 0;
    }
    buffer[0] = 0;
    if (!extension || !extension[0]) {
        return 0;
    }
    while (extension[i] && is_space_char(extension[i])) {
        ++i;
    }
    if (!extension[i]) {
        return 0;
    }
    if (extension[i] != '.') {
        append_char(buffer, &pos, capacity, '.');
    }
    while (extension[i] && !is_space_char(extension[i])) {
        append_char(buffer, &pos, capacity, ascii_tolower(extension[i]));
        ++i;
    }
    return pos > 1;
}

static int read_assoc_config(char *buffer, uint32_t capacity, uint32_t *out_len)
{
    int fd;
    uint32_t len = 0;
    if (!buffer || capacity == 0) {
        return -1;
    }
    buffer[0] = 0;
    fd = open(LEONOS_ASSOC_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        if (out_len) {
            *out_len = 0;
        }
        return 0;
    }
    for (;;) {
        long got;
        uint32_t free_bytes = capacity - len - 1;
        if (free_bytes == 0) {
            break;
        }
        got = read(fd, buffer + len, free_bytes);
        if (got < 0) {
            close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    buffer[len] = 0;
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int write_assoc_config(const char *buffer, uint32_t len)
{
    int fd = open(LEONOS_ASSOC_CONFIG_PATH,
                  LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    if (len) {
        long wrote = write(fd, buffer, len);
        if (wrote < 0) {
            close(fd);
            return (int)wrote;
        }
        if ((uint32_t)wrote != len) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
}

static int parse_assoc_line(const char *line, uint32_t len,
                            char *ext, uint32_t ext_cap,
                            char *program, uint32_t program_cap)
{
    uint32_t eq = 0;
    uint32_t left_start = 0;
    uint32_t left_end = 0;
    uint32_t right_start;
    uint32_t right_end = len;
    uint32_t pos = 0;
    if (!line || len == 0) {
        return 0;
    }
    while (left_start < len && is_space_char(line[left_start])) {
        ++left_start;
    }
    if (left_start >= len || line[left_start] == '#' || line[left_start] == ';') {
        return 0;
    }
    while (eq < len && line[eq] != '=') {
        ++eq;
    }
    if (eq >= len) {
        return 0;
    }
    left_end = eq;
    while (left_end > left_start && is_space_char(line[left_end - 1])) {
        --left_end;
    }
    right_start = eq + 1;
    while (right_start < len && is_space_char(line[right_start])) {
        ++right_start;
    }
    while (right_end > right_start && is_space_char(line[right_end - 1])) {
        --right_end;
    }
    if (left_end <= left_start || right_end <= right_start) {
        return 0;
    }
    while (left_start < left_end && is_space_char(line[left_start])) {
        ++left_start;
    }
    if (left_end <= left_start) {
        return 0;
    }
    if (line[left_start] != '.') {
        pos = 0;
        append_char(ext, &pos, ext_cap, '.');
    }
    while (left_start < left_end && pos + 1 < ext_cap) {
        append_char(ext, &pos, ext_cap, ascii_tolower(line[left_start]));
        ++left_start;
    }
    if (pos <= 1) {
        return 0;
    }
    if (!program || program_cap == 0) {
        return 1;
    }
    pos = 0;
    program[0] = 0;
    while (right_start < right_end && pos + 1 < program_cap) {
        program[pos++] = line[right_start++];
    }
    program[pos] = 0;
    return pos != 0;
}

static const char *builtin_assoc_for_extension(const char *extension)
{
    uint32_t count = (uint32_t)(sizeof(builtin_assocs) / sizeof(builtin_assocs[0]));
    for (uint32_t i = 0; i < count; ++i) {
        if (text_eq_ignore_case(builtin_assocs[i].extension, extension)) {
            return builtin_assocs[i].program_path;
        }
    }
    return 0;
}

const char *leonos_launch_builtin_path(const char *name_or_path)
{
    uint32_t count = (uint32_t)(sizeof(builtin_programs) / sizeof(builtin_programs[0]));
    if (!name_or_path || !name_or_path[0]) {
        return name_or_path;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (text_eq(name_or_path, builtin_programs[i].name) ||
            text_eq(name_or_path, builtin_programs[i].path)) {
            return builtin_programs[i].path;
        }
    }
    return name_or_path;
}

const char *leonos_launch_get_extension_for_path(const char *path, char *buffer,
                                                 uint32_t capacity)
{
    uint32_t len;
    uint32_t dot = 0;
    uint32_t slash = 0;
    if (!buffer || capacity == 0) {
        return 0;
    }
    buffer[0] = 0;
    if (!path || !path[0]) {
        return 0;
    }
    len = text_len(path);
    for (uint32_t i = 0; i < len; ++i) {
        if (path[i] == '/') {
            slash = i + 1;
            dot = 0;
        } else if (path[i] == '.') {
            dot = i;
        }
    }
    if (dot <= slash || dot >= len) {
        return 0;
    }
    if (!normalize_extension(path + dot, buffer, capacity)) {
        return 0;
    }
    return buffer;
}

int leonos_launch_get_extension_association(const char *extension, char *program_path,
                                            uint32_t capacity)
{
    char wanted[16];
    char config[LEONOS_ASSOC_CONFIG_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    if (!program_path || capacity == 0) {
        return -1;
    }
    program_path[0] = 0;
    if (!normalize_extension(extension, wanted, sizeof(wanted))) {
        return 0;
    }
    if (read_assoc_config(config, sizeof(config), &len) < 0) {
        return 0;
    }
    while (pos < len) {
        char ext[16];
        char program[LEONOS_FS_PATH_LEN];
        uint32_t start = pos;
        while (pos < len && config[pos] != '\n' && config[pos] != '\r') {
            ++pos;
        }
        if (parse_assoc_line(config + start, pos - start, ext, sizeof(ext),
                             program, sizeof(program)) &&
            text_eq_ignore_case(ext, wanted)) {
            copy_text(program_path, capacity, leonos_launch_builtin_path(program));
            return 1;
        }
        while (pos < len && (config[pos] == '\n' || config[pos] == '\r')) {
            ++pos;
        }
    }
    return 0;
}

int leonos_launch_set_extension_association(const char *extension, const char *program_path)
{
    char wanted[16];
    char old_cfg[LEONOS_ASSOC_CONFIG_MAX];
    char new_cfg[LEONOS_ASSOC_CONFIG_MAX];
    char normalized_program[LEONOS_FS_PATH_LEN];
    uint32_t old_len = 0;
    uint32_t new_len = 0;
    uint32_t pos = 0;
    uint8_t wrote = 0;
    const char *resolved_program = 0;
    if (!normalize_extension(extension, wanted, sizeof(wanted))) {
        return -1;
    }
    normalized_program[0] = 0;
    if (program_path && program_path[0]) {
        resolved_program = leonos_launch_builtin_path(program_path);
        copy_text(normalized_program, sizeof(normalized_program), resolved_program);
    }
    if (read_assoc_config(old_cfg, sizeof(old_cfg), &old_len) < 0) {
        old_cfg[0] = 0;
        old_len = 0;
    }
    new_cfg[0] = 0;
    while (pos < old_len) {
        char ext[16];
        char program[LEONOS_FS_PATH_LEN];
        uint32_t start = pos;
        uint32_t end;
        while (pos < old_len && old_cfg[pos] != '\n' && old_cfg[pos] != '\r') {
            ++pos;
        }
        end = pos;
        if (parse_assoc_line(old_cfg + start, end - start, ext, sizeof(ext),
                             program, sizeof(program)) &&
            text_eq_ignore_case(ext, wanted)) {
            if (!wrote && normalized_program[0]) {
                append_text(new_cfg, &new_len, sizeof(new_cfg), wanted);
                append_char(new_cfg, &new_len, sizeof(new_cfg), '=');
                append_text(new_cfg, &new_len, sizeof(new_cfg), normalized_program);
                append_char(new_cfg, &new_len, sizeof(new_cfg), '\n');
                wrote = 1;
            }
        } else {
            while (start < end) {
                append_char(new_cfg, &new_len, sizeof(new_cfg), old_cfg[start++]);
            }
            append_char(new_cfg, &new_len, sizeof(new_cfg), '\n');
        }
        while (pos < old_len && (old_cfg[pos] == '\n' || old_cfg[pos] == '\r')) {
            ++pos;
        }
    }
    if (!wrote && normalized_program[0]) {
        append_text(new_cfg, &new_len, sizeof(new_cfg), wanted);
        append_char(new_cfg, &new_len, sizeof(new_cfg), '=');
        append_text(new_cfg, &new_len, sizeof(new_cfg), normalized_program);
        append_char(new_cfg, &new_len, sizeof(new_cfg), '\n');
    }
    return write_assoc_config(new_cfg, new_len);
}

const struct leonos_launch_assoc_app *leonos_launch_assoc_apps(uint32_t *count)
{
    if (count) {
        *count = (uint32_t)(sizeof(assoc_apps) / sizeof(assoc_apps[0]));
    }
    return assoc_apps;
}

const char *leonos_launch_resolve_default_app_for_path(const char *path)
{
    static char program[LEONOS_FS_PATH_LEN];
    char extension[16];
    const char *builtin_program;
    if (!leonos_launch_get_extension_for_path(path, extension, sizeof(extension))) {
        return 0;
    }
    if (leonos_launch_get_extension_association(extension, program, sizeof(program)) > 0) {
        return program;
    }
    builtin_program = builtin_assoc_for_extension(extension);
    return builtin_program ? builtin_program : 0;
}

int leonos_launch_file_with_app(const char *target_path, const char *program_path)
{
    const char *resolved_program;
    const struct leonos_launch_assoc_app *app;
    struct leonos_stat st;
    if (!target_path || !target_path[0] || !program_path || !program_path[0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    resolved_program = leonos_launch_builtin_path(program_path);
    if (stat(resolved_program, &st) < 0) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    app = find_assoc_app(resolved_program);
    if (app && app->mode == LEONOS_LAUNCH_ASSOC_MODE_TERMINAL_CAT) {
        char cwd[LEONOS_FS_PATH_LEN];
        char command[LEONOS_FS_PATH_LEN + 16];
        char *argv[4];
        build_parent_path(cwd, sizeof(cwd), target_path);
        build_cat_command(command, sizeof(command), target_path);
        argv[0] = (char *)resolved_program;
        argv[1] = cwd;
        argv[2] = command;
        argv[3] = 0;
        return execve(resolved_program, argv, 0);
    }
    {
        char *argv[3];
        argv[0] = (char *)resolved_program;
        argv[1] = (char *)target_path;
        argv[2] = 0;
        return execve(resolved_program, argv, 0);
    }
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
    char extension[16];
    char *path;
    const char *default_program;
    if (!argv || !argv[0] || !argv[0][0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    path = argv[0];
    path = (char *)leonos_launch_builtin_path(path);
    argv[0] = path;
    if (stat(path, &st) < 0) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    if (st.type == LEONOS_FS_TYPE_DIR) {
        char *dir_argv[3];
        dir_argv[0] = "0:/userland/fileman.elf";
        dir_argv[1] = path;
        dir_argv[2] = 0;
        return execve(dir_argv[0], dir_argv, 0);
    }
    if (ends_with_ignore_case(path, ".elf")) {
        return execve(path, argv, 0);
    }
    default_program = leonos_launch_resolve_default_app_for_path(path);
    if (default_program) {
        return leonos_launch_file_with_app(path, default_program);
    }
    if (leonos_launch_get_extension_for_path(path, extension, sizeof(extension))) {
        default_program = builtin_assoc_for_extension(extension);
        if (default_program) {
            return leonos_launch_file_with_app(path, default_program);
        }
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
