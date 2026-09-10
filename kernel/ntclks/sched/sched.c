/*
 * LeonOS scheduler implementation: manages kernel and Ring-3 task state.
 * Selects runnable tasks, handles waits/exits, and switches address spaces.
 */
#include <ntclks/console.h>
#include <ntclks/arch.h>
#include <ntclks/paging.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/permissions.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/heap.h>
#include <ntclks/mm.h>
#include <ntclks/lock.h>
#include <ntclks/smp.h>
#include <ntclks/svga.h>
#include <ntclks/usercopy.h>
#include <linux/sched.h>
#include <ntclks/futex.h>
#include <ntclks/wait.h>
#include <ntclks/time.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/errno.h>

/* The table grows by moving only pointers; task objects keep stable addresses
 * because wait queues and interrupt paths may retain struct task pointers. */
static struct task **tasks;
static uint32_t task_count;
static uint32_t task_capacity;
static uint32_t next_pid = 1;
static uint32_t current_pid[SMP_MAX_CPUS];
static uint32_t next_session_id = 1;
static uint64_t scheduler_ticks;
static uint64_t scheduler_busy_ticks;
static uint64_t scheduler_idle_ticks;
static uint64_t scheduler_cpu_busy_ticks[SMP_MAX_CPUS];
static uint64_t scheduler_cpu_idle_ticks[SMP_MAX_CPUS];
static struct kernel_spinlock scheduler_lock = KERNEL_SPINLOCK_INIT;
static void sched_notify_parent_exit(struct task *task);

/* A READY task can still be owned by the CPU that just saved its interrupt
 * frame.  Keep that reservation until the owner either reclaims or replaces
 * it; otherwise another CPU can select the same address space concurrently. */
#define SCHED_CPU_NONE UINT32_MAX

static bool thread_group_pending(uint32_t tgid, const struct task *except)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        const struct task *task = tasks[i];
        if (task && task != except && task->pid && sched_task_tgid(task) == tgid &&
            (task->state != TASK_EXITED || task->running_cpu != SCHED_CPU_NONE)) return true;
    }
    return false;
}

static uint32_t scheduler_cpu_index(void)
{
    uint32_t cpu = smp_current_cpu();
    return cpu < SMP_MAX_CPUS ? cpu : 0;
}

uint64_t sched_all_cpu_mask(void)
{
    uint32_t count = smp_cpu_count();
    if (count == 0) {
        count = 1;
    }
    if (count >= 64u) {
        return UINT64_MAX;
    }
    return (1ULL << count) - 1ULL;
}

static bool task_cpu_allowed(const struct task *task, uint32_t cpu)
{
    uint64_t mask = task && task->affinity_mask ? task->affinity_mask : sched_all_cpu_mask();
    return cpu < 64u && (mask & (1ULL << cpu)) != 0;
}

static uint32_t scheduler_current_pid(void)
{
    return current_pid[scheduler_cpu_index()];
}

static bool task_frame_valid(const struct task *task, const struct trap_frame *frame)
{
    return task && frame && (*sched_task_as(task)).cr3 && task->stack_low && task->stack_top &&
           frame->rip >= NTCLKS_USER_BASE && frame->rip < NTCLKS_USER_TOP &&
           frame->rsp >= NTCLKS_USER_BASE && frame->rsp <= NTCLKS_USER_TOP &&
           frame->cs == NTCLKS_USER_CS && frame->ss == NTCLKS_USER_DS &&
           (frame->rflags & (1ULL << 9));
}

struct task_vma *sched_task_vma_at(struct task *task, uint32_t index)
{
    if (!task) {
        return NULL;
    }
    if (index < SCHED_TASK_VMA_MAX) {
        return &sched_task_mm(task)->vmas[index];
    }
    index -= SCHED_TASK_VMA_MAX;
    if (index >= sched_task_mm(task)->vma_extra_count) {
        uint32_t wanted = index + 1u;
        uint32_t capacity = sched_task_mm(task)->vma_extra_capacity ? sched_task_mm(task)->vma_extra_capacity : 16u;
        struct task_vma *replacement;
        while (capacity < wanted) {
            if (capacity > UINT32_MAX / 2u) {
                return NULL;
            }
            capacity *= 2u;
        }
        replacement = (struct task_vma *)kernel_malloc(
            (size_t)capacity * sizeof(*replacement));
        if (!replacement) {
            return NULL;
        }
        for (uint32_t i = 0; i < capacity; ++i) {
            replacement[i] = (struct task_vma){0};
        }
        for (uint32_t i = 0; i < sched_task_mm(task)->vma_extra_count; ++i) {
            replacement[i] = sched_task_mm(task)->vma_extra[i];
        }
        if (sched_task_mm(task)->vma_extra) {
            kernel_free(sched_task_mm(task)->vma_extra);
        }
        sched_task_mm(task)->vma_extra = replacement;
        sched_task_mm(task)->vma_extra_capacity = capacity;
        sched_task_mm(task)->vma_extra_count = wanted;
    }
    return &sched_task_mm(task)->vma_extra[index];
}

uint32_t sched_task_vma_capacity(const struct task *task)
{
    return task ? SCHED_TASK_VMA_MAX + sched_task_mm(task)->vma_extra_count : 0;
}

void sched_task_vma_release(struct task *task)
{
    if (!task || !sched_task_mm(task)->vma_extra) {
        return;
    }
    kernel_free(sched_task_mm(task)->vma_extra);
    sched_task_mm(task)->vma_extra = NULL;
    sched_task_mm(task)->vma_extra_count = 0;
    sched_task_mm(task)->vma_extra_capacity = 0;
}

struct task_file *sched_task_file_at(struct task *task, uint32_t index)
{
    if (!task) {
        return NULL;
    }
    if (index < SCHED_TASK_FILE_MAX) {
        return &sched_task_fds(task)->files[index];
    }
    index -= SCHED_TASK_FILE_MAX;
    if (index >= sched_task_fds(task)->file_extra_count) {
        uint32_t wanted = index + 1u;
        uint32_t capacity = sched_task_fds(task)->file_extra_capacity ? sched_task_fds(task)->file_extra_capacity : 16u;
        struct task_file *replacement;
        while (capacity < wanted) {
            if (capacity > UINT32_MAX / 2u) {
                return NULL;
            }
            capacity *= 2u;
        }
        replacement = (struct task_file *)kernel_malloc(
            (size_t)capacity * sizeof(*replacement));
        if (!replacement) {
            return NULL;
        }
        for (uint32_t i = 0; i < capacity; ++i) {
            replacement[i] = (struct task_file){0};
        }
        for (uint32_t i = 0; i < sched_task_fds(task)->file_extra_count; ++i) {
            replacement[i] = sched_task_fds(task)->file_extra[i];
        }
        if (sched_task_fds(task)->file_extra) {
            kernel_free(sched_task_fds(task)->file_extra);
        }
        sched_task_fds(task)->file_extra = replacement;
        sched_task_fds(task)->file_extra_capacity = capacity;
        sched_task_fds(task)->file_extra_count = wanted;
    }
    return &sched_task_fds(task)->file_extra[index];
}

uint32_t sched_task_file_capacity(const struct task *task)
{
    return task ? SCHED_TASK_FILE_MAX + sched_task_fds(task)->file_extra_count : 0;
}

void sched_task_file_release(struct task *task)
{
    if (!task || !sched_task_fds(task)->file_extra) {
        return;
    }
    kernel_free(sched_task_fds(task)->file_extra);
    sched_task_fds(task)->file_extra = NULL;
    sched_task_fds(task)->file_extra_count = 0;
    sched_task_fds(task)->file_extra_capacity = 0;
}

/**
 * @brief True when both strings are non-NULL and byte-for-byte identical.
 */
static int str_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

/**
 * @brief Copy name into the task's fixed buffer (bounded, NUL-terminated) and point task->name at it.
 */
static void task_copy_name(struct task *task, const char *name)
{
    size_t i = 0;
    if (!task) {
        return;
    }
    if (name) {
        while (i + 1 < sizeof(task->name_storage) && name[i]) {
            task->name_storage[i] = name[i];
            ++i;
        }
    }
    task->name_storage[i] = 0;
    task->name = task->name_storage;
}

/**
 * @brief Copy cwd into the task's fixed buffer, defaulting an empty or NULL value to "/".
 */
static void task_copy_cwd(struct task *task, const char *cwd)
{
    size_t i = 0;
    if (!task) {
        return;
    }
    if (!cwd || !cwd[0]) {
        cwd = "/";
    }
    while (i + 1 < LEONOS_FS_PATH_LEN && cwd[i]) {
        sched_task_cwd(task)[i] = cwd[i];
        ++i;
    }
    sched_task_cwd(task)[i] = 0;
}

/**
 * @brief Copy path into the task's fixed buffer, NUL-terminating even for a NULL source.
 */
static void task_copy_path(struct task *task, const char *path)
{
    size_t i = 0;
    if (!task) {
        return;
    }
    while (path && path[i] && i + 1 < sizeof(task->path)) {
        task->path[i] = path[i];
        ++i;
    }
    task->path[i] = 0;
}

/**
 * @brief Return the substring after the final '/', or "" for a NULL or slash-free path.
 */
static const char *task_path_basename(const char *path)
{
    const char *base = path;
    if (!path) {
        return "";
    }
    for (const char *p = path; *p; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    return base ? base : "";
}

/**
 * @brief Bounded, always-NUL-terminated copy used for username and home identity strings.
 */
static void task_copy_identity_text(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

/**
 * @brief Strip admin flag, uid, role, session, username, and home, leaving an anonymous task.
 */
static void task_clear_identity(struct task *task)
{
    if (!task) {
        return;
    }
    task->flags &= ~TASK_FLAG_ELEVATED_ADMIN;
    task_groups_release(task);
    task->uid = 0;
    task->gid = 0;
    task->euid = 0;
    task->egid = 0;
    task->suid = 0;
    task->sgid = 0;
    task->fsuid = 0;
    task->fsgid = 0;
    task->cap_effective = 0;
    task->cap_permitted = 0;
    task->cap_inheritable = 0;
    task->role = LEONOS_AUTH_ROLE_NONE;
    task->session_id = 0;
    task->username[0] = 0;
    task->home[0] = 0;
}

/**
 * @brief Inherit uid, role, session, username, and home from the parent task.
 */
static void task_copy_identity_from_parent(struct task *task, const struct task *parent)
{
    if (!task || !parent) {
        return;
    }
    task->uid = parent->uid;
    task->gid = parent->gid;
    task->euid = parent->euid;
    task->egid = parent->egid;
    task->suid = parent->suid;
    task->sgid = parent->sgid;
    task->fsuid = parent->fsuid;
    task->fsgid = parent->fsgid;
    task->cap_effective = parent->cap_effective;
    task->cap_permitted = parent->cap_permitted;
    task->cap_inheritable = parent->cap_inheritable;
    task->groups = parent->groups;
    task_groups_retain(task->groups);
    task->role = parent->role;
    task->session_id = parent->session_id;
    task_copy_identity_text(task->username, sizeof(task->username), parent->username);
    task_copy_identity_text(task->home, sizeof(task->home), parent->home);
}

/**
 * @brief Reset the task table, pid/session counters, and tick statistics to their initial state.
 */
void sched_init(void)
{
    kernel_spin_init(&scheduler_lock);
    tasks = NULL;
    task_count = 0;
    task_capacity = 0;
    next_pid = 1;
    for (uint32_t i = 0; i < SMP_MAX_CPUS; ++i) current_pid[i] = 0;
    next_session_id = 1;
    scheduler_ticks = 0;
    scheduler_busy_ticks = 0;
    scheduler_idle_ticks = 0;
    for (uint32_t i = 0; i < SMP_MAX_CPUS; ++i) {
        scheduler_cpu_busy_ticks[i] = 0;
        scheduler_cpu_idle_ticks[i] = 0;
    }
    console_printf("[ntclks] scheduler initialized\n");
}

/**
 * @brief Clear every byte of a task struct so it starts from a well-defined zero state.
 */
static void task_zero(struct task *task)
{
    if (!task) {
        return;
    }
    for (size_t i = 0; i < sizeof(*task); ++i) {
        ((uint8_t *)task)[i] = 0;
    }
}

/** @brief Drop the resource-limit reference only when a task is reaped/recycled. */
static void task_release_limits(struct task *task)
{
    struct task_rlimit_state *shared = task->shared_limits;
    if (!shared) return;
    task->limits = *shared;
    task->limits.references = 1;
    task->shared_limits = NULL;
    if (!--shared->references) kernel_free(shared);
}

/** @brief Initialize Linux v6.12 native x86-64 resource defaults. */
static void task_init_limits(struct task *task)
{
    /* Linux's non-KASAN x86-64 default: half of RAM / (16 KiB * 8),
     * with the same [20, FUTEX_TID_MASK] thread-count clamp. */
    uint64_t threads = mm_total_memory_kib() / 128;
    if (threads < 20) threads = 20;
    if (threads > 0x3fffffff) threads = 0x3fffffff;
    task->limits = (struct task_rlimit_state){.references = 1,
        .nofile = {SCHED_TASK_FILE_LIMIT, SCHED_NR_OPEN},
        .sigpending = {threads / 2, threads / 2},
        .as = {LINUX_RLIM_INFINITY, LINUX_RLIM_INFINITY},
        /* include/uapi/linux/resource.h:_STK_LIM and INIT_RLIMITS. */
        .stack = {8ULL * 1024ULL * 1024ULL, LINUX_RLIM_INFINITY}};
}

/**
 * @brief Scan forward from next_pid for an unused non-zero pid; 0 when the identifier space is exhausted.
 */
static uint32_t task_allocate_pid(void)
{
    for (uint64_t attempt = 0; attempt < UINT32_MAX; ++attempt) {
        uint32_t candidate = next_pid++;
        uint32_t used = 0;
        if (candidate == 0) {
            continue;
        }
        for (uint32_t i = 0; i < task_count; ++i) {
            if (tasks[i] && tasks[i]->pid == candidate) {
                used = 1;
                break;
            }
        }
        if (!used) {
            return candidate;
        }
    }
    return 0;
}

/**
 * @brief Return a zeroed task slot, recycling a reaped EXITED entry or growing the table as needed.
 */
static struct task *alloc_task_slot(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i] && tasks[i]->state == TASK_EXITED &&
            tasks[i]->running_cpu == SCHED_CPU_NONE &&
            (tasks[i]->flags & TASK_FLAG_RESOURCES_RELEASED) &&
            !thread_group_pending(sched_task_tgid(tasks[i]), tasks[i]) &&
            (!(tasks[i]->flags & TASK_FLAG_WAITABLE_CHILD) || tasks[i]->parent_pid == 0)) {
            task_release_limits(tasks[i]);
            task_zero(tasks[i]);
            return tasks[i];
        }
    }

    if (task_count == task_capacity) {
        uint32_t new_capacity = task_capacity ? task_capacity * 2u : SCHED_TASK_MAX;
        struct task **replacement;
        if (new_capacity < task_capacity || new_capacity > UINT32_MAX / sizeof(*tasks)) {
            return NULL;
        }
        replacement = (struct task **)kernel_malloc(
            (size_t)new_capacity * sizeof(*replacement));
        if (!replacement) {
            return NULL;
        }
        for (uint32_t i = 0; i < new_capacity; ++i) {
            replacement[i] = i < task_count ? tasks[i] : NULL;
        }
        if (tasks) {
            kernel_free(tasks);
        }
        tasks = replacement;
        task_capacity = new_capacity;
    }

    tasks[task_count] = (struct task *)kernel_malloc(sizeof(struct task));
    if (!tasks[task_count]) {
        return NULL;
    }
    task_zero(tasks[task_count]);
    return tasks[task_count++];
}

