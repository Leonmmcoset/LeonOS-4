/*
 * LeonOS IPC syscall support: anonymous pipes and descriptor endpoints.
 */
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>
#include <ntclks/object.h>
#include <ntclks/heap.h>
#include <ntclks/pty.h>
#include <leonos/fs.h>
#include <leonos/socket.h>

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
};

static struct task_pipe *task_pipes[TASK_PIPE_MAX];

#define TASK_UNIX_SOCKET_MAX 96u
#define TASK_UNIX_SOCKET_RING_CAP 16385u
#define TASK_UNIX_SOCKET_ACCEPT_CAP 32u
#define TASK_UNIX_SOCKET_FD_CAP 16u

struct task_unix_socket_fd {
    uint8_t used;
    uint8_t reserved[3];
    /* Ring position where this descriptor accompanies the stream. */
    uint32_t offset;
    struct task_file file;
};

struct task_unix_socket {
    uint8_t used;
    uint8_t listening;
    uint8_t shutdown_read;
    uint8_t shutdown_write;
    uint32_t refs;
    uint32_t owner_uid;
    uint32_t peer_handle;
    uint32_t accept_handles[TASK_UNIX_SOCKET_ACCEPT_CAP];
    uint32_t accept_head;
    uint32_t accept_tail;
    uint8_t data[TASK_UNIX_SOCKET_RING_CAP];
    uint32_t data_head;
    uint32_t data_tail;
    struct task_unix_socket_fd passed_fds[TASK_UNIX_SOCKET_FD_CAP];
    char path[LEONOS_UNIX_PATH_MAX];
};

static struct task_unix_socket *task_unix_sockets[TASK_UNIX_SOCKET_MAX];

static uint32_t unix_ring_used(uint32_t head, uint32_t tail, uint32_t capacity)
{
    return head >= tail ? head - tail : capacity - tail + head;
}

static uint32_t unix_cmsg_align(uint32_t length)
{
    return (length + sizeof(uint64_t) - 1u) & ~(sizeof(uint64_t) - 1u);
}

static uint32_t unix_cmsg_space(uint32_t length)
{
    return unix_cmsg_align(sizeof(struct leonos_cmsghdr)) + unix_cmsg_align(length);
}

static uint32_t unix_cmsg_len(uint32_t length)
{
    return unix_cmsg_align(sizeof(struct leonos_cmsghdr)) + length;
}

static struct task_unix_socket *task_unix_socket_for_file(const struct task_file *file)
{
    if (!file || !(file->flags & TASK_FILE_FLAG_UNIX_SOCKET)) {
        return NULL;
    }
    return (struct task_unix_socket *)kernel_object_lookup(kernel_objects(), file->aux,
                                                           KERNEL_OBJECT_SOCKET);
}

static struct task_unix_socket *task_unix_socket_for_handle(uint32_t handle)
{
    return (struct task_unix_socket *)kernel_object_lookup(kernel_objects(), handle,
                                                           KERNEL_OBJECT_SOCKET);
}

static void unix_copy_path(char *destination, const char *source)
{
    uint32_t index = 0;
    if (!destination) return;
    if (source) {
        while (source[index] && index + 1u < LEONOS_UNIX_PATH_MAX) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = 0;
}

static int unix_path_equal(const char *left, const char *right)
{
    uint32_t index = 0;
    if (!left || !right) return 0;
    while (left[index] == right[index]) {
        if (!left[index]) return 1;
        ++index;
    }
    return 0;
}

static uint32_t task_unix_socket_create(uint32_t uid)
{
    uint32_t index;
    uint32_t handle;
    struct task_unix_socket *socket;
    for (index = 0; index < TASK_UNIX_SOCKET_MAX; ++index) {
        if (!task_unix_sockets[index]) break;
    }
    if (index == TASK_UNIX_SOCKET_MAX) return 0;
    socket = (struct task_unix_socket *)kernel_malloc(sizeof(*socket));
    if (!socket) return 0;
    for (uint32_t i = 0; i < sizeof(*socket); ++i) ((uint8_t *)socket)[i] = 0;
    socket->used = 1;
    socket->owner_uid = uid;
    handle = kernel_object_insert(kernel_objects(), socket, KERNEL_OBJECT_SOCKET);
    if (!handle) {
        kernel_free(socket);
        return 0;
    }
    task_unix_sockets[index] = socket;
    return handle;
}

static void task_unix_socket_destroy(uint32_t handle, struct task_unix_socket *socket)
{
    void *removed = NULL;
    if (!socket) return;
    if (socket->peer_handle) {
        struct task_unix_socket *peer = task_unix_socket_for_handle(socket->peer_handle);
        if (peer && peer->peer_handle == handle) peer->peer_handle = 0;
    }
    while (socket->accept_tail != socket->accept_head) {
        uint32_t child_handle = socket->accept_handles[socket->accept_tail];
        struct task_unix_socket *child;
        socket->accept_tail = (socket->accept_tail + 1u) % TASK_UNIX_SOCKET_ACCEPT_CAP;
        child = task_unix_socket_for_handle(child_handle);
        if (child && child->refs) {
            --child->refs; /* Drop the listener queue's ownership. */
            if (!child->refs) task_unix_socket_destroy(child_handle, child);
        }
    }
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_FD_CAP; ++i) {
        if (socket->passed_fds[i].used) {
            clear_task_file(&socket->passed_fds[i].file);
            socket->passed_fds[i].used = 0;
        }
    }
    (void)kernel_object_remove(kernel_objects(), handle, KERNEL_OBJECT_SOCKET, &removed);
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_MAX; ++i) {
        if (task_unix_sockets[i] == socket) {
            task_unix_sockets[i] = NULL;
            break;
        }
    }
    if (removed) kernel_free(removed);
}

