/*
 * LeonOS kernel system calls: validates and dispatches the user ABI.
 * Implements process, file, memory, IPC, GUI, device, and timing operations.
 */
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/framebuffer.h>
#include <ntclks/lock.h>
#include <ntclks/heap.h>
#include <ntclks/input.h>
#include <ntclks/mm.h>
#include <ntclks/mouse.h>
#include <ntclks/net.h>
#include <ntclks/osmlayer.h>
#include <ntclks/platform.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/smp.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/userland.h>
#include <ntclks/kernel_debug.h>

static int64_t syscall_dispatch_regs(uint64_t number, uint64_t a0, uint64_t a1,
                                     uint64_t a2, uint64_t a3, uint64_t a4,
                                     uint64_t a5);

#include <ntclks/version.h>

#include <leonos/device.h>
#include <leonos/audio.h>
#include <leonos/driver.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/net.h>
#include <leonos/pty.h>
#include <leonos/signal.h>
#include <leonos/system.h>
#include <leonos/startup.h>
#include <leonos/text.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/soundcard.h>
#include <linux/fs.h>
#include <linux/mount.h>

#define LEONOS_TEXT_LAYOUT_MAX_BYTES 4096U
#define LEONOS_TEXT_LAYOUT_MAX_GLYPHS 512U
#define PAGE_SIZE 4096ULL
#define LEONOS_O_NONBLOCK 0x800u
#define LINUX_PROT_READ TASK_VMA_PROT_READ
#define LINUX_PROT_WRITE TASK_VMA_PROT_WRITE
#define LINUX_PROT_EXEC TASK_VMA_PROT_EXEC
#define LINUX_MAP_PRIVATE 0x02u
#define LINUX_MAP_FIXED 0x10u
#define LINUX_MAP_ANONYMOUS 0x20u
#define LINUX_MAP_SUPPORTED (LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS)
#define OOBE_DHCP_APP_PATH "/system/apps/oobe/oobe.elf"
#define OOBE_DONE_MARKER_PATH "/system/state/oobe.done"
#define SYSCONFDIALOG_APP_PATH "/system/apps/sysconfdialog/sysconfdialog.elf"
#define STARTUP_DB_PATH "/system/state/startup.db"
#define STARTUP_DENIAL_DB_PATH "/system/state/startup-denials.db"
#define STARTUP_DB_MAGIC 0x53545031U
#define STARTUP_DENIAL_DB_MAGIC 0x53544431U
#define STARTUP_DB_ENTRY_MAX 64U
#define STARTUP_DENIAL_MAX 64U
#define STARTUP_REQUEST_MAX 16U
/* Both supported PCM drivers accept 16-bit stereo samples.  This is the
 * portable OSS queue shape reported by /dev/dsp, not driver-private DMA
 * layout. */
#define OSS_DSP_FRAGMENT_BYTES 2048U
#define OSS_DSP_FRAGMENT_COUNT 8U
#define OSS_DSP_QUEUE_BYTES (OSS_DSP_FRAGMENT_BYTES * OSS_DSP_FRAGMENT_COUNT)


struct startup_db {
    uint32_t magic;
    uint32_t count;
    uint32_t next_id;
    uint32_t reserved;
    struct {
        uint32_t uid;
        struct leonos_startup_entry entry;
    } entries[STARTUP_DB_ENTRY_MAX];
};

struct startup_denial_db {
    uint32_t magic;
    uint32_t count;
    uint32_t reserved0;
    uint32_t reserved1;
    struct {
        uint32_t uid;
        char requester_path[LEONOS_FS_PATH_LEN];
        struct leonos_startup_command command;
    } entries[STARTUP_DENIAL_MAX];
};

struct startup_request_slot {
    uint32_t used;
    uint32_t id;
    uint32_t status;
    uint32_t requester_pid;
    uint32_t dialog_pid;
    uint32_t session_id;
    struct leonos_user_info user;
    char requester_path[LEONOS_FS_PATH_LEN];
    struct leonos_startup_command command;
};

static struct startup_db startup_db_scratch;
static struct startup_denial_db startup_denial_db_scratch;
static struct startup_request_slot startup_requests[STARTUP_REQUEST_MAX];
static uint32_t startup_next_request_id = 1;

struct task_snapshot_user {
    uint32_t capacity;
    uint32_t count;
    uint64_t tick;
    struct task_snapshot_info *tasks;
};

/**
 * Task effective role.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint32_t task_effective_role(const struct task *task);

/**
 * Device copy text.
 * @param dst Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param src Value supplied by the caller.
 */
static void device_copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
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
 * Device append char.
 * @param buf Value supplied by the caller.
 * @param pos Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @param ch Value supplied by the caller.
 */
static void device_append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

/**
 * Text eq cstr.
 * @param a Value supplied by the caller.
 * @param b Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int text_eq_cstr(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

/**
 * Oobe dhcp renew allowed.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int oobe_dhcp_renew_allowed(const struct task *task)
{
    struct storage_node node;
    if (!task || !text_eq_cstr(task->path, OOBE_DHCP_APP_PATH) ||
        !storage_ready()) {
        return 0;
    }
    return storage_lookup_path(OOBE_DONE_MARKER_PATH, &node) < 0;
}

/**
 * Require window server.
 * @return The value or status produced by the operation.
 */
/**
 * Require driver management.
 * @return The value or status produced by the operation.
 */
static int require_driver_management(void)
{
    struct task *task = sched_current_task();
    return task_effective_role(task) == LEONOS_AUTH_ROLE_ADMIN
               ? 0
               : -LEONOS_EPERM;
}

/**
 * Task effective role.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint32_t task_effective_role(const struct task *task)
{
    if (!task) {
        return LEONOS_AUTH_ROLE_NONE;
    }
    if (task->role == LEONOS_AUTH_ROLE_ADMIN ||
        (task->flags & TASK_FLAG_ELEVATED_ADMIN)) {
        return LEONOS_AUTH_ROLE_ADMIN;
    }
    return task->role;
}

/**
 * Require background service.
 * @return The value or status produced by the operation.
 */
static int require_background_service(void)
{
    struct task *task = sched_current_task();
    if (!task || !(task->flags & TASK_FLAG_SERVICE) ||
        (task->flags & TASK_FLAG_WINDOW_SERVER)) {
        return -LEONOS_EPERM;
    }
    return 0;
}

/**
 * Device append text.
 * @param buf Value supplied by the caller.
 * @param pos Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @param text NUL-terminated text supplied by the caller.
 */
static void device_append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        device_append_char(buf, pos, cap, *text++);
    }
}

/**
 * Device append u64.
 * @param buf Value supplied by the caller.
 * @param pos Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @param value Value supplied by the caller.
 */
