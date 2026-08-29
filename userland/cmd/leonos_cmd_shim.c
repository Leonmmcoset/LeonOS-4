/* LeonOS compatibility layer for the ChenPi11/cmd POSIX implementation. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "glibcmd.h"

#include <errno.h>
#include <fcntl.h>
#include <leonos/posix.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

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
    int process_group;
    int exit_code;
    int pids[LEONOS_CMD_JOB_PROCESS_MAX];
    char text[LEONOS_CMD_JOB_TEXT_MAX];
};

static struct leonos_cmd_job leonos_cmd_jobs[LEONOS_CMD_JOB_MAX];
static int leonos_cmd_next_job_id = 1;

extern int leonos_task_snapshot(struct leonos_cmd_task_info *tasks,
                                uint32_t capacity, uint64_t *tick);
extern unsigned long leonos_uptime_ms(void);
extern char **environ;

static void fill_exit_info(int status, libcmd_exit_info_t *exit_info)
{
    if (!exit_info) {
        return;
    }
    exit_info->exited = WIFEXITED(status);
    exit_info->exit_code = exit_info->exited ? WEXITSTATUS(status) : 0;
    exit_info->signaled = WIFSIGNALED(status);
    exit_info->signal = exit_info->signaled ? WTERMSIG(status) : 0;
    if (exit_info->signaled) {
        exit_info->exit_code = 128 + exit_info->signal;
    }
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
    struct leonos_system_info system_info;
    struct leonos_machine_identity identity;
    char node_name[_UTSNAME_LENGTH];
    uint32_t node_pos;
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    memset(name, 0, sizeof(*name));
    if (leonos_system_info(&system_info) < 0) {
        errno = EIO;
        return -1;
    }
    memset(&identity, 0, sizeof(identity));
    leonos_machine_identity(&identity);
    strncpy(name->sysname, system_info.kernel_name, sizeof(name->sysname) - 1U);
    strncpy(name->release, system_info.kernel_version, sizeof(name->release) - 1U);
    strncpy(name->version, system_info.build_time, sizeof(name->version) - 1U);
    strncpy(name->machine, system_info.architecture,
            sizeof(name->machine) - 1U);
    strncpy(node_name, "leonos", sizeof(node_name) - 1U);
    node_pos = (uint32_t)strlen(node_name);
    if (identity.platform_uuid[0] && node_pos + 1U < sizeof(node_name)) {
        node_name[node_pos++] = '-';
    }
    for (uint32_t index = 0; identity.platform_uuid[index] &&
         node_pos + 1U < sizeof(node_name); ++index) {
        char ch = identity.platform_uuid[index];
        if (ch == '-') continue;
        node_name[node_pos++] = ch;
    }
    node_name[node_pos] = 0;
    strncpy(name->nodename, node_name, sizeof(name->nodename) - 1U);
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
    return strchr(name, '/') != NULL;
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
    "basename", "busybox", "cat", "clear", "cp", "diff", "dirname", "echo", "env",
    "false", "grep", "head", "ls", "mkdir", "mv", "printenv", "printf", "pwd",
    "rm", "rmdir", "sha256sum", "sh", "sleep", "tail", "true", "uname", "unlink", "vi", "wc", NULL,
};

struct program_path {
    const char *name;
    const char *path;
};

static const struct program_path programs[] = {
    {"cmd", "/programs/cmd/cmd.elf"},
    {"file", "/programs/file/file.elf"},
    {"fastfetch", "/programs/fastfetch/fastfetch.elf"},
    {"less", "/programs/less/less.elf"},
    {"sl", "/programs/sl/sl.elf"},
    {"lua", "/programs/lua/lua.elf"},
    {"nano", "/programs/nano/nano.elf"},
    {"pleditor", "/programs/pleditor/pleditor.elf"},
    {"tcc", "/programs/tcc/tcc.elf"},
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
        waited = waitpid(job->pids[i], &status, WNOHANG);
        if (waited == job->pids[i]) {
            if (job->pids[i] == job->last_pid) {
                libcmd_exit_info_t exit_info;
                fill_exit_info(status, &exit_info);
                job->exit_code = exit_info.exit_code;
            }
            job->pids[i] = 0;
            if (job->remaining > 0) --job->remaining;
        } else if (waited == 0 && task_is_stopped(job->pids[i])) {
            ++stopped;
        } else if (waited < 0 && errno == ECHILD) {
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
    job->process_group = pids[0];
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
            return copy_exec_path(out, out_size, "/programs/busybox/busybox.elf");
    }
    for (index = 0; programs[index].name; ++index) {
        if (command_name_equal(name, programs[index].name))
            return copy_exec_path(out, out_size, programs[index].path);
    }
    return -1;
}

static void child_setup_fds(int stdin_fd, int stdout_fd, int stderr_fd)
{
    if (stdin_fd >= 0 && stdin_fd != STDIN_FILENO) {
        (void)dup2(stdin_fd, STDIN_FILENO);
        (void)close(stdin_fd);
    }
    if (stdout_fd >= 0 && stdout_fd != STDOUT_FILENO) {
        (void)dup2(stdout_fd, STDOUT_FILENO);
        (void)close(stdout_fd);
    }
    if (stderr_fd >= 0 && stderr_fd != STDERR_FILENO) {
        (void)dup2(stderr_fd, STDERR_FILENO);
        (void)close(stderr_fd);
    }
}

static int child_exec_path(const char *path, char *const argv[], char *const envp[])
{
    int busybox_dispatch;

    if (!path || !path[0] || !argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    busybox_dispatch = strcmp(path, "/programs/busybox/busybox.elf") == 0;

    if (!busybox_dispatch) {
        return execve(path, argv, envp ? envp : environ);
    }

    /* FAT32 cannot represent BusyBox applet symlinks.  Keep the executable
     * path separate from argv[0] and construct BusyBox's documented
     * "busybox <applet> ..." form without imposing an arbitrary argv cap. */
    {
        size_t argc = 0;
        size_t index;
        char **exec_argv;
        int result;
        int saved_errno;

        while (argv[argc]) {
            ++argc;
        }
        if (argc > (((size_t)-1) / sizeof(*exec_argv)) - 2U) {
            errno = E2BIG;
            return -1;
        }
        exec_argv = malloc((argc + 2U) * sizeof(*exec_argv));
        if (!exec_argv) {
            errno = ENOMEM;
            return -1;
        }
        exec_argv[0] = "busybox";
        for (index = 0; index < argc; ++index) {
            exec_argv[index + 1U] = argv[index];
        }
        exec_argv[argc + 1U] = NULL;
        result = execve(path, exec_argv, envp ? envp : environ);
        saved_errno = errno;
        free(exec_argv);
        errno = saved_errno;
        return result;
    }
}

