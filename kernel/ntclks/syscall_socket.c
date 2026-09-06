/* POSIX Unix-domain stream sockets backed by the kernel object table. */
#include <ntclks/heap.h>
#include <ntclks/net.h>
#include <ntclks/object.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/usercopy.h>
#include <ntclks/wait.h>
#include <leonos/fs.h>
#include <leonos/net.h>
#include <linux/socket.h>

#define ntohs(value) ((uint16_t)((((uint16_t)(value) & 0xffu) << 8) | ((uint16_t)(value) >> 8)))
#define htons(value) ntohs(value)
#define ntohl(value) ((((uint32_t)(value) & 0xffu) << 24) | (((uint32_t)(value) & 0xff00u) << 8) | (((uint32_t)(value) & 0xff0000u) >> 8) | ((uint32_t)(value) >> 24))
#define htonl(value) ntohl(value)

#define UNIX_SOCKET_MAX 32u
#define UNIX_SOCKET_BACKLOG_MAX 8u
#define UNIX_SOCKET_RX_CAP 16384u
#define UNIX_SOCKET_FD_TRANSFER_MAX 8u
#define UNIX_SOCKET_CMSG_CAP 256u

enum unix_socket_state { UNIX_SOCKET_OPEN = 0, UNIX_SOCKET_LISTEN = 1,
                         UNIX_SOCKET_CONNECTED = 2 };

struct unix_socket {
    uint32_t used;
    uint32_t refs;
    uint32_t handle;
    uint32_t owner_pid;
    uint32_t state;
    uint32_t peer_handle;
    uint32_t peer_pid;
    uint32_t shutdown_read;
    uint32_t shutdown_write;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t pending_count;
    uint32_t pending[UNIX_SOCKET_BACKLOG_MAX];
    uint32_t rx_fd_count;
    int32_t rx_fds[UNIX_SOCKET_FD_TRANSFER_MAX];
    char path[108];
    uint8_t rx[UNIX_SOCKET_RX_CAP];
    struct kernel_wait_queue wait_rx;
    struct kernel_wait_queue wait_tx;
    struct kernel_wait_queue wait_accept;
    struct kernel_wait_queue wait_connect;
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
    kernel_wait_queue_init(&socket->wait_rx);
    kernel_wait_queue_init(&socket->wait_tx);
    kernel_wait_queue_init(&socket->wait_accept);
    kernel_wait_queue_init(&socket->wait_connect);
    unix_sockets[slot] = socket;
    return socket;
}

static void unix_wake_peer(struct unix_socket *socket)
{
    struct unix_socket *peer;
    if (!socket || !socket->peer_handle) return;
    peer = unix_from_handle(socket->peer_handle);
    if (!peer) return;
    (void)kernel_wait_queue_wake_all(&peer->wait_rx);
    (void)kernel_wait_queue_wake_all(&peer->wait_tx);
}

