#ifndef LEONOS_UAPI_FS_ABI_H
#define LEONOS_UAPI_FS_ABI_H

#include <stdint.h>
#include <linux/fcntl.h>

#define LEONOS_FS_NAME_LEN 128U
#define LEONOS_FS_PATH_LEN 256U
#define LEONOS_FS_MAX_ENTRIES 64U
#define LEONOS_FS_ACL_MAX_ACE 16U
#define LEONOS_FS_ACL_VERSION 1U
#define LEONOS_FS_TYPE_FILE 1U
#define LEONOS_FS_TYPE_DIR 2U
#define LEONOS_FS_TYPE_DEVICE 3U
#define LEONOS_FS_TYPE_SOCKET 4U
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
#define LEONOS_FS_ACL_PRINCIPAL_GROUP 6U
#define LEONOS_FS_ACL_ACE_INHERITED 0x00000002U
#define LEONOS_FS_PERM_READ 0x00000001U
#define LEONOS_FS_PERM_WRITE 0x00000002U
#define LEONOS_FS_PERM_EXEC 0x00000004U
#define LEONOS_FS_PERM_DELETE 0x00000008U
#define LEONOS_FS_PERM_MANAGE 0x00000010U
#define LEONOS_FS_PERM_FULL (LEONOS_FS_PERM_READ | LEONOS_FS_PERM_WRITE | LEONOS_FS_PERM_EXEC | LEONOS_FS_PERM_DELETE | LEONOS_FS_PERM_MANAGE)
#define LEONOS_FS_ACL_FLAG_CORRUPT 0x00000001U
#define LEONOS_FS_ACL_FLAG_SYNTHETIC 0x00000002U
#define LEONOS_O_RDONLY LINUX_O_RDONLY
#define LEONOS_O_WRONLY LINUX_O_WRONLY
#define LEONOS_O_RDWR LINUX_O_RDWR
#define LEONOS_O_ACCMODE LINUX_O_ACCMODE
#define LEONOS_O_CREAT LINUX_O_CREAT
#define LEONOS_O_TRUNC LINUX_O_TRUNC
#define LEONOS_O_APPEND LINUX_O_APPEND
#define LEONOS_O_EXCL LINUX_O_EXCL
#define LEONOS_O_NONBLOCK LINUX_O_NONBLOCK
#define LEONOS_O_DIRECTORY LINUX_O_DIRECTORY
#define LEONOS_O_NOFOLLOW LINUX_O_NOFOLLOW
#define LEONOS_O_CLOEXEC LINUX_O_CLOEXEC
#define LEONOS_FS_IO_SLICE_BYTES 4096U
#define LEONOS_FS_FILE_WRITE_SLICE_BYTES (64U * 512U)
#define LEONOS_FS_READ_SLICE_BYTES (64U * 512U)
#define LEONOS_SEEK_SET 0
#define LEONOS_SEEK_CUR 1
#define LEONOS_SEEK_END 2

struct leonos_stat { uint32_t type; uint32_t reserved; uint64_t size; };
struct leonos_dir_entry { uint32_t type; char name[LEONOS_FS_NAME_LEN]; };
struct leonos_dir_list { const char *path; uint32_t capacity; uint32_t count; struct leonos_dir_entry *entries; };
struct leonos_fs_acl_ace { uint32_t principal; uint32_t flags; uint32_t permissions; uint32_t reserved; };
struct leonos_fs_acl { uint32_t version; uint32_t owner_uid; uint32_t flags; uint32_t ace_count; struct leonos_fs_acl_ace aces[LEONOS_FS_ACL_MAX_ACE]; };
struct leonos_fs_acl_request { uint32_t action; uint32_t actor_uid; uint32_t actor_role; uint32_t actor_flags; uint32_t status; uint32_t reserved; char username[32]; char home[96]; char path[LEONOS_FS_PATH_LEN]; char path2[LEONOS_FS_PATH_LEN]; struct leonos_fs_acl acl; };

#endif
