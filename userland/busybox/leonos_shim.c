/* POSIX-shaped file helpers backed by the LeonOS userland ABI. */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

extern int sleep_ms(unsigned long milliseconds);
extern unsigned long leonos_uptime_ms(void);
extern char **environ;

/* Ash and libbb/lineedit share this latch when an input wait is interrupted.
 * The rest of BusyBox's signals.c is intentionally replaced by the LeonOS
 * signal shim below, so keep this small state definition here as well. */
signed char bb_got_signal;

void record_signo(int signal_number)
{
    bb_got_signal = (signed char)signal_number;
}

/* POSIX requires environ to remain a valid, NULL-terminated vector.  Ash
 * calls clearenv() while preparing noexec applets, so never leave it NULL. */
static char *leonos_empty_environment[] = { 0 };

const char *leonos_shell_command_path(const char *name);

/*
 * LeonOS terminals are inherited standard streams, not reopenable named tty
 * nodes.  BusyBox less accepts this condition and safely falls back to its
 * stdout descriptor for keyboard input.
 */
int ttyname_r(int fd, char *buffer, size_t length)
{
    (void)fd;
    (void)buffer;
    (void)length;
    errno = ENOTTY;
    return ENOTTY;
}

/* Signal handlers remain a compatibility stub; kill() termination is provided
 * by the kernel syscall ABI below. */
void bb_signals(int signals, void (*handler)(int))
{
    (void)signals;
    (void)handler;
}

int sigaction_set(int signal_number, const struct sigaction *action)
{
    return sigaction(signal_number, action, NULL);
}

void kill_myself_with_sig(int signal_number)
{
    exit(128 + signal_number);
}

int sigprocmask_allsigs(int how)
{
    sigset_t signals;
    sigfillset(&signals);
    return sigprocmask(how, &signals, NULL);
}

int sigprocmask2(int how, sigset_t *signals)
{
    return sigprocmask(how, signals, signals);
}

static void leonos_zero(void *buffer, uint32_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;
    for (index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
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

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)path;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    return chown(path, owner, group);
}

int mknod(const char *path, mode_t mode, dev_t device)
{
    (void)path;
    (void)mode;
    (void)device;
    errno = ENOSYS;
    return -1;
}

int utimes(const char *path, const struct timeval times[2])
{
    (void)path;
    (void)times;
    errno = ENOSYS;
    return -1;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

int getgroups(int count, gid_t groups[])
{
    (void)groups;
    if (count < 0) {
        errno = EINVAL;
        return -1;
    }
    /* LeonOS currently has no supplementary-group database. */
    return 0;
}

const char *leonos_shell_command_path(const char *name)
{
    if (!name || !name[0]) return 0;
    if (strchr(name, '/') || strchr(name, ':')) return name;
    if (strcmp(name, "nano") == 0) return "0:/programs/nano/nano.elf";
    if (strcmp(name, "pleditor") == 0) return "0:/programs/pleditor/pleditor.elf";
    if (strcmp(name, "tcc") == 0) return "0:/programs/tcc/tcc.elf";
    if (strcmp(name, "lua") == 0) return "0:/programs/lua/lua.elf";
    if (strcmp(name, "file") == 0) return "0:/programs/file/file.elf";
    if (strcmp(name, "fastfetch") == 0) return "0:/programs/fastfetch/fastfetch.elf";
    if (strcmp(name, "less") == 0) return "0:/programs/less/less.elf";
    if (strcmp(name, "sl") == 0) return "0:/programs/sl/sl.elf";
    if (strcmp(name, "cmd") == 0) return "0:/programs/cmd/cmd.elf";
    return 0;
}

static int leonos_exec_busybox_applet(char *const argv[])
{
    size_t argc = 0;
    size_t index;
    char **exec_argv;
    int result;
    int saved_errno;

    if (!argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
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
    exec_argv[argc + 1U] = 0;
    result = execve("0:/programs/busybox/busybox.elf", exec_argv, environ);
    saved_errno = errno;
    free(exec_argv);
    errno = saved_errno;
    return result;
}

int execvp(const char *file, char *const argv[])
{
    const char *path = leonos_shell_command_path(file);
    if (path) {
        return execve(path, argv, environ);
    }

    /* Other BusyBox callers may use execvp directly. Ash uses its dedicated
     * image resolver patch, while this fallback still expresses a bare
     * applet through BusyBox's documented process form. */
    if (!argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    return leonos_exec_busybox_applet(argv);
}

int glob(const char *pattern, int flags,
         int (*error_callback)(const char *, int), glob_t *matches)
{
    (void)pattern;
    (void)flags;
    (void)error_callback;
    if (matches) {
        leonos_zero(matches, sizeof(*matches));
    }
    return GLOB_NOMATCH;
}

void globfree(glob_t *matches)
{
    (void)matches;
}

unsigned long long monotonic_ms(void)
{
    return (unsigned long long)leonos_uptime_ms();
}

unsigned long long monotonic_us(void)
{
    return (unsigned long long)leonos_uptime_ms() * 1000ULL;
}

unsigned bb_clk_tck(void)
{
    return 1000U;
}

ssize_t readlink(const char *path, char *buffer, size_t length)
{
    (void)path;
    (void)buffer;
    (void)length;
    return -1;
}

int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

mode_t umask(mode_t mode)
{
    (void)mode;
    return 0;
}

int uname(struct utsname *name)
{
    struct leonos_system_info system_info;
    struct leonos_machine_identity identity = {0};
    char node_name[_UTSNAME_LENGTH];
    uint32_t node_pos = 0;
    if (!name) {
        return -1;
    }
    leonos_zero(name, sizeof(*name));
    if (leonos_system_info(&system_info) < 0) return -1;
    (void)leonos_machine_identity(&identity);
    strncpy(name->sysname, system_info.kernel_name, sizeof(name->sysname) - 1U);
    strncpy(name->release, system_info.kernel_version, sizeof(name->release) - 1U);
    strncpy(name->version, system_info.build_time, sizeof(name->version) - 1U);
    strncpy(name->machine, system_info.architecture,
            sizeof(name->machine) - 1U);
    strncpy(node_name, "leonos", sizeof(node_name) - 1U);
    node_pos = (uint32_t)strlen(node_name);
    if (identity.platform_uuid[0]) {
        if (node_pos + 1U < sizeof(node_name)) node_name[node_pos++] = '-';
        for (uint32_t index = 0; identity.platform_uuid[index] &&
             node_pos + 1U < sizeof(node_name); ++index) {
            char ch = identity.platform_uuid[index];
            if (ch == '-') continue;
            node_name[node_pos++] = ch;
        }
    }
    node_name[node_pos] = 0;
    strncpy(name->nodename, node_name, sizeof(name->nodename) - 1U);
    return 0;
}

int clearenv(void)
{
    if (environ) {
        environ[0] = 0;
    } else {
        environ = leonos_empty_environment;
    }
    return 0;
}
