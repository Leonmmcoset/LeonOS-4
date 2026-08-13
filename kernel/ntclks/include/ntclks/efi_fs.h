/*
 * LeonOS EFI filesystem interface: declares firmware filesystem helpers.
 * Supports reading boot files and metadata from EFI-backed storage.
 */
#ifndef NTCLKS_EFI_FS_H
#define NTCLKS_EFI_FS_H

#include <leonos/fs.h>
#include <ntclks/types.h>

/**
 * @brief Coordinates the efi fs init operation.
 * @param system_table_addr Address used by this operation; its address-space interpretation follows the API.
 */
void efi_fs_init(uint64_t system_table_addr);
/**
 * @brief Coordinates the efi fs ready operation.
 * @return Result, status, or value defined by this API.
 */
bool efi_fs_ready(void);
/**
 * @brief Coordinates the efi fs read file operation.
 * @param path LeonOS path consumed by this operation.
 * @param out_data Caller-provided storage that receives output from this operation.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int efi_fs_read_file(const char *path, const void **out_data, size_t *out_len);
/**
 * @brief Coordinates the efi fs list dir operation.
 * @param path LeonOS path consumed by this operation.
 * @param entries Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
int efi_fs_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count);

#endif
