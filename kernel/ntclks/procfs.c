/* Read-only procfs implementation for the Unix migration. */
#include <ntclks/mm.h>
#include <ntclks/sched.h>
#include <ntclks/smp.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/time.h>
#include <ntclks/version.h>
#include <ntclks/uts.h>
#include <ntclks/inventory.h>
#include <ntclks/text_stream.h>
#include <ntclks/pty.h>
#include <leonos/fs.h>
#include <linux/capability.h>

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
        uint64_t busy, idle;
        sched_cpu_ticks(&busy, &idle);
        uint64_t ms = time_uptime_ms();
        proc_append_u64(buffer, &pos, capacity, ms / 1000u);
        proc_append_text(buffer, &pos, capacity, ".");
        proc_append_u64(buffer, &pos, capacity, (ms % 1000u) / 100u);
        proc_append_text(buffer, &pos, capacity, " ");
        proc_append_u64(buffer, &pos, capacity, idle / NTCLKS_TICK_HZ);
        proc_append_text(buffer, &pos, capacity, ".");
        uint64_t fraction = (idle % NTCLKS_TICK_HZ) * 100 / NTCLKS_TICK_HZ;
        if (fraction < 10) proc_append_text(buffer, &pos, capacity, "0");
        proc_append_u64(buffer, &pos, capacity, fraction);
        proc_append_text(buffer, &pos, capacity, "\n");
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
        if (info) proc_append_text(buffer, &pos, capacity, info->kernel_name);
        proc_append_text(buffer, &pos, capacity, " version ");
        if (info) proc_append_text(buffer, &pos, capacity, info->kernel_version);
        proc_append_text(buffer, &pos, capacity, " (");
        if (info) proc_append_text(buffer, &pos, capacity, info->build_time);
        proc_append_text(buffer, &pos, capacity, ")\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/filesystems")) {
        proc_append_text(buffer, &pos, capacity,
                         "\text2\n\tvfat\n\texfat\n\tiso9660\nnodev\tproc\nnodev\tdevfs\nnodev\tsysfs\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/sys/kernel/hostname") ||
        proc_text_eq(path, "/proc/sys/kernel/domainname")) {
        char hostname[65], domainname[65];
        linux_uts_names(hostname, domainname);
        proc_append_text(buffer, &pos, capacity,
                         proc_text_eq(path, "/proc/sys/kernel/hostname") ? hostname : domainname);
        proc_append_text(buffer, &pos, capacity, "\n");
        return 0;
    }
    if (proc_text_eq(path, "/proc/sys/kernel/ostype") ||
        proc_text_eq(path, "/proc/sys/kernel/osrelease") ||
        proc_text_eq(path, "/proc/sys/kernel/version")) {
        const struct leonos_system_info *info = ntclks_system_info();
        const char *value = !info ? "" :
            proc_text_eq(path, "/proc/sys/kernel/ostype") ? info->kernel_name :
            proc_text_eq(path, "/proc/sys/kernel/version") ? info->build_time :
            info->kernel_version;
        proc_append_text(buffer, &pos, capacity, value);
        proc_append_text(buffer, &pos, capacity, "\n");
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
                proc_append_text(buffer, &pos, capacity, "\nLeonOSRole:\t");
                proc_append_u64(buffer, &pos, capacity, task->role);
                proc_append_text(buffer, &pos, capacity, "\nLeonOSFlags:\t");
                proc_append_u64(buffer, &pos, capacity, task->flags);
                proc_append_text(buffer, &pos, capacity, "\n");
                return 0;
            }

        }
    }
    return -2;
}

/** @brief Recognize the global mount table and per-process view of this namespace. */
static int proc_mount_view(const char *path)
{
    const char *file = NULL;
    uint32_t pid = 0;
    int kind = proc_path_kind(path, &file, &pid);
    if (kind == 3) pid = sched_current_pid();
    if ((kind != 2 && kind != 3) || !file || !pid || !sched_find(pid)) return 0;
    return proc_text_eq(file, "mounts") ? 1 : proc_text_eq(file, "mountinfo") ? 2 : 0;
}

