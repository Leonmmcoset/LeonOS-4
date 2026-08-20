/*
 * LeonOS kernel system calls: validates and dispatches the user ABI.
 * Implements process, file, memory, IPC, GUI, device, and timing operations.
 */
#include <ntclks/console.h>
#include <ntclks/driver_manager.h>
#include <ntclks/framebuffer.h>
#include <ntclks/heap.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/input.h>
#include <ntclks/inputm.h>
#include <ntclks/mm.h>
#include <ntclks/mouse.h>
#include <ntclks/net.h>
#include <ntclks/osmlayer.h>
#include <ntclks/platform.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
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
#include <leonos/inputm.h>
#include <leonos/net.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <leonos/startup.h>
#include <leonos/text.h>

#define LEONOS_GUI_IOCTL_EVENT 0x4c455654ULL
#define LEONOS_GUI_IOCTL_VERSION 0x4c475549ULL
#define LEONOS_GUI_IOCTL_UPTIME_MS 0x4c555054ULL
#define LEONOS_GUI_IOCTL_FB_INFO 0x4c464249ULL
#define LEONOS_GUI_IOCTL_FB_FILL 0x4c464246ULL
#define LEONOS_GUI_IOCTL_FB_RECT 0x4c464252ULL
#define LEONOS_GUI_IOCTL_FB_TEXT 0x4c464254ULL
#define LEONOS_GUI_IOCTL_FB_PIXEL 0x4c464250ULL
#define LEONOS_GUI_IOCTL_FB_BLIT 0x4c46424cULL
#define LEONOS_GUI_IOCTL_FB_SET_MODE 0x4c46424dULL
#define LEONOS_GUI_IOCTL_FB_CAPS 0x4c464243ULL
#define LEONOS_GUI_IOCTL_CREATE_WINDOW 0x4c475743ULL
#define LEONOS_GUI_IOCTL_POLL_WINDOW 0x4c475750ULL
#define LEONOS_GUI_IOCTL_TASKS 0x4c54534bULL
#define LEONOS_GUI_IOCTL_PRESENT_WINDOW 0x4c475046ULL
#define LEONOS_GUI_IOCTL_FETCH_WINDOW 0x4c475746ULL
#define LEONOS_GUI_IOCTL_WINDOW_EVENT 0x4c475745ULL
#define LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT 0x4c475457ULL
#define LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT 0x4c475753ULL
#define LEONOS_GUI_IOCTL_DESTROY_WINDOW 0x4c475744ULL
#define LEONOS_GUI_IOCTL_TASK_KILL 0x4c544b49ULL
#define LEONOS_GUI_IOCTL_REBOOT 0x4c524254ULL
#define LEONOS_GUI_IOCTL_SHUTDOWN 0x4c534844ULL
#define LEONOS_GUI_IOCTL_DISPLAY_STATE 0x4c445350ULL
#define LEONOS_GUI_IOCTL_DISPLAY_REQUEST 0x4c445351ULL
#define LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST 0x4c445352ULL
#define LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE 0x4c445353ULL
#define LEONOS_TEXT_LAYOUT_MAX_BYTES 4096U
#define LEONOS_TEXT_LAYOUT_MAX_GLYPHS 512U
#define PAGE_SIZE 4096ULL
#define LINUX_PROT_READ TASK_VMA_PROT_READ
#define LINUX_PROT_WRITE TASK_VMA_PROT_WRITE
#define LINUX_PROT_EXEC TASK_VMA_PROT_EXEC
#define LINUX_MAP_PRIVATE 0x02u
#define LINUX_MAP_FIXED 0x10u
#define LINUX_MAP_ANONYMOUS 0x20u
#define LINUX_MAP_SUPPORTED (LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS)
#define OOBE_DHCP_APP_PATH "0:/system/apps/oobe/oobe.elf"
#define OOBE_DONE_MARKER_PATH "0:/system/state/oobe.done"
#define SYSCONFDIALOG_APP_PATH "0:/system/apps/sysconfdialog/sysconfdialog.elf"
#define STARTUP_DB_PATH "0:/system/state/startup.db"
#define STARTUP_DENIAL_DB_PATH "0:/system/state/startup-denials.db"
#define STARTUP_DB_MAGIC 0x53545031U
#define STARTUP_DENIAL_DB_MAGIC 0x53544431U
#define STARTUP_DB_ENTRY_MAX 64U
#define STARTUP_DENIAL_MAX 64U
#define STARTUP_REQUEST_MAX 16U

static struct leonos_user_info auth_user_scratch[LEONOS_AUTH_MAX_USERS];
static struct leonos_disk_partition disk_partition_scratch[LEONOS_DISK_MAX_PARTITIONS];

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
 * @brief Coordinates the task effective role operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
static uint32_t task_effective_role(const struct task *task);

/**
 * @brief Coordinates the device copy text operation.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
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
 * @brief Coordinates the device append char operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param ch Input or output value used by this operation.
 */
static void device_append_char(char *buf, uint32_t *pos, uint32_t cap, char ch)
{
    if (buf && pos && *pos + 1 < cap) {
        buf[(*pos)++] = ch;
        buf[*pos] = 0;
    }
}

/**
 * @brief Coordinates the text eq cstr operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the oobe dhcp renew allowed operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
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
 * @brief Validates permission for window server.
 * @return Result, status, or value defined by this API.
 */
static int require_window_server(void)
{
    struct task *task = sched_current_task();
    if (!task || !(task->flags & TASK_FLAG_WINDOW_SERVER)) {
        return -LEONOS_EPERM;
    }
    return 0;
}

/**
 * @brief Validates permission for network config access.
 * @return Result, status, or value defined by this API.
 */
static int require_network_config_access(void)
{
    struct task *task = sched_current_task();
    if (storage_installer_root_active()) {
        return 0;
    }
    if (task_effective_role(task) == LEONOS_AUTH_ROLE_ADMIN) {
        return 0;
    }
    if (task && (task->flags & TASK_FLAG_SERVICE) &&
        !(task->flags & TASK_FLAG_WINDOW_SERVER)) {
        return 0;
    }
    if (oobe_dhcp_renew_allowed(task)) {
        return 0;
    }
    return -LEONOS_EPERM;
}

/**
 * @brief Validates permission for driver management.
 * @return Result, status, or value defined by this API.
 */
static int require_driver_management(void)
{
    struct task *task = sched_current_task();
    return task_effective_role(task) == LEONOS_AUTH_ROLE_ADMIN
               ? 0
               : -LEONOS_EPERM;
}

/**
 * @brief Coordinates the task effective role operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
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
 * @brief Validates permission for background service.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the device append text operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param text Input or output value used by this operation.
 */
static void device_append_text(char *buf, uint32_t *pos, uint32_t cap, const char *text)
{
    while (text && *text) {
        device_append_char(buf, pos, cap, *text++);
    }
}

/**
 * @brief Coordinates the device append u64 operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param value Input or output value used by this operation.
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
 * @brief Coordinates the device append i32 operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param value Input or output value used by this operation.
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
 * @brief Coordinates the device append ipv4 operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param pos Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param ip Input or output value used by this operation.
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
 * @brief Coordinates the device add operation.
 * @param devices Input or output value used by this operation.
 * @param capacity Capacity, in elements or bytes, of the related output buffer.
 * @param count Length, size, or element count associated with the operation.
 * @param device_class Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @param name Input or output value used by this operation.
 * @param status Input or output value used by this operation.
 * @param detail Input or output value used by this operation.
 * @param value0 Input or output value used by this operation.
 * @param value1 Input or output value used by this operation.
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
 * @brief Coordinates the raw device add operation.
 * @param raw Input or output value used by this operation.
 * @param count Length, size, or element count associated with the operation.
 * @param kind Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @param aux0 Input or output value used by this operation.
 * @param aux1 Input or output value used by this operation.
 * @param value0 Input or output value used by this operation.
 * @param value1 Input or output value used by this operation.
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
 * @brief Coordinates the device format fb operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param fb Input or output value used by this operation.
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
 * @brief Coordinates the device format mouse operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param mouse Input or output value used by this operation.
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
 * @brief Coordinates the device format disk operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param disk Input or output value used by this operation.
 */
static void device_format_disk(char *buf, uint32_t cap, const struct leonos_install_disk *disk)
{
    uint32_t pos = 0;
    uint64_t mib = disk ? (disk->sector_count * (uint64_t)disk->sector_size) / (1024ULL * 1024ULL) : 0;
    buf[0] = 0;
    device_append_text(buf, &pos, cap, "AHCI port ");
    device_append_u64(buf, &pos, cap, disk ? disk->port : 0);
    device_append_text(buf, &pos, cap, ", ");
    device_append_u64(buf, &pos, cap, mib);
    device_append_text(buf, &pos, cap, " MiB, sector ");
    device_append_u64(buf, &pos, cap, disk ? disk->sector_size : 0);
}

/**
 * @brief Coordinates the device format time operation.
 * @param buf Buffer consumed or filled by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param time Input or output value used by this operation.
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

struct gui_create_window_user {
    uint32_t width;
    uint32_t height;
    const char *title;
    const char *text;
    uint32_t flags;
};

struct gui_present_window_user {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    const uint32_t *pixels;
};

struct gui_fetch_window_user {
    uint32_t window_id;
    uint32_t capacity_width;
    uint32_t capacity_height;
    uint32_t stride;
    uint32_t out_width;
    uint32_t out_height;
    uint32_t *pixels;
};

struct gui_wait_app_event_user {
    struct gui_ipc_app_event event;
    uint32_t timeout_ms;
};

struct gui_window_update_user {
    uint32_t window_id;
    uint32_t mask;
    uint32_t flags;
    const char *title;
};

struct gui_taskbar_request_user {
    uint32_t window_id;
    uint32_t visible;
};

struct gui_cursor_request_user {
    uint32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t style;
    uint32_t flags;
};

struct exec_params_kernel {
    uint32_t argc;
    uint32_t envc;
    char *argv[SCHED_EXEC_ARG_MAX + 1];
    char *envp[SCHED_EXEC_ENV_MAX + 1];
    char data[SCHED_EXEC_DATA_MAX];
    uint32_t data_len;
};

/**
 * @brief Coordinates the normalize dir path operation.
 * @param path LeonOS path consumed by this operation.
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
 * @brief Copies text.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param src Input or output value used by this operation.
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
 * @brief Coordinates the clear task file operation.
 * @param file Input or output value used by this operation.
 */
void clear_task_file(struct task_file *file)
{
    /* Descriptor cleanup can be reached both from the immediate exit path
     * and from later zombie reaping.  A released pipe end must only decrement
     * its shared reference count once. */
    if (!file || !file->used) {
        return;
    }
    task_pipe_release(file);
    file->used = 0;
    file->node.type = 0;
    file->node.flags = 0;
    file->node.first_cluster = 0;
    file->node.drive = 0;
    file->node.size = 0;
    file->offset = 0;
    file->aux = 0;
    file->read_cursor = (struct storage_read_cursor){0};
    file->flags = 0;
    file->fd_flags = 0;
    file->path[0] = 0;
}

/**
 * @brief Coordinates the clear task files operation.
 * @param task Task whose state or authority is inspected or updated.
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
 * @brief Coordinates the syscall release task files operation.
 * @param task Task whose state or authority is inspected or updated.
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
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_STDIO_MAX; ++i) {
        if (child->stdio_files[i].used) {
            task_pipe_retain(&child->stdio_files[i]);
        }
    }
    return 0;
}

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
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        if (task->pty_fds[i].used && (task->pty_fds[i].flags & 1u)) {
            task->pty_fds[i] = (struct task_pty_fd){0};
        }
    }
}

/**
 * @brief Coordinates the task file for fd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param fd Open file descriptor used by this operation.
 * @return Result, status, or value defined by this API.
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
#define LEONOS_FD_CLOEXEC 1

/**
 * @brief Coordinates the task pty fd for fd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param fd Open file descriptor used by this operation.
 * @return Result, status, or value defined by this API.
 */
struct task_pty_fd *task_pty_fd_for_fd(struct task *task, int fd)
{
    if (!task || !task->pty_id || fd < 4) {
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

/**
 * @brief Coordinates the task pty stream for fd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param fd Open file descriptor used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int task_pty_stream_for_fd(struct task *task, int fd)
{
    struct task_pty_fd *entry;
    if (!task || !task->pty_id) {
        return -1;
    }
    /* A redirected stdio descriptor shadows the implicit PTY stream. */
    if (task_file_for_fd(task, fd)) {
        return -1;
    }
    if (fd >= 0 && fd <= 2) {
        return fd;
    }
    entry = task_pty_fd_for_fd(task, fd);
    return entry ? (int)entry->stream : -1;
}

/**
 * @brief Coordinates the task pty fd available operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param fd Open file descriptor used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int task_pty_fd_available(struct task *task, int fd)
{
    if (fd < 4 || task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) {
        return 0;
    }
    return 1;
}

/* RLIMIT_NOFILE applies to descriptors allocated in addition to the three
 * implicit PTY standard streams.  Files and explicit PTY aliases share the
 * same process limit even though they use separate backing tables. */
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
 * @brief Coordinates the task pty duplicate fd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param old_fd Input or output value used by this operation.
 * @param minimum_fd Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int task_pty_duplicate_fd(struct task *task, int old_fd, int minimum_fd,
                                 uint32_t flags)
{
    int stream = task_pty_stream_for_fd(task, old_fd);
    int candidate;
    if (stream < 0 || minimum_fd < 0) {
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
                entry->stream = (uint32_t)stream;
                entry->flags = flags;
                return candidate;
            }
        }
        return -LEONOS_EMFILE;
    }
    return -LEONOS_EMFILE;
}

/**
 * @brief Coordinates the task pty dup2 fd operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param old_fd Input or output value used by this operation.
 * @param new_fd Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int task_pty_dup2_fd(struct task *task, int old_fd, int new_fd)
{
    int stream = task_pty_stream_for_fd(task, old_fd);
    struct task_pty_fd *entry;
    if (stream < 0 || new_fd < 0) {
        return -LEONOS_EBADF;
    }
    if (old_fd == new_fd) {
        return new_fd;
    }
    /* Restore an implicit PTY stream after a temporary redirection. */
    if (new_fd < 3) {
        struct task_file *stdio = &task->stdio_files[new_fd];
        if (stdio->used) {
            clear_task_file(stdio);
        }
        return new_fd;
    }
    if (new_fd == 3) {
        return -LEONOS_EBADF;
    }
    /* dup2() replaces an existing descriptor regardless of its backing kind. */
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
    entry->stream = (uint32_t)stream;
    entry->flags = 0;
    return new_fd;
}

