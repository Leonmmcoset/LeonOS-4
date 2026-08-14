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

static struct task tasks[SCHED_TASK_MAX];
static uint32_t task_count;
static uint32_t next_pid = 1;
static uint32_t current_pid;
static uint32_t next_session_id = 1;
static uint64_t scheduler_ticks;
static uint64_t scheduler_busy_ticks;
static uint64_t scheduler_idle_ticks;

/**
 * @brief Coordinates the str eq operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the task copy name operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param name Input or output value used by this operation.
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
 * @brief Coordinates the task copy cwd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param cwd Input or output value used by this operation.
 */
static void task_copy_cwd(struct task *task, const char *cwd)
{
    size_t i = 0;
    if (!task) {
        return;
    }
    if (!cwd || !cwd[0]) {
        cwd = "0:/";
    }
    while (i + 1 < sizeof(task->cwd) && cwd[i]) {
        task->cwd[i] = cwd[i];
        ++i;
    }
    task->cwd[i] = 0;
}

/**
 * @brief Coordinates the task copy path operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param path LeonOS path consumed by this operation.
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
 * @brief Coordinates the task path basename operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the task copy identity text operation.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
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
 * @brief Coordinates the task clear identity operation.
 * @param task Task whose state or authority is inspected or updated.
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
 * @brief Coordinates the task copy identity from parent operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param parent Input or output value used by this operation.
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
 * @brief Coordinates the sched init operation.
 */
void sched_init(void)
{
    task_count = 0;
    next_pid = 1;
    current_pid = 0;
    next_session_id = 1;
    scheduler_ticks = 0;
    scheduler_busy_ticks = 0;
    scheduler_idle_ticks = 0;
    console_printf("[ntclks] scheduler initialized\n");
}

/**
 * @brief Coordinates the task zero operation.
 * @param task Task whose state or authority is inspected or updated.
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
 * @brief Allocates task slot.
 * @return Result, status, or value defined by this API.
 */
static struct task *alloc_task_slot(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_EXITED &&
            (!(tasks[i].flags & TASK_FLAG_WAITABLE_CHILD) || tasks[i].parent_pid == 0)) {
            sched_release_task_resources(&tasks[i]);
            task_zero(&tasks[i]);
            return &tasks[i];
        }
    }
    if (task_count >= SCHED_TASK_MAX) {
        return NULL;
    }
    return &tasks[task_count++];
}

/**
 * @brief Coordinates the sched create kernel task operation.
 * @param name Input or output value used by this operation.
 * @param entry Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_create_kernel_task(const char *name, uint64_t entry)
{
    struct task *task = alloc_task_slot();
    if (!task) {
        return 0;
    }
    task_zero(task);
    task->pid = next_pid++;
    task->parent_pid = 0;
    task->process_group = 0;
    task->process_session = 0;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = 0;
    task->wake_tick = 0;
    task->priority = 0;
    task->pending_signals = 0;
    task->rlimit_nofile = SCHED_TASK_FILE_MAX;
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
    task->kind = TASK_KIND_KERNEL;
    task->flags = 0;
    task->pty_id = 0;
    task_clear_identity(task);
    task_copy_cwd(task, "0:/");
    console_printf("[ntclks] task pid=%u name=%s entry=0x%llx\n",
                   task->pid, task->name, (unsigned long long)task->entry);
    return task->pid;
}

/**
 * @brief Coordinates the sched create user task operation.
 * @param name Input or output value used by this operation.
 * @param entry Input or output value used by this operation.
 * @param stack_top Input or output value used by this operation.
 * @param parent_pid Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_create_user_task(const char *name, uint64_t entry, uint64_t stack_top,
                                uint32_t parent_pid, uint32_t flags)
{
    struct task *task = alloc_task_slot();
    if (!task) {
        return 0;
    }
    task_zero(task);
    task->pid = next_pid++;
    task->parent_pid = parent_pid;
    task->process_group = task->pid;
    task->process_session = task->pid;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = stack_top;
    task->wake_tick = 0;
    task->priority = 0;
    task->pending_signals = 0;
    task->rlimit_nofile = SCHED_TASK_FILE_MAX;
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
        return 0;
    }
    task->frame.rip = entry;
    task->frame.cs = NTCLKS_USER_CS;
    task->frame.rflags = 0x202;
    task->frame.rsp = stack_top;
    task->frame.ss = NTCLKS_USER_DS;
    arch_fpu_task_init(task->fpu_state);
    task->state = TASK_READY;
    task->kind = TASK_KIND_USER;
    task->flags = flags;
    task_clear_identity(task);
    task_copy_cwd(task, "0:/");
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
        }
    }
    console_printf("[ntclks] task pid=%u ppid=%u name=%s user entry=0x%llx stack=0x%llx flags=0x%x\n",
                   task->pid,
                   task->parent_pid,
                   task->name,
                   (unsigned long long)task->entry,
                   (unsigned long long)task->stack_top,
                   task->flags);
    return task->pid;
}

/**
 * @brief Creates a runnable fork child with COW memory and a copied user register frame.
 * @param parent_frame Saved frame for the calling process; its child copy receives rax equal to zero.
 * @return Positive child PID for the parent, or a negative errno-style value on failure.
 */
