/* POSIX Unix-domain stream sockets backed by the kernel object table. */
#include <ntclks/heap.h>
#include <ntclks/object.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/usercopy.h>
#include <leonos/fs.h>
#include <linux/socket.h>

#define UNIX_SOCKET_MAX 32u
#define UNIX_SOCKET_BACKLOG_MAX 8u
#define UNIX_SOCKET_RX_CAP 16384u

enum unix_socket_state { UNIX_SOCKET_OPEN = 0, UNIX_SOCKET_LISTEN = 1,
                         UNIX_SOCKET_CONNECTED = 2 };

struct unix_socket {
    uint32_t used;
    uint32_t refs;
    uint32_t handle;
    uint32_t owner_pid;
    uint32_t state;
    uint32_t peer_handle;
    uint32_t shutdown_read;
    uint32_t shutdown_write;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t pending_count;
    uint32_t pending[UNIX_SOCKET_BACKLOG_MAX];
    char path[108];
    uint8_t rx[UNIX_SOCKET_RX_CAP];
};

static struct unix_socket *unix_sockets[UNIX_SOCKET_MAX];

static struct unix_socket *unix_from_file(const struct task_file *file)
{
    if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return NULL;
    return (struct unix_socket *)kernel_object_lookup(kernel_objects(),
                                                       (uint32_t)file->aux,
                                                       KERNEL_OBJECT_SOCKET);
}

static struct unix_socket *unix_from_handle(uint32_t handle)
{
    return (struct unix_socket *)kernel_object_lookup(kernel_objects(), handle,
                                                       KERNEL_OBJECT_SOCKET);
}

