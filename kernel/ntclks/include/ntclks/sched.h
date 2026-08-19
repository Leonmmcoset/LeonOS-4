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
#include <ntclks/heap.h>
#include <leonos/auth.h>
#include <leonos/elf_abi.h>

#define SCHED_TASK_NAME_LEN 32u
/* Initial task-table capacity; the scheduler grows beyond this value. */
#define SCHED_TASK_MAX 64u
#define SCHED_TASK_FILE_MAX 64u
#define SCHED_TASK_FILE_LIMIT 1024u
#define SCHED_TASK_PTY_FD_MAX 8u
#define SCHED_TASK_STDIO_MAX 3u
#define SCHED_EXEC_ARG_MAX 64u
#define SCHED_EXEC_ENV_MAX 64u
#define SCHED_EXEC_DATA_MAX 8192u
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
    uint32_t fd_flags;
    struct storage_node node;
    uint64_t offset;
    uint64_t aux;
    struct storage_read_cursor read_cursor;
    char path[LEONOS_FS_PATH_LEN];
};

#define TASK_FILE_FLAG_PIPE       0x80000000u
#define TASK_FILE_FLAG_PIPE_WRITE 0x40000000u

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
    /* A SIGSTOPped task keeps its address space and descriptors but is not
     * eligible for scheduling until SIGCONT makes it ready again. */
    TASK_STOPPED = 4,
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
/* Real fork children remain zombies until their parent calls waitpid. */
#define TASK_FLAG_WAITABLE_CHILD 0x00000040u

/* A stopped or continued child retains its task slot until its parent has
 * observed the state transition through waitpid. */
#define TASK_CHILD_EVENT_NONE      0x00000000u
#define TASK_CHILD_EVENT_STOPPED   0x00000001u
#define TASK_CHILD_EVENT_CONTINUED 0x00000002u

/* The task object is split into ownership domains. Anonymous compatibility
 * views preserve task->field while code migrates to named substructures. */
struct task_process_state {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t process_group;
    uint32_t process_session;
    char name_storage[SCHED_TASK_NAME_LEN];
    const char *name;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t stack_low;
    uint64_t wake_tick;
    uint64_t exit_code;
    enum task_state state;
    enum task_kind kind;
    uint32_t flags;
    uint64_t cpu_ticks;
    int32_t priority;
};

struct task_address_space_state {
    struct address_space as;
    struct task_vma vmas[SCHED_TASK_VMA_MAX];
    struct task_vma *vma_extra;
    uint32_t vma_extra_count;
    uint32_t vma_extra_capacity;
};

struct task_fd_table_state {
    struct task_file files[SCHED_TASK_FILE_MAX];
    struct task_file stdio_files[SCHED_TASK_STDIO_MAX];
    struct task_file *file_extra;
    uint32_t file_extra_count;
    uint32_t file_extra_capacity;
};

struct task_signal_state {
    uint32_t pending_signals;
    uint32_t child_event;
    uint32_t stop_signal;
    uint32_t exit_signal;
};

struct task_credentials_state {
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint64_t rlimit_nofile;
    uint64_t rlimit_as;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
    char cwd[LEONOS_FS_PATH_LEN];
};

struct task_terminal_state {
    uint32_t wait_window_id;
    uint32_t pty_id;
    uint32_t wait_pty_id;
    struct task_pty_fd pty_fds[SCHED_TASK_PTY_FD_MAX];
};

struct task_loader_state {
    const void *image;
    size_t image_len;
    struct storage_node image_node;
    char path[LEONOS_FS_PATH_LEN];
    uint32_t exec_argc;
    uint32_t exec_envc;
    uint32_t exec_data_len;
    char *exec_argv[SCHED_EXEC_ARG_MAX + 1];
    char *exec_envp[SCHED_EXEC_ENV_MAX + 1];
    char exec_data[SCHED_EXEC_DATA_MAX];
    struct leonos_dynamic_launch dynamic_launch;
};

