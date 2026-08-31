/* POSIX-shaped file helpers backed by the LeonOS userland ABI. */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <leonos/auth.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <sys/reboot.h>
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

int reboot(unsigned int command)
{
    if (command == RB_AUTOBOOT) {
        return leonos_system_reboot();
    }
    return leonos_system_shutdown();
}

void sync(void)
{
    /* LeonOS filesystem writes are committed synchronously. */
}

/* BusyBox's env applet normally gets this helper from libbb/executable.o.
 * That object is intentionally omitted from the LeonOS minimal libbb set;
 * keep the same execvp and SUSv3 exit-code behavior in the port shim. */
extern unsigned char xfunc_error_retval;
void bb_perror_msg_and_die(const char *message, ...);

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

/* Login updates the kernel task identity of the shell's session, but the
 * shell environment was created before login and therefore may not contain a
 * user-specific HOME variable. Ash calls this on demand for ~ expansion. */
const char *leonos_shell_home(void)
{
    static char home[LEONOS_AUTH_HOME_LEN];
    struct leonos_user_info user;
    uint32_t index;
    if (leonos_auth_current(&user) < 0 || !user.uid || !user.home[0]) {
        return 0;
    }
    for (index = 0; index + 1U < sizeof(home) && user.home[index]; ++index) {
        home[index] = user.home[index];
    }
    home[index] = 0;
    return home[0] ? home : 0;
}

/* BusyBox whoami normally resolves the effective UID through /etc/passwd.
 * LeonOS keeps accounts in the authentication service instead, so expose the
 * session username directly. The unauthenticated installer shell runs as the
 * system administrator context and uses the conventional root name. */
const char *leonos_shell_user_name(void)
{
    static char username[LEONOS_AUTH_USERNAME_LEN];
    struct leonos_user_info user;
    uint32_t index;
    if (leonos_auth_current(&user) == 0 && user.username[0]) {
        for (index = 0; index + 1U < sizeof(username) && user.username[index]; ++index) {
            username[index] = user.username[index];
        }
        username[index] = 0;
        return username;
    }
    strncpy(username, "root", sizeof(username) - 1U);
    username[sizeof(username) - 1U] = 0;
    return username;
}

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
    if (strcmp(name, "nano") == 0) return "/programs/nano/nano.elf";
    if (strcmp(name, "pleditor") == 0) return "/programs/pleditor/pleditor.elf";
    if (strcmp(name, "tcc") == 0) return "/programs/tcc/tcc.elf";
    if (strcmp(name, "lua") == 0) return "/programs/lua/lua.elf";
    if (strcmp(name, "file") == 0) return "/programs/file/file.elf";
    if (strcmp(name, "fastfetch") == 0) return "/programs/fastfetch/fastfetch.elf";
    if (strcmp(name, "less") == 0) return "/programs/less/less.elf";
    if (strcmp(name, "sl") == 0) return "/programs/sl/sl.elf";
    if (strcmp(name, "cmd") == 0) return "/programs/cmd/cmd.elf";
    if (strcmp(name, "fdisk") == 0 || strcmp(name, "mkfs.fat") == 0 ||
        strcmp(name, "mkfs.fat32") == 0 || strcmp(name, "mkfs.vfat") == 0 ||
        strcmp(name, "mkfs.ext2") == 0 || strcmp(name, "mkfs.exfat") == 0 ||
        strcmp(name, "mount") == 0 || strcmp(name, "umount") == 0 ||
        strcmp(name, "fsck") == 0 || strcmp(name, "fsck.fat") == 0 ||
        strcmp(name, "fsck.fat32") == 0 || strcmp(name, "fsck.vfat") == 0 ||
        strcmp(name, "fsck.ext2") == 0 || strcmp(name, "fsck.exfat") == 0 ||
        strcmp(name, "blkid") == 0 || strcmp(name, "lsblk") == 0 ||
        strcmp(name, "leonos-grub-installer") == 0 || strcmp(name, "sync") == 0)
        return "/programs/busybox/busybox.elf";
    if (strcmp(name, "gptinit") == 0)
        return "/programs/gptinit/gptinit.elf";
    if (strcmp(name, "oobe") == 0) return "/system/apps/oobe/oobe.elf";
    if (strcmp(name, "login") == 0) return "/system/apps/login/login.elf";
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
    result = execve("/programs/busybox/busybox.elf", exec_argv, environ);
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

__attribute__((__noreturn__)) void BB_EXECVP_or_die(char **argv)
{
    int saved_errno;

    if (!argv || !argv[0]) {
        errno = EINVAL;
        bb_perror_msg_and_die("can't execute");
    }
    execvp(argv[0], argv);
    saved_errno = errno;
    xfunc_error_retval = (saved_errno == ENOENT) ? 127 : 126;
    errno = saved_errno;
    bb_perror_msg_and_die("can't execute '%s'", argv[0]);
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
