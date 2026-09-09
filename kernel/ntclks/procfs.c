/* Read-only procfs implementation for the Unix migration. */
#include <ntclks/mm.h>
#include <ntclks/sched.h>
#include <ntclks/smp.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/time.h>
#include <ntclks/version.h>
#include <leonos/fs.h>

#define PROCFS_PATH_MAX LEONOS_FS_PATH_LEN
#define PROCFS_CONTENT_MAX 1024u

static int proc_text_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

static void proc_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void proc_append_u64(char *dst, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (!value) {
        if (*pos + 1u < cap) { dst[*pos] = '0'; ++(*pos); dst[*pos] = 0; }
        return;
    }
    while (value && n + 1u < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) {
        if (*pos + 1u >= cap) return;
        dst[*pos] = tmp[--n];
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void proc_append_text(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    while (src && *src) {
        if (*pos + 1u >= cap) return;
        dst[*pos] = *src++;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static struct task *proc_task_for_path(const char *path)
{
    const char *p = path + 6; /* /proc/ */
    uint32_t pid = 0;
    if (p[0] == 's' && p[1] == 'e' && p[2] == 'l' && p[3] == 'f') {
        return sched_current_task();
    }
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10u + (uint32_t)(*p - '0');
        ++p;
    }
    if (!pid) return 0;
    return sched_find(pid);
}

static int proc_path_kind(const char *path, const char **file_name,
                          uint32_t *pid)
{
    const char *prefix = "/proc/";
    if (!path) return 0;
    for (uint32_t i = 0; prefix[i]; ++i)
        if (path[i] != prefix[i]) return 0;
    const char *p = path + 6;
    const char *slash = p;
    uint32_t value = 0;
    if (pid) *pid = 0;
    if (file_name) *file_name = 0;
    while (*slash && *slash != '/') ++slash;
    if (file_name && *slash) *file_name = slash + 1;
    while (p < slash && *p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
        ++p;
    }
    if (p == slash && value && path[6] != '0') {
        if (pid) *pid = value;
        return 2;
    }
    if (p == path + 6 && p < slash && (uint32_t)(slash - p) == 4u &&
        p[0] == 's' && p[1] == 'e' && p[2] == 'l' && p[3] == 'f') {
        return 3;
    }
    return 0;
}

static int proc_fill_content(const char *path, char *buffer, uint32_t capacity)
{
    uint32_t pos = 0;
    buffer[0] = 0;
    if (proc_text_eq(path, "/proc/uptime")) {
        uint64_t ms = time_uptime_ms();
        proc_append_u64(buffer, &pos, capacity, ms / 1000u);
        proc_append_text(buffer, &pos, capacity, ".");
        proc_append_u64(buffer, &pos, capacity, (ms % 1000u) / 100u);
        proc_append_text(buffer, &pos, capacity, " 0.00\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/meminfo")) {
        proc_append_text(buffer, &pos, capacity, "MemTotal: ");
        proc_append_u64(buffer, &pos, capacity, mm_total_memory_kib());
        proc_append_text(buffer, &pos, capacity, " kB\nMemFree: ");
        proc_append_u64(buffer, &pos, capacity, mm_free_memory_kib());
        /* There is no swap or reclaimable page-cache pool yet. All memory
         * currently available for new allocations is in the free-page pool. */
        proc_append_text(buffer, &pos, capacity, " kB\nMemAvailable: ");
        proc_append_u64(buffer, &pos, capacity, mm_free_memory_kib());
        proc_append_text(buffer, &pos, capacity, " kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/version")) {
        const struct leonos_system_info *info = ntclks_system_info();
        proc_append_text(buffer, &pos, capacity, "LeonOS ");
        if (info) proc_append_text(buffer, &pos, capacity, info->kernel_version);
        proc_append_text(buffer, &pos, capacity, " leonos\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/machine-id")) {
        proc_append_text(buffer, &pos, capacity,
                         "00000000000000000000000000000000\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/stat")) {
        uint64_t busy = 0;
        uint64_t idle = 0;
        uint64_t per_cpu_busy[SMP_MAX_CPUS] = {0};
        uint64_t per_cpu_idle[SMP_MAX_CPUS] = {0};
        uint32_t cpu_count = smp_cpu_count();
        if (cpu_count > SMP_MAX_CPUS) cpu_count = SMP_MAX_CPUS;
        sched_cpu_ticks(&busy, &idle);
        sched_cpu_ticks_per_cpu(per_cpu_busy, per_cpu_idle, cpu_count);
        proc_append_text(buffer, &pos, capacity, "cpu ");
        proc_append_u64(buffer, &pos, capacity, busy);
        proc_append_text(buffer, &pos, capacity, " 0 0 ");
        proc_append_u64(buffer, &pos, capacity, idle);
        proc_append_text(buffer, &pos, capacity, " 0 0 0 0 0 0\n");
        for (uint32_t cpu = 0; cpu < cpu_count; ++cpu) {
            proc_append_text(buffer, &pos, capacity, "cpu");
            proc_append_u64(buffer, &pos, capacity, cpu);
            proc_append_text(buffer, &pos, capacity, " ");
            proc_append_u64(buffer, &pos, capacity, per_cpu_busy[cpu]);
            proc_append_text(buffer, &pos, capacity, " 0 0 ");
            proc_append_u64(buffer, &pos, capacity, per_cpu_idle[cpu]);
            proc_append_text(buffer, &pos, capacity, " 0 0 0 0 0 0\n");
        }
        return 0;
    }
    {
        const char *file = 0;
        uint32_t pid = 0;
        int kind = proc_path_kind(path, &file, &pid);
        if (kind == 3) {
            pid = sched_current_pid();
            kind = 2;
        }
        if (kind == 2 && file && pid) {
            struct task *task = sched_find(pid);
            if (proc_text_eq(file, "status")) {
                if (!task) return -2;
                proc_append_text(buffer, &pos, capacity, "Name:\t");
                const char *name = task->name ? task->name : "?";
                for (uint32_t i = 0; name[i] && i < 15; ++i) {
                    char letter[2] = {name[i], 0};
                    if (name[i] == '\n') proc_append_text(buffer, &pos, capacity, "\\n");
                    else if (name[i] == '\\') proc_append_text(buffer, &pos, capacity, "\\\\");
                    else proc_append_text(buffer, &pos, capacity, letter);
                }
                const char *state = task->state == TASK_EXITED ? "Z (zombie)" :
                    task->state == TASK_STOPPED ? "T (stopped)" :
                    task->state == TASK_BLOCKED ? "S (sleeping)" : "R (running)";
                proc_append_text(buffer, &pos, capacity, "\nState:\t");
                proc_append_text(buffer, &pos, capacity, state);
                proc_append_text(buffer, &pos, capacity, "\nTgid:\t");
                proc_append_u64(buffer, &pos, capacity, sched_task_tgid(task));
                proc_append_text(buffer, &pos, capacity, "\nPid:\t");
                proc_append_u64(buffer, &pos, capacity, task->pid);
                proc_append_text(buffer, &pos, capacity, "\nPPid:\t");
                proc_append_u64(buffer, &pos, capacity, task->parent_pid);
                const uint32_t ids[] = {task->uid, task->euid, task->suid, task->fsuid,
                                        task->gid, task->egid, task->sgid, task->fsgid};
                for (uint32_t i = 0; i < 8; ++i) {
                    proc_append_text(buffer, &pos, capacity, i == 0 ? "\nUid:\t" : i == 4 ? "\nGid:\t" : "\t");
                    proc_append_u64(buffer, &pos, capacity, ids[i]);
                }
                if (sched_task_as(task)->cr3) {
                    proc_append_text(buffer, &pos, capacity, "\nVmRSS:\t");
                    proc_append_u64(buffer, &pos, capacity, address_space_user_resident_kib(sched_task_as(task)));
                    proc_append_text(buffer, &pos, capacity, " kB");
                }
                proc_append_text(buffer, &pos, capacity, "\n");
                return 0;
            }
            if (proc_text_eq(file, "stat")) {
                if (!task) return -2;
                proc_append_u64(buffer, &pos, capacity, task->pid);
                proc_append_text(buffer, &pos, capacity, " (");
                proc_append_text(buffer, &pos, capacity, task->name ? task->name : "?");
                proc_append_text(buffer, &pos, capacity, ") ");
                proc_append_u64(buffer, &pos, capacity, task->state);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->parent_pid);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->process_group);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->process_session);
                proc_append_text(buffer, &pos, capacity, " 0 0 0 0 0 ");
                proc_append_u64(buffer, &pos, capacity, task->cpu_ticks);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->uid);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->role);
                proc_append_text(buffer, &pos, capacity, " ");
                proc_append_u64(buffer, &pos, capacity, task->flags);
                proc_append_text(buffer, &pos, capacity, "\n");
                return 0;
            }
            if (proc_text_eq(file, "cmdline")) {
                if (!task) return -2;
                proc_append_text(buffer, &pos, capacity,
                                 task->path[0] ? task->path : task->name);
                proc_append_text(buffer, &pos, capacity, "\n");
                return 0;
            }
        }
    }
    return -2;
}

