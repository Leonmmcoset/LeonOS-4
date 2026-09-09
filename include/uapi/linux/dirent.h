#ifndef LEONOS_UAPI_LINUX_DIRENT_H
#define LEONOS_UAPI_LINUX_DIRENT_H

#include <stdint.h>

#define LINUX_DT_UNKNOWN 0U
#define LINUX_DT_FIFO 1U
#define LINUX_DT_CHR 2U
#define LINUX_DT_DIR 4U
#define LINUX_DT_BLK 6U
#define LINUX_DT_REG 8U
#define LINUX_DT_LNK 10U
#define LINUX_DT_SOCK 12U

/* getdents64 records include d_name at offset 19, then align to 8 bytes.
 * sizeof(struct linux_dirent64) includes tail padding and is not the prefix. */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

#endif
