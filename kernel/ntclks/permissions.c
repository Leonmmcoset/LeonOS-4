#include <ntclks/permissions.h>
#include <ntclks/osmlayer.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/heap.h>

bool task_in_group(const struct task *task, uint32_t gid, bool real_ids)
{
    if (!task) return !gid;
    uint32_t primary = real_ids ? task->gid : (task->fsgid ? task->fsgid : task->egid);
    if (gid == primary) return true;
    const struct task_groups *groups = task->groups;
    if (groups) for (uint32_t i = 0; i < groups->count; ++i)
        if (groups->ids[i] == gid) return true;
    return false;
}

void task_groups_retain(struct task_groups *groups)
{
    if (groups) __atomic_add_fetch(&groups->references, 1, __ATOMIC_RELAXED);
}

void task_groups_release(struct task *task)
{
    struct task_groups *groups = task->groups;
    task->groups = NULL;
    if (groups && !__atomic_sub_fetch(&groups->references, 1, __ATOMIC_ACQ_REL)) kernel_free(groups);
}

int task_groups_set(struct task *task, const uint32_t *ids, uint32_t count)
{
    if (!task || count > 65536u) return -LEONOS_EINVAL;
    struct task_groups *groups = NULL;
    if (count) {
        groups = kernel_malloc(sizeof(*groups) + (size_t)count * sizeof(*ids));
        if (!groups) return -LEONOS_ENOMEM;
        groups->references = 1;
        groups->count = count;
        for (uint32_t i = 0; i < count; ++i) {
            if (ids[i] == UINT32_MAX) { kernel_free(groups); return -LEONOS_EINVAL; }
            groups->ids[i] = ids[i];
        }
        /* Linux exposes the sorted supplementary list, retaining duplicates.
         * Heapsort bounds work for the full NGROUPS_MAX input. */
        for (uint32_t i = 1; i < count; ++i) {
            uint32_t child = i;
            while (child && groups->ids[(child - 1) / 2] < groups->ids[child]) {
                uint32_t parent = (child - 1) / 2, tmp = groups->ids[parent];
                groups->ids[parent] = groups->ids[child]; groups->ids[child] = tmp;
                child = parent;
            }
        }
        for (uint32_t end = count - 1; end; --end) {
            uint32_t tmp = groups->ids[0]; groups->ids[0] = groups->ids[end]; groups->ids[end] = tmp;
            uint32_t parent = 0;
            while (parent * 2 + 1 < end) {
                uint32_t child = parent * 2 + 1;
                if (child + 1 < end && groups->ids[child] < groups->ids[child + 1]) ++child;
                if (groups->ids[parent] >= groups->ids[child]) break;
                tmp = groups->ids[parent]; groups->ids[parent] = groups->ids[child]; groups->ids[child] = tmp;
                parent = child;
            }
        }
    }
    task_groups_release(task);
    task->groups = groups;
    return 0;
}

static struct {
    bool used;
    uint32_t kind, volume;
    struct leonos_permissions value;
} device_modes[128];

static int device_metadata(const struct storage_node *node,
                           struct leonos_permissions *value, bool write)
{
    unsigned free_slot = 128;
    for (unsigned i = 0; i < 128; ++i) {
        if (!device_modes[i].used) { if (free_slot == 128) free_slot = i; continue; }
        if (device_modes[i].kind == node->first_cluster && device_modes[i].volume == node->volume_id) {
            if (write) device_modes[i].value = *value;
            else *value = device_modes[i].value;
            return 0;
        }
    }
    if (!write) {
        uint32_t mode = node->type == LEONOS_FS_TYPE_DIR ? 0755 : 0666;
        if (node->flags & STORAGE_NODE_FLAG_DEV_BLOCK) mode = 0600;
        *value = (struct leonos_permissions){mode, 0, 0};
        return 0;
    }
    if (free_slot == 128) return -LEONOS_ENOSPC;
    device_modes[free_slot].used = true;
    device_modes[free_slot].kind = node->first_cluster;
    device_modes[free_slot].volume = node->volume_id;
    device_modes[free_slot].value = *value;
    return 0;
}

static bool metadata_path(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) if (*p == '/') base = p + 1;
    const char *name = "LEONACL.SYS";
    while (*base && *name) {
        char ch = *base++;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch != *name++) return false;
    }
    return !*base && !*name;
}

static int copy_path(char *out, const char *path)
{
    if (!path || path[0] != '/') return -LEONOS_EINVAL;
    uint32_t i = 0;
    for (; path[i]; ++i) {
        if (i + 1 >= LEONOS_FS_PATH_LEN) return -LEONOS_ENAMETOOLONG;
        out[i] = path[i];
    }
    out[i] = 0;
    return 0;
}