void task_unix_socket_retain(struct task_file *file)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    if (socket) ++socket->refs;
}

void task_unix_socket_release(struct task_file *file)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    uint32_t handle;
    if (!socket || !file) return;
    handle = (uint32_t)file->aux;
    if (socket->refs) --socket->refs;
    if (!socket->refs) task_unix_socket_destroy(handle, socket);
}

static int unix_socket_copy_address(uint64_t user_address, uint64_t address_length,
                                    struct leonos_sockaddr_un *address)
{
    if (!address || address_length < sizeof(uint16_t) ||
        address_length > sizeof(*address) ||
        !user_range_ok(user_address, address_length)) {
        return -LEONOS_EINVAL;
    }
    for (uint32_t i = 0; i < sizeof(*address); ++i) ((uint8_t *)address)[i] = 0;
    for (uint32_t i = 0; i < address_length; ++i) {
        ((uint8_t *)address)[i] = ((const uint8_t *)(uintptr_t)user_address)[i];
    }
    address->sun_path[LEONOS_UNIX_PATH_MAX - 1u] = 0;
    if (address->sun_family != LEONOS_AF_UNIX || !address->sun_path[0]) {
        return -LEONOS_EINVAL;
    }
    return 0;
}

static struct task_unix_socket *unix_listener_for_path(const char *path)
{
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_MAX; ++i) {
        struct task_unix_socket *socket = task_unix_sockets[i];
        if (socket && socket->listening && socket->path[0] &&
            unix_path_equal(socket->path, path)) return socket;
    }
    return NULL;
}

static int unix_socket_accept_ready(const struct task_unix_socket *socket)
{
    return socket && socket->listening && socket->accept_tail != socket->accept_head;
}

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
    struct task_file *file;
    int fd;
    if (!task || !kernel_object_lookup(kernel_objects(), pipe_handle,
                                       KERNEL_OBJECT_PIPE)) {
        return -LEONOS_EINVAL;
    }
    if (!task_can_allocate_fd(task)) {
        return -LEONOS_EMFILE;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(task); ++i) {
        fd = (int)i + 4;
        file = sched_task_file_at(task, i);
        if (!file || file->used || task_pty_fd_for_fd(task, fd)) continue;
        file->used = 1;
        file->flags = TASK_FILE_FLAG_PIPE | (write_end ? TASK_FILE_FLAG_PIPE_WRITE : 0) |
                      (write_end ? LEONOS_O_WRONLY : LEONOS_O_RDONLY);
        file->fd_flags = 0;
        file->aux = pipe_handle;
        file->path[0] = 0;
        task_pipe_retain(file);
        return fd;
    }
    {
        uint32_t i = sched_task_file_capacity(task);
        fd = (int)i + 4;
        file = sched_task_file_at(task, i);
        if (file && !task_pty_fd_for_fd(task, fd)) {
            file->used = 1;
            file->flags = TASK_FILE_FLAG_PIPE | (write_end ? TASK_FILE_FLAG_PIPE_WRITE : 0) |
                          (write_end ? LEONOS_O_WRONLY : LEONOS_O_RDONLY);
            file->fd_flags = 0;
            file->aux = pipe_handle;
            file->path[0] = 0;
            task_pipe_retain(file);
            return fd;
        }
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

int task_pipe_poll(const struct task_file *file, int16_t events)
{
    const struct task_pipe *pipe = task_pipe_for_file(file);
    int ready = 0;
    if (!pipe || !file) return LEONOS_POLLNVAL;
    if (file->flags & TASK_FILE_FLAG_PIPE_WRITE) {
        if (events & LEONOS_POLLOUT) {
            uint32_t next = (pipe->head + 1u) % TASK_PIPE_RING_CAP;
            if (pipe->readers && next != pipe->tail) ready |= LEONOS_POLLOUT;
        }
        if (!pipe->readers) ready |= LEONOS_POLLERR | LEONOS_POLLHUP;
    } else {
        if ((events & (LEONOS_POLLIN | LEONOS_POLLPRI)) && pipe->tail != pipe->head) {
            ready |= LEONOS_POLLIN;
        }
        if (!pipe->writers) ready |= LEONOS_POLLHUP;
    }
    return ready;
}

int task_unix_socket_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    uint32_t count = 0;
    if (!socket || !buffer) return -LEONOS_EBADF;
    if (socket->listening || socket->shutdown_read) return -LEONOS_EBADF;
    if (length == 0) return 0;
    while (socket->data_tail != socket->data_head && count < length) {
        ((uint8_t *)buffer)[count++] = socket->data[socket->data_tail];
        socket->data_tail = (socket->data_tail + 1u) % TASK_UNIX_SOCKET_RING_CAP;
    }
    if (count) return (int)count;
    return socket->peer_handle ? -LEONOS_EAGAIN : 0;
}