struct task {
    union {
        struct task_process_state process;
        struct {
            uint32_t pid;
            uint32_t parent_pid;
            uint32_t process_group;
            uint32_t process_session;
            char name_storage[SCHED_TASK_NAME_LEN];
            const char *name;
            uint64_t entry;
            uint64_t stack_top;
            uint64_t stack_low;
            uint64_t wake_tick;
            uint64_t exit_code;
            enum task_state state;
            enum task_kind kind;
            uint32_t flags;
            uint64_t cpu_ticks;
            int32_t priority;
        };
    };
    union {
        struct task_address_space_state address_space;
        struct {
            struct address_space as;
            struct task_vma vmas[SCHED_TASK_VMA_MAX];
            struct task_vma *vma_extra;
            uint32_t vma_extra_count;
            uint32_t vma_extra_capacity;
        };
    };
    union {
        struct task_fd_table_state fd_table;
        struct {
            struct task_file files[SCHED_TASK_FILE_MAX];
            struct task_file stdio_files[SCHED_TASK_STDIO_MAX];
            struct task_file *file_extra;
            uint32_t file_extra_count;
            uint32_t file_extra_capacity;
        };
    };
    union {
        struct task_signal_state signal_state;
        struct {
            uint32_t pending_signals;
            uint32_t child_event;
            uint32_t stop_signal;
            uint32_t exit_signal;
        };
    };
    union {
        struct task_credentials_state credentials;
        struct {
            uint32_t uid;
            uint32_t role;
            uint32_t session_id;
            uint64_t rlimit_nofile;
            uint64_t rlimit_as;
            char username[LEONOS_AUTH_USERNAME_LEN];
            char home[LEONOS_AUTH_HOME_LEN];
            char cwd[LEONOS_FS_PATH_LEN];
        };
    };
    union {
        struct task_terminal_state terminal_state;
        struct {
            uint32_t wait_window_id;
            uint32_t pty_id;
            uint32_t wait_pty_id;
            struct task_pty_fd pty_fds[SCHED_TASK_PTY_FD_MAX];
        };
    };
    union {
        struct task_loader_state loader_state;
        struct {
            const void *image;
            size_t image_len;
            struct storage_node image_node;
            char path[LEONOS_FS_PATH_LEN];
            uint32_t exec_argc;
            uint32_t exec_envc;
            uint32_t exec_data_len;
            char *exec_argv[SCHED_EXEC_ARG_MAX + 1];
            char *exec_envp[SCHED_EXEC_ENV_MAX + 1];
            char exec_data[SCHED_EXEC_DATA_MAX];
            struct leonos_dynamic_launch dynamic_launch;
        };
    };
    struct trap_frame frame;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
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
    int32_t priority;
    uint32_t pending_signals;
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
 * @brief Duplicates the current user task using copy-on-write user mappings.
 * @param parent_frame Saved fork syscall frame; the child receives a copy with rax set to zero.
 * @return Positive child PID to the parent or a negative errno-style failure.
 */
int64_t sched_fork_current(const struct trap_frame *parent_frame);
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
struct task_vma *sched_task_vma_at(struct task *task, uint32_t index);
uint32_t sched_task_vma_capacity(const struct task *task);
void sched_task_vma_release(struct task *task);
struct task_file *sched_task_file_at(struct task *task, uint32_t index);
uint32_t sched_task_file_capacity(const struct task *task);
void sched_task_file_release(struct task *task);
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
/**
 * @brief Determines whether a live task still refers to a numeric drive.
 * @param drive Numeric LeonOS drive identifier.
 * @return True when a CWD, open file, image, or file mapping uses the drive.
 */
bool sched_drive_in_use(uint32_t drive);
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
 * @brief Sends a supported signal to a user task.
 * @param pid Target process identifier.
 * @param signal_number POSIX signal number.
 * @return Zero on success or a negative scheduler error.
 */
int sched_signal_user_task(uint32_t pid, int signal_number);
/**
 * @brief Sends a signal to all eligible user tasks in a process group.
 * @param sender_pid Process that requested the signal.
 * @param process_group Target process group identifier.
 * @param signal_number POSIX signal number.
 * @return Number of signalled tasks, or a negative scheduler error.
 */
int sched_signal_process_group(uint32_t sender_pid, uint32_t process_group,
                               int signal_number);
/**
 * @brief Updates the process group of the caller or one of its direct children.
 * @param caller_pid Process issuing setpgid.
 * @param pid Target process, or zero for the caller.
 * @param process_group Target group, or zero to create a group led by the target.
 * @return Zero on success or a negative errno-style failure.
 */
int sched_set_process_group(uint32_t caller_pid, uint32_t pid,
                            uint32_t process_group);
/**
 * @brief Returns the process group for a task.
 * @param pid Target process, or zero for the current process.
 * @return Positive process-group identifier or a negative scheduler error.
 */
int64_t sched_get_process_group(uint32_t pid);
/**
 * @brief Creates a new POSIX-style session for the calling process.
 * @param pid Calling process identifier.
 * @return New session identifier or a negative scheduler error.
 */
int64_t sched_create_process_session(uint32_t pid);
/**
 * @brief Checks whether a process group has a task attached to a PTY.
 * @param process_group Process group identifier.
 * @param pty_id PTY identifier.
 * @return Non-zero when an attached group member exists.
 */
int sched_process_group_has_pty(uint32_t process_group, uint32_t pty_id);
/**
 * @brief Returns the POSIX process session containing a task.
 * @param pid Task process identifier.
 * @return Positive session identifier or a negative scheduler error.
 */
int64_t sched_get_process_session(uint32_t pid);
/**
 * @brief Reads or updates a task's nice-style priority.
 * @param pid Target process identifier.
 * @param priority New priority when set is non-zero.
 * @param set Non-zero to update, zero to read.
 * @return Priority on read/update or a negative error.
 */
int sched_task_priority(uint32_t pid, int priority, int set);
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
int64_t sched_wait_reap(uint32_t waiter_pid, int32_t wanted_pid,
                        uint32_t options, int *status);
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
