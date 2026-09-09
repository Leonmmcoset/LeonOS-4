#ifndef LEONOS_UAPI_LINUX_STAT_H
#define LEONOS_UAPI_LINUX_STAT_H

#include <stdint.h>

#define LINUX_S_IFMT   0170000U
#define LINUX_S_IFSOCK 0140000U
#define LINUX_S_IFLNK  0120000U
#define LINUX_S_IFREG  0100000U
#define LINUX_S_IFBLK  0060000U
#define LINUX_S_IFDIR  0040000U
#define LINUX_S_IFCHR  0020000U
#define LINUX_S_IFIFO  0010000U
#define LINUX_S_ISUID  0004000U
#define LINUX_S_ISGID  0002000U
#define LINUX_S_ISVTX  0001000U
#define LINUX_S_IRWXU  0000700U
#define LINUX_S_IRWXG  0000070U
#define LINUX_S_IRWXO  0000007U

/* Linux v6.12 arch/x86/include/uapi/asm/stat.h, native x86-64 only.
 * Timestamp names avoid libc's st_atime/st_mtime/st_ctime macros. */
struct linux_stat_abi {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t atime_sec;
    int64_t atime_nsec;
    int64_t mtime_sec;
    int64_t mtime_nsec;
    int64_t ctime_sec;
    int64_t ctime_nsec;
    int64_t reserved_words[3];
};

#endif
