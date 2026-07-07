#ifndef NTCLKS_SCHED_H
#define NTCLKS_SCHED_H

#include <ntclks/paging.h>
#include <ntclks/storage.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>
#include <leonos/auth.h>

#define SCHED_TASK_NAME_LEN 32u
#define SCHED_TASK_MAX 64u
#define SCHED_TASK_FILE_MAX 12u
#define SCHED_EXEC_ARG_MAX 8u
#define SCHED_EXEC_ENV_MAX 8u
#define SCHED_EXEC_DATA_MAX 512u
#define SCHED_TASK_VMA_MAX 16u

struct task_vma {
    uint32_t used;
    uint32_t prot;
    uint32_t flags;
    uint32_t reserved;
    uint64_t start;
    uint64_t end;
    uint64_t file_offset;
    struct storage_node file_node;
};

struct task_file {
    uint32_t used;
    uint32_t flags;
    struct storage_node node;
    uint64_t offset;
    uint64_t aux;
    char path[LEONOS_FS_PATH_LEN];
};

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
#define TASK_FLAG_WINDOW_SERVER 0x00000008u

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
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
    char cwd[LEONOS_FS_PATH_LEN];
    char path[LEONOS_FS_PATH_LEN];
    uint32_t exec_argc;
    uint32_t exec_envc;
    uint32_t exec_data_len;
    char *exec_argv[SCHED_EXEC_ARG_MAX + 1];
    char *exec_envp[SCHED_EXEC_ENV_MAX + 1];
    char exec_data[SCHED_EXEC_DATA_MAX];
    struct task_vma vmas[SCHED_TASK_VMA_MAX];
    struct task_file files[SCHED_TASK_FILE_MAX];
};

struct task_snapshot_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t flags;
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[SCHED_TASK_NAME_LEN];
    char username[LEONOS_AUTH_USERNAME_LEN];
};

void sched_init(void);
uint32_t sched_create_kernel_task(const char *name, uint64_t entry);
uint32_t sched_create_user_task(const char *name, uint64_t entry, uint64_t stack_top,
                                uint32_t parent_pid, uint32_t flags);
void sched_set_task_image(uint32_t pid, const void *image, size_t image_len);
void sched_set_task_path(uint32_t pid, const char *path);
void sched_set_task_exec_params(uint32_t pid,
                                uint32_t argc, char *const argv[],
                                uint32_t envc, char *const envp[],
                                const char *data, uint32_t data_len);
void sched_create_idle_task(void);
void sched_set_running(uint32_t pid);
void sched_exit(uint32_t pid, uint64_t code);
void sched_release_task_resources(struct task *task);
void sched_on_tick(void);
uint64_t sched_tick_count(void);
void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks);
void sched_task_counts(uint32_t *task_count, uint32_t *running_tasks,
                       uint32_t *ready_tasks, uint32_t *sleeping_tasks);
uint32_t sched_current_pid(void);
struct task *sched_current_task(void);
struct task *sched_find(uint32_t pid);
struct task *sched_find_by_name(const char *name);
struct task *sched_find_by_path(const char *path);
struct task *sched_find_by_path_basename(const char *basename);
struct task *sched_find_window_server(void);
struct task *sched_select_next_user(void);
struct trap_frame *sched_task_frame(struct task *task);
uint64_t sched_task_cr3(struct task *task);
void sched_mark_ready(uint32_t pid);
void sched_sleep_current_until(uint64_t wake_tick);
int sched_kill_user_task(uint32_t pid, uint64_t code);
int sched_kill_user_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                     uint32_t keep_pid, uint64_t code);
int64_t sched_wait_reap(uint32_t waiter_pid, uint32_t wanted_pid, uint64_t *exit_code);
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick);
void sched_set_task_identity(uint32_t pid, const struct leonos_user_info *user,
                             uint32_t session_id);
void sched_set_session_identity(uint32_t parent_pid, const struct leonos_user_info *user,
                                uint32_t session_id);
void sched_clear_session_identity(uint32_t session_id);
uint32_t sched_next_session_id(void);
void sched_dump(void);

#endif
