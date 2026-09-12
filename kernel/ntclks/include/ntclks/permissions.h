#ifndef NTCLKS_PERMISSIONS_H
#define NTCLKS_PERMISSIONS_H

#include <ntclks/sched.h>
#include <leonos/permissions.h>

#define FS_ACCESS_EXEC 1U
#define FS_ACCESS_WRITE 2U
#define FS_ACCESS_READ 4U
#define FS_LOOKUP_FOLLOW 1U
#define FS_LOOKUP_PARENT 2U

bool task_in_group(const struct task *task, uint32_t gid, bool real_ids);
void task_groups_retain(struct task_groups *groups);
void task_groups_release(struct task *task);
int task_groups_set(struct task *task, const uint32_t *ids, uint32_t count);

int fs_permissions_get(const char *path, const struct storage_node *node,
                       struct leonos_permissions *value);
int fs_permissions_check(const struct task *task, const char *path, uint32_t access,
                         bool real_ids);
int fs_permissions_search(const struct task *task, const char *path, bool real_ids);
int fs_permissions_check_node(const struct task *task, const char *path,
                              const struct storage_node *node, uint32_t access, bool real_ids);
int fs_permissions_resolve(const struct task *task, const char *base, const char *input,
                           char *out, uint32_t cap, bool real_ids);
/**
 * @brief Walks components and symlinks while checking directory search access.
 * @param task Credentials, or NULL for kernel root access.
 * @param base Absolute starting directory for a relative input.
 * @param input Kernel pathname; final missing components are left for creation.
 * @param out Receives the resolved absolute pathname, without aliasing input.
 * @param cap Output capacity in bytes including NUL.
 * @param real_ids Select real instead of filesystem credentials.
 * @param flags FOLLOW follows the final link; PARENT leaves the last component intact.
 * @return Zero or a negative errno; more than 40 link traversals returns ELOOP.
 */
int fs_permissions_resolve_flags(const struct task *task, const char *base, const char *input,
                                 char *out, uint32_t cap, bool real_ids, uint32_t flags);
int fs_permissions_parent(const struct task *task, const char *path, bool deleting);
int fs_permissions_create(const struct task *task, const char *path,
                          const struct storage_node *node, uint32_t mode);
int fs_permissions_chmod(const struct task *task, const char *path,
                         const struct storage_node *node, uint32_t mode);
int fs_permissions_chown(const struct task *task, const char *path,
                         const struct storage_node *node, uint32_t uid, uint32_t gid);

#endif
