#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <leonos/auth.h>
#include <leonos/auth_db.h>
#include <leonos/boot_handoff.h>
#include <leonos/fs.h>
#include <leonos/permissions.h>

extern void osmlayer_c_bind_services(const struct leonos_kernel_services *services);
extern int osmlayer_c_auth_op(uint32_t op, void *arg);

static uintptr_t stack_top;
static uintptr_t stack_used;
static unsigned char acl_data[8192];
static uint32_t acl_length;
static struct leonos_auth_database users_db = {
    .magic = LEONOS_AUTH_DB_MAGIC, .count = 1,
    .users = {{{.uid = 1234, .username = "alice", .home = "/home/alice"}, {0}}},
};

static void check_stack(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    assert(stack_top > sp);
    uintptr_t used = stack_top - sp;
    if (used > stack_used) stack_used = used;
    /* Leave half of the 64 KiB ring-0 stack for syscall/storage/IRQ frames. */
    if (used > 32768) {
        fprintf(stderr, "ACL stack usage %lu exceeds 32768 bytes\n", (unsigned long)used);
    }
    assert(used <= 32768);
}

static int32_t read_file(const char *path, void *buffer, uint32_t capacity,
                          uint32_t *out_length)
{
    check_stack();
    *out_length = 0;
    if (!strcmp(path, "/system/state/accounts.db")) return -2;
    if (!strcmp(path, LEONOS_AUTH_DB_PATH)) {
        *out_length = 8 + sizeof(users_db.users[0]);
        assert(capacity >= *out_length);
        memcpy(buffer, &users_db, *out_length);
        return 0;
    }
    if (strcmp(path, "/run/leonos/LEONACL.SYS")) {
        assert(strstr(path, "LEONACL.SYS"));
        return -2;
    }
    assert(!strcmp(path, "/run/leonos/LEONACL.SYS"));
    if (!acl_length) return -2;
    assert(capacity >= acl_length);
    memcpy(buffer, acl_data, acl_length);
    *out_length = acl_length;
    return 0;
}

static int32_t write_file(const char *path, const void *buffer, uint32_t length)
{
    check_stack();
    assert(!strcmp(path, "/run/leonos/LEONACL.SYS"));
    assert(length <= sizeof(acl_data));
    memcpy(acl_data, buffer, length);
    acl_length = length;
    return 0;
}

static void perform(struct leonos_fs_acl_request *req, uint32_t action)
{
    req->action = action;
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_FSPERM, req) == 0);
}

int main(void)
{
    const struct leonos_kernel_services services = {
        .read_file = read_file,
        .write_file = write_file,
    };
    struct leonos_fs_acl_request req = {
        .actor_uid = 7,
        .actor_role = LEONOS_AUTH_ROLE_ADMIN,
        .path = "/run/leonos/session-user",
    };
    osmlayer_c_bind_services(&services);
    __asm__ volatile("mov %%rsp, %0" : "=r"(stack_top));
    perform(&req, LEONOS_FS_ACL_ACTION_NOTE_CREATE);
    perform(&req, LEONOS_FS_ACL_ACTION_GET);
    assert(req.acl.owner_uid == 7 && !(req.acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC));

    req.acl.owner_uid = 9;
    perform(&req, LEONOS_FS_ACL_ACTION_SET);
    perform(&req, LEONOS_FS_ACL_ACTION_GET);
    assert(req.acl.owner_uid == 9);

    strcpy(req.path2, "/run/leonos/renamed");
    perform(&req, LEONOS_FS_ACL_ACTION_NOTE_RENAME);
    perform(&req, LEONOS_FS_ACL_ACTION_GET);
    assert(req.acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC);
    strcpy(req.path, req.path2);
    perform(&req, LEONOS_FS_ACL_ACTION_GET);
    assert(req.acl.owner_uid == 9 && !(req.acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC));

    perform(&req, LEONOS_FS_ACL_ACTION_NOTE_DELETE);
    perform(&req, LEONOS_FS_ACL_ACTION_GET);
    assert(req.acl.flags & LEONOS_FS_ACL_FLAG_SYNTHETIC);
    struct leonos_permissions_request permissions = {
        .action = LEONOS_PERMISSIONS_SET,
        .path = "/run/leonos/posix",
        .value = {0640, 70001, 90002},
    };
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    permissions.action = LEONOS_PERMISSIONS_GET;
    permissions.value = (struct leonos_permissions){0};
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    assert(permissions.value.mode == 0640 && permissions.value.uid == 70001 && permissions.value.gid == 90002);
    permissions.action = LEONOS_PERMISSIONS_SET;
    strcpy(permissions.path, "/run/leonos/posix-renamed");
    permissions.value = (struct leonos_permissions){0600, 10, 20};
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    permissions.action = LEONOS_PERMISSIONS_GET;
    strcpy(req.path, "/run/leonos/posix");
    strcpy(req.path2, "/run/leonos/posix-renamed");
    perform(&req, LEONOS_FS_ACL_ACTION_NOTE_RENAME);
    strcpy(permissions.path, req.path2);
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    assert(permissions.value.mode == 0640 && permissions.value.uid == 70001 && permissions.value.gid == 90002);
    acl_data[20] ^= 1;
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == -5);
    acl_data[20] ^= 1;
    strcpy(permissions.path, "/home/alice/document.txt");
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    assert(permissions.value.mode == 0700 && permissions.value.uid == 1234 && permissions.value.gid == 1234);
    users_db.magic = 0;
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == -5);
    users_db.magic = LEONOS_AUTH_DB_MAGIC;
    strcpy(permissions.path, LEONOS_AUTH_DB_PATH);
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    assert(permissions.value.mode == 0600 && permissions.value.uid == 0);
    strcpy(permissions.path, "/home");
    assert(osmlayer_c_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &permissions) == 0);
    assert(permissions.value.mode == 0755);
    printf("ACL create/get/set/rename/delete passed; peak stack usage %lu bytes\n",
           (unsigned long)stack_used);
    return 0;
}