int64_t sched_fork_current(const struct trap_frame *parent_frame)
{
    struct task *parent = sched_current_task();
    struct task *child;
    struct address_space empty_as = {0};
    uint32_t child_pid;
    if (!parent || !parent_frame || parent->kind != TASK_KIND_USER ||
        parent->state == TASK_EXITED || !parent->as.cr3) {
        return -22;
    }
    child = alloc_task_slot();
    if (!child) {
        return -12;
    }
    task_zero(child);
    *child = *parent;
    child->pid = next_pid++;
    child_pid = child->pid;
    child->parent_pid = parent->pid;
    child->as = empty_as;
    child->image = NULL;
    child->image_len = 0;
    child->flags &= ~(TASK_FLAG_RESOURCES_RELEASED | TASK_FLAG_PENDING_LOAD);
    child->flags |= TASK_FLAG_WAITABLE_CHILD | TASK_FLAG_STARTED;
    child->state = TASK_READY;
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
        return -12;
    }
    if (syscall_clone_task_files(parent, child) < 0) {
        address_space_destroy(&child->as);
        task_zero(child);
        child->state = TASK_EXITED;
        child->flags = TASK_FLAG_RESOURCES_RELEASED;
        return -12;
    }
    /* exec_argv and exec_envp are interior pointers, so rebuild them to
     * reference the child-owned packed string storage after the structure copy. */
    sched_set_task_exec_params(child_pid, parent->exec_argc, parent->exec_argv,
                               parent->exec_envc, parent->exec_envp,
                               parent->exec_data, parent->exec_data_len);
    console_printf("[ntclks] fork parent=%u child=%u cr3=0x%llx\n",
                   parent->pid, child_pid, (unsigned long long)child->as.cr3);
    return (int64_t)child_pid;
}

/**
 * @brief Coordinates the sched set task image operation.
 * @param pid Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param image_len Length, size, or element count associated with the operation.
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
 * @brief Coordinates the sched set task image node operation.
 * @param pid Input or output value used by this operation.
 * @param node Input or output value used by this operation.
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
 * @brief Coordinates the sched set task path operation.
 * @param pid Input or output value used by this operation.
 * @param path LeonOS path consumed by this operation.
 */
void sched_set_task_path(uint32_t pid, const char *path)
{
    task_copy_path(sched_find(pid), path);
}

/**
 * @brief Coordinates the sched set task exec params operation.
 * @param pid Input or output value used by this operation.
 * @param argc Input or output value used by this operation.
 * @param argv Input or output value used by this operation.
 * @param envc Input or output value used by this operation.
 * @param envp Input or output value used by this operation.
 * @param data Input or output value used by this operation.
 * @param data_len Length, size, or element count associated with the operation.
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
 * @brief Coordinates the sched create idle task operation.
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
    task_copy_cwd(task, "0:/");
}

/**
 * @brief Coordinates the sched set running operation.
 * @param pid Input or output value used by this operation.
 */
