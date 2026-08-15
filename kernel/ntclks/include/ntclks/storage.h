/*
 * LeonOS storage interface: declares filesystem and storage-node operations.
 * Provides path lookup, directory access, file reads, and mount abstractions.
 */
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
#define STORAGE_NODE_FLAG_EXT2    0x00000008u

struct boot_info;

/**
 * @brief Coordinates the storage init operation.
 */
void storage_init(void);
/**
 * @brief Coordinates the storage set io async context operation.
 * @param enabled Input or output value used by this operation.
 */
void storage_set_io_async_context(bool enabled);
/**
 * @brief Coordinates the storage release task io operation.
 * @param pid Input or output value used by this operation.
 */
void storage_release_task_io(uint32_t pid);
/**
 * @brief Coordinates the storage drain task io operation.
 * @param pid Input or output value used by this operation.
 */
void storage_drain_task_io(uint32_t pid);
/**
 * @brief Coordinates the storage init installer root operation.
 * @param boot Boot information supplied by the loader.
 */
void storage_init_installer_root(const struct boot_info *boot);
/**
 * @brief Coordinates the storage apply mount policy operation.
 * @param policy Input or output value used by this operation.
 */
void storage_apply_mount_policy(const struct leonos_mount_policy *policy);
/**
 * @brief Coordinates the storage ready operation.
 * @return Result, status, or value defined by this API.
 */
bool storage_ready(void);
/**
 * @brief Returns the mounted filesystem type for the runtime root volume.
 * @return Stable lowercase filesystem name, or "none" before a root mounts.
 */
const char *storage_root_filesystem_name(void);
/**
 * @brief Coordinates the storage installer root active operation.
 * @return Result, status, or value defined by this API.
 */
bool storage_installer_root_active(void);
/**
 * @brief Coordinates the storage mount ramdisk root operation.
 * @param image Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int storage_mount_ramdisk_root(const void *image, uint64_t len);
/**
 * @brief Coordinates the storage resolve path operation.
 * @param cwd Input or output value used by this operation.
 * @param input Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
int storage_resolve_path(const char *cwd, const char *input, char *out, uint32_t cap);
/**
 * @brief Coordinates the storage lookup path operation.
 * @param path LeonOS path consumed by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_lookup_path(const char *path, struct storage_node *out);
/**
 * @brief Coordinates the storage read node operation.
 * @param node Input or output value used by this operation.
 * @param offset Input or output value used by this operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out_read Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_read_node(const struct storage_node *node, uint64_t offset,
                      void *buf, uint32_t len, uint32_t *out_read);
/**
 * @brief Coordinates the storage readdir node operation.
 * @param node Input or output value used by this operation.
 * @param cursor Input or output value used by this operation.
 * @param entry Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_readdir_node(const struct storage_node *node, uint64_t *cursor,
                         struct leonos_dir_entry *entry);
/**
 * @brief Coordinates the storage read file operation.
 * @param path LeonOS path consumed by this operation.
 * @param out_data Caller-provided storage that receives output from this operation.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_read_file(const char *path, const void **out_data, size_t *out_len);
/**
 * @brief Coordinates the storage write file operation.
 * @param path LeonOS path consumed by this operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int storage_write_file(const char *path, const void *buf, uint32_t len);
/**
 * @brief Coordinates the storage truncate file operation.
 * @param path LeonOS path consumed by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int storage_truncate_file(const char *path, uint64_t length);
/**
 * @brief Coordinates the storage write node operation.
 * @param path LeonOS path consumed by this operation.
 * @param offset Input or output value used by this operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @param out_written Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_write_node(const char *path, uint64_t offset,
                       const void *buf, uint32_t len, uint32_t *out_written);
/**
 * @brief Coordinates the storage list dir operation.
 * @param path LeonOS path consumed by this operation.
 * @param entries Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_list_dir(const char *path, struct leonos_dir_entry *entries,
                     uint32_t capacity, uint32_t *out_count);
/**
 * @brief Coordinates the storage stat path operation.
 * @param path LeonOS path consumed by this operation.
 * @param st Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_stat_path(const char *path, struct leonos_stat *st);
/**
 * @brief Coordinates the storage mkdir operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_mkdir(const char *path);
/**
 * @brief Coordinates the storage unlink operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_unlink(const char *path);
/**
 * @brief Coordinates the storage rmdir operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_rmdir(const char *path);
/**
 * @brief Coordinates the storage rename operation.
 * @param old_path LeonOS path consumed by this operation.
 * @param new_path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_rename(const char *old_path, const char *new_path);
/**
 * @brief Coordinates the storage install list disks operation.
 * @param disks Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_install_list_disks(struct leonos_install_disk *disks,
                               uint32_t capacity, uint32_t *out_count);
/**
 * @brief Coordinates the storage install format esp operation.
 * @param disk_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_install_format_esp(uint32_t disk_id);
/**
 * @brief Formats an installer target as a GPT disk with FAT32 ESP and ext2 root.
 * @param disk_id Installer-selected AHCI disk identifier.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_install_format_target(uint32_t disk_id);
/**
 * @brief Coordinates the storage install mount target operation.
 * @param disk_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int storage_install_mount_target(uint32_t disk_id);
/**
 * @brief Lists GPT partitions on an AHCI disk exposed to disk management.
 * @param disk_id Detected AHCI disk identifier.
 * @param partitions Caller buffer receiving partition metadata.
 * @param capacity Number of partition records available in @p partitions.
 * @param out_count Receives the complete number of usable GPT entries.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_list_partitions(uint32_t disk_id,
                                 struct leonos_disk_partition *partitions,
                                 uint32_t capacity, uint32_t *out_count);
/**
 * @brief Formats an unprotected GPT partition as FAT32 or ext2.
 * @param request Partition selector and requested filesystem.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_format_partition(const struct leonos_disk_partition_format *request);
/**
 * @brief Removes one unprotected GPT partition entry without wiping its data area.
 * @param request Partition selector.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_delete_partition(const struct leonos_disk_partition_delete *request);
/**
 * @brief Creates and formats one GPT data partition in an unallocated disk range.
 * @param request Disk, filesystem, size, and display-name request.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_create_partition(const struct leonos_disk_partition_create *request);
/**
 * @brief Mounts one FAT32 or ext2 data partition on the next free numeric drive.
 * @param request Disk and GPT-entry selector; receives the selected drive.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_mount_partition(struct leonos_disk_partition_mount *request);
/**
 * @brief Returns the assigned drive for a runtime data-partition mount.
 * @param disk_id AHCI disk identifier.
 * @param partition_index Zero-based GPT entry index.
 * @param out_drive Receives the mounted numeric drive.
 * @return Zero when mounted, or a negative errno-style storage error.
 */
int storage_disk_partition_mounted_drive(uint32_t disk_id, uint32_t partition_index,
                                         uint32_t *out_drive);
/**
 * @brief Tears down one runtime data-partition mount after the kernel checks usage.
 * @param request Disk and GPT-entry selector.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_unmount_partition(const struct leonos_disk_partition_unmount *request);
/**
 * @brief Coordinates the storage boot identity operation.
 * @param identity Input or output value used by this operation.
 */
void storage_boot_identity(struct leonos_machine_identity *identity);

#endif
