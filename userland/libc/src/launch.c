#include <leonos/fs.h>
#include <leonos/app.h>
#include <leonos/device.h>
#include <leonos/environment.h>
#include <leonos/launch.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <unistd.h>

#define LEONOS_ASSOC_CONFIG_PATH "/system/config/fileassoc.cfg"
#define LEONOS_ASSOC_CONFIG_MAX 1024U
#define LEONOS_SHORTCUT_MAX_BYTES 384U
#define LEONOS_SHORTCUT_MAX_DEPTH 8U
#define LEONOS_TERMINAL_APP_PATH "/system/apps/terminal/terminal.elf"

#define LEONOS_LAUNCH_ASSOC_CACHE_MAX LEONOS_APP_REGISTRY_MAX
static struct leonos_launch_assoc_app assoc_cache[LEONOS_LAUNCH_ASSOC_CACHE_MAX];
static struct leonos_app_info assoc_info_cache[LEONOS_LAUNCH_ASSOC_CACHE_MAX];
static uint32_t assoc_cache_count;

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

static int is_system_desktop_path(const char *path)
{
    return text_eq_ignore_case(path, "/system/apps/desktop/desktop.elf");
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path) {
        return "";
    }
    for (uint32_t i = 0; path[i]; ++i) {
        if (path[i] == '/') {
            base = path + i + 1;
        }
    }
    return base ? base : "";
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
    while (len > 1 && dst[len - 1] != '/') {
        dst[--len] = 0;
    }
    if (len > 1) {
        dst[len - 1] = 0;
    } else {
        copy_text(dst, capacity, "/");
    }
}

static int app_requires_terminal(const char *program_path)
{
    struct leonos_app_info info;
    return leonos_app_registry_find(program_path, &info) == 0 &&
           (info.flags & LEONOS_APP_FLAG_TERMINAL) != 0;
}

static int launch_in_terminal(char *argv[])
{
    char *terminal_argv[LEONOS_LAUNCH_MAX_ARGS + 3U];
    char terminal_path[LEONOS_APP_PATH_LEN];
    const char *terminal = LEONOS_TERMINAL_APP_PATH;
    uint32_t argc = 0;
    if (leonos_app_registry_resolve("terminal", terminal_path,
                                    sizeof(terminal_path)) == 0) {
        terminal = terminal_path;
    }
    terminal_argv[0] = (char *)terminal;
    terminal_argv[1] = "--run";
    while (argv[argc]) {
        if (argc >= LEONOS_LAUNCH_MAX_ARGS) {
            return LEONOS_LAUNCH_ERR_TOO_MANY_ARGS;
        }
        terminal_argv[argc + 2U] = argv[argc];
        ++argc;
    }
    terminal_argv[argc + 2U] = 0;
    return leonos_spawn_argv(terminal_argv[0], terminal_argv);
}

