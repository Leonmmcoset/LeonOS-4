/*
 * LeonOS scheduler implementation: manages kernel and Ring-3 task state.
 * Selects runnable tasks, handles waits/exits, and switches address spaces.
 */
#include <ntclks/console.h>
#include <ntclks/arch.h>
#include <ntclks/inputm.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/heap.h>
#include <ntclks/lock.h>
#include <ntclks/smp.h>

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

/* A READY task can still be owned by the CPU that just saved its interrupt
 * frame.  Keep that reservation until the owner either reclaims or replaces
 * it; otherwise another CPU can select the same address space concurrently. */
#define SCHED_CPU_NONE UINT32_MAX

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
    return task && frame && task->as.cr3 && task->stack_low && task->stack_top &&
           frame->rip >= NTCLKS_USER_BASE && frame->rip < NTCLKS_USER_TOP &&
           frame->rsp >= task->stack_low && frame->rsp <= task->stack_top &&
           frame->cs == NTCLKS_USER_CS && frame->ss == NTCLKS_USER_DS &&
           (frame->rflags & (1ULL << 9));
}

struct task_vma *sched_task_vma_at(struct task *task, uint32_t index)
{
    if (!task) {
        return NULL;
    }
    if (index < SCHED_TASK_VMA_MAX) {
        return &task->vmas[index];
    }
    index -= SCHED_TASK_VMA_MAX;
    if (index >= task->vma_extra_count) {
        uint32_t wanted = index + 1u;
        uint32_t capacity = task->vma_extra_capacity ? task->vma_extra_capacity : 16u;
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
        for (uint32_t i = 0; i < task->vma_extra_count; ++i) {
            replacement[i] = task->vma_extra[i];
        }
        if (task->vma_extra) {
            kernel_free(task->vma_extra);
        }
        task->vma_extra = replacement;
        task->vma_extra_capacity = capacity;
        task->vma_extra_count = wanted;
    }
    return &task->vma_extra[index];
}

uint32_t sched_task_vma_capacity(const struct task *task)
{
    return task ? SCHED_TASK_VMA_MAX + task->vma_extra_count : 0;
}

void sched_task_vma_release(struct task *task)
{
    if (!task || !task->vma_extra) {
        return;
    }
    kernel_free(task->vma_extra);
    task->vma_extra = NULL;
    task->vma_extra_count = 0;
    task->vma_extra_capacity = 0;
}

struct task_file *sched_task_file_at(struct task *task, uint32_t index)
{
    if (!task) {
        return NULL;
    }
    if (index < SCHED_TASK_FILE_MAX) {
        return &task->files[index];
    }
    index -= SCHED_TASK_FILE_MAX;
    if (index >= task->file_extra_count) {
        uint32_t wanted = index + 1u;
        uint32_t capacity = task->file_extra_capacity ? task->file_extra_capacity : 16u;
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
        for (uint32_t i = 0; i < task->file_extra_count; ++i) {
            replacement[i] = task->file_extra[i];
        }
        if (task->file_extra) {
            kernel_free(task->file_extra);
        }
        task->file_extra = replacement;
        task->file_extra_capacity = capacity;
        task->file_extra_count = wanted;
    }
    return &task->file_extra[index];
}

uint32_t sched_task_file_capacity(const struct task *task)
{
    return task ? SCHED_TASK_FILE_MAX + task->file_extra_count : 0;
}