static int lookup(const char *path, struct storage_node *node)
{
    int ret = proc_lookup(path, node);
    return ret == 0 ? 0 : storage_lookup_path(path, node);
}

int fs_permissions_get(const char *path, const struct storage_node *node,
                       struct leonos_permissions *value)
{
    struct leonos_permissions_request req = {.action = LEONOS_PERMISSIONS_GET};
    struct storage_node found;
    int ret = copy_path(req.path, path);
    if (ret < 0) return ret;
    if (!node) {
        ret = lookup(path, &found);
        if (ret < 0) return ret;
        node = &found;
    }
    if (node->flags & STORAGE_NODE_FLAG_EXT2) return storage_inode_permissions(node, value, false);
    /* Synthetic nodes do not reside in a mounted data filesystem. */
    if (node->flags & (STORAGE_NODE_FLAG_DEV_NODE | STORAGE_NODE_FLAG_DEV_DIR | STORAGE_NODE_FLAG_DEV_FB0)) {
        return device_metadata(node, value, false);
    }
    if (proc_lookup(path, &found) == 0) {
        *value = (struct leonos_permissions){node->type == LEONOS_FS_TYPE_DIR ? 0555 : 0444, 0, 0};
        return 0;
    }
    ret = osmlayer_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &req);
    if (!ret) *value = req.value;
    return ret;
}

static int store(const char *path, const struct storage_node *node,
                 const struct leonos_permissions *value)
{
    struct leonos_permissions_request req = {.action = LEONOS_PERMISSIONS_SET, .value = *value};
    if (node->flags & STORAGE_NODE_FLAG_EXT2) return storage_inode_permissions(node, &req.value, true);
    if (metadata_path(path)) return -LEONOS_EPERM;
    if (node->flags & (STORAGE_NODE_FLAG_DEV_NODE | STORAGE_NODE_FLAG_DEV_DIR | STORAGE_NODE_FLAG_DEV_FB0))
        return device_metadata(node, &req.value, true);
    struct storage_node proc;
    if (proc_lookup(path, &proc) == 0) return -LEONOS_EROFS;
    int ret = copy_path(req.path, path);
    return ret < 0 ? ret : osmlayer_auth_op(LEONOS_AUTH_OP_POSIX_PERMISSIONS, &req);
}

static int check_node(const struct task *task, const char *path,
                      const struct storage_node *node, uint32_t access, bool real_ids)
{
    if ((access & FS_ACCESS_WRITE) && metadata_path(path)) return -LEONOS_EPERM;
    struct leonos_permissions value;
    uint32_t uid = task ? (real_ids ? task->uid : (task->fsuid ? task->fsuid : task->euid)) : 0;
    if (!uid && (!(access & FS_ACCESS_EXEC) || node->type == LEONOS_FS_TYPE_DIR)) return 0;
    int ret = fs_permissions_get(path, node, &value);
    if (ret < 0) return ret;
    if (!uid) {
        if (!(access & FS_ACCESS_EXEC) || node->type == LEONOS_FS_TYPE_DIR || (value.mode & 0111u)) return 0;
        return -LEONOS_EACCES;
    }
    uint32_t shift = uid == value.uid ? 6 : task_in_group(task, value.gid, real_ids) ? 3 : 0;
    return (((value.mode >> shift) & access) == access) ? 0 : -LEONOS_EACCES;
}

/**
 * @brief Resolves a pathname with Linux link ordering and directory search checks.
 * @param task Credentials or NULL for root.
 * @param base Absolute starting directory for relative input.
 * @param input Kernel pathname; a missing last component is allowed for creation.
 * @param out Non-aliasing output buffer for the absolute path.
 * @param cap Output buffer size including NUL.
 * @param real_ids Whether to use real credentials.
 * @param flags Final component policy, FS_LOOKUP_FOLLOW or FS_LOOKUP_PARENT.
 * @return Zero or a negative errno, including ELOOP after 40 links.
 */
