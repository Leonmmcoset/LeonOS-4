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
#include <ntclks/permissions.h>
#include <ntclks/platform.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/futex.h>
#include <ntclks/smp.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/userland.h>
#include <ntclks/elf.h>
#include <ntclks/kernel_debug.h>

static int64_t syscall_dispatch_regs(uint64_t number, uint64_t a0, uint64_t a1,
                                     uint64_t a2, uint64_t a3, uint64_t a4,
                                     uint64_t a5);
static int copy_user_string_fixed(char *dst, uint32_t cap, uint64_t user_ptr,
                                  uint32_t *out_len);

#include <ntclks/version.h>

#include <leonos/device.h>
#include <leonos/audio.h>
#include <leonos/driver.h>
#include <leonos/auth.h>
#include <leonos/fs.h>
#include <leonos/fb.h>
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
#include <linux/time.h>
#include <linux/resource.h>
#include <linux/sysinfo.h>
#include <linux/times.h>
#include <linux/uio.h>

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

#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/dirent.h>
_Static_assert(sizeof(struct linux_stat_abi) == 144, "Linux x86-64 stat layout");

static uint64_t linux_stat_inode(const char *path, const struct leonos_stat *st)
{

    uint64_t hash = 1469598103934665603ULL;
    if (path) {
        for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
            hash ^= *p;
            hash *= 1099511628211ULL;
        }
    }
    if (st) hash ^= st->size + ((uint64_t)st->type << 32);
    hash &= 0x7fffffffffffffffULL;
    return hash ? hash : 1;
}

static int linux_stat_from_legacy(struct linux_stat_abi *out,
                                   const struct leonos_stat *st,
                                   const char *path, const struct storage_node *node)
{
    struct storage_node found;
    if (!node && path && storage_lookup_path(path, &found) == 0) node = &found;
    if (node && (node->flags & STORAGE_NODE_FLAG_EXT2)) return storage_inode_stat(node, out);
    uint32_t type = st ? st->type : LEONOS_FS_TYPE_FILE;
    uint32_t mode = type == LEONOS_FS_TYPE_DIR ? 0040755u :
                    type == LEONOS_FS_TYPE_SOCKET ? LINUX_S_IFSOCK | 0777u :
                    type == LEONOS_FS_TYPE_DEVICE ? 0020660u : 0100644u;
    *out = (struct linux_stat_abi){0};
    out->st_dev = node ? (uint64_t)node->volume_id + 1 : 1;
    out->st_ino = linux_stat_inode(path, st);
    out->st_nlink = 1;
    out->st_mode = mode;
    if (path) {
        struct leonos_permissions value;
        int ret = fs_permissions_get(path, node, &value);
        if (ret < 0) return ret;
        out->st_uid = value.uid;
        out->st_gid = value.gid;
        out->st_mode = ((value.mode & LINUX_S_IFMT) ? value.mode & LINUX_S_IFMT : mode & LINUX_S_IFMT) |
            (value.mode & 07777u);
    }
    out->st_rdev = type == LEONOS_FS_TYPE_DEVICE ? 1 : 0;
    out->st_size = st ? (int64_t)st->size : 0;
    out->st_blksize = 4096;
    out->st_blocks = (out->st_size + 511) / 512;
    return 0;
}

