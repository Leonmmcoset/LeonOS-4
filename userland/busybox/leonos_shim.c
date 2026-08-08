/* POSIX-shaped file helpers backed by the LeonOS userland ABI. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <leonos/pty.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U
#define LEONOS_DIR_NAME_LEN 128U

struct leonos_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

struct leonos_dir_entry_raw {
    uint32_t type;
    char name[LEONOS_DIR_NAME_LEN];
};

extern int leonos_stat_raw_call(const char *path, struct leonos_stat_raw *st)
    __asm__("stat");
extern int leonos_fstat_raw_call(int fd, struct leonos_stat_raw *st)
    __asm__("fstat");
extern int leonos_readdir(int fd, struct leonos_dir_entry_raw *entry);
extern int sleep_ms(unsigned long milliseconds);
extern unsigned long leonos_uptime_ms(void);
extern int wait4(int pid, int *status, int options, void *rusage);
extern long syscall2(long number, long first, long second);
extern long syscall3(long number, long first, long second, long third);
extern char **environ;

#define LEONOS_SYS_DUP2 33
#define LEONOS_SYS_FCNTL 72
#define LEONOS_SPAWN_ARG_MAX 8U

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

/*
 * The kernel currently has no POSIX signal delivery.  less still registers
 * fatal-signal cleanup handlers; retaining the registration call as a no-op
 * is correct until those signals can actually reach Ring-3 processes.
 */
void bb_signals(int signals, void (*handler)(int))
{
    (void)signals;
    (void)handler;
}

void kill_myself_with_sig(int signal_number)
{
    exit(128 + signal_number);
}

/* Picolibc time() needs gettimeofday(); LeonOS has a monotonic uptime clock. */
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

