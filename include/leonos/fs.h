#ifndef LEONOS_FS_H
#define LEONOS_FS_H

#include <stdint.h>

#define LEONOS_IOCTL_LIST_DIR 0x4c444952UL
#define LEONOS_INSTALL_IOCTL_LIST_DISKS 0x4c49444bUL
#define LEONOS_INSTALL_IOCTL_FORMAT_ESP 0x4c49464dUL
#define LEONOS_INSTALL_IOCTL_MOUNT_TARGET 0x4c494d54UL

#define LEONOS_FS_NAME_LEN 48U
#define LEONOS_FS_PATH_LEN 96U
#define LEONOS_FS_MAX_ENTRIES 64U

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U

#define LEONOS_O_RDONLY 0x0000
#define LEONOS_O_WRONLY 0x0001
#define LEONOS_O_RDWR 0x0002
#define LEONOS_O_ACCMODE 0x0003
#define LEONOS_O_CREAT 0x0040
#define LEONOS_O_TRUNC 0x0200
#define LEONOS_O_APPEND 0x0400

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

#define LEONOS_INSTALL_MAX_DISKS 8U
#define LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT 0x00000001U
#define LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED 0x00000002U

struct leonos_install_disk {
    uint32_t id;
    uint32_t port;
    uint32_t sector_size;
    uint32_t flags;
    uint64_t sector_count;
    char name[32];
};

struct leonos_install_disk_list {
    uint32_t capacity;
    uint32_t count;
    struct leonos_install_disk *disks;
};

int leonos_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count);
int leonos_install_list_disks(struct leonos_install_disk *disks,
                              uint32_t capacity, uint32_t *out_count);
int leonos_install_format_esp(uint32_t disk_id);
int leonos_install_mount_target(uint32_t disk_id);
int stat(const char *path, struct leonos_stat *st);
int fstat(int fd, struct leonos_stat *st);
long lseek(int fd, long offset, int whence);
int leonos_readdir(int fd, struct leonos_dir_entry *entry);

#endif
