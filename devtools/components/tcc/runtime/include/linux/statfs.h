#ifndef LEONOS_UAPI_LINUX_STATFS_H
#define LEONOS_UAPI_LINUX_STATFS_H
#include <stdint.h>

/* Linux v6.12 asm-generic/statfs.h with native x86-64 word widths. */
struct linux_statfs_abi {
    int64_t f_type, f_bsize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    int64_t f_namelen, f_frsize, f_flags, f_spare[4];
};
#define LINUX_ST_RDONLY 1
#define LINUX_ST_VALID 32
#endif