/**
 * @brief Allocate and initialize a kernel task with an entry point, no stack, and cleared identity.
 */
uint32_t sched_create_kernel_task(const char *name, uint64_t entry)
{
    uint64_t lock_flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    struct task *task = alloc_task_slot();
    uint32_t pid;
    if (!task) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return 0;
    }
    pid = task_allocate_pid();
    if (!pid) {
        task_zero(task);
        task->state = TASK_EXITED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return 0;
    }
    task_zero(task);
    task->pid = pid;
    task->start_uptime_ms = time_uptime_ms();
    task->parent_pid = 0;
    task->process_group = 0;
    task->process_session = 0;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = 0;
    task->stack_low = 0;
    task->wake_tick = 0;
    task->priority = 0;
    task->affinity_mask = sched_all_cpu_mask();
    task->pending_signals = 0;
    task->timer_pending_signals = 0;
    task_init_limits(task);
    (*sched_task_umask(task)) = 022;
    task->wait_window_id = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    for (size_t i = 0; i < sizeof((*sched_task_as(task))); ++i) {
        ((uint8_t *)sched_task_as(task))[i] = 0;
    }
    for (size_t i = 0; i < sizeof(task->frame); ++i) {
        ((uint8_t *)&task->frame)[i] = 0;
    }
    task->state = TASK_READY;
    task->running_cpu = SCHED_CPU_NONE;
    task->kind = TASK_KIND_KERNEL;
    task->flags = 0;
    task->pty_id = 0;
    task_clear_identity(task);
    task_copy_cwd(task, "/");
    console_printf("[ntclks] task pid=%u name=%s entry=0x%llx\n",
                   task->pid, task->name, (unsigned long long)task->entry);
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return task->pid;
}

/**
 * @brief Allocate a user task, build its address space and stack, and inherit cwd/identity from parent.
 */
uint32_t sched_create_user_task(const char *name, uint64_t entry, uint64_t stack_top,
                                uint32_t parent_pid, uint32_t flags)
{
    uint64_t lock_flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    struct task *task = alloc_task_slot();
    uint32_t pid;
    if (!task) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return 0;
    }
    pid = task_allocate_pid();
    if (!pid) {
        task_zero(task);
        task->state = TASK_EXITED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return 0;
    }
    task_zero(task);
    task->pid = pid;
    task->start_uptime_ms = time_uptime_ms();
    task->parent_pid = parent_pid;
    task->parent_exit_signal = 17;
    task->process_group = task->pid;
    task->process_session = task->pid;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = stack_top;
    task->stack_low = stack_top >= (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL ?
                      stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * 4096ULL : 0;
    task->wake_tick = 0;
    task->priority = 0;
    task->affinity_mask = sched_all_cpu_mask();
    task->pending_signals = 0;
    task->timer_pending_signals = 0;
    task_init_limits(task);
    (*sched_task_umask(task)) = 022;
    task->wait_window_id = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    if (!address_space_create(sched_task_as(task)) ||
        !address_space_map_user_stack(sched_task_as(task), stack_top)) {
        address_space_destroy(sched_task_as(task));
        task_copy_name(task, "failed");
        task->entry = 0;
        task->stack_top = 0;
        task->image = NULL;
        task->image_len = 0;
        task->state = TASK_EXITED;
        task->flags = TASK_FLAG_RESOURCES_RELEASED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return 0;
    }
    task->frame.rip = entry;
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.rflags = 0x202;
    task->frame.rsp = stack_top;
    task->frame.ss = NTCLKS_USER_DS;
    arch_fpu_task_init(task->fpu_state);
    /* Keep a newly allocated user task off the run queue until its creator
     * has attached the executable metadata and launch arguments. */
    task->state = TASK_BLOCKED;
    task->running_cpu = SCHED_CPU_NONE;
    task->kind = TASK_KIND_USER;
    task->flags = flags;
    task_clear_identity(task);
    task_copy_cwd(task, "/");
    if (parent_pid &&
        (!(flags & TASK_FLAG_SERVICE) || (flags & TASK_FLAG_WINDOW_SERVER))) {
        struct task *parent = sched_find(parent_pid);
        if (parent) {
            task_copy_cwd(task, sched_task_cwd(parent));
            task_copy_identity_from_parent(task, parent);
            task->priority = parent->priority;
            task->limits = *sched_task_limits(parent);
            task->limits.references = 1;
            (*sched_task_umask(task)) = (*sched_task_umask(parent));
            task->process_group = parent->process_group;
            task->process_session = parent->process_session;
            task->controlling_pty_id = parent->controlling_pty_id;
            task->affinity_mask = parent->affinity_mask;
        }
    }
    console_printf("[ntclks] task pid=%u ppid=%u name=%s user entry=0x%llx stack=0x%llx flags=0x%x\n",
                   task->pid,
                   task->parent_pid,
                   task->name,
                   (unsigned long long)task->entry,
                   (unsigned long long)task->stack_top,
                   task->flags);
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return task->pid;
}

/**
 * @brief Fork the calling user task: clone its address space COW-style, copy its register frame with rax zeroed in the child, and copy descriptors; returns the child pid or a negative errno.
 */
static int task_promote_shared(struct task *task, uint64_t flags)
{
    if ((flags & CLONE_THREAD) && !task->shared_limits) {
        struct task_rlimit_state *limits = kernel_malloc(sizeof(*limits));
        if (!limits) return -12;
        *limits = task->limits;
        limits->references = 1;
        task->shared_limits = limits;
    }
    if ((flags & CLONE_VM) && !task->shared_mm) {
        struct task_address_space_state *mm = kernel_malloc(sizeof(*mm));
        if (!mm) return -12;
        *mm = task->address_space;
        mm->references = 1;
        if (!mm->initial_stack_top) mm->initial_stack_top = task->stack_top;
        if (!mm->initial_stack_low) mm->initial_stack_low = task->stack_low;
        task->shared_mm = mm;
    }
    if ((flags & CLONE_FILES) && !task->shared_files) {
        struct task_fd_table_state *files = kernel_malloc(sizeof(*files));
        if (!files) return -12;
        *files = task->fd_table;
        files->references = 1;
        task->shared_files = files;
    }
    if ((flags & CLONE_FS) && !task->shared_fs) {
        struct task_fs_state *fs = kernel_malloc(sizeof(*fs));
        if (!fs) return -12;
        fs->umask = *sched_task_umask(task);
        for (unsigned i = 0; i < sizeof(fs->cwd); ++i) fs->cwd[i] = sched_task_cwd(task)[i];
        fs->references = 1;
        task->shared_fs = fs;
    }
    if ((flags & CLONE_SIGHAND) && !task->shared_sighand) {
        struct task_sighand_state *handlers = kernel_malloc(sizeof(*handlers));
        if (!handlers) return -12;
        for (unsigned i = 0; i < KERNEL_SIGNAL_ACTION_MAX; ++i)
            handlers->actions[i] = task->signal_actions[i];
        handlers->references = 1;
        task->shared_sighand = handlers;
    }
    return 0;
}

/**
 * @brief Finish a child-side CLONE_VFORK wait under scheduler_lock.
 *
 * Linux completes tsk->vfork_done from mm_release() on both the exec and the
 * final exit path.  The parent is woken only from TASK_BLOCKED; a parent that
 * was concurrently killed or stopped keeps its terminal/job-control state and
 * simply loses the stale child pointer.
 */
static void vfork_child_done_locked(struct task *child)
{
    struct task *parent = child ? child->vfork_parent : NULL;
    if (!parent) return;
    child->vfork_parent = NULL;
    if (parent->vfork_child == child) parent->vfork_child = NULL;
    if (parent->state == TASK_BLOCKED) {
        parent->wake_tick = 0;
        parent->wait_window_id = 0;
        parent->state = TASK_READY;
    }
}

/** @brief Drop a dying parent's reference to its vfork child under lock. */
static void vfork_parent_exit_locked(struct task *parent)
{
    struct task *child = parent ? parent->vfork_child : NULL;
    if (!child) return;
    child->vfork_parent = NULL;
    parent->vfork_child = NULL;
}

/**
 * @brief Public non-locking wrapper used by exec/exit lifecycle code.
 *
 * Called with the scheduler unlocked after the child is committed to exec or
 * terminally exiting.  Keep the link cleared even when the parent already
 * died: a later task slot reuse must never observe a stale parent pointer.
 */