/**
 * @brief Allocates task fd.
 * @param task Task whose state or authority is inspected or updated.
 * @param node Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
static int alloc_task_fd(struct task *task, const struct storage_node *node, uint32_t flags, const char *path)
{
    if (!task || !node) {
        return -LEONOS_EINVAL;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        int fd = (int)i + 4;
        struct task_file *file = sched_task_file_at(task, i);
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
        file->flags = flags;
        file->fd_flags = 0;
        copy_text(file->path, sizeof(file->path), path);
        return fd;
    }
    {
        uint32_t i = sched_task_file_capacity(task);
        struct task_file *file = sched_task_file_at(task, i);
        int fd = (int)i + 4;
        if (file && !task_pty_fd_for_fd(task, fd)) {
            file->used = 1;
            file->node = *node;
            file->offset = 0;
            file->aux = 0;
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
        *replaced_pty = (struct task_pty_fd){0};
    }
    *new_file = *old_file;
    new_file->used = 1;
    new_file->fd_flags = 0;
    task_pipe_retain(new_file);
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
        /* No explicit file means the child's PTY supplies this stream. */
        if (!source && requested[i] >= 0 && requested[i] <= 2) continue;
        if (!source) {
            return -LEONOS_EBADF;
        }
        if (target->used) clear_task_file(target);
        *target = *source;
        target->used = 1;
        task_pipe_retain(target);
    }
    return 0;
}

/**
 * @brief Coordinates the file can read operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int file_can_read(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_RDONLY || acc == LEONOS_O_RDWR;
}

/**
 * @brief Coordinates the file can write operation.
 * @param file Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int file_can_write(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_WRONLY || acc == LEONOS_O_RDWR;
}

/**
 * @brief Copies user path.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param user_ptr Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the resolve user path operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param user_ptr Input or output value used by this operation.
 * @param out Caller-provided storage that receives output from this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
 */
static int resolve_user_path(struct task *task, uint64_t user_ptr, char *out, uint32_t cap)
{
    char raw[LEONOS_FS_PATH_LEN];
    int ret = copy_user_path(raw, sizeof(raw), user_ptr);
    if (ret < 0) {
        return ret;
    }
    ret = storage_resolve_path(task ? task->cwd : "0:/", raw, out, cap);
    if (ret < 0) {
        return -LEONOS_EINVAL;
    }
    normalize_dir_path(out);
    return 0;
}

/**
 * @brief Coordinates the storage errno operation.
 * @param ret Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Copies user string fixed.
 * @param dst Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param user_ptr Input or output value used by this operation.
 * @param out_len Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the kernel string len cap operation.
 * @param text Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the kernel clear secret operation.
 * @param data Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
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
 * @brief Coordinates the auth copy current user operation.
 * @param user Input or output value used by this operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the authz check path operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param op Input or output value used by this operation.
 * @param path LeonOS path consumed by this operation.
 * @param target_uid Input or output value used by this operation.
 * @param target_role Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the authz check install operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
 */
static int authz_check_install(const struct task *task)
{
    return authz_check_path(task, LEONOS_AUTHZ_INSTALL, 0, 0, 0);
}

/**
 * @brief Coordinates the fs acl fill actor operation.
 * @param req Input or output value used by this operation.
 * @param task Task whose state or authority is inspected or updated.
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
 * @brief Coordinates the fs acl dispatch operation.
 * @param req Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fs_acl_dispatch(struct leonos_fs_acl_request *req)
{
    if (!req) {
        return -LEONOS_EINVAL;
    }
    return osmlayer_auth_op(LEONOS_AUTH_OP_FSPERM, req);
}

/**
 * @brief Coordinates the fs acl notify operation.
 * @param action Input or output value used by this operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param path LeonOS path consumed by this operation.
 * @param path2 LeonOS path consumed by this operation.
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
 * @brief Coordinates the fs acl dispatch operation.
 * @param req Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
    (void)fs_acl_dispatch(&req);
}

/**
 * @brief Coordinates the fs acl handle ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param user_arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int fs_acl_handle_ioctl(uint64_t request, uint64_t user_arg)
{
    struct task *task = sched_current_task();
    struct leonos_fs_acl_request req;
    int ret;
    if (!user_range_ok(user_arg, sizeof(req))) {
        return -LEONOS_EFAULT;
    }
    req = *(struct leonos_fs_acl_request *)(uintptr_t)user_arg;
    if (kernel_string_len_cap(req.path, sizeof(req.path)) >= sizeof(req.path)) {
        return -LEONOS_EFAULT;
    }
    if (storage_resolve_path(task ? task->cwd : "0:/", req.path,
                             req.path, sizeof(req.path)) < 0) {
        return -LEONOS_EINVAL;
    }
    req.actor_uid = 0;
    req.actor_role = 0;
    req.actor_flags = 0;
    req.status = 0;
    req.username[0] = 0;
    req.home[0] = 0;
    fs_acl_fill_actor(&req, task);

    if (request == LEONOS_FS_IOCTL_ACL_GET) {
        req.action = LEONOS_FS_ACL_ACTION_GET;
        ret = authz_check_path(task, LEONOS_AUTHZ_READ, req.path, 0, 0);
        if (ret < 0 && task_effective_role(task) != LEONOS_AUTH_ROLE_ADMIN) {
            return ret;
        }
    } else if (request == LEONOS_FS_IOCTL_ACL_SET) {
        req.action = LEONOS_FS_ACL_ACTION_SET;
        ret = authz_check_path(task, LEONOS_AUTHZ_MANAGE, req.path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        if (req.acl.version != LEONOS_FS_ACL_VERSION ||
            req.acl.ace_count > LEONOS_FS_ACL_MAX_ACE) {
            return -LEONOS_EINVAL;
        }
    } else if (request == LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP) {
        req.action = LEONOS_FS_ACL_ACTION_TAKE_OWNERSHIP;
    } else if (request == LEONOS_FS_IOCTL_ACL_REPAIR) {
        req.action = LEONOS_FS_ACL_ACTION_REPAIR;
    } else {
        return -LEONOS_ENOSYS;
    }

    ret = fs_acl_dispatch(&req);
    if (ret < 0) {
        return ret;
    }
    *(struct leonos_fs_acl_request *)(uintptr_t)user_arg = req;
    return 0;
}

/**
 * @brief Coordinates the auth apply session login operation.
 * @param caller Input or output value used by this operation.
 * @param user Input or output value used by this operation.
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
 * @brief Coordinates the auth cleanup logged out task operation.
 * @param pid Input or output value used by this operation.
 */
static void auth_cleanup_logged_out_task(uint32_t pid)
{
    net_close_owner_sockets(pid);
    gui_ipc_destroy_owner(pid);
    pty_process_exit(pid);
}

/**
 * @brief Coordinates the auth kill session tasks for logout operation.
 * @param uid Input or output value used by this operation.
 * @param session_id Input or output value used by this operation.
 * @param keep_pid Input or output value used by this operation.
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
 * @brief Coordinates the auth handle ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param user_arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int auth_handle_ioctl(uint64_t request, uint64_t user_arg)
{
    struct task *task = sched_current_task();
    if (request == LEONOS_AUTH_IOCTL_STATUS) {
        struct leonos_auth_status status;
        int ret;
        if (!user_range_ok(user_arg, sizeof(status))) {
            return -LEONOS_EFAULT;
        }
        status = (struct leonos_auth_status){0};
        ret = osmlayer_auth_op(LEONOS_AUTH_OP_STATUS, &status);
        if (ret < 0) {
            return ret;
        }
        *(struct leonos_auth_status *)(uintptr_t)user_arg = status;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_CURRENT) {
        struct leonos_user_info user;
        if (!user_range_ok(user_arg, sizeof(user))) {
            return -LEONOS_EFAULT;
        }
        auth_copy_current_user(&user, task);
        *(struct leonos_user_info *)(uintptr_t)user_arg = user;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_LIST_USERS) {
        struct leonos_user_list *user_list;
        struct leonos_user_list list;
        struct leonos_user_info *users = auth_user_scratch;
        uint32_t copy_count;
        int ret;
        if (!user_range_ok(user_arg, sizeof(list))) {
            return -LEONOS_EFAULT;
        }
        user_list = (struct leonos_user_list *)(uintptr_t)user_arg;
        list = *user_list;
        if (list.capacity > LEONOS_AUTH_MAX_USERS) {
            list.capacity = LEONOS_AUTH_MAX_USERS;
        }
        if (list.capacity && (!list.users ||
            !user_range_ok((uint64_t)(uintptr_t)list.users,
                           (uint64_t)list.capacity * sizeof(struct leonos_user_info)))) {
            return -LEONOS_EFAULT;
        }
        list.actor_uid = task ? task->uid : 0;
        list.actor_role = task_effective_role(task);
        list.users = users;
        ret = osmlayer_auth_op(LEONOS_AUTH_OP_LIST_USERS, &list);
        user_list->count = list.count;
        if (ret < 0) {
            return ret;
        }
        copy_count = list.count < list.capacity ? list.count : list.capacity;
        for (uint32_t i = 0; i < copy_count; ++i) {
            ((struct leonos_user_info *)(uintptr_t)user_list->users)[i] = users[i];
        }
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_LOGIN) {
        struct leonos_auth_login login;
        int ret;
        if (!user_range_ok(user_arg, sizeof(login))) {
            return -LEONOS_EFAULT;
        }
        login = *(struct leonos_auth_login *)(uintptr_t)user_arg;
        if (kernel_string_len_cap(login.username, sizeof(login.username)) >= sizeof(login.username) ||
            kernel_string_len_cap(login.password, sizeof(login.password)) >= sizeof(login.password)) {
            kernel_clear_secret(login.password, sizeof(login.password));
            return -LEONOS_EFAULT;
        }
        ret = osmlayer_auth_op(LEONOS_AUTH_OP_LOGIN, &login);
        kernel_clear_secret(login.password, sizeof(login.password));
        if (ret < 0) {
            return ret;
        }
        auth_apply_session_login(task, &login.user);
        *(struct leonos_auth_login *)(uintptr_t)user_arg = login;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_ELEVATE_ADMIN) {
        struct leonos_auth_login login;
        int ret;
        if (!user_range_ok(user_arg, sizeof(login))) {
            return -LEONOS_EFAULT;
        }
        if (!task || !task->uid || !task->session_id) {
            return -LEONOS_EACCES;
        }
        login = *(struct leonos_auth_login *)(uintptr_t)user_arg;
        if (kernel_string_len_cap(login.username, sizeof(login.username)) >= sizeof(login.username) ||
            kernel_string_len_cap(login.password, sizeof(login.password)) >= sizeof(login.password)) {
            kernel_clear_secret(login.password, sizeof(login.password));
            return -LEONOS_EFAULT;
        }
        ret = osmlayer_auth_op(LEONOS_AUTH_OP_LOGIN, &login);
        kernel_clear_secret(login.password, sizeof(login.password));
        if (ret < 0) {
            return ret;
        }
        if (login.user.role != LEONOS_AUTH_ROLE_ADMIN) {
            return -LEONOS_EACCES;
        }
        task->flags |= TASK_FLAG_ELEVATED_ADMIN;
        *(struct leonos_auth_login *)(uintptr_t)user_arg = login;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_DELEGATE_ELEVATION) {
        struct leonos_auth_delegate_elevation delegation;
        struct task *child;
        if (!user_range_ok(user_arg, sizeof(delegation))) {
            return -LEONOS_EFAULT;
        }
        if (!task || task_effective_role(task) != LEONOS_AUTH_ROLE_ADMIN) {
            return -LEONOS_EACCES;
        }
        delegation = *(struct leonos_auth_delegate_elevation *)(uintptr_t)user_arg;
        child = sched_find(delegation.child_pid);
        if (!child || child->kind != TASK_KIND_USER ||
            child->state == TASK_EXITED || child->parent_pid != task->pid ||
            child->uid != task->uid || child->session_id != task->session_id) {
            return -LEONOS_EACCES;
        }
        child->flags |= TASK_FLAG_ELEVATED_ADMIN;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_LOGOUT) {
        uint32_t uid = task ? task->uid : 0;
        uint32_t session_id = task ? task->session_id : 0;
        if (uid && session_id) {
            auth_kill_session_tasks_for_logout(uid, session_id,
                                               task ? task->pid : 0);
            sched_clear_session_identity(session_id);
        }
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_CREATE_USER) {
        struct leonos_auth_create create;
        int ret;
        if (!user_range_ok(user_arg, sizeof(create))) {
            return -LEONOS_EFAULT;
        }
        create = *(struct leonos_auth_create *)(uintptr_t)user_arg;
        if (kernel_string_len_cap(create.username, sizeof(create.username)) >= sizeof(create.username) ||
            kernel_string_len_cap(create.password, sizeof(create.password)) >= sizeof(create.password)) {
            kernel_clear_secret(create.password, sizeof(create.password));
            return -LEONOS_EFAULT;
        }
        create.actor_uid = task ? task->uid : 0;
        create.actor_role = task_effective_role(task);
        ret = osmlayer_auth_op(LEONOS_AUTH_OP_CREATE_USER, &create);
        kernel_clear_secret(create.password, sizeof(create.password));
        if (ret < 0) {
            return ret;
        }
        *(struct leonos_auth_create *)(uintptr_t)user_arg = create;
        return 0;
    }
    if (request == LEONOS_AUTH_IOCTL_UPDATE_USER) {
        struct leonos_auth_update update;
        if (!user_range_ok(user_arg, sizeof(update))) {
            return -LEONOS_EFAULT;
        }
        update = *(struct leonos_auth_update *)(uintptr_t)user_arg;
        update.actor_uid = task ? task->uid : 0;
        update.actor_role = task_effective_role(task);
        return osmlayer_auth_op(LEONOS_AUTH_OP_UPDATE_USER, &update);
    }
    if (request == LEONOS_AUTH_IOCTL_CHANGE_PASSWORD) {
        struct leonos_auth_password password;
        if (!user_range_ok(user_arg, sizeof(password))) {
            return -LEONOS_EFAULT;
        }
        password = *(struct leonos_auth_password *)(uintptr_t)user_arg;
        if (kernel_string_len_cap(password.old_password, sizeof(password.old_password)) >= sizeof(password.old_password) ||
            kernel_string_len_cap(password.new_password, sizeof(password.new_password)) >= sizeof(password.new_password)) {
            kernel_clear_secret(password.old_password, sizeof(password.old_password));
            kernel_clear_secret(password.new_password, sizeof(password.new_password));
            return -LEONOS_EFAULT;
        }
        password.actor_uid = task ? task->uid : 0;
        password.actor_role = task_effective_role(task);
        {
            int ret = osmlayer_auth_op(LEONOS_AUTH_OP_CHANGE_PASSWORD, &password);
            kernel_clear_secret(password.old_password, sizeof(password.old_password));
            kernel_clear_secret(password.new_password, sizeof(password.new_password));
            return ret;
        }
    }
    return -LEONOS_ENOSYS;
}

/**
 * @brief Coordinates the startup release file operation.
 * @param data Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 */
