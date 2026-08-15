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
#define LEONOS_CMD_JOB_MAX 16U
#define LEONOS_CMD_JOB_PROCESS_MAX 64U
#define LEONOS_CMD_JOB_TEXT_MAX 160U
#define LEONOS_CMD_TASK_MAX 64U
#define LEONOS_CMD_TASK_NAME_LEN 32U

/* Keep this local mirror of the task-snapshot wire layout so this POSIX
 * adapter does not include leonos/gui.h, which intentionally exposes the
 * native filesystem stat ABI rather than Picolibc's POSIX stat ABI. */
struct leonos_cmd_task_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint32_t memory_kib;
    uint64_t cpu_ticks;
    int32_t priority;
    uint32_t pending_signals;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[LEONOS_CMD_TASK_NAME_LEN];
    char username[32];
};

enum leonos_cmd_job_state {
    LEONOS_CMD_JOB_RUNNING = 1,
    LEONOS_CMD_JOB_STOPPED = 2,
    LEONOS_CMD_JOB_DONE = 3,
};

struct leonos_cmd_job {
    int used;
    int id;
    int state;
    int process_count;
    int remaining;
    int last_pid;
    int exit_code;
    int pids[LEONOS_CMD_JOB_PROCESS_MAX];
    char text[LEONOS_CMD_JOB_TEXT_MAX];
};

static struct leonos_cmd_job leonos_cmd_jobs[LEONOS_CMD_JOB_MAX];
static int leonos_cmd_next_job_id = 1;

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
extern int leonos_task_snapshot(struct leonos_cmd_task_info *tasks,
                                uint32_t capacity, uint64_t *tick);
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
    "false", "grep", "head", "less", "ls", "mkdir", "mv", "printenv", "printf", "pwd",
    "rm", "rmdir", "sh", "sleep", "tail", "true", "uname", "unlink", "vi", "wc", NULL,
};

struct program_path {
    const char *name;
    const char *path;
};

static const struct program_path programs[] = {
    {"cmd", "0:/programs/cmd/cmd.elf"},
    {"file", "0:/programs/file/file.elf"},
    {"fastfetch", "0:/programs/fastfetch/fastfetch.elf"},
    {"sl", "0:/programs/sl/sl.elf"},
    {"lua", "0:/programs/lua/lua.elf"},
    {"nano", "0:/programs/nano/nano.elf"},
    {"pleditor", "0:/programs/pleditor/pleditor.elf"},
    {"tcc", "0:/programs/tcc/tcc.elf"},
    {NULL, NULL},
};

static int job_name_equal(const char *left, const char *right)
{
    return left && right && command_name_equal(left, right);
}

static int job_id_from_arg(const char *text)
{
    int id = 0;
    if (!text) return -1;
    if (*text == '%') ++text;
    if (!*text) return -1;
    while (*text) {
        if (*text < '0' || *text > '9') return -1;
        id = id * 10 + (*text - '0');
        ++text;
    }
    return id > 0 ? id : -1;
}

static int task_is_stopped(int pid)
{
    struct leonos_cmd_task_info tasks[LEONOS_CMD_TASK_MAX];
    uint64_t tick = 0;
    int count = leonos_task_snapshot(tasks, LEONOS_CMD_TASK_MAX, &tick);
    int i;
    (void)tick;
    if (count < 0) return 0;
    for (i = 0; i < count; ++i) {
        if ((int)tasks[i].pid == pid)
            return tasks[i].state == 4;
    }
    return 0;
}

int libcmd_job_is_stopped(int pid)
{
    return task_is_stopped(pid);
}

static struct leonos_cmd_job *job_by_id(int id)
{
    int i;
    for (i = 0; i < (int)LEONOS_CMD_JOB_MAX; ++i) {
        if (leonos_cmd_jobs[i].used && leonos_cmd_jobs[i].id == id)
            return &leonos_cmd_jobs[i];
    }
    return NULL;
}

static struct leonos_cmd_job *job_last(void)
{
    struct leonos_cmd_job *best = NULL;
    int i;
    for (i = 0; i < (int)LEONOS_CMD_JOB_MAX; ++i) {
        struct leonos_cmd_job *job = &leonos_cmd_jobs[i];
        if (job->used && (!best || job->id > best->id)) best = job;
    }
    return best;
}

static void job_refresh(struct leonos_cmd_job *job)
{
    int i;
    int stopped = 0;
    if (!job || !job->used || job->state == LEONOS_CMD_JOB_DONE) return;
    for (i = 0; i < job->process_count; ++i) {
        int status = 0;
        int waited;
        if (job->pids[i] <= 0) continue;
        waited = wait4(job->pids[i], &status, WNOHANG, NULL);
        if (waited == job->pids[i]) {
            if (job->pids[i] == job->last_pid)
                job->exit_code = (status >> 8) & 0xff;
            job->pids[i] = 0;
            if (job->remaining > 0) --job->remaining;
        } else if (waited == 0 && task_is_stopped(job->pids[i])) {
            ++stopped;
        } else if (waited < 0 && waited != -EAGAIN) {
            job->pids[i] = 0;
            if (job->remaining > 0) --job->remaining;
        }
    }
    if (job->remaining == 0) job->state = LEONOS_CMD_JOB_DONE;
    else job->state = stopped == job->remaining ? LEONOS_CMD_JOB_STOPPED : LEONOS_CMD_JOB_RUNNING;
}