/** @brief Map scheduler states to Linux proc characters, never expose private enums. */
static char proc_state(const struct task *task)
{
    return task->state == TASK_EXITED    ? 'Z'
           : task->state == TASK_STOPPED ? 'T'
           : task->state == TASK_BLOCKED ? 'S'
                                         : 'R';
}
/** @brief Read mutable argv bytes from the target address space, bounded by exec's argument range. */
static int proc_cmdline(struct task *task, struct text_stream *s)
{
    struct task_address_space_state *mm = sched_task_mm(task);
    if (task->state == TASK_EXITED || !mm->as.cr3 || mm->arg_end <= mm->arg_start)
        return 0;
    uint64_t size = mm->arg_end - mm->arg_start;
    if (s->offset >= size)
        return 0;
    uint64_t position = mm->arg_start + s->offset;
    while (position < mm->arg_end && s->written < s->capacity) {
        if (!address_space_user_page_readable(&mm->as, position))
            return s->written ? 0 : -5;
        uint64_t phys = address_space_user_page_phys(&mm->as, position);
        const char *page = paging_kernel_direct_map(phys);
        if (!phys || !page)
            return s->written ? 0 : -5;
        uint32_t count = 4096 - (uint32_t)(position & 4095);
        if (count > mm->arg_end - position)
            count = (uint32_t)(mm->arg_end - position);
        if (count > s->capacity - s->written)
            count = s->capacity - s->written;
        __builtin_memcpy(s->buffer + s->written, page + (position & 4095), count);
        s->written += count;
        position += count;
    }
    return 0;
}
/** @brief Linux PTRACE_MODE_READ_FSCREDS gate for stat address fields. */
static bool proc_stat_mm_visible(struct task *caller, struct task *target)
{
    if (!caller)
        return false;
    if (sched_task_tgid(caller) == sched_task_tgid(target))
        return true;
    bool privileged = (caller->cap_effective & (1ULL << CAP_SYS_PTRACE)) != 0;
    if (!privileged && (caller->fsuid != target->uid || caller->fsuid != target->euid ||
                        caller->fsuid != target->suid || caller->fsgid != target->gid ||
                        caller->fsgid != target->egid || caller->fsgid != target->sgid ||
                        sched_task_mm(target)->nondumpable))
        return false;
    return privileged || !(target->cap_permitted & ~caller->cap_effective);
}
/** @brief Emit Linux's ordered 52 stat fields using available task accounting.
 * Legacy private uid/role fields belong in status rather than Linux stat positions.
 */