int task_unix_socket_write(struct task_file *file, const void *buffer, uint32_t length)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    struct task_unix_socket *peer;
    uint32_t count = 0;
    if (!socket || !buffer) return -LEONOS_EBADF;
    if (socket->listening || socket->shutdown_write) return -LEONOS_EPIPE;
    peer = task_unix_socket_for_handle(socket->peer_handle);
    if (!peer || peer->shutdown_read) return -LEONOS_EPIPE;
    while (count < length) {
        uint32_t next = (peer->data_head + 1u) % TASK_UNIX_SOCKET_RING_CAP;
        if (next == peer->data_tail) return count ? (int)count : -LEONOS_EAGAIN;
        peer->data[peer->data_head] = ((const uint8_t *)buffer)[count++];
        peer->data_head = next;
    }
    return (int)count;
}

static struct task_unix_socket_fd *unix_socket_transfer_at(
    struct task_unix_socket *socket, uint32_t offset)
{
    if (!socket) return NULL;
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_FD_CAP; ++i) {
        if (socket->passed_fds[i].used && socket->passed_fds[i].offset == offset) {
            return &socket->passed_fds[i];
        }
    }
    return NULL;
}

static struct task_unix_socket_fd *unix_socket_transfer_free(
    struct task_unix_socket *socket)
{
    if (!socket) return NULL;
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_FD_CAP; ++i) {
        if (!socket->passed_fds[i].used) return &socket->passed_fds[i];
    }
    return NULL;
}

static uint32_t unix_socket_limit_before_transfer(const struct task_unix_socket *socket,
                                                  uint32_t maximum)
{
    uint32_t limit = maximum;
    if (!socket) return 0;
    for (uint32_t i = 0; i < TASK_UNIX_SOCKET_FD_CAP; ++i) {
        const struct task_unix_socket_fd *entry = &socket->passed_fds[i];
        uint32_t distance;
        if (!entry->used) continue;
        distance = unix_ring_used(entry->offset, socket->data_tail,
                                  TASK_UNIX_SOCKET_RING_CAP);
        if (distance && distance < limit) limit = distance;
    }
    return limit;
}

static void unix_socket_retain_file(struct task_file *file)
{
    if (!file) return;
    task_pipe_retain(file);
    task_unix_socket_retain(file);
    if (file->flags & TASK_FILE_FLAG_PTY_MASTER) {
        (void)pty_master_retain((uint32_t)file->aux);
    }
}

static int unix_socket_capture_fd(struct task *task, int fd, struct task_file *out)
{
    struct task_file *source;
    struct task_pty_fd *pty_fd;
    if (!task || !out || fd < 0) return -LEONOS_EBADF;
    *out = (struct task_file){0};
    source = task_file_for_fd(task, fd);
    if (source && source->used) {
        *out = *source;
        out->fd_flags = 0;
        unix_socket_retain_file(out);
        return 0;
    }
    if (fd >= 0 && fd <= 2 && task->pty_id &&
        (task->closed_stdio_mask & (1u << (uint32_t)fd)) == 0) {
        out->used = 1;
        out->flags = TASK_FILE_FLAG_PTY_SLAVE | LEONOS_O_RDWR;
        out->aux = task->pty_id;
        return 0;
    }
    pty_fd = task_pty_fd_for_fd(task, fd);
    if (pty_fd && task->pty_id) {
        out->used = 1;
        out->flags = TASK_FILE_FLAG_PTY_SLAVE | LEONOS_O_RDWR;
        out->aux = task->pty_id;
        return 0;
    }
    return -LEONOS_EBADF;
}

static int unix_socket_install_fd(struct task *task, const struct task_file *source)
{
    struct task_file *destination;
    int fd;
    if (!task || !source || !source->used) return -LEONOS_EBADF;
    fd = task_allocate_anon_fd(task, 0, 0, "");
    if (fd < 0) return fd;
    destination = task_file_for_fd(task, fd);
    if (!destination) return -LEONOS_EMFILE;
    *destination = *source;
    destination->used = 1;
    destination->fd_flags = 0;
    unix_socket_retain_file(destination);
    return fd;
}