void sched_set_running(uint32_t pid)
{
    current_pid = pid;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == pid) {
            tasks[i].state = TASK_RUNNING;
        } else if (tasks[i].state == TASK_RUNNING) {
            tasks[i].state = TASK_READY;
        }
    }
}

/**
 * @brief Coordinates the sched exit operation.
 * @param pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 */
void sched_exit(uint32_t pid, uint64_t code)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == pid) {
            /* Exit paths such as SIGKILL and a failed lazy image load bypass
             * the normal SYS_exit handler.  Close their descriptors here so
             * pipe readers receive EOF once the final writer is gone. */
            syscall_release_task_files(&tasks[i]);
            tasks[i].state = TASK_EXITED;
            tasks[i].exit_code = code;
            tasks[i].child_event = TASK_CHILD_EVENT_NONE;
            tasks[i].stop_signal = 0;
            console_printf("[ntclks] scheduler task exited pid=%u name=%s code=%llu\n",
                           pid,
                           tasks[i].name,
                           (unsigned long long)code);
            break;
        }
    }
    inputm_destroy_owner(pid);
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].parent_pid == pid) {
            tasks[i].parent_pid = 0;
            tasks[i].flags &= ~TASK_FLAG_WAITABLE_CHILD;
        }
    }
    if (current_pid == pid) {
        current_pid = 0;
    }
}

/**
 * @brief Coordinates the sched release task resources operation.
 * @param task Task whose state or authority is inspected or updated.
 */
void sched_release_task_resources(struct task *task)
{
    if (!task || task->kind != TASK_KIND_USER ||
        (task->flags & TASK_FLAG_RESOURCES_RELEASED)) {
        return;
    }
    storage_drain_task_io(task->pid);
    syscall_release_task_files(task);
    address_space_destroy(&task->as);
    task->flags |= TASK_FLAG_RESOURCES_RELEASED;
}

/**
 * @brief Coordinates the sched on tick operation.
 */
void sched_on_tick(void)
{
    struct task *current;
    ++scheduler_ticks;
    current = sched_find(current_pid);
    if (current && current->state == TASK_RUNNING) {
        ++scheduler_busy_ticks;
        ++current->cpu_ticks;
    } else {
        ++scheduler_idle_ticks;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_BLOCKED && tasks[i].wake_tick &&
            tasks[i].wake_tick <= scheduler_ticks) {
            tasks[i].wake_tick = 0;
            tasks[i].wait_window_id = 0;
            tasks[i].state = TASK_READY;
        }
    }
}

/**
 * @brief Coordinates the sched tick count operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t sched_tick_count(void)
{
    return scheduler_ticks;
}

/**
 * @brief Coordinates the sched cpu ticks operation.
 * @param busy_ticks Input or output value used by this operation.
 * @param idle_ticks Input or output value used by this operation.
 */
void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks)
{
    if (busy_ticks) {
        *busy_ticks = scheduler_busy_ticks;
    }
    if (idle_ticks) {
        *idle_ticks = scheduler_idle_ticks;
    }
}

/**
 * @brief Coordinates the sched task counts operation.
 * @param out_task_count Caller-provided storage that receives output from this operation.
 * @param running_tasks Input or output value used by this operation.
 * @param ready_tasks Input or output value used by this operation.
 * @param sleeping_tasks Input or output value used by this operation.
 */
void sched_task_counts(uint32_t *out_task_count, uint32_t *running_tasks,
                       uint32_t *ready_tasks, uint32_t *sleeping_tasks)
{
    uint32_t running = 0;
    uint32_t ready = 0;
    uint32_t sleeping = 0;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_RUNNING) {
            ++running;
        } else if (tasks[i].state == TASK_READY) {
            ++ready;
        } else if (tasks[i].state == TASK_BLOCKED) {
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
}