void sched_vfork_child_done(struct task *child)
{
    if (!child) return;
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    vfork_child_done_locked(child);
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

int64_t sched_clone_current(const struct trap_frame *parent_frame, uint64_t flags,
                           uint64_t stack, uint64_t parent_tid,
                           uint64_t child_tid, uint64_t tls)
{
    const uint64_t supported = CSIGNAL | CLONE_VM | CLONE_VFORK | CLONE_FS |
        CLONE_FILES | CLONE_SIGHAND | CLONE_PARENT | CLONE_THREAD | CLONE_SYSVSEM |
        CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID |
        CLONE_DETACHED | CLONE_UNTRACED | CLONE_CHILD_SETTID | CLONE_CLEAR_SIGHAND;
    if ((flags & CLONE_CLEAR_SIGHAND) && (flags & CLONE_SIGHAND)) return -22;
    if ((flags & CLONE_THREAD) && !(flags & CLONE_SIGHAND)) return -22;
    if ((flags & CLONE_SIGHAND) && !(flags & CLONE_VM)) return -22;
    if (flags & ~supported) return -LEONOS_ENOSYS;
    if ((flags & CLONE_SETTLS) && tls >= NTCLKS_USER_TLS_LIMIT) return -LEONOS_EPERM;
    if ((flags & CLONE_PARENT_SETTID) && !user_range_writable(parent_tid, 4)) return -14;
    if ((flags & CLONE_CHILD_SETTID) && !user_range_writable(child_tid, 4)) return -14;
    if (stack && (stack < NTCLKS_USER_BASE || stack >= NTCLKS_USER_TOP)) return -14;
    uint64_t lock_flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    struct task *parent = sched_current_task();
    struct task *child;
    uint32_t child_pid;
    bool files_retained = false;
    if (!parent || !parent_frame || parent->kind != TASK_KIND_USER ||
        parent->state == TASK_EXITED || !(*sched_task_as(parent)).cr3) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -22;
    }
    if (task_promote_shared(parent, flags) < 0) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -12;
    }
    child = alloc_task_slot();
    if (!child) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -12;
    }
    child_pid = task_allocate_pid();
    if (!child_pid) {
        task_zero(child);
        child->state = TASK_EXITED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -12;
    }
    task_zero(child);
    *child = *parent;
    child->limits = *sched_task_limits(parent);
    child->limits.references = 1;
    child->shared_limits = NULL;
    child->shared_mm = NULL;
    child->vfork_parent = NULL;
    child->vfork_child = NULL;
    child->shared_files = NULL;
    child->shared_fs = NULL;
    child->shared_sighand = NULL;
    child->address_space = *sched_task_mm(parent);
    child->address_space.as = (struct address_space){0};
    child->address_space.vma_extra = NULL;
    child->address_space.vma_extra_count = 0;
    child->address_space.vma_extra_capacity = 0;
    child->fd_table = *sched_task_fds(parent);
    child->fd_table.file_extra = NULL;
    child->fd_table.file_extra_count = 0;
    child->fd_table.file_extra_capacity = 0;
    task_copy_cwd(child, sched_task_cwd(parent));
    *sched_task_umask(child) = *sched_task_umask(parent);
    for (unsigned i = 0; i < KERNEL_SIGNAL_ACTION_MAX; ++i)
        child->signal_actions[i] = sched_task_actions(parent)[i];
    if (flags & CLONE_CLEAR_SIGHAND) {
        for (unsigned i = 1; i < KERNEL_SIGNAL_ACTION_MAX; ++i) {
            if (child->signal_actions[i].handler != 1)
                child->signal_actions[i] = (struct kernel_signal_action){0};
        }
    }
    child->pid = child_pid;
    child->start_uptime_ms = time_uptime_ms();
    child->tgid = (flags & CLONE_THREAD) ? sched_task_tgid(parent) : child_pid;
    child->process_pending_signals = 0;
    child->signal_queue = (struct kernel_sigqueue){0};
    child->process_signal_queue = (struct kernel_sigqueue){0};
    child->shared_process_signal_queue = flags & CLONE_THREAD ?
        sched_task_process_signal_queue(parent) : NULL;
    child->alarm_deadline = 0;
    child->alarm_interval_ns = child->alarm_last_expiry = 0;
    /* The leader's task storage survives until the last group member exits. */
    child->shared_process_pending = flags & CLONE_THREAD ? sched_task_process_pending(parent) : NULL;
    child->parent_pid = flags & (CLONE_THREAD | CLONE_PARENT)
        ? parent->parent_pid : sched_task_tgid(parent);
    child->parent_exit_signal = flags & CLONE_THREAD ? 0 : flags & CLONE_PARENT
        ? sched_find(sched_task_tgid(parent))->parent_exit_signal : flags & CSIGNAL;
    child->parent_exit_notified = 0;
    child->exit_signal = 0;
    child->clear_child_tid = (flags & CLONE_CHILD_CLEARTID) ? child_tid : 0;
    child->robust_list = 0;
    child->rseq_area = 0;
    child->rseq_len = 0;
    child->rseq_sig = 0;
    child->restart_syscall = 0;
    child->nanosleep_deadline = child->nanosleep_remaining = 0;
    child->sigwait_deadline = child->sigwait_mask = 0;
    child->signalfd_waiting = false;
    child->signalfd_wait_mask = 0;
    child->signalfd_vectors = NULL;
    child->signalfd_vector_bytes = 0;
    child->signalfd_vector_count = child->signalfd_read_flags = 0;
    child->sigwait_active = 0;
    /* POSIX timers are process objects; a forked child starts with none and
     * a CLONE_THREAD child resolves timer operations through its leader. */
    for (uint32_t i = 0; i < SCHED_TASK_TIMER_MAX; ++i)
        child->timers[i] = (struct task_posix_timer){0};
    child->waiting_queue = NULL;
    child->socket_receive_file = NULL;
    child->socket_receive_done = 0;
    child->socket_receive_message = child->socket_receive_control_capacity = 0;
    child->socket_receive_name_capacity = 0;
    child->socket_receive_flags = child->socket_receive_path_length = 0;
    child->socket_receive_name = 0;
    child->mmsg = (struct task_mmsg_state){0};
    child->sysv_msg = (struct task_sysv_msg_state){0};
    child->sysv_sem = (struct task_sysv_sem_state){0};
    child->sysv_undo = NULL;
    child->syscall_file = NULL;
    child->socket_io_deadline = 0;
    child->socket_io_timed = false;
    if ((flags & (CLONE_VM | CLONE_VFORK)) == CLONE_VM) {
        child->signal_stack_base = 0;
        child->signal_stack_size = 0;
        child->signal_stack_flags = 0;
    }
    child->futex_next = NULL;
    child->futex_state = 0;
    child->futex_waitv_count = 0;
    child->futex_waitv_index = 0;
    child->futex_address = 0;
    if (flags & CLONE_SETTLS) child->fs_base = tls;
    child->image = NULL;
    child->image_len = 0;
    /**
 * @brief Service and window-server authority belongs to the launched image, not to an arbitrary child created by that process.
 */
    child->flags &= ~(TASK_FLAG_RESOURCES_RELEASED | TASK_FLAG_PENDING_LOAD |
                      TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER);
    child->flags |= TASK_FLAG_STARTED;
    if (flags & CLONE_THREAD) child->flags &= ~TASK_FLAG_WAITABLE_CHILD;
    else child->flags |= TASK_FLAG_WAITABLE_CHILD;
    child->state = TASK_BLOCKED;
    child->running_cpu = SCHED_CPU_NONE;
    child->wake_tick = 0;
    child->poll_deadline_ticks = 0;
    child->wait_window_id = 0;
    child->exit_code = 0;
    child->cpu_ticks = 0;
    child->pending_signals = 0;
    child->timer_pending_signals = 0;
    child->frame = *parent_frame;
    child->frame.rax = 0;
    if (stack) {
        child->frame.rsp = stack;
        child->stack_low = stack & ~4095ULL;
        child->stack_top = (stack + 4095) & ~4095ULL;
        for (uint32_t i = 0; i < sched_task_vma_capacity(parent); ++i) {
            struct task_vma *vma = sched_task_vma_at(parent, i);
            if (vma && vma->used && stack > vma->start && stack <= vma->end) {
                child->stack_low = vma->start;
                child->stack_top = vma->end;
                break;
            }
        }
    }
    arch_fpu_save(child->fpu_state);
    task_copy_name(child, parent->name);
    if (flags & CLONE_VM) {
        child->shared_mm = parent->shared_mm;
        ++child->shared_mm->references;
    } else {
        if (!address_space_clone_cow(sched_task_as(parent), sched_task_as(child))) goto fail;
        for (uint32_t i = 0; i < sched_task_mm(parent)->vma_extra_count; ++i) {
            struct task_vma *dst = sched_task_vma_at(child, SCHED_TASK_VMA_MAX + i);
            if (!dst) goto fail;
            *dst = sched_task_mm(parent)->vma_extra[i];
        }
    }
    if (flags & CLONE_FILES) {
        child->shared_files = parent->shared_files;
        ++child->shared_files->references;
    } else {
        for (uint32_t i = 0; i < sched_task_fds(parent)->file_extra_count; ++i) {
            struct task_file *dst = sched_task_file_at(child, SCHED_TASK_FILE_MAX + i);
            if (!dst) goto fail;
            *dst = sched_task_fds(parent)->file_extra[i];
        }
        if (syscall_clone_task_files(parent, child) < 0) goto fail;
        files_retained = true;
    }
    if (task_sysv_sem_clone(parent, child, flags) < 0) goto fail;
    if (flags & CLONE_FS) {
        child->shared_fs = parent->shared_fs;
        ++child->shared_fs->references;
    }
    if (flags & CLONE_SIGHAND) {
        child->shared_sighand = parent->shared_sighand;
        ++child->shared_sighand->references;
    }
    if (flags & CLONE_THREAD) {
        child->shared_limits = parent->shared_limits;
        ++child->shared_limits->references;
    }
    /**
 * @brief exec_argv and exec_envp are interior pointers, so rebuild them to reference the child-owned packed string storage after the structure copy.
 */
    sched_set_task_exec_params(child_pid, parent->exec_argc, parent->exec_argv,
                               parent->exec_envc, parent->exec_envp,
                               parent->exec_data, parent->exec_data_len);
    task_groups_retain(child->groups);
    if (flags & CLONE_PARENT_SETTID) *(uint32_t *)(uintptr_t)parent_tid = child_pid;
    if (flags & CLONE_CHILD_SETTID) {
        if (!address_space_user_page_writable(sched_task_as(child), child_tid))
            address_space_handle_cow_fault(sched_task_as(child), child_tid);
        uint64_t phys = address_space_user_page_phys(sched_task_as(child), child_tid);
        *(uint32_t *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + (child_tid & 4095)) = child_pid;
    }
    if (flags & CLONE_VFORK) {
        /* Publish the parent/child relation while still holding
         * scheduler_lock.  The child cannot run and complete before the lock
         * is dropped, so the parent either observes a real completion or is
         * safely put to sleep by this transaction. */
        child->vfork_parent = parent;
        parent->vfork_child = child;
        parent->wake_tick = 0;
        parent->wait_window_id = 0;
        parent->state = TASK_BLOCKED;
    }
    child->state = TASK_READY;
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return (int64_t)child_pid;

fail:
    if (child->shared_mm) --child->shared_mm->references;
    else {
        address_space_destroy(sched_task_as(child));
        sched_task_vma_release(child);
    }
    if (child->shared_files) --child->shared_files->references;
    else {
        if (files_retained) syscall_release_task_files(child);
        sched_task_file_release(child);
    }
    task_zero(child);
    child->state = TASK_EXITED;
    child->flags = TASK_FLAG_RESOURCES_RELEASED;
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return -12;
}

int64_t sched_fork_current(const struct trap_frame *frame)
{
    return sched_clone_current(frame, 17, 0, 0, 0, 0);
}

int64_t sched_vfork_current(const struct trap_frame *frame)
{
    /* kernel/fork.c:SYSCALL_DEFINE0(vfork): exit_signal = SIGCHLD (17). */
    return sched_clone_current(frame, CLONE_VM | CLONE_VFORK | 17,
                               0, 0, 0, 0);
}

/**
 * @brief Attach an in-memory image and its length to the task with the given pid.
 */
void sched_set_task_image(uint32_t pid, const void *image, size_t image_len)
{
    struct task *task = sched_find(pid);
    if (!task) {
        return;
    }
    task->image = image;
    task->image_len = image_len;
}

/**
 * @brief Record the storage node backing a task's image and flag it for a lazy load.
 */
void sched_set_task_image_node(uint32_t pid, const struct storage_node *node)
{
    struct task *task = sched_find(pid);
    if (!task) {
        return;
    }
    task->image_node = node ? *node : (struct storage_node){0};
    task->flags |= TASK_FLAG_PENDING_LOAD;
}

/**
 * @brief Copy path into the task's fixed path field, resolving the task by pid.
 */
void sched_set_task_path(uint32_t pid, const char *path)
{
    task_copy_path(sched_find(pid), path);
}

/**
 * @brief Store clamped argc/argv/envc/envp/data, rebinding interior pointers into the owned buffer.
 */
void sched_set_task_exec_params(uint32_t pid,
                                uint32_t argc, char *const argv[],
                                uint32_t envc, char *const envp[],
                                const char *data, uint32_t data_len)
{
    struct task *task = sched_find(pid);
    uintptr_t src_base;
    uintptr_t src_end;
    if (!task) {
        return;
    }
    if (argc > SCHED_EXEC_ARG_MAX) {
        argc = SCHED_EXEC_ARG_MAX;
    }
    if (envc > SCHED_EXEC_ENV_MAX) {
        envc = SCHED_EXEC_ENV_MAX;
    }
    if (data_len > sizeof(task->exec_data)) {
        data_len = sizeof(task->exec_data);
    }
    task->exec_argc = argc;
    task->exec_envc = envc;
    task->exec_data_len = data_len;
    for (uint32_t i = 0; i < SCHED_EXEC_ARG_MAX + 1; ++i) {
        task->exec_argv[i] = 0;
    }
    for (uint32_t i = 0; i < SCHED_EXEC_ENV_MAX + 1; ++i) {
        task->exec_envp[i] = 0;
    }
    for (uint32_t i = 0; i < data_len; ++i) {
        task->exec_data[i] = data ? data[i] : 0;
    }
    for (uint32_t i = data_len; i < sizeof(task->exec_data); ++i) {
        task->exec_data[i] = 0;
    }
    src_base = (uintptr_t)data;
    src_end = src_base + data_len;
    for (uint32_t i = 0; i < argc; ++i) {
        task->exec_argv[i] = 0;
        if (!argv || !argv[i]) {
            continue;
        }
        uintptr_t ptr = (uintptr_t)argv[i];
        if (ptr >= src_base && ptr < src_end) {
            task->exec_argv[i] = task->exec_data + (ptr - src_base);
        }
    }
    for (uint32_t i = 0; i < envc; ++i) {
        task->exec_envp[i] = 0;
        if (!envp || !envp[i]) {
            continue;
        }
        uintptr_t ptr = (uintptr_t)envp[i];
        if (ptr >= src_base && ptr < src_end) {
            task->exec_envp[i] = task->exec_data + (ptr - src_base);
        }
    }
}

/**
 * @brief Create the pid-0 kernel idle task and mark it the running task.
 */
