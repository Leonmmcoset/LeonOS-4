/*
 * LeonOS scheduler interface: defines task, VMA, file, and session state.
 * Declares task lifecycle, scheduling, waiting, and process-inspection APIs.
 */
#ifndef NTCLKS_SCHED_H
#define NTCLKS_SCHED_H

#include <ntclks/paging.h>
#include <ntclks/signal.h>
#include <ntclks/storage.h>
#include <ntclks/trap.h>
#include <ntclks/types.h>
#include <ntclks/heap.h>
#include <leonos/auth.h>
#include <leonos/elf_abi.h>
#include <linux/resource.h>

#define SCHED_TASK_NAME_LEN 32u
/* Initial task-table capacity; the scheduler grows beyond this value. */
#define SCHED_TASK_MAX 64u
#define SCHED_TASK_FILE_MAX 64u
#define SCHED_TASK_FILE_LIMIT 1024u
#define SCHED_NR_OPEN 1048576u
#define SCHED_TASK_PTY_FD_MAX 8u
#define SCHED_TASK_STDIO_MAX 3u
#define SCHED_EXEC_ARG_MAX 64u
#define SCHED_EXEC_ENV_MAX 64u
#define SCHED_EXEC_DATA_MAX 8192u
#define SCHED_TASK_VMA_MAX 128u
#define SCHED_TASK_TIMER_MAX 32u

#define TASK_VMA_FLAG_PRIVATE 0x00000001u
#define TASK_VMA_FLAG_ANON    0x00000002u
#define TASK_VMA_FLAG_FILE    0x00000004u
#define TASK_VMA_FLAG_LAZY    0x00000008u
#define TASK_VMA_FLAG_SHARED_FILE 0x00000010u
#define TASK_VMA_FLAG_DYNAMIC_LOAD 0x00000020u
#define TASK_VMA_FLAG_DEVICE  0x00000040u
#define TASK_VMA_FLAG_SHARED  0x00000080u
#define TASK_VMA_FLAG_LOCKED  0x00000100u

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
    struct task_file *description;
    uint32_t references;
    uint32_t used;
    uint32_t scm_references;
    uint32_t scm_gc_seen;
    uint32_t flags;
    uint32_t fd_flags;
    uint32_t reserved;
    struct storage_node node;
    uint64_t offset;
    uint64_t aux;
    /* Per-descriptor device state; currently the evdev EVIOCGRAB token. */
    uint64_t aux2;
    struct storage_read_cursor read_cursor;
    char path[LEONOS_FS_PATH_LEN];
};

static inline struct task_file *task_file_description(struct task_file *file)
{
    return file && file->description ? file->description : file;
}

#define TASK_FILE_FLAG_PIPE       0x80000000u
#define TASK_FILE_FLAG_PIPE_WRITE 0x40000000u
#define TASK_FILE_FLAG_DEV_NULL   0x20000000u
#define TASK_FILE_FLAG_DEV_NODE   0x10000000u
#define TASK_FILE_FLAG_DEV_BLOCK  0x02000000u
#define TASK_FILE_FLAG_SOCKET     0x08000000u
#define TASK_FILE_FLAG_SOCKET_UNIX 0x04000000u
#define TASK_FILE_FLAG_DEV_SHM    0x01000000u
#define TASK_FILE_FLAG_SOCKET_INET 0x00800000u
#define TASK_FILE_FLAG_EVENTFD     0x00400000u
#define TASK_FILE_FLAG_EPOLL       0x00200000u

/* Aliases of the standard streams for a process attached to a PTY. */
struct task_pty_fd {
    uint32_t used;
    int32_t fd;
    uint32_t stream;
    /* Descriptor flags: currently only FD_CLOEXEC. */
    uint32_t flags;
    /* Open status flags: O_ACCMODE, O_APPEND and O_NONBLOCK. */
    uint32_t status_flags;
    /* Unix98 PTY endpoints use an explicit session and direction. Legacy
     * aliases leave pty_id/endpoint zero and continue using stream. */
    uint32_t pty_id;
    uint32_t endpoint;
};

