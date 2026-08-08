#ifndef LEONOS_FILE_SYS_STAT_H
#define LEONOS_FILE_SYS_STAT_H

#include <sys/types.h>

#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100
#define S_IRGRP  0000040
#define S_IWGRP  0000020
#define S_IXGRP  0000010
#define S_IROTH  0000004
#define S_IWOTH  0000002
#define S_IXOTH  0000001

#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    int64_t st_atime;
    int64_t st_mtime;
    int64_t st_ctime;
};

int lstat(const char *path, struct stat *st);

#define major(value) ((unsigned int)(value) >> 8)
#define minor(value) ((unsigned int)(value) & 0xffU)

/* Declarations intentionally live in leonos_shim.c.  The build uses
 * function-like stat/fstat macros so these names remain available to callers
 * without rewriting the `struct stat` tag. */

#endif