static int unix_socket_write_with_fd(struct task *task, struct task_file *file,
                                     const void *buffer, uint32_t length, int passed_fd)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    struct task_unix_socket *peer;
    struct task_unix_socket_fd *entry;
    struct task_file captured = {0};
    uint32_t offset;
    int result;
    if (passed_fd < 0) return task_unix_socket_write(file, buffer, length);
    if (!socket || socket->listening || socket->shutdown_write) return -LEONOS_EPIPE;
    peer = task_unix_socket_for_handle(socket->peer_handle);
    if (!peer || peer->shutdown_read) return -LEONOS_EPIPE;
    entry = unix_socket_transfer_free(peer);
    if (!entry) return -LEONOS_EAGAIN;
    result = unix_socket_capture_fd(task, passed_fd, &captured);
    if (result < 0) return result;
    offset = peer->data_head;
    result = task_unix_socket_write(file, buffer, length);
    if (result > 0) {
        *entry = (struct task_unix_socket_fd){
            .used = 1,
            .offset = offset,
            .file = captured,
        };
        return result;
    }
    clear_task_file(&captured);
    return result;
}

static int unix_socket_sendmsg(struct task *task, struct task_file *file,
                                uint64_t user_message)
{
    struct leonos_msghdr message;
    const struct leonos_iovec *iov;
    const struct leonos_cmsghdr *cmsg;
    uint8_t *buffer;
    uint64_t total = 0;
    uint32_t send_length;
    uint32_t offset = 0;
    int passed_fd = -1;
    int result;
    if (!file || !user_range_ok(user_message, sizeof(message))) return -LEONOS_EFAULT;
    message = *(const struct leonos_msghdr *)(uintptr_t)user_message;
    if (message.msg_iovlen > LEONOS_IOV_MAX ||
        (message.msg_iovlen && !user_range_ok(message.msg_iov,
                                               message.msg_iovlen * sizeof(*iov)))) {
        return -LEONOS_EINVAL;
    }
    iov = (const struct leonos_iovec *)(uintptr_t)message.msg_iov;
    for (uint64_t i = 0; i < message.msg_iovlen; ++i) {
        if (iov[i].iov_len > 0xffffffffULL - total ||
            (iov[i].iov_len && !user_range_ok(iov[i].iov_base, iov[i].iov_len))) {
            return -LEONOS_EINVAL;
        }
        total += iov[i].iov_len;
    }
    if (!total) return 0;
    if (message.msg_controllen) {
        if (message.msg_controllen < unix_cmsg_space(sizeof(int)) ||
            !user_range_ok(message.msg_control, message.msg_controllen)) {
            return -LEONOS_EINVAL;
        }
        cmsg = (const struct leonos_cmsghdr *)(uintptr_t)message.msg_control;
        if (cmsg->cmsg_len < unix_cmsg_len(sizeof(int)) ||
            cmsg->cmsg_len > message.msg_controllen ||
            cmsg->cmsg_level != LEONOS_SOL_SOCKET ||
            cmsg->cmsg_type != LEONOS_SCM_RIGHTS) {
            return -LEONOS_EINVAL;
        }
        passed_fd = *(const int *)(uintptr_t)(message.msg_control +
                    unix_cmsg_align(sizeof(*cmsg)));
    }
    send_length = total > TASK_UNIX_SOCKET_RING_CAP - 1u ?
                  TASK_UNIX_SOCKET_RING_CAP - 1u : (uint32_t)total;
    buffer = (uint8_t *)kernel_malloc(send_length);
    if (!buffer) return -LEONOS_ENOMEM;
    for (uint64_t i = 0; i < message.msg_iovlen && offset < send_length; ++i) {
        uint32_t chunk = iov[i].iov_len < send_length - offset ?
                         (uint32_t)iov[i].iov_len : send_length - offset;
        for (uint32_t j = 0; j < chunk; ++j) {
            buffer[offset + j] = ((const uint8_t *)(uintptr_t)iov[i].iov_base)[j];
        }
        offset += chunk;
    }
    result = unix_socket_write_with_fd(task, file, buffer, send_length, passed_fd);
    kernel_free(buffer);
    return result;
}

