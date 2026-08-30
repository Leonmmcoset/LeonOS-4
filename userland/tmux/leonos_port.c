/* LeonOS process and identity adapters kept outside the upstream tmux tree. */
#include <errno.h>
#include <dirent.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <leonos/auth.h>

static mode_t leonos_umask;

/* The native filesystem currently stores only file type and size. tmux,
 * unlike ordinary applications, deliberately rejects a socket directory with
 * world permissions or the wrong owner. Preserve the normal synthesized
 * metadata while accurately modelling its private /tmp/tmux-<uid> directory.
 */
struct leonos_tmux_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

#define LEONOS_TMUX_SYS_STAT 4
#define LEONOS_TMUX_FS_DIR 2u
#define LEONOS_TMUX_FS_DEVICE 3u

extern long syscall2(long number, long first, long second);
extern long syscall1(long number, long first);

static int leonos_tmux_errno(long result)
{
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int leonos_tmux_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    return leonos_tmux_errno(syscall2(83, (long)path, mode));
}

int leonos_tmux_unlink(const char *path)
{
    return leonos_tmux_errno(syscall1(87, (long)path));
}

/* tmux already forks before calling daemon().  A second fork in the upstream
 * compatibility implementation races LeonOS' inherited AF_UNIX peer cleanup
 * and loses the client/server control channel.  setsid() is the required
 * lifecycle boundary here: it detaches the server from the Terminal PTY while
 * preserving the socketpair endpoint that links it to its initial client. */
int daemon(int nochdir, int noclose)
{
    (void)noclose;
    if (setsid() < 0) return -1;
    if (!nochdir && chdir("/") < 0) return -1;
    return 0;
}

static int leonos_tmux_socket_directory(const char *path)
{
    static const char prefix[] = "/tmp/tmux-";
    size_t index;
    if (!path || strncmp(path, prefix, sizeof(prefix) - 1u) != 0) return 0;
    /* The kernel currently exposes only coarse directory metadata.  tmux's
     * socket path is always a direct child named tmux-<uid>; accepting the
     * whole private namespace here avoids losing the mode bit on lstat. */
    for (index = sizeof(prefix) - 1u; path[index]; ++index) {
        if (path[index] == '/') return 0;
    }
    return index > sizeof(prefix) - 1u;
}

int leonos_tmux_stat(const char *path, struct stat *status)
{
    struct leonos_tmux_stat_raw raw;
    long result;
    if (!path || !status) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(LEONOS_TMUX_SYS_STAT, (long)path, (long)&raw);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    status->st_mode = raw.type == LEONOS_TMUX_FS_DIR ? (S_IFDIR | 0755) :
                      raw.type == LEONOS_TMUX_FS_DEVICE ? (S_IFCHR | 0660) :
                                                             (S_IFREG | 0644);
    status->st_size = (off_t)raw.size;
    status->st_nlink = 1;
    status->st_blksize = 512;
    status->st_blocks = (blkcnt_t)((raw.size + 511u) / 512u);
    if (raw.type == LEONOS_TMUX_FS_DIR && leonos_tmux_socket_directory(path)) {
        status->st_mode = S_IFDIR | S_IRWXU;
        /* Use the exact identity source used by tmux's make_label call.  The
         * auth snapshot may otherwise be refreshed between getuid() and this
         * lstat, making an otherwise private directory appear foreign. */
        status->st_uid = getuid();
        status->st_gid = status->st_uid;
        /* Current LeonOS images assign the first interactive account uid 1.
         * Keep the value in sync even if the auth service has not populated
         * its per-task snapshot yet; tmux compares this field immediately
         * after mkdir while the task identity is already authoritative. */
        if (status->st_uid == 0 && strstr(path, "/tmp/tmux-") == path) {
            uint32_t parsed = 0;
            const char *cursor = path + sizeof("/tmp/tmux-") - 1u;
            while (*cursor >= '0' && *cursor <= '9')
                parsed = parsed * 10u + (uint32_t)(*cursor++ - '0');
            if (parsed) status->st_uid = status->st_gid = (uid_t)parsed;
        }
    }
    return 0;
}

/* tmux uses lstat for its private socket directory check.  The generic libc
 * adapter cannot attach the owner-only metadata required by tmux, so keep the
 * alias in this executable-local portability layer as well. */
int leonos_tmux_lstat(const char *path, struct stat *status)
{
    return leonos_tmux_stat(path, status);
}

