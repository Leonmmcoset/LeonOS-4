#ifndef LEONOS_FS_H
#define LEONOS_FS_H

#include <leonos/fs_abi.h>

int leonos_list_dir(const char *path, struct leonos_dir_entry *entries, uint32_t capacity, uint32_t *out_count);
int leonos_stat_legacy(const char *path, struct leonos_stat *st);
int leonos_fstat_legacy(int fd, struct leonos_stat *st);
long lseek(int fd, long offset, int whence);
int leonos_readdir(int fd, struct leonos_dir_entry *entry);
int leonos_fs_acl_get(const char *path, struct leonos_fs_acl *acl);
int leonos_fs_acl_set(const char *path, const struct leonos_fs_acl *acl);
int leonos_fs_acl_take_ownership(const char *path, struct leonos_fs_acl *acl);
int leonos_fs_acl_repair(const char *path, struct leonos_fs_acl *acl);

#endif