static void unix_copy_path(char *dst, const char *src, uint32_t len)
{
    uint32_t i = 0;
    if (!dst) return;
    while (src && i + 1u < 108u && i < len && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int unix_socket_path(const void *user_addr, uint32_t user_len, char *path)
{
    const struct sockaddr_un *address;
    uint32_t path_len;
    if (!user_addr || user_len < sizeof(sa_family_t) ||
        !user_range_ok((uint64_t)(uintptr_t)user_addr, user_len)) {
        return -LEONOS_EFAULT;
    }
    address = (const struct sockaddr_un *)user_addr;
    if (address->sun_family != AF_UNIX) return -LEONOS_EINVAL;
    path_len = user_len - sizeof(sa_family_t);
    if (path_len > sizeof(address->sun_path)) path_len = sizeof(address->sun_path);
    if (!path_len || address->sun_path[0] == 0) return -LEONOS_EINVAL;
    unix_copy_path(path, address->sun_path, path_len);
    return path[0] ? 0 : -LEONOS_EINVAL;
}

static struct unix_socket *unix_find_path(const char *path)
{
    if (!path || !path[0]) return NULL;
    for (uint32_t i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *socket = unix_sockets[i];
        if (socket && socket->used && socket->path[0]) {
            uint32_t j = 0;
            while (socket->path[j] && path[j] && socket->path[j] == path[j]) ++j;
            if (!socket->path[j] && !path[j]) return socket;
        }
    }
    return NULL;
}

static struct unix_socket *unix_alloc(uint32_t owner_pid)
{
    struct unix_socket *socket = NULL;
    uint32_t slot;
    for (slot = 0; slot < UNIX_SOCKET_MAX; ++slot) {
        if (!unix_sockets[slot]) break;
    }
    if (slot == UNIX_SOCKET_MAX) return NULL;
    socket = (struct unix_socket *)kernel_malloc(sizeof(*socket));
    if (!socket) return NULL;
    *socket = (struct unix_socket){
        .used = 1, .refs = 1, .owner_pid = owner_pid,
        .state = UNIX_SOCKET_OPEN,
    };
    socket->handle = kernel_object_insert(kernel_objects(), socket,
                                          KERNEL_OBJECT_SOCKET);
    if (!socket->handle) {
        kernel_free(socket);
        return NULL;
    }
    unix_sockets[slot] = socket;
    return socket;
}

static void unix_release_handle(uint32_t handle)
{
    struct unix_socket *socket = unix_from_handle(handle);
    if (!socket) return;
    if (socket->refs) --socket->refs;
    if (socket->refs) return;
    if (socket->peer_handle) {
        struct unix_socket *peer = unix_from_handle(socket->peer_handle);
        if (peer && peer->peer_handle == handle) peer->peer_handle = 0;
    }
    (void)kernel_object_remove(kernel_objects(), handle, KERNEL_OBJECT_SOCKET, NULL);
    for (uint32_t i = 0; i < UNIX_SOCKET_MAX; ++i) {
        if (unix_sockets[i] == socket) unix_sockets[i] = NULL;
    }
    kernel_free(socket);
}

static int unix_alloc_fd(struct task *task, struct unix_socket *socket)
{
    struct task_file *file;
    if (!task || !socket || !task_can_allocate_fd(task)) return -LEONOS_EMFILE;
    for (uint32_t i = 0; i <= sched_task_file_capacity(task); ++i) {
        int fd = (int)i + 4;
        if (task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) continue;
        file = sched_task_file_at(task, i);
        if (!file) continue;
        file->used = 1;
        file->flags = TASK_FILE_FLAG_SOCKET | TASK_FILE_FLAG_SOCKET_UNIX | LEONOS_O_RDWR;
        file->fd_flags = 0;
        file->node = (struct storage_node){.type = LEONOS_FS_TYPE_DEVICE};
        file->offset = 0;
        file->aux = socket->handle;
        file->path[0] = 0;
        ++socket->refs;
        return fd;
    }
    return -LEONOS_EMFILE;
}

void task_socket_retain(struct task_file *file)
{
    struct unix_socket *socket = unix_from_file(file);
    if (socket) ++socket->refs;
}

void task_socket_release(struct task_file *file)
{
    if (file && (file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) {
        unix_release_handle((uint32_t)file->aux);
    }
}

int task_socket_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct unix_socket *socket = unix_from_file(file);
    uint32_t count = 0;
    if (!socket || !buffer || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
    if (socket->state != UNIX_SOCKET_CONNECTED) return -LEONOS_ENOTSUP;
    while (socket->rx_tail != socket->rx_head && count < length) {
        ((uint8_t *)buffer)[count++] = socket->rx[socket->rx_tail];
        socket->rx_tail = (socket->rx_tail + 1u) % UNIX_SOCKET_RX_CAP;
    }
    if (count) return (int)count;
    return socket->peer_handle ? -LEONOS_EAGAIN : 0;
}

int task_socket_write(struct task_file *file, const void *buffer, uint32_t length)
{
    struct unix_socket *socket = unix_from_file(file);
    struct unix_socket *peer;
    uint32_t count = 0;
    if (!socket || !buffer || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
    if (socket->state != UNIX_SOCKET_CONNECTED || socket->shutdown_write) return -LEONOS_EPIPE;
    peer = unix_from_handle(socket->peer_handle);
    if (!peer || peer->shutdown_read) return -LEONOS_EPIPE;
    while (count < length) {
        uint32_t next = (peer->rx_head + 1u) % UNIX_SOCKET_RX_CAP;
        if (next == peer->rx_tail) return count ? (int)count : -LEONOS_EAGAIN;
        peer->rx[peer->rx_head] = ((const uint8_t *)buffer)[count++];
        peer->rx_head = next;
    }
    return (int)count;
}

short task_socket_poll(const struct task_file *file, short events)
{
    struct unix_socket *socket = unix_from_file(file);
    short result = 0;
    if (!socket) return POLLNVAL;
    if (socket->state == UNIX_SOCKET_LISTEN) {
        if ((events & POLLIN) && socket->pending_count) result |= POLLIN;
        return result;
    }
    if ((events & POLLIN) && socket->rx_tail != socket->rx_head) result |= POLLIN;
    if ((events & POLLOUT) && socket->peer_handle && !socket->shutdown_write) result |= POLLOUT;
    if (!socket->peer_handle) result |= POLLHUP;
    return result;
}

static int64_t unix_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                    uint64_t a2, uint64_t a3)
{
    struct task *task = sched_current_task();
    struct task_file *file;
    struct unix_socket *socket;
    (void)a3;
    if (!task) return -LEONOS_EPERM;
    if (number == __NR_socket) {
        if ((int)a0 != AF_UNIX || ((int)a1 & 0x0f) != SOCK_STREAM || a2 != 0) {
            return -LEONOS_ENOTSUP;
        }
        socket = unix_alloc(task->pid);
        if (!socket) return -LEONOS_ENOMEM;
        {
            int fd = unix_alloc_fd(task, socket);
            if (fd < 0) {
                unix_release_handle(socket->handle);
                return fd;
            }
            /* The allocation starts with an owner reference and adds one for the fd. */
            unix_release_handle(socket->handle);
            return fd;
        }
    }
    file = task_file_for_fd(task, (int)a0);
    socket = unix_from_file(file);
    if (!socket) return -LEONOS_EBADF;
    if (number == __NR_bind) {
        char path[108];
        if (unix_socket_path((const void *)(uintptr_t)a1, (uint32_t)a2, path) < 0) return -LEONOS_EINVAL;
        if (socket->path[0] || unix_find_path(path)) return -LEONOS_EADDRINUSE;
        unix_copy_path(socket->path, path, sizeof(path));
        return 0;
    }
    if (number == __NR_listen) {
        if (!socket->path[0] || (int)a1 < 0) return -LEONOS_EINVAL;
        socket->state = UNIX_SOCKET_LISTEN;
        return 0;
    }
    if (number == __NR_connect) {
        char path[108];
        struct unix_socket *listener, *server;
        if (socket->state != UNIX_SOCKET_OPEN) return -LEONOS_EISCONN;
        if (unix_socket_path((const void *)(uintptr_t)a1, (uint32_t)a2, path) < 0) return -LEONOS_EINVAL;
        listener = unix_find_path(path);
        if (!listener || listener->state != UNIX_SOCKET_LISTEN) return -LEONOS_ENOENT;
        if (listener->pending_count >= UNIX_SOCKET_BACKLOG_MAX) return -LEONOS_EAGAIN;
        server = unix_alloc(listener->owner_pid);
        if (!server) return -LEONOS_ENOMEM;
        server->state = UNIX_SOCKET_CONNECTED;
        socket->state = UNIX_SOCKET_CONNECTED;
        socket->peer_handle = server->handle;
        server->peer_handle = socket->handle;
        listener->pending[listener->pending_count++] = server->handle;
        return 0;
    }
    if (number == __NR_accept) {
        if (socket->state != UNIX_SOCKET_LISTEN || !socket->pending_count) return -LEONOS_EAGAIN;
        uint32_t handle = socket->pending[0];
        for (uint32_t i = 1; i < socket->pending_count; ++i) socket->pending[i - 1] = socket->pending[i];
        --socket->pending_count;
        struct unix_socket *accepted = unix_from_handle(handle);
        int fd = unix_alloc_fd(task, accepted);
        if (fd < 0) {
            ++socket->pending_count;
            for (uint32_t i = socket->pending_count; i > 0; --i) socket->pending[i] = socket->pending[i - 1];
            socket->pending[0] = handle;
            return fd;
        }
        unix_release_handle(handle); /* drop the listener's pending reference */
        if (a1 && a2 && user_range_ok(a2, sizeof(socklen_t))) {
            struct sockaddr_un *address = (struct sockaddr_un *)(uintptr_t)a1;
            socklen_t *length = (socklen_t *)(uintptr_t)a2;
            if (user_range_ok(a1, sizeof(*address)) && *length >= sizeof(sa_family_t)) {
                *address = (struct sockaddr_un){.sun_family = AF_UNIX};
                unix_copy_path(address->sun_path, socket->path, sizeof(address->sun_path));
                *length = sizeof(sa_family_t) + 1u;
            }
        }
        return fd;
    }
    if (number == __NR_getsockname) {
        if (!a1 || !a2 || !user_range_ok(a1, sizeof(struct sockaddr_un)) || !user_range_ok(a2, sizeof(socklen_t))) return -LEONOS_EFAULT;
        struct sockaddr_un *address = (struct sockaddr_un *)(uintptr_t)a1;
        socklen_t *length = (socklen_t *)(uintptr_t)a2;
        *address = (struct sockaddr_un){.sun_family = AF_UNIX};
        unix_copy_path(address->sun_path, socket->path, sizeof(address->sun_path));
        *length = sizeof(sa_family_t) + (socket->path[0] ? 1u : 0u);
        return 0;
    }
    if (number == __NR_setsockopt || number == __NR_getsockopt) return 0;
    if (number == __NR_shutdown) {
        if (a1 == SHUT_RD || a1 == SHUT_RDWR) socket->shutdown_read = 1;
        if (a1 == SHUT_WR || a1 == SHUT_RDWR) socket->shutdown_write = 1;
        if (a1 != SHUT_RD && a1 != SHUT_WR && a1 != SHUT_RDWR) return -LEONOS_EINVAL;
        return 0;
    }
    return -LEONOS_ENOSYS;
}

int64_t syscall_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4)
{
    (void)a4;
    if (number == __NR_send || number == __NR_sendto) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        return task_socket_write(file, (const void *)(uintptr_t)a1, (uint32_t)a2);
    }
    if (number == __NR_recv || number == __NR_recvfrom) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        return task_socket_read(file, (void *)(uintptr_t)a1, (uint32_t)a2);
    }
    return unix_socket_dispatch(number, a0, a1, a2, a3);
}