static const char *job_state_text(int state)
{
    if (state == LEONOS_CMD_JOB_STOPPED) return "Stopped";
    if (state == LEONOS_CMD_JOB_DONE) return "Done";
    return "Running";
}

static struct leonos_cmd_job *job_add(const int pids[], int count, int last_pid,
                                      const char *text)
{
    struct leonos_cmd_job *job = NULL;
    int i;
    if (!pids || count <= 0 || count > (int)LEONOS_CMD_JOB_PROCESS_MAX) return NULL;
    for (i = 0; i < (int)LEONOS_CMD_JOB_MAX; ++i) {
        if (!leonos_cmd_jobs[i].used || leonos_cmd_jobs[i].state == LEONOS_CMD_JOB_DONE) {
            job = &leonos_cmd_jobs[i];
            break;
        }
    }
    if (!job) return NULL;
    memset(job, 0, sizeof(*job));
    job->used = 1;
    job->id = leonos_cmd_next_job_id++;
    job->state = LEONOS_CMD_JOB_RUNNING;
    job->process_count = count;
    job->remaining = count;
    job->last_pid = last_pid;
    for (i = 0; i < count; ++i) job->pids[i] = pids[i];
    if (text) strncpy(job->text, text, sizeof(job->text) - 1);
    return job;
}

static void job_append_word(char *out, size_t cap, const char *word)
{
    size_t pos = strlen(out);
    size_t i = 0;
    if (pos && pos + 1 < cap) out[pos++] = ' ';
    while (word && word[i] && pos + 1 < cap) out[pos++] = word[i++];
    out[pos] = '\0';
}

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

static int spawn_argv_with_fds(const char *path, char *const argv[],
                               char *const envp[], int wait,
                               int stdin_fd, int stdout_fd, int stderr_fd,
                               libcmd_exit_info_t *exit_info)
{
    char *spawn_argv[LEONOS_SPAWN_ARG_MAX + 1];
    int busybox_dispatch;
    int pid;
    int status = 0;
    int waited;
    size_t count = 0;
    int pty_id;

    if (!path || !path[0] || !argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    busybox_dispatch = strcmp(path, "0:/programs/busybox/busybox.elf") == 0;
    if (busybox_dispatch) {
        /* Keep the image path separate from argv[0]: BusyBox recognises
         * multi-call invocation only as "busybox <applet>". */
        spawn_argv[count++] = "busybox";
    }
    while (argv[count - (busybox_dispatch ? 1U : 0U)]) {
        size_t source_index = count - (busybox_dispatch ? 1U : 0U);
        if (count >= LEONOS_SPAWN_ARG_MAX) {
            errno = E2BIG;
            return -1;
        }
        spawn_argv[count++] = argv[source_index];
    }
    spawn_argv[count] = NULL;
    pty_id = leonos_pty_self();
    pid = leonos_pty_spawn_argv_with_fds(path, pty_id > 0 ? (uint32_t)pty_id : 0U,
                                         spawn_argv, envp ? envp : environ,
                                         stdin_fd, stdout_fd, stderr_fd);
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
        if (waited != -EAGAIN) {
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
    (void)nice_level;
    return spawn_argv_with_fds(path, argv, envp, 1, stdin_fd, stdout_fd,
                               stderr_fd, exit_info);
}

int libcmd_exec_async(const char *path, char *const argv[], char *const envp[],
                      int stdin_fd, int stdout_fd, int stderr_fd, int nice_level)
{
    (void)nice_level;
    return spawn_argv_with_fds(path, argv, envp, 0, stdin_fd, stdout_fd,
                               stderr_fd, NULL);
}

int libcmd_wait_pid(int pid, libcmd_exit_info_t *exit_info)
{
    int status = 0;
    int waited;
    for (;;) {
        waited = wait4(pid, &status, 0, NULL);
        if (waited == pid)
            break;
        if (waited != -EAGAIN) {
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
    int pids[64];
    int prev_read = -1;
    int prev_owned = 0;
    int i;
    if (!cmds || !paths || n <= 0 || n > 64) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < n; ++i) {
        int fds[2] = {-1, -1};
        int child_stdin;
        int child_stdout;
        int pid;
        if (!cmds[i] || !cmds[i][0] || !paths[i] || !paths[i][0]) {
            errno = EINVAL;
            goto fail;
        }
        if (i + 1 < n && pipe(fds) < 0)
            goto fail;
        child_stdin = i == 0 ? (stdin_fd >= 0 ? stdin_fd : 0) : prev_read;
        child_stdout = i + 1 < n ? fds[1] : (stdout_fd >= 0 ? stdout_fd : 1);
        pid = spawn_argv_with_fds(paths[i], (char *const *)cmds[i], envp,
                                  0, child_stdin, child_stdout, 2, NULL);
        if (pid < 0) {
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);
            goto fail;
        }
        pids[i] = pid;
        if (prev_owned) close(prev_read);
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
        prev_owned = fds[0] >= 0;
    }
    if (prev_owned) close(prev_read);
    for (i = 0; i < n; ++i) {
        libcmd_exit_info_t current;
        if (libcmd_wait_pid(pids[i], &current) < 0)
            return -1;
        if (i == n - 1 && exit_info)
            *exit_info = current;
    }
    return 0;

fail:
    if (prev_owned) close(prev_read);
    while (i-- > 0) {
        (void)kill(pids[i], SIGTERM);
        (void)libcmd_wait_pid(pids[i], NULL);
    }
    return -1;
}

int libcmd_exec_pipeline_async(char *const *const *cmds, const char *const *paths, int n,
                               char *const envp[], int stdin_fd, int stdout_fd,
                               int pids[], int pids_capacity)
{
    int prev_read = -1;
    int prev_owned = 0;
    int i;
    if (!cmds || !paths || !pids || n <= 0 || n > pids_capacity || n > 64) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < n; ++i) {
        int fds[2] = {-1, -1};
        int child_stdin;
        int child_stdout;
        int pid;
        if (!cmds[i] || !cmds[i][0] || !paths[i] || !paths[i][0]) {
            errno = EINVAL;
            goto fail;
        }
        if (i + 1 < n && pipe(fds) < 0) goto fail;
        child_stdin = i == 0 ? (stdin_fd >= 0 ? stdin_fd : 0) : prev_read;
        child_stdout = i + 1 < n ? fds[1] : (stdout_fd >= 0 ? stdout_fd : 1);
        pid = spawn_argv_with_fds(paths[i], (char *const *)cmds[i], envp, 0,
                                  child_stdin, child_stdout, 2, NULL);
        if (pid < 0) {
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);
            goto fail;
        }
        pids[i] = pid;
        if (prev_owned) close(prev_read);
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
        prev_owned = fds[0] >= 0;
    }
    if (prev_owned) close(prev_read);
    return n;

fail:
    if (prev_owned) close(prev_read);
    while (i-- > 0) (void)kill(pids[i], SIGTERM);
    return -1;
}