void sched_create_idle_task(void)
{
    struct task *task = alloc_task_slot();
    if (!task) {
        return;
    }
    task_zero(task);
    task->pid = 0;
    task->parent_pid = 0;
    task->process_group = 0;
    task->process_session = 0;
    task_copy_name(task, "idle");
    task->entry = 0;
    task->stack_top = 0;
    task->stack_low = 0;
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    for (size_t i = 0; i < sizeof((*sched_task_as(task))); ++i) {
        ((uint8_t *)sched_task_as(task))[i] = 0;
    }
    for (size_t i = 0; i < sizeof(task->frame); ++i) {
        ((uint8_t *)&task->frame)[i] = 0;
    }
    task->state = TASK_RUNNING;
    task->kind = TASK_KIND_KERNEL;
    task->flags = 0;
    task->pty_id = 0;
    task_clear_identity(task);
    task_copy_cwd(task, "/");
}

/**
 * @brief Make pid the running task and demote every other RUNNING task back to READY.
 */
void sched_set_running(uint32_t pid)
{
    uint64_t flags;
    uint32_t cpu = scheduler_cpu_index();
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *selected = NULL;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid) {
            selected = tasks[i];
            break;
        }
    }
    if (!selected || selected->kind != TASK_KIND_USER ||
        selected->state == TASK_EXITED || !(*sched_task_as(selected)).cr3 ||
        !selected->frame.rip || (selected->frame.cs & 3ULL) != 3ULL ||
        (selected->frame.ss & 3ULL) != 3ULL ||
        !(selected->frame.rflags & (1ULL << 9))) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    current_pid[cpu] = pid;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i] == selected) {
            tasks[i]->state = TASK_RUNNING;
            tasks[i]->running_cpu = cpu;
            tasks[i]->last_cpu = cpu;
        } else if (tasks[i]->state == TASK_RUNNING && tasks[i]->running_cpu == cpu) {
            tasks[i]->state = TASK_READY;
            tasks[i]->running_cpu = SCHED_CPU_NONE;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Close the task's files, mark it exited with code, reparent its children, and clear current.
 */
void sched_exit(uint32_t pid, uint64_t code)
{
    uint64_t flags;
    bool gpu_owner_quiescent = false;
    struct task *exiting = NULL;

    /* Publish the terminal state atomically, but preserve running_cpu until
     * its owner reaches a scheduling boundary.  A remote CPU may still be
     * executing this task's user frame when a terminal/PTY destroys it. */
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid) {
            exiting = tasks[i];
            tasks[i]->state = TASK_EXITED;
            tasks[i]->exit_code = code;
            tasks[i]->child_event = TASK_CHILD_EVENT_NONE;
            tasks[i]->stop_signal = 0;
            gpu_owner_quiescent = tasks[i]->running_cpu == SCHED_CPU_NONE;
            console_printf("[ntclks] scheduler task exited pid=%u name=%s code=%llu\n",
                           pid,
                           tasks[i]->name,
                           (unsigned long long)code);
            break;
        }
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (exiting && !thread_group_pending(sched_task_tgid(exiting), exiting) &&
            tasks[i]->parent_pid == sched_task_tgid(exiting)) {
            tasks[i]->parent_pid = 0;
            tasks[i]->flags &= ~TASK_FLAG_WAITABLE_CHILD;
        }
    }
    if (exiting) {
        /* Detach a dying parent before its task slot can be reused. */
        vfork_parent_exit_locked(exiting);
        /* A remote child may still execute its user frame. Complete only
         * after retirement and the mm_release futex work below. */
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    if (gpu_owner_quiescent) {
        uint64_t execution_flags;
        kernel_execution_lock_irqsave(&execution_flags);
        sched_release_task_resources(exiting);
        kernel_execution_unlock_irqrestore(execution_flags);
    }
}

/**
 * @brief Release private queued signals and shared signals only after the last thread retires.
 * @param task Exited, quiescent task under the kernel execution lock.
 */
static void sched_release_task_signals(struct task *task)
{
    kernel_signal_flush(task, false, UINT64_MAX);
    if (!thread_group_pending(sched_task_tgid(task), task))
        kernel_signal_flush(task, true, UINT64_MAX);
}

/**
 * @brief Free a user task's storage, files, and address space once, guarded by a released flag.
 */
void sched_release_task_resources(struct task *task)
{
    if (!task || task->kind != TASK_KIND_USER ||
        task->running_cpu != SCHED_CPU_NONE ||
        (task->flags & TASK_FLAG_RESOURCES_RELEASED)) {
        return;
    }
    storage_drain_task_io(task->pid);
    if (task->waiting_queue) kernel_wait_queue_remove(task->waiting_queue, task);
    task_socket_cancel_receive(task);
    task_release_syscall_file(task);
    task_sysv_sem_exit(task);
    futex_task_exit(task);
    /* Linux mm_release clears child_tid and releases robust futexes before
     * vfork completion. This task can no longer touch the parent's memory. */
    if (task->vfork_parent || task->vfork_child) {
        uint64_t vfork_flags;
        kernel_spin_lock_irqsave(&scheduler_lock, &vfork_flags);
        vfork_parent_exit_locked(task);
        vfork_child_done_locked(task);
        kernel_spin_unlock_irqrestore(&scheduler_lock, vfork_flags);
    }
    sched_release_task_signals(task);
    bool last_files = !task->shared_files ||
        __atomic_sub_fetch(&task->shared_files->references, 1, __ATOMIC_SEQ_CST) == 0;
    if (last_files) {
        syscall_release_task_files(task);
        if (task->shared_files) kernel_free(task->shared_files);
    }
    task->shared_files = NULL;
    task_groups_release(task);
    if (!task->shared_mm || __atomic_sub_fetch(&task->shared_mm->references, 1, __ATOMIC_SEQ_CST) == 0) {
        address_space_destroy(sched_task_as(task));
        sched_task_vma_release(task);
        if (task->shared_mm) kernel_free(task->shared_mm);
    }
    task->shared_mm = NULL;
    task->address_space = (struct task_address_space_state){0};
    task->fd_table = (struct task_fd_table_state){0};
    if (task->shared_fs && __atomic_sub_fetch(&task->shared_fs->references, 1, __ATOMIC_SEQ_CST) == 0)
        kernel_free(task->shared_fs);
    task->shared_fs = NULL;
    if (task->shared_sighand && __atomic_sub_fetch(&task->shared_sighand->references, 1, __ATOMIC_SEQ_CST) == 0)
        kernel_free(task->shared_sighand);
    task->shared_sighand = NULL;
    svga_gpu_release_owner(task->pid);
    task->flags |= TASK_FLAG_RESOURCES_RELEASED;
    /* Signal/fault exits also retire private PTY owners. Publish cleanup first:
     * hangup can recursively terminate other members of the foreground group. */
    if (last_files) pty_process_exit(task->pid);
    sched_notify_parent_exit(task);
}

void sched_exit_group(uint32_t tgid, uint64_t code)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task && sched_task_tgid(task) == tgid && task->state != TASK_EXITED)
            sched_exit(task->pid, code);
        else if (task && task->pid == tgid) task->exit_code = code;
    }
}

int sched_prepare_exec_current(struct task *task)
{
    if (task) {
        task->rseq_area = 0;
        task->rseq_len = 0;
        task->rseq_sig = 0;
    }
    int result = syscall_unshare_task_files(task);
    if (result < 0) return result;
    uint32_t group = sched_task_tgid(task);
    struct task *leader = sched_find(group);
    if (leader) {
        /* Linux discards POSIX timers across execve. */
        for (uint32_t i = 0; i < SCHED_TASK_TIMER_MAX; ++i)
            leader->timers[i] = (struct task_posix_timer){0};
    }
    bool pending = false;
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *other = tasks[i];
        if (other == task || sched_task_tgid(other) != group) continue;
        sched_exit(other->pid, 0);
        if (other->running_cpu != SCHED_CPU_NONE) pending = true;
    }
    /* Remote members retire at their next kernel boundary before mm changes. */
    if (pending) return -11;
    storage_drain_task_io(task->pid);
    futex_task_exit(task);
    task->robust_list = 0;
    uint64_t signal_flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &signal_flags);
    kernel_signal_detach_process(task);
    kernel_spin_unlock_irqrestore(&scheduler_lock, signal_flags);
    if (leader) {
        task->alarm_deadline = leader->alarm_deadline;
        task->alarm_interval_ns = leader->alarm_interval_ns;
        task->alarm_last_expiry = leader->alarm_last_expiry;
    }
    if (leader && leader != task) {
        uint64_t flags;
        kernel_spin_lock_irqsave(&scheduler_lock, &flags);
        uint32_t old_tid = task->pid;
        task->parent_pid = leader->parent_pid;
        task->parent_exit_signal = leader->parent_exit_signal;
        task->parent_exit_notified = 0;
        task->flags |= leader->flags & TASK_FLAG_WAITABLE_CHILD;
        leader->flags &= ~TASK_FLAG_WAITABLE_CHILD;
        leader->process_pending_signals = 0;
        leader->alarm_deadline = 0;
        leader->alarm_interval_ns = leader->alarm_last_expiry = 0;
        leader->pid = old_tid;
        task->pid = group;
        current_pid[scheduler_cpu_index()] = group;
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    }
    if (task->shared_sighand) {
        struct task_sighand_state *old = task->shared_sighand;
        for (unsigned i = 0; i < KERNEL_SIGNAL_ACTION_MAX; ++i) task->signal_actions[i] = old->actions[i];
        task->shared_sighand = NULL;
        if (!--old->references) kernel_free(old);
    }
    if (task->shared_fs) {
        struct task_fs_state *old = task->shared_fs;
        task->shared_fs = NULL;
        task_copy_cwd(task, old->cwd);
        *sched_task_umask(task) = old->umask;
        if (!--old->references) kernel_free(old);
    }
    task->fs_base = 0;
    return 0;
}

void sched_exec_replace_mm(struct task *task, const struct address_space *replacement)
{
    struct task_address_space_state *old = task->shared_mm;
    /* exec_mmap() calls exec_mm_release() before the new mm is activated.
     * At this point userland_exec_current_path() cannot fail any more, so a
     * vfork parent must be released even though the child is still loading
     * the new image lazily.  A failing execve returns before this function
     * and therefore never wakes the parent early. */
    sched_vfork_child_done(task);
    paging_load_cr3(paging_kernel_cr3());
    if (!old || !--old->references) {
        address_space_destroy(sched_task_as(task));
        sched_task_vma_release(task);
        if (old) kernel_free(old);
    }
    task->shared_mm = NULL;
    task->address_space = (struct task_address_space_state){.as = *replacement};
}

/** @brief Wake signalfd readers attached to the mask updater's shared sighand. */
void sched_signalfd_reconfigure(struct task *updater)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->signalfd_waiting && task->state == TASK_BLOCKED && !task->vfork_child &&
            sched_task_actions(task) == sched_task_actions(updater)) {
            if (task->syscall_file && task->syscall_file->kind == TASK_FILE_KIND_SIGNALFD)
                task->signalfd_wait_mask = task->syscall_file->aux;
            task->wake_tick = 0;
            task->state = TASK_READY;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

static void sched_wake_process_signal(uint32_t tgid, uint64_t bit)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (sched_task_tgid(task) == tgid && task->state == TASK_BLOCKED &&
            (!task->vfork_child || kernel_signal_fatal_pending(task)) &&
            (((~task->blocked_signals | task->sigwait_mask) & bit) || task->signalfd_waiting)) {
            task->wake_tick = 0;
            task->wait_window_id = 0;
            task->state = TASK_READY;
        }
    }
}

/* Publish one group-exit notification, after the last user frame retires.
 * Individual pthread exits and sibling destruction during exec are silent. */
