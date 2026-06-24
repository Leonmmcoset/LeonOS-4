#ifndef LEONOS_FS_H
#define LEONOS_FS_H

#include <stdint.h>

#define LEONOS_IOCTL_LIST_DIR 0x4c444952UL

#define LEONOS_FS_NAME_LEN 48U
#define LEONOS_FS_PATH_LEN 96U
#define LEONOS_FS_MAX_ENTRIES 32U

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U

struct leonos_dir_entry {
    uint32_t type;
    char name[LEONOS_FS_NAME_LEN];
};

struct leonos_dir_list {
    const char *path;
    uint32_t capacity;
    uint32_t count;
    struct leonos_dir_entry *entries;
};

int leonos_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count);

#endif
