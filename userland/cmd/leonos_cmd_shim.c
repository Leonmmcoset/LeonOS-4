/* LeonOS compatibility layer for the ChenPi11/cmd POSIX implementation. */
#include "glibcmd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <leonos/pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U
#define LEONOS_SYS_DUP 32
#define LEONOS_SYS_DUP2 33
#define LEONOS_SYS_FCNTL 72
#define LEONOS_SPAWN_ARG_MAX 8U

struct leonos_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

struct leonos_dir_entry_raw {
    uint32_t type;
    char name[128];
};

extern int leonos_stat_raw_call(const char *path, struct leonos_stat_raw *st)
    __asm__("stat");
extern int leonos_fstat_raw_call(int fd, struct leonos_stat_raw *st)
    __asm__("fstat");
extern int leonos_readdir(int fd, struct leonos_dir_entry_raw *entry);
extern int wait4(int pid, int *status, int options, void *rusage);
extern int sleep_ms(unsigned long milliseconds);
extern unsigned long leonos_uptime_ms(void);
extern long syscall1(long number, long first);
extern long syscall2(long number, long first, long second);
extern long syscall3(long number, long first, long second, long third);
extern char **environ;

static int set_errno_from_status(int status)
{
    if (status < 0) {
        errno = -status;
        return -1;
    }
    return status;
}

static int fill_stat(const struct leonos_stat_raw *raw, struct stat *st)
{
    if (!raw || !st) {
        errno = EINVAL;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = raw->type == LEONOS_FS_TYPE_DIR ? (S_IFDIR | 0755) :
                  raw->type == LEONOS_FS_TYPE_DEVICE ? (S_IFCHR | 0660) :
                  (S_IFREG | 0755);
    st->st_size = (off_t)raw->size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((raw->size + 511U) / 512U);
    return 0;
}

int leonos_posix_stat(const char *path, struct stat *st)
{
    struct leonos_stat_raw raw;
    int result;
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    result = leonos_stat_raw_call(path, &raw);
    if (set_errno_from_status(result) < 0)
        return -1;
    return fill_stat(&raw, st);
}

int leonos_posix_fstat(int fd, struct stat *st)
{
    struct leonos_stat_raw raw;
    int result;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    result = leonos_fstat_raw_call(fd, &raw);
    if (set_errno_from_status(result) < 0)
        return -1;
    return fill_stat(&raw, st);
}

int leonos_posix_lstat(const char *path, struct stat *st)
{
    return leonos_posix_stat(path, st);
}

DIR *opendir(const char *path)
{
    DIR *directory;
    int fd;
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        errno = -fd;
        return NULL;
    }
    directory = (DIR *)calloc(1, sizeof(*directory));
    if (!directory) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    directory->fd = fd;
    return directory;
}

struct dirent *readdir(DIR *directory)
{
    struct leonos_dir_entry_raw entry;
    int result;
    size_t index;
    if (!directory) {
        errno = EINVAL;
        return NULL;
    }
    result = leonos_readdir(directory->fd, &entry);
    if (result <= 0)
        return NULL;
    memset(&directory->dirent, 0, sizeof(directory->dirent));
    directory->dirent.d_type = entry.type == LEONOS_FS_TYPE_DIR ? DT_DIR :
                                entry.type == LEONOS_FS_TYPE_DEVICE ? DT_CHR : DT_REG;
    for (index = 0; index + 1 < sizeof(directory->dirent.d_name) && entry.name[index]; ++index)
        directory->dirent.d_name[index] = entry.name[index];
    return &directory->dirent;
}

int closedir(DIR *directory)
{
    int result;
    if (!directory) {
        errno = EINVAL;
        return -1;
    }
    result = close(directory->fd);
    free(directory);
    return set_errno_from_status(result);
}

int lstat(const char *path, struct stat *st)
{
    return leonos_posix_stat(path, st);
}

int access(const char *path, int mode)
{
    struct leonos_stat_raw raw;
    int result;
    (void)mode;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    result = leonos_stat_raw_call(path, &raw);
    return set_errno_from_status(result) < 0 ? -1 : 0;
}

int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int link(const char *old_path, const char *new_path)
{
    (void)old_path;
    (void)new_path;
    errno = ENOSYS;
    return -1;
}

int symlink(const char *target, const char *link_path)
{
    (void)target;
    (void)link_path;
    errno = ENOSYS;
    return -1;
}

ssize_t readlink(const char *path, char *buffer, size_t length)
{
    (void)path;
    (void)buffer;
    (void)length;
    errno = ENOSYS;
    return -1;
}