/**
 * @brief Coordinates the sched current pid operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_current_pid(void)
{
    return current_pid;
}

/**
 * @brief Coordinates the sched find operation.
 * @param pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_find(uint32_t pid)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == pid) {
            return &tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the sched find by name operation.
 * @param name Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_find_by_name(const char *name)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (str_eq(tasks[i].name, name)) {
            return &tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the sched find by path operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_find_by_path(const char *path)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid && tasks[i].kind == TASK_KIND_USER &&
            tasks[i].state != TASK_EXITED && str_eq(tasks[i].path, path)) {
            return &tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the sched find by path basename operation.
 * @param basename Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_find_by_path_basename(const char *basename)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid && tasks[i].kind == TASK_KIND_USER &&
            tasks[i].state != TASK_EXITED &&
            str_eq(task_path_basename(tasks[i].path), basename)) {
            return &tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the sched find window server operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_find_window_server(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid && tasks[i].kind == TASK_KIND_USER &&
            tasks[i].state != TASK_EXITED &&
            (tasks[i].flags & TASK_FLAG_WINDOW_SERVER)) {
            return &tasks[i];
        }
    }
    return NULL;
}

/**
 * @brief Coordinates the sched current task operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_current_task(void)
{
    return sched_find(current_pid);
}

/**
 * @brief Coordinates the sched select next user operation.
 * @return Result, status, or value defined by this API.
 */
struct task *sched_select_next_user(void)
{
    uint32_t current_index = 0;
    struct task *best = NULL;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == current_pid) {
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
        if (tasks[i].kind != TASK_KIND_USER || tasks[i].state != TASK_READY) {
            continue;
        }
        if ((!tasks[i].entry && !(tasks[i].image && tasks[i].image_len) &&
             !(tasks[i].flags & TASK_FLAG_PENDING_LOAD)) ||
            !tasks[i].stack_top || !tasks[i].as.cr3) {
            continue;
        }
        if (!best || tasks[i].priority < best->priority) {
            best = &tasks[i];
        }
    }
    return best;
}

/**
 * @brief Coordinates the sched task frame operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
struct trap_frame *sched_task_frame(struct task *task)
{
    return task ? &task->frame : NULL;
}

/**
 * @brief Coordinates the sched task cr3 operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
uint64_t sched_task_cr3(struct task *task)
{
    return task ? task->as.cr3 : 0;
}

/**
 * @brief Coordinates the sched mark ready operation.
 * @param pid Input or output value used by this operation.
 */
void sched_mark_ready(uint32_t pid)
{
    struct task *task = sched_find(pid);
    if (!task || task->state == TASK_EXITED) {
        return;
    }
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->state = TASK_READY;
}

/**
 * @brief Coordinates the sched sleep current until operation.
 * @param wake_tick Input or output value used by this operation.
 */
void sched_sleep_current_until(uint64_t wake_tick)
{
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED) {
        return;
    }
    task->wait_window_id = 0;
    task->wake_tick = wake_tick;
    task->state = TASK_BLOCKED;
}

/**
 * @brief Coordinates the sched wait current for window event operation.
 * @param window_id Input or output value used by this operation.
 * @param wake_tick Input or output value used by this operation.
 */
void sched_wait_current_for_window_event(uint32_t window_id, uint64_t wake_tick)
{
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED || !window_id) {
        return;
    }
    task->wait_window_id = window_id;
    task->wake_tick = wake_tick;
    task->state = TASK_BLOCKED;
}

/**
 * @brief Coordinates the sched wake window event operation.
 * @param pid Input or output value used by this operation.
 * @param window_id Input or output value used by this operation.
 */
void sched_wake_window_event(uint32_t pid, uint32_t window_id)
{
    struct task *task = sched_find(pid);
    if (!task || task->state != TASK_BLOCKED || task->wait_window_id != window_id) {
        return;
    }
    task->wake_tick = 0;
    task->wait_window_id = 0;
    task->state = TASK_READY;
}

