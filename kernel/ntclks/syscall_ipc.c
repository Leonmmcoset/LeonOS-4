/*
 * LeonOS IPC syscall support: anonymous pipes and descriptor endpoints.
 */
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>
#include <ntclks/object.h>
#include <ntclks/heap.h>
#include <ntclks/wait.h>
#include <leonos/fs.h>

/* A 64-stage shell pipeline owns 63 pipes simultaneously.  Keep an extra
 * ring sentinel byte so the advertised 4096-byte capacity is usable. */
#define TASK_PIPE_MAX 256u
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
    struct kernel_wait_queue wait_read;
    struct kernel_wait_queue wait_write;
};

static struct task_pipe *task_pipes[TASK_PIPE_MAX];

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
        if (!pipe->writers) (void)kernel_wait_queue_wake_all(&pipe->wait_read);
    } else if (pipe->readers) {
        --pipe->readers;
        if (!pipe->readers) (void)kernel_wait_queue_wake_all(&pipe->wait_write);
    }
    if (!pipe->readers && !pipe->writers) {
        void *removed = NULL;
        (void)kernel_wait_queue_wake_all(&pipe->wait_read);
        (void)kernel_wait_queue_wake_all(&pipe->wait_write);
        kernel_object_remove(kernel_objects(), file->aux, KERNEL_OBJECT_PIPE, &removed);
        if (removed) {
            for (uint32_t i = 0; i < TASK_PIPE_MAX; ++i) {
                if (task_pipes[i] == (struct task_pipe *)removed) {
                    task_pipes[i] = NULL;
                    break;
                }
            }
            kernel_free(removed);
        }
    }
}

static int alloc_task_pipe_fd(struct task *task, uint32_t pipe_handle, int write_end)
{
    if (!task || !kernel_object_lookup(kernel_objects(), pipe_handle, KERNEL_OBJECT_PIPE))
        return -LEONOS_EINVAL;
    struct task_file *file;
    int fd = task_allocate_fd(task, 0, &file);
    if (fd < 0) return fd;
    file->flags = TASK_FILE_FLAG_PIPE | (write_end ? TASK_FILE_FLAG_PIPE_WRITE : 0) |
                  (write_end ? LEONOS_O_WRONLY : LEONOS_O_RDONLY);
    file->aux = pipe_handle;
    task_pipe_retain(file);
    return fd;
}

int task_pipe_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    uint32_t count = 0;
    if (!pipe || (file->flags & TASK_FILE_FLAG_PIPE_WRITE)) return -LEONOS_EBADF;
    if (length == 0) return 0;
    kernel_wait_queue_remove(&pipe->wait_read, sched_current_task());
    while (pipe->tail != pipe->head && count < length) {
        ((uint8_t *)buffer)[count++] = pipe->data[pipe->tail];
        pipe->tail = (pipe->tail + 1U) % TASK_PIPE_RING_CAP;
    }
    if (count) {
        (void)kernel_wait_queue_wake_all(&pipe->wait_write);
        return (int)count;
    }
    if (!pipe->writers) return 0;
    if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
    kernel_wait_queue_block_current(&pipe->wait_read);
    return -LEONOS_EAGAIN;
}

int task_pipe_write(struct task_file *file, const void *buffer, uint32_t length)
{
    struct task_pipe *pipe = task_pipe_for_file(file);
    uint32_t count = 0;
    if (!pipe || !(file->flags & TASK_FILE_FLAG_PIPE_WRITE)) return -LEONOS_EBADF;
    if (length == 0) return 0;
    if (!pipe->readers) {
        (void)sched_signal_user_task(sched_current_pid(), 13); /* SIGPIPE */
        return -LEONOS_EPIPE;
    }
    kernel_wait_queue_remove(&pipe->wait_write, sched_current_task());
    {
        uint32_t used = (pipe->head + TASK_PIPE_RING_CAP - pipe->tail) % TASK_PIPE_RING_CAP;
        uint32_t free_bytes = TASK_PIPE_CAP - used;
        /* Linux guarantees that writes up to PIPE_BUF are atomic. */
        if (length <= TASK_PIPE_CAP && free_bytes < length) {
            if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
            kernel_wait_queue_block_current(&pipe->wait_write);
            return -LEONOS_EAGAIN;
        }
    }
    while (count < length) {
        uint32_t next = (pipe->head + 1U) % TASK_PIPE_RING_CAP;
        if (next == pipe->tail) {
            if (count) {
                (void)kernel_wait_queue_wake_all(&pipe->wait_read);
                return (int)count;
            }
            if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
            kernel_wait_queue_block_current(&pipe->wait_write);
            return -LEONOS_EAGAIN;
        }
        pipe->data[pipe->head] = ((const uint8_t *)buffer)[count++];
        pipe->head = next;
    }
    (void)kernel_wait_queue_wake_all(&pipe->wait_read);
    return (int)count;
}