int proc_lookup(const char *path, struct storage_node *out)
{
    char content[PROCFS_CONTENT_MAX];
    if (!path || (!proc_text_eq(path, "/proc") && path[0] != '/') ||
        path[0] != '/' || path[1] != 'p' || path[2] != 'r' ||
        path[3] != 'o' || path[4] != 'c') {
        return -2;
    }
    if (proc_text_eq(path, "/proc")) {
        if (out) {
            *out = (struct storage_node){
                .type = LEONOS_FS_TYPE_DIR,
                .flags = STORAGE_NODE_FLAG_PROC,
                .first_cluster = 0x50524f43u, /* PROC */
                .volume_id = 0,
                .size = 0,
            };
        }
        return 0;
    }
    if (proc_text_eq(path, "/proc/uptime") ||
        proc_text_eq(path, "/proc/meminfo") ||
        proc_text_eq(path, "/proc/version") ||
        proc_text_eq(path, "/proc/machine-id") ||
        proc_text_eq(path, "/proc/stat")) {
        uint32_t len = 0;
        int ret = proc_fill_content(path, content, sizeof(content));
        if (ret < 0) return ret;
        while (content[len]) ++len;
        if (out) {
            *out = (struct storage_node){
                .type = LEONOS_FS_TYPE_FILE,
                .flags = STORAGE_NODE_FLAG_PROC,
                .first_cluster = 0x50524f43u,
                .volume_id = 0,
                .size = len,
            };
        }
        return 0;
    }
    {
        const char *file = 0;
        uint32_t pid = 0;
        int kind = proc_path_kind(path, &file, &pid);
        if ((kind == 2 || kind == 3) && !file) {
            if (kind == 3) pid = sched_current_pid();
            if (!pid || !sched_find(pid)) return -2;
            if (out) *out = (struct storage_node){
                .type = LEONOS_FS_TYPE_DIR,
                .flags = STORAGE_NODE_FLAG_PROC,
                .first_cluster = 0x50524f43u,
            };
            return 0;
        }
        if ((kind == 2 || kind == 3) && file &&
            (proc_text_eq(file, "stat") || proc_text_eq(file, "cmdline") || proc_text_eq(file, "status"))) {
            if (kind == 2 && !sched_find(pid)) return -2;
            if (out) {
                *out = (struct storage_node){
                    .type = LEONOS_FS_TYPE_FILE,
                    .flags = STORAGE_NODE_FLAG_PROC,
                    .first_cluster = 0x50524f43u,
                    .volume_id = 0,
                    .size = 512,
                };
            }
            return 0;
        }
    }
    return -2;
}