/* Foreground commands are placed in their own process group before the shell
 * transfers the PTY.  A non-terminal stdin is valid for batch execution. */
int leonos_cmd_set_process_group(int pid, int process_group)
{
    if (pid <= 0 || process_group <= 0) {
        errno = EINVAL;
        return -1;
    }
    return setpgid((pid_t)pid, (pid_t)process_group);
}

int leonos_cmd_foreground_enter(int fd, int process_group, int *saved_group)
{
    int previous;

    if (saved_group) {
        *saved_group = 0;
    }
    if (fd < 0 || process_group <= 0) {
        errno = EINVAL;
        return -1;
    }
    previous = (int)tcgetpgrp(fd);
    if (previous < 0) {
        return 0;
    }
    if (tcsetpgrp(fd, (pid_t)process_group) < 0) {
        return -1;
    }
    if (saved_group) {
        *saved_group = previous;
    }
    return 0;
}

void leonos_cmd_foreground_leave(int fd, int saved_group)
{
    if (fd >= 0 && saved_group > 0) {
        (void)tcsetpgrp(fd, (pid_t)saved_group);
    }
}

int libcmd_exec_sync(const char *path, char *const argv[], char *const envp[],
                     int stdin_fd, int stdout_fd, int stderr_fd, int nice_level,
                     libcmd_exit_info_t *exit_info)
{
    pid_t pid = fork();
    int status;
    int saved_group = 0;

    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        (void)libcmd_set_process_priority(nice_level);
        child_setup_fds(stdin_fd, stdout_fd, stderr_fd);
        (void)child_exec_path(path, argv, envp);
        _exit(127);
    }
    (void)leonos_cmd_set_process_group((int)pid, (int)pid);
    (void)leonos_cmd_foreground_enter(stdin_fd, (int)pid, &saved_group);
    if (waitpid(pid, &status, 0) < 0) {
        leonos_cmd_foreground_leave(stdin_fd, saved_group);
        return -1;
    }
    leonos_cmd_foreground_leave(stdin_fd, saved_group);
    fill_exit_info(status, exit_info);
    return 0;
}