static void leonos_zero(void *buffer, uint32_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;
    for (index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

static unsigned char leonos_dirent_type(uint32_t type)
{
    if (type == LEONOS_FS_TYPE_DIR) {
        return DT_DIR;
    }
    if (type == LEONOS_FS_TYPE_DEVICE) {
        return DT_CHR;
    }
    return DT_REG;
}

DIR *opendir(const char *path)
{
    DIR *directory;
    int fd;
    if (!path) {
        return 0;
    }
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    directory = (DIR *)malloc(sizeof(*directory));
    if (!directory) {
        close(fd);
        return 0;
    }
    leonos_zero(directory, sizeof(*directory));
    directory->fd = fd;
    return directory;
}

struct dirent *readdir(DIR *directory)
{
    struct leonos_dir_entry_raw entry;
    uint32_t index;
    int result;
    if (!directory) {
        return 0;
    }
    result = leonos_readdir(directory->fd, &entry);
    if (result <= 0) {
        return 0;
    }
    leonos_zero(&directory->dirent, sizeof(directory->dirent));
    directory->dirent.d_type = leonos_dirent_type(entry.type);
    for (index = 0; index + 1U < sizeof(directory->dirent.d_name) && entry.name[index]; ++index) {
        directory->dirent.d_name[index] = entry.name[index];
    }
    return &directory->dirent;
}

int closedir(DIR *directory)
{
    int result;
    if (!directory) {
        return -1;
    }
    result = close(directory->fd);
    free(directory);
    return result;
}

static int leonos_posix_stat_fill(const struct leonos_stat_raw *raw, struct stat *st)
{
    if (!raw || !st) {
        errno = EINVAL;
        return -1;
    }
    leonos_zero(st, sizeof(*st));
    st->st_mode = raw->type == LEONOS_FS_TYPE_DIR ? (S_IFDIR | 0755) :
                  raw->type == LEONOS_FS_TYPE_DEVICE ? (S_IFCHR | 0660) :
                  (S_IFREG | 0644);
    st->st_size = (off_t)raw->size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((raw->size + 511U) / 512U);
    return 0;
}

static int leonos_posix_stat_call(int result, const struct leonos_stat_raw *raw,
                                  struct stat *st)
{
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return leonos_posix_stat_fill(raw, st);
}

/* FAT32 exposes only a compact type/size record through the current ABI.
 * BusyBox's copy and move applets still need stable, distinct identity fields
 * to reject copying a file onto itself, so derive a deterministic inode from
 * the user-visible path in the path-based stat adapter. */
static ino_t leonos_path_inode(const char *path)
{
    const unsigned char *cursor = (const unsigned char *)path;
    uint64_t hash = 1469598103934665603ULL;
    while (cursor && *cursor) {
        hash ^= *cursor++;
        hash *= 1099511628211ULL;
    }
    hash ^= hash >> 32;
    hash &= 0x7fffffffffffffffULL;
    return (ino_t)(hash ? hash : 1ULL);
}

/* BusyBox is compiled with stat/fstat remapped to these POSIX ABI adapters. */
int leonos_posix_stat(const char *path, struct stat *st)
{
    struct leonos_stat_raw raw;
    int result;
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    result = leonos_stat_raw_call(path, &raw);
    if (leonos_posix_stat_call(result, &raw, st) < 0) {
        return -1;
    }
    st->st_dev = 1;
    st->st_ino = leonos_path_inode(path);
    return 0;
}

int leonos_posix_fstat(int fd, struct stat *st)
{
    struct leonos_stat_raw raw;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    return leonos_posix_stat_call(leonos_fstat_raw_call(fd, &raw), &raw, st);
}

int lstat(const char *path, struct stat *st)
{
    return leonos_posix_stat(path, st);
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

int fcntl(int fd, int command, ...)
{
    va_list args;
    long argument = 0;
    long result;
    va_start(args, command);
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC ||
        command == F_SETFD || command == F_SETFL) {
        argument = va_arg(args, int);
    }
    va_end(args);
    result = syscall3(LEONOS_SYS_FCNTL, fd, command, argument);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int dup2(int old_fd, int new_fd)
{
    long result = syscall2(LEONOS_SYS_DUP2, old_fd, new_fd);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

uid_t getuid(void)
{
    return 0;
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

pid_t getppid(void)
{
    return 0;
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

/* Execute one command through the kernel's spawn ABI and wait for it. */
int leonos_spawn_wait_argv(const char *path, char *const argv[])
{
    int pty_id = leonos_pty_self();
    int pid;
    int status = 0;
    int waited;
    if (!path || !path[0] || !argv) {
        errno = EINVAL;
        return 127;
    }
    pid = leonos_pty_spawn_argv(path, pty_id > 0 ? (uint32_t)pty_id : 0U,
                                argv, environ);
    if (pid < 0) {
        errno = -pid;
        return 127;
    }
    for (;;) {
        waited = wait4(pid, &status, 0, 0);
        if (waited == pid) {
            return (status >> 8) & 0xff;
        }
        if (waited == -ECHILD) {
            sleep_ms(1);
            continue;
        }
        errno = waited < 0 ? -waited : ECHILD;
        return 127;
    }
}

/* Use BusyBox's documented "busybox <applet>" process form. */
int leonos_spawn_busybox_applet_wait(const char *path, char *const applet_argv[])
{
    char *exec_argv[LEONOS_SPAWN_ARG_MAX + 1];
    uint32_t count = 0;

    if (!path || !path[0] || !applet_argv || !applet_argv[0]) {
        errno = EINVAL;
        return 127;
    }
    exec_argv[count++] = (char *)path;
    while (applet_argv[count - 1U]) {
        if (count >= LEONOS_SPAWN_ARG_MAX) {
            errno = E2BIG;
            return 127;
        }
        exec_argv[count] = applet_argv[count - 1U];
        ++count;
    }
    exec_argv[count] = 0;
    return leonos_spawn_wait_argv(path, exec_argv);
}

int pipe(int filedes[2])
{
    (void)filedes;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    int result;
    for (;;) {
        result = wait4(pid, status, options, 0);
        if (result >= 0 || (options & 1)) {
            return (pid_t)result;
        }
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
        leonos_zero(previous, sizeof(*previous));
        previous->sa_handler = SIG_DFL;
    }
    return 0;
}

int access(const char *path, int mode)
{
    struct leonos_stat_raw raw;
    (void)mode;
    return path && leonos_stat_raw_call(path, &raw) >= 0 ? 0 : -1;
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

unsigned bb_clk_tck(void)
{
    return 1000U;
}

const char *get_signame(int signal_number)
{
    (void)signal_number;
    return "signal";
}

int poll(struct pollfd *fds, nfds_t count, int timeout_ms)
{
    nfds_t index;
    unsigned long started = leonos_uptime_ms();
    for (;;) {
        int ready = 0;
        int stdin_available = leonos_pty_input_available();
        for (index = 0; index < count; ++index) {
            fds[index].revents = 0;
            if (fds[index].fd < 0 || !(fds[index].events & POLLIN)) {
                continue;
            }
            if (fds[index].fd != STDIN_FILENO || stdin_available != 0) {
                fds[index].revents = POLLIN;
                ++ready;
            }
        }
        if (ready || timeout_ms == 0) {
            return ready;
        }
        if (timeout_ms > 0 && leonos_uptime_ms() - started >= (unsigned long)timeout_ms) {
            return 0;
        }
        sleep_ms(4);
    }
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

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    unsigned long milliseconds;
    (void)remaining;
    if (!request) {
        return -1;
    }
    milliseconds = (unsigned long)request->tv_sec * 1000UL +
                  (unsigned long)request->tv_nsec / 1000000UL;
    return sleep_ms(milliseconds);
}

int uname(struct utsname *name)
{
    static const char system_name[] = "LeonOS";
    static const char node_name[] = "leonos";
    static const char release_name[] = "4";
    static const char version_name[] = "LeonOS 4 userland";
    static const char machine_name[] = "x86_64";
    if (!name) {
        return -1;
    }
    leonos_zero(name, sizeof(*name));
    for (uint32_t index = 0; index + 1U < sizeof(name->sysname) && system_name[index]; ++index) {
        name->sysname[index] = system_name[index];
    }
    for (uint32_t index = 0; index + 1U < sizeof(name->nodename) && node_name[index]; ++index) {
        name->nodename[index] = node_name[index];
    }
    for (uint32_t index = 0; index + 1U < sizeof(name->release) && release_name[index]; ++index) {
        name->release[index] = release_name[index];
    }
    for (uint32_t index = 0; index + 1U < sizeof(name->version) && version_name[index]; ++index) {
        name->version[index] = version_name[index];
    }
    for (uint32_t index = 0; index + 1U < sizeof(name->machine) && machine_name[index]; ++index) {
        name->machine[index] = machine_name[index];
    }
    return 0;
}

int clearenv(void)
{
    environ = 0;
    return 0;
}