#define TASK_PTY_ENDPOINT_MASTER 1u
#define TASK_PTY_ENDPOINT_SLAVE  2u

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
/* A wait4 caller has claimed this zombie and is releasing it outside the
 * scheduler lock.  It prevents a second waiter from recycling the same slot. */
#define TASK_FLAG_REAP_IN_PROGRESS 0x00000080u

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
    uint64_t fs_base;
    uint64_t clear_child_tid;
    uint64_t wake_tick;
    uint64_t poll_deadline_ticks;
    uint64_t exit_code;
    enum task_state state;
    enum task_kind kind;
    uint32_t flags;
    uint64_t cpu_ticks;
    int32_t priority;
    /* CPUs on which this task may execute. A zero mask is treated as all
     * discovered CPUs for compatibility with older task initializers. */
    uint64_t affinity_mask;
    /* Linux brk(2) state; the break is process-wide and starts at the end of
     * the executable's writable data segment. */
    uint64_t program_break_base;
    uint64_t program_break;
    uint32_t mlockall_flags;
};

struct task_address_space_state {
    struct address_space as;
    struct task_vma vmas[SCHED_TASK_VMA_MAX];
    struct task_vma *vma_extra;
    uint32_t vma_extra_count;
    uint32_t vma_extra_capacity;
    uint32_t references;
    uint64_t initial_stack_top;
    uint64_t initial_stack_low;
    /* Zero-initialized new/exec address spaces are dumpable by default. */
    bool nondumpable;
    /* Linux membarrier registration commands are process/MM scoped. */
    uint32_t membarrier_registrations;
};

struct task_fd_table_state {
    struct task_file files[SCHED_TASK_FILE_MAX];
    struct task_file stdio_files[SCHED_TASK_STDIO_MAX];
    /* Bits for closed implicit stdin/stdout/stderr descriptors. */
    uint32_t closed_stdio_mask;
    /* Bits for implicit stdio descriptors marked close-on-exec by
     * close_range(CLOSE_RANGE_CLOEXEC). */
    uint32_t cloexec_stdio_mask;
    struct task_file *file_extra;
    uint32_t file_extra_count;
    uint32_t file_extra_capacity;
    struct task_pty_fd pty_fds[SCHED_TASK_PTY_FD_MAX];
    uint32_t references;
};

struct task_fs_state {
    uint32_t umask;
    char cwd[LEONOS_FS_PATH_LEN];
    uint32_t references;
};

struct task_sighand_state {
    struct kernel_signal_action actions[KERNEL_SIGNAL_ACTION_MAX];
    uint32_t references;
};

struct task_signal_state {
    uint64_t pending_signals;
    uint64_t timer_pending_signals;
    uint64_t blocked_signals;
    uint64_t sigsuspend_saved_mask;
    uint32_t sigsuspend_active;
    uint32_t child_event;
    uint32_t stop_signal;
    uint32_t exit_signal;
    uint32_t parent_exit_signal;
    uint32_t parent_exit_notified;
    uint64_t ignored_signals;
};

/* Linux POSIX timers are per-process objects, but the native task table is
 * also the ownership boundary for this kernel.  Timer ids are small integers
 * scoped to the task that created them, matching the ids exposed by musl. */
struct task_posix_timer {
    uint32_t used;
    uint32_t id;
    int32_t clockid;
    int32_t notify;
    int32_t signo;
    int32_t target_pid;
    uint64_t value;
    uint64_t expiry_tick;
    uint64_t interval_ticks;
    uint64_t overrun;
    uint64_t value_data;
};

struct task_groups {
    uint32_t references;
    uint32_t count;
    uint32_t ids[];
};

struct task_credentials_state {
    uint32_t uid;
    uint32_t gid;
    uint32_t euid;
    uint32_t egid;
    uint32_t suid;
    uint32_t sgid;
    uint32_t fsuid;
    uint32_t fsgid;
    struct task_groups *groups;
    uint32_t role;
    uint32_t session_id;
    uint32_t umask;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
    char cwd[LEONOS_FS_PATH_LEN];
};

struct task_terminal_state {
    uint32_t wait_window_id;
    uint32_t pty_id;
    uint32_t wait_pty_id;
    uint32_t controlling_pty_id;
};

