#include <ntclks/console.h>
#include <ntclks/arch.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>

static struct task tasks[32];
static uint32_t task_count;
static uint32_t next_pid = 1;
static uint32_t current_pid;
static uint64_t scheduler_ticks;

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

void sched_init(void)
{
    task_count = 0;
    next_pid = 1;
    current_pid = 0;
    scheduler_ticks = 0;
    console_printf("[ntclks] scheduler initialized\n");
}

uint32_t sched_create_kernel_task(const char *name, uint64_t entry)
{
    if (task_count >= 32) {
        return 0;
    }
    struct task *task = &tasks[task_count++];
    task->pid = next_pid++;
    task->parent_pid = 0;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = 0;
    task->wake_tick = 0;
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
    console_printf("[ntclks] task pid=%u name=%s entry=0x%llx\n",
                   task->pid, task->name, (unsigned long long)task->entry);
    return task->pid;
}

uint32_t sched_create_user_task(const char *name, uint64_t entry, uint64_t stack_top,
                                uint32_t parent_pid, uint32_t flags)
{
    if (task_count >= 32) {
        return 0;
    }
    struct task *task = &tasks[task_count++];
    for (size_t i = 0; i < sizeof(*task); ++i) {
        ((uint8_t *)task)[i] = 0;
    }
    task->pid = next_pid++;
    task->parent_pid = parent_pid;
    task_copy_name(task, name);
    task->entry = entry;
    task->stack_top = stack_top;
    task->wake_tick = 0;
    task->exit_code = 0;
    task->image = NULL;
    task->image_len = 0;
    if (!address_space_create(&task->as) ||
        !address_space_map_user_stack(&task->as, stack_top)) {
        address_space_destroy(&task->as);
        --task_count;
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

void sched_create_idle_task(void)
{
    if (task_count >= 32) {
        return;
    }
    struct task *task = &tasks[task_count++];
    task->pid = 0;
    task->parent_pid = 0;
    task_copy_name(task, "idle");
    task->entry = 0;
    task->stack_top = 0;
    task->wake_tick = 0;
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
    ++scheduler_ticks;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_BLOCKED && tasks[i].wake_tick &&
            tasks[i].wake_tick <= scheduler_ticks) {
            tasks[i].wake_tick = 0;
            tasks[i].state = TASK_READY;
        }
    }
}

uint64_t sched_tick_count(void)
{
    return scheduler_ticks;
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
    task->state = TASK_READY;
}

void sched_sleep_current_until(uint64_t wake_tick)
{
    struct task *task = sched_current_task();
    if (!task || task->pid == 0 || task->state == TASK_EXITED) {
        return;
    }
    task->wake_tick = wake_tick;
    task->state = TASK_BLOCKED;
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
        dst->reserved = 0;
        dst->wake_tick = tasks[i].wake_tick;
        dst->entry = tasks[i].entry;
        dst->cr3 = tasks[i].as.cr3;
        snapshot_name(dst->name, sizeof(dst->name), tasks[i].name);
    }
    return n;
}

void sched_dump(void)
{
    for (uint32_t i = 0; i < task_count; ++i) {
        console_printf("[ntclks] task[%u] pid=%u ppid=%u name=%s state=%u kind=%u flags=0x%x\n",
                       i,
                       tasks[i].pid,
                       tasks[i].parent_pid,
                       tasks[i].name,
                       tasks[i].state,
                       tasks[i].kind,
                       tasks[i].flags);
    }
}