short task_pipe_poll(const struct task_file *file, short events)
{
    const struct task_pipe *pipe = task_pipe_for_file(file);
    short result = 0;
    if (!pipe) {
        return POLLNVAL;
    }
    if (file->flags & TASK_FILE_FLAG_PIPE_WRITE) {
        if (pipe->readers == 0) {
            result |= POLLERR;
        } else if (events & POLLOUT) {
            uint32_t next = (pipe->head + 1U) % TASK_PIPE_RING_CAP;
            if (next != pipe->tail) {
                result |= POLLOUT;
            }
        }
    } else {
        if (pipe->tail != pipe->head) {
            result |= POLLIN;
        } else if (pipe->writers == 0) {
            result |= POLLHUP;
        }
    }
    return result;
}

int syscall_ipc_pipe(uint64_t user_ptr)
{
    struct task *task = sched_current_task();
    int read_fd, write_fd;
    uint32_t pipe_index;
    uint32_t pipe_handle;
    if (!task || !user_range_writable(user_ptr, sizeof(int) * 2U)) {
        return -LEONOS_EFAULT;
    }
    for (pipe_index = 0; pipe_index < TASK_PIPE_MAX; ++pipe_index) {
        if (!task_pipes[pipe_index]) {
            break;
        }
    }
    if (pipe_index == TASK_PIPE_MAX) {
        return -LEONOS_EMFILE;
    }
    task_pipes[pipe_index] = (struct task_pipe *)kernel_malloc(sizeof(struct task_pipe));
    if (!task_pipes[pipe_index]) {
        return -LEONOS_ENOMEM;
    }
    *task_pipes[pipe_index] = (struct task_pipe){.used = 1};
    kernel_wait_queue_init(&task_pipes[pipe_index]->wait_read);
    kernel_wait_queue_init(&task_pipes[pipe_index]->wait_write);
    pipe_handle = kernel_object_insert(kernel_objects(), task_pipes[pipe_index],
                                       KERNEL_OBJECT_PIPE);
    if (!pipe_handle) {
        kernel_free(task_pipes[pipe_index]);
        task_pipes[pipe_index] = NULL;
        return -LEONOS_EMFILE;
    }
    read_fd = alloc_task_pipe_fd(task, pipe_handle, 0);
    write_fd = alloc_task_pipe_fd(task, pipe_handle, 1);
    if (read_fd < 0 || write_fd < 0) {
        if (read_fd >= 0) {
            task_discard_file_fd(task, read_fd);
        }
        if (write_fd >= 0) {
            task_discard_file_fd(task, write_fd);
        }
        kernel_object_remove(kernel_objects(), pipe_handle, KERNEL_OBJECT_PIPE, NULL);
        kernel_free(task_pipes[pipe_index]);
        task_pipes[pipe_index] = NULL;
        return -LEONOS_EMFILE;
    }
    ((int *)(uintptr_t)user_ptr)[0] = read_fd;
    ((int *)(uintptr_t)user_ptr)[1] = write_fd;
    return 0;
}

int syscall_ipc_pipe2(uint64_t user_ptr, uint64_t flags)
{
    flags = (uint32_t)flags;
    struct task *task = sched_current_task();
    int fds[2];
    struct task_file *read_file;
    struct task_file *write_file;
    if (flags & ~(uint32_t)(LEONOS_O_NONBLOCK | LEONOS_O_CLOEXEC)) {
        return -LEONOS_EINVAL;
    }
    if (!task || !user_range_writable(user_ptr, sizeof(fds))) return -LEONOS_EFAULT;
    int result = syscall_ipc_pipe(user_ptr);
    if (result < 0 || !task) return result;
    fds[0] = ((const int *)(uintptr_t)user_ptr)[0];
    fds[1] = ((const int *)(uintptr_t)user_ptr)[1];
    read_file = task_file_for_fd(task, fds[0]);
    write_file = task_file_for_fd(task, fds[1]);
    if (!read_file || !write_file) return -LEONOS_EBADF;
    if (flags & LEONOS_O_NONBLOCK) {
        read_file->flags |= LEONOS_O_NONBLOCK;
        write_file->flags |= LEONOS_O_NONBLOCK;
    }
    if (flags & LEONOS_O_CLOEXEC) {
        read_file->fd_flags |= LEONOS_FD_CLOEXEC;
        write_file->fd_flags |= LEONOS_FD_CLOEXEC;
    }
    return 0;
}

int syscall_ipc_owns(uint64_t number)
{
    switch (number) {
    case LINUX_SYS_PIPE:
    case LINUX_SYS_PIPE2:
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
    if (number == LINUX_SYS_PIPE2) return syscall_ipc_pipe2(a0, a1);
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
