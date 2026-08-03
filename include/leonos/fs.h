#ifndef LEONOS_FS_H
#define LEONOS_FS_H

#include <stdint.h>

#define LEONOS_IOCTL_LIST_DIR 0x4c444952UL
#define LEONOS_INSTALL_IOCTL_LIST_DISKS 0x4c49444bUL
#define LEONOS_INSTALL_IOCTL_FORMAT_ESP 0x4c49464dUL
#define LEONOS_INSTALL_IOCTL_MOUNT_TARGET 0x4c494d54UL
#define LEONOS_FS_IOCTL_ACL_GET 0x4c465047UL
#define LEONOS_FS_IOCTL_ACL_SET 0x4c465053UL
#define LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP 0x4c465054UL
#define LEONOS_FS_IOCTL_ACL_REPAIR 0x4c465052UL

#define LEONOS_FS_NAME_LEN 128U
#define LEONOS_FS_PATH_LEN 256U
#define LEONOS_FS_MAX_ENTRIES 64U
#define LEONOS_FS_ACL_MAX_ACE 16U
#define LEONOS_FS_ACL_VERSION 1U

#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U

#define LEONOS_FS_ACL_ACTION_GET 1U
#define LEONOS_FS_ACL_ACTION_SET 2U
#define LEONOS_FS_ACL_ACTION_TAKE_OWNERSHIP 3U
#define LEONOS_FS_ACL_ACTION_REPAIR 4U
#define LEONOS_FS_ACL_ACTION_NOTE_CREATE 5U
#define LEONOS_FS_ACL_ACTION_NOTE_DELETE 6U
#define LEONOS_FS_ACL_ACTION_NOTE_RENAME 7U

#define LEONOS_FS_ACL_PRINCIPAL_OWNER 1U
#define LEONOS_FS_ACL_PRINCIPAL_SYSTEM 2U
#define LEONOS_FS_ACL_PRINCIPAL_ADMINISTRATORS 3U
#define LEONOS_FS_ACL_PRINCIPAL_USERS 4U
#define LEONOS_FS_ACL_PRINCIPAL_EVERYONE 5U

#define LEONOS_FS_ACL_ACE_INHERITED 0x00000002U

#define LEONOS_FS_PERM_READ 0x00000001U
#define LEONOS_FS_PERM_WRITE 0x00000002U
#define LEONOS_FS_PERM_EXEC 0x00000004U
#define LEONOS_FS_PERM_DELETE 0x00000008U
#define LEONOS_FS_PERM_MANAGE 0x00000010U
#define LEONOS_FS_PERM_FULL (LEONOS_FS_PERM_READ | LEONOS_FS_PERM_WRITE | \
                             LEONOS_FS_PERM_EXEC | LEONOS_FS_PERM_DELETE | \
                             LEONOS_FS_PERM_MANAGE)

#define LEONOS_FS_ACL_FLAG_CORRUPT 0x00000001U
#define LEONOS_FS_ACL_FLAG_SYNTHETIC 0x00000002U

#define LEONOS_O_RDONLY 0x0000
#define LEONOS_O_WRONLY 0x0001
#define LEONOS_O_RDWR 0x0002
#define LEONOS_O_ACCMODE 0x0003
#define LEONOS_O_CREAT 0x0040
#define LEONOS_O_TRUNC 0x0200
#define LEONOS_O_APPEND 0x0400

/* Keep writes short enough for prompt scheduling. Reads match the FAT
 * readahead window so large fonts and wallpapers do not devolve into 4 KiB
 * syscall/storage cycles. */
#define LEONOS_FS_IO_SLICE_BYTES 4096U
#define LEONOS_FS_READ_SLICE_BYTES (64U * 512U)

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

struct leonos_fs_acl_ace {
    uint32_t principal;
    uint32_t flags;
    uint32_t permissions;
    uint32_t reserved;
};

struct leonos_fs_acl {
    uint32_t version;
    uint32_t owner_uid;
    uint32_t flags;
    uint32_t ace_count;
    struct leonos_fs_acl_ace aces[LEONOS_FS_ACL_MAX_ACE];
};

struct leonos_fs_acl_request {
    uint32_t action;
    uint32_t actor_uid;
    uint32_t actor_role;
    uint32_t actor_flags;
    uint32_t status;
    uint32_t reserved;
    char username[32];
    char home[96];
    char path[LEONOS_FS_PATH_LEN];
    char path2[LEONOS_FS_PATH_LEN];
    struct leonos_fs_acl acl;
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
int leonos_fs_acl_get(const char *path, struct leonos_fs_acl *acl);
int leonos_fs_acl_set(const char *path, const struct leonos_fs_acl *acl);
int leonos_fs_acl_take_ownership(const char *path, struct leonos_fs_acl *acl);
int leonos_fs_acl_repair(const char *path, struct leonos_fs_acl *acl);

#endif