int fs_permissions_resolve_flags(const struct task *task, const char *base, const char *input,
                                 char *out, uint32_t cap, bool real_ids, uint32_t flags)
{
    char pending[LEONOS_FS_PATH_LEN], target[LEONOS_FS_PATH_LEN];
    uint32_t links = 0, input_len = 0;
    if (!input || !*input) return -LEONOS_ENOENT;
    if (!out || cap < 2) return -LEONOS_ENAMETOOLONG;
    const char *start = input[0] == '/' ? "/" : base;
    if (!start || *start != '/') return -LEONOS_EINVAL;
    for (; input[input_len]; ++input_len) {
        if (input_len + 1 >= sizeof(pending)) return -LEONOS_ENAMETOOLONG;
        pending[input_len] = input[input_len];
    }
    pending[input_len] = 0;
    input = pending;
    uint32_t length = 0;
    while (start[length]) {
        if (length + 1 >= cap) return -LEONOS_ENAMETOOLONG;
        out[length] = start[length];
        ++length;
    }
    out[length] = 0;
    while (*input) {
        while (*input == '/') ++input;
        if (!*input) break;
        struct storage_node node;
        int ret = lookup(out, &node);
        if (ret < 0) return ret;
        if (node.type != LEONOS_FS_TYPE_DIR) return -LEONOS_ENOTDIR;
        ret = check_node(task, out, &node, FS_ACCESS_EXEC, real_ids);
        if (ret < 0) return ret;
        const char *component = input;
        while (*input && *input != '/') ++input;
        uint32_t size = (uint32_t)(input - component);
        const char *rest = input;
        while (*rest == '/') ++rest;
        bool last = !*rest;
        uint32_t parent_length = length;
        if (last && (flags & FS_LOOKUP_PARENT)) {
            if (size >= LEONOS_FS_NAME_LEN || length + (length > 1) + size + (*input != 0) >= cap)
                return -LEONOS_ENAMETOOLONG;
            if (length > 1) out[length++] = '/';
            for (uint32_t i = 0; i < size; ++i) out[length++] = component[i];
            if (*input) out[length++] = '/';
            out[length] = 0;
            return 0;
        }
        if (size == 1 && component[0] == '.') continue;
        if (size == 2 && component[0] == '.' && component[1] == '.') {
            while (length > 1 && out[length - 1] != '/') --length;
            if (length > 1) --length;
            out[length] = 0;
            continue;
        }
        if (size >= LEONOS_FS_NAME_LEN || length + (length > 1) + size >= cap)
            return -LEONOS_ENAMETOOLONG;
        if (length > 1) out[length++] = '/';
        for (uint32_t i = 0; i < size; ++i) out[length++] = component[i];
        out[length] = 0;
        ret = lookup(out, &node);
        if (ret == -LEONOS_ENOENT && last && !*input) return 0;
        if (ret < 0) return ret;
        if (node.type == LEONOS_FS_TYPE_SYMLINK &&
            (!last || *input || (flags & FS_LOOKUP_FOLLOW))) {
            uint32_t got = 0, suffix = 0;
            if (++links > 40) return -LINUX_ELOOP;
            ret = storage_readlink(out, target, sizeof(target), &got);
            if (ret < 0) return ret;
            if (!got) return -LEONOS_ENOENT;
            while (input[suffix]) ++suffix;
            if (got + suffix >= sizeof(pending)) return -LEONOS_ENAMETOOLONG;
            /* Link substitution can move the overlapping suffix in either direction. */
            if (pending + got < input) {
                for (uint32_t i = 0; i <= suffix; ++i) pending[got + i] = input[i];
            } else {
                for (uint32_t i = suffix + 1; i > 0; --i) pending[got + i - 1] = input[i - 1];
            }
            for (uint32_t i = 0; i < got; ++i) pending[i] = target[i];
            length = target[0] == '/' ? 1 : parent_length;
            out[length] = 0;
            input = pending;
        } else if (*input && node.type != LEONOS_FS_TYPE_DIR) {
            return -LEONOS_ENOTDIR;
        }
    }
    return 0;
}

/** @brief Resolves a pathname following the final symlink with the caller's credentials. */
int fs_permissions_resolve(const struct task *task, const char *base, const char *input,
                           char *out, uint32_t cap, bool real_ids)
{
    return fs_permissions_resolve_flags(task, base, input, out, cap, real_ids, FS_LOOKUP_FOLLOW);
}

int fs_permissions_search(const struct task *task, const char *path, bool real_ids)
{
    char prefix[LEONOS_FS_PATH_LEN];
    int ret = copy_path(prefix, path);
    if (ret < 0) return ret;
    struct storage_node node;
    for (uint32_t i = 1;; ++i) {
        if (i == 1 || prefix[i] == '/') {
            char saved = prefix[i];
            prefix[i] = 0;
            ret = lookup(prefix, &node);
            if (!ret && node.type != LEONOS_FS_TYPE_DIR) ret = -LEONOS_ENOTDIR;
            if (!ret) ret = check_node(task, prefix, &node, FS_ACCESS_EXEC, real_ids);
            prefix[i] = saved;
            if (ret < 0) return ret;
        }
        if (!prefix[i]) break;
    }
    return 0;
}

int fs_permissions_check(const struct task *task, const char *path, uint32_t access,
                         bool real_ids)
{
    struct storage_node node;
    int ret = fs_permissions_search(task, path, real_ids);
    if (!ret) ret = lookup(path, &node);
    if (!ret) ret = check_node(task, path, &node, access, real_ids);
    return ret;
}