static void sched_notify_parent_exit(struct task *task)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *leader = sched_find(sched_task_tgid(task));
    if (!leader || leader->state != TASK_EXITED || leader->running_cpu != SCHED_CPU_NONE ||
        thread_group_pending(leader->pid, leader) || leader->parent_exit_notified) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    leader->parent_exit_notified = 1;
    unsigned sig = leader->parent_exit_signal;
    if (leader->parent_pid && sig && sig < KERNEL_SIGNAL_ACTION_MAX) {
        for (uint32_t i = 0; i < task_count; ++i) {
            struct task *parent = tasks[i];
            if (sched_task_tgid(parent) != leader->parent_pid || parent->state == TASK_EXITED) continue;
            if (sched_task_actions(parent)[sig].handler != 1) {
                uint64_t bit = 1ULL << (sig - 1);
                __atomic_fetch_or(sched_task_process_pending(parent), bit, __ATOMIC_SEQ_CST);
                sched_wake_process_signal(leader->parent_pid, bit);
            }
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

static uint64_t timeval_ns(struct linux_timeval value)
{
    if ((uint64_t)value.tv_sec > INT64_MAX / 1000000000ULL) return INT64_MAX;
    uint64_t ns = (uint64_t)value.tv_sec * 1000000000ULL;
    uint64_t fraction = (uint64_t)value.tv_usec * 1000;
    return fraction > INT64_MAX - ns ? INT64_MAX : ns + fraction;
}

void sched_itimer_task(struct task *task, const struct linux_itimerval *value, struct linux_itimerval *old)
{
    struct task *leader = sched_find(sched_task_tgid(task));
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    uint64_t remaining = leader->alarm_deadline > scheduler_ticks ? leader->alarm_deadline - scheduler_ticks : 0;
    if (old) {
        old->it_value = (struct linux_timeval){remaining / NTCLKS_TICK_HZ,
            (remaining % NTCLKS_TICK_HZ) * (1000000 / NTCLKS_TICK_HZ)};
        old->it_interval = (struct linux_timeval){leader->alarm_interval_ns / 1000000000,
            (leader->alarm_interval_ns % 1000000000) / 1000};
    }
    if (value) {
        const uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
        uint64_t ns = timeval_ns(value->it_value);
        uint64_t duration = (ns + tick_ns - 1) / tick_ns;
        const uint64_t maximum = (INT64_MAX + tick_ns - 1) / tick_ns;
        leader->alarm_deadline = !duration ? 0 : duration > maximum - scheduler_ticks
            ? maximum : scheduler_ticks + duration;
        leader->alarm_interval_ns = duration ? timeval_ns(value->it_interval) : 0;
        leader->alarm_last_expiry = leader->alarm_deadline;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

uint32_t sched_alarm_task(struct task *task, uint32_t seconds)
{
    struct linux_itimerval value = {.it_value.tv_sec = seconds}, old;
    sched_itimer_task(task, &value, &old);
    uint64_t result = (uint64_t)old.it_value.tv_sec;
    uint64_t fraction = (uint64_t)old.it_value.tv_usec;
    /* Native Linux alarm rounds to nearest second, with a minimum of one while armed. */
    if ((!result && fraction) || fraction >= 500000) ++result;
    return (uint32_t)result;
}

void sched_alarm_rearm(struct task *task)
{
    struct task *leader = sched_find(sched_task_tgid(task));
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    if (leader && !leader->alarm_deadline && leader->alarm_interval_ns) {
        const uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
        uint64_t interval = (leader->alarm_interval_ns + tick_ns - 1) / tick_ns;
        uint64_t expired = leader->alarm_last_expiry;
        uint64_t periods = scheduler_ticks >= expired ? (scheduler_ticks - expired) / interval + 1 : 1;
        leader->alarm_deadline = expired + periods * interval;
        leader->alarm_last_expiry = leader->alarm_deadline;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

static void sched_alarm_expire(void)
{
    const uint64_t bit = 1ULL << 13;
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *leader = tasks[i];
        if (leader->pid != sched_task_tgid(leader) || !leader->alarm_deadline ||
            leader->alarm_deadline > scheduler_ticks) continue;
        leader->alarm_deadline = 0;
        for (uint32_t j = 0; j < task_count; ++j) {
            struct task *live = tasks[j];
            if (sched_task_tgid(live) != leader->pid || live->state == TASK_EXITED) continue;
            if (sched_task_actions(live)[14].handler != 1) {
                __atomic_fetch_or(sched_task_process_pending(live), bit, __ATOMIC_SEQ_CST);
                sched_wake_process_signal(leader->pid, bit);
            }
            break;
        }
    }
}

static uint64_t timer_timespec_ticks(const struct linux_timespec *value)
{
    uint64_t seconds, ns;
    uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
    if (!value || value->tv_sec < 0 || value->tv_nsec < 0 || value->tv_nsec >= 1000000000LL)
        return UINT64_MAX;
    seconds = (uint64_t)value->tv_sec;
    if (seconds > UINT64_MAX / 1000000000ULL) return UINT64_MAX;
    ns = seconds * 1000000000ULL + (uint64_t)value->tv_nsec;
    if (ns < seconds * 1000000000ULL) return UINT64_MAX;
    return ns / tick_ns + (ns % tick_ns != 0);
}

static void timer_ticks_timespec(uint64_t ticks, struct linux_timespec *value)
{
    if (!value) return;
    value->tv_sec = (int64_t)(ticks / NTCLKS_TICK_HZ);
    value->tv_nsec = (int64_t)((ticks % NTCLKS_TICK_HZ) * (1000000000ULL / NTCLKS_TICK_HZ));
}

static struct task_posix_timer *task_timer(struct task *task, int32_t timerid)
{
    struct task *leader = task ? sched_find(sched_task_tgid(task)) : NULL;
    if (!leader || timerid < 0 || timerid >= (int32_t)SCHED_TASK_TIMER_MAX) return NULL;
    struct task_posix_timer *timer = &leader->timers[timerid];
    return timer->used ? timer : NULL;
}

int sched_timer_create(struct task *task, int32_t clockid, int32_t notify,
                       int32_t signo, int32_t target_pid, uint64_t value,
                       int32_t *timerid)
{
    uint64_t flags;
    if (!task || !timerid || task->state == TASK_EXITED ||
        (clockid != LINUX_CLOCK_REALTIME && clockid != LINUX_CLOCK_MONOTONIC &&
         clockid != LINUX_CLOCK_BOOTTIME) ||
        (notify != 0 && notify != 1 && notify != 4) ||
        (notify != 1 && (signo <= 0 || signo >= LINUX_NSIG))) return -LINUX_EINVAL;
    if (notify == 4) {
        struct task *target = sched_find((uint32_t)target_pid);
        if (!target || target->state == TASK_EXITED || sched_task_tgid(target) != sched_task_tgid(task))
            return -LINUX_EINVAL;
    }
    struct task *owner = sched_find(sched_task_tgid(task));
    if (!owner) return -LINUX_ESRCH;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (int32_t id = 0; id < (int32_t)SCHED_TASK_TIMER_MAX; ++id) {
        if (owner->timers[id].used) continue;
        owner->timers[id] = (struct task_posix_timer){
            .used = 1, .id = (uint32_t)id, .clockid = clockid,
            .notify = notify, .signo = signo,
            .target_pid = notify == 4 ? target_pid : (int32_t)task->pid,
            .value_data = value,
        };
        *timerid = id;
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return 0;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return -LINUX_EAGAIN;
}

int sched_timer_delete(struct task *task, int32_t timerid)
{
    uint64_t flags;
    if (!task) return -LINUX_EINVAL;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task_posix_timer *timer = task_timer(task, timerid);
    if (!timer) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return -LINUX_EINVAL;
    }
    *timer = (struct task_posix_timer){0};
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return 0;
}

int sched_timer_settime(struct task *task, int32_t timerid, uint32_t flags,
                        const struct linux_itimerspec *value,
                        struct linux_itimerspec *old)
{
    uint64_t lock_flags;
    if (!task || !value || (flags & ~LINUX_TIMER_ABSTIME) ||
        value->it_value.tv_sec < 0 || value->it_value.tv_nsec < 0 ||
        value->it_value.tv_nsec >= 1000000000LL || value->it_interval.tv_sec < 0 ||
        value->it_interval.tv_nsec < 0 || value->it_interval.tv_nsec >= 1000000000LL)
        return -LINUX_EINVAL;
    uint64_t initial = timer_timespec_ticks(&value->it_value);
    uint64_t interval = timer_timespec_ticks(&value->it_interval);
    if (initial == UINT64_MAX || interval == UINT64_MAX) return -LINUX_EINVAL;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    struct task_posix_timer *timer = task_timer(task, timerid);
    if (!timer) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -LINUX_EINVAL;
    }
    if (old) {
        uint64_t remaining = timer->expiry_tick > scheduler_ticks ? timer->expiry_tick - scheduler_ticks : 0;
        timer_ticks_timespec(remaining, &old->it_value);
        timer_ticks_timespec(timer->interval_ticks, &old->it_interval);
    }
    timer->interval_ticks = interval;
    timer->overrun = 0;
    timer->expiry_tick = initial ? ((flags & LINUX_TIMER_ABSTIME) ? initial : scheduler_ticks + initial) : 0;
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return 0;
}

int sched_timer_gettime(struct task *task, int32_t timerid,
                        struct linux_itimerspec *value)
{
    uint64_t flags;
    if (!task || !value) return -LINUX_EINVAL;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task_posix_timer *timer = task_timer(task, timerid);
    if (!timer) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return -LINUX_EINVAL;
    }
    uint64_t remaining = timer->expiry_tick > scheduler_ticks ? timer->expiry_tick - scheduler_ticks : 0;
    timer_ticks_timespec(remaining, &value->it_value);
    timer_ticks_timespec(timer->interval_ticks, &value->it_interval);
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return 0;
}

int sched_timer_getoverrun(struct task *task, int32_t timerid)
{
    uint64_t flags;
    if (!task) return -LINUX_EINVAL;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task_posix_timer *timer = task_timer(task, timerid);
    int result = timer ? (timer->overrun > INT32_MAX ? INT32_MAX : (int)timer->overrun) : -LINUX_EINVAL;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

static void sched_posix_timer_expire(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *owner = tasks[i];
        if (!owner || owner->state == TASK_EXITED) continue;
        for (uint32_t j = 0; j < SCHED_TASK_TIMER_MAX; ++j) {
            struct task_posix_timer *timer = &owner->timers[j];
            if (!timer->used || !timer->expiry_tick || timer->expiry_tick > scheduler_ticks) continue;
            if (timer->notify != 1) {
                uint64_t bit = 1ULL << (uint32_t)(timer->signo - 1);
                if (timer->notify == 4) {
                    /* SIGEV_THREAD_ID is the one notification mode that is
                     * thread-directed on Linux. */
                    struct task *target = sched_find((uint32_t)timer->target_pid);
                    if (target && target->state != TASK_EXITED &&
                        sched_task_tgid(target) == sched_task_tgid(owner)) {
                        if (sched_task_pending(target) & bit) ++timer->overrun;
                        target->timer_pending_signals |= bit;
                        __atomic_fetch_or(&target->pending_signals, bit, __ATOMIC_SEQ_CST);
                        if (target->state == TASK_BLOCKED &&
                            (!target->vfork_child || kernel_signal_fatal_pending(target))) {
                            target->wake_tick = 0;
                            target->state = TASK_READY;
                        }
                    }
                } else {
                    /* SIGEV_SIGNAL is process-directed.  Keep it in the
                     * shared process queue so any eligible thread can
                     * receive it, matching kill(2)/Linux signal selection. */
                    uint64_t *pending = sched_task_process_pending(owner);
                    if ((*pending & bit) != 0) ++timer->overrun;
                    owner->timer_pending_signals |= bit;
                    __atomic_fetch_or(pending, bit, __ATOMIC_SEQ_CST);
                    sched_wake_process_signal(sched_task_tgid(owner), bit);
                }
            }
            if (!timer->interval_ticks) {
                timer->expiry_tick = 0;
            } else {
                uint64_t periods = (scheduler_ticks - timer->expiry_tick) / timer->interval_ticks + 1;
                if (periods > 1) timer->overrun += periods - 1;
                timer->expiry_tick += periods * timer->interval_ticks;
            }
        }
    }
}

/** @brief Account a tick and queue expirations; signal actions run on return to user mode. */
void sched_on_tick(void)
{
    struct task *current;
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    ++scheduler_ticks;
    current = sched_find(scheduler_current_pid());
    uint32_t cpu = scheduler_cpu_index();
    if (current && current->state == TASK_RUNNING) {
        ++scheduler_busy_ticks;
        ++scheduler_cpu_busy_ticks[cpu];
        ++current->cpu_ticks;
    } else {
        ++scheduler_idle_ticks;
        ++scheduler_cpu_idle_ticks[cpu];
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->state == TASK_BLOCKED && !tasks[i]->vfork_child && tasks[i]->wake_tick &&
            tasks[i]->wake_tick <= scheduler_ticks) {
            tasks[i]->wake_tick = 0;
            tasks[i]->wait_window_id = 0;
            tasks[i]->state = TASK_READY;
            /* A blocked task may still be reserved by the CPU that is
             * finishing its syscall.  Preserve that reservation; the owner
             * will either reclaim it or release it when selecting another
             * task. */
        }
    }
    sched_alarm_expire();
    sched_posix_timer_expire();
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * Account a local-APIC preemption tick. The BSP PIT alone advances wall time
 * and sleepers; AP ticks only account and preempt the task owned by that CPU.
 */
void sched_on_cpu_tick(void)
{
    struct task *current;
    uint32_t cpu = scheduler_cpu_index();
    uint64_t flags;

    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    current = sched_find(current_pid[cpu]);
    if (current && current->state == TASK_RUNNING && current->running_cpu == cpu) {
        ++scheduler_busy_ticks;
        ++scheduler_cpu_busy_ticks[cpu];
        ++current->cpu_ticks;
    } else {
        ++scheduler_idle_ticks;
        ++scheduler_cpu_idle_ticks[cpu];
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Return the total number of scheduler ticks since init.
 */
uint64_t sched_tick_count(void)
{
    return scheduler_ticks;
}

void sched_yield_current(void)
{
    /**
 * @brief The kernel debugger runs before the first user task exists. A real context switch is therefore neither useful nor safe here; the timer interrupt remains the scheduling boundary for the normal system.
 */
    __asm__ volatile("pause");
}

/**
 * @brief Write the accumulated busy and idle tick counters into the caller's outputs.
 */
void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    if (busy_ticks) {
        *busy_ticks = scheduler_busy_ticks;
    }
    if (idle_ticks) {
        *idle_ticks = scheduler_idle_ticks;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

void sched_cpu_ticks_per_cpu(uint64_t *busy_ticks, uint64_t *idle_ticks,
                             uint32_t capacity)
{
    uint64_t flags;
    uint32_t count = capacity < SMP_MAX_CPUS ? capacity : SMP_MAX_CPUS;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < count; ++i) {
        if (busy_ticks) {
            busy_ticks[i] = scheduler_cpu_busy_ticks[i];
        }
        if (idle_ticks) {
            idle_ticks[i] = scheduler_cpu_idle_ticks[i];
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

void sched_cpu_runtime_snapshot(uint64_t *busy_ticks, uint64_t *idle_ticks,
                                uint32_t *current_pids, uint32_t *ready_counts,
                                uint32_t capacity)
{
    uint64_t flags;
    uint32_t count = capacity < SMP_MAX_CPUS ? capacity : SMP_MAX_CPUS;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t cpu = 0; cpu < count; ++cpu) {
        if (busy_ticks) {
            busy_ticks[cpu] = scheduler_cpu_busy_ticks[cpu];
        }
        if (idle_ticks) {
            idle_ticks[cpu] = scheduler_cpu_idle_ticks[cpu];
        }
        if (current_pids) {
            current_pids[cpu] = current_pid[cpu];
        }
        if (ready_counts) {
            uint32_t ready = 0;
            for (uint32_t i = 0; i < task_count; ++i) {
                if (tasks[i]->state == TASK_READY && task_cpu_allowed(tasks[i], cpu)) {
                    ++ready;
                }
            }
            ready_counts[cpu] = ready;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Count tasks by RUNNING/READY/BLOCKED state and report each plus the total count.
 */
void sched_task_counts(uint32_t *out_task_count, uint32_t *running_tasks,
                       uint32_t *ready_tasks, uint32_t *sleeping_tasks)
{
    uint32_t running = 0;
    uint32_t ready = 0;
    uint32_t sleeping = 0;
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->state == TASK_RUNNING) {
            ++running;
        } else if (tasks[i]->state == TASK_READY) {
            ++ready;
        } else if (tasks[i]->state == TASK_BLOCKED) {
            ++sleeping;
        }
    }
    if (out_task_count) {
        *out_task_count = task_count;
    }
    if (running_tasks) {
        *running_tasks = running;
    }
    if (ready_tasks) {
        *ready_tasks = ready;
    }
    if (sleeping_tasks) {
        *sleeping_tasks = sleeping;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Return the pid of the currently running task (0 before the first switch).
 */
uint32_t sched_current_pid(void)
{
    return scheduler_current_pid();
}

int sched_get_task_affinity(uint32_t pid, uint64_t *mask)
{
    uint64_t flags;
    int result = -2;
    if (!mask) {
        return -22;
    }
    if (!pid) {
        pid = scheduler_current_pid();
    }
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid && tasks[i]->state != TASK_EXITED) {
            *mask = tasks[i]->affinity_mask ? tasks[i]->affinity_mask : sched_all_cpu_mask();
            result = 0;
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

int sched_set_task_affinity(uint32_t pid, uint64_t mask)
{
    uint64_t flags;
    uint64_t allowed = sched_all_cpu_mask();
    int result = -2;
    if (!pid) {
        pid = scheduler_current_pid();
    }
    if (!mask || !(mask & allowed)) {
        return -22;
    }
    mask &= allowed;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid && tasks[i]->state != TASK_EXITED) {
            tasks[i]->affinity_mask = mask;
            result = 0;
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

/**
 * @brief Linear scan for the task with the given pid, or NULL when absent.
 */
struct task *sched_find(uint32_t pid)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid) {
            return tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Return the first task whose name matches, or NULL when none does.
 */
struct task *sched_find_by_name(const char *name)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (str_eq(tasks[i]->name, name)) {
            return tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Return the live user task whose path matches exactly, or NULL.
 */
struct task *sched_find_by_path(const char *path)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid && tasks[i]->kind == TASK_KIND_USER &&
            tasks[i]->state != TASK_EXITED && str_eq(tasks[i]->path, path)) {
            return tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Return the live user task whose path's basename matches, or NULL.
 */
struct task *sched_find_by_path_basename(const char *basename)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid && tasks[i]->kind == TASK_KIND_USER &&
            tasks[i]->state != TASK_EXITED &&
            str_eq(task_path_basename(tasks[i]->path), basename)) {
            return tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief True when path resolves to the given volume id (used to detect in-use mounts).
 */
static bool sched_path_uses_volume(const char *path, uint32_t volume_id)
{
    uint32_t path_volume_id;
    return path && storage_path_volume_id(path, &path_volume_id) == 0 &&
           path_volume_id == volume_id;
}

/**
 * @brief True when any live task's cwd, path, image node, or open files/VMA reference this volume.
 */
bool sched_volume_in_use(uint32_t volume_id)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        const struct task *task = tasks[i];
        if (!task->pid || task->state == TASK_EXITED) {
            continue;
        }
        if (sched_path_uses_volume(sched_task_cwd(task), volume_id) ||
            sched_path_uses_volume(task->path, volume_id) ||
            task->image_node.volume_id == volume_id) {
            return true;
        }
        for (uint32_t fd = 0; fd < sched_task_file_capacity(task); ++fd) {
            const struct task_file *file = task_file_description(sched_task_file_at((struct task *)task, fd));
            if (file && file->used && file->node.volume_id == volume_id) {
                return true;
            }
        }
        for (uint32_t fd = 0; fd < SCHED_TASK_STDIO_MAX; ++fd) {
            struct task_file *file = task_file_description(&sched_task_fds(task)->stdio_files[fd]);
            if (file->used && file->node.volume_id == volume_id) {
                return true;
            }
        }
        for (uint32_t vma = 0; vma < SCHED_TASK_VMA_MAX; ++vma) {
            if (sched_task_mm(task)->vmas[vma].used &&
                (sched_task_mm(task)->vmas[vma].flags & TASK_VMA_FLAG_FILE) &&
                sched_task_mm(task)->vmas[vma].file_node.volume_id == volume_id) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Return the struct task for the currently running pid, or NULL.
 */
struct task *sched_current_task(void)
{
    return sched_find(scheduler_current_pid());
}

/**
 * Save the interrupted Ring-3 state before its CPU releases the task. The
 * frame copy and ownership change share one scheduler-lock transaction so no
 * other CPU can claim a partially published return context.
 */
bool sched_capture_current_user_frame(const struct trap_frame *frame)
{
    uint64_t flags;
    uint32_t cpu = scheduler_cpu_index();
    struct task *current = NULL;

    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == current_pid[cpu]) {
            current = tasks[i];
            break;
        }
    }
    if (!current || current->kind != TASK_KIND_USER ||
        current->state == TASK_EXITED || current->running_cpu != cpu) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    /* execve has already installed a fresh address space and reset the saved
     * frame for the pending image.  The trap frame passed here still belongs
     * to the replaced program; copying it would resurrect the old RIP and
     * register state.  Only release this CPU's reservation so the normal
     * image-loader path can construct the new initial frame. */
    if (!(current->flags & TASK_FLAG_PENDING_LOAD)) {
        if (!task_frame_valid(current, frame)) {
            kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
            return false;
        }
        current->frame = *frame;
    }
    if (current->state == TASK_RUNNING) {
        current->state = TASK_READY;
    }
    /* The interrupted frame is fully published under scheduler_lock. The
     * task is no longer executing on this CPU, so release the reservation and
     * allow another eligible CPU to steal it if this CPU has better work. */
    current->running_cpu = SCHED_CPU_NONE;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

/**
 * A remote exit may mark a task terminal while it is still executing in
 * Ring-3 on this CPU.  Keep its address space and descriptor table alive
 * until this CPU reaches a scheduling boundary, then drop only this CPU's
 * reservation.  Reapers and slot recycling require running_cpu == NONE.
 */
void sched_quiesce_exited_current(void)
{
    uint32_t cpu = scheduler_cpu_index();
    uint32_t pid;
    uint64_t flags;
    uint32_t gpu_owner = 0;

    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    pid = current_pid[cpu];
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid && tasks[i]->state == TASK_EXITED) {
            if (tasks[i]->running_cpu == cpu ||
                tasks[i]->running_cpu == SCHED_CPU_NONE) {
                tasks[i]->running_cpu = SCHED_CPU_NONE;
                current_pid[cpu] = 0;
                gpu_owner = pid;
            }
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    if (gpu_owner) svga_gpu_release_owner(gpu_owner);
}

/**
 * @brief Round-robin scan for the next READY, fully-loaded user task with the lowest priority.
 */
struct task *sched_select_next_user(void)
{
    uint32_t current_index = 0;
    struct task *best = NULL;
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == scheduler_current_pid()) {
            current_index = i;
            break;
        }
    }

    /*
     * Services (especially desktop.elf) must participate in the same
     * round-robin queue as normal applications.  The old two-pass policy
     * skipped every service while any ordinary process was READY; a compiler
     * or shell that stayed runnable could therefore starve the window server
     * indefinitely and make the whole desktop appear frozen.
     */
    for (uint32_t n = 1; n <= task_count; ++n) {
        uint32_t i = (current_index + n) % task_count;
        if (tasks[i]->kind != TASK_KIND_USER || tasks[i]->state != TASK_READY ||
            (tasks[i]->vfork_child && !kernel_signal_fatal_pending(tasks[i])) ||
            (tasks[i]->running_cpu != SCHED_CPU_NONE && tasks[i]->running_cpu != scheduler_cpu_index())) {
            continue;
        }
        if (!task_cpu_allowed(tasks[i], scheduler_cpu_index())) {
            continue;
        }
        if ((!tasks[i]->entry && !(tasks[i]->image && tasks[i]->image_len) &&
             !(tasks[i]->flags & TASK_FLAG_PENDING_LOAD)) ||
            !tasks[i]->stack_top || !(*sched_task_as(tasks[i])).cr3) {
            continue;
        }
        if (!best || tasks[i]->priority < best->priority) {
            best = tasks[i];
        }
    }
    if (best) {
        uint32_t cpu = scheduler_cpu_index();
        uint32_t old_pid = current_pid[cpu];
        if (old_pid && old_pid != best->pid) {
            struct task *old = sched_find(old_pid);
            if (old && old->running_cpu == cpu) {
                old->running_cpu = SCHED_CPU_NONE;
            }
        }
        best->state = TASK_RUNNING;
        best->running_cpu = cpu;
        best->last_cpu = cpu;
        current_pid[cpu] = best->pid;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return best;
}

/**
 * @brief Atomically put this CPU's saved task back on the running slot.
 *
 * A preemption path first changes RUNNING to READY while publishing the
 * interrupted frame. If the run queue is otherwise empty, the same task must
 * be claimed again before its frame is returned to the iret path. Leaving it
 * READY permits another CPU to claim the same address space concurrently.
 */
struct task *sched_reclaim_current_user(void)
{
    uint32_t cpu = scheduler_cpu_index();
    uint32_t pid;
    struct task *task = NULL;
    uint64_t flags;

    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    pid = current_pid[cpu];
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid) {
            task = tasks[i];
            break;
        }
    }
    if (!task || task->kind != TASK_KIND_USER || task->state != TASK_READY ||
        (task->vfork_child && !kernel_signal_fatal_pending(task)) ||
        (task->running_cpu != SCHED_CPU_NONE && task->running_cpu != cpu) ||
        !task_cpu_allowed(task, cpu) ||
        !task_frame_valid(task, &task->frame)) {
        task = NULL;
    } else {
        task->state = TASK_RUNNING;
        task->running_cpu = cpu;
        task->last_cpu = cpu;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return task;
}

/**
 * @brief Return a pointer to the task's saved trap frame, or NULL for a NULL task.
 */
struct trap_frame *sched_task_frame(struct task *task)
{
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        !task_frame_valid(task, &task->frame)) {
        if (task) {
            console_printf("[ntclks] rejected invalid user frame pid=%u rip=0x%llx rsp=0x%llx cr3=0x%llx\n",
                           task->pid,
                           (unsigned long long)task->frame.rip,
                           (unsigned long long)task->frame.rsp,
                           (unsigned long long)(*sched_task_as(task)).cr3);
        }
        return NULL;
    }
    return &task->frame;
}

/**
 * @brief Return the task's address-space CR3, or 0 for a NULL task.
 */
uint64_t sched_task_cr3(struct task *task)
{
    return task ? (*sched_task_as(task)).cr3 : 0;
}

/**
 * @brief Wake a non-exited task: clear its wait state and set it READY.
 */
void sched_block_current(void)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->state = TASK_BLOCKED;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Publish signal-wait sleep before rechecking pending notifications.
 * @param deadline Absolute tick deadline, or zero for no timeout.
 */
void sched_signal_wait_current(uint64_t deadline)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_current_task();
    if (task && task->pid && task->state != TASK_EXITED) {
        task->wake_tick = deadline;
        task->wait_window_id = 0;
        __atomic_store_n(&task->state, TASK_BLOCKED, __ATOMIC_SEQ_CST);
        uint64_t pending = __atomic_load_n(&task->pending_signals, __ATOMIC_SEQ_CST) |
            __atomic_load_n(sched_task_process_pending(task), __ATOMIC_SEQ_CST);
        if ((pending & (~task->blocked_signals | task->sigwait_mask | task->signalfd_wait_mask)) ||
            (deadline && scheduler_ticks >= deadline)) {
            task->wake_tick = 0;
            task->state = TASK_READY;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Wake a published interruptible wait without resuming stopped/exited tasks.
 * @param task Task pinned by the kernel execution lock; it may still be retiring on another CPU.
 */
void sched_wake_interruptible(struct task *task)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    if (task && task->state == TASK_BLOCKED && !task->vfork_child) {
        task->wake_tick = 0;
        task->wait_window_id = 0;
        task->state = TASK_READY;
    }
    /* running_cpu prevents another CPU from claiming a not-yet-retired frame. */
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

void sched_mark_ready(uint32_t pid)
{
    uint64_t flags;
    uint32_t cpu = scheduler_cpu_index();
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_find(pid);
    if (!task || task->state == TASK_EXITED || task->vfork_child) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    /* A running task is already runnable.  In particular, an event emitted
     * by CPU0 must not publish a task currently executing on CPU1 as READY:
     * doing so lets a third CPU claim its address space and eventually return
     * through a corrupted trap frame. */
    if (task->state == TASK_RUNNING) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    if (task->running_cpu != SCHED_CPU_NONE && task->running_cpu != cpu) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->state = TASK_READY;
    task->running_cpu = SCHED_CPU_NONE;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Put the current task to sleep until wake_tick (no-op for the idle or an exited task).
 */
void sched_sleep_current_until(uint64_t wake_tick)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    task->wait_window_id = 0;
    task->wake_tick = wake_tick;
    /* Keep the CPU reservation until the interrupt frame is captured by
     * userland_schedule_from_frame().  Clearing it here would make that
     * capture look like a second CPU owns the task. */
    task->state = TASK_BLOCKED;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Terminate a user task by pid, returning a negative error for idle/services/exited/self.
 */
int sched_kill_user_task(uint32_t pid, uint64_t code)
{
    struct task *task = sched_find(pid);
    if (!task || task->pid == 0) {
        return -2;
    }
    if (task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        (task->flags & TASK_FLAG_SERVICE) || pid == scheduler_current_pid()) {
        return -1;
    }
    sched_exit(pid, code);
    return 0;
}

/**
 * @brief Record a POSIX signal on a user task, handling STOP/CONT state changes and fatal signals.
 */
int sched_signal_user_task(uint32_t pid, int signal_number)
{
    struct task *task = sched_find(pid);
    if (!task || task->pid == 0 || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED || signal_number < 0 || (unsigned)signal_number >= KERNEL_SIGNAL_ACTION_MAX) {
        return -1;
    }
    if (signal_number == 0) {
        return 0;
    }
    /* User-handler delivery now lives beside the signal-frame ABI. Default,
     * ignored, STOP/CONT, blocked and wakeup behavior are all applied here so
     * wait/kill/PTY/console paths do not implement their own signal policy. */
    return kernel_signal_queue_task(task, signal_number);
}

void sched_signal_discard(struct task *task, int signal_number)
{
    uint64_t bit = 1ULL << (signal_number - 1);
    kernel_signal_flush(task, true, bit);
    for (uint32_t i = 0; i < task_count; ++i)
        if (sched_task_tgid(tasks[i]) == sched_task_tgid(task)) kernel_signal_flush(tasks[i], false, bit);
}

void sched_signal_job_control(uint32_t tgid, int signal_number)
{
    const uint64_t stopped = (1ULL << 18) | (1ULL << 19) | (1ULL << 20) | (1ULL << 21);
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        uint64_t flags;
        kernel_spin_lock_irqsave(&scheduler_lock, &flags);
        if (sched_task_tgid(task) != tgid || task->state == TASK_EXITED) {
            kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
            continue;
        }
        uint64_t discard = signal_number == 18 ? stopped : 1ULL << 17;
        kernel_signal_flush(task, false, discard);
        kernel_signal_flush(task, true, discard);
        if (signal_number == 18) {
            if (task->state != TASK_STOPPED) {
                kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
                continue;
            }
            task->state = task->vfork_child ? TASK_BLOCKED : TASK_READY;
            task->stop_signal = 0;
            task->child_event = TASK_CHILD_EVENT_CONTINUED;
        } else {
            if (task->vfork_child) {
                /* TASK_KILLABLE does not enter a group stop until completion. */
                kernel_signal_enqueue(task, false, signal_number, NULL);
                kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
                continue;
            }
            task->state = TASK_STOPPED;
            task->stop_signal = (uint32_t)signal_number;
            task->child_event = TASK_CHILD_EVENT_STOPPED;
        }
        task->wake_tick = 0;
        task->wait_window_id = 0;
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        if (signal_number != 18) task_sysv_sem_stop(task);
    }
}

int sched_signal_user_process(uint32_t tgid, int signal_number)
{
    struct task *leader = sched_find(tgid);
    if (!leader || sched_task_tgid(leader) != tgid) return -1;
    return sched_signal_user_process_info(tgid, signal_number, NULL);
}

/**
 * @brief Queue process-directed siginfo using the addressed PID or TID's thread group.
 * @param pid Addressed task whose credentials and limits govern the send.
 * @param signal_number Native signal number, zero for an existence probe.
 * @param info Explicit signal data, or NULL for a kernel notification.
 * @return Zero on success, or a negative target/queue error.
 */
int sched_signal_user_process_info(uint32_t pid, int signal_number,
                                   const struct linux_siginfo *info)
{
    struct task *leader = sched_find(pid), *live = NULL, *eligible = NULL;
    if (!leader || leader->kind != TASK_KIND_USER ||
        signal_number < 0 || (unsigned)signal_number >= KERNEL_SIGNAL_ACTION_MAX) return -1;
    uint32_t tgid = sched_task_tgid(leader);
    if (!signal_number) return 0;
    uint64_t bit = 1ULL << (signal_number - 1);
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (sched_task_tgid(task) != tgid || task->state == TASK_EXITED) continue;
        if (!live) live = task;
        if (!eligible && ((~task->blocked_signals | task->sigwait_mask) & bit)) eligible = task;
    }
    if (!live) return 0; /* A zombie still owns its PID until wait reaps it. */
    if (signal_number == 18) sched_signal_job_control(tgid, signal_number);
    if (signal_number >= 19 && signal_number <= 22) sched_signal_discard(live, 18);
    struct kernel_signal_action *action = &sched_task_actions(live)[signal_number];
    /* Ignore only when the addressed task has neither blocked nor waited for
     * this signal, matching Linux sig_ignored() and real_blocked semantics. */
    if (!((leader->blocked_signals | leader->sigwait_mask) & bit) &&
        (action->handler == 1 || (!action->handler && kernel_signal_default_ignored(signal_number))))
        return 0;
    if (signal_number == 9 || signal_number == 19)
        return info ? kernel_signal_queue_task_info(eligible ? eligible : live, signal_number, info) :
            kernel_signal_queue_task(eligible ? eligible : live, signal_number);
    int result = kernel_signal_enqueue(leader, true, signal_number, info);
    if (result < 0) return result;
    /* All eligible sleepers may recheck; only one consumes the process signal. */
    sched_wake_process_signal(tgid, bit);
    return 0;
}

/**
 * @brief Read or update a task's default/ignored signal disposition.
 * @param pid Target user-task ID.
 * @param signal_number POSIX signal number.
 * @param operation Query or update operation.
 * @param disposition Requested value for update and previous value on return.
 * @return Zero on success or a negative scheduler error.
 */
int sched_signal_action(uint32_t pid, int signal_number, uint32_t operation,
                        uint32_t *disposition)
{
    struct task *task = sched_find(pid);
    uint32_t bit;
    if (!task || task->pid == 0 || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED || !disposition || signal_number <= 0 ||
        signal_number >= 32 || signal_number == 9 || signal_number == 17) {
        return -1;
    }
    bit = 1u << (uint32_t)signal_number;
    if (operation == 1U) {
        *disposition = (task->ignored_signals & bit) ? 1U : 0U;
        return 0;
    }
    if (operation != 2U || *disposition > 1U) {
        return -1;
    }
    {
        uint32_t previous = (task->ignored_signals & bit) ? 1U : 0U;
        if (*disposition) {
            task->ignored_signals |= bit;
            task->pending_signals &= ~bit;
        } else {
            task->ignored_signals &= ~bit;
        }
        *disposition = previous;
    }
    return 0;
}

/**
 * @brief Signal every user task in a group the caller owns; returns the count or a negative error.
 */
int sched_signal_process_group(uint32_t sender_pid, uint32_t process_group,
                               int signal_number)
{
    struct task *sender = sched_find(sender_pid);
    int count = 0;
    if (!sender || !process_group) {
        return -1;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->kind != TASK_KIND_USER ||
            task->state == TASK_EXITED || task->process_group != process_group) {
            continue;
        }
        if (sender->uid != 0 && sender->uid != task->uid) {
            return -1;
        }
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->kind != TASK_KIND_USER ||
            task->state == TASK_EXITED || task->process_group != process_group) {
            continue;
        }
        if (task->pid == sched_task_tgid(task) && sched_signal_user_process(task->pid, signal_number) == 0) {
            ++count;
        }
    }
    return count ? count : -2;
}

/**
 * @brief Move a caller-controlled task into an existing or newly created process group.
 */
int sched_set_process_group(uint32_t caller_pid, uint32_t pid,
                            uint32_t process_group)
{
    struct task *caller = sched_find(caller_pid);
    struct task *target;
    int group_exists = 0;
    if (!caller || caller->kind != TASK_KIND_USER) {
        return -1;
    }
    if (!pid) {
        pid = caller_pid;
    }
    target = sched_find(pid);
    if (!target || target->kind != TASK_KIND_USER || target->state == TASK_EXITED) {
        return -2;
    }
    if (target != caller && target->parent_pid != caller_pid) {
        return -1;
    }
    if (!process_group) {
        process_group = pid;
    }
    if (target->process_group == target->pid && process_group != target->pid) {
        return -1;
    }
    if (target->process_session != caller->process_session) {
        return -1;
    }
    if (process_group != pid) {
        for (uint32_t i = 0; i < task_count; ++i) {
            const struct task *member = tasks[i];
            if (member->pid != 0 && member->kind == TASK_KIND_USER &&
                member->state != TASK_EXITED &&
                member->process_group == process_group &&
                member->process_session == target->process_session) {
                group_exists = 1;
                break;
            }
        }
        if (!group_exists) {
            return -2;
        }
    }
    target->process_group = process_group;
    return 0;
}

/**
 * @brief Return the process group of pid (0 = current), or a negative error.
 */
static int64_t sched_process_identity(uint32_t pid, bool session)
{
    if (!pid) pid = sched_current_pid();
    uint64_t flags;
    int64_t result = -LINUX_ESRCH;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    const struct task *task = sched_find(pid);
    /* A zombie still owns its identity. Reaped slots stay in the task table
     * until recycled, but must no longer be visible through a Linux PID. */
    if (task && task->kind == TASK_KIND_USER &&
        (task->state != TASK_EXITED || (task->flags & TASK_FLAG_WAITABLE_CHILD))) {
        result = session ? task->process_session : task->process_group;
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

int64_t sched_get_process_group(uint32_t pid)
{
    return sched_process_identity(pid, false);
}

/**
 * @brief Make the calling task the leader of a new session and process group.
 */
int64_t sched_create_process_session(uint32_t pid)
{
    struct task *task = sched_find(pid);
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED) {
        return -2;
    }
    if (task->process_group == pid) {
        return -1;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        const struct task *member = tasks[i];
        if (member != task && member->pid != 0 && member->state != TASK_EXITED &&
            member->process_group == pid) {
            return -1;
        }
    }
    task->process_session = pid;
    task->process_group = pid;
    sched_set_controlling_pty(pid, 0);
    return pid;
}

/**
 * @brief Count open PTY endpoint descriptors and controlling attachments.
 * @param pty_id PTY identifier.
 * @return Number of live references.
 */
uint32_t sched_pty_reference_count(uint32_t pty_id)
{
    uint32_t count = 0;
    uint64_t flags;
    if (!pty_id) return 0;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (!task || task->kind != TASK_KIND_USER) continue;
        if (task->pty_id == pty_id) ++count;
        for (uint32_t fd = 0; fd < SCHED_TASK_PTY_FD_MAX; ++fd) {
            if (sched_task_fds(task)->pty_fds[fd].used && sched_task_fds(task)->pty_fds[fd].pty_id == pty_id) {
                ++count;
            }
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return count;
}

uint32_t sched_pty_master_reference_count(uint32_t pty_id)
{
    uint32_t count = 0;
    uint64_t flags;
    if (!pty_id) return 0;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (!task || task->kind != TASK_KIND_USER) continue;
        for (uint32_t fd = 0; fd < SCHED_TASK_PTY_FD_MAX; ++fd) {
            const struct task_pty_fd *entry = &sched_task_fds(task)->pty_fds[fd];
            if (entry->used && entry->pty_id == pty_id &&
                entry->endpoint == TASK_PTY_ENDPOINT_MASTER) ++count;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return count;
}

/**
 * @brief True when some live task in this process group is attached to the given PTY.
 */
int sched_process_group_has_pty(uint32_t process_group, uint32_t pty_id)
{
    if (!process_group || !pty_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        const struct task *task = tasks[i];
        if (task->pid != 0 && task->kind == TASK_KIND_USER &&
            task->state != TASK_EXITED && task->process_group == process_group &&
            task->pty_id == pty_id) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Return the process session of pid, or a negative error.
 */
int64_t sched_get_process_session(uint32_t pid)
{
    return sched_process_identity(pid, true);
}

/**
 * @brief Read or set a user task's nice-style priority, clamping new values to [-20, 19].
 */
int sched_task_priority(uint32_t pid, int priority, int set)
{
    struct task *task = sched_find(pid);
    if (!task || task->pid == 0 || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED) {
        return -1;
    }
    if (set) {
        if (priority < -20) priority = -20;
        if (priority > 19) priority = 19;
        task->priority = priority;
    }
    return task->priority;
}

/**
 * @brief Kill every user task (except keep_pid) attached to pty_id and clear its PTY fds.
 */
int sched_kill_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid,
                                  uint64_t code)
{
    int killed = 0;
    if (!pty_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->pid == keep_pid ||
            task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
            (task->flags & TASK_FLAG_SERVICE) || task->pty_id != pty_id) {
            continue;
        }
        /* This task may still be executing on another CPU.  It becomes
         * reclaimable only after that CPU observes TASK_EXITED and clears
         * running_cpu at its next scheduling boundary. */
        sched_exit(task->pid, code);
        ++killed;
    }
    return killed;
}

/**
 * @brief Send SIGHUP to every user task attached to a closing PTY.
 * @param pty_id Closed PTY identifier.
 * @param keep_pid PTY owner to leave untouched.
 * @return Number of signalled tasks, or zero when no PTY was supplied.
 */
int sched_hangup_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid)
{
    int signalled = 0;
    if (!pty_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->pid == keep_pid ||
            task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
            (task->flags & TASK_FLAG_SERVICE) || task->pty_id != pty_id) {
            continue;
        }
        if (sched_signal_user_task(task->pid, 1) == 0) {
            ++signalled;
        }
        /* A process that ignored SIGHUP can outlive the terminal.  Avoid
         * leaving it attached to a PTY id which may later be reused. */
        if (task->state != TASK_EXITED) {
            task->pty_id = 0;
            task->wait_pty_id = 0;
            for (uint32_t fd = 0; fd < SCHED_TASK_PTY_FD_MAX; ++fd) {
                sched_task_fds(task)->pty_fds[fd] = (struct task_pty_fd){0};
            }
        }
    }
    pty_reap_hungup(pty_id);
    return signalled;
}

/**
 * @brief Kill every non-service user task in this uid/session except keep_pid.
 */
int sched_kill_user_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                     uint32_t keep_pid, uint64_t code)
{
    int killed = 0;
    if (!uid || !session_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->pid == keep_pid ||
            task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
            task->uid != uid || task->session_id != session_id ||
            (task->flags & TASK_FLAG_SERVICE)) {
            continue;
        }
        sched_exit(task->pid, code);
        ++killed;
    }
    return killed;
}

/**
 * @brief Reap an exited child (or report stop/continue events) matching waitpid semantics.
 */
int64_t sched_wait_reap(uint32_t waiter_pid, int32_t wanted_pid,
                        uint32_t options, int *status)
{
    uint64_t lock_flags;
    struct task *waiter;
    struct task *reap_task = NULL;
    uint32_t reap_pid = 0;
    int reap_status = 0;
    int found_child = 0;
    uint32_t wanted_group = 0;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    waiter = sched_find(waiter_pid);
    if (!waiter) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -2;
    }
    if (wanted_pid < -1) {
        wanted_group = (uint32_t)(-(int64_t)wanted_pid);
    } else if (wanted_pid == 0) {
        wanted_group = waiter->process_group;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (task->pid == 0 || task->kind != TASK_KIND_USER) {
            continue;
        }
        if (task->pid != sched_task_tgid(task)) continue;
        if (task->parent_pid != sched_task_tgid(waiter) && waiter_pid != 0) {
            continue;
        }
        if (wanted_pid > 0 && task->pid != (uint32_t)wanted_pid) {
            continue;
        }
        if (wanted_group && task->process_group != wanted_group) {
            continue;
        }
        found_child = 1;
        if (task->state == TASK_EXITED && task->running_cpu == SCHED_CPU_NONE &&
            !thread_group_pending(sched_task_tgid(task), task) &&
            !(task->flags & TASK_FLAG_REAP_IN_PROGRESS)) {
            reap_task = task;
            reap_pid = task->pid;
            if (status) {
                reap_status = task->exit_signal
                                  ? (int)(task->exit_signal & 0x7fU)
                                  : (int)((task->exit_code & 0xffU) << 8);
            }
            task->flags |= TASK_FLAG_REAP_IN_PROGRESS;
            break;
        }
        if (task->child_event == TASK_CHILD_EVENT_STOPPED && (options & 2U)) {
            if (status) {
                *status = (int)(((task->stop_signal & 0xffU) << 8) | 0x7fU);
            }
            task->child_event = TASK_CHILD_EVENT_NONE;
            kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
            return task->pid;
        }
        if (task->child_event == TASK_CHILD_EVENT_CONTINUED && (options & 4U)) {
            if (status) {
                *status = 0xffff;
            }
            task->child_event = TASK_CHILD_EVENT_NONE;
            kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
            return task->pid;
        }
    }
    if (reap_task) {
        /* Address-space, storage, and pipe teardown can take other locks and
         * may touch page tables.  Never perform that work while holding the
         * scheduler spinlock with interrupts disabled. */
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        sched_release_task_resources(reap_task);

        kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
        if (reap_task->pid == reap_pid &&
            (reap_task->flags & TASK_FLAG_REAP_IN_PROGRESS)) {
            reap_task->parent_pid = 0;
            task_release_limits(reap_task);
            reap_task->flags &= ~(TASK_FLAG_WAITABLE_CHILD | TASK_FLAG_REAP_IN_PROGRESS);
            task_copy_name(reap_task, "reaped");
            reap_task->image = NULL;
            reap_task->image_len = 0;
            reap_task->pty_id = 0;
            task_clear_identity(reap_task);
            task_copy_cwd(reap_task, "/");
            for (size_t j = 0; j < SCHED_TASK_FILE_MAX; ++j) {
                sched_task_fds(reap_task)->files[j].used = 0;
                sched_task_fds(reap_task)->files[j].offset = 0;
                sched_task_fds(reap_task)->files[j].aux = 0;
            }
            console_printf("[ntclks] scheduler wait reaped pid=%u by pid=%u\n",
                           reap_pid, waiter_pid);
        }
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        if (status) {
            *status = reap_status;
        }
        return reap_pid;
    }
    /* The syscall trap retries EAGAIN after parking the caller.  This is
     * distinct from ECHILD, which means no matching child exists at all. */
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return found_child ? -LEONOS_EAGAIN : 0;
}

/**
 * @brief Bounded, always-NUL-terminated copy for task snapshot string fields.
 */
static void snapshot_name(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (src) {
        while (i + 1 < dst_len && src[i]) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

/**
 * @brief Fill out[] with up to capacity task snapshots, storing the tick count via tick.
 */
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick)
{
    uint64_t flags;
    uint32_t result;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    if (tick) {
        *tick = scheduler_ticks;
    }
    if (!out || capacity == 0) {
        result = task_count;
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return result;
    }
    uint32_t n = task_count < capacity ? task_count : capacity;
    for (uint32_t i = 0; i < n; ++i) {
        struct task_snapshot_info *dst = &out[i];
        dst->pid = tasks[i]->pid;
        dst->parent_pid = tasks[i]->parent_pid;
        dst->state = (uint32_t)tasks[i]->state;
        dst->kind = (uint32_t)tasks[i]->kind;
        dst->flags = tasks[i]->flags;
        dst->uid = tasks[i]->uid;
        dst->role = tasks[i]->role;
        dst->session_id = tasks[i]->session_id;
        dst->memory_kib = address_space_user_memory_kib(sched_task_as(tasks[i]));
        dst->cpu_ticks = tasks[i]->cpu_ticks;
        dst->priority = tasks[i]->priority;
        dst->pending_signals = tasks[i]->pending_signals;
        dst->wake_tick = tasks[i]->wake_tick;
        dst->entry = tasks[i]->entry;
        dst->cr3 = (*sched_task_as(tasks[i])).cr3;
        dst->affinity_mask = tasks[i]->affinity_mask;
        snapshot_name(dst->name, sizeof(dst->name), tasks[i]->name);
        snapshot_name(dst->username, sizeof(dst->username), tasks[i]->username);
    }
    result = n;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
    return result;
}

/**
 * @brief Set a task's uid/role/session/username/home from user, or clear it when user is empty.
 */
void sched_set_task_identity(uint32_t pid, const struct leonos_user_info *user,
                             uint32_t session_id)
{
    struct task *task = sched_find(pid);
    if (!task) {
        return;
    }
    task->flags &= ~TASK_FLAG_ELEVATED_ADMIN;
    if (!user || !user->uid) {
        task_credentials_prepare(task, 0, 0, 0, 0, 0);
        task_clear_identity(task);
        task_copy_cwd(task, "/");
        return;
    }
    task_credentials_prepare(task, user->uid, user->uid, user->uid, user->uid, 0);
    task->uid = user->uid;
    task->gid = user->uid;
    task->euid = user->uid;
    task->egid = user->uid;
    task->suid = user->uid;
    task->sgid = user->uid;
    task->fsuid = user->uid;
    task->fsgid = user->uid;
    if (user->uid == 0) {
        const uint64_t all = (UINT64_C(1) << (CAP_LAST_CAP + 1)) - 1;
        task->cap_effective = all;
        task->cap_permitted = all;
        task->cap_inheritable = 0;
    } else {
        task->cap_effective = 0;
        task->cap_permitted = 0;
        task->cap_inheritable = 0;
    }
    task->role = user->role;
    task->session_id = session_id;
    task_copy_identity_text(task->username, sizeof(task->username), user->username);
    task_copy_identity_text(task->home, sizeof(task->home), user->home);
    if (user->home[0]) {
        task_copy_cwd(task, user->home);
    }
}

/**
 * @brief Apply identity to the given task and its non-service children in a session.
 */
void sched_set_session_identity(uint32_t parent_pid, const struct leonos_user_info *user,
                                uint32_t session_id)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = tasks[i];
        if (((task->flags & TASK_FLAG_SERVICE) == 0 ||
             (task->flags & TASK_FLAG_WINDOW_SERVER)) &&
            (task->pid == parent_pid || task->parent_pid == parent_pid)) {
            sched_set_task_identity(task->pid, user, session_id);
        }
    }
}

/**
 * @brief Clear identity and reset cwd for every non-service task in this session.
 */
void sched_clear_session_identity(uint32_t session_id)
{
    if (!session_id) {
        return;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->session_id == session_id &&
            ((tasks[i]->flags & TASK_FLAG_SERVICE) == 0 ||
             (tasks[i]->flags & TASK_FLAG_WINDOW_SERVER))) {
            task_clear_identity(tasks[i]);
            task_copy_cwd(tasks[i], "/");
        }
    }
}

/**
 * @brief Return the next session id, wrapping past zero back to 1.
 */
uint32_t sched_next_session_id(void)
{
    if (next_session_id == 0) {
        next_session_id = 1;
    }
    return next_session_id++;
}

/**
 * @brief Print one console line per task with pid, identity, name, state, and flags.
 */
void sched_dump(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        console_printf("[ntclks] task[%u] pid=%u ppid=%u uid=%u role=%u name=%s state=%u kind=%u flags=0x%x\n",
                       i,
                       tasks[i]->pid,
                       tasks[i]->parent_pid,
                       tasks[i]->uid,
                       tasks[i]->role,
                       tasks[i]->name,
                       tasks[i]->state,
                       tasks[i]->kind,
                       tasks[i]->flags);
    }
}

/**
 * @brief Publish a controlling tty change to the process thread group.
 * @param pid Target thread ID.
 * @param pty_id New tty ID, or zero.
 */
void sched_set_controlling_pty(uint32_t pid, uint32_t pty_id)
{
    struct task *target = sched_find(pid);
    if (!target) return;
    for (uint32_t i = 0; i < task_count; ++i)
        if (tasks[i] && tasks[i]->tgid == target->tgid)
            tasks[i]->controlling_pty_id = pty_id;
}

/**
 * @brief Detach all processes when a privileged session steals a tty.
 * @param pty_id Controlling tty ID to clear.
 */
void sched_clear_controlling_pty(uint32_t pty_id)
{
    for (uint32_t i = 0; i < task_count; ++i)
        if (tasks[i] && tasks[i]->controlling_pty_id == pty_id)
            tasks[i]->controlling_pty_id = 0;
}