static void unix_release_handle(uint32_t handle)
{
    struct unix_socket *socket = unix_from_handle(handle);
    if (!socket) return;
    if (socket->refs) --socket->refs;
    if (socket->refs) return;
    if (socket->peer_handle) {
        struct unix_socket *peer = unix_from_handle(socket->peer_handle);
        if (peer) {
            if (peer->peer_handle == handle) peer->peer_handle = 0;
            (void)kernel_wait_queue_wake_all(&peer->wait_rx);
            (void)kernel_wait_queue_wake_all(&peer->wait_tx);
        }
    }
    (void)kernel_wait_queue_wake_all(&socket->wait_rx);
    (void)kernel_wait_queue_wake_all(&socket->wait_tx);
    (void)kernel_wait_queue_wake_all(&socket->wait_accept);
    (void)kernel_wait_queue_wake_all(&socket->wait_connect);
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

static int unix_clone_fd_to_task(struct task *target, const struct task_file *source)
{
    struct task_file *file;
    if (!target || !source || !source->used || !task_can_allocate_fd(target)) {
        return -LEONOS_EBADF;
    }
    for (uint32_t i = 0; i < sched_task_file_capacity(target); ++i) {
        int fd = (int)i + 4;
        if (task_file_for_fd(target, fd) || task_pty_fd_for_fd(target, fd)) continue;
        file = sched_task_file_at(target, i);
        if (!file) continue;
        *file = *source;
        file->used = 1;
        file->fd_flags = 0;
        file->offset = 0;
        file->read_cursor = (struct storage_read_cursor){0};
        task_pipe_retain(file);
        task_socket_retain(file);
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
    kernel_wait_queue_remove(&socket->wait_rx, sched_current_task());
    while (socket->rx_tail != socket->rx_head && count < length) {
        ((uint8_t *)buffer)[count++] = socket->rx[socket->rx_tail];
        socket->rx_tail = (socket->rx_tail + 1u) % UNIX_SOCKET_RX_CAP;
    }
    if (count) {
        unix_wake_peer(socket);
        return (int)count;
    }
    if (!socket->peer_handle) return 0;
    if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
    kernel_wait_queue_block_current(&socket->wait_rx);
    return -LEONOS_EAGAIN;
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
    kernel_wait_queue_remove(&socket->wait_tx, sched_current_task());
    while (count < length) {
        uint32_t next = (peer->rx_head + 1u) % UNIX_SOCKET_RX_CAP;
        if (next == peer->rx_tail) {
            if (count) {
                (void)kernel_wait_queue_wake_all(&peer->wait_rx);
                return (int)count;
            }
            if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
            kernel_wait_queue_block_current(&socket->wait_tx);
            return -LEONOS_EAGAIN;
        }
        peer->rx[peer->rx_head] = ((const uint8_t *)buffer)[count++];
        peer->rx_head = next;
    }
    (void)kernel_wait_queue_wake_all(&peer->wait_rx);
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

static struct unix_socket *unix_from_inet_file(const struct task_file *file)
{
    (void)file;
    return NULL;
}

int task_inet_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct leonos_net_socket_io request = {0};
    struct task *task = sched_current_task();
    short ready;
    if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_INET)) return -LEONOS_EBADF;
    request.socket = (int32_t)file->aux;
    request.buffer = buffer;
    request.length = length;
    request.timeout_ms = (file->flags & LEONOS_O_NONBLOCK) ? 1u : 3000u;
    ready = net_socket_poll_fd(request.socket, task ? task->pid : 0, POLLIN | POLLHUP);
    if (!(ready & (POLLIN | POLLHUP)) && (file->flags & LEONOS_O_NONBLOCK)) {
        return -LEONOS_EAGAIN;
    }
    if (net_socket_recv(&request, task ? task->pid : 0) < 0) return -LEONOS_EIO;
    if (request.status == LEONOS_NET_STATUS_OK) return (int)request.transferred;
    if (request.status == LEONOS_NET_STATUS_SOCKET_CLOSED ||
        request.status == LEONOS_NET_STATUS_TCP_RESET) return 0;
    if (request.status == LEONOS_NET_STATUS_TCP_TIMEOUT && (file->flags & LEONOS_O_NONBLOCK)) return -LEONOS_EAGAIN;
    return -LEONOS_EIO;
}

int task_inet_write(struct task_file *file, const void *buffer, uint32_t length)
{
    struct leonos_net_socket_io request = {0};
    struct task *task = sched_current_task();
    if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_INET)) return -LEONOS_EBADF;
    request.socket = (int32_t)file->aux;
    request.buffer = (void *)buffer;
    request.length = length;
    request.timeout_ms = (file->flags & LEONOS_O_NONBLOCK) ? 1u : 3000u;
    if (net_socket_send(&request, task ? task->pid : 0) < 0) return -LEONOS_EIO;
    if (request.status == LEONOS_NET_STATUS_OK) return (int)request.transferred;
    if (request.status == LEONOS_NET_STATUS_SOCKET_CLOSED ||
        request.status == LEONOS_NET_STATUS_TCP_RESET) return -LEONOS_EPIPE;
    if (request.status == LEONOS_NET_STATUS_TCP_TIMEOUT && (file->flags & LEONOS_O_NONBLOCK)) return -LEONOS_EAGAIN;
    return -LEONOS_EIO;
}