int fs_permissions_parent(const struct task *task, const char *path, bool deleting)
{
    char parent[LEONOS_FS_PATH_LEN];
    int ret = copy_path(parent, path);
    if (ret < 0) return ret;
    if (metadata_path(path)) return -LEONOS_EPERM;
    uint32_t slash = 0;
    for (uint32_t i = 1; parent[i]; ++i) if (parent[i] == '/') slash = i;
    parent[slash ? slash : 1] = 0;
    ret = fs_permissions_check(task, parent, FS_ACCESS_WRITE | FS_ACCESS_EXEC, false);
    if (ret < 0 || !deleting || !task || !task->euid) return ret;
    struct leonos_permissions dir, file;
    ret = fs_permissions_get(parent, NULL, &dir);
    if (ret < 0 || !(dir.mode & 01000u) || task->euid == dir.uid) return ret;
    ret = fs_permissions_get(path, NULL, &file);
    if (ret < 0) return ret;
    return task->euid == file.uid ? 0 : -LEONOS_EPERM;
}

int fs_permissions_create(const struct task *task, const char *path,
                          const struct storage_node *node, uint32_t mode)
{
    struct leonos_permissions value = {mode & 07777u, task ? task->euid : 0, task ? task->egid : 0};
    if (node->type == LEONOS_FS_TYPE_SOCKET) value.mode |= LINUX_S_IFSOCK;
    if (node->type == LEONOS_FS_TYPE_SYMLINK) value.mode = 0777u;
    else value.mode &= ~(task ? (*sched_task_umask(task)) : 0022u);
    char parent[LEONOS_FS_PATH_LEN];
    int ret = copy_path(parent, path);
    if (ret < 0) return ret;
    uint32_t slash = 0;
    for (uint32_t i = 1; parent[i]; ++i) if (parent[i] == '/') slash = i;
    parent[slash ? slash : 1] = 0;
    struct leonos_permissions inherited;
    ret = fs_permissions_get(parent, NULL, &inherited);
    if (ret < 0) return ret;
    if (inherited.mode & 02000u) {
        value.gid = inherited.gid;
        if (node->type == LEONOS_FS_TYPE_DIR) value.mode |= 02000u;
    }
    if (task && task->euid && !task_in_group(task, value.gid, false) && node->type != LEONOS_FS_TYPE_DIR)
        value.mode &= ~02000u;
    return store(path, node, &value);
}

int fs_permissions_chmod(const struct task *task, const char *path,
                         const struct storage_node *node, uint32_t mode)
{
    struct leonos_permissions value;
    struct storage_node found;
    int ret;
    if (!node) {
        ret = fs_permissions_search(task, path, false);
        if (!ret) ret = lookup(path, &found);
        if (ret < 0) return ret;
        node = &found;
    }
    ret = fs_permissions_get(path, node, &value);
    if (ret < 0) return ret;
    if (task && task->euid && task->euid != value.uid) return -LEONOS_EPERM;
    if (node->type == LEONOS_FS_TYPE_SYMLINK) return -LEONOS_EOPNOTSUPP;
    value.mode = (value.mode & LINUX_S_IFMT) | (mode & 07777u);
    if (task && task->euid && !task_in_group(task, value.gid, false)) value.mode &= ~02000u;
    return store(path, node, &value);
}

int fs_permissions_chown(const struct task *task, const char *path,
                         const struct storage_node *node, uint32_t uid, uint32_t gid)
{
    struct leonos_permissions value;
    struct storage_node found;
    int ret;
    if (!node) {
        ret = fs_permissions_search(task, path, false);
        if (!ret) ret = lookup(path, &found);
        if (ret < 0) return ret;
        node = &found;
    }
    ret = fs_permissions_get(path, node, &value);
    if (ret < 0) return ret;
    bool drop_sgid = (value.mode & 02000u) &&
                     ((value.mode & 0010u) || (task && task->euid && !task_in_group(task, value.gid, false)));
    if (task && task->euid &&
        (((uid != UINT32_MAX || gid != UINT32_MAX ||
           (node->type != LEONOS_FS_TYPE_DIR && ((value.mode & 04000u) || drop_sgid))) && task->euid != value.uid) ||
         (uid != UINT32_MAX && uid != value.uid) ||
         (gid != UINT32_MAX && gid != value.gid && !task_in_group(task, gid, false)))) return -LEONOS_EPERM;
    if (uid != UINT32_MAX) value.uid = uid;
    if (gid != UINT32_MAX) value.gid = gid;
    if (node->type != LEONOS_FS_TYPE_DIR) {
        value.mode &= ~04000u;
        if (drop_sgid) value.mode &= ~02000u;
    }
    return store(path, node, &value);
}
