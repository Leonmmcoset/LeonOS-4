#ifndef NTCLKS_EFI_FS_H
#define NTCLKS_EFI_FS_H

#include <leonos/fs.h>
#include <ntclks/types.h>

void efi_fs_init(uint64_t system_table_addr);
bool efi_fs_ready(void);
int efi_fs_read_file(const char *path, const void **out_data, size_t *out_len);
int efi_fs_list_dir(const char *path, struct leonos_dir_entry *entries,
                    uint32_t capacity, uint32_t *out_count);

#endif
