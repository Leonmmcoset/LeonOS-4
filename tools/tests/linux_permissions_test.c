#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../kernel/ntclks/permissions.c"

static struct fixture {
    const char *path;
    uint32_t type;
    struct leonos_permissions value;
    const char *target;
} entries[] = {
    {"/", LEONOS_FS_TYPE_DIR, {0755, 0, 0}},
    {"/data", LEONOS_FS_TYPE_DIR, {0777, 100, 200}},
    {"/data/file", LEONOS_FS_TYPE_FILE, {0640, 100, 200}},
    {"/data/dir", LEONOS_FS_TYPE_DIR, {0755, 100, 200}},
    {"/jump", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "data/dir"},
    {"/data/link", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "file"},
    {"/absolute", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "/data/link"},
    {"/loop", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "loop"},
    {"/self", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "."},
    {"/dangling", LEONOS_FS_TYPE_SYMLINK, {0777, 100, 200}, "missing"},
};

static struct fixture *find(const char *path)
{
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (!strcmp(path, entries[i].path)) return &entries[i];
    return NULL;
}
void *kernel_malloc(size_t size) { return malloc(size); }
void kernel_free(void *memory) { free(memory); }
int proc_lookup(const char *path, struct storage_node *node)
{ (void)path; (void)node; return -2; }
int proc_readlink(const char *path, char *out, uint32_t capacity)
{ (void)path; (void)out; (void)capacity; return -2; }
int storage_lookup_path(const char *path, struct storage_node *node)
{
    struct fixture *f = find(path);
    if (!f) return -2;
    *node = (struct storage_node){.type = f->type};
    return 0;
}
int storage_readlink(const char *path, char *out, uint32_t capacity, uint32_t *length)
{
    struct fixture *f = find(path);
    if (!f) return -2;
    if (!f->target) return -22;
    *length = strlen(f->target);
    if (*length > capacity) *length = capacity;
    memcpy(out, f->target, *length);
    return 0;
}
int storage_inode_permissions(const struct storage_node *node,
                              struct leonos_permissions *value, bool write)
{ (void)node; (void)value; (void)write; assert(0); return -95; }
int osmlayer_auth_op(uint32_t op, void *data)
{
    assert(op == LEONOS_AUTH_OP_POSIX_PERMISSIONS);
    struct leonos_permissions_request *req = data;
    struct fixture *f = find(req->path);
    assert(f);
    if (req->action == LEONOS_PERMISSIONS_GET) req->value = f->value;
    else { assert(req->action == LEONOS_PERMISSIONS_SET); f->value = req->value; }
    return 0;
}