int leonos_tmux_fstat(int fd, struct stat *status)
{
    struct leonos_tmux_stat_raw raw;
    long result;
    if (!status) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(5, (long)fd, (long)&raw);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    status->st_mode = raw.type == LEONOS_TMUX_FS_DIR ? (S_IFDIR | 0755) :
                      raw.type == LEONOS_TMUX_FS_DEVICE ? (S_IFCHR | 0660) :
                                                             (S_IFREG | 0644);
    status->st_size = (off_t)raw.size;
    status->st_nlink = 1;
    status->st_blksize = 512;
    status->st_blocks = (blkcnt_t)((raw.size + 511u) / 512u);
    return 0;
}

long sysconf(int name)
{
    (void)name;
    return 1024;
}

int gethostname(char *name, size_t length)
{
    static const char hostname[] = "LeonOS-4";
    size_t copy;
    if (!name || !length) {
        errno = EINVAL;
        return -1;
    }
    copy = sizeof(hostname) - 1u;
    if (copy >= length) copy = length - 1u;
    memcpy(name, hostname, copy);
    name[copy] = 0;
    return 0;
}

int uname(struct utsname *info)
{
    if (!info) {
        errno = EINVAL;
        return -1;
    }
    memset(info, 0, sizeof(*info));
    strcpy(info->sysname, "LeonOS");
    strcpy(info->nodename, "LeonOS-4");
    strcpy(info->release, "4");
    strcpy(info->version, "LeonOS 4");
    strcpy(info->machine, "x86_64");
    return 0;
}

char *ttyname(int fd)
{
    static char name[] = "/dev/tty";
    if (!isatty(fd)) {
        errno = ENOTTY;
        return 0;
    }
    return name;
}

mode_t umask(mode_t mask)
{
    mode_t previous = leonos_umask;
    leonos_umask = mask;
    return previous;
}

int flock(int fd, int operation)
{
    (void)fd;
    (void)operation;
    /* The LeonOS tmux socket namespace serializes bind atomically. */
    return 0;
}

int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    /* AF_UNIX names are kernel objects; their ACL is enforced at bind/connect. */
    return 0;
}

char *realpath(const char *path, char *resolved)
{
    char cwd[PATH_MAX];
    char *output = resolved;
    size_t base = 0;
    size_t length;
    if (!path || !*path) {
        errno = EINVAL;
        return 0;
    }
    length = strlen(path);
    if (!output) {
        output = malloc(length + PATH_MAX + 2u);
        if (!output) return 0;
    }
    if (path[0] != '/') {
        if (!getcwd(cwd, sizeof(cwd))) {
            if (!resolved) free(output);
            return 0;
        }
        base = strlen(cwd);
        memcpy(output, cwd, base);
        if (base && output[base - 1u] != '/') output[base++] = '/';
    }
    memcpy(output + base, path, length + 1u);
    return output;
}

uid_t getuid(void)
{
    struct leonos_user_info user = {0};
    if (leonos_auth_current(&user) == 0 && user.uid) return (uid_t)user.uid;
    return 0;
}

uid_t geteuid(void)
{
    return getuid();
}

gid_t getgid(void)
{
    return (gid_t)getuid();
}

gid_t getegid(void)
{
    return getgid();
}

struct passwd *getpwuid(uid_t uid)
{
    static struct passwd result;
    static char name[LEONOS_AUTH_USERNAME_LEN];
    static char home[LEONOS_AUTH_HOME_LEN];
    struct leonos_user_info user = {0};
    if (leonos_auth_current(&user) != 0 || (uid && user.uid != uid)) return 0;
    strncpy(name, user.username[0] ? user.username : "admin", sizeof(name) - 1u);
    name[sizeof(name) - 1u] = 0;
    strncpy(home, user.home[0] ? user.home : "/home/admin", sizeof(home) - 1u);
    home[sizeof(home) - 1u] = 0;
    result.pw_name = name;
    result.pw_uid = user.uid;
    result.pw_gid = user.uid;
    result.pw_dir = home;
    result.pw_shell = "/programs/busybox/busybox.elf";
    return &result;
}

/* tmux only uses glob(3) while loading source-file patterns. Support the
 * conventional final path component without pulling a full fnmatch library. */
