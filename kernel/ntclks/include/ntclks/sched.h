/*
 * LeonOS scheduler interface: defines task, VMA, file, and session state.
 * Declares task lifecycle, scheduling, waiting, and process-inspection APIs.
 */
#ifndef NTCLKS_SCHED_H
#define NTCLKS_SCHED_H

#include <ntclks/paging.h>
#include <ntclks/storage.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>
#include <leonos/auth.h>
#include <leonos/elf_abi.h>

#define SCHED_TASK_NAME_LEN 32u
#define SCHED_TASK_MAX 64u
#define SCHED_TASK_FILE_MAX 12u
#define SCHED_TASK_PTY_FD_MAX 8u
#define SCHED_EXEC_ARG_MAX 8u
#define SCHED_EXEC_ENV_MAX 8u
#define SCHED_EXEC_DATA_MAX 512u
#define SCHED_TASK_VMA_MAX 128u

#define TASK_VMA_FLAG_PRIVATE 0x00000001u
#define TASK_VMA_FLAG_ANON    0x00000002u
#define TASK_VMA_FLAG_FILE    0x00000004u
#define TASK_VMA_FLAG_LAZY    0x00000008u
#define TASK_VMA_FLAG_SHARED_FILE 0x00000010u
#define TASK_VMA_FLAG_DYNAMIC_LOAD 0x00000020u

#define TASK_VMA_PROT_READ  0x1u
#define TASK_VMA_PROT_WRITE 0x2u
#define TASK_VMA_PROT_EXEC  0x4u

struct task_vma {
    uint32_t used;
    uint32_t prot;
    uint32_t max_prot;
    uint32_t flags;
    uint32_t reserved;
    uint64_t start;
    uint64_t end;
    uint64_t file_offset;
    uint64_t file_limit;
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

/* Aliases of the standard streams for a process attached to a PTY. */
struct task_pty_fd {
    uint32_t used;
    int32_t fd;
    uint32_t stream;
    uint32_t flags;
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
#define TASK_FLAG_ELEVATED_ADMIN 0x00000010u
#define TASK_FLAG_PENDING_LOAD 0x00000020u

struct task {
    uint32_t pid;
    uint32_t parent_pid;
    char name_storage[SCHED_TASK_NAME_LEN];
    const char *name;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t wake_tick;
    uint32_t wait_window_id;
    uint64_t exit_code;
    const void *image;
    size_t image_len;
    struct storage_node image_node;
    struct address_space as;
    struct trap_frame frame;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    enum task_state state;
    enum task_kind kind;
    uint32_t flags;
    uint32_t pty_id;
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint64_t cpu_ticks;
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
    struct leonos_dynamic_launch dynamic_launch;
    struct task_vma vmas[SCHED_TASK_VMA_MAX];
    struct task_file files[SCHED_TASK_FILE_MAX];
    struct task_pty_fd pty_fds[SCHED_TASK_PTY_FD_MAX];
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
    uint32_t memory_kib;
    uint64_t cpu_ticks;
    uint64_t wake_tick;
    uint64_t entry;
    uint64_t cr3;
    char name[SCHED_TASK_NAME_LEN];
    char username[LEONOS_AUTH_USERNAME_LEN];
};

/**
 * @brief Coordinates the sched init operation.
 */
void sched_init(void);
/**
 * @brief Coordinates the sched create kernel task operation.
 * @param name Input or output value used by this operation.
 * @param entry Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_create_kernel_task(const char *name, uint64_t entry);
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
                                uint32_t parent_pid, uint32_t flags);
/**
 * @brief Coordinates the sched set task image operation.
 * @param pid Input or output value used by this operation.
 * @param image Input or output value used by this operation.
 * @param image_len Length, size, or element count associated with the operation.
 */
void sched_set_task_image(uint32_t pid, const void *image, size_t image_len);
/**
 * @brief Coordinates the sched set task image node operation.
 * @param pid Input or output value used by this operation.
 * @param node Input or output value used by this operation.
 */
void sched_set_task_image_node(uint32_t pid, const struct storage_node *node);
/**
 * @brief Coordinates the sched set task path operation.
 * @param pid Input or output value used by this operation.
 * @param path LeonOS path consumed by this operation.
 */
void sched_set_task_path(uint32_t pid, const char *path);
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
                                const char *data, uint32_t data_len);
/**
 * @brief Coordinates the sched create idle task operation.
 */
void sched_create_idle_task(void);
/**
 * @brief Coordinates the sched set running operation.
 * @param pid Input or output value used by this operation.
 */
void sched_set_running(uint32_t pid);
/**
 * @brief Coordinates the sched exit operation.
 * @param pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 */
void sched_exit(uint32_t pid, uint64_t code);
/**
 * @brief Coordinates the sched release task resources operation.
 * @param task Task whose state or authority is inspected or updated.
 */
void sched_release_task_resources(struct task *task);
/**
 * @brief Coordinates the sched on tick operation.
 */
void sched_on_tick(void);
/**
 * @brief Coordinates the sched tick count operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t sched_tick_count(void);
/**
 * @brief Coordinates the sched cpu ticks operation.
 * @param busy_ticks Input or output value used by this operation.
 * @param idle_ticks Input or output value used by this operation.
 */
void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks);
/**
 * @brief Coordinates the sched task counts operation.
 * @param task_count Length, size, or element count associated with the operation.
 * @param running_tasks Input or output value used by this operation.
 * @param ready_tasks Input or output value used by this operation.
 * @param sleeping_tasks Input or output value used by this operation.
 */