int libcmd_exec_async(const char *path, char *const argv[], char *const envp[],
                      int stdin_fd, int stdout_fd, int stderr_fd, int nice_level)
{
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        (void)setsid();
        (void)libcmd_set_process_priority(nice_level);
        child_setup_fds(stdin_fd, stdout_fd, stderr_fd);
        (void)child_exec_path(path, argv, envp);
        _exit(127);
    }
    return (int)pid;
}

/* cmd.exe START needs a detached session; shell background jobs do not. */
int libcmd_exec_job_async(const char *path, char *const argv[], char *const envp[],
                          int stdin_fd, int stdout_fd, int stderr_fd, int nice_level)
{
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        (void)libcmd_set_process_priority(nice_level);
        child_setup_fds(stdin_fd, stdout_fd, stderr_fd);
        (void)child_exec_path(path, argv, envp);
        _exit(127);
    }
    (void)leonos_cmd_set_process_group((int)pid, (int)pid);
    return (int)pid;
}

int libcmd_wait_pid(int pid, libcmd_exit_info_t *exit_info)
{
    int status = 0;
    if (waitpid((pid_t)pid, &status, 0) < 0) {
        return -1;
    }
    fill_exit_info(status, exit_info);
    return 0;
}

int libcmd_fork(void)
{
    return (int)fork();
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
    int process_group = 0;
    int saved_group = 0;
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
        pid = fork();
        if (pid < 0) {
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);
            goto fail;
        }
        if (pid == 0) {
            if (fds[0] >= 0) close(fds[0]);
            child_setup_fds(child_stdin, child_stdout, STDERR_FILENO);
            (void)child_exec_path(paths[i], (char *const *)cmds[i], envp);
            _exit(127);
        }
        pids[i] = pid;
        if (!process_group) {
            process_group = pid;
        }
        (void)leonos_cmd_set_process_group(pid, process_group);
        if (prev_owned) close(prev_read);
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
        prev_owned = fds[0] >= 0;
    }
    if (prev_owned) close(prev_read);
    (void)leonos_cmd_foreground_enter(stdin_fd, process_group, &saved_group);
    for (i = 0; i < n; ++i) {
        libcmd_exit_info_t current;
        if (libcmd_wait_pid(pids[i], &current) < 0) {
            leonos_cmd_foreground_leave(stdin_fd, saved_group);
            return -1;
        }
        if (i == n - 1 && exit_info)
            *exit_info = current;
    }
    leonos_cmd_foreground_leave(stdin_fd, saved_group);
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
    int process_group = 0;
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
        pid = fork();
        if (pid < 0) {
            if (fds[0] >= 0) close(fds[0]);
            if (fds[1] >= 0) close(fds[1]);
            goto fail;
        }
        if (pid == 0) {
            if (fds[0] >= 0) close(fds[0]);
            child_setup_fds(child_stdin, child_stdout, STDERR_FILENO);
            (void)child_exec_path(paths[i], (char *const *)cmds[i], envp);
            _exit(127);
        }
        pids[i] = pid;
        if (!process_group) {
            process_group = pid;
        }
        (void)leonos_cmd_set_process_group(pid, process_group);
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
    if (job->process_group > 0) {
        (void)kill(-job->process_group, SIGCONT);
    }
    job->state = LEONOS_CMD_JOB_RUNNING;
    if (job_name_equal(argv[0], "bg")) {
        printf("[%d] %s\n", job->id, job->text);
        return 0;
    }
    {
        int saved_group = 0;
        (void)leonos_cmd_foreground_enter(STDIN_FILENO, job->process_group,
                                          &saved_group);
        for (i = 0; i < job->process_count; ++i) {
            libcmd_exit_info_t exit_info;
            if (job->pids[i] <= 0) continue;
            if (libcmd_wait_pid(job->pids[i], &exit_info) < 0) {
                leonos_cmd_foreground_leave(STDIN_FILENO, saved_group);
                return 1;
            }
            if (job->pids[i] == job->last_pid) job->exit_code = exit_info.exit_code;
            job->pids[i] = 0;
            if (job->remaining > 0) --job->remaining;
        }
        leonos_cmd_foreground_leave(STDIN_FILENO, saved_group);
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
    return path && path[0] == '/';
}

void libcmd_path_norm_sep(char *path)
{
    (void)path;
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
    if (nice_level == 0) {
        return 0;
    }
    return setpriority(PRIO_PROCESS, 0, nice_level);
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