static void linux_statx_from_legacy(struct linux_statx *out,
                                    const struct linux_stat_abi *st,
                                    uint32_t mask)
{
    const uint32_t available = LINUX_STATX_TYPE | LINUX_STATX_MODE |
        LINUX_STATX_NLINK | LINUX_STATX_UID | LINUX_STATX_GID |
        LINUX_STATX_INO | LINUX_STATX_SIZE | LINUX_STATX_BLOCKS;
    uint32_t returned = mask & available;
    *out = (struct linux_statx){0};
    out->stx_mask = returned;
    out->stx_blksize = st->st_blksize > 0 ? (uint32_t)st->st_blksize : 4096;
    if (returned & (LINUX_STATX_TYPE | LINUX_STATX_MODE)) out->stx_mode = (uint16_t)st->st_mode;
    if (returned & LINUX_STATX_NLINK) out->stx_nlink = (uint32_t)st->st_nlink;
    if (returned & LINUX_STATX_UID) out->stx_uid = st->st_uid;
    if (returned & LINUX_STATX_GID) out->stx_gid = st->st_gid;
    if (returned & LINUX_STATX_INO) out->stx_ino = st->st_ino;
    if (returned & LINUX_STATX_SIZE) out->stx_size = st->st_size < 0 ? 0 : (uint64_t)st->st_size;
    if (returned & LINUX_STATX_BLOCKS) out->stx_blocks = st->st_blocks < 0 ? 0 : (uint64_t)st->st_blocks;
}


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
    if (file->description) {
        struct task_file *description = file->description;
        *file = (struct task_file){0};
        if (--description->references == 0) {
            clear_task_file(description);
            kernel_free(description);
        }
        task_socket_collect();
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

static struct task_file *task_file_promote(struct task_file *source)
{
    if (source->description) return source->description;
    if (source->references) return source;
    struct task_file *description = kernel_malloc(sizeof(*description));
    if (!description) return NULL;
    *description = *source;
    description->references = 1;
    description->fd_flags = 0;
    source->description = description;
    return description;
}

int task_file_reference(struct task_file *destination, struct task_file *source)
{
    if (!source || !source->used || !destination) return -LEONOS_EBADF;
    struct task_file *description = task_file_promote(source);
    if (!description) return -LEONOS_ENOMEM;
    *destination = (struct task_file){.used = 1, .description = description};
    ++description->references;
    return 0;
}

struct task_file *task_file_get(struct task_file *source)
{
    if (!source || !source->used) return NULL;
    struct task_file *description = task_file_promote(source);
    if (description) ++description->references;
    return description;
}

void task_file_put(struct task_file *description)
{
    if (description && --description->references == 0) {
        clear_task_file(description);
        kernel_free(description);
    }
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
        clear_task_file(&sched_task_fds(task)->stdio_files[i]);
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
    if (!parent || !child) {
        return -LEONOS_EINVAL;
    }
    /* Finish all allocations before child references are published, so an
     * allocation failure cannot leak half of the inherited descriptor set. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < sched_task_file_capacity(child) + 3; ++i) {
            struct task_file *source = i < 3 ? &sched_task_fds(parent)->stdio_files[i] :
                sched_task_file_at((struct task *)parent, i - 3);
            struct task_file *destination = i < 3 ? &sched_task_fds(child)->stdio_files[i] :
                sched_task_file_at(child, i - 3);
            if (!source || !source->used) continue;
            if (!pass) {
                if (!task_file_promote(source)) return -LEONOS_ENOMEM;
            } else {
                task_file_reference(destination, source);
                destination->fd_flags = source->fd_flags;
            }
        }
    }
    return 0;
}

static void task_pty_release_entry(struct task_pty_fd *entry);

int syscall_unshare_task_files(struct task *task)
{
    struct task_fd_table_state *old = task->shared_files;
    if (!old || old->references == 1) return 0;
    struct task_fd_table_state *copy = kernel_malloc(sizeof(*copy));
    if (!copy) return -LEONOS_ENOMEM;
    *copy = *old;
    copy->references = 1;
    copy->file_extra = NULL;
    if (old->file_extra_capacity) {
        copy->file_extra = kernel_malloc(old->file_extra_capacity * sizeof(struct task_file));
        if (!copy->file_extra) { kernel_free(copy); return -LEONOS_ENOMEM; }
        for (uint32_t i = 0; i < old->file_extra_capacity; ++i) copy->file_extra[i] = old->file_extra[i];
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < 3 + SCHED_TASK_FILE_MAX + old->file_extra_count; ++i) {
            struct task_file *source = i < 3 ? &old->stdio_files[i] :
                i < 3 + SCHED_TASK_FILE_MAX ? &old->files[i - 3] :
                &old->file_extra[i - 3 - SCHED_TASK_FILE_MAX];
            struct task_file *destination = i < 3 ? &copy->stdio_files[i] :
                i < 3 + SCHED_TASK_FILE_MAX ? &copy->files[i - 3] :
                &copy->file_extra[i - 3 - SCHED_TASK_FILE_MAX];
            if (!source->used) continue;
            if (!pass) {
                if (!task_file_promote(source)) {
                    kernel_free(copy->file_extra);
                    kernel_free(copy);
                    return -LEONOS_ENOMEM;
                }
            } else {
                task_file_reference(destination, source);
                destination->fd_flags = source->fd_flags;
            }
        }
    }
    --old->references;
    task->shared_files = copy;
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
        if ((sched_task_fds(task)->stdio_files[i].used &&
             (sched_task_fds(task)->stdio_files[i].fd_flags & 1u)) ||
            (sched_task_fds(task)->cloexec_stdio_mask & (1u << i))) {
            clear_task_file(&sched_task_fds(task)->stdio_files[i]);
            sched_task_fds(task)->closed_stdio_mask |= 1u << i;
            sched_task_fds(task)->cloexec_stdio_mask &= ~(1u << i);
        }
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        if (sched_task_fds(task)->pty_fds[i].used && (sched_task_fds(task)->pty_fds[i].flags & 1u)) {
            int fd = sched_task_fds(task)->pty_fds[i].fd;
            task_pty_release_entry(&sched_task_fds(task)->pty_fds[i]);
            if (fd >= 0 && fd < 3) sched_task_fds(task)->closed_stdio_mask |= 1u << fd;
        }
    }
}

/**
 * Task file for fd.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
struct task_file *task_descriptor_for_fd(struct task *task, int fd)
{
    if (!task || fd < 0) {
        return NULL;
    }
    if (fd < 3) {
        return sched_task_fds(task)->stdio_files[fd].used ? &sched_task_fds(task)->stdio_files[fd] : NULL;
    }
    if (fd >= 3 + (int)sched_task_file_capacity(task)) return NULL;
    struct task_file *file = sched_task_file_at(task, (uint32_t)(fd - 3));
    if (!file) return NULL;
    return file->used ? file : NULL;
}

struct task_file *task_file_for_fd(struct task *task, int fd)
{
    return task_file_description(task_descriptor_for_fd(task, fd));
}

struct task_file *task_file_for_io(struct task *task, int fd)
{
    return task && task->syscall_file && task->syscall_fd == fd
        ? task->syscall_file : task_file_for_fd(task, fd);
}

void task_release_syscall_file(struct task *task)
{
    if (!task) return;
    struct task_file *file = task->syscall_file;
    task->syscall_file = NULL;
    task->socket_io_deadline = 0;
    task->socket_io_timed = false;
    if (file) task_file_put(file);
}

#define LEONOS_F_DUPFD LINUX_F_DUPFD
#define LEONOS_F_GETFD LINUX_F_GETFD
#define LEONOS_F_SETFD LINUX_F_SETFD
#define LEONOS_F_GETFL LINUX_F_GETFL
#define LEONOS_F_SETFL LINUX_F_SETFL
#define LEONOS_F_DUPFD_CLOEXEC LINUX_F_DUPFD_CLOEXEC

/**
 * Task pty fd for fd.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
struct task_pty_fd *task_pty_fd_for_fd(struct task *task, int fd)
{
    if (!task || fd < 0) {
        return NULL;
    }
    for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
        struct task_pty_fd *entry = &sched_task_fds(task)->pty_fds[i];
        if (entry->used && entry->fd == fd) {
            return entry;
        }
    }
    return NULL;
}

static int task_pty_fd_available(struct task *task, int fd);
static int task_unused_fd(struct task *task, int minimum);

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
    if (endpoint == TASK_PTY_ENDPOINT_SLAVE && !pty_slave_open_allowed(pty_id)) {
        return -LEONOS_EIO;
    }
    candidate = task_unused_fd(task, 0);
    if (candidate < 0) return candidate;
    for (uint32_t attempts = 0;
         attempts < SCHED_TASK_PTY_FD_MAX + SCHED_TASK_FILE_MAX + 4u;
         ++attempts, ++candidate) {
        if (!task_pty_fd_available(task, candidate)) continue;
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            struct task_pty_fd *entry = &sched_task_fds(task)->pty_fds[i];
            if (!entry->used) {
                *entry = (struct task_pty_fd){
                    .used = 1, .fd = candidate,
                    .flags = (flags & LEONOS_O_CLOEXEC) ? LEONOS_FD_CLOEXEC : 0,
                    .status_flags = flags & (LEONOS_O_ACCMODE |
                                             LEONOS_O_APPEND |
                                             LEONOS_O_NONBLOCK),
                    .stream = endpoint == TASK_PTY_ENDPOINT_MASTER ? 1u : 0u,
                    .pty_id = pty_id, .endpoint = endpoint,
                };
                if (candidate < 3) {
                    sched_task_fds(task)->closed_stdio_mask &= ~(1u << candidate);
                    sched_task_fds(task)->cloexec_stdio_mask &= ~(1u << candidate);
                }
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
static int alloc_task_fd(struct task *task, const struct storage_node *node,
                         uint32_t flags, const char *path);

static int64_t syscall_eventfd_create(uint64_t initial, uint32_t flags)
{
    struct task *task = sched_current_task();
    struct storage_node node = {
        .type = LEONOS_FS_TYPE_DEVICE,
        .flags = STORAGE_NODE_FLAG_DEV_NODE,
        .first_cluster = 0,
    };
    struct task_file *file;
    int fd;
    if (!task || initial > UINT64_MAX - 1ULL ||
        (flags & ~(LINUX_EFD_SEMAPHORE | LINUX_EFD_CLOEXEC | LINUX_EFD_NONBLOCK))) {
        return -LEONOS_EINVAL;
    }
    fd = alloc_task_fd(task, &node,
                       TASK_FILE_FLAG_EVENTFD | LEONOS_O_RDWR |
                       (flags & (LINUX_EFD_CLOEXEC | LINUX_EFD_NONBLOCK)), NULL);
    if (fd < 0) return fd;
    file = task_file_for_fd(task, fd);
    if (!file) return -LEONOS_EMFILE;
    file->aux = initial;
    file->aux2 = (flags & LINUX_EFD_SEMAPHORE) != 0;
    return fd;
}

static int64_t syscall_memfd_create(uint64_t name_ptr, uint32_t flags)
{
    struct task *task = sched_current_task();
    struct storage_node node = {
        .type = LEONOS_FS_TYPE_DEVICE,
        .flags = STORAGE_NODE_FLAG_DEV_NODE,
        .first_cluster = STORAGE_DEV_KIND_SHM,
    };
    struct task_file *file;
    char name[256];
    int fd;
    int ret;
    if (!task || !name_ptr || (flags & ~(1u | 2u))) return -LEONOS_EINVAL;
    ret = copy_user_string_fixed(name, sizeof(name), name_ptr, NULL);
    if (ret < 0) return ret;
    if (!name[0]) return -LEONOS_EINVAL;
    fd = alloc_task_fd(task, &node, LEONOS_O_RDWR, NULL);
    if (fd < 0) return fd;
    file = task_file_for_fd(task, fd);
    if (!file || task_shm_attach(file) < 0) {
        if (file) clear_task_file(file);
        return -LEONOS_ENOMEM;
    }
    if (task_shm_truncate(file, 0) < 0) {
        clear_task_file(file);
        return -LEONOS_ENOMEM;
    }
    if (flags & 1u) file->fd_flags = LEONOS_FD_CLOEXEC;
    return fd;
}

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
        (sched_task_fds(task)->closed_stdio_mask & (1u << (uint32_t)fd)) != 0) {
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
    entry = task_pty_fd_for_fd(task, fd);
    if (entry) return (int)entry->stream;
    return fd >= 0 && fd <= 2 ? fd : -1;
}

/**
 * Task pty fd available.
 * @param task Value supplied by the caller.
 * @param fd Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_pty_fd_available(struct task *task, int fd)
{
    if (!task || fd < 0 || task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) {
        return 0;
    }
    if (fd < 3 && !(sched_task_fds(task)->closed_stdio_mask & (1u << fd))) return 0;
    return 1;
}

/**
 * @brief Find the lowest free descriptor below RLIMIT_NOFILE across all backing tables.
 * @param task Descriptor-table owner.
 * @param minimum Inclusive lower bound.
 * @return Descriptor number or negative errno.
 */
static int task_unused_fd(struct task *task, int minimum)
{
    if (!task || minimum < 0) return -LEONOS_EINVAL;
    for (uint64_t fd = (uint32_t)minimum; fd < sched_task_limits(task)->nofile.rlim_cur && fd < INT32_MAX; ++fd) {
        if (task_pty_fd_available(task, (int)fd)) return (int)fd;
    }
    return -LEONOS_EMFILE;
}

int task_can_allocate_fd(const struct task *task)
{
    return task_unused_fd((struct task *)task, 0) >= 0;
}

/**
 * @brief Reserve the lowest free file descriptor, growing backing storage if needed.
 * @param task Descriptor-table owner, serialized by the syscall execution lock.
 * @param minimum Inclusive lower bound.
 * @param slot Receives the initialized descriptor entry.
 * @return Descriptor number or negative errno without consuming a slot on failure.
 */
int task_allocate_fd(struct task *task, int minimum, struct task_file **slot)
{
    int fd = task_unused_fd(task, minimum);
    if (fd < 0) return fd;
    struct task_file *file = fd < 3 ? &sched_task_fds(task)->stdio_files[fd] :
        sched_task_file_at(task, (uint32_t)fd - 3);
    if (!file) return -LEONOS_ENOMEM;
    *file = (struct task_file){.used = 1};
    if (fd < 3) {
        sched_task_fds(task)->closed_stdio_mask &= ~(1u << fd);
        sched_task_fds(task)->cloexec_stdio_mask &= ~(1u << fd);
    }
    *slot = file;
    return fd;
}

/**
 * @brief Release a file descriptor after a failed multi-fd operation, including stdio slots.
 * @param task Descriptor-table owner.
 * @param fd Reserved file descriptor to roll back.
 */
void task_discard_file_fd(struct task *task, int fd)
{
    clear_task_file(task_descriptor_for_fd(task, fd));
    if (fd >= 0 && fd < 3) {
        sched_task_fds(task)->closed_stdio_mask |= 1u << fd;
        sched_task_fds(task)->cloexec_stdio_mask &= ~(1u << fd);
    }
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
    if (stream < 0 && !(source && source->endpoint)) {
        return -LEONOS_EBADF;
    }
    if (minimum_fd < 0 || (uint64_t)minimum_fd >= sched_task_limits(task)->nofile.rlim_cur) return -LEONOS_EINVAL;
    candidate = task_unused_fd(task, minimum_fd);
    if (candidate < 0) return candidate;
    for (uint32_t attempts = 0; attempts < SCHED_TASK_PTY_FD_MAX + SCHED_TASK_FILE_MAX + 4u;
         ++attempts, ++candidate) {
        if (candidate < 0) {
            return -LEONOS_EINVAL;
        }
        if (!task_pty_fd_available(task, candidate)) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            struct task_pty_fd *entry = &sched_task_fds(task)->pty_fds[i];
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
                if (candidate < 3) sched_task_fds(task)->closed_stdio_mask &= ~(1u << candidate);
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
    struct task_pty_fd *source = task_pty_fd_for_fd(task, old_fd);
    int stream = task_pty_stream_for_fd(task, old_fd);
    if (!task || (stream < 0 && !(source && source->endpoint)) || new_fd < 0)
        return -LEONOS_EBADF;
    if (old_fd == new_fd) return new_fd;
    if ((uint64_t)new_fd >= sched_task_limits(task)->nofile.rlim_cur) return -LEONOS_EBADF;
    struct task_pty_fd retained = source ? *source : (struct task_pty_fd){
        .used = 1, .stream = (uint32_t)stream,
        .status_flags = stream == 0 ? LEONOS_O_RDONLY : LEONOS_O_WRONLY,
    };
    retained.fd = new_fd;
    retained.flags = 0;
    struct task_pty_fd *entry = task_pty_fd_for_fd(task, new_fd);
    if (!entry) {
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            if (!sched_task_fds(task)->pty_fds[i].used) {
                entry = &sched_task_fds(task)->pty_fds[i];
                break;
            }
        }
    }
    if (!entry) return -LEONOS_EMFILE;
    /* Allocate/validate before replacing any existing target reference. */
    clear_task_file(task_descriptor_for_fd(task, new_fd));
    if (entry->used) task_pty_release_entry(entry);
    *entry = retained;
    if (new_fd < 3) {
        sched_task_fds(task)->closed_stdio_mask &= ~(1u << new_fd);
        if (retained.endpoint == TASK_PTY_ENDPOINT_SLAVE) {
            task->pty_id = retained.pty_id;
        }
    }
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
    if (!task || !node) return -LEONOS_EINVAL;
    struct task_file *file;
    int fd = task_allocate_fd(task, 0, &file);
    if (fd < 0) return fd;
    file->node = *node;
    file->flags = flags;
    file->fd_flags = (flags & LEONOS_O_CLOEXEC) ? LEONOS_FD_CLOEXEC : 0;
    copy_text(file->path, sizeof(file->path), path);
    return fd;
}


static int task_dup2_fd(struct task *task, int old_fd, int new_fd)
{
    struct task_file *old_file = task_file_for_fd(task, old_fd);
    if (!task || !old_file || new_fd < 0) return -LEONOS_EBADF;
    if (old_fd == new_fd) return new_fd;
    if ((uint64_t)new_fd >= sched_task_limits(task)->nofile.rlim_cur) return -LEONOS_EBADF;
    /* Retain before table growth: an extra-table source can move when the
     * destination is expanded. Failure must also preserve the target. */
    struct task_file retained = {0};
    int ret = task_file_reference(&retained, old_file);
    if (ret < 0) return ret;
    struct task_file *new_file = new_fd < 3 ? &sched_task_fds(task)->stdio_files[new_fd] :
        sched_task_file_at(task, (uint32_t)new_fd - 3);
    if (!new_file) { clear_task_file(&retained); return -LEONOS_ENOMEM; }
    struct task_pty_fd *replaced_pty = task_pty_fd_for_fd(task, new_fd);
    if (new_file->used) clear_task_file(new_file);
    if (replaced_pty) task_pty_release_entry(replaced_pty);
    *new_file = retained;
    if (new_fd < 3) sched_task_fds(task)->closed_stdio_mask &= ~(1u << new_fd);
    return new_fd;
}

static int task_duplicate_file_fd(struct task *task, int old_fd, int minimum_fd,
                                  uint32_t fd_flags)
{
    struct task_file *source = task_file_for_fd(task, old_fd);
    if (!source) return -LEONOS_EBADF;
    if (minimum_fd < 0 || (uint64_t)minimum_fd >= sched_task_limits(task)->nofile.rlim_cur) return -LEONOS_EINVAL;
    struct task_file retained = {0};
    int ret = task_file_reference(&retained, source);
    if (ret < 0) return ret;
    struct task_file *target;
    int fd = task_allocate_fd(task, minimum_fd, &target);
    if (fd < 0) { clear_task_file(&retained); return fd; }
    *target = retained;
    target->fd_flags = fd_flags;
    return fd;
}

int syscall_inherit_task_fds(struct task *parent, struct task *child,
                             int stdin_fd, int stdout_fd, int stderr_fd)
{
    const int requested[3] = {stdin_fd, stdout_fd, stderr_fd};
    if (!child) return -LEONOS_EINVAL;
    for (int i = 0; i < 3; ++i) {
        struct task_file *source;
        struct task_file *target = &sched_task_fds(child)->stdio_files[i];
        if (requested[i] < 0) continue;
        source = task_file_for_fd(parent, requested[i]);
        if (parent && requested[i] < (int)SCHED_TASK_STDIO_MAX &&
            (sched_task_fds(parent)->closed_stdio_mask & (1u << (uint32_t)requested[i])) != 0) {
            sched_task_fds(child)->closed_stdio_mask |= 1u << (uint32_t)i;
            continue;
        }
        /* No explicit file means the child's PTY supplies this stream. */
        if (!source && requested[i] >= 0 && requested[i] <= 2) continue;
        if (!source) {
            return -LEONOS_EBADF;
        }
        if (target->used) clear_task_file(target);
        int ret = task_file_reference(target, source);
        if (ret < 0) return ret;
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
    if (_IOC_NR(request) >= 0x40 && _IOC_NR(request) < 0x80) {
        if (device_kind != STORAGE_DEV_KIND_MOUSE || size != sizeof(struct input_absinfo))
            return -LEONOS_EINVAL;
        return input_evdev_absinfo(_IOC_NR(request) - 0x40,
                                   (struct input_absinfo *)(uintptr_t)user_arg);
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
    if (len == cap) return user_range_ok(user_ptr, cap) ? -LEONOS_ENAMETOOLONG : -LEONOS_EFAULT;
    if (!user_range_ok(user_ptr, len + 1)) {
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
static int resolve_user_path_ids(struct task *task, uint64_t user_ptr, char *out,
                                  uint32_t cap, bool real_ids)
{
    char raw[LEONOS_FS_PATH_LEN];
    int ret = copy_user_path(raw, sizeof(raw), user_ptr);
    if (ret < 0) {
        return ret;
    }
    if (!raw[0]) return -LEONOS_ENOENT;
    return fs_permissions_resolve(task, task ? sched_task_cwd(task) : "/", raw, out, cap, real_ids);
}

static int resolve_user_path(struct task *task, uint64_t user_ptr, char *out, uint32_t cap)
{
    return resolve_user_path_ids(task, user_ptr, out, cap, false);
}

static int resolve_user_path_at(struct task *task, int32_t dirfd, uint64_t user_ptr,
                                bool allow_empty, char *path, struct task_file **empty_file,
                                bool real_ids)
{
    char raw[LEONOS_FS_PATH_LEN];
    int ret = copy_user_path(raw, sizeof(raw), user_ptr);
    if (ret < 0) return ret;
    if (empty_file) *empty_file = NULL;
    const char *base = task ? sched_task_cwd(task) : "/";
    if (!raw[0] && !allow_empty) return -LEONOS_ENOENT;
    if (raw[0] != '/' && dirfd != LINUX_AT_FDCWD) {
        struct task_file *file = task_file_for_fd(task, dirfd);
        if (!file || !file->path[0]) return -LEONOS_EBADF;
        if (raw[0] && file->node.type != LEONOS_FS_TYPE_DIR) return -LEONOS_ENOTDIR;
        if (!raw[0] && empty_file) *empty_file = file;
        base = file->path;
    }
    if (!raw[0]) { copy_text(path, LEONOS_FS_PATH_LEN, base); return 0; }
    return fs_permissions_resolve(task, base, raw, path, LEONOS_FS_PATH_LEN, real_ids);
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

/*
 * Linux sendfile(2) transfers bytes from a regular input file without
 * changing its open-file-description offset when an explicit offset pointer
 * is supplied. Keep the copy in kernel memory so the output may be a pipe or
 * socket as well as another regular file.
 */
static int64_t syscall_sendfile(uint64_t out_fd_arg, uint64_t in_fd_arg,
                                uint64_t offset_arg, uint64_t count_arg)
{
    struct task *task = sched_current_task();
    struct task_file *out;
    struct task_file *in;
    uint8_t *buffer;
    uint64_t position;
    uint64_t total = 0;
    uint64_t count = count_arg > 0x7ffff000ULL ? 0x7ffff000ULL : count_arg;
    int64_t result = 0;

    if (!task) return -LEONOS_EPERM;
    out = task_file_for_fd(task, (int)out_fd_arg);
    in = task_file_for_fd(task, (int)in_fd_arg);
    if (!out || !in) return -LEONOS_EBADF;
    if (out == in || task_file_description(out) == task_file_description(in)) {
        return -LEONOS_EINVAL;
    }
    if (!file_can_read(in) || !file_can_write(out)) return -LEONOS_EBADF;
    if (in->node.type != LEONOS_FS_TYPE_FILE ||
        (in->node.flags & STORAGE_NODE_FLAG_PROC)) return -LEONOS_EINVAL;
    if (out->node.type != LEONOS_FS_TYPE_FILE &&
        !(out->flags & (TASK_FILE_FLAG_PIPE | TASK_FILE_FLAG_SOCKET_UNIX |
                        TASK_FILE_FLAG_SOCKET_INET))) return -LEONOS_EINVAL;
    if (out->node.type == LEONOS_FS_TYPE_FILE && (out->flags & LEONOS_O_APPEND)) {
        return -LEONOS_EINVAL;
    }
    if (offset_arg) {
        if (!user_range_ok(offset_arg, sizeof(int64_t)) ||
            !user_range_writable(offset_arg, sizeof(int64_t))) return -LEONOS_EFAULT;
        if (*(const int64_t *)(uintptr_t)offset_arg < 0) return -LEONOS_EINVAL;
        position = (uint64_t)*(const int64_t *)(uintptr_t)offset_arg;
    } else {
        position = in->offset;
    }
    if (!count || position >= in->node.size) return 0;

    buffer = kernel_malloc(LEONOS_FS_IO_SLICE_BYTES);
    if (!buffer) return -LEONOS_ENOMEM;
    while (total < count && position < in->node.size) {
        uint64_t available = in->node.size - position;
        uint32_t request = (uint32_t)(count - total);
        uint32_t got = 0;
        uint32_t written = 0;
        int ret;
        if (request > LEONOS_FS_IO_SLICE_BYTES) request = LEONOS_FS_IO_SLICE_BYTES;
        if ((uint64_t)request > available) request = (uint32_t)available;
        ret = storage_read_node(&in->node, position, buffer, request, &got);
        if (ret < 0) {
            result = total ? (int64_t)total : storage_errno(ret);
            break;
        }
        if (!got) break;
        if (out->node.type == LEONOS_FS_TYPE_FILE) {
            ret = storage_write_node(out->path, out->offset, buffer, got, &written);
            if (ret < 0) {
                result = total ? (int64_t)total : storage_errno(ret);
                break;
            }
            out->read_cursor.valid = 0;
            out->offset += written;
            if (written > 0 && out->node.first_cluster < 2) {
                struct storage_node updated;
                if (storage_lookup_path(out->path, &updated) == 0) out->node = updated;
            }
            if (written > 0 && out->offset > out->node.size) out->node.size = out->offset;
        } else if (out->flags & TASK_FILE_FLAG_PIPE) {
            ret = task_pipe_write(out, buffer, got);
            if (ret < 0) {
                result = total ? (int64_t)total : ret;
                break;
            }
            written = (uint32_t)ret;
        } else if (out->flags & TASK_FILE_FLAG_SOCKET_UNIX) {
            ret = task_socket_write(out, buffer, got);
            if (ret < 0) {
                result = total ? (int64_t)total : ret;
                break;
            }
            written = (uint32_t)ret;
        } else {
            ret = task_inet_write(out, buffer, got);
            if (ret < 0) {
                result = total ? (int64_t)total : ret;
                break;
            }
            written = (uint32_t)ret;
        }
        if (!written) break;
        if (written > got) written = got;
        position += written;
        total += written;
        if (written < got) break;
    }
    kernel_free(buffer);
    if (offset_arg) {
        *(int64_t *)(uintptr_t)offset_arg = (int64_t)position;
    } else {
        in->offset = position;
    }
    if (result) return result;
    return (int64_t)total;
}

/* Linux copy_file_range has independent optional offsets for both files.
 * The storage backends are synchronous, so each completed chunk is visible
 * before the syscall advances either offset. */
static int64_t syscall_copy_file_range(uint64_t in_fd_arg, uint64_t off_in_arg,
                                       uint64_t out_fd_arg, uint64_t off_out_arg,
                                       uint64_t len_arg, uint64_t flags_arg)
{
    struct task *task = sched_current_task();
    struct task_file *in;
    struct task_file *out;
    uint8_t *buffer;
    uint64_t in_position;
    uint64_t out_position;
    uint64_t total = 0;
    uint64_t len = len_arg > 0x7ffff000ULL ? 0x7ffff000ULL : len_arg;
    int64_t result = 0;

    if (!task) return -LEONOS_EPERM;
    if (flags_arg) return -LEONOS_EINVAL;
    in = task_file_for_fd(task, (int)in_fd_arg);
    out = task_file_for_fd(task, (int)out_fd_arg);
    if (!in || !out) return -LEONOS_EBADF;
    if (!file_can_read(in) || !file_can_write(out)) return -LEONOS_EBADF;
    if (in->node.type != LEONOS_FS_TYPE_FILE ||
        out->node.type != LEONOS_FS_TYPE_FILE ||
        (in->node.flags & STORAGE_NODE_FLAG_PROC) ||
        (out->node.flags & STORAGE_NODE_FLAG_PROC)) return -LEONOS_EINVAL;
    if (off_in_arg && (!user_range_ok(off_in_arg, sizeof(int64_t)) ||
                       !user_range_writable(off_in_arg, sizeof(int64_t)))) return -LEONOS_EFAULT;
    if (off_out_arg && (!user_range_ok(off_out_arg, sizeof(int64_t)) ||
                        !user_range_writable(off_out_arg, sizeof(int64_t)))) return -LEONOS_EFAULT;
    if (off_in_arg && *(const int64_t *)(uintptr_t)off_in_arg < 0) return -LEONOS_EINVAL;
    if (off_out_arg && *(const int64_t *)(uintptr_t)off_out_arg < 0) return -LEONOS_EINVAL;
    in_position = off_in_arg ? (uint64_t)*(const int64_t *)(uintptr_t)off_in_arg : in->offset;
    out_position = off_out_arg ? (uint64_t)*(const int64_t *)(uintptr_t)off_out_arg : out->offset;
    if (in->path[0] && storage_lookup_path(in->path, &in->node) < 0) return -LEONOS_EIO;
    if (out->path[0] && storage_lookup_path(out->path, &out->node) < 0) return -LEONOS_EIO;
    if (!len || in_position >= in->node.size) return 0;

    buffer = kernel_malloc(LEONOS_FS_IO_SLICE_BYTES);
    if (!buffer) return -LEONOS_ENOMEM;
    while (total < len && in_position < in->node.size) {
        uint64_t available = in->node.size - in_position;
        uint32_t request = (uint32_t)(len - total);
        uint32_t got = 0;
        uint32_t written = 0;
        int ret;
        if (request > LEONOS_FS_IO_SLICE_BYTES) request = LEONOS_FS_IO_SLICE_BYTES;
        if ((uint64_t)request > available) request = (uint32_t)available;
        ret = storage_read_node(&in->node, in_position, buffer, request, &got);
        if (ret < 0) {
            result = total ? (int64_t)total : storage_errno(ret);
            break;
        }
        if (!got) break;
        ret = storage_write_node(out->path, out_position, buffer, got, &written);
        if (ret < 0) {
            result = total ? (int64_t)total : storage_errno(ret);
            break;
        }
        if (!written) break;
        if (written > got) written = got;
        in_position += written;
        out_position += written;
        total += written;
        out->read_cursor.valid = 0;
        if (written < got) break;
        if (out->node.first_cluster < 2) {
            struct storage_node updated;
            if (storage_lookup_path(out->path, &updated) == 0) out->node = updated;
        } else if (out_position > out->node.size) {
            out->node.size = out_position;
        }
    }
    kernel_free(buffer);
    if (off_in_arg) *(int64_t *)(uintptr_t)off_in_arg = (int64_t)in_position;
    else in->offset = in_position;
    if (off_out_arg) *(int64_t *)(uintptr_t)off_out_arg = (int64_t)out_position;
    else out->offset = out_position;
    return result ? result : (int64_t)total;
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
    if (op == LEONOS_AUTHZ_READ || op == LEONOS_AUTHZ_WRITE || op == LEONOS_AUTHZ_EXEC) {
        uint32_t access = op == LEONOS_AUTHZ_READ ? FS_ACCESS_READ :
                          op == LEONOS_AUTHZ_WRITE ? FS_ACCESS_WRITE : FS_ACCESS_EXEC;
        return fs_permissions_check(task, path, access, false);
    }
    if (op == LEONOS_AUTHZ_DELETE) return fs_permissions_parent(task, path, true);
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
    ret = storage_resolve_path(sched_task_cwd(task), command->path, resolved, sizeof(resolved));
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
        (sched_task_fds(task)->closed_stdio_mask & (1u << (uint32_t)fd)) != 0) {
        return -LEONOS_EBADF;
    }
    if ((fd >= 0 && fd < 3) || task_pty_fd_for_fd(task, fd)) {
        st->type = LEONOS_FS_TYPE_DEVICE;
        st->reserved = 0;
        st->size = 0;
        return 0;
    }
    return -LEONOS_EBADF;
}

/**
 * @brief Park the caller for one scheduler tick and report a retryable EAGAIN.
 *
 * The int 0x80 epilogue rewinds the instruction, so the poll rescans its
 * descriptors after every tick until an event arrives or the deadline set in
 * poll_deadline_ticks expires. This gives poll its POSIX timeout without a
 * dedicated wait queue.
 */
static int64_t syscall_poll_park(struct task *task, int64_t timeout_ms)
{
    uint64_t now = time_ticks();
    if (!task->poll_deadline_ticks) {
        task->poll_deadline_ticks = timeout_ms < 0
                                        ? UINT64_MAX
                                        : now + (uint64_t)timeout_ms / 10u + 1u;
    } else if (now >= task->poll_deadline_ticks) {
        task->poll_deadline_ticks = 0;
        return 0;
    }
    return -LEONOS_EAGAIN;
}

static int64_t syscall_poll_impl(struct task *task, struct pollfd *fds,
                                 uint32_t nfds, int64_t timeout_ms)
{
    uint64_t ready = 0;
    timeout_ms = (int32_t)timeout_ms;

    if (!nfds) {
        /* poll(NULL, 0, timeout) is the classic millisecond sleep idiom. */
        if (timeout_ms != 0) return syscall_poll_park(task, timeout_ms);
        task->poll_deadline_ticks = 0;
        return 0;
    }
    if (timeout_ms != 0 && task->poll_deadline_ticks &&
        time_ticks() >= task->poll_deadline_ticks) {
        task->poll_deadline_ticks = 0;
        return 0;
    }
    for (uint32_t i = 0; i < nfds; ++i) {
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
        } else if (file && (file->flags & TASK_FILE_FLAG_EVENTFD)) {
            if ((events & (POLLIN | POLLRDNORM)) && file->aux) revents |= POLLIN | POLLRDNORM;
            if ((events & (POLLOUT | POLLWRNORM)) && file->aux < UINT64_MAX - 1ULL)
                revents |= POLLOUT | POLLWRNORM;
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
            /* Linux regular files are always ready: readiness is not tied to
             * the current offset or the descriptor's access mode. */
            if (file->node.type == LEONOS_FS_TYPE_FILE ||
                file->node.type == LEONOS_FS_TYPE_DIR) {
                revents |= events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);
            } else {
                if ((events & POLLIN) && file_can_read(file)) revents |= POLLIN;
                if ((events & POLLOUT) && file_can_write(file)) revents |= POLLOUT;
            }
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
    if (ready) {
        task->poll_deadline_ticks = 0;
        return (int64_t)ready;
    }
    if (timeout_ms == 0) {
        task->poll_deadline_ticks = 0;
        return 0;
    }
    return syscall_poll_park(task, timeout_ms);
}

int64_t syscall_poll(uint64_t fds_ptr, uint64_t count, int64_t timeout_ms)
{
    struct task *task = sched_current_task();
    uint32_t nfds = (uint32_t)count;
    if (!task || nfds > sched_task_limits(task)->nofile.rlim_cur) {
        return -LEONOS_EINVAL;
    }
    if (nfds && !user_range_writable(fds_ptr, (uint64_t)nfds * sizeof(struct pollfd))) {
        return -LEONOS_EFAULT;
    }
    return syscall_poll_impl(task, (struct pollfd *)(uintptr_t)fds_ptr, nfds, timeout_ms);
}

#define LINUX_SELECT_MAX_FDS 1024U

static bool syscall_select_bit(const uint64_t *set, uint32_t fd)
{
    return set && (set[fd >> 6] & (1ULL << (fd & 63u))) != 0;
}

static void syscall_rusage_cpu_time(uint64_t ticks, struct linux_timeval *out)
{
    uint64_t usec = (ticks * 1000000ULL) / NTCLKS_TICK_HZ;
    out->tv_sec = (int64_t)(usec / 1000000ULL);
    out->tv_usec = (int64_t)(usec % 1000000ULL);
}

static int64_t syscall_getrusage(uint64_t who_arg, uint64_t usage_ptr)
{
    struct task *task = sched_current_task();
    struct linux_rusage usage = {0};
    int32_t who = (int32_t)who_arg;
    if (!task || !usage_ptr || !user_range_writable(usage_ptr, sizeof(usage))) {
        return -LEONOS_EFAULT;
    }
    if (who != 0 && who != -1 && who != -2) return -LEONOS_EINVAL;
    if (who == 0 || who == -2) {
        syscall_rusage_cpu_time(task->cpu_ticks, &usage.ru_utime);
        usage.ru_maxrss = address_space_user_resident_kib(sched_task_as(task));
    }
    *(struct linux_rusage *)(uintptr_t)usage_ptr = usage;
    return 0;
}

static int64_t syscall_sysinfo(uint64_t info_ptr)
{
    struct linux_sysinfo info = {0};
    uint32_t task_count = 0;
    if (!info_ptr || !user_range_writable(info_ptr, sizeof(info))) return -LEONOS_EFAULT;
    info.uptime = (int64_t)(time_ticks() / NTCLKS_TICK_HZ);
    info.totalram = mm_total_memory_kib();
    info.freeram = mm_free_memory_kib();
    info.mem_unit = 1024;
    sched_task_counts(&task_count, NULL, NULL, NULL);
    info.procs = task_count > UINT16_MAX ? UINT16_MAX : (uint16_t)task_count;
    *(struct linux_sysinfo *)(uintptr_t)info_ptr = info;
    return 0;
}

static int64_t syscall_times(uint64_t tms_ptr)
{
    struct task *task = sched_current_task();
    if (!task) return -LEONOS_EFAULT;
    if (tms_ptr && !user_range_writable(tms_ptr, sizeof(struct linux_tms))) {
        return -LEONOS_EFAULT;
    }
    if (tms_ptr) {
        struct linux_tms value = {
            .tms_utime = (int64_t)task->cpu_ticks,
            .tms_stime = 0,
            .tms_cutime = 0,
            .tms_cstime = 0,
        };
        *(struct linux_tms *)(uintptr_t)tms_ptr = value;
    }
    return (int64_t)time_ticks();
}

/* Translate the Linux fd_set ABI to the kernel's poll implementation. The
 * scheduler already owns the blocking/retry path; select only needs the
 * fixed-width bitsets and native timeout conversion around it. */
static int64_t syscall_select_common(uint64_t nfds_arg, uint64_t read_ptr,
                                     uint64_t write_ptr, uint64_t except_ptr,
                                     uint64_t timeout_ptr, bool pselect_timeout)
{
    struct task *task = sched_current_task();
    uint64_t read_set[LINUX_SELECT_MAX_FDS / 64] = {0};
    uint64_t write_set[LINUX_SELECT_MAX_FDS / 64] = {0};
    uint64_t except_set[LINUX_SELECT_MAX_FDS / 64] = {0};
    struct pollfd *pollfds = NULL;
    int *fd_map = NULL;
    uint64_t nfds = nfds_arg;
    uint64_t bytes;
    uint32_t selected = 0;
    int64_t timeout_ms = -1;
    int64_t result;

    if (!task || nfds > LINUX_SELECT_MAX_FDS) return -LEONOS_EINVAL;
    bytes = ((nfds + 63) / 64) * sizeof(uint64_t);
    if (bytes) {
        if (read_ptr && !user_range_ok(read_ptr, bytes)) return -LEONOS_EFAULT;
        if (write_ptr && !user_range_ok(write_ptr, bytes)) return -LEONOS_EFAULT;
        if (except_ptr && !user_range_ok(except_ptr, bytes)) return -LEONOS_EFAULT;
        if (read_ptr && !user_range_writable(read_ptr, bytes)) return -LEONOS_EFAULT;
        if (write_ptr && !user_range_writable(write_ptr, bytes)) return -LEONOS_EFAULT;
        if (except_ptr && !user_range_writable(except_ptr, bytes)) return -LEONOS_EFAULT;
        if (read_ptr) __builtin_memcpy(read_set, (const void *)(uintptr_t)read_ptr, bytes);
        if (write_ptr) __builtin_memcpy(write_set, (const void *)(uintptr_t)write_ptr, bytes);
        if (except_ptr) __builtin_memcpy(except_set, (const void *)(uintptr_t)except_ptr, bytes);
    }
    if (timeout_ptr) {
        if (pselect_timeout) {
            struct linux_timespec timeout;
            if (!user_range_ok(timeout_ptr, sizeof(timeout))) return -LEONOS_EFAULT;
            timeout = *(const struct linux_timespec *)(uintptr_t)timeout_ptr;
            if (timeout.tv_sec < 0 || timeout.tv_nsec < 0 || timeout.tv_nsec >= 1000000000) {
                return -LEONOS_EINVAL;
            }
            timeout_ms = timeout.tv_sec > 2147482
                            ? 2147483647
                            : timeout.tv_sec * 1000 + (timeout.tv_nsec + 999999) / 1000000;
        } else {
            struct linux_timeval timeout;
            if (!user_range_ok(timeout_ptr, sizeof(timeout)) ||
                !user_range_writable(timeout_ptr, sizeof(timeout))) return -LEONOS_EFAULT;
            timeout = *(const struct linux_timeval *)(uintptr_t)timeout_ptr;
            if (timeout.tv_sec < 0 || timeout.tv_usec < 0 || timeout.tv_usec >= 1000000) {
                return -LEONOS_EINVAL;
            }
            timeout_ms = timeout.tv_sec > 2147482
                            ? 2147483647
                            : timeout.tv_sec * 1000 + (timeout.tv_usec + 999) / 1000;
        }
    }
    for (uint32_t fd = 0; fd < (uint32_t)nfds; ++fd) {
        short events = 0;
        if (syscall_select_bit(read_set, fd)) events |= POLLIN | POLLRDNORM;
        if (syscall_select_bit(write_set, fd)) events |= POLLOUT | POLLWRNORM;
        if (syscall_select_bit(except_set, fd)) events |= POLLPRI;
        if (events) ++selected;
    }
    if (selected) {
        pollfds = (struct pollfd *)kernel_malloc((size_t)selected * sizeof(*pollfds));
        fd_map = (int *)kernel_malloc((size_t)selected * sizeof(*fd_map));
        if (!pollfds || !fd_map) {
            kernel_free(pollfds);
            kernel_free(fd_map);
            return -LEONOS_ENOMEM;
        }
        uint32_t at = 0;
        for (uint32_t fd = 0; fd < (uint32_t)nfds; ++fd) {
            short events = 0;
            if (syscall_select_bit(read_set, fd)) events |= POLLIN | POLLRDNORM;
            if (syscall_select_bit(write_set, fd)) events |= POLLOUT | POLLWRNORM;
            if (syscall_select_bit(except_set, fd)) events |= POLLPRI;
            if (!events) continue;
            pollfds[at] = (struct pollfd){(int32_t)fd, events, 0};
            fd_map[at++] = (int)fd;
        }
    }
    result = syscall_poll_impl(task, pollfds, selected, timeout_ms);
    if (result < 0) goto select_out;
    if (read_ptr && bytes) __builtin_memset((void *)(uintptr_t)read_ptr, 0, bytes);
    if (write_ptr && bytes) __builtin_memset((void *)(uintptr_t)write_ptr, 0, bytes);
    if (except_ptr && bytes) __builtin_memset((void *)(uintptr_t)except_ptr, 0, bytes);
    for (uint32_t i = 0; i < selected; ++i) {
        short revents = pollfds[i].revents;
        uint32_t fd = (uint32_t)fd_map[i];
        if (revents & POLLNVAL) {
            result = -LEONOS_EBADF;
            goto select_out;
        }
        if (read_ptr && (revents & (POLLIN | POLLRDNORM | POLLHUP | POLLERR)))
            ((uint64_t *)(uintptr_t)read_ptr)[fd >> 6] |= 1ULL << (fd & 63u);
        if (write_ptr && (revents & (POLLOUT | POLLWRNORM | POLLHUP | POLLERR)))
            ((uint64_t *)(uintptr_t)write_ptr)[fd >> 6] |= 1ULL << (fd & 63u);
        if (except_ptr && (revents & POLLPRI))
            ((uint64_t *)(uintptr_t)except_ptr)[fd >> 6] |= 1ULL << (fd & 63u);
    }
    if (!pselect_timeout && timeout_ptr && result == 0) {
        struct linux_timeval zero = {0, 0};
        *(struct linux_timeval *)(uintptr_t)timeout_ptr = zero;
    }
select_out:
    kernel_free(pollfds);
    kernel_free(fd_map);
    return result;
}

/* ppoll shares poll's descriptor ABI but carries a nanosecond timeout.  The
 * scheduler currently has no atomic signal-mask handoff for a parked poll;
 * reject a non-null mask instead of silently widening the interrupt window. */
static int64_t syscall_ppoll(uint64_t fds_ptr, uint64_t count, uint64_t timeout_ptr,
                             uint64_t signal_mask_ptr, uint64_t signal_mask_size)
{
    int64_t timeout_ms = -1;
    if (signal_mask_ptr) {
        if (signal_mask_size != sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (!user_range_ok(signal_mask_ptr, sizeof(uint64_t))) return -LEONOS_EFAULT;
        return -LEONOS_EOPNOTSUPP;
    }
    if (signal_mask_size != 0 && signal_mask_size != sizeof(uint64_t)) {
        return -LEONOS_EINVAL;
    }
    if (timeout_ptr) {
        struct linux_timespec timeout;
        if (!user_range_ok(timeout_ptr, sizeof(timeout))) return -LEONOS_EFAULT;
        timeout = *(const struct linux_timespec *)(uintptr_t)timeout_ptr;
        if (timeout.tv_sec < 0 || timeout.tv_nsec < 0 || timeout.tv_nsec >= 1000000000) {
            return -LEONOS_EINVAL;
        }
        if (timeout.tv_sec > 2147482) {
            timeout_ms = 2147483647;
        } else {
            timeout_ms = timeout.tv_sec * 1000 + (timeout.tv_nsec + 999999) / 1000000;
            if (timeout_ms > 2147483647) timeout_ms = 2147483647;
        }
    }
    return syscall_poll(fds_ptr, count, timeout_ms);
}

/**
 * @brief Implements the validated Linux openat2 argument block and delegates
 * the supported no-resolve path to the existing openat object allocator.
 * @param dirfd Directory descriptor used for relative resolution.
 * @param pathname User pathname pointer.
 * @param how_ptr User open_how pointer.
 * @param size Size of the user open_how block.
 * @return New descriptor or negative errno.
 */
static int64_t syscall_openat2(uint64_t dirfd, uint64_t pathname,
                               uint64_t how_ptr, uint64_t size)
{
    struct open_how how;
    const uint64_t supported_flags = LINUX_O_RDONLY | LINUX_O_WRONLY | LINUX_O_RDWR |
        LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_NOCTTY | LINUX_O_TRUNC |
        LINUX_O_APPEND | LINUX_O_NONBLOCK | LINUX_O_CLOEXEC | LINUX_O_DIRECTORY;
    const uint64_t valid_flags = supported_flags | LINUX_O_DSYNC | LINUX_O_SYNC |
        LINUX_O_ASYNC | LINUX_O_DIRECT | LINUX_O_LARGEFILE | LINUX_O_NOFOLLOW |
        LINUX_O_NOATIME | LINUX_O_PATH | LINUX_O_TMPFILE;
    const uint64_t valid_resolve = RESOLVE_NO_XDEV | RESOLVE_NO_MAGICLINKS |
        RESOLVE_NO_SYMLINKS | RESOLVE_BENEATH | RESOLVE_IN_ROOT | RESOLVE_CACHED;
    if (size < sizeof(how)) return -LEONOS_EINVAL;
    if (size > 4096u) return -LEONOS_E2BIG;
    if (!user_range_ok(how_ptr, size)) return -LEONOS_EFAULT;
    __builtin_memcpy(&how, (const void *)(uintptr_t)how_ptr, sizeof(how));
    for (uint64_t offset = sizeof(how); offset < size; ++offset) {
        if (((const uint8_t *)(uintptr_t)how_ptr)[offset] != 0) return -LEONOS_E2BIG;
    }
    /* Linux strips __FMODE_NONOTIFY before checking valid open flags. */
    how.flags &= ~0x04000000ULL;
    if (how.flags & ~valid_flags) return -LEONOS_EINVAL;
    if (how.resolve & ~valid_resolve) return -LEONOS_EINVAL;
    if ((how.resolve & (RESOLVE_BENEATH | RESOLVE_IN_ROOT)) ==
        (RESOLVE_BENEATH | RESOLVE_IN_ROOT)) return -LEONOS_EINVAL;
    bool tmpfile = (how.flags & (LINUX_O_TMPFILE & ~LINUX_O_DIRECTORY)) != 0;
    if (how.mode && !(how.flags & LINUX_O_CREAT) && !tmpfile) return -LEONOS_EINVAL;
    if (how.mode & ~07777ULL) return -LEONOS_EINVAL;
    if ((how.flags & (LINUX_O_CREAT | LINUX_O_DIRECTORY)) ==
        (LINUX_O_CREAT | LINUX_O_DIRECTORY)) return -LEONOS_EINVAL;
    if (tmpfile && (!(how.flags & LINUX_O_DIRECTORY) ||
                   !(how.flags & (LINUX_O_WRONLY | LINUX_O_RDWR)))) return -LEONOS_EINVAL;
    if ((how.flags & LINUX_O_PATH) && (how.flags &
        ~(uint64_t)(LINUX_O_PATH | LINUX_O_CLOEXEC | LINUX_O_DIRECTORY | LINUX_O_NOFOLLOW)))
        return -LEONOS_EINVAL;
    if (how.flags & ~supported_flags) return -LEONOS_EOPNOTSUPP;
    if (how.resolve) return -LEONOS_EOPNOTSUPP;
    return syscall_dispatch_regs_legacy(LINUX_SYS_OPENAT, dirfd, pathname,
                                         (uint32_t)how.flags, (uint32_t)how.mode,
                                         0, 0);
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
    case LINUX_SYS_OPENAT2:
    case LINUX_SYS_CLOSE:
    case LINUX_SYS_STAT:
    case LINUX_SYS_FSTAT:
    case LINUX_SYS_STATX:
    case LINUX_SYS_SENDFILE:
    case LINUX_SYS_COPY_FILE_RANGE:
    case LINUX_SYS_EVENTFD:
    case LINUX_SYS_EVENTFD2:
    case LINUX_SYS_MEMFD_CREATE:
    case LINUX_SYS_LSEEK:
    case LINUX_SYS_FTRUNCATE:
    case LINUX_SYS_BRK:
    case LINUX_SYS_MSYNC:
    case LINUX_SYS_MINCORE:
    case LINUX_SYS_MADVISE:
    case LINUX_SYS_FADVISE64:
    case LINUX_SYS_READAHEAD:
    case LINUX_SYS_FALLOCATE:
    case LINUX_SYS_SYNC_FILE_RANGE:
    case LINUX_SYS_PREADV:
    case LINUX_SYS_PWRITEV:
    case LINUX_SYS_PREADV2:
    case LINUX_SYS_PWRITEV2:
    case LINUX_SYS_SYNC:
    case LINUX_SYS_SYNCFS:
    case LINUX_SYS_IOCTL:
    case LINUX_SYS_SELECT:
    case LINUX_SYS_PSELECT6:
    case LINUX_SYS_GETRUSAGE:
    case LINUX_SYS_SYSINFO:
    case LINUX_SYS_TIMES:
    case LINUX_SYS_POLL:
    case LINUX_SYS_PPOLL:
    case LINUX_SYS_SCHED_YIELD:
    case LINUX_SYS_NANOSLEEP:
    case LINUX_SYS_ALARM:
    case LINUX_SYS_GETITIMER:
    case LINUX_SYS_SETITIMER:
    case LINUX_SYS_EXECVE:
    case LINUX_SYS_EXIT:
    case LINUX_SYS_EXIT_GROUP:
    case LINUX_SYS_WAIT4:
    case LINUX_SYS_WAITID:
    case LINUX_SYS_CHMOD:
    case LINUX_SYS_FCHMOD:
    case LINUX_SYS_CHOWN:
    case LINUX_SYS_FCHOWN:
    case LINUX_SYS_GETCWD:
    case LINUX_SYS_CHDIR:
    case LINUX_SYS_RENAME:
    case LINUX_SYS_RENAMEAT:
    case LINUX_SYS_RENAMEAT2:
    case LINUX_SYS_CREAT:
    case LINUX_SYS_GETDENTS:
    case LINUX_SYS_MKDIR:
    case LINUX_SYS_RMDIR:
    case LINUX_SYS_UNLINK:
    case LINUX_SYS_LINK:
    case LINUX_SYS_SYMLINK:
    case LINUX_SYS_READLINK:
    case LINUX_SYS_LINKAT:
    case LINUX_SYS_SYMLINKAT:
    case LINUX_SYS_READLINKAT:
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
    case LINUX_SYS_GETPEERNAME:
    case LINUX_SYS_GETSOCKOPT:
    case LINUX_SYS_SETSOCKOPT:
    case LINUX_SYS_SHUTDOWN:
    case LINUX_SYS_SEND:
    case LINUX_SYS_RECV:
    case LINUX_SYS_RT_SIGACTION:
    case LINUX_SYS_RT_SIGPROCMASK:
    case LINUX_SYS_RT_SIGSUSPEND:
    case LINUX_SYS_RT_SIGPENDING:
    case LINUX_SYS_SIGALTSTACK:
    case LINUX_SYS_FUTEX:
    case LINUX_SYS_FUTEX_WAKE:
    case LINUX_SYS_FUTEX_WAIT:
    case LINUX_SYS_FUTEX_REQUEUE:
    case LINUX_SYS_CLOCK_NANOSLEEP:
    case LINUX_SYS_GETRANDOM:
    case LINUX_SYS_TIME:
    case LINUX_SYS_GETCPU:
    case LINUX_SYS_CLOSE_RANGE:
        return syscall_dispatch_regs(frame->number, frame->args[0], frame->args[1],
                                     frame->args[2], frame->args[3], frame->args[4],
                                     frame->args[5]);
    case LINUX_SYS_CLOCK_GETRES:
    case LINUX_SYS_GETTID:
    case LINUX_SYS_SET_TID_ADDRESS:
    case LINUX_SYS_SET_ROBUST_LIST:
    case LINUX_SYS_GET_ROBUST_LIST:
    case LINUX_SYS_ARCH_PRCTL:
        return syscall_dispatch_regs(frame->number, frame->args[0], frame->args[1],
                                     frame->args[2], frame->args[3], frame->args[4],
                                     frame->args[5]);
    case LINUX_SYS_PAUSE:
        return syscall_dispatch_regs(frame->number, 0, 0, 0, 0, 0, 0);
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
    case LINUX_SYS_SETREUID:
    case LINUX_SYS_SETREGID:
    case LINUX_SYS_SETRESUID:
    case LINUX_SYS_GETRESUID:
    case LINUX_SYS_SETRESGID:
    case LINUX_SYS_GETRESGID:
    case LINUX_SYS_SETFSUID:
    case LINUX_SYS_SETFSGID:
    case LINUX_SYS_SCHED_GET_PRIORITY_MAX:
    case LINUX_SYS_SCHED_GET_PRIORITY_MIN:
    case LINUX_SYS_SCHED_GETPARAM:
    case LINUX_SYS_SCHED_GETSCHEDULER:
    case LINUX_SYS_SCHED_SETPARAM:
    case LINUX_SYS_SCHED_SETSCHEDULER:
    case LINUX_SYS_SCHED_RR_GET_INTERVAL:
    case LINUX_SYS_PERSONALITY:
    case LINUX_SYS_PRCTL:
    case LINUX_SYS_UNAME:
    case LINUX_SYS_SETHOSTNAME:
    case LINUX_SYS_SETDOMAINNAME:
    case LINUX_SYS_MEMBARRIER:
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
    case LINUX_SYS_GETSID:
    case LINUX_SYS_KILL:
    case LINUX_SYS_TKILL:
    case LINUX_SYS_TGKILL:
    case LEONOS_SYS_NICE:
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
    case LINUX_SYS_MREMAP:
        return syscall_mm_mremap(frame->args[0], frame->args[1], frame->args[2],
                                 frame->args[3], frame->args[4]);
    case LINUX_SYS_MLOCK:
    case LINUX_SYS_MLOCK2:
        return syscall_mm_mlock(frame->args[0], frame->args[1],
                                frame->number == LINUX_SYS_MLOCK2 ? frame->args[2] : 0);
    case LINUX_SYS_MUNLOCK:
        return syscall_mm_munlock(frame->args[0], frame->args[1]);
    case LINUX_SYS_MLOCKALL:
        return syscall_mm_mlockall(frame->args[0]);
    case LINUX_SYS_MUNLOCKALL:
        return syscall_mm_munlockall();
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
    if (number == LINUX_SYS_OPENAT2) {
        return syscall_openat2(a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_SOCKET || number == LINUX_SYS_CONNECT ||
        number == LINUX_SYS_ACCEPT || number == LINUX_SYS_ACCEPT4 ||
        number == LINUX_SYS_SOCKETPAIR || number == LINUX_SYS_BIND ||
        number == LINUX_SYS_LISTEN || number == LINUX_SYS_GETSOCKNAME || number == LINUX_SYS_GETPEERNAME ||
        number == LINUX_SYS_GETSOCKOPT || number == LINUX_SYS_SETSOCKOPT ||
        number == LINUX_SYS_SHUTDOWN || number == LINUX_SYS_SEND ||
        number == LINUX_SYS_RECV || number == LINUX_SYS_SENDTO ||
        number == LINUX_SYS_RECVFROM || number == LINUX_SYS_SENDMSG ||
        number == LINUX_SYS_RECVMSG) {
        return syscall_socket_dispatch(number, a0, a1, a2, a3, a4, a5);
    }
    if (number == LINUX_SYS_GETUID || number == LINUX_SYS_GETGID ||
        number == LINUX_SYS_GETEUID || number == LINUX_SYS_GETEGID ||
        number == LINUX_SYS_SETUID || number == LINUX_SYS_SETGID ||
        number == LINUX_SYS_SETREUID || number == LINUX_SYS_SETREGID ||
        number == LINUX_SYS_SETRESUID || number == LINUX_SYS_GETRESUID ||
        number == LINUX_SYS_SETRESGID || number == LINUX_SYS_GETRESGID ||
        number == LINUX_SYS_SETFSUID || number == LINUX_SYS_SETFSGID ||
        number == LINUX_SYS_SCHED_GET_PRIORITY_MAX ||
        number == LINUX_SYS_SCHED_GET_PRIORITY_MIN ||
        number == LINUX_SYS_SCHED_GETPARAM ||
        number == LINUX_SYS_SCHED_GETSCHEDULER ||
        number == LINUX_SYS_SCHED_SETPARAM ||
        number == LINUX_SYS_SCHED_SETSCHEDULER ||
        number == LINUX_SYS_SCHED_RR_GET_INTERVAL ||
        number == LINUX_SYS_PERSONALITY || number == LINUX_SYS_PRCTL ||
        number == LINUX_SYS_UNAME || number == LINUX_SYS_SETHOSTNAME ||
        number == LINUX_SYS_SETDOMAINNAME || number == LINUX_SYS_MEMBARRIER ||
        number == LINUX_SYS_GETTIMEOFDAY ||
        number == LINUX_SYS_CLOCK_SETTIME ||
        number == LINUX_SYS_SETTIMEOFDAY || number == LINUX_SYS_CLOCK_GETTIME ||
        number == LINUX_SYS_SCHED_SETAFFINITY ||
        number == LINUX_SYS_SCHED_GETAFFINITY || number == LINUX_SYS_REBOOT) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_RT_SIGACTION || number == LINUX_SYS_RT_SIGPROCMASK ||
        number == LINUX_SYS_RT_SIGSUSPEND || number == LINUX_SYS_RT_SIGPENDING ||
        number == LINUX_SYS_SIGALTSTACK) {
        return syscall_linux_signal(number, a0, a1, a2, a3, a4);
    }
    if (number == LINUX_SYS_CLOCK_GETRES || number == LINUX_SYS_GETTID ||
        number == LINUX_SYS_SET_TID_ADDRESS || number == LINUX_SYS_ARCH_PRCTL ||
        number == LINUX_SYS_SET_ROBUST_LIST || number == LINUX_SYS_GET_ROBUST_LIST) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_PAUSE) {
        sched_block_current();
        return -LEONOS_EINTR;
    }
    if (number == LINUX_SYS_EVENTFD || number == LINUX_SYS_EVENTFD2) {
        return syscall_eventfd_create(a0, number == LINUX_SYS_EVENTFD2 ? (uint32_t)a1 : 0);
    }
    if (number == LINUX_SYS_MEMFD_CREATE) return syscall_memfd_create(a0, (uint32_t)a1);
    if (number == LINUX_SYS_POLL) {
        return syscall_poll(a0, a1, (int64_t)a2);
    }
    if (number == LINUX_SYS_GETRUSAGE) {
        return syscall_getrusage(a0, a1);
    }
    if (number == LINUX_SYS_SYSINFO) {
        return syscall_sysinfo(a0);
    }
    if (number == LINUX_SYS_TIMES) {
        return syscall_times(a0);
    }
    if (number == LINUX_SYS_SELECT) {
        return syscall_select_common(a0, a1, a2, a3, a4, false);
    }
    if (number == LINUX_SYS_PSELECT6) {
        uint64_t mask_ptr = 0;
        uint64_t mask_len = 0;
        if (a5) {
            if (!user_range_ok(a5, sizeof(mask_ptr) + sizeof(mask_len))) return -LEONOS_EFAULT;
            __builtin_memcpy(&mask_ptr, (const void *)(uintptr_t)a5, sizeof(mask_ptr));
            __builtin_memcpy(&mask_len, (const void *)(uintptr_t)(a5 + sizeof(mask_ptr)), sizeof(mask_len));
            if (mask_ptr || mask_len != sizeof(uint64_t)) return -LEONOS_EOPNOTSUPP;
        }
        return syscall_select_common(a0, a1, a2, a3, a4, true);
    }
    if (number == LINUX_SYS_PPOLL) {
        return syscall_ppoll(a0, a1, a2, a3, a4);
    }
    if (number == LINUX_SYS_MSYNC) {
        return syscall_mm_msync(a0, a1, a2);
    }
    if (number == LINUX_SYS_MINCORE) {
        return syscall_mm_mincore(a0, a1, a2);
    }
    if (number == LINUX_SYS_MADVISE) {
        return syscall_mm_madvise(a0, a1, a2);
    }
    if (number == LINUX_SYS_FADVISE64) {
        struct task *task = sched_current_task();
        if ((int64_t)a1 < 0 || (int64_t)a2 < 0) return -LEONOS_EINVAL;
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (a3 > 5) return -LEONOS_EINVAL;
        return 0;
    }
    if (number == LINUX_SYS_READAHEAD) {
        struct task_file *file = task_file_for_fd(sched_current_task(), (int)a0);
        if (!file) return -LEONOS_EBADF;
        if ((int64_t)a1 < 0) return -LEONOS_EINVAL;
        if (file->node.type != LEONOS_FS_TYPE_FILE &&
            !(file->flags & TASK_FILE_FLAG_DEV_BLOCK)) return -LINUX_ESPIPE;
        return 0;
    }
    if (number == LINUX_SYS_FALLOCATE) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        uint64_t end;
        if (!file) return -LEONOS_EBADF;
        if (a1 != 0) return -LEONOS_EOPNOTSUPP;
        if ((int64_t)a2 < 0 || (int64_t)a3 < 0 || !a3 || a2 > UINT64_MAX - a3) {
            return -LEONOS_EINVAL;
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE || !file_can_write(file) || !file->path[0]) {
            return -LEONOS_EBADF;
        }
        end = a2 + a3;
        if (end <= file->node.size) return 0;
        /* Storage backends have no sparse/preallocation primitive. Extending
         * with mode zero uses the existing truncate zero-fill contract. */
        int ret = storage_truncate_file(file->path, end);
        if (ret < 0) return ret;
        if (storage_lookup_path(file->path, &file->node) < 0) return -LEONOS_EIO;
        file->read_cursor.valid = 0;
        return 0;
    }
    if (number == LINUX_SYS_SYNC_FILE_RANGE) {
        struct task_file *file = task_file_for_fd(sched_current_task(), (int)a0);
        if (!file) return -LEONOS_EBADF;
        if ((int64_t)a1 < 0 || (int64_t)a2 < 0 || (a3 & ~7u)) {
            return -LEONOS_EINVAL;
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE &&
            !(file->flags & TASK_FILE_FLAG_DEV_BLOCK)) {
            return -LEONOS_EINVAL;
        }
        /* Storage writes are synchronous before write(2) returns. Valid
         * Linux wait/write flags therefore complete here without a second
         * backend operation; the descriptor, range and flag contract still
         * applies at the syscall boundary. */
        return 0;
    }
    if (number == LINUX_SYS_SYNC) {
        return 0;
    }
    if (number == LINUX_SYS_SYNCFS) {
        return task_file_for_fd(sched_current_task(), (int)a0) ? 0 : -LEONOS_EBADF;
    }
    if (number == LINUX_SYS_FCHMODAT || number == LINUX_SYS_FCHMODAT2 ||
        number == LINUX_SYS_FCHOWNAT || number == LINUX_SYS_FACCESSAT ||
        number == LINUX_SYS_FACCESSAT2) {
        struct task *task = sched_current_task();
        struct task_file *empty_file;
        char path[LEONOS_FS_PATH_LEN];
        uint32_t flags = number == LINUX_SYS_FCHOWNAT ? (uint32_t)a4 :
                         (number == LINUX_SYS_FCHMODAT2 || number == LINUX_SYS_FACCESSAT2) ? (uint32_t)a3 : 0;
        uint32_t supported = LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EMPTY_PATH;
        if (number == LINUX_SYS_FACCESSAT2) supported |= LINUX_AT_EACCESS;
        if (flags & ~supported) return -LEONOS_EINVAL;
        bool real_ids = (number == LINUX_SYS_FACCESSAT || number == LINUX_SYS_FACCESSAT2) &&
                        !(flags & LINUX_AT_EACCESS);
        int ret = resolve_user_path_at(task, (int32_t)a0, a1, flags & LINUX_AT_EMPTY_PATH, path, &empty_file, real_ids);
        if (ret < 0) return ret;
        const struct storage_node *node = empty_file ? &empty_file->node : NULL;
        if (number == LINUX_SYS_FCHOWNAT) return fs_permissions_chown(task, path, node, (uint32_t)a2, (uint32_t)a3);
        if (number == LINUX_SYS_FCHMODAT || number == LINUX_SYS_FCHMODAT2)
            return fs_permissions_chmod(task, path, node, (uint32_t)a2);
        if (a2 & ~7u) return -LEONOS_EINVAL;
        return fs_permissions_check(task, path, (uint32_t)a2, !(flags & LINUX_AT_EACCESS));
    }
    if (number == LINUX_SYS_GETGROUPS || number == LINUX_SYS_SETGROUPS) {
        struct task *task = sched_current_task();
        int32_t size = (int32_t)a0;
        if (!task) return -LEONOS_EPERM;
        if (number == LINUX_SYS_SETGROUPS) {
            if (task->euid) return -LEONOS_EPERM;
            if (size < 0 || size > 65536) return -LEONOS_EINVAL;
            if (size && !user_range_ok(a1, (uint64_t)size * 4)) return -LEONOS_EFAULT;
            return task_groups_set(task, (const uint32_t *)(uintptr_t)a1, (uint32_t)size);
        }
        uint32_t count = task->groups ? task->groups->count : 0;
        if (size < 0) return -LEONOS_EINVAL;
        if (!size) return count;
        if ((uint32_t)size < count) return -LEONOS_EINVAL;
        if (count && !user_range_writable(a1, (uint64_t)count * 4)) return -LEONOS_EFAULT;
        for (uint32_t i = 0; i < count; ++i) ((uint32_t *)(uintptr_t)a1)[i] = task->groups->ids[i];
        return count;
    }
    if (number == LINUX_SYS_CHMOD || number == LINUX_SYS_CHOWN || number == LINUX_SYS_LCHOWN) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) return ret;
        return number == LINUX_SYS_CHMOD
                   ? fs_permissions_chmod(task, path, NULL, (uint32_t)a1)
                   : fs_permissions_chown(task, path, NULL, (uint32_t)a1, (uint32_t)a2);
    }
    if (number == LINUX_SYS_FCHMOD || number == LINUX_SYS_FCHOWN) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file || !file->path[0]) return -LEONOS_EBADF;
        return number == LINUX_SYS_FCHMOD
                   ? fs_permissions_chmod(task, file->path, &file->node, (uint32_t)a1)
                   : fs_permissions_chown(task, file->path, &file->node, (uint32_t)a1, (uint32_t)a2);
    }

    if (number == LINUX_SYS_WRITE) {
        struct task *task = sched_current_task();
        uint32_t request_len;
        int pty_stream;
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        {
            struct task_pty_fd *endpoint = task->syscall_file ? NULL : task_pty_endpoint_for_fd(task, (int)a0);
            if (endpoint) {
                uint32_t request = a2 > LEONOS_FS_IO_SLICE_BYTES
                                       ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
                if (endpoint->endpoint == TASK_PTY_ENDPOINT_MASTER) {
                    return pty_write_input(0, endpoint->pty_id,
                                           (const char *)(uintptr_t)a1, request);
                }
                return pty_write_output(endpoint->pty_id,
                                        (const char *)(uintptr_t)a1, request);
            }
        }
        {
            struct task_file *pipe_file = task_file_for_io(task, (int)a0);
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_PIPE)) {
                uint32_t request = a2 > TASK_PIPE_CAP ? TASK_PIPE_CAP : (uint32_t)a2;
                return task_pipe_write(pipe_file, (const void *)(uintptr_t)a1, request);
            }
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) {
                uint32_t request = a2 > 0x7ffff000 ? 0x7ffff000 : (uint32_t)a2;
                if (!user_range_ok(a1, request)) return -LEONOS_EFAULT;
                int ret = task_socket_write(pipe_file, (const void *)(uintptr_t)a1, request);
                if (ret == -LEONOS_EPIPE) sched_signal_user_task(task->pid, 13);
                return ret;
            }
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_SOCKET_INET)) {
                uint32_t request = a2 > LEONOS_FS_IO_SLICE_BYTES ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
                if (!user_range_ok(a1, request)) return -LEONOS_EFAULT;
                return task_inet_write(pipe_file, (const void *)(uintptr_t)a1, request);
            }
        }
        pty_stream = task->syscall_file ? -1 : task_pty_stream_for_fd(task, (int)a0);
        if (pty_stream == 1 || pty_stream == 2) {
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES
                              : (uint32_t)a2;
            return pty_write_output(task->pty_id, (const char *)(uintptr_t)a1, request_len);
        }
        /**
 * @brief A redirected stdio descriptor must be handled by the regular file path below. Only an unbound implicit PTY stream falls back to the kernel console; otherwise a pipe on fd 1/2 would be silently bypassed and its consumer would receive EOF/zero bytes.
 */
        if ((a0 == 1 || a0 == 2) &&
            (sched_task_fds(task)->closed_stdio_mask & (1u << (uint32_t)a0)) == 0 &&
            !task_file_for_io(task, (int)a0)) {
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES
                              : (uint32_t)a2;
            console_write_len((const char *)(uintptr_t)a1, (size_t)request_len);
            return (int64_t)request_len;
        }
        struct task_file *file = task_file_for_io(task, (int)a0);
        uint32_t wrote = 0;
        int ret;
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->flags & TASK_FILE_FLAG_EVENTFD) {
            uint64_t value;
            if (a2 != sizeof(uint64_t)) return -LEONOS_EINVAL;
            value = *(const uint64_t *)(uintptr_t)a1;
            if (value == UINT64_MAX) return -LEONOS_EINVAL;
            if (UINT64_MAX - 1ULL - file->aux < value) return -LEONOS_EAGAIN;
            file->aux += value;
            return sizeof(uint64_t);
        }
        if (file->flags & TASK_FILE_FLAG_DEV_SHM) {
            if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
            return task_shm_write(file, (const void *)(uintptr_t)a1, (uint32_t)a2);
        }
        if (file->flags & TASK_FILE_FLAG_DEV_NODE) {
            if (!file_can_write(file)) return -LEONOS_EBADF;
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
            {
                int result = task_device_write(task, file, (const void *)(uintptr_t)a1,
                                               request_len);
                if (result > 0 && (file->flags & TASK_FILE_FLAG_DEV_BLOCK)) {
                    file->offset += (uint32_t)result;
                }
                return result;
            }
        }
        if (file->flags & TASK_FILE_FLAG_DEV_NULL) {
            if (!file_can_write(file)) {
                return -LEONOS_EBADF;
            }
            return (int64_t)a2;
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
        if (!user_range_writable(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        {
            struct task_pty_fd *endpoint = task->syscall_file ? NULL : task_pty_endpoint_for_fd(task, (int)a0);
            if (endpoint) {
                uint32_t request = a2 > LEONOS_FS_IO_SLICE_BYTES
                                       ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
                if (endpoint->endpoint == TASK_PTY_ENDPOINT_MASTER) {
                    if ((endpoint->status_flags & LEONOS_O_NONBLOCK) &&
                        !pty_output_available(endpoint->pty_id) &&
                        !pty_is_hungup(endpoint->pty_id)) {
                        return -LEONOS_EAGAIN;
                    }
                    return pty_read_output(0, endpoint->pty_id,
                                           (char *)(uintptr_t)a1, request);
                }
                if ((endpoint->status_flags & LEONOS_O_NONBLOCK) &&
                    !pty_input_available(endpoint->pty_id) &&
                    !pty_is_hungup(endpoint->pty_id)) {
                    return -LEONOS_EAGAIN;
                }
                return pty_read_input(endpoint->pty_id,
                                      (char *)(uintptr_t)a1, request);
            }
        }
        {
            struct task_file *pipe_file = task_file_for_io(task, (int)a0);
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_PIPE)) {
                uint32_t request = a2 > TASK_PIPE_CAP ? TASK_PIPE_CAP : (uint32_t)a2;
                return task_pipe_read(pipe_file, (void *)(uintptr_t)a1, request);
            }
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) {
                uint32_t request = a2 > LEONOS_FS_IO_SLICE_BYTES ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
                if (!user_range_ok(a1, request)) return -LEONOS_EFAULT;
                return task_socket_read(pipe_file, (void *)(uintptr_t)a1, request);
            }
            if (pipe_file && (pipe_file->flags & TASK_FILE_FLAG_SOCKET_INET)) {
                uint32_t request = a2 > LEONOS_FS_IO_SLICE_BYTES ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
                if (!user_range_ok(a1, request)) return -LEONOS_EFAULT;
                return task_inet_read(pipe_file, (void *)(uintptr_t)a1, request);
            }
        }
        pty_stream = task->syscall_file ? -1 : task_pty_stream_for_fd(task, (int)a0);
        if (pty_stream == 0) {
            return pty_read_input(task->pty_id, (char *)(uintptr_t)a1, (uint32_t)a2);
        }
        file = task_file_for_io(task, (int)a0);
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->flags & TASK_FILE_FLAG_EVENTFD) {
            uint64_t value;
            if (a2 != sizeof(uint64_t)) return -LEONOS_EINVAL;
            if (!file->aux) return -LEONOS_EAGAIN;
            value = file->aux2 ? 1 : file->aux;
            file->aux -= value;
            *(uint64_t *)(uintptr_t)a1 = value;
            return sizeof(uint64_t);
        }
        if (file->flags & TASK_FILE_FLAG_DEV_SHM) {
            if (a2 && !user_range_writable(a1, a2)) return -LEONOS_EFAULT;
            return task_shm_read(file, (void *)(uintptr_t)a1, (uint32_t)a2);
        }
        if (file->flags & TASK_FILE_FLAG_DEV_NODE) {
            if (!file_can_read(file)) return -LEONOS_EBADF;
            request_len = a2 > LEONOS_FS_IO_SLICE_BYTES
                              ? LEONOS_FS_IO_SLICE_BYTES : (uint32_t)a2;
            {
                int result = task_device_read(task, file, (void *)(uintptr_t)a1,
                                              request_len);
                if (result > 0 && (file->flags & TASK_FILE_FLAG_DEV_BLOCK)) {
                    file->offset += (uint32_t)result;
                }
                return result;
            }
        }
        if (file->flags & TASK_FILE_FLAG_DEV_NULL) {
            if (!file_can_read(file)) {
                return -LEONOS_EBADF;
            }
            return 0;
        }
        if (file->path[0] == '/' && file->path[1] == 'p' &&
            file->path[2] == 'r' && file->path[3] == 'o' &&
            file->path[4] == 'c') {
            if (file->node.type == LEONOS_FS_TYPE_DIR) {
                struct leonos_dir_entry entry;
                int step = proc_readdir(file->path, &file->offset, &entry);
                if (step < 0) return step;
                if (step == 0) return 0;
                if (a2 < sizeof(entry)) return -LEONOS_EINVAL;
                *(struct leonos_dir_entry *)(uintptr_t)a1 = entry;
                return (int64_t)sizeof(entry);
            }
            if (file->node.type == LEONOS_FS_TYPE_FILE) {
                uint32_t request = a2 > LEONOS_FS_READ_SLICE_BYTES
                                       ? LEONOS_FS_READ_SLICE_BYTES : (uint32_t)a2;
                uint32_t proc_got = 0;
                int ret = proc_read(file->path, file->offset,
                                    (void *)(uintptr_t)a1, request, &proc_got);
                if (ret < 0) return ret == -2 ? -LEONOS_ENOENT : ret;
                file->offset += proc_got;
                return (int64_t)proc_got;
            }
            return -LEONOS_EBADF;
        }
        /* Open file access survives later chmod/chown, as on Linux. */
        if (file->node.type == LEONOS_FS_TYPE_DIR) {
            struct leonos_dir_entry entry;
            int step = storage_readdir_node(&file->node, &file->offset, &entry);
            if (step == 0 && file->node.volume_id == 0 &&
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

    if (number == LINUX_SYS_EXIT || number == LINUX_SYS_EXIT_GROUP) {
        struct task *task = sched_current_task();
        uint32_t pid = sched_current_pid();
        if (number == LINUX_SYS_EXIT_GROUP && task) sched_exit_group(sched_task_tgid(task), a0 & 255);
        if (!task || !task->shared_files || task->shared_files->references == 1) pty_process_exit(pid);
        userland_process_exit(a0 & 255);
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

    if (number == LINUX_SYS_CREAT) {
        /* creat(2) is exactly open(path, O_CREAT|O_WRONLY|O_TRUNC, mode)
         * on Linux; preserve the raw syscall's two-argument ABI. */
        return syscall_dispatch_regs_legacy(LINUX_SYS_OPEN, a0,
                                             LEONOS_O_CREAT | LEONOS_O_WRONLY |
                                             LEONOS_O_TRUNC, a1, 0, 0, 0);
    }

    if (number == LINUX_SYS_OPEN || number == LINUX_SYS_OPENAT) {
        struct task *task = sched_current_task();
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        uint32_t flags = (uint32_t)(number == LINUX_SYS_OPENAT ? a2 : a1);
        uint32_t mode = (uint32_t)(number == LINUX_SYS_OPENAT ? a3 : a2);
        if ((flags & (LEONOS_O_CREAT | LEONOS_O_DIRECTORY)) ==
            (LEONOS_O_CREAT | LEONOS_O_DIRECTORY)) return -LEONOS_EINVAL;
        uint8_t created = 0;
        int ret = number == LINUX_SYS_OPENAT
                    ? resolve_user_path_at(task, (int32_t)a0, a1, false, path, NULL, false)
                    : resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        /* POSIX stream aliases follow the caller's current descriptors,
         * including redirections installed by dup2().  Do this before the
         * synthetic devfs lookup so /dev/stdin is not mistaken for a fresh
         * PTY device node. */
        if (text_eq_cstr(path, "/dev/stdin") ||
            text_eq_cstr(path, "/dev/stdout") ||
            text_eq_cstr(path, "/dev/stderr")) {
            int source = text_eq_cstr(path, "/dev/stdin") ? 0 :
                         (text_eq_cstr(path, "/dev/stdout") ? 1 : 2);
            struct task_file *source_file = task_file_for_fd(task, source);
            if (source_file) {
                return task_duplicate_file_fd(task, source, 0, 0);
            }
            if (task_pty_stream_for_fd(task, source) >= 0) {
                return task_pty_duplicate_fd(task, source, 0, 0);
            }
            return -LEONOS_EBADF;
        }
        if (text_eq_cstr(path, "/dev/null")) {
            uint32_t access = (flags & LEONOS_O_ACCMODE) == LEONOS_O_RDONLY ? FS_ACCESS_READ :
                              (flags & LEONOS_O_ACCMODE) == LEONOS_O_WRONLY ? FS_ACCESS_WRITE :
                              FS_ACCESS_READ | FS_ACCESS_WRITE;
            ret = fs_permissions_check(task, path, access, false);
            if (ret < 0) return ret;
            if (flags & LEONOS_O_DIRECTORY) return -LEONOS_ENOTDIR;
            struct storage_node null_node = {
                .type = LEONOS_FS_TYPE_DEVICE,
                .flags = STORAGE_NODE_FLAG_DEV_NODE,
                .first_cluster = STORAGE_DEV_KIND_NULL,
            };
            return alloc_task_fd(task, &null_node,
                                 flags | TASK_FILE_FLAG_DEV_NULL |
                                 TASK_FILE_FLAG_DEV_NODE, path);
        }
        /* Unix98 PTY allocation is represented by explicit endpoint FDs.
         * The session owner is the opening task; fork/exec inherit the fd. */
        if (text_eq_cstr(path, "/dev/ptmx")) {
            uint32_t access = (flags & LEONOS_O_ACCMODE) == LEONOS_O_RDONLY ? FS_ACCESS_READ :
                              (flags & LEONOS_O_ACCMODE) == LEONOS_O_WRONLY ? FS_ACCESS_WRITE :
                              FS_ACCESS_READ | FS_ACCESS_WRITE;
            ret = fs_permissions_check(task, path, access, false);
            if (ret < 0) return ret;
            if (flags & LEONOS_O_DIRECTORY) return -LEONOS_ENOTDIR;
            int32_t pty_id = pty_create(task ? task->pid : 0);
            if (pty_id < 0) return pty_id;
            ret = task_pty_endpoint_fd(task, (uint32_t)pty_id,
                                       TASK_PTY_ENDPOINT_MASTER, flags);
            if (ret < 0) {
                (void)pty_destroy(task ? task->pid : 0, (uint32_t)pty_id);
            }
            return ret;
        }
        {
            uint32_t pty_id;
            if (task_pty_endpoint_path(path, &pty_id)) {
                return task_pty_endpoint_fd(task, pty_id,
                                            TASK_PTY_ENDPOINT_SLAVE, flags);
            }
        }
        if (path[0] == '/' && path[1] == 'p' && path[2] == 'r' &&
            path[3] == 'o' && path[4] == 'c' && (path[5] == 0 || path[5] == '/')) {
            struct storage_node proc_node;
            if (proc_lookup(path, &proc_node) == 0) {
                ret = fs_permissions_check(task, path, FS_ACCESS_READ, false);
                if (ret < 0) return ret;
                if ((flags & LEONOS_O_ACCMODE) != LEONOS_O_RDONLY || (flags & LEONOS_O_TRUNC))
                    return -LEONOS_EROFS;
                int fd = alloc_task_fd(task, &proc_node, flags, path);
                if (fd >= 0) {
                    struct task_file *proc_file = task_file_for_fd(task, fd);
                    if (proc_file) proc_file->node = proc_node;
                }
                return fd;
            }
            return -LEONOS_ENOENT;
        }
        ret = fs_permissions_search(task, path, false);
        if (ret < 0) return ret;
        ret = storage_lookup_path(path, &node);
        if (ret < 0) {
            if (ret == -2 && (flags & LEONOS_O_CREAT)) {
                ret = fs_permissions_parent(task, path, false);
                if (ret < 0) return ret;
                ret = storage_write_file(path, "", 0);
                if (ret < 0) {
                    return ret;
                }
                created = 1;
                ret = storage_lookup_path(path, &node);
                if (!ret) ret = fs_permissions_create(task, path, &node, mode);
                if (ret < 0) {
                    (void)storage_unlink(path);
                    return ret;
                }
            }
            if (ret < 0) {
                return ret == -2 ? -LEONOS_ENOENT : ret;
            }
        }
        if ((flags & (LEONOS_O_CREAT | LEONOS_O_EXCL)) ==
            (LEONOS_O_CREAT | LEONOS_O_EXCL) && !created) {
            return -LEONOS_EEXIST;
        }
        if (!created) {
            uint32_t access = (flags & LEONOS_O_ACCMODE) == LEONOS_O_RDONLY ? FS_ACCESS_READ :
                              (flags & LEONOS_O_ACCMODE) == LEONOS_O_WRONLY ? FS_ACCESS_WRITE :
                              FS_ACCESS_READ | FS_ACCESS_WRITE;
            if (flags & LEONOS_O_TRUNC) access |= FS_ACCESS_WRITE;
            ret = fs_permissions_check(task, path, access, false);
            if (ret < 0) return ret;
        }
        if ((flags & LEONOS_O_DIRECTORY) && node.type != LEONOS_FS_TYPE_DIR) {
            return -LEONOS_ENOTDIR;
        }
        if (node.type == LEONOS_FS_TYPE_SOCKET) return -LINUX_ENXIO;
        if (node.type == LEONOS_FS_TYPE_FILE) {
            struct leonos_permissions value;
            ret = fs_permissions_get(path, &node, &value);
            if (ret < 0) return ret;
            if ((value.mode & LINUX_S_IFMT) == LINUX_S_IFSOCK) return -LINUX_ENXIO;
        }
        if (node.flags & STORAGE_NODE_FLAG_DEV_NODE) {
            uint32_t device_flags = TASK_FILE_FLAG_DEV_NODE;
            if (node.flags & STORAGE_NODE_FLAG_DEV_BLOCK) {
                device_flags |= TASK_FILE_FLAG_DEV_BLOCK;
            }
            int fd = alloc_task_fd(task, &node,
                                   flags | device_flags, path);
            if (fd >= 0 && node.first_cluster == STORAGE_DEV_KIND_SHM) {
                struct task_file *file = task_file_for_fd(task, fd);
                if (!file || task_shm_attach(file) < 0) {
                    if (file) clear_task_file(file);
                    return -LEONOS_ENOMEM;
                }
            }
            if (fd >= 0 && (node.first_cluster == STORAGE_DEV_KIND_KEYBOARD ||
                            node.first_cluster == STORAGE_DEV_KIND_MOUSE)) {
                struct task_file *file = task_file_for_fd(task, fd);
                if (file) file->aux = input_evdev_cursor_now();
            }
            return fd;
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

    if (number == LINUX_SYS_PIPE2) {
        return syscall_ipc_pipe2(a0, a1);
    }

    if (number == LINUX_SYS_CLOSE) {
        struct task *task = sched_current_task();
        /* Linux close takes unsigned int, including on native x86-64. */
        uint32_t fd = (uint32_t)a0;
        if (!task) return -LEONOS_EBADF;
        if (fd < SCHED_TASK_STDIO_MAX) {
            struct task_file *stdio_file = task_descriptor_for_fd(task, (int)fd);
            struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, (int)fd);
            if (stdio_file) {
                clear_task_file(stdio_file);
            } else if (pty_fd) {
                if (pty_fd->endpoint == TASK_PTY_ENDPOINT_MASTER &&
                    pty_is_owner(pty_fd->pty_id, task->pid) &&
                    sched_pty_master_reference_count(pty_fd->pty_id) == 1)
                    (void)pty_destroy(task->pid, pty_fd->pty_id);
                task_pty_release_entry(pty_fd);
            } else if (sched_task_fds(task)->closed_stdio_mask & (1u << fd)) {
                return -LEONOS_EBADF;
            }
            /* The inherited PTY/console streams have no task_file entry.
             * Closing one must still release its descriptor number so that
             * open("/dev/null") after close(0) returns zero. */
            sched_task_fds(task)->closed_stdio_mask |= 1u << fd;
            return 0;
        }
        struct task_file *descriptor = task_descriptor_for_fd(task, (int)fd);
        if (descriptor) {
            clear_task_file(descriptor);
            return 0;
        }
        {
            struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, (int)fd);
            if (pty_fd) {
                if (pty_fd->endpoint == TASK_PTY_ENDPOINT_MASTER &&
                    pty_is_owner(pty_fd->pty_id, task ? task->pid : 0) &&
                    sched_pty_master_reference_count(pty_fd->pty_id) == 1) {
                    (void)pty_destroy(task->pid, pty_fd->pty_id);
                }
                task_pty_release_entry(pty_fd);
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
        if (file->flags & TASK_FILE_FLAG_DEV_SHM) {
            return task_shm_truncate(file, (uint64_t)a1);
        }
        if (file->node.type != LEONOS_FS_TYPE_FILE || !file_can_write(file) || !file->path[0]) {
            return -LEONOS_EBADF;
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
        return 0;
    }

    if (number == LINUX_SYS_DUP3) {
        struct task *task = sched_current_task();
        struct task_file *file;
        int result;
        a2 = (uint32_t)a2;
        if ((int)a0 == (int)a1) return -LEONOS_EINVAL;
        if (a2 & ~(uint64_t)LEONOS_O_CLOEXEC) return -LEONOS_EINVAL;
        if (task_file_for_fd(task, (int)a0)) {
            result = task_dup2_fd(task, (int)a0, (int)a1);
            if (result < 0) return result;
            file = task_descriptor_for_fd(task, result);
            if (file) file->fd_flags = (a2 & LEONOS_O_CLOEXEC) ? LEONOS_FD_CLOEXEC : 0;
            return result;
        }
        result = task_pty_dup2_fd(task, (int)a0, (int)a1);
        if (result < 0) return result;
        struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, result);
        if (pty_fd) pty_fd->flags = (a2 & LEONOS_O_CLOEXEC) ? LEONOS_FD_CLOEXEC : 0;
        return result;
    }

    if (number == LINUX_SYS_DUP || number == LINUX_SYS_DUP2 || number == LINUX_SYS_FCNTL) {
        struct task *task = sched_current_task();
        /* fcntl's command is unsigned int; its argument remains unsigned long. */
        if (number == LINUX_SYS_FCNTL) a1 = (uint32_t)a1;
        if (number == LINUX_SYS_DUP) {
            struct task_file *source = task_file_for_fd(task, (int)a0);
            if (source) {
                if (!sched_task_limits(task)->nofile.rlim_cur) return -LEONOS_EMFILE;
                return task_duplicate_file_fd(task, (int)a0, 0, 0);
            }
            if (task && !sched_task_limits(task)->nofile.rlim_cur &&
                (task_pty_fd_for_fd(task, (int)a0) || task_pty_stream_for_fd(task, (int)a0) >= 0))
                return -LEONOS_EMFILE;
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
            struct task_file *descriptor = task_descriptor_for_fd(task, (int)a0);
            struct task_pty_fd *pty_fd = task_pty_fd_for_fd(task, (int)a0);
            if (!file_fd && !pty_fd && task_pty_stream_for_fd(task, (int)a0) < 0) {
                return -LEONOS_EBADF;
            }
            if (a1 == LEONOS_F_GETFD) {
                return descriptor ? (int64_t)descriptor->fd_flags :
                       (pty_fd ? (int64_t)pty_fd->flags : 0);
            }
            if (a1 == LEONOS_F_SETFD) {
                if (file_fd) {
                    descriptor->fd_flags = (uint32_t)a2 & LEONOS_FD_CLOEXEC;
                } else if (pty_fd) {
                    pty_fd->flags = (uint32_t)a2 & LEONOS_FD_CLOEXEC;
                }
                return 0;
            }
            if (a1 == LEONOS_F_GETFL) {
                if (file_fd) {
                    return (int64_t)(file_fd->flags &
                        (LEONOS_O_ACCMODE | LEONOS_O_APPEND | LEONOS_O_NONBLOCK));
                }
                if (pty_fd) {
                    return (int64_t)pty_fd->status_flags;
                }
                /* Implicit controlling-TTY stdio descriptors are readable
                 * (fd 0) or writable (fd 1/2). */
                return a0 == 0 ? (int64_t)LEONOS_O_RDONLY
                               : (int64_t)LEONOS_O_WRONLY;
            }
            if (a1 == LEONOS_F_SETFL) {
                if (file_fd) {
                    /* Preserve descriptor-kind/device bits (PIPE, SOCKET_*,
                     * DEV_*) while updating only the open-status flags. */
                    file_fd->flags = (file_fd->flags &
                                      ~(LEONOS_O_APPEND | LEONOS_O_NONBLOCK)) |
                                     (file_fd->flags & LEONOS_O_ACCMODE) |
                                     ((uint32_t)a2 & (LEONOS_O_APPEND |
                                                      LEONOS_O_NONBLOCK));
                } else if (pty_fd) {
                    pty_fd->status_flags = (pty_fd->status_flags & LEONOS_O_ACCMODE) |
                                           ((uint32_t)a2 & (LEONOS_O_APPEND |
                                                            LEONOS_O_NONBLOCK));
                }
                return 0;
            }
        }
        return -LEONOS_ENOSYS;
    }

    if (number == LINUX_SYS_STATFS || number == LINUX_SYS_FSTATFS) {
        struct task *task = sched_current_task();
        struct linux_statfs_abi value = {0};
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        int ret = 0;
        bool synthetic = false;
        if (number == LINUX_SYS_FSTATFS) {
            struct task_file *file = task_file_for_fd(task, (int32_t)a0);
            if (file) {
                node = file->node;
                copy_text(path, sizeof(path), file->path);
                if (file->flags & TASK_FILE_FLAG_PIPE) { value.f_type = 0x50495045; synthetic = true; }
                if (file->flags & (TASK_FILE_FLAG_SOCKET_UNIX | TASK_FILE_FLAG_SOCKET_INET)) {
                    value.f_type = 0x534f434b; synthetic = true;
                }
            } else if (task_pty_fd_for_fd(task, (int32_t)a0) || task_pty_stream_for_fd(task, (int32_t)a0) >= 0) {
                value.f_type = 0x1cd1; synthetic = true;
                path[0] = 0;
            } else return -LEONOS_EBADF;
        } else {
            ret = resolve_user_path(task, a0, path, sizeof(path));
            if (!ret) ret = fs_permissions_search(task, path, false);
            if (ret < 0) return ret;
            if (proc_lookup(path, &node) < 0) ret = storage_lookup_path(path, &node);
            if (ret < 0) return ret;
        }
        struct storage_node proc;
        if (path[0] && proc_lookup(path, &proc) == 0) { value.f_type = 0x9fa0; synthetic = true; }
        if (synthetic) {
            value.f_bsize = value.f_frsize = 4096;
            value.f_namelen = 255;
            value.f_flags = LINUX_ST_VALID;
        } else ret = storage_statfs(&node, &value);
        if (ret < 0) return ret;
        if (!user_range_writable(a1, sizeof(value))) return -LEONOS_EFAULT;
        *(struct linux_statfs_abi *)(uintptr_t)a1 = value;
        return 0;
    }

    if (number == LINUX_SYS_STAT || number == LINUX_SYS_LSTAT || number == LINUX_SYS_NEWFSTATAT) {
        struct task *task = sched_current_task();
        struct leonos_stat st;
        struct linux_stat_abi linux_st;
        char path[LEONOS_FS_PATH_LEN];
        int ret;
        if (number == LINUX_SYS_NEWFSTATAT) {
            struct task_file *empty_file;
            if ((uint32_t)a3 & ~(LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EMPTY_PATH | LINUX_AT_NO_AUTOMOUNT))
                return -LEONOS_EINVAL;
            ret = resolve_user_path_at(task, (int32_t)a0, a1, a3 & LINUX_AT_EMPTY_PATH, path, &empty_file, false);
            if (ret < 0) return ret;
            if (empty_file) return syscall_dispatch_regs_legacy(LINUX_SYS_FSTAT, a0, a2, 0, 0, 0, 0);
            a1 = a2;
        } else {
            ret = resolve_user_path(task, a0, path, sizeof(path));
            if (ret < 0) return ret;
        }
        if (!user_range_writable(a1, sizeof(linux_st))) {
            return -LEONOS_EFAULT;
        }
        ret = fs_permissions_search(task, path, false);
        if (ret < 0) return ret;
        if (text_eq_cstr(path, "/dev/null")) {
            st.type = LEONOS_FS_TYPE_DEVICE;
            st.reserved = 0;
            st.size = 0;
            ret = linux_stat_from_legacy(&linux_st, &st, path, NULL);
            if (ret < 0) return ret;
            *(struct linux_stat_abi *)(uintptr_t)a1 = linux_st;
            return 0;
        }
        {
            struct storage_node proc_node;
            if (proc_lookup(path, &proc_node) == 0) {
                st.type = proc_node.type;
                st.reserved = 0;
                st.size = proc_node.size;
                ret = linux_stat_from_legacy(&linux_st, &st, path, &proc_node);
                if (ret < 0) return ret;
                *(struct linux_stat_abi *)(uintptr_t)a1 = linux_st;
                return 0;
            }
        }
        ret = storage_stat_path(path, &st);
        if (ret < 0) {
            return ret == -2 ? -LEONOS_ENOENT : ret;
        }
        ret = linux_stat_from_legacy(&linux_st, &st, path, NULL);
        if (ret < 0) return ret;
        *(struct linux_stat_abi *)(uintptr_t)a1 = linux_st;
        return 0;
    }

    if (number == LINUX_SYS_STATX) {
        struct task *task = sched_current_task();
        struct task_file *empty_file = NULL;
        struct leonos_stat st = {0};
        struct linux_stat_abi linux_st;
        struct linux_statx linux_stx;
        char path[LEONOS_FS_PATH_LEN];
        uint32_t flags = (uint32_t)a2;
        uint32_t mask = (uint32_t)a3;
        int ret;
        const uint32_t supported_flags = LINUX_AT_SYMLINK_NOFOLLOW |
            LINUX_AT_NO_AUTOMOUNT | LINUX_AT_EMPTY_PATH | LINUX_AT_STATX_SYNC_TYPE;
        if (!mask || (flags & ~supported_flags) ||
            (flags & LINUX_AT_STATX_SYNC_TYPE) == LINUX_AT_STATX_SYNC_TYPE) {
            return -LEONOS_EINVAL;
        }
        if (!user_range_writable(a4, sizeof(linux_stx))) return -LEONOS_EFAULT;
        ret = resolve_user_path_at(task, (int32_t)a0, a1,
                                   flags & LINUX_AT_EMPTY_PATH, path,
                                   &empty_file, false);
        if (ret < 0) return ret;
        if (empty_file) {
            st.type = empty_file->node.type;
            st.size = empty_file->node.size;
            ret = linux_stat_from_legacy(&linux_st, &st,
                                         empty_file->path[0] ? empty_file->path : NULL,
                                         &empty_file->node);
        } else {
            ret = fs_permissions_search(task, path, false);
            if (ret < 0) return ret;
            {
                struct storage_node proc_node;
                if (proc_lookup(path, &proc_node) == 0) {
                    st.type = proc_node.type;
                    st.size = proc_node.size;
                    ret = linux_stat_from_legacy(&linux_st, &st, path, &proc_node);
                } else {
                    ret = storage_stat_path(path, &st);
                    if (ret >= 0) ret = linux_stat_from_legacy(&linux_st, &st, path, NULL);
                }
            }
        }
        if (ret < 0) return ret == -2 ? -LEONOS_ENOENT : ret;
        linux_statx_from_legacy(&linux_stx, &linux_st, mask);
        *(struct linux_statx *)(uintptr_t)a4 = linux_stx;
        return 0;
    }

    if (number == LINUX_SYS_SENDFILE) {
        return syscall_sendfile(a0, a1, a2, a3);
    }

    if (number == LINUX_SYS_COPY_FILE_RANGE) {
        return syscall_copy_file_range(a0, a1, a2, a3, a4, a5);
    }

    if (number == LINUX_SYS_FSTAT) {
        struct task *task = sched_current_task();
        struct leonos_stat st;
        struct linux_stat_abi linux_st;
        int ret;
        if (!user_range_writable(a1, sizeof(linux_st))) {
            return -LEONOS_EFAULT;
        }
        ret = stat_for_fd((int)a0, task, &st);
        if (ret < 0) {
            return ret;
        }
        struct task_file *file = task_file_for_fd(task, (int)a0);
        const char *path = file && file->path[0] ? file->path : NULL;
        if (file && (file->flags & (TASK_FILE_FLAG_PIPE | TASK_FILE_FLAG_SOCKET_UNIX | TASK_FILE_FLAG_SOCKET_INET))) path = NULL;
        ret = linux_stat_from_legacy(&linux_st, &st, path, file ? &file->node : NULL);
        if (ret < 0) return ret;
        *(struct linux_stat_abi *)(uintptr_t)a1 = linux_st;
        return 0;
    }

    if (number == LINUX_SYS_ACCESS) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path_ids(task, a0, path, sizeof(path), true);
        if (ret < 0) return ret;
        if (a1 & ~(4u | 2u | 1u)) return -LEONOS_EINVAL;
        return fs_permissions_check(task, path, (uint32_t)a1, true);
    }

    if (number == LINUX_SYS_READLINK || number == LINUX_SYS_READLINKAT) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        uint64_t pathname = number == LINUX_SYS_READLINKAT ? a1 : a0;
        uint64_t buffer = number == LINUX_SYS_READLINKAT ? a2 : a1;
        int32_t capacity = (int32_t)(number == LINUX_SYS_READLINKAT ? a3 : a2);
        int32_t dirfd = number == LINUX_SYS_READLINKAT ? (int32_t)a0 : LINUX_AT_FDCWD;
        int ret;
        if (capacity <= 0) return -LEONOS_EINVAL;
        ret = resolve_user_path_at(task, dirfd, pathname, 0, path, NULL, false);
        if (ret < 0) return ret;
        char target[LEONOS_FS_PATH_LEN];
        ret = proc_readlink(path, target, sizeof(target));
        if (ret == -LEONOS_ENOENT) {
            struct storage_node node;
            int found = proc_lookup(path, &node);
            if (found < 0) found = storage_lookup_path(path, &node);
            return found < 0 ? storage_errno(found) : -LEONOS_EINVAL;
        }
        if (ret < 0) return ret;
        if (ret > capacity) ret = capacity;
        if (!user_range_writable(buffer, (uint32_t)ret)) return -LEONOS_EFAULT;
        for (int i = 0; i < ret; ++i) ((char *)(uintptr_t)buffer)[i] = target[i];
        return ret;
    }

    if (number == LINUX_SYS_FCHDIR) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (file->node.type != LEONOS_FS_TYPE_DIR || !file->path[0]) return -LEONOS_ENOTDIR;
        int ret = fs_permissions_check(task, file->path, FS_ACCESS_EXEC, false);
        if (ret < 0) return ret;
        copy_text(sched_task_cwd(task), LEONOS_FS_PATH_LEN, file->path);
        return 0;
    }

    if (number == LINUX_SYS_TRUNCATE) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret;
        if ((int64_t)a1 < 0) return -LEONOS_EINVAL;
        ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) return ret;
        ret = authz_check_path(task, LEONOS_AUTHZ_WRITE, path, 0, 0);
        if (ret < 0) return ret;
        ret = storage_truncate_file(path, a1);
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_FSYNC || number == LINUX_SYS_FDATASYNC) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        /* All current storage writes are synchronous before the syscall
         * returns. Device queues expose their own ioctl/flush contracts. */
        return 0;
    }

    if (number == LINUX_SYS_UMASK) {
        struct task *task = sched_current_task();
        uint32_t old;
        if (!task) return -LEONOS_EPERM;
        old = (*sched_task_umask(task)) & 0777u;
        (*sched_task_umask(task)) = (uint32_t)a0 & 0777u;
        return old;
    }

    if (number == LINUX_SYS_PREAD64 || number == LINUX_SYS_PWRITE64) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        uint64_t saved;
        int64_t result;
        if (!file || (int64_t)a3 < 0) return -LEONOS_EBADF;
        saved = file->offset;
        file->offset = a3;
        result = syscall_dispatch_regs_legacy(number == LINUX_SYS_PREAD64 ? LINUX_SYS_READ : LINUX_SYS_WRITE,
                                              a0, a1, a2, 0, 0, 0);
        file->offset = saved;
        return result;
    }

    if (number == LINUX_SYS_TIME) {
        struct linux_timespec now;
        int ret;
        ret = time_clock_get(LINUX_CLOCK_REALTIME, &now);
        if (ret < 0) return ret;
        if (a0) {
            if (!user_range_writable(a0, sizeof(int64_t))) return -LEONOS_EFAULT;
            *(int64_t *)(uintptr_t)a0 = now.tv_sec;
        }
        return now.tv_sec;
    }

    if (number == LINUX_SYS_GETCPU) {
        uint32_t cpu = smp_current_cpu();
        uint32_t node = 0;
        if (a0 && !user_range_writable(a0, sizeof(uint32_t))) return -LEONOS_EFAULT;
        if (a1 && !user_range_writable(a1, sizeof(uint32_t))) return -LEONOS_EFAULT;
        if (a0) *(uint32_t *)(uintptr_t)a0 = cpu;
        if (a1) *(uint32_t *)(uintptr_t)a1 = node;
        return 0;
    }

    if (number == LINUX_SYS_CLOSE_RANGE) {
        struct task *task = sched_current_task();
        uint32_t first = (uint32_t)a0;
        uint32_t last = (uint32_t)a1;
        uint32_t flags = (uint32_t)a2;
        if (!task) return -LEONOS_EPERM;
        if (last < first || (flags & ~6u)) return -LEONOS_EINVAL;
        if (flags & 2u) {
            int unshare_ret = syscall_unshare_task_files(task);
            if (unshare_ret < 0) return unshare_ret;
        }
        /* Linux accepts UINT_MAX as the open-ended upper bound. Iterate only
         * materialized descriptor slots; PTY aliases live in a separate small
         * table and are handled independently below. */
        uint32_t stdio_last = last < 2u ? last : 2u;
        for (uint32_t fd = first; fd <= stdio_last; ++fd) {
            if (flags & 4u) {
                sched_task_fds(task)->cloexec_stdio_mask |= 1u << fd;
            } else {
                (void)syscall_dispatch_regs_legacy(LINUX_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
            }
        }
        uint32_t file_first = first < 3u ? 3u : first;
        uint32_t file_capacity = sched_task_file_capacity(task);
        uint64_t file_end = (uint64_t)file_capacity + 2u;
        uint32_t file_last = last < file_end ? last :
                             file_end > UINT32_MAX ? UINT32_MAX : (uint32_t)file_end;
        if (file_first <= file_last) {
            for (uint64_t fd = file_first; fd <= file_last; ++fd) {
                if (flags & 4u) {
                    struct task_file *descriptor = task_descriptor_for_fd(task, (int)fd);
                    if (descriptor) descriptor->fd_flags |= LEONOS_FD_CLOEXEC;
                } else {
                    (void)syscall_dispatch_regs_legacy(LINUX_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                }
            }
        }
        for (uint32_t i = 0; i < SCHED_TASK_PTY_FD_MAX; ++i) {
            struct task_pty_fd *pty_fd = &sched_task_fds(task)->pty_fds[i];
            if (!pty_fd->used || pty_fd->fd < 0 || (uint32_t)pty_fd->fd < first ||
                (uint32_t)pty_fd->fd > last) continue;
            if (flags & 4u) pty_fd->flags |= LEONOS_FD_CLOEXEC;
            else (void)syscall_dispatch_regs_legacy(LINUX_SYS_CLOSE,
                                                     (uint32_t)pty_fd->fd, 0, 0, 0, 0, 0);
        }
        return 0;
    }

    if (number == LINUX_SYS_READV || number == LINUX_SYS_WRITEV ||
        number == LINUX_SYS_PREADV || number == LINUX_SYS_PWRITEV ||
        number == LINUX_SYS_PREADV2 || number == LINUX_SYS_PWRITEV2) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_io(task, (int)a0);
        bool writing = number == LINUX_SYS_WRITEV || number == LINUX_SYS_PWRITEV ||
                       number == LINUX_SYS_PWRITEV2;
        bool positional = number == LINUX_SYS_PREADV || number == LINUX_SYS_PWRITEV ||
                          number == LINUX_SYS_PREADV2 || number == LINUX_SYS_PWRITEV2;
        uint64_t position = (uint64_t)(uint32_t)a3 |
                            ((uint64_t)(uint32_t)a4 << 32);
        uint64_t positional_flags = (number == LINUX_SYS_PREADV2 ||
                                     number == LINUX_SYS_PWRITEV2) ? a5 : 0;
        uint64_t saved_offset = 0;
        int64_t total;
        if (positional && positional_flags) return -LEONOS_EOPNOTSUPP;
        if (file ? !(writing ? file_can_write(file) : file_can_read(file)) :
            (!task_pty_endpoint_for_fd(task, (int)a0) &&
             ((uint32_t)a0 >= SCHED_TASK_STDIO_MAX ||
              (sched_task_fds(task)->closed_stdio_mask & (1u << (uint32_t)a0))))) return -LEONOS_EBADF;
        if (a2 > UIO_MAXIOV) return -LEONOS_EINVAL;
        if (!a2) return 0;
        if (!user_range_ok(a1, a2 * sizeof(struct iovec))) return -LEONOS_EFAULT;
        struct iovec iov[UIO_MAXIOV];
        const struct iovec *user_iov = (const struct iovec *)(uintptr_t)a1;
        uint64_t requested = 0;
        for (uint64_t i = 0; i < a2; ++i) {
            iov[i] = user_iov[i];
            if ((int64_t)iov[i].iov_len < 0) return -LEONOS_EINVAL;
            if (iov[i].iov_len > 0x7ffff000u - requested) iov[i].iov_len = 0x7ffff000u - requested;
            if (iov[i].iov_len && !(writing ? user_range_ok((uintptr_t)iov[i].iov_base, iov[i].iov_len) :
                user_range_writable((uintptr_t)iov[i].iov_base, iov[i].iov_len))) return -LEONOS_EFAULT;
            requested += iov[i].iov_len;
        }
        if (!requested) return 0;
        if (positional) {
            if (!file || (int64_t)position < 0) return -LEONOS_EINVAL;
            if (file->node.type != LEONOS_FS_TYPE_FILE) return -LEONOS_ESPIPE;
            saved_offset = file->offset;
            file->offset = position;
        }
        if (file && (file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) {
            if (positional) file->offset = saved_offset;
            return task_socket_vector(file, iov, (uint32_t)a2, requested, writing);
        }
        total = 0;
        for (uint64_t i = 0; i < a2; ++i) {
            if (!iov[i].iov_len) continue;
            int64_t part = syscall_dispatch_regs_legacy(writing ? LINUX_SYS_WRITE : LINUX_SYS_READ,
                                                        a0, (uintptr_t)iov[i].iov_base, iov[i].iov_len, 0, 0, 0);
            if (part < 0) {
                if (positional) file->offset = saved_offset;
                return total ? total : part;
            }
            total += part;
            if ((uint64_t)part < iov[i].iov_len) break;
        }
        if (positional) file->offset = saved_offset;
        return total;
    }

    if (number == LINUX_SYS_GETDENTS || number == LINUX_SYS_GETDENTS64) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        uint32_t written = 0;
        uint32_t capacity = (uint32_t)a2;
        if (!file) return -LEONOS_EBADF;
        if (file->node.type != LEONOS_FS_TYPE_DIR) return -LEONOS_ENOTDIR;
        if (!file_can_read(file)) return -LEONOS_EBADF;
        if (!user_range_writable(a1, capacity)) return -LEONOS_EFAULT;
        while (1) {
            struct leonos_dir_entry entry;
            uint64_t next_offset = file->offset;
            int step = file->node.flags & STORAGE_NODE_FLAG_PROC
                         ? proc_readdir(file->path, &next_offset, &entry)
                         : storage_readdir_node(&file->node, &next_offset, &entry);
            uint32_t name_len;
            uint32_t reclen;
            uint8_t *dst;
            if (step < 0) return written ? (int64_t)written : storage_errno(step);
            if (step == 0) break;
            name_len = 0;
            while (name_len < sizeof(entry.name) && entry.name[name_len]) ++name_len;
            /* The legacy getdents record has no explicit d_type field.  Since
             * Linux 2.6 the type byte is stored in the final byte of each
             * aligned record; getdents64 carries it at offset 18 instead. */
            uint32_t name_offset = number == LINUX_SYS_GETDENTS64 ?
                (uint32_t)offsetof(struct linux_dirent64, d_name) : 18u;
            uint32_t type_bytes = number == LINUX_SYS_GETDENTS64 ? 0u : 1u;
            reclen = (uint32_t)((name_offset + name_len + 1 + type_bytes + 7) & ~7u);
            if (reclen > capacity - written) return written ? (int64_t)written : -LEONOS_EINVAL;
            dst = (uint8_t *)(uintptr_t)a1 + written;
            *(uint64_t *)(dst + 0) = linux_stat_inode(entry.name, NULL);
            *(int64_t *)(dst + 8) = (int64_t)next_offset;
            *(uint16_t *)(dst + 16) = (uint16_t)reclen;
            uint8_t dtype = entry.type == LEONOS_FS_TYPE_DIR ? LINUX_DT_DIR :
                            entry.type == LEONOS_FS_TYPE_SOCKET ? LINUX_DT_SOCK :
                            entry.type == LEONOS_FS_TYPE_DEVICE ? LINUX_DT_CHR : LINUX_DT_REG;
            if (entry.type == LEONOS_FS_TYPE_FILE && !(file->node.flags & STORAGE_NODE_FLAG_PROC)) {
                char entry_path[LEONOS_FS_PATH_LEN];
                struct leonos_permissions permissions;
                if (storage_resolve_path(file->path, entry.name, entry_path, sizeof(entry_path)) == 0 &&
                    fs_permissions_get(entry_path, NULL, &permissions) == 0 &&
                    (permissions.mode & LINUX_S_IFMT) == LINUX_S_IFSOCK) dtype = LINUX_DT_SOCK;
            }
            for (uint32_t i = 0; i <= name_len; ++i) dst[name_offset + i] = entry.name[i];
            for (uint32_t i = name_offset + name_len + 1; i < reclen; ++i) dst[i] = 0;
            if (number == LINUX_SYS_GETDENTS) dst[reclen - 1] = dtype;
            else dst[18] = dtype;
            file->offset = next_offset;
            written += reclen;
        }
        return written;
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
        } else if (file->flags & TASK_FILE_FLAG_DEV_BLOCK) {
            /* Block devices are seekable byte streams.  GPT editors and
             * filesystem tools use lseek(2) before issuing aligned raw I/O;
             * treating them as non-seekable makes every read after offset
             * zero fail with EINVAL. */
            size = (int64_t)file->node.size;
            if ((int)a2 == LEONOS_SEEK_CUR) {
                base = (int64_t)file->offset;
            } else if ((int)a2 == LEONOS_SEEK_END) {
                base = size;
            } else if ((int)a2 != LEONOS_SEEK_SET) {
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
        const char *cwd = (task && sched_task_cwd(task)[0]) ? sched_task_cwd(task) : "/";
        size_t len = 0;
        while (cwd[len]) {
            ++len;
        }
        if (a1 == 0 || len + 1 > a1) {
            return -LEONOS_ERANGE;
        }
        if (!user_range_ok(a0, len + 1)) {
            return -LEONOS_EFAULT;
        }
        for (size_t i = 0; i <= len; ++i) {
            ((char *)(uintptr_t)a0)[i] = cwd[i];
        }
        return (int64_t)(len + 1);
    }

    if (number == LINUX_SYS_CHDIR) {
        struct task *task = sched_current_task();
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = fs_permissions_check(task, path, FS_ACCESS_EXEC, false);
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
            copy_text(sched_task_cwd(task), LEONOS_FS_PATH_LEN, path);
        }
        return 0;
    }

    if (number == LINUX_SYS_MKDIR || number == LINUX_SYS_MKDIRAT) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        uint32_t mode = number == LINUX_SYS_MKDIRAT ? (uint32_t)a2 : (uint32_t)a1;
        int ret = number == LINUX_SYS_MKDIRAT
                    ? resolve_user_path_at(task, (int32_t)a0, a1, false, path, NULL, false)
                    : resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = fs_permissions_parent(task, path, false);
        if (ret < 0) {
            return ret;
        }
        ret = storage_mkdir(path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        struct storage_node node;
        ret = storage_lookup_path(path, &node);
        if (!ret) ret = fs_permissions_create(task, path, &node, mode);
        if (ret < 0) (void)storage_rmdir(path);
        return ret;
    }

    if (number == LINUX_SYS_UNLINK || number == LINUX_SYS_RMDIR || number == LINUX_SYS_UNLINKAT) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        bool directory = number == LINUX_SYS_RMDIR || (number == LINUX_SYS_UNLINKAT && ((uint32_t)a2 & LINUX_AT_REMOVEDIR));
        if (number == LINUX_SYS_UNLINKAT && ((uint32_t)a2 & ~LINUX_AT_REMOVEDIR)) return -LEONOS_EINVAL;
        int ret = number == LINUX_SYS_UNLINKAT
                    ? resolve_user_path_at(task, (int32_t)a0, a1, false, path, NULL, false)
                    : resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_DELETE, path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        ret = directory ? storage_rmdir(path) : storage_unlink(path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_DELETE, task, path, 0);
        task_socket_unlink_path(path);
        return 0;
    }

    if (number == LINUX_SYS_RENAME || number == LINUX_SYS_RENAMEAT ||
        number == LINUX_SYS_RENAMEAT2) {
        struct task *task = sched_current_task();
        char old_path[LEONOS_FS_PATH_LEN];
        char new_path[LEONOS_FS_PATH_LEN];
        uint32_t flags = number == LINUX_SYS_RENAMEAT2 ? (uint32_t)a4 : 0;
        if (number == LINUX_SYS_RENAMEAT2 && flags != 0) {
            /* The storage backends do not implement exchange/noreplace
             * atomics; never silently turn those requests into rename. */
            return -LEONOS_EOPNOTSUPP;
        }
        int ret = number == LINUX_SYS_RENAME
                    ? resolve_user_path(task, a0, old_path, sizeof(old_path))
                    : resolve_user_path_at(task, (int32_t)a0, a1, false,
                                           old_path, NULL, false);
        if (ret < 0) {
            return ret;
        }
        ret = number == LINUX_SYS_RENAME
                    ? resolve_user_path(task, a1, new_path, sizeof(new_path))
                    : resolve_user_path_at(task, (int32_t)a2, a3, false,
                                           new_path, NULL, false);
        if (ret < 0) {
            return ret;
        }
        ret = authz_check_path(task, LEONOS_AUTHZ_DELETE, old_path, 0, 0);
        if (ret < 0) {
            return ret;
        }
        struct storage_node destination;
        int destination_exists = storage_lookup_path(new_path, &destination) == 0;
        ret = fs_permissions_parent(task, new_path, destination_exists);
        if (ret < 0) {
            return ret;
        }
        ret = storage_rename(old_path, new_path);
        if (ret < 0) {
            return storage_errno(ret);
        }
        fs_acl_notify(LEONOS_FS_ACL_ACTION_NOTE_RENAME, task, old_path, new_path);
        task_socket_rename_path(old_path, new_path);
        return 0;
    }

    if (number == LINUX_SYS_MOUNT) {
        struct task *task = sched_current_task();
        struct storage_node source_node;
        struct storage_node target_node;
        char source[LEONOS_FS_PATH_LEN];
        char target[LEONOS_FS_PATH_LEN];
        char filesystem[16];
        uint32_t disk_id;
        int32_t partition_index;
        int ret;

        if (a4 != 0 ||
            (a3 & ~(MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC |
                     MS_SYNCHRONOUS | MS_DIRSYNC | MS_NOATIME | MS_NODIRATIME |
                     MS_RELATIME | MS_STRICTATIME | MS_LAZYTIME)) != 0) {
            return -LEONOS_ENOTSUP;
        }
        /* The storage backends are read/write today. Do not claim a
         * read-only mount while later writes would still succeed. */
        if (a3 & MS_RDONLY) {
            return -LEONOS_ENOTSUP;
        }
        ret = authz_check_install(task);
        if (ret < 0) {
            return ret;
        }
        ret = resolve_user_path(task, a0, source, sizeof(source));
        if (ret < 0) {
            return ret;
        }
        ret = resolve_user_path(task, a1, target, sizeof(target));
        if (ret < 0) {
            return ret;
        }
        if (a2) {
            ret = copy_user_string_fixed(filesystem, sizeof(filesystem), a2, NULL);
            if (ret < 0) {
                return ret;
            }
        } else {
            filesystem[0] = 0;
        }
        ret = storage_lookup_path(source, &source_node);
        if (ret < 0) {
            return storage_errno(ret);
        }
        if (!(source_node.flags & STORAGE_NODE_FLAG_DEV_BLOCK)) {
            return -LEONOS_ENOTTY;
        }
        disk_id = STORAGE_BLOCK_DISK_ID(source_node.volume_id);
        partition_index = STORAGE_BLOCK_PARTITION(source_node.volume_id);
        if (partition_index < 0) {
            return -LEONOS_EINVAL;
        }
        ret = storage_lookup_path(target, &target_node);
        if (ret < 0) {
            return storage_errno(ret);
        }
        if (target_node.type != LEONOS_FS_TYPE_DIR) {
            return -LEONOS_ENOTDIR;
        }
        ret = storage_mount_block_partition(disk_id, (uint32_t)partition_index,
                                            target, filesystem[0] ? filesystem : NULL,
                                            a3, NULL);
        if (ret < 0) {
            console_printf("[ntclks] mount syscall failed source=%s target=%s fs=%s ret=%d\n",
                           source, target, filesystem[0] ? filesystem : "auto", ret);
        }
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_UMOUNT2) {
        struct task *task = sched_current_task();
        char target[LEONOS_FS_PATH_LEN];
        uint32_t volume_id;
        int ret;
        if (a1 & ~(uint64_t)UMOUNT_NOFOLLOW) {
            return -LEONOS_ENOTSUP;
        }
        ret = authz_check_install(task);
        if (ret < 0) {
            return ret;
        }
        ret = resolve_user_path(task, a0, target, sizeof(target));
        if (ret < 0) {
            return ret;
        }
        ret = storage_mount_path_volume_id(target, &volume_id);
        if (ret < 0) {
            return storage_errno(ret);
        }
        if (sched_volume_in_use(volume_id)) {
            return -LEONOS_EBUSY;
        }
        ret = storage_unmount_path(target, NULL);
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_GETPID || number == LINUX_SYS_GETPPID ||
        number == LINUX_SYS_SETPGID || number == LINUX_SYS_GETPGRP ||
        number == LINUX_SYS_SETSID || number == LINUX_SYS_GETPGID || number == LINUX_SYS_GETSID ||
        number == LINUX_SYS_KILL || number == LINUX_SYS_TKILL ||
        number == LINUX_SYS_TGKILL || number == LEONOS_SYS_NICE ||
        number == LINUX_SYS_GETPRIORITY || number == LINUX_SYS_SETPRIORITY) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }
    if (number == LINUX_SYS_GETRLIMIT || number == LINUX_SYS_SETRLIMIT || number == LINUX_SYS_PRLIMIT64) {
        return syscall_process_control(number, a0, a1, a2, a3);
    }

    if (number == LINUX_SYS_SCHED_YIELD) {
        userland_yield_if_runnable();
        return 0;
    }

    if (number == LINUX_SYS_NANOSLEEP) {
        return syscall_nanosleep(1, 0, a0, a1);
    }

    if (number == LINUX_SYS_ALARM) {
        struct task *task = sched_current_task();
        return task ? (int64_t)sched_alarm_task(task, (uint32_t)a0) : -LINUX_ESRCH;
    }
    if (number == LINUX_SYS_GETITIMER || number == LINUX_SYS_SETITIMER) {
        return syscall_itimer(number == LINUX_SYS_SETITIMER, (int32_t)a0, a1, a2);
    }

    if (number == LINUX_SYS_TIMER_CREATE)
        return syscall_timer_create(a0, a1, a2);
    if (number == LINUX_SYS_TIMER_SETTIME)
        return syscall_timer_settime(a0, a1, a2, a3);
    if (number == LINUX_SYS_TIMER_GETTIME)
        return syscall_timer_gettime(a0, a1);
    if (number == LINUX_SYS_TIMER_GETOVERRUN)
        return syscall_timer_getoverrun(a0);
    if (number == LINUX_SYS_TIMER_DELETE)
        return syscall_timer_delete(a0);
    if (number == LINUX_SYS_RT_SIGTIMEDWAIT)
        return syscall_rt_sigtimedwait(a0, a1, a2, a3);

    if (number == LINUX_SYS_CLOCK_NANOSLEEP) {
        return syscall_nanosleep((int32_t)a0, (uint32_t)a1, a2, a3);
    }

    if (number == LINUX_SYS_GETRANDOM) {
        if (a2 & ~7u) return -LEONOS_EINVAL;
        if (a1 && !user_range_ok(a0, a1)) return -LEONOS_EFAULT;
        if (a1 > 0x7ffff000ULL) return -LEONOS_EINVAL;
        elf64_random_fill((void *)(uintptr_t)a0, (size_t)a1);
        return (int64_t)a1;
    }

    if (number == LINUX_SYS_FUTEX) {
        return syscall_futex(a0, a1, a2, a3, a4, a5);
    }
    if (number == LINUX_SYS_FUTEX_WAKE) return syscall_futex_wake2(a0, a1, a2, a3);
    if (number == LINUX_SYS_FUTEX_WAIT) return syscall_futex_wait2(a0, a1, a2, a3, a4, a5);
    if (number == LINUX_SYS_FUTEX_REQUEUE) return syscall_futex_requeue2(a0, a1, a2, a3);

    if (number == LINUX_SYS_MMAP) {
        return syscall_mm_mmap(a0, a1, a2, a3, a4, a5);
    }

    if (number == LINUX_SYS_MREMAP) {
        return syscall_mm_mremap(a0, a1, a2, a3, a4);
    }
    if (number == LINUX_SYS_MLOCK || number == LINUX_SYS_MLOCK2) {
        return syscall_mm_mlock(a0, a1, number == LINUX_SYS_MLOCK2 ? a2 : 0);
    }
    if (number == LINUX_SYS_MUNLOCK) return syscall_mm_munlock(a0, a1);
    if (number == LINUX_SYS_MLOCKALL) return syscall_mm_mlockall(a0);
    if (number == LINUX_SYS_MUNLOCKALL) return syscall_mm_munlockall();

    if (number == LINUX_SYS_BRK) {
        return syscall_mm_brk(a0);
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

    if (number == LINUX_SYS_WAITID) {
        /* waitid(2) reports the same child state as wait4, but writes a
         * fixed 128-byte siginfo_t and always returns zero on success. */
        enum { LINUX_P_ALL = 0, LINUX_P_PID = 1, LINUX_P_PGID = 2 };
        enum { LINUX_WNOHANG = 1, LINUX_WSTOPPED = 2, LINUX_WEXITED = 4,
               LINUX_WCONTINUED = 8 };
        uint32_t idtype = (uint32_t)a0;
        uint32_t options = (uint32_t)a3;
        int32_t wanted = -1;
        int status = 0;
        int64_t pid;
        if (idtype == LINUX_P_PID) wanted = (int32_t)a1;
        else if (idtype == LINUX_P_PGID) {
            wanted = a1 ? -(int32_t)a1 : 0;
        } else if (idtype != LINUX_P_ALL) return -LEONOS_EINVAL;
        if (!(options & (LINUX_WEXITED | LINUX_WSTOPPED | LINUX_WCONTINUED)) ||
            options & ~(uint32_t)(LINUX_WNOHANG | LINUX_WSTOPPED |
                                  LINUX_WEXITED | LINUX_WCONTINUED)) {
            return -LEONOS_EINVAL;
        }
        if (!a2 || !user_range_writable(a2, sizeof(struct linux_siginfo))) {
            return -LEONOS_EFAULT;
        }
        pid = sched_wait_reap(sched_current_pid(), wanted,
                              (options & LINUX_WNOHANG) |
                              ((options & LINUX_WSTOPPED) ? 2u : 0u) |
                              ((options & LINUX_WCONTINUED) ? 4u : 0u), &status);
        if (pid == -LEONOS_EAGAIN) {
            if (!(options & LINUX_WNOHANG)) return pid;
            *(struct linux_siginfo *)(uintptr_t)a2 = (struct linux_siginfo){0};
            return 0;
        }
        if (pid <= 0) return -LEONOS_ECHILD;
        struct linux_siginfo info = {0};
        info.signo = 17; /* SIGCHLD */
        info.code = (status & 0x7f) ? 2 : 1; /* CLD_KILLED/CLD_EXITED */
        info.fields.sender.pid = (int32_t)pid;
        info.fields.sender.uid = 0;
        *(struct linux_siginfo *)(uintptr_t)a2 = info;
        return 0;
    }

    if (number == LINUX_SYS_IOCTL) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (file && (file->flags & TASK_FILE_FLAG_SOCKET_UNIX))
            return task_socket_ioctl(file, a1, a2);
        int evdev_ret = task_evdev_ioctl(file, a1, a2);
        if (evdev_ret != -LEONOS_ENOTTY) {
            return evdev_ret;
        }
        {
            int oss_ret = task_oss_dsp_ioctl(file, a1, a2);
            if (oss_ret != -LEONOS_ENOTTY) {
                return oss_ret;
            }
        }
    }

    /* Linux block-device UAPI.  A descriptor carries the physical disk and
     * optional GPT entry, so standard tools do not need the private disk
     * management ioctl family just to inspect or stream sectors. */
    if (number == LINUX_SYS_IOCTL) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        uint32_t disk_id;
        int32_t partition_index;
        uint64_t first_lba;
        uint64_t sector_count;
        if (task_block_device(file, &disk_id, &partition_index)) {
            if (a1 == BLKGETSIZE64 || a1 == BLKGETSIZE) {
                if (!a2 || !user_range_ok(a2, sizeof(uint64_t))) {
                    return -LEONOS_EFAULT;
                }
                {
                    int ret = storage_disk_block_info(disk_id, partition_index,
                                                      &first_lba, &sector_count);
                    if (ret < 0) return ret;
                }
                *(uint64_t *)(uintptr_t)a2 = a1 == BLKGETSIZE64
                                                  ? sector_count * 512ULL
                                                  : sector_count;
                return 0;
            }
            if (a1 == BLKSSZGET) {
                if (!a2 || !user_range_ok(a2, sizeof(int))) return -LEONOS_EFAULT;
                *(int *)(uintptr_t)a2 = 512;
                return 0;
            }
            if (a1 == BLKROGET) {
                if (!a2 || !user_range_ok(a2, sizeof(int))) return -LEONOS_EFAULT;
                *(int *)(uintptr_t)a2 = 0;
                return 0;
            }
            if (a1 == BLKRRPART) {
                if (!task || (task_effective_role(task) != LEONOS_AUTH_ROLE_ADMIN &&
                              !(task->uid == 0 && storage_installer_root_active()))) {
                    return -LEONOS_EACCES;
                }
                if (partition_index >= 0) return -LEONOS_EINVAL;
                return storage_disk_block_reread(disk_id);
            }
            if (a1 == BLKROSET) {
                return -LEONOS_EPERM;
            }
        }
    }

    /* Linux fbdev compatibility is intentionally scoped to the /dev/fb0
     * descriptor.  The old GUI framebuffer requests remain available to the
     * windowing transition layer, while standard fb applications use these
     * three Linux UAPI operations. */
    if (number == LINUX_SYS_IOCTL &&
        (a1 == FBIOGET_VSCREENINFO || a1 == FBIOPUT_VSCREENINFO ||
         a1 == FBIOGET_FSCREENINFO || a1 == FBIOPAN_DISPLAY ||
         a1 == LEONOS_FBIOGET_CAPABILITIES)) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        const struct framebuffer *fb = framebuffer_get();
        if (!file || !(file->flags & TASK_FILE_FLAG_DEV_NODE) ||
            file->node.first_cluster != STORAGE_DEV_KIND_FB0 || !fb ||
            !fb->available) {
            return -LEONOS_ENOTTY;
        }
        /* Panning display is the Linux fbdev flush request: VMware SVGA
         * scanout only refreshes from VRAM when the FIFO receives an
         * update command, and mmap writers bypass every other path. */
        if (a1 == FBIOPAN_DISPLAY) {
            framebuffer_present_region(0, 0, fb->width, fb->height);
            return 0;
        }
        if (!a2) {
            return -LEONOS_ENOTTY;
        }
        if (a1 == LEONOS_FBIOGET_CAPABILITIES) {
            if (!user_range_ok(a2, sizeof(struct leonos_fb_capabilities)))
                return -LEONOS_EFAULT;
            *(struct leonos_fb_capabilities *)(uintptr_t)a2 =
                (struct leonos_fb_capabilities){
                    .bytes_per_pixel = fb->bytes_per_pixel,
                    .capabilities = fb->capabilities,
                    .max_width = fb->max_width,
                    .max_height = fb->max_height,
                    .max_bytes = fb->max_bytes,
                    .backend = fb->backend,
                };
            return 0;
        }
        if (a1 == FBIOGET_VSCREENINFO) {
            struct fb_var_screeninfo info;
            if (!user_range_ok(a2, sizeof(info))) return -LEONOS_EFAULT;
            info = (struct fb_var_screeninfo){
                .xres = fb->width,
                .yres = fb->height,
                .xres_virtual = fb->width,
                .yres_virtual = fb->height,
                .bits_per_pixel = fb->bpp,
                .red = { .offset = fb->red_field_position,
                         .length = fb->red_mask_size },
                .green = { .offset = fb->green_field_position,
                           .length = fb->green_mask_size },
                .blue = { .offset = fb->blue_field_position,
                          .length = fb->blue_mask_size },
            };
            *(struct fb_var_screeninfo *)(uintptr_t)a2 = info;
            return 0;
        }
        if (a1 == FBIOGET_FSCREENINFO) {
            struct fb_fix_screeninfo info = {0};
            if (!user_range_ok(a2, sizeof(info))) return -LEONOS_EFAULT;
            copy_text(info.id, sizeof(info.id), "leonos-fb");
            info.smem_start = (uint64_t)(uintptr_t)fb->pixels;
            info.smem_len = fb->pitch * fb->height;
            info.type = 0;
            info.line_length = fb->pitch;
            *(struct fb_fix_screeninfo *)(uintptr_t)a2 = info;
            return 0;
        }
        if (!user_range_ok(a2, sizeof(struct fb_var_screeninfo))) {
            return -LEONOS_EFAULT;
        }
        {
            const struct fb_var_screeninfo *info =
                (const struct fb_var_screeninfo *)(uintptr_t)a2;
            if (!info->xres || !info->yres || info->xres > fb->max_width ||
                info->yres > fb->max_height || info->bits_per_pixel != fb->bpp) {
                return -LEONOS_EINVAL;
            }
            return framebuffer_set_mode(info->xres, info->yres) == 0
                       ? 0 : -LEONOS_EINVAL;
        }
    }

    /* TCGETS uses the 36-byte kernel ABI, not libc's larger public struct. */
    if (number == LINUX_SYS_IOCTL &&
        (a1 == TCGETS || a1 == TCSETS || a1 == TCSETSW || a1 == TCSETSF ||
         a1 == LINUX_TCGETS2 || a1 == LINUX_TCSETS2 ||
         a1 == LINUX_TCSETSW2 || a1 == LINUX_TCSETSF2 ||
         a1 == TIOCGWINSZ || a1 == TIOCSWINSZ || a1 == TIOCGPTN ||
         a1 == TIOCSPTLCK || a1 == TIOCGPTLCK || a1 == TIOCSCTTY ||
         a1 == TIOCGPGRP || a1 == TIOCSPGRP)) {
        struct task *task = sched_current_task();
        struct task_pty_fd *endpoint = task_pty_endpoint_for_fd(task, (int)a0);
        int stream = task_pty_stream_for_fd(task, (int)a0);
        uint32_t pty_id = endpoint ? endpoint->pty_id : (stream >= 0 && task ? task->pty_id : 0);
        if (!pty_id || !pty_is_active(pty_id)) return -LEONOS_ENOTTY;
        if (a1 == TIOCSCTTY) {
            uint32_t access = endpoint ? endpoint->status_flags & LEONOS_O_ACCMODE : LEONOS_O_RDWR;
            return pty_acquire_controlling(pty_id, task->pid, (int32_t)a2,
                                           access != LEONOS_O_WRONLY);
        }
        if (a1 == TIOCGPGRP || a1 == TIOCSPGRP) {
            int *process_group;
            if (!user_range_ok(a2, sizeof(*process_group))) {
                return -LEONOS_EFAULT;
            }
            process_group = (int *)(uintptr_t)a2;
            if (a1 == TIOCGPGRP) {
                uint32_t value = 0;
                int result = pty_get_foreground_pgid(pty_id, &value);
                if (result < 0) {
                    return result;
                }
                *process_group = (int)value;
                return 0;
            }
            if (*process_group <= 0 || !task) {
                return -LEONOS_EINVAL;
            }
            return pty_set_foreground_pgid(pty_id, task->pid,
                                           (uint32_t)*process_group);
        }
        if (a1 == TIOCGPTN) {
            if (!endpoint || endpoint->endpoint != TASK_PTY_ENDPOINT_MASTER ||
                !user_range_ok(a2, sizeof(uint32_t))) return -LEONOS_EINVAL;
            *(uint32_t *)(uintptr_t)a2 = pty_id;
            return 0;
        }
        if (a1 == TIOCSPTLCK) {
            int locked;
            if (!endpoint || endpoint->endpoint != TASK_PTY_ENDPOINT_MASTER ||
                !user_range_ok(a2, sizeof(int))) return -LEONOS_EINVAL;
            locked = *(int *)(uintptr_t)a2;
            if (locked != 0 && locked != 1) return -LEONOS_EINVAL;
            return pty_set_lock(pty_id, locked);
        }
        if (a1 == TIOCGPTLCK) {
            int locked;
            if (!endpoint || endpoint->endpoint != TASK_PTY_ENDPOINT_MASTER ||
                !user_range_ok(a2, sizeof(int))) return -LEONOS_EINVAL;
            int ret = pty_get_lock(pty_id, &locked);
            if (ret < 0) return ret;
            *(int *)(uintptr_t)a2 = locked;
            return 0;
        }
        if (a1 == TIOCGWINSZ || a1 == TIOCSWINSZ) {
            struct linux_winsize *size;
            struct leonos_pty_winsize native;
            if (!user_range_ok(a2, sizeof(*size))) return -LEONOS_EFAULT;
            size = (struct linux_winsize *)(uintptr_t)a2;
            if (a1 == TIOCGWINSZ) {
                int ret = pty_get_winsize(pty_id, &native);
                if (ret < 0) return ret;
                size->ws_row = native.ws_row;
                size->ws_col = native.ws_col;
                size->ws_xpixel = 0;
                size->ws_ypixel = 0;
                return 0;
            }
            native.ws_row = size->ws_row;
            native.ws_col = size->ws_col;
            return pty_set_winsize(pty_id, &native);
        }
        {
            struct linux_termios2 termios;
            int extended = a1 == LINUX_TCGETS2 || a1 == LINUX_TCSETS2 ||
                           a1 == LINUX_TCSETSW2 || a1 == LINUX_TCSETSF2;
            int get = a1 == TCGETS || a1 == LINUX_TCGETS2;
            size_t length = extended ? sizeof(termios) : sizeof(struct linux_termios);
            int ret = pty_get_termios(pty_id, &termios);
            if (ret < 0) return ret;
            uint8_t *user = (uint8_t *)(uintptr_t)a2;
            uint8_t *native = (uint8_t *)&termios;
            if (get) {
                if (!user_range_writable(a2, length)) return -LEONOS_EFAULT;
                for (size_t i = 0; i < length; ++i) user[i] = native[i];
                return 0;
            }
            if (!user_range_ok(a2, length)) return -LEONOS_EFAULT;
            for (size_t i = 0; i < length; ++i) native[i] = user[i];
            if (a1 == TCSETSF || a1 == LINUX_TCSETSF2) pty_flush_input(pty_id);
            return pty_set_termios(pty_id, &termios);
        }
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
    /* Linux ioctl's cmd is unsigned int, even when libc sign-extends an int. */
    if (number == LINUX_SYS_IOCTL) a1 = (uint32_t)a1;

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
    if (number == LINUX_SYS_SENDFILE || number == LINUX_SYS_COPY_FILE_RANGE ||
        number == LINUX_SYS_RECV || number == LINUX_SYS_RECVFROM ||
        number == LINUX_SYS_RECVMSG || number == LINUX_SYS_SEND ||
        number == LINUX_SYS_SENDTO || number == LINUX_SYS_SENDMSG ||
        number == LINUX_SYS_ACCEPT || number == LINUX_SYS_ACCEPT4 ||
        number == LINUX_SYS_CONNECT) {
        /* The dedicated socket calls report EAGAIN for genuinely nonblocking
         * descriptors; the rewind path below would otherwise park the caller
         * inside the kernel until data or space appears, silently disabling
         * every user-space poll/deadline loop built on these calls. */
        file = task_file_for_io(task, (int)fd);
        return file && (file->flags & LEONOS_O_NONBLOCK) ? 1 : 0;
    }
    if (number != LINUX_SYS_READ && number != LINUX_SYS_WRITE &&
        number != LINUX_SYS_READV && number != LINUX_SYS_WRITEV &&
        number != LINUX_SYS_PREADV && number != LINUX_SYS_PWRITEV &&
        number != LINUX_SYS_PREADV2 && number != LINUX_SYS_PWRITEV2) {
        return 0;
    }
    endpoint = task && task->syscall_file ? NULL : task_pty_endpoint_for_fd(task, (int)fd);
    if (endpoint) {
        return (endpoint->status_flags & LEONOS_O_NONBLOCK) != 0;
    }
    file = task_file_for_io(task, (int)fd);
    if (file && (file->flags & LEONOS_O_NONBLOCK)) {
        if (file->flags & (TASK_FILE_FLAG_PIPE |
                           TASK_FILE_FLAG_SOCKET_UNIX |
                           TASK_FILE_FLAG_SOCKET_INET |
                           TASK_FILE_FLAG_EVENTFD)) {
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
    /* Several storage and GUI services still own global scratch buffers. */
    kernel_execution_lock_irqsave(&lock_flags);
    number = frame->rax;
    struct task *calling_task = sched_current_task();
    if (calling_task && number != LINUX_SYS_RT_SIGRETURN) {
        calling_task->restart_syscall = 0;
        /* sigaltstack must inspect the live user SP, not the last timer frame. */
        calling_task->frame.rsp = frame->rsp;
    }
    bool file_io = number == LINUX_SYS_READ || number == LINUX_SYS_WRITE ||
        number == LINUX_SYS_READV || number == LINUX_SYS_WRITEV ||
        number == LINUX_SYS_PREADV || number == LINUX_SYS_PWRITEV ||
        number == LINUX_SYS_PREADV2 || number == LINUX_SYS_PWRITEV2 ||
        number == LINUX_SYS_SENDFILE || number == LINUX_SYS_COPY_FILE_RANGE ||
        number == LINUX_SYS_SENDTO || number == LINUX_SYS_RECVFROM ||
        number == LINUX_SYS_SENDMSG || number == LINUX_SYS_RECVMSG ||
        number == LINUX_SYS_ACCEPT || number == LINUX_SYS_ACCEPT4 || number == LINUX_SYS_CONNECT;
    int pin_error = 0;
    if (calling_task && number != LINUX_SYS_RT_SIGRETURN) {
        if (calling_task->syscall_file && (!file_io || calling_task->syscall_fd != (int32_t)frame->rdi ||
            calling_task->syscall_file_number != number)) task_release_syscall_file(calling_task);
        if (file_io && !calling_task->syscall_file) {
            struct task_file *file = task_file_for_fd(calling_task, (int32_t)frame->rdi);
            if (file) {
                calling_task->syscall_file = task_file_get(file);
                if (!calling_task->syscall_file) pin_error = -LEONOS_ENOMEM;
                calling_task->syscall_fd = (int32_t)frame->rdi;
                calling_task->syscall_file_number = (uint32_t)number;
            }
        }
    }
    storage_set_io_async_context(true);
    if (pin_error) {
        result = pin_error;
    } else if (number == LINUX_SYS_RT_SIGRETURN) {
        struct task *task = sched_current_task();
        result = task ? kernel_signal_rt_sigreturn(task, frame) : -LEONOS_EPERM;
        restored_signal_frame = result >= 0;
    } else if (number == LINUX_SYS_CLONE) {
        result = sched_clone_current(frame, frame->rdi, frame->rsi,
                                     frame->rdx, frame->r10, frame->r8);
    } else if (number == LINUX_SYS_CLONE3) {
        result = syscall_clone3(frame, frame->rdi, frame->rsi);
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
    if ((number == LINUX_SYS_FUTEX || number == LINUX_SYS_FUTEX_WAIT ||
         number == LINUX_SYS_FUTEX_REQUEUE || number == LINUX_SYS_CLONE ||
         number == LINUX_SYS_CLONE3) && result == -LEONOS_EAGAIN) {
        eagain_from_nonblocking = 1;
    }
    if (result == -LEONOS_EAGAIN && calling_task && calling_task->socket_io_timed &&
        calling_task->socket_io_deadline <= time_ticks()) eagain_from_nonblocking = 1;
    if (result == -LEONOS_EAGAIN &&
        (((number == LINUX_SYS_SENDMSG || number == LINUX_SYS_RECVMSG) && (frame->rdx & 0x40)) ||
         ((number == LINUX_SYS_SENDTO || number == LINUX_SYS_RECVFROM) && (frame->r10 & 0x40))))
        eagain_from_nonblocking = 1;
    if (result != -LEONOS_EAGAIN || eagain_from_nonblocking) {
        storage_release_task_io(sched_current_pid());
    }
    storage_set_io_async_context(false);
    if (restored_signal_frame) {
        /* The restored context already contains the interrupted syscall's
         * original rax; do not overwrite it with rt_sigreturn's return value. */
    } else if (result == KERNEL_SYSCALL_BLOCKED ||
               (result == -LEONOS_EAGAIN && !eagain_from_nonblocking)) {
        /* int $0x80 has advanced RIP by two bytes.  Park this task for one
         * timer tick and re-execute the exact same instruction when its AHCI
         * DMA request can be polled again.  User programs keep normal
         * blocking read/open/stat semantics and never observe EAGAIN. */
        frame->rax = number;
        frame->rip -= 2u;
        if (calling_task) calling_task->restart_syscall = number + 1;
        if (result != KERNEL_SYSCALL_BLOCKED)
            sched_sleep_current_until(time_ticks() + 1u);
        kernel_execution_unlock_irqrestore(lock_flags);
        return;
    }
    if (!restored_signal_frame) {
        frame->rax = (uint64_t)result;
    }
    task_release_syscall_file(calling_task);
    kernel_execution_unlock_irqrestore(lock_flags);
}