int main(void)
{
    struct task owner = {.uid = 100, .euid = 100, .gid = 200, .egid = 200, .umask = 0027};
    struct task group = {.uid = 101, .euid = 101, .gid = 200, .egid = 200};
    struct task other = {.uid = 102, .euid = 102, .gid = 300, .egid = 300};
    struct task root = {0};
    const char *file = "/data/file";
    assert(fs_permissions_check(&owner, file, 6, false) == 0);
    assert(fs_permissions_check(&group, file, 4, false) == 0);
    assert(fs_permissions_check(&group, file, 2, false) == -13);
    assert(fs_permissions_check(&other, file, 4, false) == -13);
    assert(fs_permissions_chmod(&other, file, NULL, 0777) == -1);
    assert(fs_permissions_chown(&other, file, NULL, UINT32_MAX, UINT32_MAX) == 0);
    assert(fs_permissions_chmod(&owner, "/data/missing", NULL, 0777) == -2);
    assert(fs_permissions_chmod(&owner, file, NULL, 0600) == 0);
    assert(fs_permissions_check(&group, file, 4, false) == -13);
    assert(fs_permissions_check(&root, file, 1, false) == -13);
    assert(fs_permissions_check(&root, file, 6, false) == 0);
    assert(fs_permissions_chmod(&owner, file, NULL, 0004) == 0);
    assert(fs_permissions_check(&owner, file, 4, false) == -13);
    assert(fs_permissions_check(&other, file, 4, false) == 0);
    entries[1].value.mode = 0700;
    assert(fs_permissions_check(&other, file, 4, false) == -13);
    char resolved[LEONOS_FS_PATH_LEN];
    assert(fs_permissions_resolve(&owner, "/", "/jump/../file", resolved, sizeof(resolved), false) == 0);
    assert(!strcmp(resolved, "/data/file"));
    assert(fs_permissions_resolve(&owner, "/", "/absolute", resolved, sizeof(resolved), false) == 0);
    assert(!strcmp(resolved, "/data/file"));
    assert(fs_permissions_resolve(&other, "/", "/absolute", resolved, sizeof(resolved), false) == -13);
    assert(fs_permissions_resolve(&owner, "/", "/loop", resolved, sizeof(resolved), false) == -40);
    assert(fs_permissions_resolve_flags(&owner, "/", "/loop", resolved, sizeof(resolved), false, 0) == 0);
    assert(!strcmp(resolved, "/loop"));
    assert(fs_permissions_resolve_flags(&owner, "/", "/jump/../link", resolved, sizeof(resolved), false, 0) == 0);
    assert(!strcmp(resolved, "/data/link"));
    assert(fs_permissions_resolve_flags(&owner, "/", "/jump/", resolved, sizeof(resolved), false, 0) == 0);
    assert(!strcmp(resolved, "/data/dir"));
    assert(fs_permissions_resolve_flags(&owner, "/", "/jump/", resolved, sizeof(resolved), false, FS_LOOKUP_PARENT) == 0);
    assert(!strcmp(resolved, "/jump/"));
    assert(fs_permissions_resolve_flags(&owner, "/", "/dangling", resolved, sizeof(resolved), false, FS_LOOKUP_PARENT) == 0);
    assert(!strcmp(resolved, "/dangling"));
    assert(fs_permissions_resolve(&owner, "/", "/dangling", resolved, sizeof(resolved), false) == 0);
    assert(!strcmp(resolved, "/missing"));
    assert(fs_permissions_resolve(&owner, "/", "/data/file/", resolved, sizeof(resolved), false) == -20);
    char chain[256] = "";
    for (unsigned i = 0; i < 40; ++i) strcat(chain, "/self");
    assert(fs_permissions_resolve(&owner, "/", chain, resolved, sizeof(resolved), false) == 0);
    assert(!strcmp(resolved, "/"));
    strcat(chain, "/self");
    assert(fs_permissions_resolve(&owner, "/", chain, resolved, sizeof(resolved), false) == -40);
    assert(fs_permissions_resolve(&other, "/", "/data/../", resolved, sizeof(resolved), false) == -13);
    assert(fs_permissions_resolve(&owner, "/", "/data/../data/file", resolved, sizeof(resolved), false) == 0);
    assert(!strcmp(resolved, file));
    assert(fs_permissions_resolve(&root, "/", "/absent/../data", resolved, sizeof(resolved), false) == -2);
    assert(fs_permissions_resolve(&owner, "/data", "file/../file", resolved, sizeof(resolved), false) == -20);
    assert(fs_permissions_resolve(&owner, "/data", "./file", resolved, sizeof(resolved), false) == 0);
    struct task access_root = other;
    access_root.uid = 0;
    assert(fs_permissions_resolve(&access_root, "/", "/data/../", resolved, sizeof(resolved), true) == 0);
    assert(fs_permissions_resolve(&access_root, "/", "/data/../", resolved, sizeof(resolved), false) == -13);
    entries[1].value.mode = 01777;
    assert(fs_permissions_parent(&other, file, true) == -1);
    assert(fs_permissions_parent(&owner, file, true) == 0);
    assert(fs_permissions_chown(&owner, file, NULL, 101, UINT32_MAX) == -1);
    assert(fs_permissions_chown(&owner, file, NULL, UINT32_MAX, 300) == -1);
    assert(fs_permissions_chmod(&root, file, NULL, 06755) == 0);
    assert(fs_permissions_chown(&root, file, NULL, 102, 300) == 0);
    assert(entries[2].value.uid == 102 && entries[2].value.gid == 300);
    assert(entries[2].value.mode == 0755);
    assert(fs_permissions_chown(&other, file, NULL, UINT32_MAX, UINT32_MAX) == 0);
    assert(entries[2].value.uid == 102 && entries[2].value.gid == 300);
    struct storage_node node = {.type = LEONOS_FS_TYPE_FILE};
    assert(fs_permissions_create(&owner, file, &node, 0666) == 0);
    assert(entries[2].value.mode == 0640 && entries[2].value.uid == 100 && entries[2].value.gid == 200);
    struct storage_node link_node = {.type = LEONOS_FS_TYPE_SYMLINK};
    entries[1].value = (struct leonos_permissions){02777, 100, 700};
    assert(fs_permissions_create(&owner, "/data/link", &link_node, 0777) == 0);
    assert(find("/data/link")->value.mode == 0777 && find("/data/link")->value.gid == 700);
    assert(fs_permissions_chmod(&owner, "/data/link", &link_node, 0600) == -95);
    assert(fs_permissions_chown(&root, "/data/link", &link_node, 123, 456) == 0);
    assert(find("/data/link")->value.uid == 123 && find("/data/link")->value.mode == 0777);
    assert(entries[2].value.uid == 100 && entries[2].value.mode == 0640);
    entries[1].value = (struct leonos_permissions){01777, 100, 200};
    struct task elevated = owner;
    elevated.uid = 102;
    assert(fs_permissions_check(&elevated, file, 6, false) == 0);
    assert(fs_permissions_check(&elevated, file, 6, true) == -13);
    uint32_t gids[] = {400, 200, 400, 300};
    assert(task_groups_set(&other, gids, 4) == 0);
    assert(other.groups->count == 4 && other.groups->ids[0] == 200 && other.groups->ids[1] == 300);
    assert(other.groups->ids[2] == 400 && other.groups->ids[3] == 400);
    assert(fs_permissions_check(&other, file, 4, false) == 0);
    assert(fs_permissions_check(&other, file, 2, false) == -13);
    struct task child = other;
    task_groups_retain(child.groups);
    assert(task_groups_set(&other, NULL, 0) == 0 && !other.groups);
    assert(fs_permissions_check(&child, file, 4, false) == 0);
    task_groups_release(&child);
    assert(!child.groups);
    puts("PASS Unix permissions: owner/group/other priority, search, sticky, chmod, chown, umask, access IDs");
}
