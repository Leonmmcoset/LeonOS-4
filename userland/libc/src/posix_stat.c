/* POSIX file-status adapters shared by dynamic applications and port shims. */
#include <errno.h>
#include <leonos/auth.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#define LEONOS_SYS_STAT 4
#define LEONOS_SYS_FSTAT 5

struct leonos_posix_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

extern long syscall2(long number, long first, long second);

#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U

static int fill_posix_stat(const struct leonos_posix_stat_raw *raw,
                           struct stat *status)
{
    if (!raw || !status) {
        errno = EINVAL;
        return -1;
    }
    memset(status, 0, sizeof(*status));
    status->st_mode = raw->type == LEONOS_FS_TYPE_DIR ? (S_IFDIR | 0755) :
                      raw->type == LEONOS_FS_TYPE_DEVICE ? (S_IFCHR | 0660) :
                                                          (S_IFREG | 0644);
    status->st_size = (off_t)raw->size;
    status->st_nlink = 1;
    status->st_blksize = 512;
    status->st_blocks = (blkcnt_t)((raw->size + 511U) / 512U);
    return 0;
}

static ino_t posix_path_inode(const char *path)
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

static int posix_tmux_socket_directory(const char *path)
{
    static const char prefix[] = "/tmp/tmux-";
    size_t index;
    if (!path || strncmp(path, prefix, sizeof(prefix) - 1u) != 0) return 0;
    for (index = sizeof(prefix) - 1u; path[index]; ++index) {
        if (path[index] == '/') return 0;
    }
    return index > sizeof(prefix) - 1u;
}

int leonos_posix_stat(const char *path, struct stat *status)
{
    struct leonos_posix_stat_raw raw;
    long result;
    if (!path || !status) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(LEONOS_SYS_STAT, (long)path, (long)&raw);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (fill_posix_stat(&raw, status) < 0) {
        return -1;
    }
    status->st_dev = 1;
    status->st_ino = posix_path_inode(path);
    if (raw.type == LEONOS_FS_TYPE_DIR && posix_tmux_socket_directory(path)) {
        struct leonos_user_info user = {0};
        status->st_mode = S_IFDIR | S_IRWXU;
        if (leonos_auth_current(&user) == 0 && user.uid) {
            status->st_uid = user.uid;
            status->st_gid = user.uid;
        }
    }
    return 0;
}

int leonos_posix_fstat(int fd, struct stat *status)
{
    struct leonos_posix_stat_raw raw;
    long result;
    if (!status) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(LEONOS_SYS_FSTAT, fd, (long)&raw);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return fill_posix_stat(&raw, status);
}

int leonos_posix_lstat(const char *path, struct stat *status)
{
    return leonos_posix_stat(path, status);
}

int lstat(const char *path, struct stat *status)
{
    return leonos_posix_lstat(path, status);
}

int access(const char *path, int mode)
{
    struct leonos_posix_stat_raw raw;
    long result;
    (void)mode;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    result = syscall2(LEONOS_SYS_STAT, (long)path, (long)&raw);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}
