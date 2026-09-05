#ifndef LEONOS_BLOCKDEV_H
#define LEONOS_BLOCKDEV_H

#include <stdint.h>

#define LEONOS_BLOCK_MAX_DISKS 8U
#define LEONOS_BLOCK_MAX_PARTITIONS 128U
#define LEONOS_BLOCK_PATH_LEN 64U
#define LEONOS_BLOCK_NAME_LEN 72U

enum leonos_block_filesystem {
    LEONOS_BLOCK_FILESYSTEM_UNKNOWN = 0,
    LEONOS_BLOCK_FILESYSTEM_FAT32 = 1,
    LEONOS_BLOCK_FILESYSTEM_EXT2 = 2,
    LEONOS_BLOCK_FILESYSTEM_ISO9660 = 3,
    LEONOS_BLOCK_FILESYSTEM_EXFAT = 4,
};

enum leonos_block_gpt_type {
    LEONOS_BLOCK_GPT_BASIC_DATA = 1,
    LEONOS_BLOCK_GPT_ESP = 2,
    LEONOS_BLOCK_GPT_LINUX = 3,
};

struct leonos_block_disk_info {
    uint32_t id;
    uint32_t sector_size;
    uint64_t sector_count;
    char path[LEONOS_BLOCK_PATH_LEN];
    char name[32];
};

struct leonos_block_partition {
    uint32_t index;
    uint32_t filesystem;
    uint32_t gpt_type;
    uint32_t flags;
    uint64_t first_lba;
    uint64_t sector_count;
    char path[LEONOS_BLOCK_PATH_LEN];
    char name[LEONOS_BLOCK_NAME_LEN];
};

int leonos_block_list_disks(struct leonos_block_disk_info *disks, uint32_t capacity,
                            uint32_t *out_count);
int leonos_block_get_info(const char *path, struct leonos_block_disk_info *out);
int leonos_block_list_partitions(const char *disk_path,
                                 struct leonos_block_partition *partitions,
                                 uint32_t capacity, uint32_t *out_count);
int leonos_block_gpt_initialize(const char *disk_path, int force);
int leonos_block_gpt_create(const char *disk_path, uint32_t filesystem,
                            uint32_t size_mib, const char *name,
                            uint32_t *out_index);
int leonos_block_gpt_delete(const char *disk_path, uint32_t index);
int leonos_block_gpt_set_type(const char *disk_path, uint32_t index, uint32_t type);
int leonos_block_gpt_set_name(const char *disk_path, uint32_t index, const char *name);
int leonos_block_format(const char *partition_path, uint32_t filesystem,
                        const char *label);
int leonos_block_probe_filesystem(const char *partition_path, uint32_t *out_filesystem);
int leonos_block_partition_path(const char *disk_path, uint32_t index,
                                char *out, uint32_t capacity);
const char *leonos_block_filesystem_name(uint32_t filesystem);
const char *leonos_block_gpt_type_name(uint32_t type);

#endif