static void proc_task_stat(struct task *task, struct text_stream *s)
{
    uint64_t values[53] = {0};
    struct task_address_space_state *mm = sched_task_mm(task);
    struct task_snapshot_info snapshots[SCHED_TASK_MAX];
    uint32_t count = sched_snapshot(snapshots, SCHED_TASK_MAX, 0), threads = 0;
    uint64_t ticks = 0;
    for (uint32_t i = 0; i < count; ++i) {
        struct task *other = sched_find(snapshots[i].pid);
        if (other && sched_task_tgid(other) == sched_task_tgid(task)) {
            ++threads;
            ticks += other->cpu_ticks;
        }
    }
    uint32_t foreground = 0;
    if (task->controlling_pty_id && pty_get_foreground_pgid(task->controlling_pty_id, &foreground) == 0)
        values[7] = (136u << 8) | (task->controlling_pty_id - 1);
    values[4] = task->parent_pid;
    values[5] = task->process_group;
    values[6] = task->process_session;
    values[8] = values[7] ? foreground : (uint64_t)-1;
    values[14] = ticks * 100 / NTCLKS_TICK_HZ;
    values[18] = 20 + task->priority;
    values[19] = task->priority;
    values[20] = threads;
    values[22] = task->start_uptime_ms / 10;
    for (uint32_t i = 0; i < SCHED_TASK_VMA_MAX + mm->vma_extra_count; ++i) {
        const struct task_vma *vma = sched_task_vma_at(task, i);
        if (vma && vma->used)
            values[23] += vma->end - vma->start;
    }
    values[24] = address_space_user_resident_kib(&mm->as) / 4;
    values[25] = UINT64_MAX;
    values[28] = task->stack_top;
    values[31] = sched_task_pending(task) & 0x7fffffff;
    values[32] = task->blocked_signals & 0x7fffffff;
    values[38] = task->parent_exit_signal;
    values[39] = task->last_cpu;
    values[47] = task->program_break_base;
    values[48] = mm->arg_start;
    values[49] = mm->arg_end;
    values[50] = mm->env_start;
    values[51] = mm->env_end;
    values[52] = task->state == TASK_EXITED
                     ? (task->exit_signal ? task->exit_signal & 0x7f : (task->exit_code & 0xff) << 8)
                     : 0;
    if (!proc_stat_mm_visible(sched_current_task(), task)) {
        values[28] = 0;
        for (uint32_t field = 45; field <= 52; ++field)
            values[field] = 0;
    }
    text_unsigned(s, task->pid);
    text_string(s, " (");
    const char *name = task->name ? task->name : "?";
    uint32_t length = 0;
    while (name[length] && length < 15)
        ++length;
    text_bytes(s, name, length);
    text_string(s, ") ");
    char state = proc_state(task);
    text_bytes(s, &state, 1);
    for (uint32_t field = 4; field <= 52; ++field) {
        text_string(s, " ");
        if (field == 8 || field == 18 || field == 19)
            text_signed(s, (int64_t)values[field]);
        else
            text_unsigned(s, values[field]);
    }
    text_string(s, "\n");
}

/** @brief Look up proc node types without following the last symbolic link.
 * @param path Absolute kernel pathname.
 * @param out Optional returned node.
 * @return Zero or negative ENOENT.
 */
int proc_lookup(const char *path, struct storage_node *out)
{
    char content[PROCFS_CONTENT_MAX];
    if (path && !__builtin_strncmp(path,"/sys",4) && (!path[4] || path[4]=='/')) return sysfs_lookup(path,out);
    if (!path || (path[0] != '/' || path[1] != 'p' || path[2] != 'r' ||
                  path[3] != 'o' || path[4] != 'c' || (path[5] && path[5] != '/'))) return -2;
    if (proc_text_eq(path, "/proc/mounts")) {
        if (out) *out = (struct storage_node){.type = LEONOS_FS_TYPE_SYMLINK,
            .flags = STORAGE_NODE_FLAG_PROC, .size = 11};
        return 0;
    }
    if (proc_text_eq(path, "/proc") || proc_text_eq(path, "/proc/sys") ||
        proc_text_eq(path, "/proc/sys/kernel")) {
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
    if (proc_mount_view(path) || proc_text_eq(path,"/proc/cpuinfo")) {
        if (out) *out = (struct storage_node){
            .type = LEONOS_FS_TYPE_FILE, .flags = STORAGE_NODE_FLAG_PROC,
            .first_cluster = 0x50524f43u, .size = 0,
        };
        return 0;
    }
    if (proc_text_eq(path, "/proc/uptime") ||
        proc_text_eq(path, "/proc/meminfo") ||
        proc_text_eq(path, "/proc/version") ||
        proc_text_eq(path, "/proc/filesystems") ||
        proc_text_eq(path, "/proc/sys/kernel/hostname") ||
        proc_text_eq(path, "/proc/sys/kernel/domainname") ||
        proc_text_eq(path, "/proc/sys/kernel/ostype") ||
        proc_text_eq(path, "/proc/sys/kernel/osrelease") ||
        proc_text_eq(path, "/proc/sys/kernel/version") ||
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
                .size = 0,
            };
        }
        return 0;
    }
    {
        const char *file = 0;
        uint32_t pid = 0;
        int kind = proc_path_kind(path, &file, &pid);
        if ((kind == 2 || kind == 3) && file &&
            (proc_text_eq(file, "exe") || proc_text_eq(file, "cwd") || proc_text_eq(file, "root"))) {
            if (kind == 3) pid = sched_current_pid();
            if (!pid || !sched_find(pid)) return -2;
            if (out) *out = (struct storage_node){.type = LEONOS_FS_TYPE_SYMLINK,
                .flags = STORAGE_NODE_FLAG_PROC};
            return 0;
        }
        if ((kind == 2 || kind == 3) && !file) {
            if (kind == 3) pid = sched_current_pid();
            if (!pid || !sched_find(pid)) return -2;
            if (out) *out = (struct storage_node){
                .type = kind == 3 ? LEONOS_FS_TYPE_SYMLINK : LEONOS_FS_TYPE_DIR,
                .flags = STORAGE_NODE_FLAG_PROC,
                .first_cluster = 0x50524f43u,
            };
            return 0;
        }
        if ((kind == 2 || kind == 3) && file &&
            (proc_text_eq(file, "stat") || proc_text_eq(file, "comm") || proc_text_eq(file, "cmdline") || proc_text_eq(file, "status"))) {
            if (kind == 2 && !sched_find(pid)) return -2;
            if (out) {
                *out = (struct storage_node){
                    .type = LEONOS_FS_TYPE_FILE,
                    .flags = STORAGE_NODE_FLAG_PROC,
                    .first_cluster = 0x50524f43u,
                    .volume_id = 0,
                    .size = 0,
                };
            }
            return 0;
        }
    }
    return -2;
}

