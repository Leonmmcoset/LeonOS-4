/* POSIX ABI adapters required by upstream libmagic. */
#include <errno.h>
#include <stddef.h>
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct leonos_stat_raw {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U

extern int leonos_stat_raw_call(const char *path, struct leonos_stat_raw *st)
    __asm__("stat");
extern int leonos_fstat_raw_call(int fd, struct leonos_stat_raw *st)
    __asm__("fstat");

static int leonos_posix_stat_fill(const struct leonos_stat_raw *raw,
                                  struct stat *st)
{
    if (!raw || !st) {
        errno = EINVAL;
        return -1;
    }
    *st = (struct stat){0};
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
    int result;

    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    result = leonos_stat_raw_call(path, &raw);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return leonos_posix_stat_fill(&raw, st);
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
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return leonos_posix_stat_fill(&raw, st);
}

int lstat(const char *path, struct stat *st)
{
    return leonos_posix_stat(path, st);
}

ssize_t pread(int fd, void *buffer, size_t length, off_t offset)
{
    long previous;
    long result;
    int saved_errno;

    previous = lseek(fd, 0, SEEK_CUR);
    if (previous < 0 || lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    result = read(fd, buffer, length);
    saved_errno = errno;
    (void)lseek(fd, previous, SEEK_SET);
    errno = saved_errno;
    return (ssize_t)result;
}

ssize_t readlink(const char *path, char *buffer, size_t capacity)
{
    (void)path;
    (void)buffer;
    (void)capacity;
    errno = EINVAL;
    return -1;
}

int dup2(int old_fd, int new_fd)
{
    (void)old_fd;
    (void)new_fd;
    errno = ENOSYS;
    return -1;
}

int pipe(int fds[2])
{
    (void)fds;
    errno = ENOSYS;
    return -1;
}

int access(const char *path, int mode)
{
    struct stat st;

    if (leonos_posix_stat(path, &st) < 0) {
        return -1;
    }
    if (mode & (W_OK | X_OK)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

DIR *opendir(const char *path)
{
    (void)path;
    errno = ENOTSUP;
    return NULL;
}

struct dirent *readdir(DIR *directory)
{
    (void)directory;
    return NULL;
}

int closedir(DIR *directory)
{
    (void)directory;
    return 0;
}