static int unix_socket_recvmsg(struct task *task, struct task_file *file,
                                uint64_t user_message)
{
    struct leonos_msghdr message;
    struct leonos_iovec *iov;
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    struct task_unix_socket_fd *transfer;
    uint64_t total_capacity = 0;
    uint64_t control_capacity;
    uint32_t remaining;
    uint32_t total = 0;
    int received_fd = -1;
    int result;
    if (!task || !socket || !user_range_ok(user_message, sizeof(message))) {
        return -LEONOS_EFAULT;
    }
    message = *(struct leonos_msghdr *)(uintptr_t)user_message;
    if (message.msg_iovlen > LEONOS_IOV_MAX ||
        (message.msg_iovlen && !user_range_ok(message.msg_iov,
                                               message.msg_iovlen * sizeof(*iov)))) {
        return -LEONOS_EINVAL;
    }
    iov = (struct leonos_iovec *)(uintptr_t)message.msg_iov;
    for (uint64_t i = 0; i < message.msg_iovlen; ++i) {
        if (iov[i].iov_len > 0xffffffffULL - total_capacity ||
            (iov[i].iov_len && !user_range_ok(iov[i].iov_base, iov[i].iov_len))) {
            return -LEONOS_EINVAL;
        }
        total_capacity += iov[i].iov_len;
    }
    transfer = unix_socket_transfer_at(socket, socket->data_tail);
    control_capacity = message.msg_controllen;
    if (transfer && message.msg_control &&
        message.msg_controllen >= unix_cmsg_space(sizeof(int))) {
        if (!user_range_ok(message.msg_control, message.msg_controllen)) return -LEONOS_EFAULT;
        if (!task_can_allocate_fd(task)) return -LEONOS_EMFILE;
    }
    remaining = (uint32_t)total_capacity;
    if (!transfer) remaining = unix_socket_limit_before_transfer(socket, remaining);
    if (!remaining) return 0;
    for (uint64_t i = 0; i < message.msg_iovlen && remaining; ++i) {
        uint32_t chunk = iov[i].iov_len < remaining ? (uint32_t)iov[i].iov_len : remaining;
        if (!chunk) continue;
        result = task_unix_socket_read(file, (void *)(uintptr_t)iov[i].iov_base, chunk);
        if (result < 0) return total ? (int)total : result;
        total += (uint32_t)result;
        remaining -= (uint32_t)result;
        if ((uint32_t)result != chunk) break;
    }
    if (!total) {
        return 0;
    }
    message.msg_flags = 0;
    message.msg_controllen = 0;
    if (transfer) {
        if (message.msg_control && unix_cmsg_space(sizeof(int)) <= control_capacity) {
            struct leonos_cmsghdr *cmsg = (struct leonos_cmsghdr *)(uintptr_t)message.msg_control;
            received_fd = unix_socket_install_fd(task, &transfer->file);
            if (received_fd < 0) return received_fd;
            cmsg->cmsg_len = unix_cmsg_len(sizeof(int));
            cmsg->cmsg_level = LEONOS_SOL_SOCKET;
            cmsg->cmsg_type = LEONOS_SCM_RIGHTS;
            *(int *)((uint8_t *)cmsg + unix_cmsg_align(sizeof(*cmsg))) = received_fd;
            message.msg_controllen = unix_cmsg_space(sizeof(int));
        } else {
            message.msg_flags |= LEONOS_MSG_CTRUNC;
        }
        clear_task_file(&transfer->file);
        *transfer = (struct task_unix_socket_fd){0};
    }
    *(struct leonos_msghdr *)(uintptr_t)user_message = message;
    return (int)total;
}

int task_unix_socket_poll(const struct task_file *file, int16_t events)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    struct task_unix_socket *peer;
    int ready = 0;
    if (!socket) return LEONOS_POLLNVAL;
    if (socket->listening) {
        if ((events & (LEONOS_POLLIN | LEONOS_POLLPRI)) && unix_socket_accept_ready(socket)) {
            ready |= LEONOS_POLLIN;
        }
        return ready;
    }
    peer = task_unix_socket_for_handle(socket->peer_handle);
    if ((events & (LEONOS_POLLIN | LEONOS_POLLPRI)) &&
        socket->data_tail != socket->data_head) ready |= LEONOS_POLLIN;
    if ((events & LEONOS_POLLOUT) && !socket->shutdown_write && peer &&
        !peer->shutdown_read &&
        (peer->data_head + 1u) % TASK_UNIX_SOCKET_RING_CAP != peer->data_tail) {
        ready |= LEONOS_POLLOUT;
    }
    if (!peer) {
        ready |= LEONOS_POLLHUP;
    }
    return ready;
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
            clear_task_file(task_file_for_fd(task, read_fd));
        }
        if (write_fd >= 0) {
            clear_task_file(task_file_for_fd(task, write_fd));
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

static int unix_socket_new_fd(struct task *task, uint32_t handle)
{
    int fd;
    struct task_file *file;
    fd = task_allocate_anon_fd(task, TASK_FILE_FLAG_UNIX_SOCKET | LEONOS_O_RDWR,
                               handle, "socket:[unix]");
    if (fd < 0) return fd;
    file = task_file_for_fd(task, fd);
    if (!file) return -LEONOS_EMFILE;
    task_unix_socket_retain(file);
    return fd;
}

static int unix_socket_bind_fd(struct task *task, struct task_file *file,
                               uint64_t user_address, uint64_t address_length)
{
    struct leonos_sockaddr_un address;
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    if (!task || !socket) return -LEONOS_ENOTSOCK;
    if (socket->path[0]) return -LEONOS_EINVAL;
    if (unix_socket_copy_address(user_address, address_length, &address) < 0) {
        return -LEONOS_EINVAL;
    }
    if (unix_listener_for_path(address.sun_path)) return -LEONOS_EADDRINUSE;
    unix_copy_path(socket->path, address.sun_path);
    socket->owner_uid = task->uid;
    return 0;
}

static int unix_socket_listen_fd(struct task_file *file, uint64_t backlog)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    (void)backlog;
    if (!socket) return -LEONOS_ENOTSOCK;
    if (!socket->path[0]) return -LEONOS_EINVAL;
    socket->listening = 1;
    return 0;
}

