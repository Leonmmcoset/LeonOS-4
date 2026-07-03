#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/input.h>
#include <ntclks/osmlayer.h>
#include <ntclks/power.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/userland.h>
#include <ntclks/version.h>

#include <leonos/fs.h>
#include <leonos/pty.h>
#include <leonos/system.h>
#include <leonos/text.h>

#define LEONOS_GUI_IOCTL_EVENT 0x4c455654ULL
#define LEONOS_GUI_IOCTL_UPTIME_MS 0x4c555054ULL
#define LEONOS_GUI_IOCTL_FB_INFO 0x4c464249ULL
#define LEONOS_GUI_IOCTL_FB_FILL 0x4c464246ULL
#define LEONOS_GUI_IOCTL_FB_RECT 0x4c464252ULL
#define LEONOS_GUI_IOCTL_FB_TEXT 0x4c464254ULL
#define LEONOS_GUI_IOCTL_FB_PIXEL 0x4c464250ULL
#define LEONOS_GUI_IOCTL_FB_BLIT 0x4c46424cULL
#define LEONOS_GUI_IOCTL_CREATE_WINDOW 0x4c475743ULL
#define LEONOS_GUI_IOCTL_POLL_WINDOW 0x4c475750ULL
#define LEONOS_GUI_IOCTL_TASKS 0x4c54534bULL
#define LEONOS_GUI_IOCTL_PRESENT_WINDOW 0x4c475046ULL
#define LEONOS_GUI_IOCTL_FETCH_WINDOW 0x4c475746ULL
#define LEONOS_GUI_IOCTL_WINDOW_EVENT 0x4c475745ULL
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

struct task_snapshot_user {
    uint32_t capacity;
    uint32_t count;
    uint64_t tick;
    struct task_snapshot_info *tasks;
};

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

struct exec_params_kernel {
    uint32_t argc;
    uint32_t envc;
    char *argv[SCHED_EXEC_ARG_MAX + 1];
    char *envp[SCHED_EXEC_ENV_MAX + 1];
    char data[SCHED_EXEC_DATA_MAX];
    uint32_t data_len;
};

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

static void clear_task_file(struct task_file *file)
{
    if (!file) {
        return;
    }
    file->used = 0;
    file->node.type = 0;
    file->node.flags = 0;
    file->node.first_cluster = 0;
    file->node.drive = 0;
    file->node.size = 0;
    file->offset = 0;
    file->aux = 0;
    file->flags = 0;
    file->path[0] = 0;
}

static void clear_task_files(struct task *task)
{
    if (!task) {
        return;
    }
    for (uint32_t i = 0; i < SCHED_TASK_FILE_MAX; ++i) {
        clear_task_file(&task->files[i]);
    }
}

static struct task_file *task_file_for_fd(struct task *task, int fd)
{
    if (!task || fd < 4 || fd >= 4 + (int)SCHED_TASK_FILE_MAX) {
        return NULL;
    }
    struct task_file *file = &task->files[fd - 4];
    return file->used ? file : NULL;
}

static int alloc_task_fd(struct task *task, const struct storage_node *node, uint32_t flags, const char *path)
{
    if (!task || !node) {
        return -LEONOS_EINVAL;
    }
    for (uint32_t i = 0; i < SCHED_TASK_FILE_MAX; ++i) {
        if (task->files[i].used) {
            continue;
        }
        task->files[i].used = 1;
        task->files[i].node = *node;
        task->files[i].offset = 0;
        task->files[i].aux = 0;
        task->files[i].flags = flags;
        copy_text(task->files[i].path, sizeof(task->files[i].path), path);
        return (int)i + 4;
    }
    return -LEONOS_EMFILE;
}

static int file_can_read(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_RDONLY || acc == LEONOS_O_RDWR;
}

static int file_can_write(const struct task_file *file)
{
    uint32_t acc = file ? (file->flags & LEONOS_O_ACCMODE) : LEONOS_O_RDONLY;
    return acc == LEONOS_O_WRONLY || acc == LEONOS_O_RDWR;
}

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

static int storage_errno(int ret)
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