static void device_append_u64(char *buf, uint32_t *pos, uint32_t cap, uint64_t value)
{
    char tmp[24];
    uint32_t n = 0;
    if (value == 0) {
        device_append_char(buf, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n) {
        device_append_char(buf, pos, cap, tmp[--n]);
    }
}

/**
 * Device append i32.
 * @param buf Value supplied by the caller.
 * @param pos Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @param value Value supplied by the caller.
 */
static void device_append_i32(char *buf, uint32_t *pos, uint32_t cap, int32_t value)
{
    if (value < 0) {
        device_append_char(buf, pos, cap, '-');
        value = -value;
    }
    device_append_u64(buf, pos, cap, (uint32_t)value);
}

/**
 * Device append ipv4.
 * @param buf Value supplied by the caller.
 * @param pos Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @param ip Value supplied by the caller.
 */
static void device_append_ipv4(char *buf, uint32_t *pos, uint32_t cap, uint32_t ip)
{
    device_append_u64(buf, pos, cap, (ip >> 24) & 0xffu);
    device_append_char(buf, pos, cap, '.');
    device_append_u64(buf, pos, cap, (ip >> 16) & 0xffu);
    device_append_char(buf, pos, cap, '.');
    device_append_u64(buf, pos, cap, (ip >> 8) & 0xffu);
    device_append_char(buf, pos, cap, '.');
    device_append_u64(buf, pos, cap, ip & 0xffu);
}

/**
 * Device add.
 * @param devices Caller-owned structure read or updated by the function.
 * @param capacity Maximum number of elements available in the related buffer.
 * @param count Output storage updated by the function.
 * @param device_class Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param name NUL-terminated text supplied by the caller.
 * @param status Output storage updated by the function.
 * @param detail Value supplied by the caller.
 * @param value0 Value supplied by the caller.
 * @param value1 Value supplied by the caller.
 */
static void device_add(struct leonos_device_info *devices, uint32_t capacity,
                       uint32_t *count, uint32_t device_class, uint32_t flags,
                       const char *name, const char *status, const char *detail,
                       uint64_t value0, uint64_t value1)
{
    uint32_t index;
    if (!count) {
        return;
    }
    index = *count;
    *count = index + 1;
    if (!devices || index >= capacity) {
        return;
    }
    devices[index] = (struct leonos_device_info){0};
    devices[index].id = index;
    devices[index].device_class = device_class;
    devices[index].flags = flags;
    devices[index].value0 = value0;
    devices[index].value1 = value1;
    device_copy_text(devices[index].name, sizeof(devices[index].name), name);
    device_copy_text(devices[index].status, sizeof(devices[index].status), status);
    device_copy_text(devices[index].detail, sizeof(devices[index].detail), detail);
}

/**
 * Raw device add.
 * @param raw Caller-owned structure read or updated by the function.
 * @param count Output storage updated by the function.
 * @param kind Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param aux0 Value supplied by the caller.
 * @param aux1 Value supplied by the caller.
 * @param value0 Value supplied by the caller.
 * @param value1 Value supplied by the caller.
 */
static void raw_device_add(struct leonos_raw_device_info *raw, uint32_t *count,
                           uint32_t kind, uint32_t flags, uint32_t aux0, uint32_t aux1,
                           uint64_t value0, uint64_t value1)
{
    uint32_t index;
    if (!raw || !count || *count >= LEONOS_RAW_DEVICE_MAX) {
        return;
    }
    index = (*count)++;
    raw[index] = (struct leonos_raw_device_info){
        .kind = kind,
        .flags = flags,
        .aux0 = aux0,
        .aux1 = aux1,
        .value0 = value0,
        .value1 = value1,
    };
}

/**
 * Device format fb.
 * @param buf Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param fb Value supplied by the caller.
 */
static void device_format_fb(char *buf, uint32_t cap, const struct framebuffer *fb)
{
    uint32_t pos = 0;
    buf[0] = 0;
    if (!fb || !fb->available) {
        device_append_text(buf, &pos, cap, "No GOP framebuffer");
        return;
    }
    device_append_u64(buf, &pos, cap, fb->width);
    device_append_char(buf, &pos, cap, 'x');
    device_append_u64(buf, &pos, cap, fb->height);
    device_append_text(buf, &pos, cap, " bpp=");
    device_append_u64(buf, &pos, cap, fb->bpp);
    device_append_text(buf, &pos, cap, " pitch=");
    device_append_u64(buf, &pos, cap, fb->pitch);
}

/**
 * Device format mouse.
 * @param buf Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param mouse Value supplied by the caller.
 */
static void device_format_mouse(char *buf, uint32_t cap, const struct mouse_state *mouse)
{
    uint32_t pos = 0;
    buf[0] = 0;
    if (!mouse || !mouse->present) {
        device_append_text(buf, &pos, cap, "PS/2 mouse not detected");
        return;
    }
    device_append_text(buf, &pos, cap, mouse->absolute ? "absolute " : "relative ");
    device_append_text(buf, &pos, cap, "x=");
    device_append_i32(buf, &pos, cap, mouse->x);
    device_append_text(buf, &pos, cap, " y=");
    device_append_i32(buf, &pos, cap, mouse->y);
    device_append_text(buf, &pos, cap, " buttons=");
    device_append_u64(buf, &pos, cap, mouse->buttons);
}

/**
 * Device format disk.
 * @param buf Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param disk Value supplied by the caller.
 */
static void device_format_disk(char *buf, uint32_t cap, const struct leonos_install_disk *disk)
{
    uint32_t pos = 0;
    uint64_t mib = disk ? (disk->sector_count * (uint64_t)disk->sector_size) / (1024ULL * 1024ULL) : 0;
    buf[0] = 0;
    device_append_text(buf, &pos, cap,
                       disk && disk->name[0] == 'I' ? "IDE/PATA port "
                       : (disk && disk->name[0] == 'N' ? "NVMe namespace " : "AHCI port "));
    device_append_u64(buf, &pos, cap, disk ? disk->port : 0);
    device_append_text(buf, &pos, cap, ", ");
    device_append_u64(buf, &pos, cap, mib);
    device_append_text(buf, &pos, cap, " MiB, sector ");
    device_append_u64(buf, &pos, cap, disk ? disk->sector_size : 0);
}

/**
 * Device format time.
 * @param buf Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param time Value supplied by the caller.
 */
static void device_format_time(char *buf, uint32_t cap, const struct leonos_time_info *time)
{
    uint32_t pos = 0;
    buf[0] = 0;
    if (!time || !time->valid) {
        device_append_text(buf, &pos, cap, "CMOS RTC unavailable");
        return;
    }
    device_append_u64(buf, &pos, cap, time->year);
    device_append_char(buf, &pos, cap, '-');
    device_append_u64(buf, &pos, cap, time->month);
    device_append_char(buf, &pos, cap, '-');
    device_append_u64(buf, &pos, cap, time->day);
    device_append_char(buf, &pos, cap, ' ');
    device_append_u64(buf, &pos, cap, time->hour);
    device_append_char(buf, &pos, cap, ':');
    device_append_u64(buf, &pos, cap, time->minute);
    device_append_char(buf, &pos, cap, ':');
    device_append_u64(buf, &pos, cap, time->second);
}

struct exec_params_kernel {
    uint32_t argc;
    uint32_t envc;
    char *argv[SCHED_EXEC_ARG_MAX + 1];
    char *envp[SCHED_EXEC_ENV_MAX + 1];
    char data[SCHED_EXEC_DATA_MAX];
    uint32_t data_len;
};

/**
 * Normalize dir path.
 * @param path NUL-terminated text supplied by the caller.
 */
static void normalize_dir_path(char *path)
{
    size_t len = 0;
    if (!path) {
        return;
    }
    while (path[len]) {
        ++len;
    }
    while (len > 3 && path[len - 1] == '/') {
        path[--len] = 0;
    }
}

/**
 * Copy text.
 * @param dst Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param src Value supplied by the caller.
 */
static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
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
 * Clear task file.
 * @param file Value supplied by the caller.
 */
void clear_task_file(struct task_file *file)
{
    /**
 * @brief Descriptor cleanup can be reached both from the immediate exit path and from later zombie reaping. A released pipe end must only decrement its shared reference count once.
 */
    if (!file || !file->used) {
        return;
    }
    task_pipe_release(file);
    task_socket_release(file);
    task_inet_release(file);
    task_shm_release(file);
    if ((file->flags & TASK_FILE_FLAG_DEV_NODE) &&
        (file->node.first_cluster == STORAGE_DEV_KIND_KEYBOARD ||
         file->node.first_cluster == STORAGE_DEV_KIND_MOUSE) &&
        file->aux2 != 0) {
        input_evdev_release(file->node.first_cluster, file->aux2,
                            sched_current_pid());
    }
    file->used = 0;
    file->node.type = 0;
    file->node.flags = 0;
    file->node.first_cluster = 0;
    file->node.volume_id = 0;
    file->node.size = 0;
    file->offset = 0;
    file->aux = 0;
    file->aux2 = 0;
    file->read_cursor = (struct storage_read_cursor){0};
    file->flags = 0;
    file->fd_flags = 0;
    file->path[0] = 0;
}

/**
 * Clear task files.
 * @param task Value supplied by the caller.
 */
static void clear_task_files(struct task *task)
{
    if (!task) {
        return;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        struct task_file *file = sched_task_file_at(task, i);
        if (file) clear_task_file(file);
    }
    for (uint32_t i = 0; i < SCHED_TASK_STDIO_MAX; ++i) {
        clear_task_file(&task->stdio_files[i]);
    }
}

/**
 * Syscall release task files.
 * @param task Value supplied by the caller.
 */
void syscall_release_task_files(struct task *task)
{
    if (task) {
        net_close_owner_sockets(task->pid);
    }
    clear_task_files(task);
    sched_task_file_release(task);
}

/**
 * @brief Retains pipe endpoint references after fork copies descriptor entries by value.
 * @param parent Source process, required to prevent accidental use on detached task records.
 * @param child Fork child whose copied pipe endpoints require new references.
 * @return Zero on success or a negative errno-style result for invalid arguments.
 */
int syscall_clone_task_files(const struct task *parent, struct task *child)
{
    if (!parent || !child || child->parent_pid != parent->pid) {
        return -LEONOS_EINVAL;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(child); ++i) {
        struct task_file *file = sched_task_file_at(child, i);
        if (file && file->used) {
            task_pipe_retain(file);
            task_socket_retain(file);
            task_inet_retain(file);
            task_shm_retain(file);
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_STDIO_MAX; ++i) {
        if (child->stdio_files[i].used) {
            task_pipe_retain(&child->stdio_files[i]);
            task_socket_retain(&child->stdio_files[i]);
            task_inet_retain(&child->stdio_files[i]);
            task_shm_retain(&child->stdio_files[i]);
        }
    }
    return 0;
}

static void task_pty_release_entry(struct task_pty_fd *entry);

/**
 * @brief Closes file and explicit PTY aliases marked FD_CLOEXEC for execve.
 * @param task Process whose existing descriptor table survives the image replacement.
 */
void syscall_close_cloexec_files(struct task *task)
{
    if (!task) {
        return;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        struct task_file *file = sched_task_file_at(task, i);
        if (file && file->used && (file->fd_flags & 1u)) {
            clear_task_file(file);
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_STDIO_MAX; ++i) {
        if (task->stdio_files[i].used && (task->stdio_files[i].fd_flags & 1u)) {
            clear_task_file(&task->stdio_files[i]);
            task->closed_stdio_mask |= 1u << i;
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        if (task->pty_fds[i].used && (task->pty_fds[i].flags & 1u)) {
            task_pty_release_entry(&task->pty_fds[i]);
        }
    }
}

/**
 * Task file for fd.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
struct task_file *task_file_for_fd(struct task *task, int fd)
{
    if (!task || fd < 0) {
        return NULL;
    }
    if (fd < 3) {
        return task->stdio_files[fd].used ? &task->stdio_files[fd] : NULL;
    }
    if (fd == 3) return NULL;
    if (fd >= 4 + (int)sched_task_file_capacity(task)) return NULL;
    struct task_file *file = sched_task_file_at(task, (uint32_t)(fd - 4));
    if (!file) return NULL;
    return file->used ? file : NULL;
}

#define LEONOS_F_DUPFD 0
#define LEONOS_F_GETFD 1
#define LEONOS_F_SETFD 2
#define LEONOS_F_GETFL 3
#define LEONOS_F_SETFL 4
#define LEONOS_F_DUPFD_CLOEXEC 14

/**
 * Task pty fd for fd.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
struct task_pty_fd *task_pty_fd_for_fd(struct task *task, int fd)
{
    if (!task || fd < 4) {
        return NULL;
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        struct task_pty_fd *entry = &task->pty_fds[i];
        if (entry->used && entry->fd == fd) {
            return entry;
        }
    }
    return NULL;
}

static int task_pty_fd_available(struct task *task, int fd);

static struct task_pty_fd *task_pty_endpoint_for_fd(struct task *task, int fd)
{
    struct task_pty_fd *entry = task_pty_fd_for_fd(task, fd);
    return entry && entry->endpoint && entry->pty_id ? entry : NULL;
}

static void task_pty_release_entry(struct task_pty_fd *entry)
{
    uint32_t pty_id;
    if (!entry || !entry->used) {
        return;
    }
    pty_id = entry->pty_id;
    *entry = (struct task_pty_fd){0};
    if (pty_id) {
        pty_reap_hungup(pty_id);
    }
}

static int task_pty_endpoint_fd(struct task *task, uint32_t pty_id,
                                uint32_t endpoint, uint32_t flags)
{
    int candidate;
    if (!task || !pty_id || !endpoint || !pty_is_active(pty_id) ||
        !task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    candidate = 4;
    for (uint32_t attempts = 0;
         attempts < SCHED_TASK_PTY_FD_MAX + SCHED_TASK_FILE_MAX + 4u;
         ++attempts, ++candidate) {
        if (!task_pty_fd_available(task, candidate)) continue;
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            struct task_pty_fd *entry = &task->pty_fds[i];
            if (!entry->used) {
                *entry = (struct task_pty_fd){
                    .used = 1, .fd = candidate, .flags = 0,
                    .status_flags = flags & (LEONOS_O_ACCMODE |
                                             LEONOS_O_APPEND |
                                             LEONOS_O_NONBLOCK),
                    .stream = endpoint == TASK_PTY_ENDPOINT_MASTER ? 1u : 0u,
                    .pty_id = pty_id, .endpoint = endpoint,
                };
                return candidate;
            }
        }
        return -LEONOS_EMFILE;
    }
    return -LEONOS_EMFILE;
}

static int task_pty_endpoint_path(const char *path, uint32_t *pty_id)
{
    const char *prefix = "/dev/pts/";
    uint32_t value = 0;
    uint32_t digits = 0;
    if (!path || !pty_id) return 0;
    while (*prefix && *path && *prefix == *path) { ++prefix; ++path; }
    if (*prefix || !*path) return 0;
    while (*path >= '0' && *path <= '9') {
        value = value * 10u + (uint32_t)(*path - '0');
        ++path; ++digits;
        if (value > 0xffffu) return 0;
    }
    if (!digits || *path || !value || !pty_is_active(value)) return 0;
    *pty_id = value;
    return 1;
}

static int task_device_is(const struct task_file *file, uint32_t kind);

/**
 * Task pty stream for fd.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_pty_stream_for_fd(struct task *task, int fd)
{
    struct task_pty_fd *entry;
    struct task_file *file;
    if (!task || !task->pty_id) {
        return -1;
    }
    if (fd >= 0 && fd < (int)SCHED_TASK_STDIO_MAX &&
        (task->closed_stdio_mask & (1u << (uint32_t)fd)) != 0) {
        return -1;
    }
    /**
 * @brief A redirected stdio descriptor shadows the implicit PTY stream.
 */
    file = task_file_for_fd(task, fd);
    if (file) {
        /* A real /dev/tty node is an alias for the caller's controlling PTY.
         * Keep it visible to termios/ioctl and terminal process-group calls,
         * while leaving /dev/console and ordinary files on their own paths. */
        if (task_device_is(file, STORAGE_DEV_KIND_TTY)) {
            return 0;
        }
        return -1;
    }
    if (fd >= 0 && fd <= 2) {
        return fd;
    }
    entry = task_pty_fd_for_fd(task, fd);
    return entry ? (int)entry->stream : -1;
}

/**
 * Task pty fd available.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_pty_fd_available(struct task *task, int fd)
{
    if (fd < 4 || task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) {
        return 0;
    }
    return 1;
}

/**
 * @brief RLIMIT_NOFILE applies to descriptors allocated in addition to the three implicit PTY standard streams. Files and explicit PTY aliases share the same process limit even though they use separate backing tables.
 */
static uint32_t task_allocated_fd_count(const struct task *task)
{
    uint32_t count = 0;
    if (!task) {
        return 0;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        const struct task_file *file = sched_task_file_at((struct task *)task, i);
        if (file && file->used) {
            ++count;
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        if (task->pty_fds[i].used) {
            ++count;
        }
    }
    return count;
}

int task_can_allocate_fd(const struct task *task)
{
    return task && task_allocated_fd_count(task) < task->rlimit_nofile;
}

/**
 * Task pty duplicate fd.
 * @param task Value supplied by the caller.
 * @param old_fd Value supplied by the caller.
 * @param minimum_fd Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @return The value or status produced by the operation.
 */
static int task_pty_duplicate_fd(struct task *task, int old_fd, int minimum_fd,
                                 uint32_t flags)
{
    struct task_pty_fd *source = task_pty_fd_for_fd(task, old_fd);
    int stream = task_pty_stream_for_fd(task, old_fd);
    int candidate;
    if ((stream < 0 && !(source && source->endpoint)) || minimum_fd < 0) {
        return -LEONOS_EBADF;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    candidate = minimum_fd < 4 ? 4 : minimum_fd;
    for (uint32_t attempts = 0; attempts < SCHED_TASK_PTY_FD_MAX + SCHED_TASK_FILE_MAX + 4u;
         ++attempts, ++candidate) {
        if (candidate < 0) {
            return -LEONOS_EINVAL;
        }
        if (!task_pty_fd_available(task, candidate)) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            struct task_pty_fd *entry = &task->pty_fds[i];
            if (!entry->used) {
                entry->used = 1;
                entry->fd = candidate;
                entry->flags = flags;
                /* dup() of a Unix98 endpoint must preserve the endpoint and
                 * direction, not degrade it into an implicit stdio alias. */
                if (source && source->endpoint) {
                    entry->pty_id = source->pty_id;
                    entry->endpoint = source->endpoint;
                    entry->stream = source->stream;
                    entry->status_flags = source->status_flags;
                } else {
                    entry->stream = (uint32_t)stream;
                    entry->status_flags = stream == 0 ? LEONOS_O_RDONLY
                                                      : LEONOS_O_WRONLY;
                }
                return candidate;
            }
        }
        return -LEONOS_EMFILE;
    }
    return -LEONOS_EMFILE;
}

/**
 * Task pty dup2 fd.
 * @param task Value supplied by the caller.
 * @param old_fd Value supplied by the caller.
 * @param new_fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_pty_dup2_fd(struct task *task, int old_fd, int new_fd)
{
    struct task_pty_fd *endpoint;
    int stream = task_pty_stream_for_fd(task, old_fd);
    struct task_pty_fd *entry;
    if (new_fd < 0) {
        return -LEONOS_EBADF;
    }
    if (old_fd == new_fd) {
        return new_fd;
    }
    /* forkpty/openpty children call dup2(slave, 0/1/2) before closing the
     * original /dev/pts/N descriptor. That must publish the explicit PTY id
     * as the child controlling terminal even when the parent had no legacy
     * controlling TTY and task->pty_id was zero. */
    endpoint = task_pty_endpoint_for_fd(task, old_fd);
    if (new_fd < (int)SCHED_TASK_STDIO_MAX && endpoint &&
        endpoint->endpoint == TASK_PTY_ENDPOINT_SLAVE) {
        struct task_file *stdio = &task->stdio_files[new_fd];
        if (stdio->used) clear_task_file(stdio);
        task->closed_stdio_mask &= ~(1u << (uint32_t)new_fd);
        task->pty_id = endpoint->pty_id;
        if (new_fd == 0) {
            /* setsid() ran before this dup2.  Adopt the child session/pgrp
             * as the PTY controlling session so tcgetpgrp/tcsetpgrp work
             * for job control and SIGTTIN/TTOU delivery. */
            pty_acquire_controlling(endpoint->pty_id, task->pid);
            console_printf("[ntclks] dup2 pty pid=%u old=%d new=%d pty=%u session=%u pgrp=%u\n",
                           task->pid, old_fd, new_fd, endpoint->pty_id,
                           task->process_session, task->process_group);
        }
        return new_fd;
    }
    if (stream < 0 && !endpoint) {
        return -LEONOS_EBADF;
    }
    /**
 * @brief Restore an implicit PTY stream after a temporary redirection.
 */
    if (new_fd < 3) {
        struct task_file *stdio = &task->stdio_files[new_fd];
        if (stdio->used) {
            clear_task_file(stdio);
        }
        task->closed_stdio_mask &= ~(1u << (uint32_t)new_fd);
        return new_fd;
    }
    if (new_fd == 3) {
        return -LEONOS_EBADF;
    }
    /**
 * @brief dup2() replaces an existing descriptor regardless of its backing kind.
 */
    if (task_file_for_fd(task, new_fd)) {
        clear_task_file(task_file_for_fd(task, new_fd));
    }
    entry = task_pty_fd_for_fd(task, new_fd);
    if (!entry) {
        if (!task_can_allocate_fd(task)) {
            return -LEONOS_EMFILE;
        }
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            if (!task->pty_fds[i].used) {
                entry = &task->pty_fds[i];
                entry->used = 1;
                entry->fd = new_fd;
                break;
            }
        }
    }
    if (!entry) {
        return -LEONOS_EMFILE;
    }
    if (endpoint) {
        entry->pty_id = endpoint->pty_id;
        entry->endpoint = endpoint->endpoint;
        entry->stream = endpoint->stream;
        entry->status_flags = endpoint->status_flags;
    } else {
        entry->pty_id = 0;
        entry->endpoint = 0;
        entry->stream = (uint32_t)stream;
        entry->status_flags = stream == 0 ? LEONOS_O_RDONLY
                                          : LEONOS_O_WRONLY;
    }
    entry->flags = 0;
    return new_fd;
}

/**
 * Alloc task fd.
 * @param task Value supplied by the caller.
 * @param node Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param path NUL-terminated text supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int alloc_task_fd(struct task *task, const struct storage_node *node, uint32_t flags, const char *path)
{
    struct task_file *file;
    if (!task || !node) {
        return -LEONOS_EINVAL;
    }
    /* POSIX open() reuses a closed standard descriptor before allocating a
     * regular descriptor.  nohup relies on this after closing stdin. */
    for (uint32_t i = 0; i < SCHED_TASK_STDIO_MAX; ++i) {
        uint32_t bit = 1u << i;
        if ((task->closed_stdio_mask & bit) == 0) {
            continue;
        }
        file = &task->stdio_files[i];
        if (file->used) {
            continue;
        }
        file->used = 1;
        file->node = *node;
        file->offset = 0;
        file->aux = 0;
        file->aux2 = 0;
        file->flags = flags;
        file->fd_flags = 0;
        copy_text(file->path, sizeof(file->path), path);
        task->closed_stdio_mask &= ~bit;
        return (int)i;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        int fd = (int)i + 4;
        file = sched_task_file_at(task, i);
        if (!file || file->used) {
            continue;
        }
        if (task_pty_fd_for_fd(task, fd)) {
            continue;
        }
        file->used = 1;
        file->node = *node;
        file->offset = 0;
        file->aux = 0;
        file->aux2 = 0;
        file->flags = flags;
        file->fd_flags = 0;
        copy_text(file->path, sizeof(file->path), path);
        return fd;
    }
    {
        uint32_t i = sched_task_file_capacity(task);
        file = sched_task_file_at(task, i);
        int fd = (int)i + 4;
        if (file && !task_pty_fd_for_fd(task, fd)) {
            file->used = 1;
            file->node = *node;
            file->offset = 0;
            file->aux = 0;
            file->aux2 = 0;
            file->flags = flags;
            file->fd_flags = 0;
            copy_text(file->path, sizeof(file->path), path);
            return fd;
        }
    }
    return -LEONOS_EMFILE;
}


static int task_dup2_fd(struct task *task, int old_fd, int new_fd)
{
    struct task_file *old_file = task_file_for_fd(task, old_fd);
    struct task_file *new_file;
    struct task_pty_fd *replaced_pty = NULL;
    if (!task || old_fd < 0 || new_fd < 0 || !old_file) return -LEONOS_EBADF;
    if (old_fd == new_fd) return new_fd;
    if (new_fd < 3) {
        new_file = &task->stdio_files[new_fd];
    } else if (new_fd >= 4 && new_fd < 4 + (int)sched_task_file_capacity(task)) {
        new_file = sched_task_file_at(task, (uint32_t)(new_fd - 4));
        if (!new_file) return -LEONOS_EMFILE;
    } else {
        return -LEONOS_EBADF;
    }
    if (new_fd >= 4) {
        replaced_pty = task_pty_fd_for_fd(task, new_fd);
    }
    if (!new_file->used && !replaced_pty && new_fd >= 4 && !task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    if (new_file->used) clear_task_file(new_file);
    if (replaced_pty) {
        task_pty_release_entry(replaced_pty);
    }
    *new_file = *old_file;
    new_file->used = 1;
    new_file->fd_flags = 0;
    if (new_fd < (int)SCHED_TASK_STDIO_MAX) {
        task->closed_stdio_mask &= ~(1u << (uint32_t)new_fd);
    }
    task_pipe_retain(new_file);
    task_socket_retain(new_file);
    task_inet_retain(new_file);
    task_shm_retain(new_file);
    return new_fd;
}

static int task_duplicate_file_fd(struct task *task, int old_fd, int minimum_fd,
                                  uint32_t fd_flags)
{
    struct task_file *source = task_file_for_fd(task, old_fd);
    int fd;
    if (!source || minimum_fd < 0) {
        return -LEONOS_EBADF;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    if (minimum_fd < 4) {
        minimum_fd = 4;
    }
    for (fd = minimum_fd; fd < 4 + (int)sched_task_file_capacity(task); ++fd) {
        struct task_file *target;
        if (task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) {
            continue;
        }
        target = sched_task_file_at(task, (uint32_t)(fd - 4));
        if (!target) continue;
        *target = *source;
        target->used = 1;
        target->fd_flags = fd_flags;
        task_pipe_retain(target);
        task_socket_retain(target);
        task_inet_retain(target);
        task_shm_retain(target);
        return fd;
    }
    return -LEONOS_EMFILE;
}

int syscall_inherit_task_fds(struct task *parent, struct task *child,
                             int stdin_fd, int stdout_fd, int stderr_fd)
{
    const int requested[3] = {stdin_fd, stdout_fd, stderr_fd};
    if (!child) return -LEONOS_EINVAL;
    for (int i = 0; i < 3; ++i) {
        struct task_file *source;
        struct task_file *target = &child->stdio_files[i];
        if (requested[i] < 0) continue;
        source = task_file_for_fd(parent, requested[i]);
        if (parent && requested[i] < (int)SCHED_TASK_STDIO_MAX &&
            (parent->closed_stdio_mask & (1u << (uint32_t)requested[i])) != 0) {
            child->closed_stdio_mask |= 1u << (uint32_t)i;
            continue;
        }
        /* No explicit file means the child's PTY supplies this stream. */
        if (!source && requested[i] >= 0 && requested[i] <= 2) continue;
        if (!source) {
            return -LEONOS_EBADF;
        }
        if (target->used) clear_task_file(target);
        *target = *source;
        target->used = 1;
        task_pipe_retain(target);
        task_socket_retain(target);
        task_inet_retain(target);
        task_shm_retain(target);
    }
    return 0;
}

/**
 * File can read.
 * @param file Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int file_can_read(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_RDONLY || acc == LEONOS_O_RDWR;
}

/**
 * File can write.
 * @param file Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int file_can_write(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_WRONLY || acc == LEONOS_O_RDWR;
}

static int task_device_is(const struct task_file *file, uint32_t kind)
{
    return file && (file->flags & TASK_FILE_FLAG_DEV_NODE) &&
           file->node.first_cluster == kind;
}

static int task_block_device(const struct task_file *file, uint32_t *disk_id,
                             int32_t *partition_index)
{
    if (!file || !(file->flags & TASK_FILE_FLAG_DEV_BLOCK) ||
        !task_device_is(file, STORAGE_DEV_KIND_DISK)) return 0;
    if (disk_id) *disk_id = STORAGE_BLOCK_DISK_ID(file->node.volume_id);
    if (partition_index) *partition_index = STORAGE_BLOCK_PARTITION(file->node.volume_id);
    return 1;
}

static int task_device_read(struct task *task, struct task_file *file,
                            void *buffer, uint32_t length)
{
    uint32_t disk_id;
    int32_t partition_index;
    uint32_t got = 0;
    if (!task || !file || !buffer || !length) return 0;
    if (task_block_device(file, &disk_id, &partition_index)) {
        if (task_effective_role(task) != LEONOS_AUTH_ROLE_ADMIN &&
            !(task->uid == 0 && storage_installer_root_active())) {
            return -LEONOS_EACCES;
        }
        int ret = storage_disk_block_read(disk_id, partition_index, file->offset,
                                          buffer, length, &got);
        return ret < 0 ? ret : (int)got;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_NULL) ||
        task_device_is(file, STORAGE_DEV_KIND_FULL) ||
        task_device_is(file, STORAGE_DEV_KIND_CONSOLE) ||
        task_device_is(file, STORAGE_DEV_KIND_SERIAL)) return 0;
    if (task_device_is(file, STORAGE_DEV_KIND_ZERO) ||
        task_device_is(file, STORAGE_DEV_KIND_RANDOM) ||
        task_device_is(file, STORAGE_DEV_KIND_URANDOM)) {
        uint8_t *dst = (uint8_t *)buffer;
        for (uint32_t i = 0; i < length; ++i) dst[i] = 0;
        return (int)length;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_TTY)) {
        return (int)pty_read_input(task->pty_id, (char *)buffer, length);
    }
    if (task_device_is(file, STORAGE_DEV_KIND_KEYBOARD) ||
        task_device_is(file, STORAGE_DEV_KIND_MOUSE)) {
        int ret = input_evdev_read(file->node.first_cluster, &file->aux,
                                   buffer, length, file->aux2);
        if (ret == 0 && (file->flags & LEONOS_O_NONBLOCK)) {
            return -LEONOS_EAGAIN;
        }
        return ret;
    }
    /* The first OSS implementation is playback-only. */
    if (task_device_is(file, STORAGE_DEV_KIND_AUDIO)) {
        return -LEONOS_EBADF;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_KMSG)) {
        /* /dev/kmsg is a write-oriented diagnostics sink in this kernel;
         * reads report EOF instead of a private ioctl state. */
        return 0;
    }
    return -LEONOS_EBADF;
}

static int task_device_write(struct task *task, struct task_file *file,
                             const void *buffer, uint32_t length)
{
    uint32_t disk_id;
    int32_t partition_index;
    uint32_t wrote = 0;
    if (!task || !file) return -LEONOS_EBADF;
    if (task_block_device(file, &disk_id, &partition_index)) {
        if (task_effective_role(task) != LEONOS_AUTH_ROLE_ADMIN &&
            !(task->uid == 0 && storage_installer_root_active())) {
            return -LEONOS_EACCES;
        }
        int ret = storage_disk_block_write(disk_id, partition_index, file->offset,
                                           buffer, length, &wrote);
        return ret < 0 ? ret : (int)wrote;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_NULL) ||
        task_device_is(file, STORAGE_DEV_KIND_ZERO) ||
        task_device_is(file, STORAGE_DEV_KIND_RANDOM) ||
        task_device_is(file, STORAGE_DEV_KIND_URANDOM)) return (int)length;
    if (task_device_is(file, STORAGE_DEV_KIND_FULL)) return -LEONOS_ENOSPC;
    if (task_device_is(file, STORAGE_DEV_KIND_TTY) ||
        task_device_is(file, STORAGE_DEV_KIND_CONSOLE)) {
        if (!buffer) return -LEONOS_EFAULT;
        return (int)pty_write_output(task->pty_id, (const char *)buffer, length);
    }
    if (task_device_is(file, STORAGE_DEV_KIND_KMSG)) {
        if (!buffer) return -LEONOS_EFAULT;
        console_write_len((const char *)buffer, length);
        return (int)length;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_SERIAL)) {
        const char *src = (const char *)buffer;
        uint32_t done = 0;
        if (!buffer) return -LEONOS_EFAULT;
        /* The serial driver exposes a string-oriented backend.  Keep each
         * temporary chunk bounded and preserve the write length. */
        while (done < length) {
            char chunk[128];
            uint32_t take = length - done;
            if (take >= sizeof(chunk)) take = sizeof(chunk) - 1u;
            for (uint32_t i = 0; i < take; ++i) chunk[i] = src[done + i];
            chunk[take] = 0;
            serial_write(chunk);
            done += take;
        }
        return (int)done;
    }
    if (task_device_is(file, STORAGE_DEV_KIND_AUDIO)) {
        uint32_t status = LEONOS_AUDIO_STATUS_OK;
        long written;
        if (length && !buffer) return -LEONOS_EFAULT;
        written = driver_manager_audio_write(buffer, length, &status);
        if (written == 0 && status == LEONOS_AUDIO_STATUS_WOULD_BLOCK &&
            (file->flags & LEONOS_O_NONBLOCK)) {
            return -LEONOS_EAGAIN;
        }
        return (int)written;
    }
    return -LEONOS_EBADF;
}

