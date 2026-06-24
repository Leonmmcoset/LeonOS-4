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

#define LEONOS_SEEK_SET 0
#define LEONOS_SEEK_CUR 1
#define LEONOS_SEEK_END 2

struct leonos_stat {
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
};

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
int stat(const char *path, struct leonos_stat *st);
int fstat(int fd, struct leonos_stat *st);
long lseek(int fd, long offset, int whence);
int leonos_readdir(int fd, struct leonos_dir_entry *entry);

#endif
