#ifndef NTCLKS_SCHED_H
#define NTCLKS_SCHED_H

#include <ntclks/paging.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>

#define SCHED_TASK_NAME_LEN 32u

enum task_state {
    TASK_READY = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_EXITED = 3,
};

enum task_kind {
    TASK_KIND_KERNEL = 0,
    TASK_KIND_USER = 1,
};

#define TASK_FLAG_SERVICE 0x00000001u
#define TASK_FLAG_STARTED 0x00000002u
#define TASK_FLAG_RESOURCES_RELEASED 0x00000004u

struct task {
    uint32_t pid;
    uint32_t parent_pid;
    char name_storage[SCHED_TASK_NAME_LEN];
    const char *name;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t wake_tick;
    uint64_t exit_code;
    const void *image;
    size_t image_len;
    struct address_space as;
    struct trap_frame frame;
    enum task_state state;
    enum task_kind kind;
    uint32_t flags;
    uint32_t pty_id;
};

struct task_snapshot_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t reserved;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[SCHED_TASK_NAME_LEN];
};

void sched_init(void);
uint32_t sched_create_kernel_task(const char *name, uint64_t entry);
uint32_t sched_create_user_task(const char *name, uint64_t entry, uint64_t stack_top,
                                uint32_t parent_pid, uint32_t flags);
void sched_set_task_image(uint32_t pid, const void *image, size_t image_len);
void sched_create_idle_task(void);
void sched_set_running(uint32_t pid);
void sched_exit(uint32_t pid, uint64_t code);
void sched_release_task_resources(struct task *task);
void sched_on_tick(void);
uint64_t sched_tick_count(void);
uint32_t sched_current_pid(void);
struct task *sched_current_task(void);
struct task *sched_find(uint32_t pid);
struct task *sched_find_by_name(const char *name);
struct task *sched_select_next_user(void);
struct trap_frame *sched_task_frame(struct task *task);
uint64_t sched_task_cr3(struct task *task);
void sched_mark_ready(uint32_t pid);
void sched_sleep_current_until(uint64_t wake_tick);
int64_t sched_wait_reap(uint32_t waiter_pid, uint32_t wanted_pid, uint64_t *exit_code);
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick);
void sched_dump(void);

#endif