static void evdev_copy_text(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int task_evdev_ioctl(struct task_file *file, uint64_t request,
                            uint64_t user_arg)
{
    uint32_t device_kind;
    uint32_t size;
    if (!file || !(file->flags & TASK_FILE_FLAG_DEV_NODE) ||
        (file->node.first_cluster != STORAGE_DEV_KIND_KEYBOARD &&
         file->node.first_cluster != STORAGE_DEV_KIND_MOUSE)) {
        return -LEONOS_ENOTTY;
    }
    device_kind = file->node.first_cluster;
    if (request == EVIOCGVERSION) {
        if (!user_range_ok(user_arg, sizeof(int))) return -LEONOS_EFAULT;
        *(int *)(uintptr_t)user_arg = 0x00010001;
        return 0;
    }
    if (request == EVIOCGID) {
        struct input_id id = {
            .bustype = BUS_I8042,
            .vendor = 0x0001,
            .product = device_kind == STORAGE_DEV_KIND_KEYBOARD ? 0x0001 : 0x0002,
            .version = 0x0100,
        };
        if (!user_range_ok(user_arg, sizeof(id))) return -LEONOS_EFAULT;
        *(struct input_id *)(uintptr_t)user_arg = id;
        return 0;
    }
    if (request == EVIOCGRAB) {
        int enable;
        int64_t token;
        if (!user_range_ok(user_arg, sizeof(int))) return -LEONOS_EFAULT;
        enable = *(int *)(uintptr_t)user_arg != 0;
        token = input_evdev_grab(device_kind, file->aux2, enable,
                                 sched_current_pid());
        if (token < 0) return (int)token;
        file->aux2 = (uint64_t)token;
        return 0;
    }
    if (_IOC_TYPE(request) != 'E') {
        return -LEONOS_ENOTTY;
    }
    size = _IOC_SIZE(request);
    if (!size || !user_range_ok(user_arg, size)) {
        return -LEONOS_EFAULT;
    }
    if (_IOC_NR(request) == 0x06 || _IOC_NR(request) == 0x07) {
        const char *value;
        if (_IOC_NR(request) == 0x06) {
            value = device_kind == STORAGE_DEV_KIND_KEYBOARD
                        ? "LeonOS PS/2 Keyboard" : "LeonOS PS/2 Mouse";
        } else {
            value = device_kind == STORAGE_DEV_KIND_KEYBOARD
                        ? "platform/i8042/serio0" : "platform/i8042/serio1";
        }
        evdev_copy_text((char *)(uintptr_t)user_arg, size, value);
        return 0;
    }
    if (_IOC_NR(request) == 0x18) {
        input_evdev_key_state((void *)(uintptr_t)user_arg, size);
        return 0;
    }
    if (_IOC_NR(request) == 0x40 + EV_ABS) {
        /* The PS/2 pointer is a relative device and has no ABS records. */
        return -LEONOS_EINVAL;
    }
    if (_IOC_NR(request) >= 0x20 && _IOC_NR(request) <= 0x20 + EV_MAX) {
        uint32_t event_type = _IOC_NR(request) - 0x20U;
        input_evdev_capabilities(device_kind, event_type,
                                 (void *)(uintptr_t)user_arg, size);
        return 0;
    }
    return -LEONOS_ENOTTY;
}

static int task_oss_dsp_state(struct leonos_audio_state *out)
{
    if (!out) return -LEONOS_EINVAL;
    driver_manager_audio_get_state(out);
    return out->present && out->active ? 0 : -LEONOS_ENODEV;
}

static int task_oss_dsp_set_rate(uint32_t requested_rate, uint32_t *actual_rate)
{
    struct leonos_audio_format format;
    struct leonos_audio_state state = {0};
    int ret;
    ret = task_oss_dsp_state(&state);
    if (ret < 0) return ret;
    format = (struct leonos_audio_format){
        .sample_rate = requested_rate ? requested_rate : state.sample_rate,
        .channels = 2U,
        .bits_per_sample = 16U,
    };
    if (!format.sample_rate) format.sample_rate = 48000U;
    ret = driver_manager_audio_configure(&format);
    if (ret < 0) return ret;
    driver_manager_audio_get_state(&state);
    if (actual_rate) *actual_rate = state.sample_rate;
    return state.active ? 0 : -LEONOS_ENODEV;
}

static int task_oss_dsp_writable(const struct task_file *file)
{
    struct leonos_audio_state state = {0};
    if (!task_device_is(file, STORAGE_DEV_KIND_AUDIO) || !file_can_write(file) ||
        task_oss_dsp_state(&state) < 0) {
        return 0;
    }
    return state.queued_bytes + OSS_DSP_FRAGMENT_BYTES <= OSS_DSP_QUEUE_BYTES;
}

static int task_oss_dsp_ioctl(struct task_file *file, uint64_t request,
                              uint64_t user_arg)
{
    struct leonos_audio_state state = {0};
    int value;
    int ret;
    if (!task_device_is(file, STORAGE_DEV_KIND_AUDIO)) return -LEONOS_ENOTTY;
    if (request != SNDCTL_DSP_RESET && request != SNDCTL_DSP_SYNC &&
        request != SNDCTL_DSP_NONBLOCK && request != SNDCTL_DSP_GETFMTS &&
        request != SNDCTL_DSP_GETCAPS && request != SNDCTL_DSP_GETBLKSIZE &&
        request != SNDCTL_DSP_SETFMT && request != SNDCTL_DSP_SPEED &&
        request != SNDCTL_DSP_STEREO && request != SNDCTL_DSP_CHANNELS &&
        request != SOUND_PCM_READ_CHANNELS && request != SOUND_PCM_READ_RATE &&
        request != SNDCTL_DSP_GETODELAY && request != SNDCTL_DSP_GETOSPACE) {
        return -LEONOS_ENOTTY;
    }
    ret = task_oss_dsp_state(&state);
    if (ret < 0) return ret;

    if (request == SNDCTL_DSP_RESET || request == SNDCTL_DSP_SYNC) {
        /* Hardware drains asynchronously. There is no software-only PCM
         * queue to reset or flush at this layer. */
        return 0;
    }
    if (request == SNDCTL_DSP_NONBLOCK) {
        file->flags |= LEONOS_O_NONBLOCK;
        return 0;
    }
    if (request == SNDCTL_DSP_GETFMTS || request == SNDCTL_DSP_GETCAPS ||
        request == SNDCTL_DSP_GETBLKSIZE || request == SNDCTL_DSP_SETFMT ||
        request == SNDCTL_DSP_SPEED || request == SNDCTL_DSP_STEREO ||
        request == SNDCTL_DSP_CHANNELS || request == SOUND_PCM_READ_CHANNELS ||
        request == SOUND_PCM_READ_RATE || request == SNDCTL_DSP_GETODELAY) {
        if (!user_range_ok(user_arg, sizeof(int))) return -LEONOS_EFAULT;
    }

    if (request == SNDCTL_DSP_GETFMTS) {
        *(int *)(uintptr_t)user_arg = AFMT_S16_LE;
        return 0;
    }
    if (request == SNDCTL_DSP_GETCAPS) {
        *(int *)(uintptr_t)user_arg = DSP_CAP_OUTPUT;
        return 0;
    }
    if (request == SNDCTL_DSP_GETBLKSIZE) {
        *(int *)(uintptr_t)user_arg = OSS_DSP_FRAGMENT_BYTES;
        return 0;
    }
    if (request == SNDCTL_DSP_SETFMT) {
        value = *(int *)(uintptr_t)user_arg;
        /* OSS callers use the returned value to detect the selected format. */
        *(int *)(uintptr_t)user_arg = AFMT_S16_LE;
        return value == AFMT_QUERY || value == AFMT_S16_LE ? 0 : -LEONOS_ENOTSUP;
    }
    if (request == SNDCTL_DSP_STEREO) {
        value = *(int *)(uintptr_t)user_arg;
        *(int *)(uintptr_t)user_arg = 1;
        return value == 0 || value == 1 ? 0 : -LEONOS_EINVAL;
    }
    if (request == SNDCTL_DSP_CHANNELS) {
        value = *(int *)(uintptr_t)user_arg;
        *(int *)(uintptr_t)user_arg = 2;
        return value == 0 || value == 2 ? 0 : -LEONOS_EINVAL;
    }
    if (request == SOUND_PCM_READ_CHANNELS) {
        *(int *)(uintptr_t)user_arg = 2;
        return 0;
    }
    if (request == SNDCTL_DSP_SPEED) {
        value = *(int *)(uintptr_t)user_arg;
        ret = task_oss_dsp_set_rate(value > 0 ? (uint32_t)value : 0, &state.sample_rate);
        if (ret < 0) return ret;
        *(int *)(uintptr_t)user_arg = (int)state.sample_rate;
        return 0;
    }
    if (request == SOUND_PCM_READ_RATE) {
        *(int *)(uintptr_t)user_arg = (int)state.sample_rate;
        return 0;
    }
    if (request == SNDCTL_DSP_GETODELAY) {
        *(int *)(uintptr_t)user_arg = (int)state.queued_bytes;
        return 0;
    }
    if (request == SNDCTL_DSP_GETOSPACE) {
        struct audio_buf_info info;
        uint32_t available;
        if (!user_range_ok(user_arg, sizeof(info))) return -LEONOS_EFAULT;
        driver_manager_audio_get_state(&state);
        available = state.queued_bytes < OSS_DSP_QUEUE_BYTES
                        ? OSS_DSP_QUEUE_BYTES - state.queued_bytes : 0;
        info = (struct audio_buf_info){
            .fragments = (int)(available / OSS_DSP_FRAGMENT_BYTES),
            .fragstotal = OSS_DSP_FRAGMENT_COUNT,
            .fragsize = OSS_DSP_FRAGMENT_BYTES,
            .bytes = (int)available,
        };
        *(struct audio_buf_info *)(uintptr_t)user_arg = info;
        return 0;
    }
    return -LEONOS_ENOTTY;
}

/**
 * Copy user path.
 * @param dst Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param user_ptr Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int copy_user_path(char *dst, uint32_t cap, uint64_t user_ptr)
{
    size_t len;
    if (!dst || !cap || !user_range_ok(user_ptr, 1)) {
        return -LEONOS_EFAULT;
    }
    len = user_strlen((const char *)(uintptr_t)user_ptr, cap);
    if (len == cap || !user_range_ok(user_ptr, len + 1)) {
        return -LEONOS_EFAULT;
    }
    for (size_t i = 0; i <= len; ++i) {
        dst[i] = ((const char *)(uintptr_t)user_ptr)[i];
    }
    return 0;
}

/**
 * Resolve user path.
 * @param task Value supplied by the caller.
 * @param user_ptr Value supplied by the caller.
 * @param out Output storage updated by the function.
 * @param cap Maximum number of elements available in the related buffer.
 * @return The value or status produced by the operation.
 */
static int resolve_user_path(struct task *task, uint64_t user_ptr, char *out, uint32_t cap)
{
    char raw[LEONOS_FS_PATH_LEN];
    int ret = copy_user_path(raw, sizeof(raw), user_ptr);
    if (ret < 0) {
        return ret;
    }
    ret = storage_resolve_path(task ? task->cwd : "/", raw, out, cap);
    if (ret < 0) {
        return -LEONOS_EINVAL;
    }
    normalize_dir_path(out);
    return 0;
}

/**
 * Storage errno.
 * @param ret Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int storage_errno(int ret)
{
    if (ret == -2) {
        return -LEONOS_ENOENT;
    }
    if (ret == -17) {
        return -LEONOS_EEXIST;
    }
    if (ret == -20) {
        return -LEONOS_ENOTDIR;
    }
    if (ret == -21) {
        return -LEONOS_EISDIR;
    }
    if (ret == -39) {
        return -LEONOS_ENOTEMPTY;
    }
    return ret;
}

/**
 * Copy user string fixed.
 * @param dst Value supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @param user_ptr Value supplied by the caller.
 * @param out_len Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int copy_user_string_fixed(char *dst, uint32_t cap, uint64_t user_ptr, uint32_t *out_len)
{
    size_t len;
    if (!dst || !cap) {
        return -LEONOS_EINVAL;
    }
    if (!user_ptr || !user_range_ok(user_ptr, 1)) {
        return -LEONOS_EFAULT;
    }
    len = user_strlen((const char *)(uintptr_t)user_ptr, cap);
    if (len == cap || !user_range_ok(user_ptr, len + 1)) {
        return -LEONOS_EFAULT;
    }
    for (size_t i = 0; i <= len; ++i) {
        dst[i] = ((const char *)(uintptr_t)user_ptr)[i];
    }
    if (out_len) {
        *out_len = (uint32_t)len;
    }
    return 0;
}

/**
 * Kernel string len cap.
 * @param text NUL-terminated text supplied by the caller.
 * @param cap Maximum number of elements available in the related buffer.
 * @return The value or status produced by the operation.
 */
static uint32_t kernel_string_len_cap(const char *text, uint32_t cap)
{
    uint32_t len = 0;
    while (text && len < cap && text[len]) {
        ++len;
    }
    return len;
}

/**
 * Kernel clear secret.
 * @param data Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 */
static void kernel_clear_secret(void *data, uint32_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (p && len) {
        *p++ = 0;
        --len;
    }
}

/**
 * Auth copy current user.
 * @param user Value supplied by the caller.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int auth_copy_current_user(struct leonos_user_info *user, const struct task *task)
{
    if (!user) {
        return -LEONOS_EINVAL;
    }
    *user = (struct leonos_user_info){0};
    if (!task || !task->uid) {
        user->role = LEONOS_AUTH_ROLE_NONE;
        return 0;
    }
    user->uid = task->uid;
    user->role = task_effective_role(task);
    copy_text(user->username, sizeof(user->username), task->username);
    copy_text(user->home, sizeof(user->home), task->home);
    return 0;
}

/**
 * Authz check path.
 * @param task Value supplied by the caller.
 * @param op Identifier or flags controlling the operation.
 * @param path NUL-terminated text supplied by the caller.
 * @param target_uid Value supplied by the caller.
 * @param target_role Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int authz_check_path(const struct task *task, uint32_t op,
                            const char *path, uint32_t target_uid,
                            uint32_t target_role)
{
    struct leonos_authz_request req;
    int ret;
    if (storage_installer_root_active() &&
        (op == LEONOS_AUTHZ_READ || op == LEONOS_AUTHZ_WRITE ||
         op == LEONOS_AUTHZ_EXEC || op == LEONOS_AUTHZ_DELETE ||
         op == LEONOS_AUTHZ_MANAGE || op == LEONOS_AUTHZ_INSTALL)) {
        return 0;
    }
    req = (struct leonos_authz_request){0};
    if (task) {
        req.uid = task->uid;
        req.role = task_effective_role(task);
        req.session_id = task->session_id;
        if ((task->flags & TASK_FLAG_SERVICE) &&
            !(task->flags & TASK_FLAG_WINDOW_SERVER)) {
            req.actor_flags |= LEONOS_AUTHZ_ACTOR_SERVICE;
        }
        copy_text(req.username, sizeof(req.username), task->username);
        copy_text(req.home, sizeof(req.home), task->home);
    }
    req.op = op;
    req.target_uid = target_uid;
    req.target_role = target_role;
    if (path) {
        copy_text(req.path, sizeof(req.path), path);
    }
    ret = osmlayer_auth_op(LEONOS_AUTH_OP_AUTHORIZE, &req);
    if (ret < 0) {
        return ret;
    }
    return req.allowed ? 0 : -LEONOS_EACCES;
}

/**
 * Authz check install.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int authz_check_install(const struct task *task)
{
    return authz_check_path(task, LEONOS_AUTHZ_INSTALL, 0, 0, 0);
}

/**
 * Fs acl fill actor.
 * @param req Caller-owned structure read or updated by the function.
 * @param task Value supplied by the caller.
 */
static void fs_acl_fill_actor(struct leonos_fs_acl_request *req,
                              const struct task *task)
{
    if (!req) {
        return;
    }
    if (task) {
        req->actor_uid = task->uid;
        req->actor_role = task_effective_role(task);
        if ((task->flags & TASK_FLAG_SERVICE) &&
            !(task->flags & TASK_FLAG_WINDOW_SERVER)) {
            req->actor_flags |= LEONOS_AUTHZ_ACTOR_SERVICE;
        }
        copy_text(req->username, sizeof(req->username), task->username);
        copy_text(req->home, sizeof(req->home), task->home);
    } else {
        req->actor_role = LEONOS_AUTH_ROLE_NONE;
    }
}

/**
 * Fs acl dispatch.
 * @param req Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
static int fs_acl_dispatch(struct leonos_fs_acl_request *req)
{
    if (!req) {
        return -LEONOS_EINVAL;
    }
    return osmlayer_auth_op(LEONOS_AUTH_OP_FSPERM, req);
}

/**
 * Fs acl notify.
 * @param action Value supplied by the caller.
 * @param task Value supplied by the caller.
 * @param path NUL-terminated text supplied by the caller.
 * @param path2 Value supplied by the caller.
 */
static void fs_acl_notify(uint32_t action, const struct task *task,
                          const char *path, const char *path2)
{
    struct leonos_fs_acl_request req;
    if (!path || !path[0]) {
        return;
    }
    req = (struct leonos_fs_acl_request){0};
    req.action = action;
    copy_text(req.path, sizeof(req.path), path);
    if (path2) {
        copy_text(req.path2, sizeof(req.path2), path2);
    }
    fs_acl_fill_actor(&req, task);
    /**
 * Fs acl handle ioctl.
 * @param req Caller-owned structure read or updated by the function.
 * @return The value or status produced by the operation.
 */
    (void)fs_acl_dispatch(&req);
}

static int fs_acl_std_chmod(const char *path, uint32_t mode)
{
    struct task *task = sched_current_task();
    struct leonos_fs_acl_request req = {0};
    int ret;
    if (!path || !path[0] || (mode & ~07777u)) return -LEONOS_EINVAL;
    copy_text(req.path, sizeof(req.path), path);
    if (storage_resolve_path(task ? task->cwd : "/", req.path,
                             req.path, sizeof(req.path)) < 0) {
        return -LEONOS_EINVAL;
    }
    fs_acl_fill_actor(&req, task);
    ret = authz_check_path(task, LEONOS_AUTHZ_MANAGE, req.path, 0, 0);
    if (ret < 0) return ret;
    req.action = LEONOS_FS_ACL_ACTION_GET;
    if (fs_acl_dispatch(&req) < 0) {
        req.acl.owner_uid = task ? task->uid : 0;
    }
    req.acl.version = LEONOS_FS_ACL_VERSION;
    req.acl.flags |= LEONOS_FS_ACL_FLAG_SYNTHETIC;
    req.acl.ace_count = 2;
    req.acl.aces[0] = (struct leonos_fs_acl_ace){
        .principal = LEONOS_FS_ACL_PRINCIPAL_OWNER,
        .flags = 0,
        .permissions = ((mode >> 6) & 7u),
        .reserved = 0,
    };
    req.acl.aces[1] = (struct leonos_fs_acl_ace){
        .principal = LEONOS_FS_ACL_PRINCIPAL_EVERYONE,
        .flags = 0,
        .permissions = mode & 7u,
        .reserved = 0,
    };
    req.action = LEONOS_FS_ACL_ACTION_SET;
    ret = fs_acl_dispatch(&req);
    return ret < 0 ? ret : 0;
}

static int fs_acl_std_chown(const char *path, uint32_t owner, uint32_t group)
{
    struct task *task = sched_current_task();
    struct leonos_fs_acl_request req = {0};
    int ret;
    (void)group;
    if (!path || !path[0]) return -LEONOS_EINVAL;
    copy_text(req.path, sizeof(req.path), path);
    if (storage_resolve_path(task ? task->cwd : "/", req.path,
                             req.path, sizeof(req.path)) < 0) {
        return -LEONOS_EINVAL;
    }
    fs_acl_fill_actor(&req, task);
    ret = authz_check_path(task, LEONOS_AUTHZ_MANAGE, req.path, 0, 0);
    if (ret < 0) return ret;
    req.action = LEONOS_FS_ACL_ACTION_GET;
    (void)fs_acl_dispatch(&req);
    req.acl.version = LEONOS_FS_ACL_VERSION;
    req.acl.owner_uid = owner;
    req.action = LEONOS_FS_ACL_ACTION_SET;
    ret = fs_acl_dispatch(&req);
    return ret < 0 ? ret : 0;
}

/**
 * Auth apply session login.
 * @param caller Value supplied by the caller.
 * @param user Value supplied by the caller.
 */
static void auth_apply_session_login(struct task *caller,
                                     const struct leonos_user_info *user)
{
    uint32_t session_id;
    uint32_t root_pid;
    if (!caller || !user || !user->uid) {
        return;
    }
    session_id = sched_next_session_id();
    root_pid = caller->parent_pid ? caller->parent_pid : caller->pid;
    sched_set_session_identity(root_pid, user, session_id);
    sched_set_task_identity(caller->pid, user, session_id);
}

/**
 * Auth cleanup logged out task.
 * @param pid Identifier or flags controlling the operation.
 */
static void auth_cleanup_logged_out_task(uint32_t pid)
{
    net_close_owner_sockets(pid);
    pty_process_exit(pid);
}

/**
 * Auth kill session tasks for logout.
 * @param uid Identifier or flags controlling the operation.
 * @param session_id Identifier or flags controlling the operation.
 * @param keep_pid Value supplied by the caller.
 */
static void auth_kill_session_tasks_for_logout(uint32_t uid, uint32_t session_id,
                                               uint32_t keep_pid)
{
    struct task_snapshot_info *tasks;
    uint64_t tick;
    uint32_t count;
    if (!uid || !session_id) {
        return;
    }
    count = sched_snapshot(NULL, 0, &tick);
    if (!count || count > UINT32_MAX / sizeof(*tasks)) {
        return;
    }
    tasks = (struct task_snapshot_info *)kernel_malloc(
        (size_t)count * sizeof(*tasks));
    if (!tasks) {
        return;
    }
    count = sched_snapshot(tasks, count, &tick);
    for (uint32_t i = 0; i < count; ++i) {
        const struct task_snapshot_info *task = &tasks[i];
        if (task->pid == 0 || task->pid == keep_pid ||
            task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
            task->uid != uid || task->session_id != session_id ||
            (task->flags & TASK_FLAG_SERVICE)) {
            continue;
        }
        if (sched_kill_user_task(task->pid, 0) == 0) {
            auth_cleanup_logged_out_task(task->pid);
        }
    }
    kernel_free(tasks);
}

/**
 * Startup release file.
 * @param data Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 */
static void startup_release_file(const void *data, size_t len)
{
    uint32_t pages = (uint32_t)((len + 4095U) / 4096U);
    if (data && pages) {
        mm_free_pages((uint64_t)(uintptr_t)data, pages);
    }
}

/**
 * Startup command is well formed.
 * @param command Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_command_is_well_formed(const struct leonos_startup_command *command)
{
    if (!command || !command->path[0] || command->argc > LEONOS_STARTUP_MAX_ARGS ||
        kernel_string_len_cap(command->path, sizeof(command->path)) >= sizeof(command->path)) {
        return 0;
    }
    for (uint32_t i = 0; i < command->argc; ++i) {
        if (kernel_string_len_cap(command->args[i], sizeof(command->args[i])) >=
            sizeof(command->args[i])) {
            return 0;
        }
    }
    return 1;
}

/**
 * Startup db is well formed.
 * @param db Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_db_is_well_formed(const struct startup_db *db)
{
    if (!db || db->magic != STARTUP_DB_MAGIC || db->count > STARTUP_DB_ENTRY_MAX ||
        !db->next_id) {
        return 0;
    }
    for (uint32_t i = 0; i < db->count; ++i) {
        if (!db->entries[i].uid || !db->entries[i].entry.id ||
            !startup_command_is_well_formed(&db->entries[i].entry.command)) {
            return 0;
        }
    }
    return 1;
}

/**
 * Startup denial db is well formed.
 * @param db Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_denial_db_is_well_formed(const struct startup_denial_db *db)
{
    if (!db || db->magic != STARTUP_DENIAL_DB_MAGIC || db->count > STARTUP_DENIAL_MAX) {
        return 0;
    }
    for (uint32_t i = 0; i < db->count; ++i) {
        if (!db->entries[i].uid ||
            kernel_string_len_cap(db->entries[i].requester_path,
                                  sizeof(db->entries[i].requester_path)) >=
                sizeof(db->entries[i].requester_path) ||
            !startup_command_is_well_formed(&db->entries[i].command)) {
            return 0;
        }
    }
    return 1;
}

/**
 * Startup db load.
 */
static void startup_db_load(void)
{
    const void *data = 0;
    size_t len = 0;
    startup_db_scratch = (struct startup_db){0};
    startup_db_scratch.magic = STARTUP_DB_MAGIC;
    startup_db_scratch.next_id = 1;
    if (storage_read_file(STARTUP_DB_PATH, &data, &len) == 0 &&
        data && len == sizeof(startup_db_scratch)) {
        const struct startup_db *saved = (const struct startup_db *)data;
        if (startup_db_is_well_formed(saved)) {
            startup_db_scratch = *saved;
        }
    }
    startup_release_file(data, len);
}

/**
 * Startup db save.
 * @return The value or status produced by the operation.
 */
static int startup_db_save(void)
{
    (void)storage_mkdir("/system");
    (void)storage_mkdir("/system/state");
    return storage_write_file(STARTUP_DB_PATH, &startup_db_scratch,
                              sizeof(startup_db_scratch));
}

/**
 * Startup denial db load.
 */
static void startup_denial_db_load(void)
{
    const void *data = 0;
    size_t len = 0;
    startup_denial_db_scratch = (struct startup_denial_db){0};
    startup_denial_db_scratch.magic = STARTUP_DENIAL_DB_MAGIC;
    if (storage_read_file(STARTUP_DENIAL_DB_PATH, &data, &len) == 0 &&
        data && len == sizeof(startup_denial_db_scratch)) {
        const struct startup_denial_db *saved = (const struct startup_denial_db *)data;
        if (startup_denial_db_is_well_formed(saved)) {
            startup_denial_db_scratch = *saved;
        }
    }
    startup_release_file(data, len);
}

/**
 * Startup denial db save.
 * @return The value or status produced by the operation.
 */
static int startup_denial_db_save(void)
{
    (void)storage_mkdir("/system");
    (void)storage_mkdir("/system/state");
    return storage_write_file(STARTUP_DENIAL_DB_PATH, &startup_denial_db_scratch,
                              sizeof(startup_denial_db_scratch));
}

/**
 * Startup text eq.
 * @param a Value supplied by the caller.
 * @param b Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_text_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

/**
 * Startup command equal.
 * @param a Value supplied by the caller.
 * @param b Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_command_equal(const struct leonos_startup_command *a,
                                 const struct leonos_startup_command *b)
{
    if (!a || !b || a->argc != b->argc || !startup_text_eq(a->path, b->path)) {
        return 0;
    }
    for (uint32_t i = 0; i < a->argc; ++i) {
        if (!startup_text_eq(a->args[i], b->args[i])) {
            return 0;
        }
    }
    return 1;
}

/**
 * Startup command validate.
 * @param command Value supplied by the caller.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_command_validate(struct leonos_startup_command *command,
                                    const struct task *task)
{
    char resolved[LEONOS_FS_PATH_LEN];
    struct leonos_stat st;
    uint32_t exec_bytes;
    int ret;
    if (!command || !task || !command->path[0] ||
        command->argc > LEONOS_STARTUP_MAX_ARGS ||
        kernel_string_len_cap(command->path, sizeof(command->path)) >= sizeof(command->path)) {
        return -LEONOS_EINVAL;
    }
    for (uint32_t i = 0; i < command->argc; ++i) {
        uint32_t arg_len = kernel_string_len_cap(command->args[i], sizeof(command->args[i]));
        if (arg_len >= sizeof(command->args[i])) {
            return -LEONOS_EINVAL;
        }
    }
    ret = storage_resolve_path(task->cwd, command->path, resolved, sizeof(resolved));
    if (ret < 0) {
        return -LEONOS_EINVAL;
    }
    ret = storage_stat_path(resolved, &st);
    if (ret < 0) {
        return ret == -2 ? -LEONOS_EINVAL : ret;
    }
    if (st.type != LEONOS_FS_TYPE_FILE) {
        return -LEONOS_EINVAL;
    }
    ret = authz_check_path(task, LEONOS_AUTHZ_EXEC, resolved, 0, 0);
    if (ret < 0) {
        return ret;
    }
    copy_text(command->path, sizeof(command->path), resolved);
    exec_bytes = kernel_string_len_cap(command->path, sizeof(command->path)) + 1U;
    for (uint32_t i = 0; i < command->argc; ++i) {
        exec_bytes += kernel_string_len_cap(command->args[i], sizeof(command->args[i])) + 1U;
    }
    if (exec_bytes > SCHED_EXEC_DATA_MAX) {
        return -LEONOS_EINVAL;
    }
    command->reserved = 0;
    for (uint32_t i = command->argc; i < LEONOS_STARTUP_MAX_ARGS; ++i) {
        command->args[i][0] = 0;
    }
    return 0;
}

/**
 * Startup can manage uid.
 * @param task Value supplied by the caller.
 * @param uid Identifier or flags controlling the operation.
 * @return The value or status produced by the operation.
 */
static int startup_can_manage_uid(const struct task *task, uint32_t uid)
{
    return task && task->uid && uid &&
           (task_effective_role(task) == LEONOS_AUTH_ROLE_ADMIN ||
            task->uid == uid);
}

/**
 * Startup db find.
 * @param uid Identifier or flags controlling the operation.
 * @param command Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_db_find(uint32_t uid, const struct leonos_startup_command *command)
{
    for (uint32_t i = 0; i < startup_db_scratch.count; ++i) {
        if (startup_db_scratch.entries[i].uid == uid &&
            startup_command_equal(&startup_db_scratch.entries[i].entry.command, command)) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Startup denial find.
 * @param uid Identifier or flags controlling the operation.
 * @param requester_path NUL-terminated text supplied by the caller.
 * @param command Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_denial_find(uint32_t uid, const char *requester_path,
                               const struct leonos_startup_command *command)
{
    for (uint32_t i = 0; i < startup_denial_db_scratch.count; ++i) {
        if (startup_denial_db_scratch.entries[i].uid == uid &&
            startup_text_eq(startup_denial_db_scratch.entries[i].requester_path, requester_path) &&
            startup_command_equal(&startup_denial_db_scratch.entries[i].command, command)) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Startup remember denial.
 * @param uid Identifier or flags controlling the operation.
 * @param requester_path NUL-terminated text supplied by the caller.
 * @param command Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_remember_denial(uint32_t uid, const char *requester_path,
                                   const struct leonos_startup_command *command)
{
    if (startup_denial_find(uid, requester_path, command) >= 0 ||
        startup_denial_db_scratch.count >= STARTUP_DENIAL_MAX) {
        return -LEONOS_E2BIG;
    }
    uint32_t i = startup_denial_db_scratch.count++;
    startup_denial_db_scratch.entries[i].uid = uid;
    copy_text(startup_denial_db_scratch.entries[i].requester_path,
              sizeof(startup_denial_db_scratch.entries[i].requester_path), requester_path);
    startup_denial_db_scratch.entries[i].command = *command;
    return startup_denial_db_save();
}

/**
 * Startup request find.
 * @param id Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static struct startup_request_slot *startup_request_find(uint32_t id)
{
    for (uint32_t i = 0; i < STARTUP_REQUEST_MAX; ++i) {
        if (startup_requests[i].used && startup_requests[i].id == id) {
            return &startup_requests[i];
        }
    }
    return 0;
}

/**
 * Startup request reconcile.
 * @param slot Value supplied by the caller.
 */
static void startup_request_reconcile(struct startup_request_slot *slot)
{
    struct task *dialog;
    if (!slot || slot->status != LEONOS_STARTUP_STATUS_PENDING) {
        return;
    }
    dialog = sched_find(slot->dialog_pid);
    if (!dialog || dialog->state == TASK_EXITED) {
        slot->status = LEONOS_STARTUP_STATUS_DENIED;
    }
}

/**
 * Startup request alloc.
 * @return The value or status produced by the operation.
 */
static struct startup_request_slot *startup_request_alloc(void)
{
    for (uint32_t i = 0; i < STARTUP_REQUEST_MAX; ++i) {
        struct task *requester;
        startup_request_reconcile(&startup_requests[i]);
        requester = startup_requests[i].used
                        ? sched_find(startup_requests[i].requester_pid) : 0;
        if (startup_requests[i].used && startup_requests[i].status != LEONOS_STARTUP_STATUS_PENDING &&
            (!requester || requester->state == TASK_EXITED)) {
            startup_requests[i].used = 0;
        }
        if (!startup_requests[i].used) {
            startup_requests[i] = (struct startup_request_slot){0};
            startup_requests[i].used = 1;
            startup_requests[i].id = startup_next_request_id++;
            if (!startup_next_request_id) {
                startup_next_request_id = 1;
            }
            return &startup_requests[i];
        }
    }
    return 0;
}

/**
 * Startup add entry.
 * @param uid Identifier or flags controlling the operation.
 * @param command Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_add_entry(uint32_t uid, const struct leonos_startup_command *command)
{
    int existing;
    uint32_t user_entry_count = 0;
    startup_db_load();
    existing = startup_db_find(uid, command);
    if (existing >= 0) {
        return 1;
    }
    for (uint32_t i = 0; i < startup_db_scratch.count; ++i) {
        if (startup_db_scratch.entries[i].uid == uid) {
            ++user_entry_count;
        }
    }
    if (user_entry_count >= LEONOS_STARTUP_MAX_ENTRIES) {
        return -LEONOS_E2BIG;
    }
    if (startup_db_scratch.count >= STARTUP_DB_ENTRY_MAX) {
        return -LEONOS_E2BIG;
    }
    uint32_t i = startup_db_scratch.count++;
    startup_db_scratch.entries[i].uid = uid;
    startup_db_scratch.entries[i].entry.id = startup_db_scratch.next_id++;
    if (!startup_db_scratch.next_id) {
        startup_db_scratch.next_id = 1;
    }
    startup_db_scratch.entries[i].entry.enabled = 1;
    startup_db_scratch.entries[i].entry.command = *command;
    return startup_db_save();
}

/**
 * Startup dialog spawn.
 * @param slot Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int startup_dialog_spawn(struct startup_request_slot *slot)
{
    const char *argv[] = {SYSCONFDIALOG_APP_PATH, 0};
    int64_t pid;
    if (!slot || !slot->user.uid || !slot->session_id) {
        return -LEONOS_EINVAL;
    }
    pid = userland_spawn_path_argv_for_user(SYSCONFDIALOG_APP_PATH, argv, 0,
                                            slot->requester_pid, &slot->user,
                                            slot->session_id);
    if (pid <= 0) {
        slot->status = LEONOS_STARTUP_STATUS_FAILED;
        return (int)pid;
    }
    slot->dialog_pid = (uint32_t)pid;
    return 0;
}

/**
 * Copy user vector.
 * @param user_ptr Value supplied by the caller.
 * @param max_count Value supplied by the caller.
 * @param out_ptrs Value supplied by the caller.
 * @param data Value supplied by the caller.
 * @param data_cap Value supplied by the caller.
 * @param out_count Value supplied by the caller.
 * @param data_len Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int copy_user_vector(uint64_t user_ptr, uint32_t max_count,
                            char *out_ptrs[], char *data,
                            uint32_t data_cap, uint32_t *out_count, uint32_t *data_len)
{
    uint64_t *user_vec = (uint64_t *)(uintptr_t)user_ptr;
    uint32_t count = 0;
    if (!out_ptrs || !data || !out_count || !data_len) {
        return -LEONOS_EINVAL;
    }
    if (!user_ptr) {
        *out_count = 0;
        return 0;
    }
    for (;;) {
        uint64_t entry_ptr;
        if (count >= max_count) {
            return -LEONOS_E2BIG;
        }
        if (!user_range_ok((uint64_t)(uintptr_t)&user_vec[count], sizeof(uint64_t))) {
            return -LEONOS_EFAULT;
        }
        entry_ptr = user_vec[count];
        if (!entry_ptr) {
            break;
        }
        uint32_t len = 0;
        uint32_t start = *data_len;
        int ret = copy_user_string_fixed(data + start, data_cap - start, entry_ptr, &len);
        if (ret < 0) {
            return ret;
        }
        out_ptrs[count] = data + start;
        *data_len += len + 1;
        ++count;
    }
    out_ptrs[count] = 0;
    *out_count = count;
    return 0;
}

/**
 * Copy exec params from user.
 * @param task Value supplied by the caller.
 * @param path_ptr Value supplied by the caller.
 * @param argv_ptr Value supplied by the caller.
 * @param envp_ptr Value supplied by the caller.
 * @param path_out Value supplied by the caller.
 * @param path_cap Value supplied by the caller.
 * @param params Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int copy_exec_params_from_user(struct task *task, uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr,
                                      char *path_out, uint32_t path_cap, struct exec_params_kernel *params)
{
    int ret;
    uint32_t data_len = 0;
    if (!params) {
        return -LEONOS_EINVAL;
    }
    for (uint32_t i = 0; i < SCHED_EXEC_ARG_MAX + 1; ++i) {
        params->argv[i] = 0;
    }
    for (uint32_t i = 0; i < SCHED_EXEC_ENV_MAX + 1; ++i) {
        params->envp[i] = 0;
    }
    params->argc = 0;
    params->envc = 0;
    params->data_len = 0;

    ret = resolve_user_path(task, path_ptr, path_out, path_cap);
    if (ret < 0) {
        return ret;
    }
    if (!argv_ptr) {
        uint32_t len = 0;
        while (path_out[len]) {
            if (len + 1 >= sizeof(params->data)) {
                return -LEONOS_E2BIG;
            }
            params->data[len] = path_out[len];
            ++len;
        }
        params->data[len++] = 0;
        params->argv[0] = params->data;
        params->argv[1] = 0;
        params->argc = 1;
        data_len = len;
    } else {
        ret = copy_user_vector(argv_ptr, SCHED_EXEC_ARG_MAX, params->argv, params->data,
                               sizeof(params->data), &params->argc, &data_len);
        if (ret < 0) {
            return ret;
        }
        if (params->argc == 0) {
            return -LEONOS_EINVAL;
        }
    }
    ret = copy_user_vector(envp_ptr, SCHED_EXEC_ENV_MAX, params->envp, params->data,
                           sizeof(params->data), &params->envc, &data_len);
    if (ret < 0) {
        return ret;
    }
    params->data_len = data_len;
    return 0;
}

/**
 * Stat for fd.
 * @param fd Value supplied by the caller.
 * @param task Value supplied by the caller.
 * @param st Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int stat_for_fd(int fd, struct task *task, struct leonos_stat *st)
{
    struct task_file *file;
    if (!st) {
        return -LEONOS_EFAULT;
    }
    file = task_file_for_fd(task, fd);
    if (file) {
        st->type = ((file->flags & (TASK_FILE_FLAG_PIPE | TASK_FILE_FLAG_DEV_NULL)) != 0)
                       ? LEONOS_FS_TYPE_DEVICE
                                                         : file->node.type;
        st->reserved = 0;
        st->size = file->node.size;
        return 0;
    }
    if (fd >= 0 && fd < (int)SCHED_TASK_STDIO_MAX &&
        (task->closed_stdio_mask & (1u << (uint32_t)fd)) != 0) {
        return -LEONOS_EBADF;
    }
    if (fd >= 0 && fd <= 2) {
        st->type = LEONOS_FS_TYPE_DEVICE;
        st->reserved = 0;
        st->size = 0;
        return 0;
    }
    return -LEONOS_EBADF;
}

int64_t syscall_poll(uint64_t fds_ptr, uint64_t count, int64_t timeout_ms)
{
    struct task *task = sched_current_task();
        struct pollfd *fds;
    uint64_t ready = 0;
    (void)timeout_ms;

    if (!task || count > 1024U || (count && !user_range_ok(fds_ptr,
                                                            count * sizeof(*fds)))) {
        return -LEONOS_EFAULT;
    }
    if (!count) {
        return 0;
    }
    fds = (struct pollfd *)(uintptr_t)fds_ptr;
    for (uint64_t i = 0; i < count; ++i) {
        short events = fds[i].events;
        short revents = 0;
        int fd = fds[i].fd;
        struct task_file *file;
        fds[i].revents = 0;
        if (fd < 0) {
            continue;
        }
        file = task_file_for_fd(task, fd);
        {
            struct task_pty_fd *endpoint = task_pty_endpoint_for_fd(task, fd);
            if (endpoint) {
                if (pty_is_hungup(endpoint->pty_id)) {
                    if (events & POLLIN) revents |= POLLIN;
                    revents |= POLLHUP;
                    if (endpoint->endpoint == TASK_PTY_ENDPOINT_SLAVE) {
                        revents |= POLLERR;
                    }
                } else {
                    if (events & POLLIN) {
                        uint32_t available = endpoint->endpoint == TASK_PTY_ENDPOINT_MASTER
                                                 ? pty_output_available(endpoint->pty_id)
                                                 : pty_input_available(endpoint->pty_id);
                        if (available) revents |= POLLIN;
                    }
                    if (events & POLLOUT) revents |= POLLOUT;
                }
                fds[i].revents = revents;
                if (revents) ++ready;
                continue;
            }
        }
        if (file && (file->flags & TASK_FILE_FLAG_PIPE)) {
            revents = task_pipe_poll(file, events);
        } else if (file && (file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) {
            revents = task_socket_poll(file, events);
        } else if (file && (file->flags & TASK_FILE_FLAG_SOCKET_INET)) {
            revents = task_inet_poll(file, events);
        } else if (file && (file->flags & TASK_FILE_FLAG_DEV_NODE)) {
            if (task_device_is(file, STORAGE_DEV_KIND_KEYBOARD) ||
                task_device_is(file, STORAGE_DEV_KIND_MOUSE)) {
                if ((events & POLLIN) &&
                    input_evdev_available(file->node.first_cluster, file->aux,
                                          file->aux2)) {
                    revents |= POLLIN;
                }
            } else if (task_device_is(file, STORAGE_DEV_KIND_AUDIO)) {
                struct leonos_audio_state audio = {0};
                if (task_oss_dsp_state(&audio) < 0) {
                    revents |= POLLERR;
                } else if ((events & POLLOUT) && task_oss_dsp_writable(file)) {
                    revents |= POLLOUT;
                }
            } else if (task_device_is(file, STORAGE_DEV_KIND_TTY)) {
                if (pty_input_available(task->pty_id)) revents |= POLLIN;
                if (pty_is_hungup(task->pty_id)) {
                    revents |= POLLHUP;
                }
            } else if (events & POLLIN) {
                revents |= POLLIN;
            }
            if ((events & POLLOUT) && file_can_write(file) &&
                !task_device_is(file, STORAGE_DEV_KIND_KEYBOARD) &&
                !task_device_is(file, STORAGE_DEV_KIND_MOUSE) &&
                !task_device_is(file, STORAGE_DEV_KIND_AUDIO)) {
                revents |= POLLOUT;
            }
        } else if (file) {
            if ((events & POLLIN) && file_can_read(file)) {
                if (file->node.type == LEONOS_FS_TYPE_DIR || file->offset < file->node.size)
                    revents |= POLLIN;
            }
            if ((events & POLLOUT) && file_can_write(file)) revents |= POLLOUT;
        } else if (fd >= 0 && fd <= 2 && task_pty_stream_for_fd(task, fd) >= 0) {
            if (fd == 0) {
                if (pty_input_available(task->pty_id)) revents |= POLLIN;
                if (pty_is_hungup(task->pty_id)) revents |= POLLHUP;
            } else if (pty_is_hungup(task->pty_id)) {
                revents |= POLLHUP;
            } else if (events & POLLOUT) {
                revents |= POLLOUT;
            }
        } else if (!task_pty_fd_for_fd(task, fd)) {
            revents |= POLLNVAL;
        } else if (events & POLLOUT) {
            revents |= POLLOUT;
        }
        fds[i].revents = revents;
        if (revents) ++ready;
    }
    return (int64_t)ready;
}

void syscall_init(void)
{
    console_printf("[ntclks] Linux x86_64 syscall ABI registered\n");
}

/**
 * @brief Handles process identity, signal, and nice-style priority syscalls.
 * @param number Linux syscall number.
 * @param a0 First syscall argument.
 * @param a1 Second syscall argument.
 * @param a2 Third syscall argument.
 * @param a3 Fourth syscall argument.
 * @return Syscall result or negative errno.
 */
/**
 * Syscall dispatch.
 * @param frame Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int64_t syscall_dispatch(const struct syscall_frame *frame)
{
    if (!frame) {
        return -LEONOS_EFAULT;
    }

    switch (frame->number) {
    case LINUX_SYS_READ:
    case LINUX_SYS_WRITE:
    case LINUX_SYS_OPEN:
    case LINUX_SYS_OPENAT:
    case LINUX_SYS_CLOSE:
    case LINUX_SYS_STAT:
    case LINUX_SYS_FSTAT:
    case LINUX_SYS_LSEEK:
    case LINUX_SYS_FTRUNCATE:
    case LINUX_SYS_IOCTL:
    case LINUX_SYS_POLL:
    case LINUX_SYS_SCHED_YIELD:
    case LINUX_SYS_NANOSLEEP:
    case LINUX_SYS_EXECVE:
    case LINUX_SYS_EXIT:
    case LINUX_SYS_WAIT4:
    case LINUX_SYS_CHMOD:
    case LINUX_SYS_FCHMOD:
    case LINUX_SYS_CHOWN:
    case LINUX_SYS_FCHOWN:
    case LINUX_SYS_GETCWD:
    case LINUX_SYS_CHDIR:
    case LINUX_SYS_RENAME:
    case LINUX_SYS_MKDIR:
    case LINUX_SYS_RMDIR:
    case LINUX_SYS_UNLINK:
    case LINUX_SYS_PIPE:
    case LINUX_SYS_PIPE2:
    case LINUX_SYS_DUP:
    case LINUX_SYS_DUP2:
    case LINUX_SYS_DUP3:
    case LINUX_SYS_FORK:
    case LINUX_SYS_VFORK:
    case LINUX_SYS_FCNTL:
    case LINUX_SYS_SOCKET:
    case LINUX_SYS_SOCKETPAIR:
    case LINUX_SYS_SENDMSG:
    case LINUX_SYS_RECVMSG:
    case LINUX_SYS_ACCEPT4:
    case LINUX_SYS_CONNECT:
    case LINUX_SYS_ACCEPT:
    case LINUX_SYS_BIND:
    case LINUX_SYS_LISTEN:
    case LINUX_SYS_GETSOCKNAME:
    case LINUX_SYS_GETSOCKOPT:
    case LINUX_SYS_SETSOCKOPT:
    case LINUX_SYS_SHUTDOWN:
    case LINUX_SYS_SEND:
    case LINUX_SYS_RECV:
    case LINUX_SYS_RT_SIGACTION:
    case LINUX_SYS_RT_SIGPROCMASK:
    case LINUX_SYS_RT_SIGSUSPEND:
        return syscall_dispatch_regs(frame->number, frame->args[0], frame->args[1],
                                     frame->args[2], frame->args[3], frame->args[4],
                                     frame->args[5]);
    case LINUX_SYS_RT_SIGRETURN:
        /* rt_sigreturn restores the live trap frame directly. The regs-only
         * debug dispatch entry has no frame to restore and returns ENOSYS. */
        return -LEONOS_ENOSYS;
    case LINUX_SYS_GETUID:
    case LINUX_SYS_GETGID:
    case LINUX_SYS_GETEUID:
    case LINUX_SYS_GETEGID:
    case LINUX_SYS_SETUID:
    case LINUX_SYS_SETGID:
    case LINUX_SYS_UNAME:
    case LINUX_SYS_GETTIMEOFDAY:
    case LINUX_SYS_SETTIMEOFDAY:
    case LINUX_SYS_SCHED_SETAFFINITY:
    case LINUX_SYS_SCHED_GETAFFINITY:
    case LINUX_SYS_CLOCK_GETTIME:
    case LINUX_SYS_REBOOT:
    case LINUX_SYS_GETPID:
    case LINUX_SYS_GETPPID:
    case LINUX_SYS_SETPGID:
    case LINUX_SYS_GETPGRP:
    case LINUX_SYS_SETSID:
    case LINUX_SYS_GETPGID:
    case LINUX_SYS_KILL:
    case LINUX_SYS_NICE:
    case LINUX_SYS_GETPRIORITY:
    case LINUX_SYS_SETPRIORITY:
        return syscall_process_control(frame->number, frame->args[0], frame->args[1],
                                       frame->args[2], frame->args[3]);
    case LINUX_SYS_GETRLIMIT:
    case LINUX_SYS_SETRLIMIT:
        return syscall_process_control(frame->number, frame->args[0], frame->args[1],
                                       frame->args[2], frame->args[3]);
    case LINUX_SYS_MMAP:
        return syscall_mm_mmap(frame->args[0], frame->args[1], frame->args[2],
                        frame->args[3], frame->args[4], frame->args[5]);
    case LINUX_SYS_MUNMAP:
        return syscall_mm_munmap(frame->args[0], frame->args[1]);
    case LINUX_SYS_MPROTECT:
        return syscall_mm_mprotect(frame->args[0], frame->args[1], frame->args[2]);
    default:
        return -LEONOS_ENOSYS;
    }
}