/**
 * @brief Coordinates the sched kill user task operation.
 * @param pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_task(uint32_t pid, uint64_t code)
{
    struct task *task = sched_find(pid);
    if (!task || task->pid == 0) {
        return -2;
    }
    if (task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        (task->flags & TASK_FLAG_SERVICE) || pid == current_pid) {
        return -1;
    }
    sched_exit(pid, code);
    return 0;
}

/**
 * @brief Sends a supported process-control signal to a user task.
 * @param pid Target process identifier.
 * @param signal_number POSIX signal number.
 * @return Zero on success or a negative scheduler error.
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
    if (signal_number == 19) { /* SIGCONT */
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
 * @brief Sends a signal to all members of a process group owned by the caller.
 * @param sender_pid Process issuing the request.
 * @param process_group Target process group.
 * @param signal_number POSIX signal number.
 * @return Number of signalled tasks, or a negative scheduler error.
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
        struct task *task = &tasks[i];
        if (task->pid == 0 || task->kind != TASK_KIND_USER ||
            task->state == TASK_EXITED || task->process_group != process_group) {
            continue;
        }
        if (sender->uid != 0 && sender->uid != task->uid) {
            return -1;
        }
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = &tasks[i];
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
 * @brief Changes the process group for a task controlled by the caller.
 * @param caller_pid Process issuing the request.
 * @param pid Target task, or zero for the caller.
 * @param process_group Target group, or zero to create one led by the target.
 * @return Zero on success or a negative scheduler error.
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
            const struct task *member = &tasks[i];
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
 * @brief Returns the process group assigned to a task.
 * @param pid Task identifier, or zero for the current task.
 * @return Process-group identifier or a negative scheduler error.
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
 * @brief Creates a process session whose leader and initial group are the caller.
 * @param pid Calling task identifier.
 * @return New session identifier or a negative scheduler error.
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
        const struct task *member = &tasks[i];
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
 * @brief Checks whether an attached task belongs to a process group.
 * @param process_group Process group to inspect.
 * @param pty_id PTY to match.
 * @return Non-zero if a matching live task exists.
 */