void sched_task_counts(uint32_t *task_count, uint32_t *running_tasks,
                       uint32_t *ready_tasks, uint32_t *sleeping_tasks);
/**
 * @brief Coordinates the sched current pid operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_current_pid(void);
struct task *sched_current_task(void);
struct task *sched_find(uint32_t pid);
struct task *sched_find_by_name(const char *name);
struct task *sched_find_by_path(const char *path);
struct task *sched_find_by_path_basename(const char *basename);
struct task *sched_find_window_server(void);
struct task *sched_select_next_user(void);
struct trap_frame *sched_task_frame(struct task *task);
/**
 * @brief Coordinates the sched task cr3 operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
uint64_t sched_task_cr3(struct task *task);
/**
 * @brief Coordinates the sched mark ready operation.
 * @param pid Input or output value used by this operation.
 */
void sched_mark_ready(uint32_t pid);
/**
 * @brief Coordinates the sched sleep current until operation.
 * @param wake_tick Input or output value used by this operation.
 */
void sched_sleep_current_until(uint64_t wake_tick);
/**
 * @brief Coordinates the sched wait current for window event operation.
 * @param window_id Input or output value used by this operation.
 * @param wake_tick Input or output value used by this operation.
 */
void sched_wait_current_for_window_event(uint32_t window_id, uint64_t wake_tick);
/**
 * @brief Coordinates the sched wake window event operation.
 * @param pid Input or output value used by this operation.
 * @param window_id Input or output value used by this operation.
 */
void sched_wake_window_event(uint32_t pid, uint32_t window_id);
/**
 * @brief Coordinates the sched kill user task operation.
 * @param pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_task(uint32_t pid, uint64_t code);
/**
 * @brief Coordinates the sched kill user tasks for pty operation.
 * @param pty_id Input or output value used by this operation.
 * @param keep_pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid,
                                  uint64_t code);
/**
 * @brief Coordinates the sched kill user tasks for logout operation.
 * @param uid Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 * @param keep_pid Input or output value used by this operation.
 * @param code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int sched_kill_user_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                     uint32_t keep_pid, uint64_t code);
/**
 * @brief Coordinates the sched wait reap operation.
 * @param waiter_pid Input or output value used by this operation.
 * @param wanted_pid Input or output value used by this operation.
 * @param exit_code Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t sched_wait_reap(uint32_t waiter_pid, uint32_t wanted_pid, uint64_t *exit_code);
/**
 * @brief Coordinates the sched snapshot operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param tick Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick);
/**
 * @brief Coordinates the sched set task identity operation.
 * @param pid Input or output value used by this operation.
 * @param user Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 */
void sched_set_task_identity(uint32_t pid, const struct leonos_user_info *user,
                             uint32_t session_id);
/**
 * @brief Coordinates the sched set session identity operation.
 * @param parent_pid Input or output value used by this operation.
 * @param user Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 */
void sched_set_session_identity(uint32_t parent_pid, const struct leonos_user_info *user,
                                uint32_t session_id);
/**
 * @brief Coordinates the sched clear session identity operation.
 * @param session_id Input or output value used by this operation.
 */
void sched_clear_session_identity(uint32_t session_id);
/**
 * @brief Coordinates the sched next session id operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t sched_next_session_id(void);
/**
 * @brief Coordinates the sched dump operation.
 */
void sched_dump(void);

#endif
