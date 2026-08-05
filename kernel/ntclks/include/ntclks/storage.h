#ifndef NTCLKS_STORAGE_H
#define NTCLKS_STORAGE_H

#include <leonos/boot_handoff.h>
#include <leonos/fs.h>
#include <leonos/system.h>
#include <ntclks/types.h>

struct storage_node {
    uint32_t type;
    uint32_t flags;
    uint32_t first_cluster;
    uint32_t drive;
    uint64_t size;
};

#define STORAGE_NODE_FLAG_ROOT    0x00000001u
#define STORAGE_NODE_FLAG_DEV_DIR 0x00000002u
#define STORAGE_NODE_FLAG_DEV_FB0 0x00000004u

struct boot_info;

void storage_init(void);
void storage_init_installer_root(const struct boot_info *boot);
void storage_apply_mount_policy(const struct leonos_mount_policy *policy);
bool storage_ready(void);
bool storage_installer_root_active(void);
int storage_mount_ramdisk_root(const void *image, uint64_t len);
int storage_resolve_path(const char *cwd, const char *input, char *out, uint32_t cap);
int storage_lookup_path(const char *path, struct storage_node *out);
int storage_read_node(const struct storage_node *node, uint64_t offset,
                      void *buf, uint32_t len, uint32_t *out_read);
int storage_readdir_node(const struct storage_node *node, uint64_t *cursor,
                         struct leonos_dir_entry *entry);
int storage_read_file(const char *path, const void **out_data, size_t *out_len);
int storage_write_file(const char *path, const void *buf, uint32_t len);
int storage_truncate_file(const char *path, uint64_t length);
int storage_write_node(const char *path, uint64_t offset,
                       const void *buf, uint32_t len, uint32_t *out_written);
int storage_list_dir(const char *path, struct leonos_dir_entry *entries,
                     uint32_t capacity, uint32_t *out_count);
int storage_stat_path(const char *path, struct leonos_stat *st);
int storage_mkdir(const char *path);
int storage_unlink(const char *path);
int storage_rmdir(const char *path);
int storage_rename(const char *old_path, const char *new_path);
int storage_install_list_disks(struct leonos_install_disk *disks,
                               uint32_t capacity, uint32_t *out_count);
int storage_install_format_esp(uint32_t disk_id);
int storage_install_mount_target(uint32_t disk_id);
void storage_boot_identity(struct leonos_machine_identity *identity);

#endif