/**
 * Syscall dispatch regs legacy.
 * @param number Value supplied by the caller.
 * @param a0 Value supplied by the caller.
 * @param a1 Value supplied by the caller.
 * @param a2 Value supplied by the caller.
 * @param a3 Value supplied by the caller.
 * @param a4 Value supplied by the caller.
 * @param a5 Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int64_t syscall_dispatch_regs_legacy(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (number == LINUX_SYS_SOCKET || number == LINUX_SYS_CONNECT ||
        number == LINUX_SYS_ACCEPT || number == LINUX_SYS_ACCEPT4 ||
        number == LINUX_SYS_SOCKETPAIR || number == LINUX_SYS_BIND ||
        number == LINUX_SYS_LISTEN || number == LINUX_SYS_GETSOCKNAME ||
        number == LINUX_SYS_GETSOCKOPT || number == LINUX_SYS_SETSOCKOPT ||
        number == LINUX_SYS_SHUTDOWN || number == LINUX_SYS_SEND ||
        number == LINUX_SYS_RECV || number == LINUX_SYS_SENDTO ||
        number == LINUX_SYS_RECVFROM || number == LINUX_SYS_SENDMSG ||
        number == LINUX_SYS_RECVMSG) {
        return syscall_socket_dispatch(number, a0, a1, a2, a3, a4);
    }
    if (number == LINUX_SYS_GETUID || number == LINUX_SYS_GETGID ||
        number == LINUX_SYS_GETEUID || number == LINUX_SYS_GETEGID ||
        number == LINUX_SYS_SETUID || number == LINUX_SYS_SETGID ||
        number == LINUX_SYS_UNAME || number == LINUX_SYS_GETTIMEOFDAY ||
        number == LINUX_SYS_SETTIMEOFDAY || number == LINUX_SYS_SCHED_SETAFFINITY ||
        number == LINUX_SYS_SCHED_GETAFFINITY || number == LINUX_SYS_REBOOT) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_RT_SIGACTION || number == LINUX_SYS_RT_SIGPROCMASK ||
        number == LINUX_SYS_RT_SIGSUSPEND) {
        return syscall_linux_signal(number, a0, a1, a2, a3, a4);
    }
    if (number == LINUX_SYS_POLL) {
        return syscall_poll(a0, a1, (int64_t)a2);
    }
    /* openat is the canonical Linux file-open entry point.  LeonOS currently
     * has no directory-FD lookup object, so AT_FDCWD is handled by the same
     * path resolver as open(2); other dirfds fail explicitly instead of
     * silently ignoring the caller's directory. */
    if (number == LINUX_SYS_OPENAT) {
        if (a0 != (uint64_t)-100) { /* AT_FDCWD */
            struct task *task = sched_current_task();
            struct task_file *directory = task_file_for_fd(task, (int)a0);
            char raw[LEONOS_FS_PATH_LEN];
            char saved_cwd[LEONOS_FS_PATH_LEN];
            int ret;
            if (!task || !directory) return -LEONOS_EBADF;
            if (directory->node.type != LEONOS_FS_TYPE_DIR) return -LEONOS_ENOTDIR;
            ret = copy_user_path(raw, sizeof(raw), a1);
            if (ret < 0) return ret;
            if (raw[0] != '/') {
                copy_text(saved_cwd, sizeof(saved_cwd), task->cwd);
                copy_text(task->cwd, sizeof(task->cwd), directory->path[0] ? directory->path : "/");
                ret = (int)syscall_dispatch_regs_legacy(LINUX_SYS_OPEN, a1, a2, a3, a4, a5, 0);
                copy_text(task->cwd, sizeof(task->cwd), saved_cwd);
                return ret;
            }
            number = LINUX_SYS_OPEN;
            a0 = a1;
            a1 = a2;
            a2 = a3;
        } else {
            number = LINUX_SYS_OPEN;
            a0 = a1;
            a1 = a2;
            a2 = a3;
        }
    }

    if (number == LINUX_SYS_CHMOD || number == LINUX_SYS_CHOWN) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = copy_user_path(path, sizeof(path), a0);
        if (ret < 0) return ret;
        if (storage_resolve_path(task ? task->cwd : "/", path, path,
                                 sizeof(path)) < 0) {
            return -LEONOS_ENOENT;
        }
        return number == LINUX_SYS_CHMOD
                   ? fs_acl_std_chmod(path, (uint32_t)a1)
                   : fs_acl_std_chown(path, (uint32_t)a1, (uint32_t)a2);
    }
    if (number == LINUX_SYS_FCHMOD || number == LINUX_SYS_FCHOWN) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file || !file->path[0]) return -LEONOS_EBADF;
        return number == LINUX_SYS_FCHMOD
                   ? fs_acl_std_chmod(file->path, (uint32_t)a1)
                   : fs_acl_std_chown(file->path, (uint32_t)a1, (uint32_t)a2);
    }

    return -LEONOS_ENOSYS;
}

