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
#define LINUX_STATX_TYPE 0x00000001U
#define LINUX_STATX_MODE 0x00000002U
#define LINUX_STATX_NLINK 0x00000004U
#define LINUX_STATX_UID 0x00000008U
#define LINUX_STATX_GID 0x00000010U
#define LINUX_STATX_INO 0x00000100U
#define LINUX_STATX_SIZE 0x00000200U
#define LINUX_STATX_BLOCKS 0x00000400U
#define LINUX_STATX_BASIC_STATS 0x000007ffU
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

/* Linux v6.12 struct statx, native x86-64 layout. */
struct linux_statx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct linux_statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __pad0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct linux_statx_timestamp stx_atime;
    struct linux_statx_timestamp stx_btime;
    struct linux_statx_timestamp stx_ctime;
    struct linux_statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t stx_subvol;
    uint32_t stx_atomic_write_unit_min;
    uint32_t stx_atomic_write_unit_max;
    uint32_t stx_atomic_write_segments_max;
    uint32_t __pad1;
    uint64_t __pad2[9];
};

_Static_assert(sizeof(struct linux_statx) == 256, "Linux x86-64 statx layout");

#endif