static void startup_release_file(const void *data, size_t len)
{
    uint32_t pages = (uint32_t)((len + 4095U) / 4096U);
    if (data && pages) {
        mm_free_pages((uint64_t)(uintptr_t)data, pages);
    }
}

/**
 * @brief Coordinates the startup command is well formed operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup db is well formed operation.
 * @param db Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup denial db is well formed operation.
 * @param db Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup db load operation.
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
 * @brief Coordinates the startup db save operation.
 * @return Result, status, or value defined by this API.
 */
static int startup_db_save(void)
{
    (void)storage_mkdir("0:/system");
    (void)storage_mkdir("0:/system/state");
    return storage_write_file(STARTUP_DB_PATH, &startup_db_scratch,
                              sizeof(startup_db_scratch));
}

/**
 * @brief Coordinates the startup denial db load operation.
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
 * @brief Coordinates the startup denial db save operation.
 * @return Result, status, or value defined by this API.
 */
static int startup_denial_db_save(void)
{
    (void)storage_mkdir("0:/system");
    (void)storage_mkdir("0:/system/state");
    return storage_write_file(STARTUP_DENIAL_DB_PATH, &startup_denial_db_scratch,
                              sizeof(startup_denial_db_scratch));
}

/**
 * @brief Coordinates the startup text eq operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup command equal operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup command validate operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @param task Task whose state or authority is inspected or updated.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup can manage uid operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param uid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int startup_can_manage_uid(const struct task *task, uint32_t uid)
{
    return task && task->uid && uid &&
           (task_effective_role(task) == LEONOS_AUTH_ROLE_ADMIN ||
            task->uid == uid);
}

/**
 * @brief Coordinates the startup db find operation.
 * @param uid Input or output value used by this operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup denial find operation.
 * @param uid Input or output value used by this operation.
 * @param requester_path LeonOS path consumed by this operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup remember denial operation.
 * @param uid Input or output value used by this operation.
 * @param requester_path LeonOS path consumed by this operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup request find operation.
 * @param id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup request reconcile operation.
 * @param slot Input or output value used by this operation.
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
 * @brief Coordinates the startup request alloc operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup add entry operation.
 * @param uid Input or output value used by this operation.
 * @param command Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup dialog spawn operation.
 * @param slot Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the startup handle ioctl operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param user_arg Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int startup_handle_ioctl(uint64_t request, uint64_t user_arg)
{
    struct task *task = sched_current_task();
    if (request == LEONOS_STARTUP_IOCTL_REQUEST) {
        struct leonos_startup_request input;
        struct startup_request_slot *slot;
        int ret;
        if (!task || !task->uid || !task->session_id || !task->path[0] ||
            !user_range_ok(user_arg, sizeof(input))) {
            return -LEONOS_EACCES;
        }
        input = *(struct leonos_startup_request *)(uintptr_t)user_arg;
        ret = startup_command_validate(&input.command, task);
        if (ret < 0) {
            return ret;
        }
        slot = startup_request_alloc();
        if (!slot) {
            return -LEONOS_E2BIG;
        }
        slot->requester_pid = task->pid;
        slot->session_id = task->session_id;
        auth_copy_current_user(&slot->user, task);
        copy_text(slot->requester_path, sizeof(slot->requester_path), task->path);
        slot->command = input.command;
        startup_db_load();
        if (startup_db_find(task->uid, &slot->command) >= 0) {
            slot->status = LEONOS_STARTUP_STATUS_EXISTS;
        } else {
            startup_denial_db_load();
            if (startup_denial_find(task->uid, slot->requester_path, &slot->command) >= 0) {
                slot->status = LEONOS_STARTUP_STATUS_DENIED_REMEMBERED;
            } else {
                slot->status = LEONOS_STARTUP_STATUS_PENDING;
                ret = startup_dialog_spawn(slot);
                if (ret < 0) {
                    input.request_id = slot->id;
                    input.status = slot->status;
                    *(struct leonos_startup_request *)(uintptr_t)user_arg = input;
                    return ret;
                }
            }
        }
        input.request_id = slot->id;
        input.status = slot->status;
        *(struct leonos_startup_request *)(uintptr_t)user_arg = input;
        return 0;
    }
    if (request == LEONOS_STARTUP_IOCTL_REQUEST_STATUS) {
        struct leonos_startup_request_status status;
        struct startup_request_slot *slot;
        if (!task || !user_range_ok(user_arg, sizeof(status))) {
            return -LEONOS_EFAULT;
        }
        status = *(struct leonos_startup_request_status *)(uintptr_t)user_arg;
        slot = startup_request_find(status.request_id);
        if (!slot || slot->requester_pid != task->pid) {
            return -LEONOS_EACCES;
        }
        startup_request_reconcile(slot);
        status.status = slot->status;
        *(struct leonos_startup_request_status *)(uintptr_t)user_arg = status;
        if (slot->status != LEONOS_STARTUP_STATUS_PENDING) {
            slot->used = 0;
        }
        return 0;
    }
    if (request == LEONOS_STARTUP_IOCTL_DIALOG_GET) {
        struct leonos_startup_dialog_request output;
        struct startup_request_slot *slot = 0;
        if (!task || !text_eq_cstr(task->path, SYSCONFDIALOG_APP_PATH) ||
            !user_range_ok(user_arg, sizeof(output))) {
            return -LEONOS_EACCES;
        }
        for (uint32_t i = 0; i < STARTUP_REQUEST_MAX; ++i) {
            startup_request_reconcile(&startup_requests[i]);
            if (startup_requests[i].used && startup_requests[i].dialog_pid == task->pid &&
                startup_requests[i].status == LEONOS_STARTUP_STATUS_PENDING) {
                slot = &startup_requests[i];
                break;
            }
        }
        if (!slot) {
            return -LEONOS_EINVAL;
        }
        output = (struct leonos_startup_dialog_request){0};
        output.request_id = slot->id;
        output.uid = slot->user.uid;
        copy_text(output.requester_path, sizeof(output.requester_path), slot->requester_path);
        output.command = slot->command;
        *(struct leonos_startup_dialog_request *)(uintptr_t)user_arg = output;
        return 0;
    }
    if (request == LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE) {
        struct leonos_startup_dialog_resolution resolution;
        struct startup_request_slot *slot;
        int ret;
        if (!task || !text_eq_cstr(task->path, SYSCONFDIALOG_APP_PATH) ||
            !user_range_ok(user_arg, sizeof(resolution))) {
            return -LEONOS_EACCES;
        }
        resolution = *(struct leonos_startup_dialog_resolution *)(uintptr_t)user_arg;
        slot = startup_request_find(resolution.request_id);
        if (!slot || slot->dialog_pid != task->pid ||
            slot->status != LEONOS_STARTUP_STATUS_PENDING) {
            return -LEONOS_EACCES;
        }
        if (resolution.decision == LEONOS_STARTUP_DECISION_ALLOW) {
            ret = startup_add_entry(slot->user.uid, &slot->command);
            slot->status = ret < 0 ? LEONOS_STARTUP_STATUS_FAILED :
                           (ret > 0 ? LEONOS_STARTUP_STATUS_EXISTS :
                                      LEONOS_STARTUP_STATUS_APPROVED);
        } else if (resolution.decision == LEONOS_STARTUP_DECISION_DENY ||
                   resolution.decision == LEONOS_STARTUP_DECISION_DENY_REMEMBERED) {
            if (resolution.decision == LEONOS_STARTUP_DECISION_DENY_REMEMBERED) {
                startup_denial_db_load();
                ret = startup_remember_denial(slot->user.uid, slot->requester_path, &slot->command);
                slot->status = ret < 0 ? LEONOS_STARTUP_STATUS_DENIED :
                                       LEONOS_STARTUP_STATUS_DENIED_REMEMBERED;
            } else {
                slot->status = LEONOS_STARTUP_STATUS_DENIED;
            }
        } else {
            return -LEONOS_EINVAL;
        }
        return 0;
    }
    if (request == LEONOS_STARTUP_IOCTL_LIST) {
        struct leonos_startup_list list;
        uint32_t out = 0;
        if (!task || !user_range_ok(user_arg, sizeof(list))) {
            return -LEONOS_EFAULT;
        }
        list = *(struct leonos_startup_list *)(uintptr_t)user_arg;
        if (!startup_can_manage_uid(task, list.uid) || list.capacity > LEONOS_STARTUP_MAX_ENTRIES ||
            (list.capacity && (!list.entries || !user_range_ok((uint64_t)(uintptr_t)list.entries,
             (uint64_t)list.capacity * sizeof(struct leonos_startup_entry))))) {
            return -LEONOS_EACCES;
        }
        startup_db_load();
        for (uint32_t i = 0; i < startup_db_scratch.count; ++i) {
            if (startup_db_scratch.entries[i].uid != list.uid) {
                continue;
            }
            if (list.entries && out < list.capacity) {
                list.entries[out] = startup_db_scratch.entries[i].entry;
            }
            ++out;
        }
        list.count = out;
        *(struct leonos_startup_list *)(uintptr_t)user_arg = list;
        return 0;
    }
    if (request == LEONOS_STARTUP_IOCTL_SET_ENABLED || request == LEONOS_STARTUP_IOCTL_REMOVE) {
        struct leonos_startup_update update;
        if (!task || !user_range_ok(user_arg, sizeof(update))) {
            return -LEONOS_EFAULT;
        }
        update = *(struct leonos_startup_update *)(uintptr_t)user_arg;
        if (!startup_can_manage_uid(task, update.uid)) {
            return -LEONOS_EACCES;
        }
        startup_db_load();
        for (uint32_t i = 0; i < startup_db_scratch.count; ++i) {
            if (startup_db_scratch.entries[i].uid != update.uid ||
                startup_db_scratch.entries[i].entry.id != update.entry_id) {
                continue;
            }
            if (request == LEONOS_STARTUP_IOCTL_SET_ENABLED) {
                startup_db_scratch.entries[i].entry.enabled = update.enabled ? 1U : 0U;
            } else {
                for (uint32_t j = i + 1; j < startup_db_scratch.count; ++j) {
                    startup_db_scratch.entries[j - 1] = startup_db_scratch.entries[j];
                }
                --startup_db_scratch.count;
            }
            return startup_db_save();
        }
        return -LEONOS_EINVAL;
    }
    if (request == LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT) {
        const char *argv[LEONOS_STARTUP_MAX_ARGS + 2];
        int launched = 0;
        if (!task || !task->uid || !task->session_id ||
            !(task->flags & TASK_FLAG_WINDOW_SERVER) ||
            !text_eq_cstr(task->path, "0:/system/apps/desktop/desktop.elf")) {
            return -LEONOS_EACCES;
        }
        startup_db_load();
        for (uint32_t i = 0; i < startup_db_scratch.count; ++i) {
            struct leonos_startup_entry *entry = &startup_db_scratch.entries[i].entry;
            if (startup_db_scratch.entries[i].uid != task->uid || !entry->enabled ||
                authz_check_path(task, LEONOS_AUTHZ_EXEC, entry->command.path, 0, 0) < 0) {
                continue;
            }
            argv[0] = entry->command.path;
            for (uint32_t j = 0; j < entry->command.argc; ++j) {
                argv[j + 1] = entry->command.args[j];
            }
            argv[entry->command.argc + 1] = 0;
            if (userland_spawn_path_argv(entry->command.path, argv, 0, 0) > 0) {
                ++launched;
            }
        }
        return launched;
    }
    return -LEONOS_ENOSYS;
}

/**
 * @brief Copies user vector.
 * @param user_ptr Input or output value used by this operation.
 * @param max_count Length, size, or element count associated with the operation.
 * @param out_ptrs Caller-provided storage that receives output from this operation.
 * @param data Input or output value used by this operation.
 * @param data_cap Capacity, in elements or bytes, of the related output buffer.
 * @param out_count Caller-provided storage that receives output from this operation.
 * @param data_len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Copies exec params from user.
 * @param task Task whose state or authority is inspected or updated.
 * @param path_ptr LeonOS path consumed by this operation.
 * @param argv_ptr Input or output value used by this operation.
 * @param envp_ptr Input or output value used by this operation.
 * @param path_out LeonOS path consumed by this operation.
 * @param path_cap Capacity, in elements or bytes, of the related output buffer.
 * @param params Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the stat for fd operation.
 * @param fd Open file descriptor used by this operation.
 * @param task Task whose state or authority is inspected or updated.
 * @param st Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int stat_for_fd(int fd, struct task *task, struct leonos_stat *st)
{
    struct task_file *file;
    if (!st) {
        return -LEONOS_EFAULT;
    }
    file = task_file_for_fd(task, fd);
    if (file) {
        st->type = (file->flags & TASK_FILE_FLAG_PIPE) ? LEONOS_FS_TYPE_DEVICE
                                                         : file->node.type;
        st->reserved = 0;
        st->size = file->node.size;
        return 0;
    }
    if (fd >= 0 && fd <= 3) {
        st->type = LEONOS_FS_TYPE_DEVICE;
        st->reserved = 0;
        st->size = 0;
        return 0;
    }
    return -LEONOS_EBADF;
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
 * @brief Coordinates the syscall dispatch operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @return Result, status, or value defined by this API.
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
    case LINUX_SYS_CLOSE:
    case LINUX_SYS_STAT:
    case LINUX_SYS_FSTAT:
    case LINUX_SYS_LSEEK:
    case LINUX_SYS_FTRUNCATE:
    case LINUX_SYS_IOCTL:
    case LINUX_SYS_SCHED_YIELD:
    case LINUX_SYS_NANOSLEEP:
    case LINUX_SYS_EXECVE:
    case LINUX_SYS_EXIT:
    case LINUX_SYS_WAIT4:
    case LINUX_SYS_GETCWD:
    case LINUX_SYS_CHDIR:
    case LINUX_SYS_RENAME:
    case LINUX_SYS_MKDIR:
    case LINUX_SYS_RMDIR:
    case LINUX_SYS_UNLINK:
    case LINUX_SYS_PIPE:
    case LINUX_SYS_DUP:
    case LINUX_SYS_DUP2:
    case LINUX_SYS_FORK:
    case LINUX_SYS_VFORK:
    case LINUX_SYS_FCNTL:
        return syscall_dispatch_regs(frame->number, frame->args[0], frame->args[1],
                                     frame->args[2], frame->args[3], frame->args[4],
                                     frame->args[5]);
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
 * @brief Coordinates the syscall dispatch regs operation.
 * @param number Input or output value used by this operation.
 * @param a0 Input or output value used by this operation.
 * @param a1 Input or output value used by this operation.
 * @param a2 Input or output value used by this operation.
 * @param a3 Input or output value used by this operation.
 * @param a4 Input or output value used by this operation.
 * @param a5 Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int64_t syscall_dispatch_regs_legacy(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_AUTH_IOCTL_STATUS ||
          a1 == LEONOS_AUTH_IOCTL_CURRENT ||
          a1 == LEONOS_AUTH_IOCTL_LIST_USERS ||
          a1 == LEONOS_AUTH_IOCTL_LOGIN ||
          a1 == LEONOS_AUTH_IOCTL_ELEVATE_ADMIN ||
          a1 == LEONOS_AUTH_IOCTL_DELEGATE_ELEVATION ||
          a1 == LEONOS_AUTH_IOCTL_LOGOUT ||
         a1 == LEONOS_AUTH_IOCTL_CREATE_USER ||
         a1 == LEONOS_AUTH_IOCTL_UPDATE_USER ||
         a1 == LEONOS_AUTH_IOCTL_CHANGE_PASSWORD)) {
        return auth_handle_ioctl(a1, a2);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_STARTUP_IOCTL_REQUEST ||
         a1 == LEONOS_STARTUP_IOCTL_REQUEST_STATUS ||
         a1 == LEONOS_STARTUP_IOCTL_DIALOG_GET ||
         a1 == LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE ||
         a1 == LEONOS_STARTUP_IOCTL_LIST ||
         a1 == LEONOS_STARTUP_IOCTL_SET_ENABLED ||
         a1 == LEONOS_STARTUP_IOCTL_REMOVE ||
         a1 == LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT)) {
        return startup_handle_ioctl(a1, a2);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_FS_IOCTL_ACL_GET ||
         a1 == LEONOS_FS_IOCTL_ACL_SET ||
         a1 == LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP ||
         a1 == LEONOS_FS_IOCTL_ACL_REPAIR)) {
        return fs_acl_handle_ioctl(a1, a2);
    }

    if (number == LINUX_SYS_WRITE) {
        struct task *task = sched_current_task();
        uint32_t request_len;
        int pty_stream;
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        {
            struct task_file *pipe_file = task_file_for_fd(task, (int)a0);
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_PIPE)) {
                uint32_t request = a2 > TASK_PIPE_CAP ? TASK_PIPE_CAP : (uint32_t)a2;
                return task_pipe_write(pipe_file, (const void *)(uintptr_t)a1, request);
            }
        }
        pty_stream = task_pty_stream_for_fd(task, (int)a0);
        if (pty_stream == 1 || pty_stream == 2) {
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES
                              : (uint32_t)a2;
            return pty_write_output(task->pty_id, (const char *)(uintptr_t)a1, request_len);
        }
        /* A redirected stdio descriptor must be handled by the regular file
         * path below.  Only an unbound implicit PTY stream falls back to the
         * kernel console; otherwise a pipe on fd 1/2 would be silently
         * bypassed and its consumer would receive EOF/zero bytes. */
        if ((a0 == 1 || a0 == 2) && !task_file_for_fd(task, (int)a0)) {
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES
                              : (uint32_t)a2;
            console_write_len((const char *)(uintptr_t)a1, (size_t)request_len);
            return (int64_t)request_len;
        }
        struct task_file *file = task_file_for_fd(task, (int)a0);
        uint32_t wrote = 0;
        int ret;
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (!file_can_write(file)) {
            return -LEONOS_EBADF;
        }
        if (file->node.type == LEONOS_FS_TYPE_DIR) {
            return -LEONOS_EISDIR;
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE || !file->path[0]) {
            return -LEONOS_EBADF;
        }
        request_len = a2 > LEONOS_FS_FILE_WRITE_SLICE_BYTES
                          ? LEONOS_FS_FILE_WRITE_SLICE_BYTES
                          : (uint32_t)a2;
        ret = authz_check_path(task, LEONOS_AUTHZ_WRITE, file->path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        if (file->flags & LEONOS_O_APPEND) {
            file->offset = file->node.size;
        }
        file->read_cursor.valid = 0;
        ret = storage_write_node(file->path, file->offset,
                                 (const void *)(uintptr_t)a1, request_len, &wrote);
        if (ret < 0) {
            return ret;
        }
        file->offset += wrote;
        if (wrote && file->node.first_cluster < 2) {
            struct storage_node updated;
            if (storage_lookup_path(file->path, &updated) == 0) {
                file->node = updated;
            }
        }
        file->node.size = file->offset > file->node.size ? file->offset : file->node.size;
        return (int64_t)wrote;
    }

    if (number == LINUX_SYS_READ) {
        struct task *task = sched_current_task();
        struct task_file *file;
        uint32_t got = 0;
        uint32_t request_len;
        int pty_stream;
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        {
            struct task_file *pipe_file = task_file_for_fd(task, (int)a0);
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_PIPE)) {
                uint32_t request = a2 > TASK_PIPE_CAP ? TASK_PIPE_CAP : (uint32_t)a2;
                return task_pipe_read(pipe_file, (void *)(uintptr_t)a1, request);
            }
        }
        pty_stream = task_pty_stream_for_fd(task, (int)a0);
        if (pty_stream == 0) {
            return pty_read_input(task->pty_id, (char *)(uintptr_t)a1, (uint32_t)a2);
        }
        file = task_file_for_fd(task, (int)a0);
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->path[0]) {
            int ret = authz_check_path(task, LEONOS_AUTHZ_READ, file->path, 0, 0);
            if (ret < 0) {
                return ret;
            }
        }
        if (file->node.type == LEONOS_FS_TYPE_DIR) {
            struct leonos_dir_entry entry;
            int step = storage_readdir_node(&file->node, &file->offset, &entry);
            if (step == 0 && file->node.drive == 0 &&
                (file->node.flags & STORAGE_NODE_FLAG_ROOT) && file->aux == 0) {
                entry.type = LEONOS_FS_TYPE_DIR;
                copy_text(entry.name, sizeof(entry.name), "dev");
                file->aux = 1;
                step = 1;
            }
            if (step < 0) {
                return step;
            }
            if (step == 0) {
                return 0;
            }
            if (a2 < sizeof(entry)) {
                return -LEONOS_EINVAL;
            }
            *(struct leonos_dir_entry *)(uintptr_t)a1 = entry;
            return (int64_t)sizeof(entry);
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE) {
            return -LEONOS_EBADF;
        }
        if (!file_can_read(file)) {
            return -LEONOS_EBADF;
        }
        request_len = a2 > LEONOS_FS_READ_SLICE_BYTES
                          ? LEONOS_FS_READ_SLICE_BYTES
                          : (uint32_t)a2;
        {
            int ret = storage_read_node_cursor(&file->node, file->offset,
                                               (void *)(uintptr_t)a1, request_len, &got,
                                               &file->read_cursor);
            if (ret < 0) {
                return storage_errno(ret);
            }
        }
        file->offset += got;
        return (int64_t)got;
    }

    if (number == LINUX_SYS_EXIT) {
        uint32_t pid = sched_current_pid();
        net_close_owner_sockets(pid);
        clear_task_files(sched_current_task());
        gui_ipc_destroy_owner(pid);
        pty_process_exit(pid);
        userland_process_exit(a0);
        return 0;
    }

    if (number == LINUX_SYS_EXECVE) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        struct exec_params_kernel params;
        int ret = copy_exec_params_from_user(task, a0, a1, a2, path, sizeof(path), &params);
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_EXEC, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        return userland_exec_current_path(path, params.argc, params.argv,
                                          params.envc, params.envp,
                                          params.data, params.data_len);
    }

    if (number == LINUX_SYS_FORK || number == LINUX_SYS_VFORK) {
        struct task *task = sched_current_task();
        return sched_fork_current(task ? &task->frame : NULL);
    }

    if (number == LINUX_SYS_OPEN) {
        struct task *task = sched_current_task();
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        uint32_t flags = (uint32_t)a1;
        uint8_t created = 0;
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task,
                               ((flags & LEONOS_O_ACCMODE) != LEONOS_O_RDONLY ||
                                (flags & (LEONOS_O_CREAT | LEONOS_O_TRUNC)))
                                   ? LEONOS_AUTHZ_WRITE
                                   : LEONOS_AUTHZ_READ,
                               path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_lookup_path(path, &node);
        if (ret < 0) {
            if (ret == -2 && (flags & LEONOS_O_CREAT)) {
                ret = storage_write_file(path, "", 0);
                if (ret < 0) {
                    return ret;
                }
                created = 1;
                ret = storage_lookup_path(path, &node);
            }
            if (ret < 0) {
                return ret == -2 ? -LEONOS_ENOENT : ret;
            }
        }
        if ((node.flags & STORAGE_NODE_FLAG_DEV_FB0) != 0) {
            return 3;
        }
        if (node.type == LEONOS_FS_TYPE_DIR && ((flags & LEONOS_O_ACCMODE) != LEONOS_O_RDONLY)) {
            return -LEONOS_EISDIR;
        }
        if (node.type == LEONOS_FS_TYPE_FILE && (flags & LEONOS_O_TRUNC) && file_can_write(&(struct task_file){.flags = flags})) {
            ret = storage_write_file(path, "", 0);
            if (ret < 0) {
                return ret;
            }
            ret = storage_lookup_path(path, &node);
            if (ret < 0) {
                return ret == -2 ? -LEONOS_ENOENT : ret;
            }
        }
        int fd = alloc_task_fd(task, &node, flags, path);
        if (fd >= 0 && created) {
            fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_CREATE, task, path, 0);
        }
        if (fd >= 0 && (flags & LEONOS_O_APPEND)) {
            struct task_file *file = task_file_for_fd(task, fd);
            if (file && file->node.type == LEONOS_FS_TYPE_FILE) {
                file->offset = file->node.size;
            }
        }
        return fd;
    }

    if (number == LINUX_SYS_PIPE) {
        return syscall_ipc_pipe(a0);
    }

    if (number == LINUX_SYS_CLOSE) {
        struct task *task = sched_current_task();
        if (a0 <= 3) {
            struct task_file *stdio_file = task_file_for_fd(task, (int)a0);
            if (stdio_file) clear_task_file(stdio_file);
            return 0;
        }
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (file) {
            clear_task_file(file);
            return 0;
        }
        {
            struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, (int)a0);
            if (pty_fd) {
                *pty_fd = (struct task_pty_fd){0};
                return 0;
            }
        }
        return -LEONOS_EBADF;
    }

    if (number == LINUX_SYS_FTRUNCATE) {
        struct task *task = sched_current_task();
        struct task_file *file;
        int ret;
        if ((int64_t)a1 < 0) {
            return -LEONOS_EINVAL;
        }
        file = task_file_for_fd(task, (int)a0);
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE || !file_can_write(file) || !file->path[0]) {
            return -LEONOS_EBADF;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_WRITE, file->path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_truncate_file(file->path, a1);
        if (ret < 0) {
            return ret;
        }
        file->read_cursor.valid = 0;
        ret = storage_lookup_path(file->path, &file->node);
        if (ret < 0) {
            return ret;
        }
        if (file->offset > file->node.size) {
            file->offset = file->node.size;
        }
        return 0;
    }

    if (number == LINUX_SYS_DUP || number == LINUX_SYS_DUP2 || number == LINUX_SYS_FCNTL) {
        struct task *task = sched_current_task();
        if (number == LINUX_SYS_DUP) {
            struct task_file *source = task_file_for_fd(task, (int)a0);
            if (source) {
                return task_duplicate_file_fd(task, (int)a0, 4, 0);
            }
            return task_pty_duplicate_fd(task, (int)a0, 0, 0);
        }
        if (number == LINUX_SYS_DUP2) {
            if (task_file_for_fd(task, (int)a0)) {
                return task_dup2_fd(task, (int)a0, (int)a1);
            }
            return task_pty_dup2_fd(task, (int)a0, (int)a1);
        }
        if (a1 == LEONOS_F_DUPFD || a1 == LEONOS_F_DUPFD_CLOEXEC) {
            if (task_file_for_fd(task, (int)a0)) {
                return task_duplicate_file_fd(task, (int)a0, (int)a2,
                                              a1 == LEONOS_F_DUPFD_CLOEXEC ?
                                                  LEONOS_FD_CLOEXEC : 0);
            }
            return task_pty_duplicate_fd(task, (int)a0, (int)a2,
                                         a1 == LEONOS_F_DUPFD_CLOEXEC ? LEONOS_FD_CLOEXEC : 0);
        }
        {
            struct task_file *file_fd = task_file_for_fd(task, (int)a0);
            struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, (int)a0);
            if (!file_fd && !pty_fd && task_pty_stream_for_fd(task, (int)a0) < 0) {
                return -LEONOS_EBADF;
            }
            if (a1 == LEONOS_F_GETFD) {
                return file_fd ? (int64_t)file_fd->fd_flags :
                       (pty_fd ? (int64_t)pty_fd->flags : 0);
            }
            if (a1 == LEONOS_F_SETFD) {
                if (file_fd) {
                    file_fd->fd_flags = (uint32_t)a2 & LEONOS_FD_CLOEXEC;
                } else if (pty_fd) {
                    pty_fd->flags = (uint32_t)a2 & LEONOS_FD_CLOEXEC;
                }
                return 0;
            }
            if (a1 == LEONOS_F_GETFL || a1 == LEONOS_F_SETFL) {
                return 0;
            }
        }
        return -LEONOS_ENOSYS;
    }

    if (number == LINUX_SYS_STAT) {
        struct task *task = sched_current_task();
        struct leonos_stat st;
        char path[LEONOS_FS_PATH_LEN];
        int ret;
        if (!user_range_ok(a1, sizeof(st))) {
            return -LEONOS_EFAULT;
        }
        ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_READ, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_stat_path(path, &st);
        if (ret < 0) {
            return ret == -2 ? -LEONOS_ENOENT : ret;
        }
        *(struct leonos_stat *)(uintptr_t)a1 = st;
        return 0;
    }

    if (number == LINUX_SYS_FSTAT) {
        struct task *task = sched_current_task();
        struct leonos_stat st;
        int ret;
        if (!user_range_ok(a1, sizeof(st))) {
            return -LEONOS_EFAULT;
        }
        ret = stat_for_fd((int)a0, task, &st);
        if (ret < 0) {
            return ret;
        }
        *(struct leonos_stat *)(uintptr_t)a1 = st;
        return 0;
    }

    if (number == LINUX_SYS_LSEEK) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        int64_t offset = (int64_t)a1;
        int64_t base = 0;
        int64_t size = 0;
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->node.type == LEONOS_FS_TYPE_FILE) {
            size = (int64_t)file->node.size;
            base = 0;
            if ((int)a2 == LEONOS_SEEK_CUR) {
                base = (int64_t)file->offset;
            } else if ((int)a2 == LEONOS_SEEK_END) {
                base = size;
            } else if ((int)a2 != LEONOS_SEEK_SET) {
                return -LEONOS_EINVAL;
            }
        } else if (file->node.type == LEONOS_FS_TYPE_DIR) {
            if ((int)a2 == LEONOS_SEEK_CUR) {
                base = (int64_t)file->offset * (int64_t)sizeof(struct leonos_dir_entry);
            } else if ((int)a2 == LEONOS_SEEK_SET) {
                base = 0;
            } else {
                return -LEONOS_EINVAL;
            }
        } else {
            return -LEONOS_EINVAL;
        }
        if (base + offset < 0) {
            return -LEONOS_EINVAL;
        }
        if (file->node.type == LEONOS_FS_TYPE_DIR) {
            file->offset = (uint64_t)((base + offset) / (int64_t)sizeof(struct leonos_dir_entry));
            file->aux = 0;
            return (int64_t)(file->offset * sizeof(struct leonos_dir_entry));
        }
        file->offset = (uint64_t)(base + offset);
        file->read_cursor.valid = 0;
        return (int64_t)file->offset;
    }

    if (number == LINUX_SYS_GETCWD) {
        struct task *task = sched_current_task();
        const char *cwd = (task && task->cwd[0]) ? task->cwd : "0:/";
        size_t len = 0;
        while (cwd[len]) {
            ++len;
        }
        if (!user_range_ok(a0, a1) || a1 == 0 || len + 1 > a1) {
            return -LEONOS_EFAULT;
        }
        for (size_t i = 0; i <= len; ++i) {
            ((char *)(uintptr_t)a0)[i] = cwd[i];
        }
        return (int64_t)a0;
    }

    if (number == LINUX_SYS_CHDIR) {
        struct task *task = sched_current_task();
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_READ, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_lookup_path(path, &node);
        if (ret < 0) {
            return ret == -2 ? -LEONOS_ENOENT : ret;
        }
        if (node.type != LEONOS_FS_TYPE_DIR) {
            return -LEONOS_ENOTDIR;
        }
        if (task) {
            copy_text(task->cwd, sizeof(task->cwd), path);
        }
        return 0;
    }

    if (number == LINUX_SYS_MKDIR) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_WRITE, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_mkdir(path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_CREATE, task, path, 0);
        return 0;
    }

    if (number == LINUX_SYS_UNLINK) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_DELETE, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_unlink(path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_DELETE, task, path, 0);
        return 0;
    }

    if (number == LINUX_SYS_RMDIR) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_DELETE, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_rmdir(path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_DELETE, task, path, 0);
        return 0;
    }

    if (number == LINUX_SYS_RENAME) {
        struct task *task = sched_current_task();
        char old_path[LEONOS_FS_PATH_LEN];
        char new_path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, old_path, sizeof(old_path));
        if (ret < 0) {
            return ret;
        }
        ret = resolve_user_path(task, a1, new_path, sizeof(new_path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_DELETE, old_path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_WRITE, new_path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = storage_rename(old_path, new_path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_RENAME, task, old_path, new_path);
        return 0;
    }

    if (number == LINUX_SYS_GETPID || number == LINUX_SYS_GETPPID ||
        number == LINUX_SYS_SETPGID || number == LINUX_SYS_GETPGRP ||
        number == LINUX_SYS_SETSID || number == LINUX_SYS_GETPGID ||
        number == LINUX_SYS_KILL || number == LINUX_SYS_NICE ||
        number == LINUX_SYS_GETPRIORITY || number == LINUX_SYS_SETPRIORITY) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_GETRLIMIT || number == LINUX_SYS_SETRLIMIT) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }

    if (number == LINUX_SYS_SCHED_YIELD) {
        userland_yield_if_runnable();
        return 0;
    }

    if (number == LINUX_SYS_NANOSLEEP) {
        uint64_t ms = a0;
        if (user_range_ok(a0, 16)) {
            const uint64_t *ts = (const uint64_t *)(uintptr_t)a0;
            ms = ts[0] * 1000ULL + ts[1] / 1000000ULL;
        }
        uint64_t delta = (ms * NTCLKS_TICK_HZ + 999ULL) / 1000ULL;
        if (delta == 0) {
            delta = 1;
        }
        sched_sleep_current_until(time_ticks() + delta);
        userland_yield_if_runnable();
        return 0;
    }

    if (number == LINUX_SYS_MMAP) {
        return syscall_mm_mmap(a0, a1, a2, a3, a4, a5);
    }

    if (number == LINUX_SYS_MUNMAP) {
        return syscall_mm_munmap(a0, a1);
    }
    if (number == LINUX_SYS_MPROTECT) {
        return syscall_mm_mprotect(a0, a1, a2);
    }

    if (number == LINUX_SYS_WAIT4) {
        int32_t requested_pid = (int32_t)a0;
        int status = 0;
        int64_t pid;
        if (a1 && !user_range_ok(a1, sizeof(int))) {
            return -LEONOS_EFAULT;
        }
        pid = sched_wait_reap(sched_current_pid(), requested_pid, (uint32_t)a2,
                              a1 ? &status : NULL);
        if (pid == -LEONOS_EAGAIN) {
            return (a2 & 1U) != 0 ? 0 : -LEONOS_EAGAIN;
        }
        if (pid <= 0) {
            return -LEONOS_ECHILD;
        }
        if (a1) {
            *(int *)(uintptr_t)a1 = status;
        }
        return pid;
    }

    if (number == LINUX_SYS_IOCTL && inputm_handles_ioctl(a1)) {
        return inputm_handle_ioctl(a1, a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_KERNEL_DEBUG_IOCTL_CONTROL) {
        if (!user_range_ok(a2, sizeof(struct leonos_kernel_debug_control))) {
            return -LEONOS_EFAULT;
        }
        return kernel_debug_control((struct leonos_kernel_debug_control *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_VERSION) {
        return 1;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_EVENT) {
        struct input_event event;
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(event))) {
            return -LEONOS_EFAULT;
        }
        if (!input_pop(&event)) {
            return 0;
        }
        struct input_event *dst = (struct input_event *)(uintptr_t)a2;
        *dst = event;
        return 1;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_UPTIME_MS) {
        return (int64_t)time_uptime_ms();
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_INFO) {
        if (!user_range_ok(a2, sizeof(struct framebuffer_info))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer *fb = framebuffer_get();
        struct framebuffer_info *info = (struct framebuffer_info *)(uintptr_t)a2;
        *info = (struct framebuffer_info){
            .width = fb->width,
            .height = fb->height,
            .pitch = fb->pitch,
            .bpp = fb->bpp,
        };
        return fb->available ? 0 : -LEONOS_EINVAL;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_CAPS) {
        if (!user_range_ok(a2, sizeof(struct framebuffer_capabilities))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer *fb = framebuffer_get();
        struct framebuffer_capabilities *caps =
            (struct framebuffer_capabilities *)(uintptr_t)a2;
        *caps = (struct framebuffer_capabilities){
            .bytes_per_pixel = fb->bytes_per_pixel,
            .reserved = 0,
            .capabilities = fb->capabilities,
            .max_width = fb->max_width,
            .max_height = fb->max_height,
            .max_bytes = fb->max_bytes,
            .backend = fb->backend,
        };
        return fb->available ? 0 : -LEONOS_EINVAL;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_SET_MODE) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct framebuffer_mode_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_mode_cmd *cmd =
            (const struct framebuffer_mode_cmd *)(uintptr_t)a2;
        return framebuffer_set_mode(cmd->width, cmd->height) == 0 ? 0 : -LEONOS_EINVAL;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_FILL) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        framebuffer_clear((uint32_t)a2);
        framebuffer_present();
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_RECT) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct framebuffer_rect_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_rect_cmd *cmd = (const struct framebuffer_rect_cmd *)(uintptr_t)a2;
        framebuffer_rect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        framebuffer_present_region(cmd->x, cmd->y, cmd->width, cmd->height);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_TEXT) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct framebuffer_text_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_text_cmd *cmd = (const struct framebuffer_text_cmd *)(uintptr_t)a2;
        size_t len = user_strlen(cmd->text, 160);
        if (len == 160 || !user_range_ok((uint64_t)(uintptr_t)cmd->text, len + 1)) {
            return -LEONOS_EFAULT;
        }
        framebuffer_text(cmd->x, cmd->y, cmd->text, cmd->fg, cmd->bg);
        framebuffer_present();
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_PIXEL) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        uint32_t x = (uint32_t)(a2 & 0xffffffffULL);
        uint32_t y = (uint32_t)(a2 >> 32);
        return (int64_t)framebuffer_get_pixel_public(x, y);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_BLIT) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct framebuffer_blit_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_blit_cmd *cmd = (const struct framebuffer_blit_cmd *)(uintptr_t)a2;
        if (!cmd->pixels || cmd->stride < cmd->width) {
            return -LEONOS_EINVAL;
        }
        uint64_t bytes = (uint64_t)cmd->stride * cmd->height * sizeof(uint32_t);
        if (!user_range_ok((uint64_t)(uintptr_t)cmd->pixels, bytes)) {
            return -LEONOS_EFAULT;
        }
        framebuffer_blit(cmd->x, cmd->y, cmd->width, cmd->height, cmd->stride, cmd->pixels);
        framebuffer_present_region(cmd->x, cmd->y, cmd->width, cmd->height);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_CREATE_WINDOW) {
        if (!user_range_ok(a2, sizeof(struct gui_create_window_user))) {
            return -LEONOS_EFAULT;
        }
        const struct gui_create_window_user *cmd = (const struct gui_create_window_user *)(uintptr_t)a2;
        if (!cmd->title || !cmd->text) {
            return -LEONOS_EFAULT;
        }
        if (!gui_ipc_validate_surface_geometry(cmd->width, cmd->height, cmd->width, NULL)) {
            return -LEONOS_EINVAL;
        }
        if (user_strlen(cmd->title, GUI_IPC_WINDOW_TITLE_MAX - 1) == GUI_IPC_WINDOW_TITLE_MAX - 1 ||
            user_strlen(cmd->text, GUI_IPC_WINDOW_TEXT_MAX - 1) == GUI_IPC_WINDOW_TEXT_MAX - 1) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_create_window(sched_current_pid(),
                                     cmd->width,
                                     cmd->height,
                                     cmd->title,
                                     cmd->text,
                                     cmd->flags);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_POLL_WINDOW) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_window))) {
            return -LEONOS_EFAULT;
        }
        struct gui_ipc_window *dst = (struct gui_ipc_window *)(uintptr_t)a2;
        return gui_ipc_pop_window(sched_current_pid(), dst) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_PRESENT_WINDOW) {
        if (!user_range_ok(a2, sizeof(struct gui_present_window_user))) {
            return -LEONOS_EFAULT;
        }
        const struct gui_present_window_user *cmd = (const struct gui_present_window_user *)(uintptr_t)a2;
        uint64_t bytes;
        if (!cmd->pixels ||
            !gui_ipc_validate_surface_geometry(cmd->width, cmd->height,
                                               cmd->stride, &bytes)) {
            return -LEONOS_EINVAL;
        }
        if (!user_range_ok((uint64_t)(uintptr_t)cmd->pixels, bytes)) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_present_window(sched_current_pid(),
                                      cmd->window_id,
                                      cmd->width,
                                      cmd->height,
                                      cmd->stride,
                                      cmd->pixels) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FETCH_WINDOW) {
        struct gui_fetch_window_user *cmd;
        uint64_t bytes;
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_fetch_window_user))) {
            return -LEONOS_EFAULT;
        }
        cmd = (struct gui_fetch_window_user *)(uintptr_t)a2;
        if (!cmd->pixels ||
            !gui_ipc_validate_surface_geometry(cmd->capacity_width,
                                               cmd->capacity_height,
                                               cmd->stride, &bytes)) {
            return -LEONOS_EINVAL;
        }
        if (!user_range_ok((uint64_t)(uintptr_t)cmd->pixels, bytes)) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_fetch_window(sched_current_pid(),
                                    cmd->window_id,
                                    cmd->capacity_width,
                                    cmd->capacity_height,
                                    cmd->stride,
                                    cmd->pixels,
                                    &cmd->out_width,
                                    &cmd->out_height) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_DESTROY_WINDOW) {
        return gui_ipc_destroy_window(sched_current_pid(), (uint32_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_SET_MOUSE_VISIBLE) {
        if (a2 == 0) {
            return gui_ipc_mouse_visible();
        }
        uint32_t window_id = (uint32_t)(a2 >> 32);
        uint32_t visible = (uint32_t)a2;
        return gui_ipc_set_mouse_visible(sched_current_pid(), window_id, visible) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_UPDATE_WINDOW) {
        const struct gui_window_update_user *cmd;
        if (!user_range_ok(a2, sizeof(struct gui_window_update_user))) {
            return -LEONOS_EFAULT;
        }
        cmd = (const struct gui_window_update_user *)(uintptr_t)a2;
        if (!cmd->window_id || !cmd->mask ||
            (cmd->mask & ~GUI_IPC_WINDOW_UPDATE_ALL)) {
            return -LEONOS_EINVAL;
        }
        if (cmd->mask & GUI_IPC_WINDOW_UPDATE_TITLE) {
            size_t len;
            if (!cmd->title ||
                !user_range_ok((uint64_t)(uintptr_t)cmd->title, 1)) {
                return -LEONOS_EFAULT;
            }
            len = user_strlen(cmd->title, GUI_IPC_WINDOW_TITLE_MAX - 1);
            if (len == GUI_IPC_WINDOW_TITLE_MAX - 1 ||
                !user_range_ok((uint64_t)(uintptr_t)cmd->title, len + 1)) {
                return -LEONOS_EFAULT;
            }
        }
        return gui_ipc_update_window(sched_current_pid(), cmd->window_id,
                                     cmd->mask, cmd->flags, cmd->title) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_SET_TASKBAR_VISIBLE) {
        const struct gui_taskbar_request_user *cmd;
        if (!user_range_ok(a2, sizeof(struct gui_taskbar_request_user))) {
            return -LEONOS_EFAULT;
        }
        cmd = (const struct gui_taskbar_request_user *)(uintptr_t)a2;
        if (!cmd->window_id) {
            return -LEONOS_EINVAL;
        }
        return gui_ipc_set_taskbar_visible(sched_current_pid(), cmd->window_id,
                                           cmd->visible) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_CURSOR_REQUEST) {
        const struct gui_cursor_request_user *cmd;
        if (!user_range_ok(a2, sizeof(struct gui_cursor_request_user))) {
            return -LEONOS_EFAULT;
        }
        cmd = (const struct gui_cursor_request_user *)(uintptr_t)a2;
        if (!cmd->window_id || !cmd->flags ||
            (cmd->flags & ~GUI_IPC_CURSOR_REQUEST_ALL) ||
            ((cmd->flags & GUI_IPC_CURSOR_REQUEST_STYLE) &&
             cmd->style >= GUI_IPC_CURSOR_STYLE_COUNT)) {
            return -LEONOS_EINVAL;
        }
        return gui_ipc_request_cursor(sched_current_pid(), cmd->window_id,
                                      cmd->x, cmd->y, cmd->style,
                                      cmd->flags) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_WINDOW_EVENT) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_app_event))) {
            return -LEONOS_EFAULT;
        }
        struct gui_ipc_app_event *dst = (struct gui_ipc_app_event *)(uintptr_t)a2;
        return gui_ipc_pop_event(sched_current_pid(), dst->window_id, dst) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT) {
        struct gui_wait_app_event_user *wait;
        uint64_t delta;
        if (!user_range_ok(a2, sizeof(struct gui_wait_app_event_user))) {
            return -LEONOS_EFAULT;
        }
        wait = (struct gui_wait_app_event_user *)(uintptr_t)a2;
        if (!wait->event.window_id) {
            return -LEONOS_EINVAL;
        }
        if (gui_ipc_pop_event(sched_current_pid(), wait->event.window_id, &wait->event)) {
            return 1;
        }
        if (wait->timeout_ms == 0) {
            return 0;
        }
        delta = ((uint64_t)wait->timeout_ms * NTCLKS_TICK_HZ + 999ULL) / 1000ULL;
        if (delta == 0) {
            delta = 1;
        }
        sched_wait_current_for_window_event(wait->event.window_id, time_ticks() + delta);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_app_event))) {
            return -LEONOS_EFAULT;
        }
        const struct gui_ipc_app_event *src = (const struct gui_ipc_app_event *)(uintptr_t)a2;
        return gui_ipc_push_event(sched_current_pid(), src->window_id, src) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_TASKS) {
        struct task *viewer_task = sched_current_task();
        struct task_snapshot_info *temp;
        uint64_t tick = 0;
        uint32_t total;
        uint32_t visible = 0;
        if (!user_range_ok(a2, sizeof(struct task_snapshot_user))) {
            return -LEONOS_EFAULT;
        }
        struct task_snapshot_user *snap = (struct task_snapshot_user *)(uintptr_t)a2;
        if (snap->capacity > UINT32_MAX / sizeof(struct task_snapshot_info)) {
            return -LEONOS_EINVAL;
        }
        if (snap->capacity && !user_range_ok((uint64_t)(uintptr_t)snap->tasks,
                                             (uint64_t)snap->capacity * sizeof(struct task_snapshot_info))) {
            return -LEONOS_EFAULT;
        }
        total = sched_snapshot(NULL, 0, &tick);
        if (!total || total > UINT32_MAX / sizeof(*temp)) {
            snap->tick = tick;
            snap->count = 0;
            return 0;
        }
        temp = (struct task_snapshot_info *)kernel_malloc(
            (size_t)total * sizeof(*temp));
        if (!temp) {
            return -LEONOS_ENOMEM;
        }
        total = sched_snapshot(temp, total, &tick);
        for (uint32_t i = 0; i < total; ++i) {
            if (viewer_task &&
                task_effective_role(viewer_task) != LEONOS_AUTH_ROLE_ADMIN &&
                (!viewer_task->uid || temp[i].uid != viewer_task->uid)) {
                continue;
            }
            if (snap->tasks && visible < snap->capacity) {
                snap->tasks[visible] = temp[i];
            }
            ++visible;
        }
        snap->tick = tick;
        snap->count = visible;
        kernel_free(temp);
        return (int64_t)snap->count;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_TASK_KILL) {
        struct task *viewer_task = sched_current_task();
        struct task *target_task = sched_find((uint32_t)a2);
        int ret;
        if (!target_task) {
            return -LEONOS_ENOENT;
        }
        ret = authz_check_path(viewer_task, LEONOS_AUTHZ_KILL_TASK, 0,
                               target_task->uid, target_task->role);
        if (ret < 0) {
            return ret;
        }
        ret = sched_kill_user_task((uint32_t)a2, 137);
        if (ret == -2) {
            return -LEONOS_ENOENT;
        }
        if (ret < 0) {
            return -LEONOS_EINVAL;
        }
        net_close_owner_sockets((uint32_t)a2);
        gui_ipc_destroy_owner((uint32_t)a2);
        pty_process_exit((uint32_t)a2);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_REBOOT) {
        power_reboot();
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_SHUTDOWN) {
        power_shutdown();
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_DISPLAY_STATE) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_state))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_display_state((struct gui_ipc_display_state *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_DISPLAY_REQUEST) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_request))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_request_display((const struct gui_ipc_display_request *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_POLL_DISPLAY_REQUEST) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_request))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_pop_display_request(sched_current_pid(),
                                           (struct gui_ipc_display_request *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_state))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_publish_display_state(sched_current_pid(),
                                             (const struct gui_ipc_display_state *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_APPEARANCE_STATE) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_appearance_state))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_appearance_state((struct gui_ipc_appearance_state *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_APPEARANCE_REQUEST) {
        struct task *task = sched_current_task();
        if (!task || !task->uid) {
            return -LEONOS_EPERM;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_appearance_request))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_request_appearance(
                   (const struct gui_ipc_appearance_request *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_appearance_request))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_pop_appearance_request(sched_current_pid(),
                                               (struct gui_ipc_appearance_request *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE) {
        int ret = require_window_server();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct gui_ipc_appearance_state))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_publish_appearance_state(sched_current_pid(),
                                                 (const struct gui_ipc_appearance_state *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_TEXT_IOCTL_LAYOUT_UTF8) {
        static char text_buf[LEONOS_TEXT_LAYOUT_MAX_BYTES];
        static struct leonos_text_glyph glyph_buf[LEONOS_TEXT_LAYOUT_MAX_GLYPHS];
        struct leonos_text_layout *query;
        struct leonos_text_layout layout;
        uint32_t len;
        uint32_t capacity;
        uint32_t copy_count;
        if (!user_range_ok(a2, sizeof(struct leonos_text_layout))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_text_layout *)(uintptr_t)a2;
        layout = *query;
        if (!layout.text) {
            return -LEONOS_EFAULT;
        }
        len = layout.byte_len;
        if (len == 0) {
            len = (uint32_t)user_strlen(layout.text, LEONOS_TEXT_LAYOUT_MAX_BYTES);
            if (len == LEONOS_TEXT_LAYOUT_MAX_BYTES) {
                return -LEONOS_E2BIG;
            }
        }
        if (len > LEONOS_TEXT_LAYOUT_MAX_BYTES ||
            !user_range_ok((uint64_t)(uintptr_t)layout.text, len)) {
            return -LEONOS_EFAULT;
        }
        capacity = layout.capacity;
        if (capacity > LEONOS_TEXT_LAYOUT_MAX_GLYPHS) {
            capacity = LEONOS_TEXT_LAYOUT_MAX_GLYPHS;
        }
        if (capacity && (!layout.glyphs ||
            !user_range_ok((uint64_t)(uintptr_t)layout.glyphs,
                           (uint64_t)capacity * sizeof(struct leonos_text_glyph)))) {
            return -LEONOS_EFAULT;
        }
        for (uint32_t i = 0; i < len; ++i) {
            text_buf[i] = layout.text[i];
        }
        layout.text = text_buf;
        layout.byte_len = len;
        layout.capacity = capacity;
        layout.glyphs = glyph_buf;
        if (osmlayer_unicode_layout_utf8(&layout) < 0) {
            return -LEONOS_EINVAL;
        }
        copy_count = layout.count < capacity ? layout.count : capacity;
        for (uint32_t i = 0; i < copy_count; ++i) {
            query->glyphs[i] = glyph_buf[i];
        }
        query->byte_len = len;
        query->count = layout.count;
        query->total_cells = layout.total_cells;
        query->total_px = layout.total_px;
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_INSTALL_IOCTL_LIST_DISKS) {
        struct leonos_install_disk_list *query;
        struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
        uint32_t count = LEONOS_INSTALL_MAX_DISKS;
        if (!user_range_ok(a2, sizeof(struct leonos_install_disk_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_install_disk_list *)(uintptr_t)a2;
        if (query->capacity > LEONOS_INSTALL_MAX_DISKS) {
            query->capacity = LEONOS_INSTALL_MAX_DISKS;
        }
        if (query->capacity && (!query->disks ||
            !user_range_ok((uint64_t)(uintptr_t)query->disks,
                           (uint64_t)query->capacity * sizeof(struct leonos_install_disk)))) {
            return -LEONOS_EFAULT;
        }
        if (storage_install_list_disks(disks, count, &count) < 0) {
            return -LEONOS_EINVAL;
        }
        query->count = count;
        if (query->capacity < count) {
            count = query->capacity;
        }
        for (uint32_t i = 0; i < count; ++i) {
            ((struct leonos_install_disk *)(uintptr_t)query->disks)[i] = disks[i];
        }
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_INSTALL_IOCTL_FORMAT_TARGET) {
        int ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        return storage_install_format_target((uint32_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_INSTALL_IOCTL_MOUNT_TARGET) {
        int ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        return storage_install_mount_target((uint32_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_LIST_PARTITIONS) {
        struct leonos_disk_partition_list *query;
        struct leonos_disk_partition_list request;
        uint32_t count = 0;
        uint32_t copy_count;
        int ret;
        if (!user_range_ok(a2, sizeof(struct leonos_disk_partition_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_disk_partition_list *)(uintptr_t)a2;
        request = *query;
        if (request.reserved != 0 || request.capacity > LEONOS_DISK_MAX_PARTITIONS) {
            return -LEONOS_EINVAL;
        }
        if (request.capacity && (!request.partitions ||
            !user_range_ok((uint64_t)(uintptr_t)request.partitions,
                           (uint64_t)request.capacity * sizeof(struct leonos_disk_partition)))) {
            return -LEONOS_EFAULT;
        }
        ret = storage_disk_list_partitions(request.disk_id, disk_partition_scratch,
                                           LEONOS_DISK_MAX_PARTITIONS, &count);
        if (ret < 0) {
            return ret;
        }
        query->count = count;
        copy_count = count < request.capacity ? count : request.capacity;
        for (uint32_t i = 0; i < copy_count; ++i) {
            request.partitions[i] = disk_partition_scratch[i];
        }
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_FORMAT_PARTITION) {
        struct leonos_disk_partition_format request;
        int ret;
        if (!user_range_ok(a2, sizeof(request))) {
            return -LEONOS_EFAULT;
        }
        request = *(const struct leonos_disk_partition_format *)(uintptr_t)a2;
        if (request.reserved != 0 ||
            (request.filesystem != LEONOS_DISK_FILESYSTEM_FAT32 &&
             request.filesystem != LEONOS_DISK_FILESYSTEM_EXT2)) {
            return -LEONOS_EINVAL;
        }
        ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        return storage_disk_format_partition(&request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_DELETE_PARTITION) {
        struct leonos_disk_partition_delete request;
        int ret;
        if (!user_range_ok(a2, sizeof(request))) {
            return -LEONOS_EFAULT;
        }
        request = *(const struct leonos_disk_partition_delete *)(uintptr_t)a2;
        if (request.reserved0 != 0 || request.reserved1 != 0) {
            return -LEONOS_EINVAL;
        }
        ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        return storage_disk_delete_partition(&request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_CREATE_PARTITION) {
        struct leonos_disk_partition_create request;
        int ret;
        if (!user_range_ok(a2, sizeof(request))) {
            return -LEONOS_EFAULT;
        }
        request = *(const struct leonos_disk_partition_create *)(uintptr_t)a2;
        request.name[LEONOS_DISK_PARTITION_NAME_LEN - 1u] = 0;
        if (request.reserved != 0 || request.size_mib == 0 ||
            (request.filesystem != LEONOS_DISK_FILESYSTEM_FAT32 &&
             request.filesystem != LEONOS_DISK_FILESYSTEM_EXT2)) {
            return -LEONOS_EINVAL;
        }
        ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        return storage_disk_create_partition(&request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_MOUNT_PARTITION) {
        struct leonos_disk_partition_mount *query;
        struct leonos_disk_partition_mount request;
        int ret;
        if (!user_range_ok(a2, sizeof(request))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_disk_partition_mount *)(uintptr_t)a2;
        request = *query;
        if (request.reserved != 0 || request.drive != LEONOS_DISK_DRIVE_NONE) {
            return -LEONOS_EINVAL;
        }
        ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        ret = storage_disk_mount_partition(&request);
        if (ret == 0) {
            query->drive = request.drive;
        }
        return ret;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_DISK_IOCTL_UNMOUNT_PARTITION) {
        struct leonos_disk_partition_unmount request;
        uint32_t drive;
        int ret;
        if (!user_range_ok(a2, sizeof(request))) {
            return -LEONOS_EFAULT;
        }
        request = *(const struct leonos_disk_partition_unmount *)(uintptr_t)a2;
        if (request.reserved0 != 0 || request.reserved1 != 0) {
            return -LEONOS_EINVAL;
        }
        ret = authz_check_install(sched_current_task());
        if (ret < 0) {
            return ret;
        }
        ret = storage_disk_partition_mounted_drive(request.disk_id, request.partition_index,
                                                   &drive);
        if (ret < 0) {
            return ret;
        }
        if (sched_drive_in_use(drive)) {
            return -LEONOS_EBUSY;
        }
        return storage_disk_unmount_partition(&request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_LIST_DIR) {
        char path[LEONOS_FS_PATH_LEN];
        size_t len;
        uint32_t count = 0;
        struct leonos_dir_list *query;
        if (!user_range_ok(a2, sizeof(struct leonos_dir_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_dir_list *)(uintptr_t)a2;
        if (!query->path) {
            return -LEONOS_EFAULT;
        }
        len = user_strlen(query->path, LEONOS_FS_PATH_LEN);
        if (len == LEONOS_FS_PATH_LEN || !user_range_ok((uint64_t)(uintptr_t)query->path, len + 1)) {
            return -LEONOS_EFAULT;
        }
        for (size_t i = 0; i <= len; ++i) {
            path[i] = query->path[i];
        }
        if (storage_resolve_path(sched_current_task() ? sched_current_task()->cwd : "0:/",
                                 path, path, sizeof(path)) < 0) {
            return -LEONOS_EINVAL;
        }
        {
            int ret = authz_check_path(sched_current_task(), LEONOS_AUTHZ_READ, path, 0, 0);
            if (ret < 0) {
                return ret;
            }
        }
        if (query->capacity > LEONOS_FS_MAX_ENTRIES) {
            query->capacity = LEONOS_FS_MAX_ENTRIES;
        }
        if (query->capacity) {
            if (!query->entries) {
                return -LEONOS_EFAULT;
            }
            if (!user_range_ok((uint64_t)(uintptr_t)query->entries,
                               (uint64_t)query->capacity * sizeof(struct leonos_dir_entry))) {
                return -LEONOS_EFAULT;
            }
        }
        int ret = userland_list_dir(path, query->entries, query->capacity, &count);
        query->count = count;
        return ret;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_SYSTEM_INFO) {
        if (!user_range_ok(a2, sizeof(struct leonos_system_info))) {
            return -LEONOS_EFAULT;
        }
        *(struct leonos_system_info *)(uintptr_t)a2 = *ntclks_system_info();
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_TIME_INFO) {
        if (!user_range_ok(a2, sizeof(struct leonos_time_info))) {
            return -LEONOS_EFAULT;
        }
        return time_wall_clock((struct leonos_time_info *)(uintptr_t)a2) == 0
                   ? 0
                    : -LEONOS_EINVAL;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_TIME_NTP_SYNC) {
        int ret = require_background_service();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct leonos_time_sync))) {
            return -LEONOS_EFAULT;
        }
        return net_ntp_sync((struct leonos_time_sync *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_MACHINE_IDENTITY) {
        struct leonos_machine_identity identity;
        if (!user_range_ok(a2, sizeof(struct leonos_machine_identity))) {
            return -LEONOS_EFAULT;
        }
        platform_machine_identity(&identity);
        storage_boot_identity(&identity);
        *(struct leonos_machine_identity *)(uintptr_t)a2 = identity;
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_PING) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_ping))) {
            return -LEONOS_EFAULT;
        }
        return net_ping((struct leonos_net_ping *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_CONFIG) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_config))) {
            return -LEONOS_EFAULT;
        }
        return net_get_config((struct leonos_net_config *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_DNS_POLICY) {
        struct leonos_net_dns_policy *request;
        if (!user_range_ok(a2, sizeof(struct leonos_net_dns_policy))) {
            return -LEONOS_EFAULT;
        }
        request = (struct leonos_net_dns_policy *)(uintptr_t)a2;
        if (request->mode != LEONOS_NET_DNS_MODE_QUERY) {
            int ret = require_network_config_access();
            if (ret < 0) {
                return ret;
            }
        }
        return net_set_dns_policy(request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_DHCP) {
        int ret = require_network_config_access();
        if (ret < 0) {
            return ret;
        }
        if (!user_range_ok(a2, sizeof(struct leonos_net_dhcp))) {
            return -LEONOS_EFAULT;
        }
        return net_dhcp_renew((struct leonos_net_dhcp *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_DNS) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_dns))) {
            return -LEONOS_EFAULT;
        }
        return net_dns_resolve((struct leonos_net_dns *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_HTTP_GET) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_http_get))) {
            return -LEONOS_EFAULT;
        }
        return net_http_get((struct leonos_net_http_get *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_SOCKET_OPEN) {
        struct task *task = sched_current_task();
        if (!user_range_ok(a2, sizeof(struct leonos_net_socket_open))) {
            return -LEONOS_EFAULT;
        }
        return net_socket_open((struct leonos_net_socket_open *)(uintptr_t)a2,
                               task ? task->pid : 0,
                               task ? task->uid : 0);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_SOCKET_CONNECT) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_socket_connect))) {
            return -LEONOS_EFAULT;
        }
        return net_socket_connect((struct leonos_net_socket_connect *)(uintptr_t)a2,
                                  sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_SOCKET_SEND) {
        struct leonos_net_socket_io *io;
        if (!user_range_ok(a2, sizeof(struct leonos_net_socket_io))) {
            return -LEONOS_EFAULT;
        }
        io = (struct leonos_net_socket_io *)(uintptr_t)a2;
        if (io->length &&
            (!io->buffer ||
             !user_range_ok((uint64_t)(uintptr_t)io->buffer, io->length))) {
            return -LEONOS_EFAULT;
        }
        return net_socket_send(io, sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_SOCKET_RECV) {
        struct leonos_net_socket_io *io;
        if (!user_range_ok(a2, sizeof(struct leonos_net_socket_io))) {
            return -LEONOS_EFAULT;
        }
        io = (struct leonos_net_socket_io *)(uintptr_t)a2;
        if (io->length &&
            (!io->buffer ||
             !user_range_ok((uint64_t)(uintptr_t)io->buffer, io->length))) {
            return -LEONOS_EFAULT;
        }
        return net_socket_recv(io, sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_SOCKET_CLOSE) {
        if (!user_range_ok(a2, sizeof(struct leonos_net_socket_close))) {
            return -LEONOS_EFAULT;
        }
        return net_socket_close((struct leonos_net_socket_close *)(uintptr_t)a2,
                                sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_NET_CONNECTIONS) {
        struct leonos_net_connection_list *query;
        if (!user_range_ok(a2, sizeof(struct leonos_net_connection_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_net_connection_list *)(uintptr_t)a2;
        if (query->capacity > LEONOS_NET_SOCKET_MAX) {
            query->capacity = LEONOS_NET_SOCKET_MAX;
        }
        if (query->capacity &&
            (!query->entries ||
             !user_range_ok((uint64_t)(uintptr_t)query->entries,
                            (uint64_t)query->capacity *
                                sizeof(struct leonos_net_connection_info)))) {
            return -LEONOS_EFAULT;
        }
        return net_connections(query, sched_current_task());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_DRIVER_LIST) {
        struct leonos_driver_list *query;
        if (!user_range_ok(a2, sizeof(struct leonos_driver_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_driver_list *)(uintptr_t)a2;
        if (query->capacity > LEONOS_DRIVER_MAX) {
            query->capacity = LEONOS_DRIVER_MAX;
        }
        if (query->capacity &&
            (!query->drivers ||
             !user_range_ok((uint64_t)(uintptr_t)query->drivers,
                            (uint64_t)query->capacity *
                                sizeof(struct leonos_driver_info)))) {
            return -LEONOS_EFAULT;
        }
        return driver_manager_list(query);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_DRIVER_CONTROL) {
        struct leonos_driver_control *request;
        int ret;
        if (!user_range_ok(a2, sizeof(struct leonos_driver_control))) {
            return -LEONOS_EFAULT;
        }
        ret = require_driver_management();
        if (ret < 0) {
            return ret;
        }
        request = (struct leonos_driver_control *)(uintptr_t)a2;
        return driver_manager_control(request);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_AUDIO_CONFIGURE) {
        struct leonos_audio_format format;
        if (!user_range_ok(a2, sizeof(struct leonos_audio_format))) {
            return -LEONOS_EFAULT;
        }
        format = *(const struct leonos_audio_format *)(uintptr_t)a2;
        return driver_manager_audio_configure(&format);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_AUDIO_WRITE) {
        struct leonos_audio_write *request;
        uint32_t request_len;
        long ret;
        if (!user_range_ok(a2, sizeof(struct leonos_audio_write))) {
            return -LEONOS_EFAULT;
        }
        request = (struct leonos_audio_write *)(uintptr_t)a2;
        if (request->length > LEONOS_AUDIO_MAX_WRITE ||
            (request->length && (!request->data ||
             !user_range_ok((uint64_t)(uintptr_t)request->data,
                            request->length)))) {
            return -LEONOS_EFAULT;
        }
        request_len = request->length > LEONOS_AUDIO_IO_SLICE_BYTES
                          ? LEONOS_AUDIO_IO_SLICE_BYTES
                          : request->length;
        ret = driver_manager_audio_write(request->data, request_len,
                                         &request->status);
        request->transferred = ret > 0 ? (uint32_t)ret : 0;
        return ret < 0 ? ret : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_AUDIO_GET_STATE) {
        if (!user_range_ok(a2, sizeof(struct leonos_audio_state))) {
            return -LEONOS_EFAULT;
        }
        driver_manager_audio_get_state((struct leonos_audio_state *)(uintptr_t)a2);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_DEVICE_LIST) {
        struct leonos_device_list *query;
        struct leonos_device_info *devices;
        struct leonos_time_info time_info = {0};
        struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
        struct leonos_raw_device_info raw[LEONOS_RAW_DEVICE_MAX];
        uint32_t disk_count = LEONOS_INSTALL_MAX_DISKS;
        uint32_t raw_count = 0;
        uint32_t count = 0;
        char detail[LEONOS_DEVICE_DETAIL_LEN];
        if (!user_range_ok(a2, sizeof(struct leonos_device_list))) {
            return -LEONOS_EFAULT;
        }
        query = (struct leonos_device_list *)(uintptr_t)a2;
        if (query->capacity > LEONOS_DEVICE_MAX) {
            query->capacity = LEONOS_DEVICE_MAX;
        }
        if (query->capacity) {
            if (!query->devices ||
                !user_range_ok((uint64_t)(uintptr_t)query->devices,
                               (uint64_t)query->capacity * sizeof(struct leonos_device_info))) {
                return -LEONOS_EFAULT;
            }
        }
        devices = query->devices;

        if (time_wall_clock(&time_info) == 0) {
            uint32_t date = ((uint32_t)time_info.year << 16) |
                            ((uint32_t)time_info.month << 8) |
                            (uint32_t)time_info.day;
            uint32_t clock = ((uint32_t)time_info.hour << 16) |
                             ((uint32_t)time_info.minute << 8) |
                             (uint32_t)time_info.second;
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_RTC,
                           LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE,
                           date, clock, time_info.unix_seconds, time_info.uptime_ms);
        } else {
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_RTC, 0, 0, 0, 0, 0);
        }
        raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_KEYBOARD,
                       LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE,
                       1, 0, 1, 0);
        {
            const struct mouse_state *mouse = mouse_get_state();
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_MOUSE,
                           mouse && mouse->present
                               ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                               : 0,
                           mouse ? mouse->buttons : 0,
                           mouse && mouse->absolute ? 1u : 0u,
                           mouse ? (uint64_t)(uint32_t)mouse->x : 0,
                           mouse ? (uint64_t)(uint32_t)mouse->y : 0);
        }
        {
            const struct framebuffer *fb = framebuffer_get();
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_FRAMEBUFFER,
                           fb && fb->available
                               ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                               : 0,
                           fb ? fb->bpp : 0,
                           fb ? fb->pitch : 0,
                           fb ? fb->width : 0,
                           fb ? fb->height : 0);
        }
        if (storage_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) {
            disk_count = 0;
        }
        raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_AHCI,
                       storage_ready()
                           ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                           : 0,
                       0, 0, disk_count, 0);
        for (uint32_t i = 0; i < disk_count && i < LEONOS_INSTALL_MAX_DISKS; ++i) {
            uint32_t flags = LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE;
            if (disks[i].flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT) {
                flags |= LEONOS_DEVICE_FLAG_BOOT;
            }
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_DISK,
                           flags, disks[i].port, (uint32_t)i,
                           disks[i].sector_count, disks[i].sector_size);
        }
        raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_SERIAL,
                       serial_is_ready()
                           ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                           : 0,
                       0x3f8, 0, 0x3f8, 0);
        {
            uint32_t net_flags = 0;
            uint64_t net_mac = 0;
            uint32_t net_ip = 0;
            struct leonos_net_config net_cfg;
            net_device_info(&net_flags, &net_mac, &net_ip);
            if (net_get_config(&net_cfg) < 0) {
                net_cfg = (struct leonos_net_config){0};
            }
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_E1000,
                           net_flags, net_ip, net_cfg.source, net_mac, net_cfg.gateway_ip);
        }
        {
            struct leonos_audio_state audio = {0};
            uint32_t flags = 0;
            driver_manager_audio_get_state(&audio);
            if (audio.present) {
                flags |= LEONOS_DEVICE_FLAG_PRESENT;
            }
            if (audio.active) {
                flags |= LEONOS_DEVICE_FLAG_ACTIVE;
            }
            raw_device_add(raw, &raw_count, LEONOS_RAW_DEVICE_KIND_AC97, flags,
                           audio.sample_rate,
                           ((uint32_t)audio.channels << 16) | audio.bits_per_sample,
                           ((uint64_t)audio.vendor_id << 16) | audio.device_id,
                           ((uint64_t)audio.bus << 16) |
                               ((uint64_t)audio.slot << 8) | audio.function);
        }
        {
            struct leonos_device_catalog_query catalog = {
                .raw = raw,
                .raw_count = raw_count,
                .capacity = query->capacity,
                .devices = devices,
                .count = 0,
                .reserved = 0,
            };
            if (osmlayer_device_catalog(&catalog) == 0) {
                query->count = catalog.count;
                return 0;
            }
        }

        if (time_wall_clock(&time_info) == 0) {
            device_format_time(detail, sizeof(detail), &time_info);
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_SYSTEM,
                       LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE,
                       "RTC", "Running", detail, time_info.unix_seconds, time_info.uptime_ms);
        } else {
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_SYSTEM,
                       0, "RTC", "Unavailable", "CMOS wall clock not available", 0, 0);
        }

        device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_INPUT,
                   LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE,
                   "PS/2 Keyboard", "Running", "IRQ1 scancode input", 1, 0);

        {
            const struct mouse_state *mouse = mouse_get_state();
            device_format_mouse(detail, sizeof(detail), mouse);
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_INPUT,
                       mouse && mouse->present
                           ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                           : 0,
                       "PS/2 Mouse", mouse && mouse->present ? "Running" : "Unavailable",
                       detail, mouse ? (uint64_t)(uint32_t)mouse->x : 0,
                       mouse ? (uint64_t)(uint32_t)mouse->y : 0);
        }

        {
            const struct framebuffer *fb = framebuffer_get();
            device_format_fb(detail, sizeof(detail), fb);
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_DISPLAY,
                       fb && fb->available
                           ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                           : 0,
                       "Framebuffer", fb && fb->available ? "Running" : "Unavailable",
                       detail, fb ? fb->width : 0, fb ? fb->height : 0);
        }

        {
            uint32_t pos = 0;
            detail[0] = 0;
            device_append_text(detail, &pos, sizeof(detail), "SATA/AHCI controller, disks=");
            device_append_u64(detail, &pos, sizeof(detail), disk_count);
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_STORAGE,
                       storage_ready()
                           ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                           : 0,
                       "AHCI Controller", storage_ready() ? "Running" : "Unavailable",
                       detail, disk_count, 0);
        }

        for (uint32_t i = 0; i < disk_count && i < LEONOS_INSTALL_MAX_DISKS; ++i) {
            char name[LEONOS_DEVICE_NAME_LEN];
            uint32_t pos = 0;
            uint32_t flags = LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE;
            name[0] = 0;
            device_append_text(name, &pos, sizeof(name), "Disk ");
            device_append_u64(name, &pos, sizeof(name), i);
            device_format_disk(detail, sizeof(detail), &disks[i]);
            if (disks[i].flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT) {
                flags |= LEONOS_DEVICE_FLAG_BOOT;
            }
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_STORAGE,
                       flags, name,
                       (disks[i].flags & LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT)
                           ? "Boot root"
                           : "Ready",
                       detail, disks[i].sector_count, disks[i].sector_size);
        }

        device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_SERIAL,
                   serial_is_ready()
                       ? LEONOS_DEVICE_FLAG_PRESENT | LEONOS_DEVICE_FLAG_ACTIVE
                       : 0,
                   "Serial COM1", serial_is_ready() ? "Running" : "Unavailable",
                   "I/O port 0x3f8 debug console", 0x3f8, 0);

        {
            uint32_t net_flags = 0;
            uint64_t net_mac = 0;
            uint32_t net_ip = 0;
            struct leonos_net_config net_cfg;
            uint32_t pos = 0;
            net_device_info(&net_flags, &net_mac, &net_ip);
            if (net_get_config(&net_cfg) < 0) {
                net_cfg = (struct leonos_net_config){0};
            }
            detail[0] = 0;
            device_append_text(detail, &pos, sizeof(detail), "Intel e1000, ");
            device_append_text(detail, &pos, sizeof(detail),
                               net_cfg.source == LEONOS_NET_CONFIG_SOURCE_DHCP ? "DHCP IPv4 " : "static IPv4 ");
            device_append_ipv4(detail, &pos, sizeof(detail), net_cfg.local_ip);
            device_append_text(detail, &pos, sizeof(detail), ", gateway ");
            device_append_ipv4(detail, &pos, sizeof(detail), net_cfg.gateway_ip);
            device_append_text(detail, &pos, sizeof(detail), ", DNS ");
            device_append_ipv4(detail, &pos, sizeof(detail), net_cfg.dns_ip);
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_NETWORK,
                       net_flags, "Intel e1000", net_is_ready() ? "Running" : "Unavailable",
                       detail, net_mac, net_ip);
        }

        {
            struct leonos_audio_state audio = {0};
            const char *audio_name;
            uint32_t flags = 0;
            uint32_t pos = 0;
            driver_manager_audio_get_state(&audio);
            audio_name = audio.vendor_id == 0x1274U && audio.device_id == 0x1371U
                             ? "Ensoniq AudioPCI ES1371"
                             : audio.vendor_id == 0x8086U && audio.device_id == 0x2415U
                                   ? "Intel ICH AC'97"
                                   : "Audio Device";
            if (audio.present) {
                flags |= LEONOS_DEVICE_FLAG_PRESENT;
            }
            if (audio.active) {
                flags |= LEONOS_DEVICE_FLAG_ACTIVE;
            }
            detail[0] = 0;
            if (audio.active) {
                device_append_u64(detail, &pos, sizeof(detail), audio.sample_rate);
                device_append_text(detail, &pos, sizeof(detail), " Hz, ");
                device_append_u64(detail, &pos, sizeof(detail), audio.channels);
                device_append_text(detail, &pos, sizeof(detail), " ch, ");
                device_append_u64(detail, &pos, sizeof(detail), audio.bits_per_sample);
                device_append_text(detail, &pos, sizeof(detail), "-bit PCM");
            } else if (audio.present) {
                device_append_text(detail, &pos, sizeof(detail),
                                   "Audio device detected but driver not active");
            } else {
                device_append_text(detail, &pos, sizeof(detail),
                                   "No supported audio device detected");
            }
            device_add(devices, query->capacity, &count, LEONOS_DEVICE_CLASS_AUDIO,
                       flags, audio_name, audio.active ? "Running" : "Unavailable",
                       detail, ((uint64_t)audio.vendor_id << 16) | audio.device_id,
                       ((uint64_t)audio.bus << 16) |
                           ((uint64_t)audio.slot << 8) | audio.function);
        }

        query->count = count;
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_IOCTL_PERF_INFO) {
        struct leonos_perf_info *info;
        if (!user_range_ok(a2, sizeof(struct leonos_perf_info))) {
            return -LEONOS_EFAULT;
        }
        info = (struct leonos_perf_info *)(uintptr_t)a2;
        info->uptime_ms = time_uptime_ms();
        info->total_memory_kib = mm_total_memory_kib();
        info->free_memory_kib = mm_free_memory_kib();
        sched_cpu_ticks(&info->busy_ticks, &info->idle_ticks);
        sched_task_counts(&info->task_count, &info->running_tasks,
                          &info->ready_tasks, &info->sleeping_tasks);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_CREATE) {
        return pty_create(sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_DESTROY) {
        return pty_destroy(sched_current_pid(), (uint32_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_SELF) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->pty_id : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_INPUT_AVAILABLE) {
        struct task *task = sched_current_task();
        if (!task || !task->pty_id) {
            return -LEONOS_EINVAL;
        }
        return (int64_t)pty_input_available(task->pty_id);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_PTY_IOCTL_GET_PGRP || a1 == LEONOS_PTY_IOCTL_SET_PGRP)) {
        struct task *task = sched_current_task();
        int *process_group;
        if (!task || task_pty_stream_for_fd(task, (int)a0) < 0) {
            return -LEONOS_ENOTTY;
        }
        if (!user_range_ok(a2, sizeof(*process_group))) {
            return -LEONOS_EFAULT;
        }
        process_group = (int *)(uintptr_t)a2;
        if (a1 == LEONOS_PTY_IOCTL_GET_PGRP) {
            uint32_t value = 0;
            int result = pty_get_foreground_pgid(task->pty_id, &value);
            if (result < 0) {
                return result;
            }
            *process_group = (int)value;
            return 0;
        }
        if (*process_group <= 0) {
            return -LEONOS_EINVAL;
        }
        return pty_set_foreground_pgid(task->pty_id, task->pid,
                                       (uint32_t)*process_group);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_PTY_IOCTL_OWNER_GET_ATTR ||
         a1 == LEONOS_PTY_IOCTL_OWNER_SET_ATTR)) {
        struct leonos_pty_termios_io *io;
        if (!user_range_ok(a2, sizeof(*io))) {
            return -LEONOS_EFAULT;
        }
        io = (struct leonos_pty_termios_io *)(uintptr_t)a2;
        if (!pty_is_owner(io->pty_id, sched_current_pid())) {
            return -LEONOS_EINVAL;
        }
        if (a1 == LEONOS_PTY_IOCTL_OWNER_GET_ATTR) {
            return pty_get_termios(io->pty_id, &io->termios);
        }
        return pty_set_termios(io->pty_id, &io->termios);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_PTY_IOCTL_OWNER_GET_WINSIZE ||
         a1 == LEONOS_PTY_IOCTL_OWNER_SET_WINSIZE)) {
        struct leonos_pty_winsize_io *io;
        if (!user_range_ok(a2, sizeof(*io))) {
            return -LEONOS_EFAULT;
        }
        io = (struct leonos_pty_winsize_io *)(uintptr_t)a2;
        if (!pty_is_owner(io->pty_id, sched_current_pid())) {
            return -LEONOS_EINVAL;
        }
        if (a1 == LEONOS_PTY_IOCTL_OWNER_GET_WINSIZE) {
            return pty_get_winsize(io->pty_id, &io->winsize);
        }
        return pty_set_winsize(io->pty_id, &io->winsize);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_GET_ATTR) {
        struct task *task = sched_current_task();
        struct leonos_pty_termios *termios;
        if (!task || task_pty_stream_for_fd(task, (int)a0) < 0) {
            return -LEONOS_ENOTTY;
        }
        if (!user_range_ok(a2, sizeof(struct leonos_pty_termios))) {
            return -LEONOS_EFAULT;
        }
        termios = (struct leonos_pty_termios *)(uintptr_t)a2;
        return pty_get_termios(task->pty_id, termios);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_SET_ATTR) {
        struct task *task = sched_current_task();
        const struct leonos_pty_termios_request *request;
        if (!task || task_pty_stream_for_fd(task, (int)a0) < 0) {
            return -LEONOS_ENOTTY;
        }
        if (!user_range_ok(a2, sizeof(struct leonos_pty_termios_request))) {
            return -LEONOS_EFAULT;
        }
        request = (const struct leonos_pty_termios_request *)(uintptr_t)a2;
        return pty_set_termios(task->pty_id, &request->termios);
    }

    if (number == LINUX_SYS_IOCTL &&
        (a1 == LEONOS_PTY_IOCTL_GET_WINSIZE || a1 == LEONOS_PTY_IOCTL_SET_WINSIZE)) {
        struct task *task = sched_current_task();
        if (!task || task_pty_stream_for_fd(task, (int)a0) < 0) {
            return -LEONOS_ENOTTY;
        }
        if (!user_range_ok(a2, sizeof(struct leonos_pty_winsize))) {
            return -LEONOS_EFAULT;
        }
        if (a1 == LEONOS_PTY_IOCTL_GET_WINSIZE) {
            return pty_get_winsize(task->pty_id,
                                   (struct leonos_pty_winsize *)(uintptr_t)a2);
        }
        return pty_set_winsize(task->pty_id,
                               (const struct leonos_pty_winsize *)(uintptr_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_READ_OUTPUT) {
        struct leonos_pty_io *io;
        if (!user_range_ok(a2, sizeof(struct leonos_pty_io))) {
            return -LEONOS_EFAULT;
        }
        io = (struct leonos_pty_io *)(uintptr_t)a2;
        if (!io->buffer || !io->length ||
            !user_range_ok((uint64_t)(uintptr_t)io->buffer, io->length)) {
            return 0;
        }
        return pty_read_output(sched_current_pid(), io->pty_id, io->buffer, io->length);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_WRITE_INPUT) {
        const struct leonos_pty_io *io;
        if (!user_range_ok(a2, sizeof(struct leonos_pty_io))) {
            return -LEONOS_EFAULT;
        }
        io = (const struct leonos_pty_io *)(uintptr_t)a2;
        if (!io->buffer || !io->length ||
            !user_range_ok((uint64_t)(uintptr_t)io->buffer, io->length)) {
            return 0;
        }
        return pty_write_input(sched_current_pid(), io->pty_id, io->buffer, io->length);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_SPAWN) {
        const struct leonos_pty_spawn *spawn;
        struct task *task = sched_current_task();
        struct exec_params_kernel params;
        char path[LEONOS_FS_PATH_LEN];
        size_t len;
        if (!user_range_ok(a2, sizeof(struct leonos_pty_spawn))) {
            return -LEONOS_EFAULT;
        }
        spawn = (const struct leonos_pty_spawn *)(uintptr_t)a2;
        if (!spawn->path) {
            return -LEONOS_EFAULT;
        }
        len = user_strlen(spawn->path, LEONOS_PTY_PATH_LEN);
        if (len == LEONOS_PTY_PATH_LEN ||
            !user_range_ok((uint64_t)(uintptr_t)spawn->path, len + 1)) {
            return -LEONOS_EFAULT;
        }
        /* The terminal owns the PTY, while its shell and commands inherit it.
         * Permit both the owner and an attached descendant to spawn, but do
         * not accept arbitrary PTY ids from an unrelated process. */
        if (!task || !pty_is_active(spawn->pty_id) ||
            (!pty_is_owner(spawn->pty_id, sched_current_pid()) &&
             task->pty_id != spawn->pty_id)) {
            return -LEONOS_EINVAL;
        }
        {
            int ret = copy_exec_params_from_user(sched_current_task(),
                                                 (uint64_t)(uintptr_t)spawn->path,
                                                 (uint64_t)(uintptr_t)spawn->argv,
                                                 (uint64_t)(uintptr_t)spawn->envp,
                                                 path, sizeof(path), &params);
            if (ret < 0) {
                return ret;
            }
        }
        {
            int ret = authz_check_path(sched_current_task(), LEONOS_AUTHZ_EXEC, path, 0, 0);
            if (ret < 0) {
                return ret;
            }
        }
        int64_t pid = (spawn->stdin_fd >= 0 || spawn->stdout_fd >= 0 || spawn->stderr_fd >= 0)
                          ? userland_spawn_path_argv_with_fds(path,
                                                               (const char *const *)params.argv,
                                                               (const char *const *)params.envp,
                                                               spawn->pty_id,
                                                               spawn->stdin_fd,
                                                               spawn->stdout_fd,
                                                               spawn->stderr_fd)
                          : userland_spawn_path_argv(path,
                                                     (const char *const *)params.argv,
                                                     (const char *const *)params.envp,
                                                     spawn->pty_id);
        if (pid == -2) {
            return -LEONOS_ENOENT;
        }
        if (pid == -12) {
            return -LEONOS_ENOMEM;
        }
        if (pid == -7) {
            return -LEONOS_E2BIG;
        }
        return pid;
    }

    return -LEONOS_ENOSYS;
}

/**
 * @brief Coordinates the syscall dispatch frame operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
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

void syscall_dispatch_frame(struct trap_frame *frame)
{
    uint64_t number;
    int64_t result;
    if (!frame) {
        return;
    }
    number = frame->rax;
    storage_set_io_async_context(true);
    if (number == LINUX_SYS_FORK || number == LINUX_SYS_VFORK) {
        /* vfork intentionally uses fork semantics for now: sharing the
         * address space would let the child corrupt its suspended parent. */
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
    if (result != -LEONOS_EAGAIN) {
        storage_release_task_io(sched_current_pid());
    }
    storage_set_io_async_context(false);
    if (result == -LEONOS_EAGAIN) {
        /* int $0x80 has advanced RIP by two bytes.  Park this task for one
         * timer tick and re-execute the exact same instruction when its AHCI
         * DMA request can be polled again.  User programs keep normal
         * blocking read/open/stat semantics and never observe EAGAIN. */
        frame->rax = number;
        frame->rip -= 2u;
        sched_sleep_current_until(time_ticks() + 1u);
        return;
    }
    frame->rax = (uint64_t)result;
}