int proc_read(const char *path, uint64_t offset, void *buffer, uint32_t length,
              uint32_t *out_read)
{
    char content[PROCFS_CONTENT_MAX];
    uint32_t len = 0;
    int ret = proc_fill_content(path, content, sizeof(content));
    if (ret < 0) return ret;
    while (content[len]) ++len;
    if (offset >= len) {
        if (out_read) *out_read = 0;
        return 0;
    }
    if (length > len - offset) length = (uint32_t)(len - offset);
    for (uint32_t i = 0; i < length; ++i) {
        ((uint8_t *)buffer)[i] = (uint8_t)content[offset + i];
    }
    if (out_read) *out_read = length;
    return 0;
}

int proc_readlink(const char *path, char *buffer, uint32_t capacity)
{
    const char *file = NULL;
    uint32_t pid = 0;
    struct task *caller = sched_current_task();
    struct task *task;
    uint32_t length = 0;
    if (!path || !buffer || !capacity) return -LEONOS_EINVAL;
    int kind = proc_path_kind(path, &file, &pid);
    if (kind != 2 && kind != 3) return -LEONOS_ENOENT;
    if (kind == 3 && !file) {
        char target[16] = {0};
        if (!caller) return -LEONOS_ENOENT;
        proc_append_u64(target, &length, sizeof(target), sched_task_tgid(caller));
        if (length > capacity) length = capacity;
        for (uint32_t i = 0; i < length; ++i) buffer[i] = target[i];
        return (int)length;
    }
    task = kind == 3 ? caller : sched_find(pid);
    if (!task) return -LEONOS_ENOENT;
    if (!file || !proc_text_eq(file, "exe"))
        return proc_lookup(path, NULL) == 0 ? -LEONOS_EINVAL : -LEONOS_ENOENT;
    /* Match the available FSCREDS/dumpable policy. Capability namespaces and
     * executable inode tracking still need the common ptrace/VFS machinery. */
    if (!caller) return -LEONOS_EACCES;
    if (sched_task_tgid(caller) != sched_task_tgid(task) && caller->euid &&
        (caller->fsuid != task->uid || caller->fsuid != task->euid ||
         caller->fsuid != task->suid || caller->fsgid != task->gid ||
         caller->fsgid != task->egid || caller->fsgid != task->sgid ||
         sched_task_mm(task)->nondumpable)) return -LEONOS_EACCES;
    if (!task->path[0]) return -LEONOS_ENOENT;
    while (length < capacity && task->path[length]) {
        buffer[length] = task->path[length];
        ++length;
    }
    /* readlink(2) does not append NUL and truncates at bufsiz. */
    return (int)length;
}

