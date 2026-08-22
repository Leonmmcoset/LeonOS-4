/*
 * LeonOS EFI filesystem interface: declares firmware filesystem helpers.
 * Supports reading boot files and metadata from EFI-backed storage.
 */
#ifndef NTCLKS_EFI_FS_H
#define NTCLKS_EFI_FS_H

#include <leonos/fs.h>
#include <ntclks/types.h>

/**
 * @brief Bring up the EFI firmware filesystem using the system table at system_table_addr.
 */
void efi_fs_init(uint64_t system_table_addr);
/**
 * @brief Return true once the EFI filesystem is usable.
 */
bool efi_fs_ready(void);
/**
 * @brief Load the file at path into memory, returning a pointer in out_data and its size in out_len.
 */
int efi_fs_read_file(const char *path, const void **out_data, size_t *out_len);
/**
 * @brief List up to capacity directory entries from path into entries, setting out_count.
 */
int efi_fs_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count);

#endif