short task_inet_poll(const struct task_file *file, short events)
{
    struct task *task = sched_current_task();
    if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_INET)) return POLLNVAL;
    return net_socket_poll_fd((int32_t)file->aux, task ? task->pid : 0, events);
}

void task_inet_retain(struct task_file *file)
{
    (void)file;
}

void task_inet_release(struct task_file *file)
{
    /* net.c sockets remain owner-pid scoped and are reaped on process exit.
     * Forked descriptors therefore keep sharing the parent connection until
     * the last process closes; the ABI migration does not change that. */
    (void)file;
}

static void inet_ip_to_text(uint32_t ip, char *text, uint32_t capacity)
{
    if (capacity < 16u) return;
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t value = (ip >> (i * 8u)) & 0xffu;
        uint32_t digits = value ? 0u : 1u;
        uint32_t temp = value;
        while (temp) { temp /= 10u; ++digits; }
        if (digits > 2u) {
            text[0] = (char)('0' + (value / 100u)); ++text;
            value %= 100u;
            text[0] = (char)('0' + (value / 10u)); ++text;
            value %= 10u;
            text[0] = (char)('0' + value); ++text;
        } else if (digits == 2u) {
            text[0] = (char)('0' + (value / 10u)); ++text;
            text[0] = (char)('0' + (value % 10u)); ++text;
        } else {
            text[0] = (char)('0' + value); ++text;
        }
        if (i != 3u) { *text = '.'; ++text; }
    }
    *text = 0;
}

static int64_t inet_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                    uint64_t a2, uint64_t a3, uint64_t a4)
{
    struct task *task = sched_current_task();
    struct task_file *file;
    (void)a3;
    if (!task) return -LEONOS_EPERM;
    if (number == __NR_socket) {
        struct leonos_net_socket_open request = {
            .domain = (uint32_t)a0,
            .type = (uint32_t)a1,
            .protocol = (uint32_t)a2,
        };
        if ((int)a0 != AF_INET || ((int)a1 & 0x0f) != SOCK_STREAM) {
            return -LEONOS_ENOTSUP;
        }
        if (net_socket_open(&request, task->pid, task->uid) < 0) return -LEONOS_EIO;
        if (request.status != LEONOS_NET_STATUS_OK) return -LEONOS_ENOMEM;
        file = NULL;
        if (!task_can_allocate_fd(task)) {
            struct leonos_net_socket_close close = {.socket = request.socket};
            (void)net_socket_close(&close, task->pid);
            return -LEONOS_EMFILE;
        }
        for (uint32_t i = 0; i <= sched_task_file_capacity(task); ++i) {
            int fd = (int)i + 4;
            if (task_file_for_fd(task, fd) || task_pty_fd_for_fd(task, fd)) continue;
            file = sched_task_file_at(task, i);
            if (!file) continue;
            file->used = 1;
            file->flags = TASK_FILE_FLAG_SOCKET | TASK_FILE_FLAG_SOCKET_INET |
                          LEONOS_O_RDWR | (((int)a1 & SOCK_NONBLOCK) ? LEONOS_O_NONBLOCK : 0u);
            file->fd_flags = ((int)a1 & SOCK_CLOEXEC) ? 1u : 0u;
            file->node = (struct storage_node){.type = LEONOS_FS_TYPE_DEVICE};
            file->offset = 0;
            file->aux = (uint64_t)(uint32_t)request.socket;
            file->path[0] = 0;
            return fd;
        }
        {
            struct leonos_net_socket_close close = {.socket = request.socket};
            (void)net_socket_close(&close, task->pid);
        }
        return -LEONOS_EMFILE;
    }
    file = task_file_for_fd(task, (int)a0);
    if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_INET)) return -LEONOS_EBADF;
    if (number == __NR_connect) {
        const struct sockaddr_in *address = (const struct sockaddr_in *)(uintptr_t)a1;
        struct leonos_net_socket_connect request = {0};
        if (!user_range_ok(a1, sizeof(*address)) || address->sin_family != AF_INET) {
            return -LEONOS_EINVAL;
        }
        request.socket = (int32_t)file->aux;
        request.port = ntohs(address->sin_port);
        request.timeout_ms = (file->flags & LEONOS_O_NONBLOCK) ? 1u : 5000u;
        inet_ip_to_text(ntohl(address->sin_addr.s_addr), request.host,
                        sizeof(request.host));
        if (net_socket_connect(&request, task->pid) < 0) return -LEONOS_EIO;
        if (request.status != LEONOS_NET_STATUS_OK) {
            if ((file->flags & LEONOS_O_NONBLOCK) &&
                request.status == LEONOS_NET_STATUS_TCP_TIMEOUT) return -LEONOS_EAGAIN;
            if (request.status == LEONOS_NET_STATUS_NO_DEVICE) return -LEONOS_ENODEV;
            return -LEONOS_EIO;
        }
        return 0;
    }
    if (number == __NR_getsockname || number == __NR_getpeername) {
        uint32_t local_ip, remote_ip;
        uint16_t local_port, remote_port;
        struct sockaddr_in *address = (struct sockaddr_in *)(uintptr_t)a1;
        socklen_t *length = (socklen_t *)(uintptr_t)a2;
        if (!user_range_ok(a1, sizeof(*address)) || !user_range_ok(a2, sizeof(*length))) return -LEONOS_EFAULT;
        if (net_socket_address((int32_t)file->aux, task->pid,
                               &local_ip, &local_port, &remote_ip, &remote_port) < 0) return -LEONOS_EBADF;
        *address = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = number == __NR_getsockname ? htons(local_port) : htons(remote_port),
            .sin_addr = {.s_addr = number == __NR_getsockname ? htonl(local_ip) : htonl(remote_ip)},
        };
        *length = sizeof(*address);
        return 0;
    }
    if (number == __NR_shutdown || number == __NR_setsockopt ||
        number == __NR_getsockopt) {
        return 0;
    }
    return -LEONOS_ENOSYS;
}