struct task_loader_state {
    const void *image;
    size_t image_len;
    struct storage_node image_node;
    char path[LEONOS_FS_PATH_LEN];
    uint32_t exec_argc;
    uint32_t exec_envc;
    uint32_t exec_data_len;
    uint16_t exec_phnum;
    uint16_t exec_reserved;
    char *exec_argv[SCHED_EXEC_ARG_MAX + 1];
    char *exec_envp[SCHED_EXEC_ENV_MAX + 1];
    char exec_data[SCHED_EXEC_DATA_MAX];
    struct leonos_dynamic_launch dynamic_launch;
};

struct task_rlimit_state {
    uint32_t references;
    struct linux_rlimit64 nofile;
    struct linux_rlimit64 as;
};

struct task {
    struct task_rlimit_state limits;
    struct task_rlimit_state *shared_limits;
    uint32_t tgid;
    uint64_t process_pending_signals;
    uint64_t *shared_process_pending;
    uint64_t alarm_deadline;
    uint64_t alarm_interval_ns;
    uint64_t alarm_last_expiry;
    struct task_address_space_state *shared_mm;
    struct task_fd_table_state *shared_files;
    struct task_fs_state *shared_fs;
    struct task_sighand_state *shared_sighand;
    uint64_t robust_list;
    uint64_t signal_stack_base;
    uint64_t signal_stack_size;
    uint32_t signal_stack_flags;
    uint64_t restart_syscall;
    uint64_t nanosleep_deadline;
    uint64_t nanosleep_remaining;
    int32_t nanosleep_clock;
    uint64_t sigwait_deadline;
    uint64_t sigwait_mask;
    struct task_posix_timer timers[SCHED_TASK_TIMER_MAX];
    struct kernel_wait_queue *waiting_queue;
    struct task_file *socket_receive_file;
    uint64_t socket_receive_done;
    int32_t socket_receive_pid;
    uint32_t socket_receive_uid;
    uint32_t socket_receive_gid;
    uint64_t socket_receive_message;
    uint64_t socket_receive_control_capacity;
    uint32_t socket_receive_name_capacity;
    struct task_file *syscall_file;
    int32_t syscall_fd;
    uint32_t syscall_file_number;
    uint64_t socket_io_deadline;
    bool socket_io_timed;
    struct task *futex_next;
    uint64_t futex_key;
    uint64_t futex_domain;
    uint64_t futex_address;
    uint64_t futex_deadline;
    uint32_t futex_bitset;
    int32_t futex_state;
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
            uint64_t fs_base;
            uint64_t clear_child_tid;
            uint64_t wake_tick;
            uint64_t poll_deadline_ticks;
            uint64_t exit_code;
            enum task_state state;
            enum task_kind kind;
            uint32_t flags;
            uint64_t cpu_ticks;
            int32_t priority;
            uint64_t affinity_mask;
            uint64_t program_break_base;
            uint64_t program_break;
            uint32_t mlockall_flags;
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
            uint32_t closed_stdio_mask;
            struct task_file *file_extra;
            uint32_t file_extra_count;
            uint32_t file_extra_capacity;
            struct task_pty_fd pty_fds[SCHED_TASK_PTY_FD_MAX];
            uint32_t file_references;
        };
    };
    union {
        struct task_signal_state signal_state;
        struct {
            uint64_t pending_signals;
            uint64_t timer_pending_signals;
            uint64_t blocked_signals;
            uint64_t sigsuspend_saved_mask;
            uint32_t sigsuspend_active;
            uint32_t child_event;
            uint32_t stop_signal;
            uint32_t exit_signal;
            uint32_t parent_exit_signal;
            uint32_t parent_exit_notified;
            uint64_t ignored_signals;
        };
    };
    union {
        struct task_credentials_state credentials;
        struct {
            uint32_t uid;
            uint32_t gid;
            uint32_t euid;
            uint32_t egid;
            uint32_t suid;
            uint32_t sgid;
            uint32_t fsuid;
            uint32_t fsgid;
            struct task_groups *groups;
            uint32_t role;
            uint32_t session_id;
            uint32_t umask;
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
            uint32_t controlling_pty_id;
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
            uint16_t exec_phnum;
            uint16_t exec_reserved;
            char *exec_argv[SCHED_EXEC_ARG_MAX + 1];
            char *exec_envp[SCHED_EXEC_ENV_MAX + 1];
            char exec_data[SCHED_EXEC_DATA_MAX];
            struct leonos_dynamic_launch dynamic_launch;
        };
    };
    struct trap_frame frame;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    struct kernel_signal_action signal_actions[KERNEL_SIGNAL_ACTION_MAX];
    uint32_t running_cpu;
};