/**
 * Syscall dispatch regs.
 * @param frame Value supplied by the caller.
 */
static int64_t syscall_dispatch_regs(uint64_t number, uint64_t a0, uint64_t a1,
                                     uint64_t a2, uint64_t a3, uint64_t a4,
                                     uint64_t a5)
{
    if (syscall_fs_owns(number)) {
        return syscall_fs_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    if (syscall_ipc_owns(number)) {
        return syscall_ipc_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    if (syscall_security_owns(number, a1)) {
        return syscall_security_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    if (syscall_gui_owns(number, a1)) {
        return syscall_gui_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    if (syscall_device_owns(number, a1)) {
        return syscall_device_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}

/**
 * @brief Return 1 when an EAGAIN result is a real nonblocking-device status.
 *
 * The legacy storage path reuses EAGAIN to rewind and retry the int 0x80
 * instruction after a one-tick DMA wait.  POSIX descriptors opened with
 * O_NONBLOCK must observe EAGAIN instead.
 */
static int syscall_eagain_is_nonblocking_device(struct task *task,
                                                uint64_t number, uint64_t fd)
{
    struct task_pty_fd *endpoint;
    struct task_file *file;
    if (number != LINUX_SYS_READ && number != LINUX_SYS_WRITE) {
        return 0;
    }
    endpoint = task_pty_endpoint_for_fd(task, (int)fd);
    if (endpoint) {
        return (endpoint->status_flags & LEONOS_O_NONBLOCK) != 0;
    }
    file = task_file_for_fd(task, (int)fd);
    if (file && (file->flags & LEONOS_O_NONBLOCK)) {
        if (file->flags & (TASK_FILE_FLAG_PIPE |
                           TASK_FILE_FLAG_SOCKET_UNIX |
                           TASK_FILE_FLAG_SOCKET_INET)) {
            return 1;
        }
        if ((file->flags & TASK_FILE_FLAG_DEV_NODE) &&
            (task_device_is(file, STORAGE_DEV_KIND_KEYBOARD) ||
             task_device_is(file, STORAGE_DEV_KIND_MOUSE) ||
             task_device_is(file, STORAGE_DEV_KIND_AUDIO))) {
            return 1;
        }
    }
    return 0;
}

void syscall_dispatch_frame(struct trap_frame *frame)
{
    uint64_t number;
    uint64_t lock_flags;
    int64_t result;
    int restored_signal_frame = 0;
    int eagain_from_nonblocking = 0;
    if (!frame) {
        return;
    }
    /* Storage, GUI IPC, startup databases, text layout scratch and several
     * legacy device services remain globally buffered. User instructions run
     * in parallel; serialize only their kernel service transition. */
    kernel_execution_lock_irqsave(&lock_flags);
    number = frame->rax;
    storage_set_io_async_context(true);
    if (number == LINUX_SYS_RT_SIGRETURN) {
        struct task *task = sched_current_task();
        result = task ? kernel_signal_rt_sigreturn(task, frame) : -LEONOS_EPERM;
        restored_signal_frame = result >= 0;
    } else if (number == LINUX_SYS_FORK || number == LINUX_SYS_VFORK) {
        /**
 * @brief vfork intentionally uses fork semantics for now: sharing the address space would let the child corrupt its suspended parent.
 */
        result = sched_fork_current(frame);
    } else {
        result = syscall_dispatch_regs(number,
                                       frame->rdi,
                                       frame->rsi,
                                       frame->rdx,
                                       frame->r10,
                                       frame->r8,
                                       frame->r9);
    }
    eagain_from_nonblocking =
        result == -LEONOS_EAGAIN &&
        syscall_eagain_is_nonblocking_device(sched_current_task(),
                                             number, frame->rdi);
    if (result != -LEONOS_EAGAIN || eagain_from_nonblocking) {
        storage_release_task_io(sched_current_pid());
    }
    storage_set_io_async_context(false);
    if (restored_signal_frame) {
        /* The restored context already contains the interrupted syscall's
         * original rax; do not overwrite it with rt_sigreturn's return value. */
    } else if (result == -LEONOS_EAGAIN && !eagain_from_nonblocking) {
        /* int $0x80 has advanced RIP by two bytes.  Park this task for one
         * timer tick and re-execute the exact same instruction when its AHCI
         * DMA request can be polled again.  User programs keep normal
         * blocking read/open/stat semantics and never observe EAGAIN. */
        frame->rax = number;
        frame->rip -= 2u;
        kernel_execution_unlock_irqrestore(lock_flags);
        sched_sleep_current_until(time_ticks() + 1u);
        return;
    }
    if (!restored_signal_frame) {
        frame->rax = (uint64_t)result;
    }
    kernel_execution_unlock_irqrestore(lock_flags);
}