static int unix_sendmsg(struct task *task, struct task_file *file,
                       const struct msghdr *user_msg)
{
    struct unix_socket *socket = unix_from_file(file);
    struct unix_socket *peer;
    struct msghdr message;
    struct iovec vector;
    uint64_t cmsg_offset = 0;
    uint32_t sent;
    uint32_t data_len;
    if (!socket || !user_msg || !user_range_ok((uint64_t)(uintptr_t)user_msg,
                                               sizeof(*user_msg))) {
        return -LEONOS_EFAULT;
    }
    message = *user_msg;
    if (socket->state != UNIX_SOCKET_CONNECTED) return -LEONOS_ENOTSUP;
    if (message.msg_iovlen > 1u) return -LEONOS_ENOTSUP;
    if (message.msg_iovlen != 0u) {
        if (!message.msg_iov || !user_range_ok((uint64_t)(uintptr_t)message.msg_iov,
                                                sizeof(vector))) {
            return -LEONOS_EFAULT;
        }
        vector = *message.msg_iov;
        data_len = vector.iov_len > UNIX_SOCKET_RX_CAP ? UNIX_SOCKET_RX_CAP
                                                       : (uint32_t)vector.iov_len;
        if (data_len && (!vector.iov_base || !user_range_ok((uint64_t)(uintptr_t)vector.iov_base,
                                                            data_len))) {
            return -LEONOS_EFAULT;
        }
        sent = (uint32_t)task_socket_write(file, vector.iov_base, data_len);
        if ((int32_t)sent < 0) return (int)sent;
    } else {
        sent = 0;
    }

    peer = unix_from_handle(socket->peer_handle);
    if (peer && message.msg_controllen != 0u) {
        uint8_t control[UNIX_SOCKET_CMSG_CAP];
        if (!message.msg_control || message.msg_controllen > sizeof(control)) {
            return -LEONOS_EINVAL;
        }
        if (!user_range_ok((uint64_t)(uintptr_t)message.msg_control,
                           message.msg_controllen)) {
            return -LEONOS_EFAULT;
        }
        for (uint32_t i = 0; i < message.msg_controllen; ++i) {
            control[i] = ((const uint8_t *)(uintptr_t)message.msg_control)[i];
        }
        while (cmsg_offset + sizeof(struct cmsghdr) <= message.msg_controllen) {
            struct cmsghdr *header = (struct cmsghdr *)(control + cmsg_offset);
            uint64_t advance = header->cmsg_len;
            if (advance < sizeof(*header) ||
                cmsg_offset + advance > message.msg_controllen) {
                return -LEONOS_EINVAL;
            }
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
                uint64_t payload = header->cmsg_len - CMSG_ALIGN(sizeof(*header));
                uint64_t count = payload / sizeof(int32_t);
                if (payload % sizeof(int32_t) || count > UNIX_SOCKET_FD_TRANSFER_MAX) {
                    return -LEONOS_EINVAL;
                }
                for (uint64_t j = 0; j < count; ++j) {
                    const int32_t *fds = (const int32_t *)CMSG_DATA(header);
                    struct task_file *source = task_file_for_fd(task, fds[j]);
                    struct task *receiver = sched_find(socket->peer_pid);
                    int new_fd = unix_clone_fd_to_task(receiver, source);
                    if (new_fd < 0 || peer->rx_fd_count >= UNIX_SOCKET_FD_TRANSFER_MAX) {
                        continue;
                    }
                    peer->rx_fds[peer->rx_fd_count++] = new_fd;
                }
            }
            cmsg_offset += CMSG_ALIGN(advance);
        }
    }
    return sent ? (int)sent : (message.msg_controllen != 0u ? 0 : (int)sent);
}

