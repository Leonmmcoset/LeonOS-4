/*
 * LeonOS IPC syscall support: anonymous pipes and descriptor endpoints.
 */
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>
#include <ntclks/object.h>
#include <leonos/fs.h>

/* A 64-stage shell pipeline owns 63 pipes simultaneously.  Keep an extra
 * ring sentinel byte so the advertised 4096-byte capacity is usable. */
#define TASK_PIPE_MAX 64u
#define TASK_PIPE_CAP 4096u
#define TASK_PIPE_RING_CAP (TASK_PIPE_CAP + 1u)

struct task_pipe {
    uint8_t used;
    uint8_t reserved[3];
    uint32_t readers;
    uint32_t writers;
    uint32_t head;
    uint32_t tail;
    uint8_t data[TASK_PIPE_RING_CAP];
};

static struct task_pipe task_pipes[TASK_PIPE_MAX];

static struct task_pipe *task_pipe_for_file(const struct task_file *file)
{
    if (!file || !(file->flags & TASK_FILE_FLAG_PIPE)) {
        return NULL;
    }
    return (struct task_pipe *)kernel_object_lookup(kernel_objects(), file->aux,
                                                    KERNEL_OBJECT_PIPE);
}

void task_pipe_retain(struct task_file *file)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    if (!pipe) return;
    if (file->flags & TASK_FILE_FLAG_PIPE_WRITE) ++pipe->writers;
    else ++pipe->readers;
}

void task_pipe_release(struct task_file *file)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    if (!pipe) return;
    if (file->flags & TASK_FILE_FLAG_PIPE_WRITE) {
        if (pipe->writers) --pipe->writers;
    } else if (pipe->readers) {
        --pipe->readers;
    }
    if (!pipe->readers && !pipe->writers) {
        void *removed = NULL;
        kernel_object_remove(kernel_objects(), file->aux, KERNEL_OBJECT_PIPE, &removed);
        if (removed) {
            *(struct task_pipe *)removed = (struct task_pipe){0};
        }
    }
}

static int alloc_task_pipe_fd(struct task *task, uint32_t pipe_handle, int write_end)
{
    struct task_file *file;
    int fd;
    if (!task || !kernel_object_lookup(kernel_objects(), pipe_handle,
                                       KERNEL_OBJECT_PIPE)) {
        return -LEONOS_EINVAL;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    for (uint32_t i = 0; i < SCHED_TASK_FILE_MAX; ++i) {
        fd = (int)i + 4;
        if (task->files[i].used || task_pty_fd_for_fd(task, fd)) continue;
        file = &task->files[i];
        file->used = 1;
        file->flags = TASK_FILE_FLAG_PIPE | (write_end ? TASK_FILE_FLAG_PIPE_WRITE : 0) |
                      (write_end ? LEONOS_O_WRONLY : LEONOS_O_RDONLY);
        file->fd_flags = 0;
        file->aux = pipe_handle;
        file->path[0] = 0;
        task_pipe_retain(file);
        return fd;
    }
    return -LEONOS_EMFILE;
}

int task_pipe_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    uint32_t count = 0;
    if (!pipe || (file->flags & TASK_FILE_FLAG_PIPE_WRITE)) return -LEONOS_EBADF;
    if (length == 0) return 0;
    while (pipe->tail != pipe->head && count < length) {
        ((uint8_t *)buffer)[count++] = pipe->data[pipe->tail];
        pipe->tail = (pipe->tail + 1U) % TASK_PIPE_RING_CAP;
    }
    return count ? (int)count : (pipe->writers ? -LEONOS_EAGAIN : 0);
}

int task_pipe_write(struct task_file *file, const void *buffer, uint32_t length)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    uint32_t count = 0;
    if (!pipe || !(file->flags & TASK_FILE_FLAG_PIPE_WRITE)) return -LEONOS_EBADF;
    if (length == 0) return 0;
    if (!pipe->readers) return -LEONOS_EPIPE;
    while (count < length) {
        uint32_t next = (pipe->head + 1U) % TASK_PIPE_RING_CAP;
        if (next == pipe->tail) {
            return count ? (int)count : -LEONOS_EAGAIN;
        }
        pipe->data[pipe->head] = ((const uint8_t *)buffer)[count++];
        pipe->head = next;
    }
    return (int)count;
}

int syscall_ipc_pipe(uint64_t user_ptr)
{
    struct task *task = sched_current_task();
    int read_fd, write_fd;
    uint32_t pipe_index;
    uint32_t pipe_handle;
    if (!task || !user_range_ok(user_ptr, sizeof(int) * 2U)) {
        return -LEONOS_EFAULT;
    }
    for (pipe_index = 0; pipe_index < TASK_PIPE_MAX; ++pipe_index) {
        if (!task_pipes[pipe_index].used) {
            break;
        }
    }
    if (pipe_index == TASK_PIPE_MAX) {
        return -LEONOS_EMFILE;
    }
    task_pipes[pipe_index] = (struct task_pipe){.used = 1};
    pipe_handle = kernel_object_insert(kernel_objects(), &task_pipes[pipe_index],
                                       KERNEL_OBJECT_PIPE);
    if (!pipe_handle) {
        task_pipes[pipe_index] = (struct task_pipe){0};
        return -LEONOS_EMFILE;
    }
    read_fd = alloc_task_pipe_fd(task, pipe_handle, 0);
    write_fd = alloc_task_pipe_fd(task, pipe_handle, 1);
    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) {
            clear_task_file(task_file_for_fd(task, read_fd));
        }
        if (write_fd >= 0) {
            clear_task_file(task_file_for_fd(task, write_fd));
        }
        kernel_object_remove(kernel_objects(), pipe_handle, KERNEL_OBJECT_PIPE, NULL);
        task_pipes[pipe_index] = (struct task_pipe){0};
        return -LEONOS_EMFILE;
    }
    ((int *)(uintptr_t)user_ptr)[0] = read_fd;
    ((int *)(uintptr_t)user_ptr)[1] = write_fd;
    return 0;
}

int syscall_ipc_owns(uint64_t number)
{
    switch (number) {
    case LINUX_SYS_PIPE:
    case LINUX_SYS_DUP:
    case LINUX_SYS_DUP2:
    case LINUX_SYS_FORK:
    case LINUX_SYS_VFORK:
    case LINUX_SYS_EXECVE:
    case LINUX_SYS_EXIT:
    case LINUX_SYS_WAIT4:
        return 1;
    default:
        return 0;
    }
}

int64_t syscall_ipc_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