static int stat_for_fd(int fd, struct task *task, struct leonos_stat *st)
{
    if (!st) {
        return -LEONOS_EFAULT;
    }
    if (fd >= 0 && fd <= 3) {
        st->type = LEONOS_FS_TYPE_DEVICE;
        st->reserved = 0;
        st->size = 0;
        return 0;
    }
    struct task_file *file = task_file_for_fd(task, fd);
    if (!file) {
        return -LEONOS_EBADF;
    }
    st->type = file->node.type;
    st->reserved = 0;
    st->size = file->node.size;
    return 0;
}

void syscall_init(void)
{
    console_printf("[ntclks] Linux x86_64 syscall ABI registered\n");
}

int64_t syscall_dispatch(const struct syscall_frame *frame)
{
    if (!frame) {
        return -LEONOS_EFAULT;
    }

    switch (frame->number) {
    case LINUX_SYS_WRITE:
    case LINUX_SYS_READ:
    case LINUX_SYS_OPEN:
    case LINUX_SYS_CLOSE:
    case LINUX_SYS_STAT:
    case LINUX_SYS_FSTAT:
    case LINUX_SYS_LSEEK:
    case LINUX_SYS_GETCWD:
    case LINUX_SYS_CHDIR:
    case LINUX_SYS_EXECVE:
    case LINUX_SYS_WAIT4:
    case LINUX_SYS_EXIT:
    case LINUX_SYS_NANOSLEEP:
    case LINUX_SYS_MMAP:
    case LINUX_SYS_MUNMAP:
    case LINUX_SYS_IOCTL:
    case LINUX_SYS_GETPID:
        return osmlayer_bridge_syscall(frame);
    default:
        return -LEONOS_ENOSYS;
    }
}