static int unix_recvmsg(struct task *task, struct task_file *file,
                       const struct msghdr *user_msg)
{
    (void)task;
    struct unix_socket *socket = unix_from_file(file);
    struct msghdr message;
    struct iovec vector;
    uint32_t data_len;
    int received;
    if (!socket || !user_msg || !user_range_ok((uint64_t)(uintptr_t)user_msg,
                                               sizeof(*user_msg))) {
        return -LEONOS_EFAULT;
    }
    message = *user_msg;
    if (socket->state != UNIX_SOCKET_CONNECTED) return -LEONOS_ENOTSUP;
    if (message.msg_iovlen > 1u) return -LEONOS_ENOTSUP;
    if (message.msg_iovlen != 0u) {
        if (!message.msg_iov || !user_range_ok((uint64_t)(uintptr_t)message.msg_iov,
                                                sizeof(vector))) {
            return -LEONOS_EFAULT;
        }
        vector = *message.msg_iov;
        data_len = vector.iov_len > UNIX_SOCKET_RX_CAP ? UNIX_SOCKET_RX_CAP
                                                       : (uint32_t)vector.iov_len;
        if (data_len && (!vector.iov_base || !user_range_ok((uint64_t)(uintptr_t)vector.iov_base,
                                                            data_len))) {
            return -LEONOS_EFAULT;
        }
        received = task_socket_read(file, vector.iov_base, data_len);
        if (received < 0) return received;
    } else {
        received = 0;
    }

    if (socket->rx_fd_count && message.msg_control) {
        uint64_t want = CMSG_LEN((uint64_t)socket->rx_fd_count * sizeof(int32_t));
        if (want > message.msg_controllen) {
            message.msg_flags |= MSG_CTRUNC;
        } else if (user_range_ok((uint64_t)(uintptr_t)message.msg_control, want)) {
            struct cmsghdr *header = (struct cmsghdr *)(uintptr_t)message.msg_control;
            header->cmsg_len = want;
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            int32_t *fds = (int32_t *)CMSG_DATA(header);
            for (uint32_t i = 0; i < socket->rx_fd_count; ++i) {
                fds[i] = socket->rx_fds[i];
            }
        }
        socket->rx_fd_count = 0;
    }
    if (user_range_ok((uint64_t)(uintptr_t)user_msg, sizeof(message))) {
        struct msghdr *output = (struct msghdr *)(uintptr_t)user_msg;
        output->msg_controllen = socket->rx_fd_count ? CMSG_LEN((uint64_t)socket->rx_fd_count * sizeof(int32_t)) : 0;
        output->msg_flags = message.msg_flags;
    }
    return received;
}