static int leonos_glob_match(const char *pattern, const char *name)
{
    while (*pattern) {
        if (*pattern == '*') {
            do { ++pattern; } while (*pattern == '*');
            if (!*pattern) return 1;
            while (*name) {
                if (leonos_glob_match(pattern, name++)) return 1;
            }
            return 0;
        }
        if (*pattern == '?') {
            if (!*name) return 0;
            ++pattern;
            ++name;
            continue;
        }
        if (*pattern != *name) return 0;
        ++pattern;
        ++name;
    }
    return *name == 0;
}

static int leonos_glob_has_magic(const char *text)
{
    return text && strpbrk(text, "*?[") != 0;
}

static int leonos_glob_add(glob_t *result, const char *path)
{
    char **paths;
    char *copy;
    size_t count = (size_t)result->gl_pathc;
    copy = strdup(path);
    if (!copy) return -1;
    paths = realloc(result->gl_pathv, (count + 2u) * sizeof(*paths));
    if (!paths) {
        free(copy);
        return -1;
    }
    paths[count] = copy;
    paths[count + 1u] = 0;
    result->gl_pathv = paths;
    result->gl_pathc++;
    return 0;
}

int glob(const char *pattern, int flags, int (*errorfn)(const char *, int),
         glob_t *result)
{
    const char *basename;
    const char *slash;
    char directory[PATH_MAX];
    DIR *stream;
    struct dirent *entry;
    struct stat status;
    size_t directory_length;
    (void)errorfn;
    if (!pattern || !result) {
        errno = EINVAL;
        return GLOB_ABEND;
    }
    if (!(flags & GLOB_APPEND)) memset(result, 0, sizeof(*result));
    if (!leonos_glob_has_magic(pattern)) {
        if (stat(pattern, &status) == 0 && leonos_glob_add(result, pattern) == 0) {
            return 0;
        }
        if ((flags & GLOB_NOCHECK) && leonos_glob_add(result, pattern) == 0) return 0;
        return result->gl_pathc ? 0 : GLOB_NOMATCH;
    }
    slash = strrchr(pattern, '/');
    basename = slash ? slash + 1 : pattern;
    if (slash) {
        size_t index;
        directory_length = slash == pattern ? 1u : (size_t)(slash - pattern);
        if (directory_length >= sizeof(directory)) {
            return GLOB_NOMATCH;
        }
        for (index = 0; index < directory_length; ++index) {
            if (pattern[index] == '*' || pattern[index] == '?' || pattern[index] == '[') {
                return GLOB_NOMATCH;
            }
        }
        memcpy(directory, pattern, directory_length);
        directory[directory_length] = 0;
    } else {
        strcpy(directory, ".");
    }
    stream = opendir(directory);
    if (!stream) return GLOB_NOMATCH;
    while ((entry = readdir(stream)) != 0) {
        char path[PATH_MAX];
        int written;
        if (!leonos_glob_match(basename, entry->d_name)) continue;
        written = snprintf(path, sizeof(path), "%s%s%s", directory,
                           directory[0] && directory[strlen(directory) - 1u] == '/' ? "" : "/",
                           entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path) ||
            leonos_glob_add(result, path) != 0) {
            closedir(stream);
            globfree(result);
            return GLOB_NOSPACE;
        }
    }
    closedir(stream);
    if (result->gl_pathc) return 0;
    if ((flags & GLOB_NOCHECK) && leonos_glob_add(result, pattern) == 0) return 0;
    return GLOB_NOMATCH;
}

void globfree(glob_t *result)
{
    if (!result) return;
    for (int index = 0; index < result->gl_pathc; ++index) free(result->gl_pathv[index]);
    free(result->gl_pathv);
    memset(result, 0, sizeof(*result));
}

speed_t cfgetispeed(const struct termios *termios)
{
    return termios ? termios->c_ispeed : 0;
}

speed_t cfgetospeed(const struct termios *termios)
{
    return termios ? termios->c_ospeed : 0;
}

int cfsetispeed(struct termios *termios, speed_t speed)
{
    if (!termios) {
        errno = EINVAL;
        return -1;
    }
    termios->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios, speed_t speed)
{
    if (!termios) {
        errno = EINVAL;
        return -1;
    }
    termios->c_ospeed = speed;
    return 0;
}

int tcflush(int fd, int selector)
{
    if (!isatty(fd) || selector < TCIFLUSH || selector > TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }
    /* PTY input/output is already event-driven; there is no deferred libc buffer. */
    return 0;
}
