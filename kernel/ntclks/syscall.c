#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/gui_ipc.h>
#include <ntclks/input.h>
#include <ntclks/osmlayer.h>
#include <ntclks/pty.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/userland.h>

#include <leonos/fs.h>
#include <leonos/pty.h>

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
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        struct task *task = sched_current_task();
        if (task && task->pty_id && (a0 == 1 || a0 == 2)) {
            return pty_write_output(task->pty_id, (const char *)(uintptr_t)a1, (uint32_t)a2);
        }
        if (a0 != 1 && a0 != 2) {
            return -LEONOS_EINVAL;
        }
        console_write_len((const char *)(uintptr_t)a1, (size_t)a2);
        return (int64_t)a2;
    }

    if (number == LINUX_SYS_READ) {
        struct task *task = sched_current_task();
        if (!user_range_ok(a1, a2)) {
            return -LEONOS_EFAULT;
        }
        if (!task || !task->pty_id || a0 != 0) {
            return 0;
        }
        return pty_read_input(task->pty_id, (char *)(uintptr_t)a1, (uint32_t)a2);
    }

    if (number == LINUX_SYS_EXIT) {
        gui_ipc_destroy_owner(sched_current_pid());
        pty_process_exit(sched_current_pid());
        userland_process_exit(a0);
        return 0;
    }

    if (number == LINUX_SYS_EXECVE) {
        if (!user_range_ok(a0, 1)) {
            return -LEONOS_EFAULT;
        }
        size_t len = user_strlen((const char *)(uintptr_t)a0, 160);
        if (len == 160 || !user_range_ok(a0, len + 1)) {
            return -LEONOS_EFAULT;
        }
        int64_t pid = userland_spawn_path((const char *)(uintptr_t)a0);
        if (pid == -2) {
            return -LEONOS_ENOENT;
        }
        if (pid == -12) {
            return -LEONOS_ENOMEM;
        }
        return pid;
    }

    if (number == LINUX_SYS_GETPID) {
        return (int64_t)sched_current_pid();
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
        return gui_ipc_create_window(sched_current_pid(), cmd->width, cmd->height, cmd->title, cmd->text);
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
        if (snap->capacity > 32) {
            snap->capacity = 32;
        }
        if (snap->capacity && !user_range_ok((uint64_t)(uintptr_t)snap->tasks,
                                             (uint64_t)snap->capacity * sizeof(struct task_snapshot_info))) {
            return -LEONOS_EFAULT;
        }
        snap->count = sched_snapshot(snap->tasks, snap->capacity, &snap->tick);
        return (int64_t)snap->count;
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
        normalize_dir_path(path);
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
        int64_t pid = userland_spawn_path_with_pty(spawn->path, spawn->pty_id);
        if (pid == -2) {
            return -LEONOS_ENOENT;
        }
        if (pid == -12) {
            return -LEONOS_ENOMEM;
        }
        return pid;
    }

    if (number == LINUX_SYS_CHDIR || number == LINUX_SYS_CLOSE || number == LINUX_SYS_GETCWD) {
        return 0;
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