int proc_readdir(const char *path, uint64_t *offset, struct leonos_dir_entry *entry)
{
    uint32_t index;
    static const char *files[] = {"uptime", "meminfo", "version", "machine-id", "stat"};
    if (!path || !offset || !entry) return -22;
    if (!proc_text_eq(path, "/proc")) {
        const char *file = NULL;
        uint32_t pid;
        int kind = proc_path_kind(path, &file, &pid);
        if (kind == 3) pid = sched_current_pid();
        if ((kind != 2 && kind != 3) || file) return -20;
        if (!sched_find(pid)) return -2;
        static const char *task_files[] = {"stat", "cmdline", "status"};
        if (*offset >= sizeof(task_files) / sizeof(task_files[0])) return 0;
        *entry = (struct leonos_dir_entry){.type = LEONOS_FS_TYPE_FILE};
        proc_copy(entry->name, sizeof(entry->name), task_files[(*offset)++]);
        return 1;
    }
    index = (uint32_t)*offset;
    if (index < sizeof(files) / sizeof(files[0])) {
        entry->type = LEONOS_FS_TYPE_FILE;
        proc_copy(entry->name, sizeof(entry->name), files[index]);
        *offset = index + 1u;
        return 1;
    }
    {
        struct task_snapshot_info snapshots[SCHED_TASK_MAX];
        uint32_t count = sched_snapshot(snapshots, SCHED_TASK_MAX, 0);
        uint32_t task_index = index - (uint32_t)(sizeof(files) / sizeof(files[0]));
        while (task_index < count) {
            ++*offset;
            if (!snapshots[task_index].pid) { ++task_index; continue; }
            char name[16];
            uint32_t pos = 0;
            proc_copy(name, sizeof(name), "");
            proc_append_u64(name, &pos, sizeof(name), snapshots[task_index].pid);
            entry->type = LEONOS_FS_TYPE_DIR;
            proc_copy(entry->name, sizeof(entry->name), name);
            return 1;
        }
    }
    return 0;
}