int leonos_cmd_builtin(int argc, char **argv, int *handled)
{
    struct leonos_cmd_job *job;
    int id;
    int i;
    if (handled) *handled = 0;
    if (argc <= 0 || !argv || !argv[0]) return 0;
    if (job_name_equal(argv[0], "jobs")) {
        if (handled) *handled = 1;
        for (i = 0; i < (int)LEONOS_CMD_JOB_MAX; ++i) {
            job = &leonos_cmd_jobs[i];
            if (!job->used) continue;
            job_refresh(job);
            if (job->state == LEONOS_CMD_JOB_DONE)
                printf("[%d] %s %s (exit %d)\n", job->id, job_state_text(job->state),
                       job->text, job->exit_code);
            else
                printf("[%d] %s %s\n", job->id, job_state_text(job->state), job->text);
        }
        return 0;
    }
    if (!job_name_equal(argv[0], "fg") && !job_name_equal(argv[0], "bg")) return 0;
    if (handled) *handled = 1;
    if (argc > 2) {
        fputs("cmd: fg/bg accepts at most one job id\n", stderr);
        return 1;
    }
    id = argc == 2 ? job_id_from_arg(argv[1]) : -1;
    job = id > 0 ? job_by_id(id) : job_last();
    if (!job) {
        fputs("cmd: no such job\n", stderr);
        return 1;
    }
    job_refresh(job);
    if (job->state == LEONOS_CMD_JOB_DONE) {
        fprintf(stderr, "cmd: job [%d] already completed (exit %d)\n", job->id, job->exit_code);
        return job->exit_code;
    }
    for (i = 0; i < job->process_count; ++i) {
        if (job->pids[i] > 0) (void)kill(job->pids[i], SIGCONT);
    }
    job->state = LEONOS_CMD_JOB_RUNNING;
    if (job_name_equal(argv[0], "bg")) {
        printf("[%d] %s\n", job->id, job->text);
        return 0;
    }
    for (i = 0; i < job->process_count; ++i) {
        libcmd_exit_info_t exit_info;
        if (job->pids[i] <= 0) continue;
        if (libcmd_wait_pid(job->pids[i], &exit_info) < 0) return 1;
        if (job->pids[i] == job->last_pid) job->exit_code = exit_info.exit_code;
        job->pids[i] = 0;
        if (job->remaining > 0) --job->remaining;
    }
    job->state = LEONOS_CMD_JOB_DONE;
    return job->exit_code;
}

int leonos_cmd_register_job(const int pids[], int count, int last_pid, const char *text)
{
    struct leonos_cmd_job *job = job_add(pids, count, last_pid, text);
    if (!job) {
        errno = EAGAIN;
        return -1;
    }
    printf("[%d] %d\n", job->id, last_pid);
    return 0;
}

void leonos_cmd_job_append_word(char *out, size_t cap, const char *word)
{
    job_append_word(out, cap, word);
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
