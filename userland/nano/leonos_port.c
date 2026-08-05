/* LeonOS file-status ABI adapter for GNU nano's POSIX-facing source. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <leonos/system.h>

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U
#define LEONOS_SYS_FCNTL 72

struct leonos_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

extern int leonos_stat_raw_call(const char *path, struct leonos_stat_raw *st)
    __asm__("stat");
extern int leonos_fstat_raw_call(int fd, struct leonos_stat_raw *st)
    __asm__("fstat");
extern long syscall3(long number, long first, long second, long third);

static void clear_stat(struct stat *st)
{
    uint8_t *bytes = (uint8_t *)st;
    uint32_t index;
    for (index = 0; index < sizeof(*st); ++index) {
        bytes[index] = 0;
    }
}

static int fill_stat(int result, const struct leonos_stat_raw *raw, struct stat *st)
{
    if (!raw || !st) {
        errno = EINVAL;
        return -1;
    }
    if (result < 0) {
        errno = -result;
        return -1;
    }
    clear_stat(st);
    st->st_mode = raw->type == LEONOS_FS_TYPE_DIR ? (S_IFDIR | 0755) :
                  raw->type == LEONOS_FS_TYPE_DEVICE ? (S_IFCHR | 0660) :
                  (S_IFREG | 0644);
    st->st_size = (off_t)raw->size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((raw->size + 511U) / 512U);
    return 0;
}

int leonos_posix_stat(const char *path, struct stat *st)
{
    struct leonos_stat_raw raw;
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    return fill_stat(leonos_stat_raw_call(path, &raw), &raw, st);
}

int leonos_posix_fstat(int fd, struct stat *st)
{
    struct leonos_stat_raw raw;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    return fill_stat(leonos_fstat_raw_call(fd, &raw), &raw, st);
}

int leonos_posix_lstat(const char *path, struct stat *st)
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
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}

char *realpath(const char *path, char *resolved)
{
    char cwd[4096];
    char *result = resolved;
    size_t path_len;
    size_t cwd_len;
    if (!path || !path[0] || access(path, 0) < 0) {
        return 0;
    }
    if (path[1] == ':') {
        path_len = strlen(path);
        if (!result) {
            result = (char *)malloc(path_len + 1U);
        }
        if (!result) {
            errno = ENOMEM;
            return 0;
        }
        memcpy(result, path, path_len + 1U);
        return result;
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    cwd_len = strlen(cwd);
    path_len = strlen(path);
    if (!result) {
        result = (char *)malloc(cwd_len + path_len + 2U);
    }
    if (!result) {
        errno = ENOMEM;
        return 0;
    }
    memcpy(result, cwd, cwd_len);
    if (cwd_len && result[cwd_len - 1U] != '/') {
        result[cwd_len++] = '/';
    }
    memcpy(result + cwd_len, path, path_len + 1U);
    return result;
}

int fcntl(int fd, int command, ...)
{
    va_list args;
    long argument = 0;
    long result;
    va_start(args, command);
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC || command == F_SETFD ||
        command == F_SETFL) {
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

int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *previous)
{
    (void)signal_number;
    (void)action;
    if (previous) {
        uint8_t *bytes = (uint8_t *)previous;
        uint32_t index;
        for (index = 0; index < sizeof(*previous); ++index) {
            bytes[index] = 0;
        }
        previous->sa_handler = SIG_DFL;
    }
    return 0;
}

int gettimeofday(struct timeval *time_value, void *timezone)
{
    struct leonos_time_info info;
    (void)timezone;
    if (!time_value) {
        errno = EINVAL;
        return -1;
    }
    if (leonos_time_info(&info) < 0) {
        return -1;
    }
    time_value->tv_sec = (time_t)info.unix_seconds;
    time_value->tv_usec = (suseconds_t)((info.uptime_ms % 1000U) * 1000U);
    return 0;
}