int sched_prepare_exec_current(struct task *task);
void sched_exec_replace_mm(struct task *task, const struct address_space *replacement);

/* Inline storage is promoted on first sharing. All users of an ownership
 * domain must resolve through these accessors, including page-fault paths. */
/** @brief Resolve the process-wide resource limits shared by CLONE_THREAD. */
static inline struct task_rlimit_state *sched_task_limits(const struct task *task)
{
    return task->shared_limits ? task->shared_limits : (struct task_rlimit_state *)&task->limits;
}

static inline struct task_address_space_state *sched_task_mm(const struct task *task)
{
    return task->shared_mm ? task->shared_mm : (struct task_address_space_state *)&task->address_space;
}

static inline struct address_space *sched_task_as(const struct task *task)
{
    return &sched_task_mm(task)->as;
}

static inline struct task_fd_table_state *sched_task_fds(const struct task *task)
{
    return task->shared_files ? task->shared_files : (struct task_fd_table_state *)&task->fd_table;
}

static inline char *sched_task_cwd(const struct task *task)
{
    return task->shared_fs ? task->shared_fs->cwd : (char *)task->cwd;
}

static inline uint32_t *sched_task_umask(const struct task *task)
{
    return task->shared_fs ? &task->shared_fs->umask : (uint32_t *)&task->umask;
}

static inline struct kernel_signal_action *sched_task_actions(const struct task *task)
{
    return task->shared_sighand ? task->shared_sighand->actions :
        (struct kernel_signal_action *)task->signal_actions;
}

static inline uint32_t sched_task_tgid(const struct task *task)
{
    return task->tgid ? task->tgid : task->pid;
}

static inline uint64_t *sched_task_process_pending(const struct task *task)
{
    return task->shared_process_pending ? task->shared_process_pending :
        (uint64_t *)&task->process_pending_signals;
}

static inline uint64_t sched_task_pending(const struct task *task)
{
    return task->pending_signals | *sched_task_process_pending(task);
}

int64_t sched_clone_current(const struct trap_frame *frame, uint64_t flags,
                           uint64_t stack, uint64_t parent_tid,
                           uint64_t child_tid, uint64_t tls);
void sched_exit_group(uint32_t tgid, uint64_t code);

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
    uint64_t affinity_mask;
    char name[SCHED_TASK_NAME_LEN];
    char username[LEONOS_AUTH_USERNAME_LEN];
};

/**
 * @brief Initialize the scheduler, its run queue, and the tick bookkeeping.
 */
void sched_init(void);
/**
 * @brief Create a kernel task named name that starts at entry; returns its pid.
 */
uint32_t sched_create_kernel_task(const char *name, uint64_t entry);
/**
 * @brief Create a user task named name with the given entry, stack, parent, and flags; returns pid.
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
 * @brief Attach the executable image (image/image_len) to task pid for a later exec.
 */
void sched_set_task_image(uint32_t pid, const void *image, size_t image_len);
/**
 * @brief Attach the executable image described by node to task pid.
 */
void sched_set_task_image_node(uint32_t pid, const struct storage_node *node);
/**
 * @brief Record the executable path that task pid will run.
 */
void sched_set_task_path(uint32_t pid, const char *path);
/**
 * @brief Set argv/envp and the packed aux data for task pid's upcoming exec.
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
 * @brief Create the always-runnable idle task that runs when nothing else can.
 */
void sched_create_idle_task(void);
/**
 * @brief Mark task pid as the currently running task.
 */
