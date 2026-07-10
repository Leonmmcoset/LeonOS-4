#include <ntclks/console.h>
#include <ntclks/arch.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>

static struct task tasks[SCHED_TASK_MAX];
static uint32_t task_count;
static uint32_t next_pid = 1;
static uint32_t current_pid;
static uint32_t next_session_id = 1;
static uint64_t scheduler_ticks;
static uint64_t scheduler_busy_ticks;
static uint64_t scheduler_idle_ticks;

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

static void task_clear_identity(struct task *task)
{
    if (!task) {
        return;
    }
    task->uid = 0;
    task->role = LEONOS_AUTH_ROLE_NONE;
    task->session_id = 0;
    task->username[0] = 0;
    task->home[0] = 0;
}

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

static void task_zero(struct task *task)
{
    if (!task) {
        return;
    }
    for (size_t i = 0; i < sizeof(*task); ++i) {
        ((uint8_t *)task)[i] = 0;
    }
}

static struct task *alloc_task_slot(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_EXITED) {
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

uint32_t sched_create_kernel_task(const char *name, uint64_t entry)
{
    struct task *task = alloc_task_slot();
    if (!task) {
        return 0;
    }
    task_zero(task);
    task->pid = next_pid++;
    task->parent_pid = 0;
    task_copy_name(task, name);
    task->entry = entry;
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
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = stack_top;
    task->wake_tick = 0;
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

void sched_set_task_image(uint32_t pid, const void *image, size_t image_len)
{
    struct task *task = sched_find(pid);
    if (!task) {
        return;
    }
    task->image = image;
    task->image_len = image_len;
}

void sched_set_task_path(uint32_t pid, const char *path)
{
    task_copy_path(sched_find(pid), path);
}

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

void sched_create_idle_task(void)
{
    struct task *task = alloc_task_slot();
    if (!task) {
        return;
    }
    task_zero(task);
    task->pid = 0;
    task->parent_pid = 0;
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

void sched_exit(uint32_t pid, uint64_t code)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == pid) {
            tasks[i].state = TASK_EXITED;
            tasks[i].exit_code = code;
            console_printf("[ntclks] scheduler task exited pid=%u name=%s code=%llu\n",
                           pid,
                           tasks[i].name,
                           (unsigned long long)code);
            break;
        }
    }
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].parent_pid == pid) {
            tasks[i].parent_pid = 0;
        }
    }
    if (current_pid == pid) {
        current_pid = 0;
    }
}

void sched_release_task_resources(struct task *task)
{
    if (!task || task->kind != TASK_KIND_USER ||
        (task->flags & TASK_FLAG_RESOURCES_RELEASED)) {
        return;
    }
    address_space_destroy(&task->as);
    task->flags |= TASK_FLAG_RESOURCES_RELEASED;
}

void sched_on_tick(void)
{
    struct task *current;
    ++scheduler_ticks;
    current = sched_find(current_pid);
    if (current && current->state == TASK_RUNNING) {
        ++scheduler_busy_ticks;
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

uint64_t sched_tick_count(void)
{
    return scheduler_ticks;
}

void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks)
{
    if (busy_ticks) {
        *busy_ticks = scheduler_busy_ticks;
    }
    if (idle_ticks) {
        *idle_ticks = scheduler_idle_ticks;
    }
}

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

uint32_t sched_current_pid(void)
{
    return current_pid;
}

struct task *sched_find(uint32_t pid)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == pid) {
            return &tasks[i];
        }
    }
    return NULL;
}

struct task *sched_find_by_name(const char *name)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        if (str_eq(tasks[i].name, name)) {
            return &tasks[i];
        }
    }
    return NULL;
}

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

struct task *sched_current_task(void)
{
    return sched_find(current_pid);
}

struct task *sched_select_next_user(void)
{
    uint32_t current_index = 0;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].pid == current_pid) {
            current_index = i;
            break;
        }
    }

    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t n = 1; n <= task_count; ++n) {
            uint32_t i = (current_index + n) % task_count;
            if (tasks[i].kind != TASK_KIND_USER || tasks[i].state != TASK_READY) {
                continue;
            }
            if (pass == 0 && (tasks[i].flags & TASK_FLAG_SERVICE)) {
                continue;
            }
            if ((!tasks[i].entry && !(tasks[i].image && tasks[i].image_len)) ||
                !tasks[i].stack_top || !tasks[i].as.cr3) {
                continue;
            }
            return &tasks[i];
        }
    }
    return NULL;
}

struct trap_frame *sched_task_frame(struct task *task)
{
    return task ? &task->frame : NULL;
}

uint64_t sched_task_cr3(struct task *task)
{
    return task ? task->as.cr3 : 0;
}

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

int64_t sched_wait_reap(uint32_t waiter_pid, uint32_t wanted_pid, uint64_t *exit_code)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        struct task *task = &tasks[i];
        if (task->pid == 0 || task->kind != TASK_KIND_USER) {
            continue;
        }
        if (task->parent_pid != waiter_pid && waiter_pid != 0) {
            continue;
        }
        if (wanted_pid != 0 && task->pid != wanted_pid) {
            continue;
        }
        if (task->state == TASK_EXITED) {
            uint32_t pid = task->pid;
            if (exit_code) {
                *exit_code = task->exit_code;
            }
            sched_release_task_resources(task);
            task->parent_pid = 0;
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
    }
    return 0;
}

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
        dst->wake_tick = tasks[i].wake_tick;
        dst->entry = tasks[i].entry;
        dst->cr3 = tasks[i].as.cr3;
        snapshot_name(dst->name, sizeof(dst->name), tasks[i].name);
        snapshot_name(dst->username, sizeof(dst->username), tasks[i].username);
    }
    return n;
}

void sched_set_task_identity(uint32_t pid, const struct leonos_user_info *user,
                             uint32_t session_id)
{
    struct task *task = sched_find(pid);
    if (!task) {
        return;
    }
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

uint32_t sched_next_session_id(void)
{
    if (next_session_id == 0) {
        next_session_id = 1;
    }
    return next_session_id++;
}

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