static int64_t unix_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                    uint64_t a2, uint64_t a3)
{
    struct task *task = sched_current_task();
    struct task_file *file;
    struct unix_socket *socket;
    uint32_t accept4_flags = (number == __NR_accept4) ? (uint32_t)a3 : 0u;
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
    if (number == __NR_socketpair) {
        struct unix_socket *left;
        struct unix_socket *right;
        int left_fd;
        int right_fd;
        if ((int)a0 != AF_UNIX || ((int)a1 & 0x0f) != SOCK_STREAM || a2 != 0 ||
            !user_range_ok(a3, sizeof(int) * 2u)) {
            return -LEONOS_EINVAL;
        }
        left = unix_alloc(task->pid);
        if (!left) return -LEONOS_ENOMEM;
        right = unix_alloc(task->pid);
        if (!right) {
            unix_release_handle(left->handle);
            return -LEONOS_ENOMEM;
        }
        left->state = UNIX_SOCKET_CONNECTED;
        right->state = UNIX_SOCKET_CONNECTED;
        left->peer_handle = right->handle;
        right->peer_handle = left->handle;
        left->peer_pid = task->pid;
        right->peer_pid = task->pid;
        left_fd = unix_alloc_fd(task, left);
        right_fd = unix_alloc_fd(task, right);
        unix_release_handle(left->handle);
        unix_release_handle(right->handle);
        if (left_fd < 0 || right_fd < 0) {
            if (left_fd >= 0) clear_task_file(task_file_for_fd(task, left_fd));
            if (right_fd >= 0) clear_task_file(task_file_for_fd(task, right_fd));
            return -LEONOS_EMFILE;
        }
        ((int *)(uintptr_t)a3)[0] = left_fd;
        ((int *)(uintptr_t)a3)[1] = right_fd;
        return 0;
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
        kernel_wait_queue_remove(&listener->wait_connect, task);
        if (listener->pending_count >= UNIX_SOCKET_BACKLOG_MAX) {
            if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
            kernel_wait_queue_block_current(&listener->wait_connect);
            return -LEONOS_EAGAIN;
        }
        server = unix_alloc(listener->owner_pid);
        if (!server) return -LEONOS_ENOMEM;
        server->state = UNIX_SOCKET_CONNECTED;
        socket->state = UNIX_SOCKET_CONNECTED;
        socket->peer_handle = server->handle;
        socket->peer_pid = listener->owner_pid;
        server->peer_handle = socket->handle;
        server->peer_pid = task->pid;
        listener->pending[listener->pending_count++] = server->handle;
        (void)kernel_wait_queue_wake_one(&listener->wait_accept);
        (void)kernel_wait_queue_wake_one(&listener->wait_connect);
        return 0;
    }
    if (number == __NR_accept4) {
        if (a3 & ~(uint32_t)(SOCK_NONBLOCK | SOCK_CLOEXEC)) return -LEONOS_EINVAL;
        number = __NR_accept;
    }
    if (number == __NR_accept) {
        kernel_wait_queue_remove(&socket->wait_accept, task);
        if (socket->state != UNIX_SOCKET_LISTEN) return -LEONOS_EINVAL;
        if (!socket->pending_count) {
            if (file->flags & LEONOS_O_NONBLOCK) return -LEONOS_EAGAIN;
            kernel_wait_queue_block_current(&socket->wait_accept);
            return -LEONOS_EAGAIN;
        }
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
        {
            struct task_file *accepted_file = task_file_for_fd(task, fd);
            if (accepted_file) {
                if (accept4_flags & SOCK_NONBLOCK) accepted_file->flags |= LEONOS_O_NONBLOCK;
                if (accept4_flags & SOCK_CLOEXEC) accepted_file->fd_flags |= 1u;
            }
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
        unix_wake_peer(socket);
        return 0;
    }
    return -LEONOS_ENOSYS;
}

int64_t syscall_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4)
{
    struct task *task;
    struct task_file *file;
    if (number == __NR_getsockopt) {
        struct task_file *file;
        struct unix_socket *socket;
        int level = (int)a1;
        int option = (int)a2;
        task = sched_current_task();
        file = task_file_for_fd(task, (int)a0);
        socket = unix_from_file(file);
        if (!socket) return -LEONOS_EBADF;
        if (!user_range_ok(a4, sizeof(socklen_t))) return -LEONOS_EFAULT;
        {
            socklen_t *length = (socklen_t *)(uintptr_t)a4;
            if (level == SOL_SOCKET && option == SO_PEERCRED) {
                struct task *peer_task;
                struct ucred credentials = {0};
                if (!user_range_ok(a3, sizeof(credentials)) ||
                    *length < sizeof(credentials)) {
                    return -LEONOS_EINVAL;
                }
                peer_task = sched_find(socket->peer_pid);
                if (peer_task) {
                    credentials.pid = (int32_t)peer_task->pid;
                    credentials.uid = peer_task->uid;
                    credentials.gid = peer_task->gid;
                }
                *(struct ucred *)(uintptr_t)a3 = credentials;
                *length = sizeof(credentials);
                return 0;
            }
            if (level == SOL_SOCKET && option == SO_TYPE) {
                int32_t type = SOCK_STREAM;
                if (!user_range_ok(a3, sizeof(type)) || *length < sizeof(type)) {
                    return -LEONOS_EINVAL;
                }
                *(int32_t *)(uintptr_t)a3 = type;
                *length = sizeof(type);
                return 0;
            }
            if (level == SOL_SOCKET && option == SO_ERROR) {
                int32_t error = 0;
                if (!user_range_ok(a3, sizeof(error)) || *length < sizeof(error)) {
                    return -LEONOS_EINVAL;
                }
                *(int32_t *)(uintptr_t)a3 = error;
                *length = sizeof(error);
                return 0;
            }
        }
        return 0;
    }
    if (number == __NR_sendmsg) {
        task = sched_current_task();
        {
            struct task_file *file = task_file_for_fd(task, (int)a0);
            if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
            return unix_sendmsg(task, file, (const struct msghdr *)(uintptr_t)a1);
        }
    }
    if (number == __NR_recvmsg) {
        task = sched_current_task();
        {
            struct task_file *file = task_file_for_fd(task, (int)a0);
            if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
            return unix_recvmsg(task, file, (const struct msghdr *)(uintptr_t)a1);
        }
    }
    if (number == __NR_send || number == __NR_sendto) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        if (file->flags & TASK_FILE_FLAG_SOCKET_INET) return task_inet_write(file, (const void *)(uintptr_t)a1, (uint32_t)a2);
        if (!(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        return task_socket_write(file, (const void *)(uintptr_t)a1, (uint32_t)a2);
    }
    if (number == __NR_recv || number == __NR_recvfrom) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        if (file->flags & TASK_FILE_FLAG_SOCKET_INET) return task_inet_read(file, (void *)(uintptr_t)a1, (uint32_t)a2);
        if (!(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        return task_socket_read(file, (void *)(uintptr_t)a1, (uint32_t)a2);
    }
    task = sched_current_task();
    if (number == __NR_socket && (int)a0 == AF_INET) return inet_socket_dispatch(number, a0, a1, a2, a3, a4);
    file = task_file_for_fd(task, (int)a0);
    if (number != __NR_socket && file && (file->flags & TASK_FILE_FLAG_SOCKET_INET)) {
        return inet_socket_dispatch(number, a0, a1, a2, a3, a4);
    }
    return unix_socket_dispatch(number, a0, a1, a2, a3);
}