/** @brief Read a bounded range from a current proc view.
 * @param path Resolved absolute kernel pathname.
 * @param offset Byte offset, including positions past EOF.
 * @param buffer Kernel destination, nullable for zero length.
 * @param length Maximum bytes to copy.
 * @param out_read Optional actual byte count.
 * @return Zero or negative errno, preserving mount backend failures.
 */
int proc_read(const char *path, uint64_t offset, void *buffer, uint32_t length,
              uint32_t *out_read)
{
    if (path && !__builtin_strncmp(path,"/sys",4) && (!path[4] || path[4]=='/')) return sysfs_read(path,offset,buffer,length,out_read);
    if (proc_text_eq(path,"/proc/cpuinfo")) return cpu_inventory_read(offset,buffer,length,out_read);
    if (out_read) *out_read = 0;
    if (!buffer && length) return -22;
    const char *file=0; uint32_t pid=0; int kind=proc_path_kind(path,&file,&pid);
    if(kind==3) pid=sched_current_pid();
    if((kind==2 || kind==3) && file && (proc_text_eq(file,"cmdline") || proc_text_eq(file,"stat") || proc_text_eq(file,"comm"))) {
        struct task *task=sched_find(pid); if(!task) return -2;
        struct text_stream stream={.offset=offset,.buffer=buffer,.capacity=length};
        int ret=0;
        if(proc_text_eq(file,"cmdline")) ret=proc_cmdline(task,&stream);
        else if(proc_text_eq(file,"stat")) proc_task_stat(task,&stream);
        else { const char *name=task->name?task->name:"?"; uint32_t n=0; while(name[n] && n<15) ++n;
            text_bytes(&stream,name,n); text_string(&stream,"\n"); }
        if(out_read) *out_read=stream.written; return ret;
    }
    int view = proc_mount_view(path);
    if (view == 1) return storage_read_mounts(offset, buffer, length, out_read);
    if (view == 2) return storage_read_mountinfo(offset, buffer, length, out_read);
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

/** @brief Read a proc link with truncation and no terminating NUL.
 * @param path Absolute kernel pathname whose final component is not followed.
 * @param buffer Kernel destination.
 * @param capacity Nonzero destination capacity.
 * @return Copied byte count, EINVAL for non-links, ENOENT, or EACCES on cross-task denial.
 */
int proc_readlink(const char *path, char *buffer, uint32_t capacity)
{
    const char *file = NULL;
    uint32_t pid = 0;
    struct task *caller = sched_current_task();
    struct task *task;
    uint32_t length = 0;
    if (!path || !buffer || !capacity) return -LEONOS_EINVAL;
    if (!__builtin_strncmp(path,"/sys",4) && (!path[4] || path[4]=='/')) return sysfs_readlink(path,buffer,capacity);
    if (proc_text_eq(path, "/proc/mounts")) {
        const char *target = "self/mounts";
        length = capacity < 11 ? capacity : 11;
        for (uint32_t i = 0; i < length; ++i) buffer[i] = target[i];
        return (int)length;
    }
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
    if (!file || (!proc_text_eq(file, "exe") && !proc_text_eq(file, "cwd") && !proc_text_eq(file, "root")))
        return proc_lookup(path, NULL) == 0 ? -LEONOS_EINVAL : -LEONOS_ENOENT;
    /* Match the available FSCREDS/dumpable policy. Capability namespaces and
     * executable inode tracking still need the common ptrace/VFS machinery. */
    if (!caller) return -LEONOS_EACCES;
    if (sched_task_tgid(caller) != sched_task_tgid(task) && caller->euid &&
        (caller->fsuid != task->uid || caller->fsuid != task->euid ||
         caller->fsuid != task->suid || caller->fsgid != task->gid ||
         caller->fsgid != task->egid || caller->fsgid != task->sgid ||
         sched_task_mm(task)->nondumpable)) return -LEONOS_EACCES;
    const char *target = proc_text_eq(file, "exe") ? task->path :
        proc_text_eq(file, "cwd") ? sched_task_cwd(task) : "/";
    if (!target[0]) return -LEONOS_ENOENT;
    while (length < capacity && target[length]) {
        buffer[length] = target[length];
        ++length;
    }
    /* readlink(2) does not append NUL and truncates at bufsiz. */
    return (int)length;
}

/** @brief Enumerate a proc directory with types matching lookup.
 * @param path Resolved proc directory pathname.
 * @param offset In/out enumeration position.
 * @param entry Writable returned directory entry.
 * @return One entry, zero at EOF, or negative errno.
 */
int proc_readdir(const char *path, uint64_t *offset, struct leonos_dir_entry *entry)
{
    uint32_t index;
    static const char *files[] = {"uptime", "meminfo", "version", "filesystems", "stat", "mounts", "self", "sys", "cpuinfo"};
    if (!path || !offset || !entry) return -22;
    if (!__builtin_strncmp(path,"/sys",4) && (!path[4] || path[4]=='/')) return sysfs_readdir(path,offset,entry);
    if (proc_text_eq(path, "/proc/sys") || proc_text_eq(path, "/proc/sys/kernel")) {
        static const char *values[] = {"hostname", "domainname", "ostype", "osrelease", "version"};
        int parent = proc_text_eq(path, "/proc/sys");
        if (*offset >= (parent ? 1 : sizeof(values) / sizeof(values[0]))) return 0;
        entry->type = parent ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
        proc_copy(entry->name, sizeof(entry->name), parent ? "kernel" : values[*offset]);
        ++*offset;
        return 1;
    }
    if (!proc_text_eq(path, "/proc")) {
        const char *file = NULL;
        uint32_t pid;
        int kind = proc_path_kind(path, &file, &pid);
        if (kind == 3) pid = sched_current_pid();
        if ((kind != 2 && kind != 3) || file) return -20;
        if (!sched_find(pid)) return -2;
        static const char *task_files[] = {"stat", "cmdline", "status", "mounts", "mountinfo", "exe", "cwd", "root", "comm"};
        if (*offset >= sizeof(task_files) / sizeof(task_files[0])) return 0;
        *entry = (struct leonos_dir_entry){.type = *offset >= 5 && *offset <= 7 ? LEONOS_FS_TYPE_SYMLINK : LEONOS_FS_TYPE_FILE};
        proc_copy(entry->name, sizeof(entry->name), task_files[(*offset)++]);
        return 1;
    }
    if (*offset > UINT32_MAX) return 0;
    index = (uint32_t)*offset;
    if (index < sizeof(files) / sizeof(files[0])) {
        entry->type = index == 5 || index == 6 ? LEONOS_FS_TYPE_SYMLINK :
            index == 7 ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
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