int sched_process_group_has_pty(uint32_t process_group, uint32_t pty_id)
{
    if (!process_group || !pty_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        const struct task *task = &tasks[i];
        if (task->pid != 0 && task->kind == TASK_KIND_USER &&
            task->state != TASK_EXITED && task->process_group == process_group &&
            task->pty_id == pty_id) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Returns the POSIX-style process session for a task.
 * @param pid Task identifier.
 * @return Session identifier or a negative scheduler error.
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
 * @brief Reads or updates a task's nice-style priority.
 * @param pid Target process identifier.
 * @param priority New priority when set is non-zero.
 * @param set Non-zero to update, zero to read.
 * @return Priority on success or a negative error.
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
 * @brief Coordinates the sched kill user tasks for pty operation.
 * @param pty_id Input or output value used by this operation.
 * @param keep_pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid,
                                  uint64_t code)
{
    int killed = 0;
    if (!pty_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = &tasks[i];
        if (task->pid == 0 || task->pid == keep_pid ||
            task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
            (task->flags & TASK_FLAG_SERVICE) || task->pty_id != pty_id) {
            continue;
        }
        sched_exit(task->pid, code);
        task->pty_id = 0;
        for (uint32_t fd = 0; fd < SCHED_TASK_PTY_FD_MAX; ++fd) {
            task->pty_fds[fd] = (struct task_pty_fd){0};
        }
        sched_release_task_resources(task);
        ++killed;
    }
    return killed;
}

/**
 * @brief Coordinates the sched kill user tasks for logout operation.
 * @param uid Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 * @param keep_pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                     uint32_t keep_pid, uint64_t code)
{
    int killed = 0;
    if (!uid || !session_id) {
        return 0;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = &tasks[i];
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
 * @brief Coordinates the sched wait reap operation.
 * @param waiter_pid Input or output value used by this operation.
 * @param wanted_pid Input or output value used by this operation.
 * @param options waitpid option bit mask.
 * @param status Destination for the encoded wait status.
 * @return Result, status, or value defined by this API.
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
        struct task *task = &tasks[i];
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
        if (task->state == TASK_EXITED) {
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
            task_copy_cwd(task, "0:/");
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
 * @brief Coordinates the snapshot name operation.
 * @param dst Input or output value used by this operation.
 * @param dst_len Length, size, or element count associated with the operation.
 * @param src Input or output value used by this operation.
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
 * @brief Coordinates the sched snapshot operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param tick Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick)
{
    if (tick) {
        *tick = scheduler_ticks;
    }
    if (!out || capacity == 0) {
        return task_count;
    }
    uint32_t n = task_count < capacity ? task_count : capacity;
    for (uint32_t i = 0; i < n; ++i) {
        struct task_snapshot_info *dst = &out[i];
        dst->pid = tasks[i].pid;
        dst->parent_pid = tasks[i].parent_pid;
        dst->state = (uint32_t)tasks[i].state;
        dst->kind = (uint32_t)tasks[i].kind;
        dst->flags = tasks[i].flags;
        dst->uid = tasks[i].uid;
        dst->role = tasks[i].role;
        dst->session_id = tasks[i].session_id;
        dst->memory_kib = address_space_user_memory_kib(&tasks[i].as);
        dst->cpu_ticks = tasks[i].cpu_ticks;
        dst->priority = tasks[i].priority;
        dst->pending_signals = tasks[i].pending_signals;
        dst->wake_tick = tasks[i].wake_tick;
        dst->entry = tasks[i].entry;
        dst->cr3 = tasks[i].as.cr3;
        snapshot_name(dst->name, sizeof(dst->name), tasks[i].name);
        snapshot_name(dst->username, sizeof(dst->username), tasks[i].username);
    }
    return n;
}

/**
 * @brief Coordinates the sched set task identity operation.
 * @param pid Input or output value used by this operation.
 * @param user Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
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
        task_copy_cwd(task, "0:/");
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
 * @brief Coordinates the sched set session identity operation.
 * @param parent_pid Input or output value used by this operation.
 * @param user Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 */
void sched_set_session_identity(uint32_t parent_pid, const struct leonos_user_info *user,
                                uint32_t session_id)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = &tasks[i];
        if (((task->flags & TASK_FLAG_SERVICE) == 0 ||
             (task->flags & TASK_FLAG_WINDOW_SERVER)) &&
            (task->pid == parent_pid || task->parent_pid == parent_pid)) {
            sched_set_task_identity(task->pid, user, session_id);
        }
    }
}

/**
 * @brief Coordinates the sched clear session identity operation.
 * @param session_id Input or output value used by this operation.
 */
void sched_clear_session_identity(uint32_t session_id)
{
    if (!session_id) {
        return;
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].session_id == session_id &&
            ((tasks[i].flags & TASK_FLAG_SERVICE) == 0 ||
             (tasks[i].flags & TASK_FLAG_WINDOW_SERVER))) {
            task_clear_identity(&tasks[i]);
            task_copy_cwd(&tasks[i], "0:/");
        }
    }
}

/**
 * @brief Coordinates the sched next session id operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_next_session_id(void)
{
    if (next_session_id == 0) {
        next_session_id = 1;
    }
    return next_session_id++;
}

/**
 * @brief Coordinates the sched dump operation.
 */
void sched_dump(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        console_printf("[ntclks] task[%u] pid=%u ppid=%u uid=%u role=%u name=%s state=%u kind=%u flags=0x%x\n",
                       i,
                       tasks[i].pid,
                       tasks[i].parent_pid,
                       tasks[i].uid,
                       tasks[i].role,
                       tasks[i].name,
                       tasks[i].state,
                       tasks[i].kind,
                       tasks[i].flags);
    }
}