void sched_task_file_release(struct task *task)
{
    if (!task || !task->file_extra) {
        return;
    }
    kernel_free(task->file_extra);
    task->file_extra = NULL;
    task->file_extra_count = 0;
    task->file_extra_capacity = 0;
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
    while (i + 1 < sizeof(task->cwd) && cwd[i]) {
        task->cwd[i] = cwd[i];
        ++i;
    }
    task->cwd[i] = 0;
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
    task->uid = 0;
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
            (!(tasks[i]->flags & TASK_FLAG_WAITABLE_CHILD) || tasks[i]->parent_pid == 0)) {
            sched_release_task_resources(tasks[i]);
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
    task->rlimit_nofile = SCHED_TASK_FILE_LIMIT;
    task->rlimit_as = 0;
    task->wait_window_id = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    for (size_t i = 0; i < sizeof(task->as); ++i) {
        ((uint8_t *)&task->as)[i] = 0;
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
    task->parent_pid = parent_pid;
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
    task->rlimit_nofile = SCHED_TASK_FILE_LIMIT;
    task->rlimit_as = 0;
    task->wait_window_id = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    if (!address_space_create(&task->as) ||
        !address_space_map_user_stack(&task->as, stack_top)) {
        address_space_destroy(&task->as);
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
            task_copy_cwd(task, parent->cwd);
            task_copy_identity_from_parent(task, parent);
            task->priority = parent->priority;
            task->rlimit_nofile = parent->rlimit_nofile;
            task->rlimit_as = parent->rlimit_as;
            task->process_group = parent->process_group;
            task->process_session = parent->process_session;
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
int64_t sched_fork_current(const struct trap_frame *parent_frame)
{
    uint64_t lock_flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &lock_flags);
    struct task *parent = sched_current_task();
    struct task *child;
    struct address_space empty_as = {0};
    uint32_t child_pid;
    if (!parent || !parent_frame || parent->kind != TASK_KIND_USER ||
        parent->state == TASK_EXITED || !parent->as.cr3) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -22;
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
    child->pid = child_pid;
    child->parent_pid = parent->pid;
    child->as = empty_as;
    child->image = NULL;
    child->image_len = 0;
    /**
 * @brief Service and window-server authority belongs to the launched image, not to an arbitrary child created by that process.
 */
    child->flags &= ~(TASK_FLAG_RESOURCES_RELEASED | TASK_FLAG_PENDING_LOAD |
                      TASK_FLAG_SERVICE | TASK_FLAG_WINDOW_SERVER);
    child->flags |= TASK_FLAG_WAITABLE_CHILD | TASK_FLAG_STARTED;
    child->state = TASK_READY;
    child->running_cpu = SCHED_CPU_NONE;
    child->wake_tick = 0;
    child->wait_window_id = 0;
    child->exit_code = 0;
    child->cpu_ticks = 0;
    child->pending_signals = 0;
    child->frame = *parent_frame;
    child->frame.rax = 0;
    task_copy_name(child, parent->name);
    if (!address_space_clone_cow(&parent->as, &child->as)) {
        task_zero(child);
        child->state = TASK_EXITED;
        child->flags = TASK_FLAG_RESOURCES_RELEASED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -12;
    }
    child->vma_extra = NULL;
    child->vma_extra_count = 0;
    child->vma_extra_capacity = 0;
    if (parent->vma_extra_count) {
        for (uint32_t i = 0; i < parent->vma_extra_count; ++i) {
            struct task_vma *dst = sched_task_vma_at(child, SCHED_TASK_VMA_MAX + i);
            if (!dst) {
                address_space_destroy(&child->as);
                sched_task_vma_release(child);
                task_zero(child);
                child->state = TASK_EXITED;
                child->flags = TASK_FLAG_RESOURCES_RELEASED;
                kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
                return -12;
            }
            *dst = parent->vma_extra[i];
        }
    }
    child->file_extra = NULL;
    child->file_extra_count = 0;
    child->file_extra_capacity = 0;
    for (uint32_t i = 0; i < parent->file_extra_count; ++i) {
        struct task_file *dst = sched_task_file_at(child, SCHED_TASK_FILE_MAX + i);
        if (!dst) {
            address_space_destroy(&child->as);
            sched_task_vma_release(child);
            sched_task_file_release(child);
            task_zero(child);
            child->state = TASK_EXITED;
            child->flags = TASK_FLAG_RESOURCES_RELEASED;
            kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
            return -12;
        }
        *dst = parent->file_extra[i];
    }
    if (syscall_clone_task_files(parent, child) < 0) {
        address_space_destroy(&child->as);
        sched_task_vma_release(child);
        sched_task_file_release(child);
        task_zero(child);
        child->state = TASK_EXITED;
        child->flags = TASK_FLAG_RESOURCES_RELEASED;
        kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
        return -12;
    }
    /**
 * @brief exec_argv and exec_envp are interior pointers, so rebuild them to reference the child-owned packed string storage after the structure copy.
 */
    sched_set_task_exec_params(child_pid, parent->exec_argc, parent->exec_argv,
                               parent->exec_envc, parent->exec_envp,
                               parent->exec_data, parent->exec_data_len);
    console_printf("[ntclks] fork parent=%u child=%u cr3=0x%llx\n",
                   parent->pid, child_pid, (unsigned long long)child->as.cr3);
    kernel_spin_unlock_irqrestore(&scheduler_lock, lock_flags);
    return (int64_t)child_pid;
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
    for (size_t i = 0; i < sizeof(task->as); ++i) {
        ((uint8_t *)&task->as)[i] = 0;
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
        selected->state == TASK_EXITED || !selected->as.cr3 ||
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

    /* Publish the terminal state atomically, but preserve running_cpu until
     * its owner reaches a scheduling boundary.  A remote CPU may still be
     * executing this task's user frame when a terminal/PTY destroys it. */
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid) {
            tasks[i]->state = TASK_EXITED;
            tasks[i]->exit_code = code;
            tasks[i]->child_event = TASK_CHILD_EVENT_NONE;
            tasks[i]->stop_signal = 0;
            console_printf("[ntclks] scheduler task exited pid=%u name=%s code=%llu\n",
                           pid,
                           tasks[i]->name,
                           (unsigned long long)code);
            break;
        }
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->parent_pid == pid) {
            tasks[i]->parent_pid = 0;
            tasks[i]->flags &= ~TASK_FLAG_WAITABLE_CHILD;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
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
    syscall_release_task_files(task);
    address_space_destroy(&task->as);
    sched_task_vma_release(task);
    sched_task_file_release(task);
    inputm_destroy_owner(task->pid);
    task->flags |= TASK_FLAG_RESOURCES_RELEASED;
}

/**
 * @brief Advance scheduler time, credit busy/idle ticks, and wake blocked tasks past their deadline.
 */
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
        if (tasks[i]->state == TASK_BLOCKED && tasks[i]->wake_tick &&
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
 * @brief Return the live user task flagged as the window server, or NULL.
 */
struct task *sched_find_window_server(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid && tasks[i]->kind == TASK_KIND_USER &&
            tasks[i]->state != TASK_EXITED &&
            (tasks[i]->flags & TASK_FLAG_WINDOW_SERVER)) {
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
        if (sched_path_uses_volume(task->cwd, volume_id) ||
            sched_path_uses_volume(task->path, volume_id) ||
            task->image_node.volume_id == volume_id) {
            return true;
        }
        for (uint32_t fd = 0; fd < sched_task_file_capacity(task); ++fd) {
            const struct task_file *file = sched_task_file_at((struct task *)task, fd);
            if (file && file->used && file->node.volume_id == volume_id) {
                return true;
            }
        }
        for (uint32_t fd = 0; fd < SCHED_TASK_STDIO_MAX; ++fd) {
            if (task->stdio_files[fd].used && task->stdio_files[fd].node.volume_id == volume_id) {
                return true;
            }
        }
        for (uint32_t vma = 0; vma < SCHED_TASK_VMA_MAX; ++vma) {
            if (task->vmas[vma].used &&
                (task->vmas[vma].flags & TASK_VMA_FLAG_FILE) &&
                task->vmas[vma].file_node.volume_id == volume_id) {
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

    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    pid = current_pid[cpu];
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i]->pid == pid && tasks[i]->state == TASK_EXITED) {
            if (tasks[i]->running_cpu == cpu ||
                tasks[i]->running_cpu == SCHED_CPU_NONE) {
                tasks[i]->running_cpu = SCHED_CPU_NONE;
                current_pid[cpu] = 0;
            }
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
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
            (tasks[i]->running_cpu != SCHED_CPU_NONE && tasks[i]->running_cpu != scheduler_cpu_index())) {
            continue;
        }
        if (!task_cpu_allowed(tasks[i], scheduler_cpu_index())) {
            continue;
        }
        if ((!tasks[i]->entry && !(tasks[i]->image && tasks[i]->image_len) &&
             !(tasks[i]->flags & TASK_FLAG_PENDING_LOAD)) ||
            !tasks[i]->stack_top || !tasks[i]->as.cr3) {
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
        (task->running_cpu != SCHED_CPU_NONE && task->running_cpu != cpu) ||
        !task_cpu_allowed(task, cpu) ||
        !task_frame_valid(task, &task->frame)) {
        task = NULL;
    } else {
        task->state = TASK_RUNNING;
        task->running_cpu = cpu;
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
                           (unsigned long long)task->as.cr3);
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
    return task ? task->as.cr3 : 0;
}

/**
 * @brief Wake a non-exited task: clear its wait state and set it READY.
 */
void sched_mark_ready(uint32_t pid)
{
    uint64_t flags;
    uint32_t cpu = scheduler_cpu_index();
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_find(pid);
    if (!task || task->state == TASK_EXITED) {
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
 * @brief Block the current task until window_id gets an event or wake_tick elapses.
 */
void sched_wait_current_for_window_event(uint32_t window_id, uint64_t wake_tick)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED || !window_id) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    task->wait_window_id = window_id;
    task->wake_tick = wake_tick;
    task->state = TASK_BLOCKED;
    kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
}

/**
 * @brief Wake a BLOCKED task only when it is waiting for exactly this window event.
 */
void sched_wake_window_event(uint32_t pid, uint32_t window_id)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&scheduler_lock, &flags);
    struct task *task = sched_find(pid);
    if (!task || task->state != TASK_BLOCKED || task->wait_window_id != window_id) {
        kernel_spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->state = TASK_READY;
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
        task->state == TASK_EXITED || signal_number < 0 || signal_number >= 32) {
        return -1;
    }
    if (signal_number == 0) {
        return 0;
    }
    task->pending_signals |= 1u << (uint32_t)signal_number;
    if (signal_number == 17 || signal_number == 18) { /* SIGSTOP or SIGTSTP */
        task->wake_tick = 0;
        task->wait_window_id = 0;
        task->state = TASK_STOPPED;
        task->stop_signal = (uint32_t)signal_number;
        task->child_event = TASK_CHILD_EVENT_STOPPED;
        return 0;
    }
    if (signal_number == 19) { /**
 * @brief SIGCONT
 */
        task->pending_signals &= ~((1u << 17) | (1u << 18));
        if (task->state == TASK_STOPPED) {
            task->wake_tick = 0;
            task->wait_window_id = 0;
            task->state = TASK_READY;
            task->stop_signal = 0;
            task->child_event = TASK_CHILD_EVENT_CONTINUED;
        }
        return 0;
    }
    if (signal_number == 1 || signal_number == 2 || signal_number == 3 || signal_number == 9 ||
        signal_number == 15) {
        task->exit_signal = (uint32_t)signal_number;
        sched_exit(pid, (uint64_t)(128 + signal_number));
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
        if (sched_signal_user_task(task->pid, signal_number) == 0) {
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
int64_t sched_get_process_group(uint32_t pid)
{
    struct task *task;
    if (!pid) {
        pid = sched_current_pid();
    }
    task = sched_find(pid);
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        !task->process_group) {
        return -2;
    }
    return task->process_group;
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
    return pid;
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
    struct task *task = sched_find(pid);
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        !task->process_session) {
        return -2;
    }
    return task->process_session;
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
    struct task *waiter = sched_find(waiter_pid);
    int found_child = 0;
    uint32_t wanted_group = 0;
    if (!waiter) {
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
        if (task->parent_pid != waiter_pid && waiter_pid != 0) {
            continue;
        }
        if (wanted_pid > 0 && task->pid != (uint32_t)wanted_pid) {
            continue;
        }
        if (wanted_group && task->process_group != wanted_group) {
            continue;
        }
        found_child = 1;
        if (task->state == TASK_EXITED && task->running_cpu == SCHED_CPU_NONE) {
            uint32_t pid = task->pid;
            if (status) {
                *status = task->exit_signal
                              ? (int)(task->exit_signal & 0x7fU)
                              : (int)((task->exit_code & 0xffU) << 8);
            }
            sched_release_task_resources(task);
            task->parent_pid = 0;
            task->flags &= ~TASK_FLAG_WAITABLE_CHILD;
            task_copy_name(task, "reaped");
            task->image = NULL;
            task->image_len = 0;
            task->pty_id = 0;
            task_clear_identity(task);
            task_copy_cwd(task, "/");
            for (size_t j = 0; j < SCHED_TASK_FILE_MAX; ++j) {
                task->files[j].used = 0;
                task->files[j].offset = 0;
                task->files[j].aux = 0;
            }
            console_printf("[ntclks] scheduler wait reaped pid=%u by pid=%u\n", pid, waiter_pid);
            return pid;
        }
        if (task->child_event == TASK_CHILD_EVENT_STOPPED && (options & 2U)) {
            if (status) {
                *status = (int)(((task->stop_signal & 0xffU) << 8) | 0x7fU);
            }
            task->child_event = TASK_CHILD_EVENT_NONE;
            return task->pid;
        }
        if (task->child_event == TASK_CHILD_EVENT_CONTINUED && (options & 4U)) {
            if (status) {
                *status = 0xffff;
            }
            task->child_event = TASK_CHILD_EVENT_NONE;
            return task->pid;
        }
    }
    /* The syscall trap retries EAGAIN after parking the caller.  This is
     * distinct from ECHILD, which means no matching child exists at all. */
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
        dst->memory_kib = address_space_user_memory_kib(&tasks[i]->as);
        dst->cpu_ticks = tasks[i]->cpu_ticks;
        dst->priority = tasks[i]->priority;
        dst->pending_signals = tasks[i]->pending_signals;
        dst->wake_tick = tasks[i]->wake_tick;
        dst->entry = tasks[i]->entry;
        dst->cr3 = tasks[i]->as.cr3;
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
        task_clear_identity(task);
        task_copy_cwd(task, "/");
        return;
    }
    task->uid = user->uid;
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