static int unix_socket_connect_fd(struct task *task, struct task_file *file,
                                  uint64_t user_address, uint64_t address_length)
{
    struct leonos_sockaddr_un address;
    struct task_unix_socket *client = task_unix_socket_for_file(file);
    struct task_unix_socket *listener;
    struct task_unix_socket *server;
    uint32_t server_handle;
    uint32_t next;
    if (!task || !client) return -LEONOS_ENOTSOCK;
    if (client->peer_handle) return -LEONOS_EISDIR;
    if (unix_socket_copy_address(user_address, address_length, &address) < 0) {
        return -LEONOS_EINVAL;
    }
    listener = unix_listener_for_path(address.sun_path);
    if (!listener || !listener->listening) return -LEONOS_ECONNREFUSED;
    if (task->uid != listener->owner_uid && task->uid != 0) return -LEONOS_EACCES;
    next = (listener->accept_head + 1u) % TASK_UNIX_SOCKET_ACCEPT_CAP;
    if (next == listener->accept_tail) return -LEONOS_EAGAIN;
    server_handle = task_unix_socket_create(listener->owner_uid);
    if (!server_handle) return -LEONOS_ENOMEM;
    server = task_unix_socket_for_handle(server_handle);
    if (!server) return -LEONOS_ENOMEM;
    server->peer_handle = (uint32_t)file->aux;
    client->peer_handle = server_handle;
    ++server->refs; /* Listener queue owns the server endpoint until accept(). */
    listener->accept_handles[listener->accept_head] = server_handle;
    listener->accept_head = next;
    return 0;
}

static int unix_socket_accept_fd(struct task *task, struct task_file *file,
                                 uint64_t user_address, uint64_t user_length)
{
    struct task_unix_socket *listener = task_unix_socket_for_file(file);
    struct task_unix_socket *child;
    uint32_t handle;
    int fd;
    if (!task || !listener) return -LEONOS_ENOTSOCK;
    if (!listener->listening) return -LEONOS_EINVAL;
    if (!unix_socket_accept_ready(listener)) return -LEONOS_EAGAIN;
    handle = listener->accept_handles[listener->accept_tail];
    child = task_unix_socket_for_handle(handle);
    if (!child) return -LEONOS_ECONNRESET;
    fd = unix_socket_new_fd(task, handle);
    if (fd < 0) return fd;
    listener->accept_tail = (listener->accept_tail + 1u) % TASK_UNIX_SOCKET_ACCEPT_CAP;
    if (child->refs) --child->refs; /* Transfer listener queue ownership to fd. */
    if (user_address && user_length) {
        struct leonos_sockaddr_un address = {.sun_family = LEONOS_AF_UNIX};
        leonos_socklen_t length;
        if (!user_range_ok(user_length, sizeof(length))) {
            clear_task_file(task_file_for_fd(task, fd));
            return -LEONOS_EFAULT;
        }
        length = *(leonos_socklen_t *)(uintptr_t)user_length;
        if (length > sizeof(address)) length = sizeof(address);
        if (length && !user_range_ok(user_address, length)) {
            clear_task_file(task_file_for_fd(task, fd));
            return -LEONOS_EFAULT;
        }
        for (uint32_t i = 0; i < length; ++i) {
            ((uint8_t *)(uintptr_t)user_address)[i] = ((const uint8_t *)&address)[i];
        }
        *(leonos_socklen_t *)(uintptr_t)user_length = sizeof(address);
    }
    return fd;
}

static int unix_socketpair(struct task *task, uint64_t user_fds)
{
    uint32_t first_handle;
    uint32_t second_handle;
    struct task_unix_socket *first;
    struct task_unix_socket *second;
    int first_fd;
    int second_fd;
    if (!task || !user_range_ok(user_fds, sizeof(int) * 2u)) return -LEONOS_EFAULT;
    first_handle = task_unix_socket_create(task->uid);
    second_handle = task_unix_socket_create(task->uid);
    if (!first_handle || !second_handle) return -LEONOS_ENOMEM;
    first = task_unix_socket_for_handle(first_handle);
    second = task_unix_socket_for_handle(second_handle);
    if (!first || !second) return -LEONOS_ENOMEM;
    first->peer_handle = second_handle;
    second->peer_handle = first_handle;
    first_fd = unix_socket_new_fd(task, first_handle);
    second_fd = unix_socket_new_fd(task, second_handle);
    if (first_fd < 0 || second_fd < 0) {
        if (first_fd >= 0) clear_task_file(task_file_for_fd(task, first_fd));
        if (second_fd >= 0) clear_task_file(task_file_for_fd(task, second_fd));
        first = task_unix_socket_for_handle(first_handle);
        second = task_unix_socket_for_handle(second_handle);
        if (first && !first->refs) task_unix_socket_destroy(first_handle, first);
        if (second && !second->refs) task_unix_socket_destroy(second_handle, second);
        return -LEONOS_EMFILE;
    }
    ((int *)(uintptr_t)user_fds)[0] = first_fd;
    ((int *)(uintptr_t)user_fds)[1] = second_fd;
    return 0;
}