static int64_t syscall_dispatch_regs(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3;
    (void)a4;
    (void)a5;

    if (number == LINUX_SYS_WRITE) {
        struct task *task = sched_current_task();
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        if (task && task->pty_id && (a0 == 1 || a0 == 2)) {
            return pty_write_output(task->pty_id, (const char *)(uintptr_t)a1, (uint32_t)a2);
        }
        if (a0 == 1 || a0 == 2) {
            console_write_len((const char *)(uintptr_t)a1, (size_t)a2);
            return (int64_t)a2;
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
        if (file->flags & LEONOS_O_APPEND) {
            file->offset = file->node.size;
        }
        ret = storage_write_node(file->path, file->offset,
                                 (const void *)(uintptr_t)a1, (uint32_t)a2, &wrote);
        if (ret < 0) {
            return ret;
        }
        file->offset += wrote;
        file->node.size = file->offset > file->node.size ? file->offset : file->node.size;
        return (int64_t)wrote;
    }

    if (number == LINUX_SYS_READ) {
        struct task *task = sched_current_task();
        struct task_file *file;
        uint32_t got = 0;
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        if (task && task->pty_id && a0 == 0) {
            return pty_read_input(task->pty_id, (char *)(uintptr_t)a1, (uint32_t)a2);
        }
        file = task_file_for_fd(task, (int)a0);
        if (!file) {
            return -LEONOS_EBADF;
        }
        if (file->node.type == LEONOS_FS_TYPE_DIR) {
            struct leonos_dir_entry entry;
            int step = storage_readdir_node(&file->node, &file->offset, &entry);
            if (step == 0 && (file->node.flags & STORAGE_NODE_FLAG_ROOT) && file->aux == 0) {
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
        if (storage_read_node(&file->node, file->offset, (void *)(uintptr_t)a1, (uint32_t)a2, &got) < 0) {
            return -LEONOS_EINVAL;
        }
        file->offset += got;
        return (int64_t)got;
    }

    if (number == LINUX_SYS_EXIT) {
        clear_task_files(sched_current_task());
        gui_ipc_destroy_owner(sched_current_pid());
        pty_process_exit(sched_current_pid());
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
        int64_t pid = userland_spawn_path_argv(path,
                                               (const char *const *)params.argv,
                                               (const char *const *)params.envp,
                                               0);
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

    if (number == LINUX_SYS_OPEN) {
        struct task *task = sched_current_task();
        struct storage_node node;
        char path[LEONOS_FS_PATH_LEN];
        uint32_t flags = (uint32_t)a1;
        int ret = resolve_user_path(task, a0, path, sizeof(path));
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
        if (fd >= 0 && (flags & LEONOS_O_APPEND)) {
            struct task_file *file = task_file_for_fd(task, fd);
            if (file && file->node.type == LEONOS_FS_TYPE_FILE) {
                file->offset = file->node.size;
            }
        }
        return fd;
    }

    if (number == LINUX_SYS_CLOSE) {
        struct task *task = sched_current_task();
        if (a0 <= 3) {
            return 0;
        }
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) {
            return -LEONOS_EBADF;
        }
        clear_task_file(file);
        return 0;
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
        ret = storage_mkdir(path);
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_UNLINK) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = storage_unlink(path);
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_RMDIR) {
        struct task *task = sched_current_task();
        char path[LEONOS_FS_PATH_LEN];
        int ret = resolve_user_path(task, a0, path, sizeof(path));
        if (ret < 0) {
            return ret;
        }
        ret = storage_rmdir(path);
        return ret < 0 ? storage_errno(ret) : 0;
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
        ret = storage_rename(old_path, new_path);
        return ret < 0 ? storage_errno(ret) : 0;
    }

    if (number == LINUX_SYS_GETPID) {
        return (int64_t)sched_current_pid();
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

    if (number == LINUX_SYS_WAIT4) {
        uint32_t wanted_pid = (uint32_t)a0;
        uint64_t code = 0;
        int64_t pid = sched_wait_reap(sched_current_pid(), wanted_pid, &code);
        if (pid <= 0) {
            return -LEONOS_ECHILD;
        }
        if (a1) {
            if (!user_range_ok(a1, sizeof(int))) {
                return -LEONOS_EFAULT;
            }
            int *status = (int *)(uintptr_t)a1;
            *status = (int)((code & 0xff) << 8);
        }
        return pid;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_EVENT) {
        struct input_event event;
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
        info->width = fb->width;
        info->height = fb->height;
        info->pitch = fb->pitch;
        info->bpp = fb->bpp;
        return fb->available ? 0 : -LEONOS_EINVAL;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_FILL) {
        framebuffer_clear((uint32_t)a2);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_RECT) {
        if (!user_range_ok(a2, sizeof(struct framebuffer_rect_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_rect_cmd *cmd = (const struct framebuffer_rect_cmd *)(uintptr_t)a2;
        framebuffer_rect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_TEXT) {
        if (!user_range_ok(a2, sizeof(struct framebuffer_text_cmd))) {
            return -LEONOS_EFAULT;
        }
        const struct framebuffer_text_cmd *cmd = (const struct framebuffer_text_cmd *)(uintptr_t)a2;
        size_t len = user_strlen(cmd->text, 160);
        if (len == 160 || !user_range_ok((uint64_t)(uintptr_t)cmd->text, len + 1)) {
            return -LEONOS_EFAULT;
        }
        framebuffer_text(cmd->x, cmd->y, cmd->text, cmd->fg, cmd->bg);
        return 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_PIXEL) {
        uint32_t x = (uint32_t)(a2 & 0xffffffffULL);
        uint32_t y = (uint32_t)(a2 >> 32);
        return (int64_t)framebuffer_get_pixel_public(x, y);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_FB_BLIT) {
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
        if (user_strlen(cmd->title, 47) == 47 || user_strlen(cmd->text, 95) == 95) {
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
        if (!user_range_ok(a2, sizeof(struct gui_ipc_window))) {
            return -LEONOS_EFAULT;
        }
        struct gui_ipc_window *dst = (struct gui_ipc_window *)(uintptr_t)a2;
        return gui_ipc_pop_window(dst) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_PRESENT_WINDOW) {
        if (!user_range_ok(a2, sizeof(struct gui_present_window_user))) {
            return -LEONOS_EFAULT;
        }
        const struct gui_present_window_user *cmd = (const struct gui_present_window_user *)(uintptr_t)a2;
        uint64_t bytes;
        if (!cmd->pixels || !cmd->width || !cmd->height || cmd->stride < cmd->width) {
            return -LEONOS_EINVAL;
        }
        bytes = (uint64_t)cmd->stride * cmd->height * sizeof(uint32_t);
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
        if (!user_range_ok(a2, sizeof(struct gui_fetch_window_user))) {
            return -LEONOS_EFAULT;
        }
        cmd = (struct gui_fetch_window_user *)(uintptr_t)a2;
        if (!cmd->pixels || !cmd->capacity_width || !cmd->capacity_height || cmd->stride < cmd->capacity_width) {
            return -LEONOS_EINVAL;
        }
        if (!user_range_ok((uint64_t)(uintptr_t)cmd->pixels,
                           (uint64_t)cmd->stride * cmd->capacity_height * sizeof(uint32_t))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_fetch_window(cmd->window_id,
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

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_WINDOW_EVENT) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_app_event))) {
            return -LEONOS_EFAULT;
        }
        struct gui_ipc_app_event *dst = (struct gui_ipc_app_event *)(uintptr_t)a2;
        return gui_ipc_pop_event(sched_current_pid(), dst->window_id, dst) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_SEND_WINDOW_EVENT) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_app_event))) {
            return -LEONOS_EFAULT;
        }
        const struct gui_ipc_app_event *src = (const struct gui_ipc_app_event *)(uintptr_t)a2;
        return gui_ipc_push_event(src->window_id, src) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_TASKS) {
        if (!user_range_ok(a2, sizeof(struct task_snapshot_user))) {
            return -LEONOS_EFAULT;
        }
        struct task_snapshot_user *snap = (struct task_snapshot_user *)(uintptr_t)a2;
        if (snap->capacity > SCHED_TASK_MAX) {
            snap->capacity = SCHED_TASK_MAX;
        }
        if (snap->capacity && !user_range_ok((uint64_t)(uintptr_t)snap->tasks,
                                             (uint64_t)snap->capacity * sizeof(struct task_snapshot_info))) {
            return -LEONOS_EFAULT;
        }
        snap->count = sched_snapshot(snap->tasks, snap->capacity, &snap->tick);
        return (int64_t)snap->count;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_TASK_KILL) {
        int ret = sched_kill_user_task((uint32_t)a2, 137);
        if (ret == -2) {
            return -LEONOS_ENOENT;
        }
        if (ret < 0) {
            return -LEONOS_EINVAL;
        }
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
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_request))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_pop_display_request((struct gui_ipc_display_request *)(uintptr_t)a2) ? 1 : 0;
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_GUI_IOCTL_PUBLISH_DISPLAY_STATE) {
        if (!user_range_ok(a2, sizeof(struct gui_ipc_display_state))) {
            return -LEONOS_EFAULT;
        }
        return gui_ipc_publish_display_state((const struct gui_ipc_display_state *)(uintptr_t)a2) ? 1 : 0;
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

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_INSTALL_IOCTL_FORMAT_ESP) {
        return storage_install_format_esp((uint32_t)a2);
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_INSTALL_IOCTL_MOUNT_TARGET) {
        return storage_install_mount_target((uint32_t)a2);
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

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_CREATE) {
        return pty_create(sched_current_pid());
    }

    if (number == LINUX_SYS_IOCTL && a1 == LEONOS_PTY_IOCTL_SELF) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->pty_id : 0;
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
        if (!pty_is_owner(spawn->pty_id, sched_current_pid())) {
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
        int64_t pid = userland_spawn_path_argv(path,
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

    struct syscall_frame frame = {
        .number = number,
        .args = {a0, a1, a2, a3, a4, a5},
    };
    return syscall_dispatch(&frame);
}

void syscall_dispatch_frame(struct trap_frame *frame)
{
    if (!frame) {
        return;
    }
    frame->rax = (uint64_t)syscall_dispatch_regs(frame->rax,
                                                 frame->rdi,
                                                 frame->rsi,
                                                 frame->rdx,
                                                 frame->r10,
                                                 frame->r8,
                                                 frame->r9);
}