int statvfs(const char *path, struct statvfs *st)
{
    (void)path;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->f_bsize = 512;
    st->f_frsize = 512;
    st->f_namemax = 127;
    return 0;
}

int fstatvfs(int fd, struct statvfs *st)
{
    (void)fd;
    return statvfs(NULL, st);
}

int fcntl(int fd, int command, ...)
{
    va_list args;
    long argument = 0;
    long result;
    va_start(args, command);
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC ||
        command == F_SETFD || command == F_SETFL)
        argument = va_arg(args, int);
    va_end(args);
    result = syscall3(LEONOS_SYS_FCNTL, fd, command, argument);
    return set_errno_from_status((int)result);
}

int dup(int fd)
{
    return set_errno_from_status((int)syscall1(LEONOS_SYS_DUP, fd));
}

int dup2(int old_fd, int new_fd)
{
    return set_errno_from_status((int)syscall2(LEONOS_SYS_DUP2, old_fd, new_fd));
}

int pipe(int filedes[2])
{
    (void)filedes;
    errno = ENOSYS;
    return -1;
}

pid_t fork(void)
{
    errno = ENOSYS;
    return -1;
}

pid_t vfork(void)
{
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    int result;
    for (;;) {
        result = wait4((int)pid, status, options, NULL);
        if (result >= 0 || (options & 1))
            return (pid_t)result;
        if (result != -ECHILD) {
            errno = -result;
            return -1;
        }
        sleep_ms(1);
    }
}

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    (void)signal_number;
    (void)action;
    if (previous) {
        memset(previous, 0, sizeof(*previous));
        previous->sa_handler = SIG_DFL;
    }
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
    (void)how;
    (void)set;
    if (old_set)
        memset(old_set, 0, sizeof(*old_set));
    return 0;
}

int gettimeofday(struct timeval *value, void *timezone)
{
    unsigned long milliseconds;
    (void)timezone;
    if (!value) {
        errno = EINVAL;
        return -1;
    }
    milliseconds = leonos_uptime_ms();
    value->tv_sec = (time_t)(milliseconds / 1000U);
    value->tv_usec = (suseconds_t)((milliseconds % 1000U) * 1000U);
    return 0;
}

int nice(int increment)
{
    (void)increment;
    return 0;
}

int stime(const time_t *time_value)
{
    (void)time_value;
    errno = ENOSYS;
    return -1;
}

int uname(struct utsname *name)
{
    static const char system_name[] = "LeonOS";
    static const char node_name[] = "leonos";
    static const char release_name[] = "4";
    static const char version_name[] = "LeonOS 4 userland";
    static const char machine_name[] = "x86_64";
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    memset(name, 0, sizeof(*name));
    strncpy(name->sysname, system_name, sizeof(name->sysname) - 1);
    strncpy(name->nodename, node_name, sizeof(name->nodename) - 1);
    strncpy(name->release, release_name, sizeof(name->release) - 1);
    strncpy(name->version, version_name, sizeof(name->version) - 1);
    strncpy(name->machine, machine_name, sizeof(name->machine) - 1);
    return 0;
}