static int unix_socket_name_fd(struct task_file *file, uint64_t user_address,
                               uint64_t user_length)
{
    struct task_unix_socket *socket = task_unix_socket_for_file(file);
    struct leonos_sockaddr_un address;
    leonos_socklen_t length;
    if (!socket) return -LEONOS_ENOTSOCK;
    if (!user_address || !user_length || !user_range_ok(user_length, sizeof(length))) {
        return -LEONOS_EFAULT;
    }
    length = *(leonos_socklen_t *)(uintptr_t)user_length;
    if (length && !user_range_ok(user_address, length)) return -LEONOS_EFAULT;
    for (uint32_t i = 0; i < sizeof(address); ++i) ((uint8_t *)&address)[i] = 0;
    address.sun_family = LEONOS_AF_UNIX;
    unix_copy_path(address.sun_path, socket->path);
    if (length > sizeof(address)) length = sizeof(address);
    for (uint32_t i = 0; i < length; ++i) {
        ((uint8_t *)(uintptr_t)user_address)[i] = ((const uint8_t *)&address)[i];
    }
    *(leonos_socklen_t *)(uintptr_t)user_length = sizeof(address);
    return 0;
}

static int unix_socket_poll(uint64_t user_fds, uint64_t count)
{
    struct task *task = sched_current_task();
    struct leonos_pollfd *fds = (struct leonos_pollfd *)(uintptr_t)user_fds;
    int ready = 0;
    if (!task || count > SCHED_TASK_FILE_LIMIT || (count &&
        !user_range_ok(user_fds, count * sizeof(*fds)))) return -LEONOS_EFAULT;
    for (uint64_t i = 0; i < count; ++i) {
        struct task_file *file;
        int revents = 0;
        fds[i].revents = 0;
        if (fds[i].fd < 0) continue;
        file = task_file_for_fd(task, fds[i].fd);
        if (file) {
            if (file->flags & TASK_FILE_FLAG_PIPE) {
                revents = task_pipe_poll(file, fds[i].events);
            } else if (file->flags & TASK_FILE_FLAG_UNIX_SOCKET) {
                revents = task_unix_socket_poll(file, fds[i].events);
            } else if (file->flags & TASK_FILE_FLAG_PTY_MASTER) {
                revents = task_pty_master_poll(file, fds[i].events);
            } else if (file->flags & TASK_FILE_FLAG_DEV_NULL) {
                if (fds[i].events & LEONOS_POLLIN) revents |= LEONOS_POLLIN;
                if (fds[i].events & LEONOS_POLLOUT) revents |= LEONOS_POLLOUT;
            } else {
                if (fds[i].events & LEONOS_POLLIN) revents |= LEONOS_POLLIN;
                if (fds[i].events & LEONOS_POLLOUT) revents |= LEONOS_POLLOUT;
            }
        } else if (fds[i].fd >= 0 && fds[i].fd <= 2 &&
                   (task->closed_stdio_mask & (1u << (uint32_t)fds[i].fd)) == 0) {
            if (fds[i].fd == 0 && (fds[i].events & LEONOS_POLLIN) &&
                pty_input_available(task->pty_id)) revents |= LEONOS_POLLIN;
            if ((fds[i].fd == 1 || fds[i].fd == 2) &&
                (fds[i].events & LEONOS_POLLOUT)) revents |= LEONOS_POLLOUT;
        } else {
            revents = LEONOS_POLLNVAL;
        }
        fds[i].revents = (int16_t)revents;
        if (revents) ++ready;
    }
    return ready;
}