int leonos_spawn_argv(const char *path, char *const argv[])
{
    char **envp = 0;
    pid_t pid;
    int result;

    if (!path || !path[0] || !argv || !argv[0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    result = leonos_environment_build(0, &envp);
    if (result < 0) {
        return result;
    }
    pid = fork();
    if (pid == 0) {
        (void)execve(path, argv, envp);
        _exit(127);
    }
    result = pid < 0 ? -1 : (int)pid;
    leonos_environment_free(envp);
    return result;
}

static void build_child_path(char *dst, uint32_t capacity,
                             const char *parent, const char *name)
{
    uint32_t pos = 0;
    if (!dst || capacity == 0) {
        return;
    }
    dst[0] = 0;
    append_text(dst, &pos, capacity, parent);
    if (!text_eq(parent, "/")) {
        append_char(dst, &pos, capacity, '/');
    }
    append_text(dst, &pos, capacity, name);
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

void leonos_launch_default_shortcut_name(const char *target_path, char *buffer,
                                         uint32_t capacity)
{
    uint32_t pos = 0;
    const char *base = path_basename(target_path);
    if (!buffer || capacity == 0) {
        return;
    }
    buffer[0] = 0;
    if (!base || !base[0]) {
        base = "Shortcut";
    }
    append_text(buffer, &pos, capacity, base);
    if (!ends_with_ignore_case(buffer, ".lnk")) {
        append_text(buffer, &pos, capacity, ".lnk");
    }
}

static void build_numbered_shortcut_name(char *dst, uint32_t capacity,
                                         const char *target_path, uint32_t number)
{
    uint32_t pos = 0;
    const char *base = path_basename(target_path);
    if (!dst || capacity == 0) {
        return;
    }
    dst[0] = 0;
    if (!base || !base[0]) {
        base = "Shortcut";
    }
    append_text(dst, &pos, capacity, base);
    append_char(dst, &pos, capacity, ' ');
    if (number >= 10) {
        append_char(dst, &pos, capacity, (char)('0' + (number / 10) % 10));
    }
    append_char(dst, &pos, capacity, (char)('0' + number % 10));
    append_text(dst, &pos, capacity, ".lnk");
}

int leonos_launch_create_shortcut(const char *shortcut_path, const char *target_path)
{
    struct leonos_stat st;
    char body[LEONOS_SHORTCUT_MAX_BYTES];
    uint32_t pos = 0;
    int fd;
    long wrote;
    if (!shortcut_path || !shortcut_path[0] || !target_path || !target_path[0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    if (!ends_with_ignore_case(shortcut_path, ".lnk")) {
        return LEONOS_LAUNCH_ERR_INVALID_SHORTCUT;
    }
    if (leonos_stat_legacy(target_path, &st) < 0) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    if (leonos_stat_legacy(shortcut_path, &st) == 0) {
        return LEONOS_LAUNCH_ERR_EXISTS;
    }
    body[0] = 0;
    append_text(body, &pos, sizeof(body), "# LeonOS shortcut\n");
    append_text(body, &pos, sizeof(body), "target=");
    append_text(body, &pos, sizeof(body), target_path);
    append_char(body, &pos, sizeof(body), '\n');
    fd = open(shortcut_path, LEONOS_O_WRONLY | LEONOS_O_CREAT | LEONOS_O_TRUNC, 0);
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, body, pos);
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    return (uint32_t)wrote == pos ? 0 : -1;
}

int leonos_launch_create_shortcut_in_dir(const char *dir_path, const char *target_path,
                                         char *out_path, uint32_t out_capacity)
{
    struct leonos_stat st;
    char name[LEONOS_FS_NAME_LEN];
    char path[LEONOS_FS_PATH_LEN];
    int ret;
    if (!dir_path || !dir_path[0] || !target_path || !target_path[0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    if (leonos_stat_legacy(dir_path, &st) < 0 || st.type != LEONOS_FS_TYPE_DIR) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    leonos_launch_default_shortcut_name(target_path, name, sizeof(name));
    for (uint32_t i = 0; i < 100; ++i) {
        if (i > 0) {
            build_numbered_shortcut_name(name, sizeof(name), target_path, i + 1);
        }
        build_child_path(path, sizeof(path), dir_path, name);
        if (leonos_stat_legacy(path, &st) == 0) {
            continue;
        }
        ret = leonos_launch_create_shortcut(path, target_path);
        if (ret == 0 && out_path && out_capacity) {
            copy_text(out_path, out_capacity, path);
        }
        return ret;
    }
    return LEONOS_LAUNCH_ERR_EXISTS;
}

static const struct leonos_launch_assoc_app *find_assoc_app(const char *program_path)
{
    struct leonos_app_info info;
    if (leonos_app_registry_find(program_path, &info) < 0) {
        return 0;
    }
    if ((info.flags & LEONOS_APP_FLAG_OPEN_WITH) == 0 &&
        !text_eq(info.id, "terminal")) {
        return 0;
    }
    assoc_info_cache[0] = info;
    assoc_cache[0].name = assoc_info_cache[0].name;
    assoc_cache[0].detail = assoc_info_cache[0].category;
    assoc_cache[0].program_path = assoc_info_cache[0].exec;
    assoc_cache[0].mode = text_eq(info.id, "terminal")
                              ? LEONOS_LAUNCH_ASSOC_MODE_TERMINAL_CAT
                              : LEONOS_LAUNCH_ASSOC_MODE_EXEC;
    return &assoc_cache[0];
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

const char *leonos_launch_builtin_path(const char *name_or_path)
{
    static char resolved[LEONOS_APP_PATH_LEN];
    if (name_or_path && name_or_path[0] &&
        leonos_app_registry_resolve(name_or_path, resolved, sizeof(resolved)) == 0) {
        return resolved;
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
    uint32_t total = leonos_app_registry_count();
    assoc_cache_count = 0;
    for (uint32_t i = 0; i < total && assoc_cache_count < LEONOS_LAUNCH_ASSOC_CACHE_MAX; ++i) {
        if (leonos_app_registry_get(i, &assoc_info_cache[assoc_cache_count]) < 0) continue;
        if ((assoc_info_cache[assoc_cache_count].flags & LEONOS_APP_FLAG_OPEN_WITH) == 0 &&
            !text_eq(assoc_info_cache[assoc_cache_count].id, "terminal") &&
            !text_eq(assoc_info_cache[assoc_cache_count].id, "run")) continue;
        assoc_cache[assoc_cache_count].name = assoc_info_cache[assoc_cache_count].name;
        assoc_cache[assoc_cache_count].detail = assoc_info_cache[assoc_cache_count].category;
        assoc_cache[assoc_cache_count].program_path = assoc_info_cache[assoc_cache_count].exec;
        assoc_cache[assoc_cache_count].mode = text_eq(assoc_info_cache[assoc_cache_count].id, "terminal")
                                                  ? LEONOS_LAUNCH_ASSOC_MODE_TERMINAL_CAT
                                                  : LEONOS_LAUNCH_ASSOC_MODE_EXEC;
        ++assoc_cache_count;
    }
    if (count) {
        *count = assoc_cache_count;
    }
    return assoc_cache;
}

const char *leonos_launch_resolve_default_app_for_path(const char *path)
{
    static char program[LEONOS_FS_PATH_LEN];
    char extension[16];
    if (!leonos_launch_get_extension_for_path(path, extension, sizeof(extension))) {
        return 0;
    }
    if (leonos_launch_get_extension_association(extension, program, sizeof(program)) > 0) {
        return program;
    }
    return leonos_app_registry_default_for_extension(extension, program,
                                                     sizeof(program)) == 0
               ? program : 0;
}

static int shortcut_line_starts_with(const char *line, uint32_t len, const char *prefix)
{
    uint32_t i = 0;
    while (prefix && prefix[i]) {
        if (i >= len || line[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return i > 0;
}

static int parse_shortcut_target(const char *buffer, uint32_t len,
                                 char *target, uint32_t capacity)
{
    uint32_t pos = 0;
    if (!target || capacity == 0) {
        return LEONOS_LAUNCH_ERR_INVALID_SHORTCUT;
    }
    target[0] = 0;
    while (pos < len) {
        uint32_t start = pos;
        uint32_t line_len;
        uint32_t text_start;
        uint32_t text_end;
        uint32_t out = 0;
        while (pos < len && buffer[pos] != '\n' && buffer[pos] != '\r') {
            ++pos;
        }
        line_len = pos - start;
        while (pos < len && (buffer[pos] == '\n' || buffer[pos] == '\r')) {
            ++pos;
        }
        text_start = start;
        text_end = start + line_len;
        while (text_start < text_end && is_space_char(buffer[text_start])) {
            ++text_start;
        }
        while (text_end > text_start && is_space_char(buffer[text_end - 1])) {
            --text_end;
        }
        if (text_start >= text_end || buffer[text_start] == '#') {
            continue;
        }
        if (shortcut_line_starts_with(buffer + text_start, text_end - text_start, "target=")) {
            text_start += 7;
        }
        while (text_start < text_end && out + 1 < capacity) {
            target[out++] = buffer[text_start++];
        }
        target[out] = 0;
        return target[0] ? 0 : LEONOS_LAUNCH_ERR_INVALID_SHORTCUT;
    }
    return LEONOS_LAUNCH_ERR_INVALID_SHORTCUT;
}

static int read_shortcut_target(const char *shortcut_path, char *target, uint32_t capacity)
{
    char buffer[LEONOS_SHORTCUT_MAX_BYTES];
    uint32_t len = 0;
    int fd;
    int ret;
    struct leonos_stat st;
    if (!shortcut_path || !target || capacity == 0) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    target[0] = 0;
    fd = open(shortcut_path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    while (len + 1 < sizeof(buffer)) {
        long got = read(fd, buffer + len, sizeof(buffer) - len - 1);
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
    ret = parse_shortcut_target(buffer, len, target, capacity);
    if (ret < 0) {
        return ret;
    }
    return leonos_stat_legacy(target, &st) < 0 ? LEONOS_LAUNCH_ERR_NOT_FOUND : 0;
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
    if (leonos_stat_legacy(resolved_program, &st) < 0) {
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
        return leonos_spawn_argv(resolved_program, argv);
    }
    {
        char *argv[3];
        argv[0] = (char *)resolved_program;
        argv[1] = (char *)target_path;
        argv[2] = 0;
        /* The caller explicitly selected this executable as the handler.
         * Do not feed it back through leonos_launch_argv(): that routine is
         * for user-entered paths and may reinterpret its first argument as a
         * directory or another associated document.  An explicit handler
         * must be spawned directly, otherwise selecting Notepad can fall
         * through to File Manager when the path is re-resolved. */
        return leonos_spawn_argv(resolved_program, argv);
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

static int leonos_launch_argv_depth(char *argv[], uint32_t depth)
{
    struct leonos_stat st;
    char extension[16];
    char associated_program[LEONOS_FS_PATH_LEN];
    char *path;
    const char *default_program;
    if (!argv || !argv[0] || !argv[0][0]) {
        return LEONOS_LAUNCH_ERR_EMPTY;
    }
    path = argv[0];
    path = (char *)leonos_launch_builtin_path(path);
    argv[0] = path;
    if (ends_with_ignore_case(path, ".lnk")) {
        char target[LEONOS_FS_PATH_LEN];
        char *target_argv[LEONOS_LAUNCH_MAX_ARGS];
        uint32_t i = 1;
        int ret;
        if (depth >= LEONOS_SHORTCUT_MAX_DEPTH) {
            return LEONOS_LAUNCH_ERR_SHORTCUT_LOOP;
        }
        ret = read_shortcut_target(path, target, sizeof(target));
        if (ret < 0) {
            return ret;
        }
        target_argv[0] = target;
        while (i + 1 < LEONOS_LAUNCH_MAX_ARGS && argv[i]) {
            target_argv[i] = argv[i];
            ++i;
        }
        target_argv[i] = 0;
        return leonos_launch_argv_depth(target_argv, depth + 1);
    }
    if (ends_with_ignore_case(path, ".elf")) {
        if (app_requires_terminal(path)) {
            return launch_in_terminal(argv);
        }
        int ret = leonos_spawn_argv(path, argv);
        if (ret == -LEONOS_EEXIST && is_system_desktop_path(path)) {
            return LEONOS_LAUNCH_ERR_ALREADY_RUNNING;
        }
        return ret;
    }
    if (leonos_stat_legacy(path, &st) < 0) {
        return LEONOS_LAUNCH_ERR_NOT_FOUND;
    }
    if (st.type == LEONOS_FS_TYPE_DIR) {
        char *dir_argv[3];
        char fileman_path[LEONOS_APP_PATH_LEN];
        if (leonos_app_registry_resolve("fileman", fileman_path,
                                        sizeof(fileman_path)) < 0) {
            return LEONOS_LAUNCH_ERR_NOT_FOUND;
        }
        dir_argv[0] = fileman_path;
        dir_argv[1] = path;
        dir_argv[2] = 0;
        return leonos_spawn_argv(dir_argv[0], dir_argv);
    }
    default_program = leonos_launch_resolve_default_app_for_path(path);
    if (default_program) {
        return leonos_launch_file_with_app(path, default_program);
    }
    if (leonos_launch_get_extension_for_path(path, extension, sizeof(extension))) {
        if (leonos_app_registry_default_for_extension(extension, associated_program,
                                                      sizeof(associated_program)) == 0) {
            default_program = associated_program;
            return leonos_launch_file_with_app(path, default_program);
        }
    }
    return LEONOS_LAUNCH_ERR_NO_ASSOCIATION;
}

int leonos_launch_argv(char *argv[])
{
    return leonos_launch_argv_depth(argv, 0);
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
    case LEONOS_LAUNCH_ERR_INVALID_SHORTCUT:
        return "Invalid shortcut";
    case LEONOS_LAUNCH_ERR_SHORTCUT_LOOP:
        return "Shortcut loop detected";
    case LEONOS_LAUNCH_ERR_EXISTS:
        return "Shortcut already exists";
    case LEONOS_LAUNCH_ERR_ALREADY_RUNNING:
        return "Desktop is already running";
    default:
        return "Launch failed";
    }
}