static int command_name_equal(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left >= 'A' && *left <= 'Z' ? (char)(*left + 32) : *left;
        char b = *right >= 'A' && *right <= 'Z' ? (char)(*right + 32) : *right;
        if (a != b)
            return 0;
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static int command_has_path(const char *name)
{
    return strchr(name, '/') != NULL || strchr(name, '\\') != NULL;
}

static int copy_exec_path(char *out, size_t out_size, const char *path)
{
    size_t length;
    if (!out || !out_size || !path) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(path) + 1;
    if (length > out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, path, length);
    return 0;
}

static const char *const busybox_applets[] = {
    "basename", "busybox", "cat", "clear", "cp", "diff", "dirname", "echo",
    "false", "head", "less", "ls", "mkdir", "mv", "printenv", "printf", "pwd",
    "rm", "rmdir", "sh", "sleep", "tail", "true", "uname", "unlink", "vi", "wc", NULL,
};

struct program_path {
    const char *name;
    const char *path;
};

static const struct program_path programs[] = {
    {"cmd", "0:/programs/cmd/cmd.elf"},
    {"file", "0:/programs/file/file.elf"},
    {"lua", "0:/programs/lua/lua.elf"},
    {"nano", "0:/programs/nano/nano.elf"},
    {"pleditor", "0:/programs/pleditor/pleditor.elf"},
    {"tcc", "0:/programs/tcc/tcc.elf"},
    {NULL, NULL},
};

int libcmd_find_exec(const char *name, const char *path_env, char *out, size_t out_size)
{
    size_t index;
    struct stat st;
    (void)path_env;
    if (!name || !name[0] || !out || !out_size) {
        errno = EINVAL;
        return -1;
    }
    if (command_has_path(name)) {
        if (leonos_posix_stat(name, &st) == 0 && S_ISREG(st.st_mode))
            return copy_exec_path(out, out_size, name);
        return -1;
    }
    for (index = 0; busybox_applets[index]; ++index) {
        if (command_name_equal(name, busybox_applets[index]))
            return copy_exec_path(out, out_size, "0:/programs/busybox/busybox.elf");
    }
    for (index = 0; programs[index].name; ++index) {
        if (command_name_equal(name, programs[index].name))
            return copy_exec_path(out, out_size, programs[index].path);
    }
    return -1;
}

static int spawn_argv(const char *path, char *const argv[], int wait,
                      libcmd_exit_info_t *exit_info)
{
    int pid;
    int status = 0;
    int waited;
    size_t count = 0;
    int pty_id;

    if (!path || !path[0] || !argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    while (argv[count]) {
        if (++count > LEONOS_SPAWN_ARG_MAX) {
            errno = E2BIG;
            return -1;
        }
    }
    pty_id = leonos_pty_self();
    pid = leonos_pty_spawn_argv(path, pty_id > 0 ? (uint32_t)pty_id : 0U, argv, environ);
    if (pid < 0) {
        errno = -pid;
        return -1;
    }
    if (!wait)
        return pid;
    for (;;) {
        waited = wait4(pid, &status, 0, NULL);
        if (waited == pid)
            break;
        if (waited != -ECHILD) {
            errno = waited < 0 ? -waited : ECHILD;
            return -1;
        }
        sleep_ms(1);
    }
    if (exit_info) {
        exit_info->exited = 1;
        exit_info->exit_code = (status >> 8) & 0xff;
        exit_info->signaled = 0;
        exit_info->signal = 0;
    }
    return 0;
}

int libcmd_exec_sync(const char *path, char *const argv[], char *const envp[],
                     int stdin_fd, int stdout_fd, int stderr_fd, int nice_level,
                     libcmd_exit_info_t *exit_info)
{
    (void)envp;
    (void)stdin_fd;
    (void)stdout_fd;
    (void)stderr_fd;
    (void)nice_level;
    return spawn_argv(path, argv, 1, exit_info);
}

int libcmd_exec_async(const char *path, char *const argv[], char *const envp[],
                      int stdin_fd, int stdout_fd, int stderr_fd, int nice_level)
{
    (void)envp;
    (void)stdin_fd;
    (void)stdout_fd;
    (void)stderr_fd;
    (void)nice_level;
    return spawn_argv(path, argv, 0, NULL);
}

int libcmd_wait_pid(int pid, libcmd_exit_info_t *exit_info)
{
    int status = 0;
    int waited;
    for (;;) {
        waited = wait4(pid, &status, 0, NULL);
        if (waited == pid)
            break;
        if (waited != -ECHILD) {
            errno = waited < 0 ? -waited : ECHILD;
            return -1;
        }
        sleep_ms(1);
    }
    if (exit_info) {
        exit_info->exited = 1;
        exit_info->exit_code = (status >> 8) & 0xff;
        exit_info->signaled = 0;
        exit_info->signal = 0;
    }
    return 0;
}

int libcmd_fork(void)
{
    errno = ENOSYS;
    return -1;
}

void libcmd_exit(int status)
{
    _exit(status);
}

int libcmd_exec_pipeline(char *const *const *cmds, const char *const *paths, int n,
                         char *const envp[], int stdin_fd, int stdout_fd,
                         libcmd_exit_info_t *exit_info)
{
    (void)cmds;
    (void)paths;
    (void)n;
    (void)envp;
    (void)stdin_fd;
    (void)stdout_fd;
    (void)exit_info;
    errno = ENOSYS;
    return -1;
}

FILE *libcmd_popen(const char *cmd, const char *mode)
{
    (void)cmd;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int libcmd_pclose(FILE *stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return fclose(stream);
}

static int (*readline_signal_hook)(void);

void libcmd_readline_init(void)
{
}

void libcmd_readline_shutdown(void)
{
}

void libcmd_readline_set_signal_hook(int (*hook)(void))
{
    readline_signal_hook = hook;
}

void libcmd_readline_set_file_completion(int on)
{
    (void)on;
}

char *libcmd_readline(const char *prompt, char *buf, size_t size)
{
    size_t length;
    if (!buf || size < 2) {
        errno = EINVAL;
        return NULL;
    }
    if (prompt && prompt[0]) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (!fgets(buf, (int)size, stdin)) {
        if (readline_signal_hook && readline_signal_hook())
            errno = EINTR;
        return NULL;
    }
    length = strlen(buf);
    while (length && (buf[length - 1] == '\n' || buf[length - 1] == '\r'))
        buf[--length] = 0;
    return buf;
}

int libcmd_path_is_abs(const char *path)
{
    return path && (path[0] == '/' ||
                    (((path[0] >= 'A' && path[0] <= 'Z') ||
                      (path[0] >= 'a' && path[0] <= 'z')) &&
                     path[1] == ':' && path[2] == '/'));
}

void libcmd_path_norm_sep(char *path)
{
    while (path && *path) {
        if (*path == '\\')
            *path = '/';
        ++path;
    }
}

int libcmd_path_join(const char *base, const char *rel, char *buf, size_t size)
{
    size_t base_length;
    if (!base || !rel || !buf || !size) {
        errno = EINVAL;
        return -1;
    }
    if (libcmd_path_is_abs(rel))
        return copy_exec_path(buf, size, rel);
    base_length = strlen(base);
    if (base_length && base[base_length - 1] == '/')
        return libcmd_sprintf_s(buf, size, "%s%s", base, rel) < 0 ? -1 : 0;
    return libcmd_sprintf_s(buf, size, "%s/%s", base, rel) < 0 ? -1 : 0;
}

int libcmd_path_dirname(const char *path, char *buf, size_t size)
{
    const char *last;
    size_t length;
    if (!path || !buf || !size) {
        errno = EINVAL;
        return -1;
    }
    last = strrchr(path, '/');
    if (!last)
        return copy_exec_path(buf, size, ".");
    if (last == path + 2 && path[1] == ':')
        return copy_exec_path(buf, size, "0:/");
    if (last == path)
        return copy_exec_path(buf, size, "/");
    length = (size_t)(last - path);
    if (length + 1 > size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf, path, length);
    buf[length] = 0;
    return 0;
}

const char *libcmd_path_basename(const char *path)
{
    const char *last = path ? strrchr(path, '/') : NULL;
    return last ? last + 1 : (path ? path : "");
}

const char *libcmd_path_ext(const char *path)
{
    const char *base = libcmd_path_basename(path);
    const char *dot = strrchr(base, '.');
    return dot && dot != base ? dot : base + strlen(base);
}

int libcmd_path_abs(const char *path, char *buf, size_t size)
{
    char cwd[4096];
    if (libcmd_path_is_abs(path))
        return copy_exec_path(buf, size, path);
    if (!getcwd(cwd, sizeof(cwd)))
        return -1;
    return libcmd_path_join(cwd, path, buf, size);
}

char *libcmd_path_normalize(char *path)
{
    libcmd_path_norm_sep(path);
    return path;
}

int libcmd_is_switch(const char *arg, const char *known)
{
    char value;
    if (!arg || arg[0] != '/' || !arg[1])
        return 0;
    value = arg[1] >= 'a' && arg[1] <= 'z' ? (char)(arg[1] - 32) : arg[1];
    return value == '-' || value == '?' || (known && strchr(known, value) != NULL);
}

int libcmd_set_system_time(time_t value)
{
    (void)value;
    errno = ENOSYS;
    return -1;
}

int libcmd_set_process_priority(int nice_level)
{
    (void)nice_level;
    return 0;
}

FILE *libcmd_open_memstream(char **ptr, size_t *size)
{
    if (ptr)
        *ptr = NULL;
    if (size)
        *size = 0;
    errno = ENOSYS;
    return NULL;
}

int libcmd_memstream_close(FILE *stream)
{
    return stream ? fclose(stream) : -1;
}

int libcmd_fnmatch(const char *pattern, const char *text)
{
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*')
                ++pattern;
            if (!*pattern)
                return 0;
            while (*text) {
                if (libcmd_fnmatch(pattern, text) == 0)
                    return 0;
                ++text;
            }
            return 1;
        }
        if (*pattern != '?' && *pattern != *text)
            return 1;
        if (!*text)
            return 1;
        ++pattern;
        ++text;
    }
    return *text ? 1 : 0;
}