void sched_set_running(uint32_t pid);
/**
 * @brief End task pid with exit code, releasing it once its parent reaps it.
 */
void sched_exit(uint32_t pid, uint64_t code);
/**
 * @brief Free the memory, descriptors, and mappings still held by task.
 */
void sched_release_task_resources(struct task *task);
/**
 * @brief Advance scheduler time and preempt or wake tasks as needed.
 */
void sched_on_tick(void);
/** Account a local APIC tick without advancing the BSP's global clock. */
void sched_on_cpu_tick(void);
/**
 * @brief Return the number of scheduler ticks since boot.
 */
uint64_t sched_tick_count(void);
void sched_yield_current(void);
/**
 * @brief Report the total busy and idle ticks accumulated so far.
 */
void sched_cpu_ticks(uint64_t *busy_ticks, uint64_t *idle_ticks);
/** Copy the accumulated busy and idle tick counters for each CPU. */
void sched_cpu_ticks_per_cpu(uint64_t *busy_ticks, uint64_t *idle_ticks,
                             uint32_t capacity);
/** Copy a coherent per-CPU runtime snapshot, including current pid and
 * eligible ready-queue counts. */
void sched_cpu_runtime_snapshot(uint64_t *busy_ticks, uint64_t *idle_ticks,
                                uint32_t *current_pids, uint32_t *ready_counts,
                                uint32_t capacity);
/**
 * @brief Report the current task totals: all, running, ready, and sleeping.
 */
void sched_task_counts(uint32_t *task_count, uint32_t *running_tasks,
                       uint32_t *ready_tasks, uint32_t *sleeping_tasks);
/**
 * @brief Return the pid of the task currently running on this CPU.
 */
uint32_t sched_current_pid(void);
uint64_t sched_all_cpu_mask(void);
int sched_get_task_affinity(uint32_t pid, uint64_t *mask);
int sched_set_task_affinity(uint32_t pid, uint64_t mask);
struct task *sched_current_task(void);
struct task *sched_find(uint32_t pid);

static inline uint64_t *sched_task_timer_pending(const struct task *task)
{
    struct task *leader = task ? sched_find(sched_task_tgid(task)) : NULL;
    return leader ? &leader->timer_pending_signals : NULL;
}

struct task *sched_find_by_name(const char *name);
struct task *sched_find_by_path(const char *path);
struct task *sched_find_by_path_basename(const char *basename);
/** Returns true when a CWD, file, image, or mapping references a volume. */
bool sched_volume_in_use(uint32_t volume_id);
/** Save this CPU's user trap frame and release its current task when runnable. */
bool sched_capture_current_user_frame(const struct trap_frame *frame);
/** Release this CPU's ownership after its current task was marked EXITED. */
void sched_quiesce_exited_current(void);
struct task *sched_select_next_user(void);
/* Reclaim the task just saved by this CPU when no other task is runnable. */
struct task *sched_reclaim_current_user(void);
struct trap_frame *sched_task_frame(struct task *task);
/**
 * @brief Return the page-table root (CR3 value) of task.
 */
uint64_t sched_task_cr3(struct task *task);
/**
 * @brief Move task pid back onto the ready queue so it can be scheduled.
 */
void sched_mark_ready(uint32_t pid);
/**
 * @brief Put the current task to sleep with no wake deadline (wait queues own
 * the wakeup path). Used by IPC primitives before returning EAGAIN internally;
 * the syscall epilogue rewinds and retries the instruction after wakeup.
 */
void sched_block_current(void);
/**
 * @brief Put the current task to sleep until the given tick.
 */
void sched_sleep_current_until(uint64_t wake_tick);
/**
 * @brief Sleep the current task until a window event or the given tick.
 */
/**
 * @brief Wake task pid if it is waiting on window_id.
 */
/**
 * @brief Terminate user task pid with code; 0 on success.
 */
int sched_kill_user_task(uint32_t pid, uint64_t code);
/**
 * @brief Sends a supported signal to a user task.
 * @param pid Target process identifier.
 * @param signal_number POSIX signal number.
 * @return Zero on success or a negative scheduler error.
 */