int task_unix_socket_syscall(uint64_t number, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    struct task *task = sched_current_task();
    struct task_file *file;
    (void)a4;
    (void)a5;
    if (number == LINUX_SYS_POLL) return unix_socket_poll(a0, a1);
    if (!task) return -LEONOS_EBADF;
    if (number == LINUX_SYS_SOCKET) {
        uint32_t handle;
        if (a0 != LEONOS_AF_UNIX || (a1 & 0xffu) != LEONOS_SOCK_STREAM || a2 != 0) {
            return -LEONOS_EINVAL;
        }
        handle = task_unix_socket_create(task->uid);
        if (!handle) return -LEONOS_EMFILE;
        {
            int fd = unix_socket_new_fd(task, handle);
            if (fd < 0) {
                struct task_unix_socket *socket = task_unix_socket_for_handle(handle);
                if (socket) task_unix_socket_destroy(handle, socket);
            }
            return fd;
        }
    }
    if (number == LINUX_SYS_SOCKETPAIR) {
        if (a0 != LEONOS_AF_UNIX || (a1 & 0xffu) != LEONOS_SOCK_STREAM || a2 != 0) {
            return -LEONOS_EINVAL;
        }
        return unix_socketpair(task, a3);
    }
    file = task_file_for_fd(task, (int)a0);
    switch (number) {
    case LINUX_SYS_BIND:
        return unix_socket_bind_fd(task, file, a1, a2);
    case LINUX_SYS_LISTEN:
        return unix_socket_listen_fd(file, a1);
    case LINUX_SYS_CONNECT:
        return unix_socket_connect_fd(task, file, a1, a2);
    case LINUX_SYS_ACCEPT:
        return unix_socket_accept_fd(task, file, a1, a2);
    case LINUX_SYS_SENDTO:
        if (!file || !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        if (a4 || a5) return -LEONOS_EISDIR;
        return task_unix_socket_write(file, (const void *)(uintptr_t)a1,
                                      a2 > TASK_UNIX_SOCKET_RING_CAP - 1u ?
                                      TASK_UNIX_SOCKET_RING_CAP - 1u : (uint32_t)a2);
    case LINUX_SYS_RECVFROM:
        if (!file || !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        return task_unix_socket_read(file, (void *)(uintptr_t)a1,
                                     a2 > TASK_UNIX_SOCKET_RING_CAP - 1u ?
                                     TASK_UNIX_SOCKET_RING_CAP - 1u : (uint32_t)a2);
    case LINUX_SYS_SENDMSG:
        return unix_socket_sendmsg(task, file, a1);
    case LINUX_SYS_RECVMSG:
        return unix_socket_recvmsg(task, file, a1);
    case LINUX_SYS_SHUTDOWN: {
        struct task_unix_socket *socket = task_unix_socket_for_file(file);
        if (!socket) return -LEONOS_ENOTSOCK;
        if (a1 == LEONOS_SHUT_RD || a1 == LEONOS_SHUT_RDWR) socket->shutdown_read = 1;
        if (a1 == LEONOS_SHUT_WR || a1 == LEONOS_SHUT_RDWR) socket->shutdown_write = 1;
        if (a1 > LEONOS_SHUT_RDWR) return -LEONOS_EINVAL;
        return 0;
    }
    case LINUX_SYS_GETSOCKNAME:
        return unix_socket_name_fd(file, a1, a2);
    case LINUX_SYS_SETSOCKOPT:
        return task_unix_socket_for_file(file) ? 0 : -LEONOS_ENOTSOCK;
    case LINUX_SYS_GETSOCKOPT: {
        int value;
        leonos_socklen_t length;
        if (!task_unix_socket_for_file(file)) return -LEONOS_ENOTSOCK;
        if (a1 != LEONOS_SOL_SOCKET || !a3 || !a4 ||
            !user_range_ok(a4, sizeof(length))) return -LEONOS_EINVAL;
        length = *(leonos_socklen_t *)(uintptr_t)a4;
        if (length < sizeof(value) || !user_range_ok(a3, sizeof(value))) return -LEONOS_EINVAL;
        value = a2 == LEONOS_SO_TYPE ? LEONOS_SOCK_STREAM : 0;
        *(int *)(uintptr_t)a3 = value;
        *(leonos_socklen_t *)(uintptr_t)a4 = sizeof(value);
        return 0;
    }
    default:
        return -LEONOS_ENOSYS;
    }
}

int syscall_ipc_owns(uint64_t number)
{
    switch (number) {
    case LINUX_SYS_POLL:
    case LINUX_SYS_SOCKET:
    case LINUX_SYS_CONNECT:
    case LINUX_SYS_ACCEPT:
    case LINUX_SYS_SENDTO:
    case LINUX_SYS_RECVFROM:
    case LINUX_SYS_SENDMSG:
    case LINUX_SYS_RECVMSG:
    case LINUX_SYS_SHUTDOWN:
    case LINUX_SYS_BIND:
    case LINUX_SYS_LISTEN:
    case LINUX_SYS_GETSOCKNAME:
    case LINUX_SYS_SETSOCKOPT:
    case LINUX_SYS_GETSOCKOPT:
    case LINUX_SYS_SOCKETPAIR:
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
    if (number == LINUX_SYS_POLL || number == LINUX_SYS_SOCKETPAIR ||
        (number >= LINUX_SYS_SOCKET && number <= LINUX_SYS_GETSOCKOPT)) {
        return task_unix_socket_syscall(number, a0, a1, a2, a3, a4, a5);
    }
    return syscall_dispatch_regs_legacy(number, a0, a1, a2, a3, a4, a5);
}