int sched_signal_user_task(uint32_t pid, int signal_number);
int sched_signal_user_process(uint32_t tgid, int signal_number);
void sched_signal_job_control(uint32_t tgid, int signal_number);
void sched_signal_discard(struct task *task, int signal_number);
uint32_t sched_alarm_task(struct task *task, uint32_t seconds);
struct linux_itimerval;
void sched_itimer_task(struct task *task, const struct linux_itimerval *value, struct linux_itimerval *old);
void sched_alarm_rearm(struct task *task);
struct linux_itimerspec;
int sched_timer_create(struct task *task, int32_t clockid, int32_t notify,
                       int32_t signo, int32_t target_pid, uint64_t value,
                       int32_t *timerid);
int sched_timer_delete(struct task *task, int32_t timerid);
int sched_timer_settime(struct task *task, int32_t timerid, uint32_t flags,
                        const struct linux_itimerspec *value,
                        struct linux_itimerspec *old);
int sched_timer_gettime(struct task *task, int32_t timerid,
                        struct linux_itimerspec *value);
int sched_timer_getoverrun(struct task *task, int32_t timerid);
/**
 * @brief Read or update a task's default/ignored signal disposition.
 * @param pid Target user-task ID.
 * @param signal_number POSIX signal number.
 * @param operation Query or update operation.
 * @param disposition Requested value for update and previous value on return.
 * @return Zero on success or a negative scheduler error.
 */
int sched_signal_action(uint32_t pid, int signal_number, uint32_t operation,
                        uint32_t *disposition);
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
 * @brief Count open PTY endpoint descriptors and legacy controlling-TTY
 * attachments referencing pty_id.
 */
uint32_t sched_pty_reference_count(uint32_t pty_id);
uint32_t sched_pty_master_reference_count(uint32_t pty_id);
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
 * @brief Kill every task attached to pty_id except keep_pid; returns the count.
 */
int sched_kill_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid,
                                  uint64_t code);
/**
 * @brief Send SIGHUP to tasks attached to a closed PTY and detach survivors.
 * @param pty_id Closed PTY identifier.
 * @param keep_pid PTY owner to leave untouched.
 * @return Number of signalled tasks, or zero when no PTY was supplied.
 */
int sched_hangup_user_tasks_for_pty(uint32_t pty_id, uint32_t keep_pid);
/**
 * @brief Kill tasks of uid/session_id except keep_pid; returns the count.
 */
int sched_kill_user_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                     uint32_t keep_pid, uint64_t code);
/**
 * @brief Wait for a child to change state (waitpid): returns child pid or a negative error.
 */
int64_t sched_wait_reap(uint32_t waiter_pid, int32_t wanted_pid,
                        uint32_t options, int *status);
/**
 * @brief Copy up to capacity task summaries into out and report the current tick.
 */
uint32_t sched_snapshot(struct task_snapshot_info *out, uint32_t capacity, uint64_t *tick);
/**
 * @brief Attach user identity and session to task pid.
 */
void sched_set_task_identity(uint32_t pid, const struct leonos_user_info *user,
                             uint32_t session_id);
/**
 * @brief Attach user identity and session to parent_pid's existing session.
 */
void sched_set_session_identity(uint32_t parent_pid, const struct leonos_user_info *user,
                                uint32_t session_id);
/**
 * @brief Detach the identity associated with session_id.
 */
void sched_clear_session_identity(uint32_t session_id);
/**
 * @brief Allocate and return a fresh session identifier.
 */
uint32_t sched_next_session_id(void);
/**
 * @brief Print scheduler and task state to the console for debugging.
 */
void sched_dump(void);


/**
 * @brief Set the controlling tty of every thread in a process.
 * @param pid Any thread ID in the process.
 * @param pty_id New controlling tty ID, or zero to detach.
 */
void sched_set_controlling_pty(uint32_t pid, uint32_t pty_id);
/**
 * @brief Clear a stolen controlling tty from all its former processes.
 * @param pty_id Controlling tty to detach.
 */
void sched_clear_controlling_pty(uint32_t pty_id);

#endif
