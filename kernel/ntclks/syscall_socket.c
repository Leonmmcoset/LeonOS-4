/* Unix-domain sockets backed by the kernel object table. */
#include <ntclks/heap.h>
#include <ntclks/net.h>
#include <ntclks/object.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/usercopy.h>
#include <ntclks/wait.h>
#include <ntclks/futex.h>
#include <ntclks/time.h>
#include <ntclks/permissions.h>
#include <leonos/fs.h>
#include <leonos/net.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/tty.h>

#define ntohs(value) ((uint16_t)((((uint16_t)(value) & 0xffu) << 8) | ((uint16_t)(value) >> 8)))
#define htons(value) ntohs(value)
#define ntohl(value) ((((uint32_t)(value) & 0xffu) << 24) | (((uint32_t)(value) & 0xff00u) << 8) | (((uint32_t)(value) & 0xff0000u) >> 8) | ((uint32_t)(value) >> 24))
#define htonl(value) ntohl(value)

#define UNIX_SOCKET_MAX 32u
#define UNIX_SOCKET_BACKLOG_MAX 8u
#define UNIX_SOCKET_RX_CAP 16384u
#define UNIX_SOCKET_FD_TRANSFER_MAX 253u
#define UNIX_SOCKET_CMSG_CAP 65536u

struct unix_rights {
    struct unix_rights *next;
    uint64_t position;
    uint64_t end;
    uint32_t count;
    struct task_file *files[UNIX_SOCKET_FD_TRANSFER_MAX];
};

struct unix_packet {
    struct unix_packet *next;
    uint32_t length;
    uint32_t path_len;
    struct ucred credentials;
    char path[109];
    unsigned char data[];
};

struct unix_receive_result {
    uint32_t flags;
    uint32_t barrier;
    uint32_t path_len;
    struct ucred credentials;
    char path[109];
};

struct unix_stream_credentials {
    struct unix_stream_credentials *next;
    uint64_t start, end;
    struct ucred credentials;
};

enum unix_socket_state { UNIX_SOCKET_OPEN = 0, UNIX_SOCKET_LISTEN = 1,
                         UNIX_SOCKET_CONNECTED = 2 };

struct unix_socket {
    uint32_t used;
    uint32_t refs;
    uint32_t gc_roots;
    uint32_t gc_mark;
    uint32_t handle;
    uint32_t owner_pid;
    uint32_t state;
    uint32_t type;
    uint32_t peer_handle;
    uint32_t peer_pid;
    uint32_t shutdown_read;
    uint32_t shutdown_write;
    int32_t error;
    uint32_t passcred;
    uint32_t receive_lowwater;
    bool bound;
    uint32_t unaccepted;
    uint64_t receive_timeout;
    uint64_t send_timeout;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t pending_count;
    uint32_t backlog;
    uint32_t pending[UNIX_SOCKET_BACKLOG_MAX];
    uint64_t rx_read;
    uint64_t rx_written;
    struct unix_rights *rights;
    struct unix_stream_credentials *cred_head, *cred_tail;
    struct unix_packet *packets;
    struct unix_packet *packet_tail;
    uint32_t packet_count;
    uint32_t packet_bytes;
    char path[109];
    uint32_t path_len;
    char peer_path[109];
    uint32_t peer_path_len;
    char bound_path[LEONOS_FS_PATH_LEN];
    struct ucred credentials;
    struct ucred peer_credentials;
    uint8_t rx[UNIX_SOCKET_RX_CAP];
    struct kernel_wait_queue wait_rx;
    struct kernel_wait_queue wait_tx;
    struct kernel_wait_queue wait_accept;
    struct kernel_wait_queue wait_connect;
};

static struct unix_socket *unix_sockets[UNIX_SOCKET_MAX];

static int unix_block(struct unix_socket *socket, struct task_file *file,
                       struct kernel_wait_queue *queue, bool receiving, uint32_t flags)
{
    struct task *task = sched_current_task();
    if ((file->flags & LEONOS_O_NONBLOCK) || (flags & MSG_DONTWAIT)) return -LEONOS_EAGAIN;
    uint64_t now = time_ticks();
    if (!task->socket_io_deadline) {
        uint64_t timeout = receiving ? socket->receive_timeout : socket->send_timeout;
        task->socket_io_timed = timeout != UINT64_MAX;
        task->socket_io_deadline = UINT64_MAX - now < timeout ? UINT64_MAX : now + timeout;
    }
    if (task->socket_io_deadline <= now) return -LEONOS_EAGAIN;
    if (sched_task_pending(task) & ~task->blocked_signals) return KERNEL_SYSCALL_BLOCKED;
    if (kernel_wait_queue_add(queue, task) < 0) return -LINUX_ENOBUFS;
    if (task->socket_io_deadline != UINT64_MAX) sched_sleep_current_until(task->socket_io_deadline);
    else sched_block_current();
    return KERNEL_SYSCALL_BLOCKED;
}

static bool unix_credentials_equal(struct ucred left, struct ucred right)
{
    return left.pid == right.pid && left.uid == right.uid && left.gid == right.gid;
}

static struct ucred unix_send_credentials(struct unix_socket *socket,
                                         struct unix_socket *peer,
                                         const struct ucred *explicit_credentials)
{
    if (explicit_credentials) return *explicit_credentials;
    struct task *task = sched_current_task();
    if (socket->passcred || peer->passcred || peer->unaccepted)
        return (struct ucred){(int32_t)sched_task_tgid(task), task->uid, task->gid};
    return (struct ucred){0, 65534, 65534};
}

static void unix_rights_free(struct unix_rights *rights)
{
    if (!rights) return;
    for (uint32_t i = 0; i < rights->count; ++i) {
        --rights->files[i]->scm_references;
        task_file_put(rights->files[i]);
    }
    kernel_free(rights);
}

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

static int unix_socket_path(const void *user_addr, uint32_t user_len, char *path,
                            uint32_t *length, char *resolved)
{
    const struct sockaddr_un *address;
    uint32_t path_len;
    if (user_len < sizeof(sa_family_t) || user_len > sizeof(struct sockaddr_un)) return -LEONOS_EINVAL;
    if (!user_addr || !user_range_ok((uint64_t)(uintptr_t)user_addr, user_len)) {
        return -LEONOS_EFAULT;
    }
    address = (const struct sockaddr_un *)user_addr;
    if (address->sun_family != AF_UNIX) return -LEONOS_EINVAL;
    path_len = user_len - sizeof(sa_family_t);
    resolved[0] = 0;
    for (uint32_t i = 0; i < path_len; ++i) path[i] = address->sun_path[i];
    path[path_len] = 0;
    *length = path_len;
    if (!path_len || !path[0]) return 0;
    for (uint32_t i = 0; i < path_len; ++i) {
        if (!path[i]) { *length = i + 1; break; }
        if (i + 1 == path_len) *length = path_len + 1;
    }
    struct task *task = sched_current_task();
    return fs_permissions_resolve(task, sched_task_cwd(task), path, resolved, LEONOS_FS_PATH_LEN, false);
}

static int unix_text_equal(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static struct unix_socket *unix_find_path(const char *path, uint32_t length, const char *resolved)
{
    for (uint32_t i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *socket = unix_sockets[i];
        if (!socket || !socket->used || !socket->bound || !socket->path_len) continue;
        if (resolved[0]) {
            if (socket->bound_path[0] && unix_text_equal(socket->bound_path, resolved)) return socket;
        } else if (!socket->path[0] && socket->path_len == length) {
            uint32_t j;
            for (j = 0; j < length && path[j] == socket->path[j]; ++j) {}
            if (j == length) return socket;
        }
    }
    return NULL;
}

static void unix_autobind(struct unix_socket *socket)
{
    static uint32_t next_autobind;
    if (socket->path_len) return;
    do {
        uint32_t id = ++next_autobind;
        socket->path[0] = 0;
        for (unsigned i = 0; i < 5; ++i)
            socket->path[i + 1] = "0123456789abcdef"[(id >> (4 * i)) & 15];
    } while (unix_find_path(socket->path, 6, ""));
    socket->path_len = 6;
    socket->bound = true;
}

void task_socket_unlink_path(const char *path)
{
    for (uint32_t i = 0; i < UNIX_SOCKET_MAX; ++i)
        if (unix_sockets[i] && unix_text_equal(unix_sockets[i]->bound_path, path))
            unix_sockets[i]->bound_path[0] = 0;
}

void task_socket_rename_path(const char *old_path, const char *new_path)
{
    if (unix_text_equal(old_path, new_path)) return;
    task_socket_unlink_path(new_path);
    uint32_t length = 0, new_length = 0;
    while (old_path[length]) ++length;
    while (new_path[new_length]) ++new_length;
    for (uint32_t i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *socket = unix_sockets[i];
        if (!socket) continue;
        uint32_t j;
        for (j = 0; j < length && socket->bound_path[j] == old_path[j]; ++j) {}
        if (j != length || (socket->bound_path[j] && socket->bound_path[j] != '/')) continue;
        char moved[LEONOS_FS_PATH_LEN];
        uint32_t n = 0;
        while (socket->bound_path[length + n]) ++n;
        if (new_length + n >= sizeof(moved)) continue;
        for (j = 0; j < new_length; ++j) moved[j] = new_path[j];
        for (j = 0; j <= n; ++j) moved[new_length + j] = socket->bound_path[length + j];
        for (j = 0; j <= new_length + n; ++j) socket->bound_path[j] = moved[j];
    }
}

static int unix_name_out(const char *path, uint32_t path_len, uint64_t address, uint64_t length)
{
    if (!user_range_writable(length, sizeof(socklen_t))) return -LEONOS_EFAULT;
    socklen_t capacity = *(socklen_t *)(uintptr_t)length;
    if ((int32_t)capacity < 0) return -LEONOS_EINVAL;
    uint32_t size = 2 + path_len;
    uint32_t copied = capacity < size ? capacity : size;
    if (!user_range_writable(address, copied)) return -LEONOS_EFAULT;
    unsigned char bytes[111] = {AF_UNIX, 0};
    for (uint32_t i = 0; i < path_len; ++i) bytes[2 + i] = path[i];
    for (uint32_t i = 0; i < copied; ++i) ((unsigned char *)(uintptr_t)address)[i] = bytes[i];
    *(socklen_t *)(uintptr_t)length = size;
    return 0;
}

static int unix_address_out(const struct unix_socket *socket, uint64_t address, uint64_t length)
{
    return unix_name_out(socket ? socket->path : NULL, socket ? socket->path_len : 0, address, length);
}

static struct unix_socket *unix_alloc(uint32_t owner_pid, uint32_t type)
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
        .state = UNIX_SOCKET_OPEN, .type = type,
        .receive_timeout = UINT64_MAX, .send_timeout = UINT64_MAX,
        .receive_lowwater = 1,
    };
    struct task *owner = sched_find(owner_pid);
    if (owner) socket->credentials = (struct ucred){(int32_t)sched_task_tgid(owner), owner->euid, owner->egid};
    socket->peer_credentials = (struct ucred){0, (uint32_t)-1, (uint32_t)-1};
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
    bool unread = socket->rx_head != socket->rx_tail || socket->packets || socket->unaccepted;
    while (socket->cred_head) {
        struct unix_stream_credentials *credentials = socket->cred_head;
        socket->cred_head = credentials->next;
        kernel_free(credentials);
    }
    while (socket->packets) {
        struct unix_packet *p = socket->packets;
        socket->packets = p->next;
        kernel_free(p);
    }
    while (socket->rights) {
        struct unix_rights *rights = socket->rights;
        socket->rights = rights->next;
        unix_rights_free(rights);
    }
    for (uint32_t i = 0; i < socket->pending_count; ++i) unix_release_handle(socket->pending[i]);
    if (socket->peer_handle) {
        struct unix_socket *peer = unix_from_handle(socket->peer_handle);
        if (peer) {
            if (socket->type == SOCK_STREAM || socket->type == SOCK_SEQPACKET) {
                peer->shutdown_read = peer->shutdown_write = 1;
                if (unread) peer->error = LINUX_ECONNRESET;
            }
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
    if (!task || !socket) return -LEONOS_EINVAL;
    struct task_file *file;
    int fd = task_allocate_fd(task, 0, &file);
    if (fd < 0) return fd;
    file->flags = TASK_FILE_FLAG_SOCKET | TASK_FILE_FLAG_SOCKET_UNIX | LEONOS_O_RDWR;
    file->node = (struct storage_node){.type = LEONOS_FS_TYPE_SOCKET};
    file->aux = socket->handle;
    ++socket->refs;
    return fd;
}

static int unix_clone_fd_to_task(struct task *target, struct task_file *source)
{
    if (!target || !source || !source->used) return -LEONOS_EBADF;
    struct task_file retained = {0};
    int ret = task_file_reference(&retained, source);
    if (ret < 0) return ret;
    struct task_file *file;
    int fd = task_allocate_fd(target, 0, &file);
    if (fd < 0) { clear_task_file(&retained); return fd; }
    *file = retained;
    return fd;
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

void task_socket_collect(void)
{
    static bool collecting;
    if (collecting) return;
    collecting = true;
    for (unsigned i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *s = unix_sockets[i];
        if (!s) continue;
        s->gc_roots = s->refs;
        s->gc_mark = 0;
        for (struct unix_rights *r = s->rights; r; r = r->next)
            for (unsigned j = 0; j < r->count; ++j) r->files[j]->scm_gc_seen = 0;
    }
    /* References owned only by queued SCM files and pending accepts are
     * graph edges. Everything else is an externally reachable root. */
    for (unsigned i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *s = unix_sockets[i];
        if (!s) continue;
        for (unsigned j = 0; j < s->pending_count; ++j) {
            struct unix_socket *p = unix_from_handle(s->pending[j]);
            if (p && p->gc_roots) --p->gc_roots;
        }
        for (struct unix_rights *r = s->rights; r; r = r->next) {
            for (unsigned j = 0; j < r->count; ++j) {
                struct task_file *f = r->files[j];
                struct unix_socket *p = unix_from_file(f);
                if (f->scm_gc_seen) continue;
                f->scm_gc_seen = 1;
                if (p && f->references == f->scm_references && p->gc_roots) --p->gc_roots;
            }
        }
    }
    for (unsigned i = 0; i < UNIX_SOCKET_MAX; ++i)
        if (unix_sockets[i]) unix_sockets[i]->gc_mark = unix_sockets[i]->gc_roots != 0;
    bool changed;
    do {
        changed = false;
        for (unsigned i = 0; i < UNIX_SOCKET_MAX; ++i) {
            struct unix_socket *s = unix_sockets[i];
            if (!s || !s->gc_mark) continue;
            for (unsigned j = 0; j < s->pending_count; ++j) {
                struct unix_socket *p = unix_from_handle(s->pending[j]);
                if (p && !p->gc_mark) { p->gc_mark = 1; changed = true; }
            }
            for (struct unix_rights *r = s->rights; r; r = r->next)
                for (unsigned j = 0; j < r->count; ++j) {
                    struct unix_socket *p = unix_from_file(r->files[j]);
                    if (p && !p->gc_mark) { p->gc_mark = 1; changed = true; }
                }
        }
    } while (changed);
    struct unix_rights *garbage = NULL;
    for (unsigned i = 0; i < UNIX_SOCKET_MAX; ++i) {
        struct unix_socket *s = unix_sockets[i];
        if (!s || s->gc_mark) continue;
        while (s->rights) {
            struct unix_rights *r = s->rights;
            s->rights = r->next;
            r->next = garbage;
            garbage = r;
        }
    }
    while (garbage) {
        struct unix_rights *r = garbage;
        garbage = r->next;
        unix_rights_free(r);
    }
    collecting = false;
}

static int unix_read_data(struct task_file *file, void *buffer, uint32_t length,
                          uint32_t flags, struct unix_rights **ancillary,
                          struct unix_receive_result *metadata)
{
    struct unix_socket *socket = unix_from_file(file);
    uint32_t count = 0;
    if (!socket || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
    if (!buffer && length) return -LEONOS_EFAULT;
    if (metadata) *metadata = (struct unix_receive_result){.credentials = {0, 65534, 65534}};
    if (socket->type != SOCK_STREAM) {
        kernel_wait_queue_remove(&socket->wait_rx, sched_current_task());
        struct unix_packet *p = socket->packets;
        if (socket->error) {
            int error = socket->error;
            socket->error = 0;
            return -error;
        }
        if (!p) {
            if (socket->shutdown_read) return 0;
            struct unix_socket *peer = unix_from_handle(socket->peer_handle);
            if (socket->type == SOCK_SEQPACKET && (!peer || peer->shutdown_write)) return 0;
            return unix_block(socket, file, &socket->wait_rx, true, flags);
        }
        uint32_t copied = length < p->length ? length : p->length;
        for (unsigned i = 0; i < copied; ++i) ((uint8_t *)buffer)[i] = p->data[i];
        if (metadata) {
            metadata->flags = copied < p->length ? MSG_TRUNC : 0;
            metadata->credentials = p->credentials;
            metadata->path_len = p->path_len;
            for (unsigned i = 0; i < p->path_len; ++i) metadata->path[i] = p->path[i];
        }
        int result = flags & MSG_TRUNC ? (int)p->length : (int)copied;
        struct unix_rights *rights = socket->rights;
        if (rights && rights->position == socket->rx_read) {
            if (ancillary) *ancillary = rights;
            if (!(flags & MSG_PEEK)) {
                socket->rights = rights->next;
                if (!ancillary) unix_rights_free(rights);
            }
        }
        if (!(flags & MSG_PEEK)) {
            socket->packets = p->next;
            if (!socket->packets) socket->packet_tail = NULL;
            --socket->packet_count;
            socket->packet_bytes -= p->length;
            ++socket->rx_read;
            kernel_free(p);
            (void)kernel_wait_queue_wake_all(&socket->wait_tx);
        }
        return result;
    }
    if (socket->state != UNIX_SOCKET_CONNECTED) return -LEONOS_ENOTSUP;
    if (!length) return 0;
    kernel_wait_queue_remove(&socket->wait_rx, sched_current_task());
    uint32_t tail = socket->rx_tail;
    struct unix_stream_credentials *credentials = socket->cred_head;
    struct ucred received = {0, 65534, 65534};
    if (credentials && credentials->start <= socket->rx_read) received = credentials->credentials;
    if (metadata) metadata->credentials = received;
    if (socket->passcred) {
        struct task *task = sched_current_task();
        if (task->socket_receive_done && socket->rx_tail != socket->rx_head &&
            !unix_credentials_equal(received, (struct ucred){task->socket_receive_pid,
                                      task->socket_receive_uid, task->socket_receive_gid})) return 0;
        if (credentials) {
            uint64_t boundary = credentials->start > socket->rx_read ? credentials->start : credentials->end;
            if (length > boundary - socket->rx_read) length = (uint32_t)(boundary - socket->rx_read);
        }
    }
    struct unix_rights *rights = socket->rights;
    /* Linux consumes preceding ordinary bytes and stops at the end of the
     * sendmsg segment carrying SCM_RIGHTS. Peeking retains this same barrier. */
    if (rights && rights->end > socket->rx_read && length > rights->end - socket->rx_read)
        length = (uint32_t)(rights->end - socket->rx_read);
    while (socket->rx_tail != socket->rx_head && count < length) {
        ((uint8_t *)buffer)[count++] = socket->rx[socket->rx_tail];
        socket->rx_tail = (socket->rx_tail + 1u) % UNIX_SOCKET_RX_CAP;
    }
    if (count) {
        if (rights && rights->position < socket->rx_read + count) {
            if (metadata) metadata->barrier = 1;
            if (ancillary) *ancillary = rights;
            if (!(flags & MSG_PEEK)) {
                socket->rights = rights->next;
                if (!ancillary) unix_rights_free(rights);
            }
        }
        if (flags & MSG_PEEK) socket->rx_tail = tail;
        else {
            socket->rx_read += count;
            while (socket->cred_head && socket->cred_head->end <= socket->rx_read) {
                struct unix_stream_credentials *expired = socket->cred_head;
                socket->cred_head = expired->next;
                kernel_free(expired);
            }
            if (!socket->cred_head) socket->cred_tail = NULL;
        }
        unix_wake_peer(socket);
        return (int)count;
    }
    struct unix_socket *peer = unix_from_handle(socket->peer_handle);
    if (socket->error) {
        int error = socket->error;
        socket->error = 0;
        return -error;
    }
    if (socket->shutdown_read || !peer || peer->shutdown_write) return 0;
    return unix_block(socket, file, &socket->wait_rx, true, flags);
}

uint64_t task_socket_cancel_receive(struct task *task)
{
    uint64_t count = task->socket_receive_done;
    struct task_file *file = task->socket_receive_file;
    task->socket_receive_file = NULL;
    task->socket_receive_done = 0;
    task->socket_receive_pid = 0;
    task->socket_receive_uid = task->socket_receive_gid = 65534;
    task->socket_receive_message = task->socket_receive_control_capacity = 0;
    task->socket_receive_name_capacity = 0;
    if (file) task_file_put(file);
    return count;
}

static int64_t unix_receive_progress(struct task *task, struct task_file *file,
                                   uint64_t total, int result, uint32_t flags,
                                   const struct unix_receive_result *metadata)
{
    struct unix_socket *socket = unix_from_file(file);
    if (!socket || (flags & MSG_PEEK) || socket->type != SOCK_STREAM)
        return result;
    uint64_t done = task->socket_receive_done + (result > 0 ? (uint32_t)result : 0);
    uint64_t target = flags & MSG_WAITALL ? total : socket->receive_lowwater;
    if (target > total) target = total;
    struct unix_socket *peer = unix_from_handle(socket->peer_handle);
    bool waiting = result == KERNEL_SYSCALL_BLOCKED ||
        (result > 0 && done < target && !metadata->barrier && peer && !peer->shutdown_write &&
         !socket->shutdown_read && !(flags & MSG_DONTWAIT) && !(file->flags & LEONOS_O_NONBLOCK));
    if (waiting) {
        if (!task->socket_receive_file) task->socket_receive_file = task_file_get(file);
        if (!task->socket_receive_file) return done ? (int64_t)done : -LEONOS_ENOMEM;
        if (!task->socket_receive_done && result > 0) {
            task->socket_receive_pid = metadata->credentials.pid;
            task->socket_receive_uid = metadata->credentials.uid;
            task->socket_receive_gid = metadata->credentials.gid;
        }
        task->socket_receive_done = done;
        if (socket->rx_head == socket->rx_tail) {
            int ret = unix_block(socket, file, &socket->wait_rx, true, flags);
            if (ret != KERNEL_SYSCALL_BLOCKED) {
                task_socket_cancel_receive(task);
                return done ? (int64_t)done : ret;
            }
        }
        return KERNEL_SYSCALL_BLOCKED;
    }
    task_socket_cancel_receive(task);
    return done ? (int64_t)done : result;
}

int task_socket_read(struct task_file *file, void *buffer, uint32_t length)
{
    struct task *task = sched_current_task();
    uint64_t already = task->socket_receive_done;
    if (already > length) { task_socket_cancel_receive(task); return -LEONOS_EINVAL; }
    struct unix_receive_result metadata;
    int ret = unix_read_data(file, (uint8_t *)buffer + already, length - already,
                             0, NULL, &metadata);
    return (int)unix_receive_progress(task, file, length, ret, 0, &metadata);
}

static int unix_packet_peer(struct unix_socket *socket, const void *address,
                             uint32_t length, struct unix_socket **out)
{
    *out = NULL;
    if (!address) {
        *out = unix_from_handle(socket->peer_handle);
        return *out ? 0 : socket->type == SOCK_DGRAM
            ? (socket->state == UNIX_SOCKET_CONNECTED ? -LINUX_ECONNREFUSED : -LINUX_EDESTADDRREQ)
            : -LINUX_ENOTCONN;
    }
    char path[109], resolved[LEONOS_FS_PATH_LEN];
    uint32_t path_len;
    int result = unix_socket_path(address, length, path, &path_len, resolved);
    if (result < 0) return result;
    if (!path_len) return -LEONOS_EINVAL;
    if (resolved[0]) {
        result = fs_permissions_check(sched_current_task(), resolved, FS_ACCESS_WRITE, false);
        if (result < 0) return result;
    }
    *out = unix_find_path(path, path_len, resolved);
    if (!*out) return -LINUX_ECONNREFUSED;
    if ((*out)->type != socket->type) return -LINUX_EPROTOTYPE;
    return 0;
}

static int unix_send_packet(struct task_file *file, struct unix_socket *peer,
                             const void *data, uint32_t length, uint32_t flags,
                             const struct ucred *credentials)
{
    struct unix_socket *socket = unix_from_file(file);
    if (socket->shutdown_write) return -LEONOS_EPIPE;
    if (peer->shutdown_read) return -LINUX_ECONNREFUSED;
    if (socket->type == SOCK_DGRAM && peer->peer_handle && peer->peer_handle != socket->handle)
        return -LEONOS_EPERM;
    if (length > 65536) return -LINUX_EMSGSIZE;
    if (socket->passcred) unix_autobind(socket);
    kernel_wait_queue_remove(&peer->wait_tx, sched_current_task());
    if (peer->packet_count >= 10 || peer->packet_bytes > 65536 - length) {
        return unix_block(socket, file, &peer->wait_tx, false, flags);
    }
    struct unix_packet *packet = kernel_malloc(sizeof(*packet) + length);
    if (!packet) return -LEONOS_ENOMEM;
    *packet = (struct unix_packet){.length = length, .path_len = socket->path_len};
    packet->credentials = unix_send_credentials(socket, peer, credentials);
    for (unsigned i = 0; i < socket->path_len; ++i) packet->path[i] = socket->path[i];
    for (unsigned i = 0; i < length; ++i) packet->data[i] = ((const uint8_t *)data)[i];
    if (peer->packet_tail) peer->packet_tail->next = packet;
    else peer->packets = packet;
    peer->packet_tail = packet;
    ++peer->packet_count;
    peer->packet_bytes += length;
    ++peer->rx_written;
    (void)kernel_wait_queue_wake_all(&peer->wait_rx);
    return (int)length;
}

static int unix_write_data(struct task_file *file, const void *buffer, uint32_t length,
                            const struct ucred *credentials)
{
    struct unix_socket *socket = unix_from_file(file);
    struct unix_socket *peer;
    uint32_t count = 0;
    uint32_t free_space;
    if (!socket || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
    if (!buffer && length) return -LEONOS_EFAULT;
    if (socket->type != SOCK_STREAM) {
        int result = unix_packet_peer(socket, NULL, 0, &peer);
        return result < 0 ? result : unix_send_packet(file, peer, buffer, length, 0, credentials);
    }
    if (socket->state != UNIX_SOCKET_CONNECTED || socket->shutdown_write) return -LEONOS_EPIPE;
    peer = unix_from_handle(socket->peer_handle);
    if (!peer || peer->shutdown_read) return -LEONOS_EPIPE;
    kernel_wait_queue_remove(&socket->wait_tx, sched_current_task());
    if (!length) return 0;
    free_space = (peer->rx_tail + UNIX_SOCKET_RX_CAP - peer->rx_head - 1u) %
                 UNIX_SOCKET_RX_CAP;
    if (!free_space) {
        return unix_block(socket, file, &socket->wait_tx, false, 0);
    }
    if (length > free_space) length = free_space;
    struct ucred sent = unix_send_credentials(socket, peer, credentials);
    if (sent.pid) {
        struct unix_stream_credentials *segment = peer->cred_tail;
        if (!segment || segment->end != peer->rx_written ||
            !unix_credentials_equal(segment->credentials, sent)) {
            segment = kernel_malloc(sizeof(*segment));
            if (!segment) return -LEONOS_ENOMEM;
            *segment = (struct unix_stream_credentials){.start = peer->rx_written,
                .end = peer->rx_written, .credentials = sent};
            if (peer->cred_tail) peer->cred_tail->next = segment;
            else peer->cred_head = segment;
            peer->cred_tail = segment;
        }
        segment->end += length;
    }
    while (count < length) {
        uint32_t next = (peer->rx_head + 1u) % UNIX_SOCKET_RX_CAP;
        peer->rx[peer->rx_head] = ((const uint8_t *)buffer)[count++];
        peer->rx_head = next;
    }
    peer->rx_written += count;
    (void)kernel_wait_queue_wake_all(&peer->wait_rx);
    return (int)count;
}

int task_socket_write(struct task_file *file, const void *buffer, uint32_t length)
{
    return unix_write_data(file, buffer, length, NULL);
}

int task_socket_vector(struct task_file *file, const struct iovec *vectors,
                       uint32_t count, uint64_t total, bool writing)
{
    struct unix_socket *socket = unix_from_file(file);
    if (!socket) return -LEONOS_EBADF;
    if (writing && socket->type != SOCK_STREAM && total > 65536) return -LINUX_EMSGSIZE;
    struct task *task = sched_current_task();
    uint64_t already = writing ? 0 : task->socket_receive_done;
    if (already > total) { task_socket_cancel_receive(task); return -LEONOS_EINVAL; }
    uint32_t limit = socket->type == SOCK_STREAM ? UNIX_SOCKET_RX_CAP : 65536;
    uint32_t length = total - already < limit ? (uint32_t)(total - already) : limit;
    if (!length) return 0;
    uint8_t *buffer = kernel_malloc(length);
    if (!buffer) return -LEONOS_ENOMEM;
    uint32_t copied = 0;
    if (writing) {
        for (uint32_t i = 0; i < count && copied < length; ++i) {
            uint64_t n = vectors[i].iov_len;
            if (n > length - copied) n = length - copied;
            for (uint64_t j = 0; j < n; ++j)
                buffer[copied++] = ((const uint8_t *)vectors[i].iov_base)[j];
        }
    }
    /* One vector operation is one packet or one stream receive attempt. */
    struct unix_receive_result metadata;
    int ret = writing ? task_socket_write(file, buffer, length)
                      : unix_read_data(file, buffer, length, 0, NULL, &metadata);
    if (!writing && ret > 0) {
        uint64_t skip = already;
        for (uint32_t i = 0; i < count && copied < (uint32_t)ret; ++i) {
            uint64_t n = vectors[i].iov_len;
            if (skip >= n) { skip -= n; continue; }
            uint64_t offset = skip;
            n -= skip;
            skip = 0;
            if (n > (uint32_t)ret - copied) n = (uint32_t)ret - copied;
            for (uint64_t j = 0; j < n; ++j)
                ((uint8_t *)vectors[i].iov_base)[offset + j] = buffer[copied++];
        }
    }
    kernel_free(buffer);
    if (!writing) ret = (int)unix_receive_progress(task, file, total, ret, 0, &metadata);
    if (ret == -LEONOS_EPIPE && writing) sched_signal_user_task(sched_current_pid(), 13);
    return ret;
}

/**
 * @brief Report socket readiness, including errors and shutdown without requiring requested bits.
 * @param file Retained Unix socket open file description.
 * @param events Requested Linux poll bits.
 * @return Requested readiness plus unconditional POLLERR/POLLHUP, or POLLNVAL.
 */
short task_socket_poll(const struct task_file *file, short events)
{
    struct unix_socket *socket = unix_from_file(file);
    short result = 0;
    if (!socket) return POLLNVAL;
    if (socket->error) result |= POLLERR;
    if ((socket->shutdown_read && socket->shutdown_write) ||
        (socket->state == UNIX_SOCKET_OPEN && socket->type != SOCK_DGRAM)) result |= POLLHUP;
    if (socket->shutdown_read) result |= POLLIN | POLLRDNORM | POLLRDHUP;
    bool readable = socket->state == UNIX_SOCKET_LISTEN ? socket->pending_count != 0 :
        socket->type == SOCK_STREAM ? socket->rx_head != socket->rx_tail : socket->packets != NULL;
    if (readable) result |= POLLIN | POLLRDNORM;
    struct unix_socket *peer = unix_from_handle(socket->peer_handle);
    bool writable = !peer || (socket->type == SOCK_STREAM ?
        (peer->rx_head + 1) % UNIX_SOCKET_RX_CAP != peer->rx_tail :
        peer->packet_count < 10 && peer->packet_bytes < 65536);
    if (socket->state != UNIX_SOCKET_LISTEN && writable)
        result |= POLLOUT | POLLWRNORM | POLLWRBAND;
    return result & (events | POLLERR | POLLHUP);
}

/**
 * @brief Query queued receive bytes or update a socket OFD's nonblocking flag.
 * @param file Retained socket open file description.
 * @param request Native FIONREAD or FIONBIO request.
 * @param address User pointer to one 32-bit result or input flag.
 * @return Zero, or negative EBADF/ENOTTY/EINVAL/EFAULT.
 */
int task_socket_ioctl(struct task_file *file, uint64_t request, uint64_t address)
{
    struct unix_socket *socket = unix_from_file(file);
    if (!socket) return -LEONOS_EBADF;
    if (request == FIONBIO) {
        if (!user_range_ok(address, sizeof(int32_t))) return -LEONOS_EFAULT;
        if (*(const int32_t *)(uintptr_t)address) file->flags |= LEONOS_O_NONBLOCK;
        else file->flags &= ~LEONOS_O_NONBLOCK;
        return 0;
    }
    if (request != FIONREAD) return -LEONOS_ENOTTY;
    if (socket->state == UNIX_SOCKET_LISTEN) return -LEONOS_EINVAL;
    if (!user_range_writable(address, sizeof(int32_t))) return -LEONOS_EFAULT;
    uint32_t bytes = socket->type == SOCK_STREAM
        ? (socket->rx_head + UNIX_SOCKET_RX_CAP - socket->rx_tail) % UNIX_SOCKET_RX_CAP
        : socket->type == SOCK_SEQPACKET ? socket->packet_bytes
        : socket->packets ? socket->packets->length : 0;
    *(int32_t *)(uintptr_t)address = (int32_t)bytes;
    return 0;
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
        if (a1 & ~(uint32_t)(SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC)) return -LEONOS_EINVAL;
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
        int fd = task_allocate_fd(task, 0, &file);
        if (fd >= 0) {
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
        return fd;
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

static int unix_message_vectors(const struct msghdr *message, int writing, uint64_t *total)
{
    *total = 0;
    if (message->msg_iovlen > 1024) return -LINUX_EMSGSIZE;
    if (!user_range_ok((uintptr_t)message->msg_iov, message->msg_iovlen * sizeof(struct iovec)))
        return -LEONOS_EFAULT;
    for (uint64_t i = 0; i < message->msg_iovlen; ++i) {
        const struct iovec *v = &message->msg_iov[i];
        if (v->iov_len > INT64_MAX - *total) return -LEONOS_EINVAL;
        if (v->iov_len && !(writing ? user_range_writable((uintptr_t)v->iov_base, v->iov_len) :
                           user_range_ok((uintptr_t)v->iov_base, v->iov_len))) return -LEONOS_EFAULT;
        *total += v->iov_len;
    }
    return 0;
}

static int unix_message_rights(struct task *task, const struct msghdr *message,
                               struct unix_rights **out, struct ucred *credentials,
                               bool *explicit_credentials)
{
    *out = NULL;
    *explicit_credentials = false;
    if (!message->msg_controllen) return 0;
    if (message->msg_controllen > UNIX_SOCKET_CMSG_CAP) return -LINUX_ENOBUFS;
    if (!user_range_ok((uintptr_t)message->msg_control, message->msg_controllen)) return -LEONOS_EFAULT;
    struct unix_rights *rights = kernel_malloc(sizeof(*rights));
    if (!rights) return -LEONOS_ENOMEM;
    *rights = (struct unix_rights){0};
    int result = 0;
    uint64_t position = 0;
    while (position + sizeof(struct cmsghdr) <= message->msg_controllen) {
        struct cmsghdr header;
        const uint8_t *bytes = (const uint8_t *)message->msg_control + position;
        for (unsigned i = 0; i < sizeof(header); ++i) ((uint8_t *)&header)[i] = bytes[i];
        if (header.cmsg_len < CMSG_LEN(0) || header.cmsg_len > message->msg_controllen - position) {
            result = -LEONOS_EINVAL; break;
        }
        if (header.cmsg_level != SOL_SOCKET) { position += CMSG_ALIGN(header.cmsg_len); continue; }
        if (header.cmsg_type == SCM_CREDENTIALS) {
            if (header.cmsg_len != CMSG_LEN(sizeof(*credentials))) { result = -LEONOS_EINVAL; break; }
            for (unsigned i = 0; i < sizeof(*credentials); ++i)
                ((uint8_t *)credentials)[i] = bytes[CMSG_LEN(0) + i];
            if (credentials->uid == UINT32_MAX || credentials->gid == UINT32_MAX) {
                result = -LEONOS_EINVAL; break;
            }
            if (task->euid != 0 &&
                (credentials->pid != (int32_t)sched_task_tgid(task) ||
                 (credentials->uid != task->uid && credentials->uid != task->euid && credentials->uid != task->suid) ||
                 (credentials->gid != task->gid && credentials->gid != task->egid && credentials->gid != task->sgid))) {
                result = -LEONOS_EPERM; break;
            }
            if (credentials->pid <= 0 || !sched_find((uint32_t)credentials->pid)) {
                result = -LINUX_ESRCH; break;
            }
            *explicit_credentials = true;
            position += CMSG_ALIGN(header.cmsg_len);
            continue;
        }
        if (header.cmsg_type != SCM_RIGHTS) {
            result = -LEONOS_EINVAL; break;
        }
        uint64_t count = (header.cmsg_len - CMSG_LEN(0)) / sizeof(int);
        if (count > UNIX_SOCKET_FD_TRANSFER_MAX - rights->count) { result = -LEONOS_EINVAL; break; }
        for (uint64_t i = 0; i < count; ++i) {
            int fd;
            for (unsigned j = 0; j < sizeof(fd); ++j)
                ((uint8_t *)&fd)[j] = bytes[CMSG_LEN(0) + i * sizeof(fd) + j];
            struct task_file *source = task_file_for_fd(task, fd);
            if (!source) { result = -LEONOS_EBADF; break; }
            struct task_file *held = task_file_get(source);
            if (!held) { result = -LEONOS_ENOMEM; break; }
            ++held->scm_references;
            rights->files[rights->count++] = held;
        }
        if (result) break;
        position += CMSG_ALIGN(header.cmsg_len);
    }
    if (result || !rights->count) unix_rights_free(rights);
    else *out = rights;
    return result;
}

static int unix_sendmsg(struct task *task, struct task_file *file,
                       const struct msghdr *user_msg, uint32_t flags)
{
    if (!user_range_ok((uintptr_t)user_msg, sizeof(*user_msg))) return -LEONOS_EFAULT;
    struct msghdr message = *user_msg;
    struct unix_socket *socket = unix_from_file(file);
    if (!socket) return -LINUX_ENOTSOCK;
    if (flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL | MSG_MORE | MSG_EOR)) return -LINUX_EOPNOTSUPP;
    uint64_t total;
    int ret = unix_message_vectors(&message, 0, &total);
    if (ret) return ret;
    if (socket->type != SOCK_STREAM && total > 65536) return -LINUX_EMSGSIZE;
    struct unix_rights *rights = NULL;
    struct ucred credentials;
    bool explicit_credentials;
    ret = unix_message_rights(task, &message, &rights, &credentials, &explicit_credentials);
    if (ret) return ret;
    uint32_t length = socket->type == SOCK_STREAM && total > UNIX_SOCKET_RX_CAP
        ? UNIX_SOCKET_RX_CAP : (uint32_t)total;
    uint8_t *data = length ? kernel_malloc(length) : NULL;
    if (length && !data) { unix_rights_free(rights); return -LEONOS_ENOMEM; }
    uint32_t copied = 0;
    for (uint64_t i = 0; i < message.msg_iovlen && copied < length; ++i) {
        uint64_t n = message.msg_iov[i].iov_len;
        if (n > length - copied) n = length - copied;
        for (uint64_t j = 0; j < n; ++j) data[copied++] = ((uint8_t *)message.msg_iov[i].iov_base)[j];
    }
    struct unix_socket *peer = unix_from_handle(socket->peer_handle);
    if (socket->type != SOCK_STREAM) {
        ret = unix_packet_peer(socket, message.msg_name, message.msg_namelen, &peer);
        if (ret < 0) { kernel_free(data); unix_rights_free(rights); return ret; }
    } else if (message.msg_name) {
        kernel_free(data); unix_rights_free(rights); return -LEONOS_EISCONN;
    }
    uint64_t position = peer ? peer->rx_written : 0;
    struct task_file local = *file;
    if (flags & MSG_DONTWAIT) local.flags |= LEONOS_O_NONBLOCK;
    ret = socket->type == SOCK_STREAM ? unix_write_data(&local, data ? data : (uint8_t *)"", length,
            explicit_credentials ? &credentials : NULL)
        : unix_send_packet(&local, peer, data, length, flags, explicit_credentials ? &credentials : NULL);
    if (data) kernel_free(data);
    if ((ret > 0 || (ret == 0 && socket->type != SOCK_STREAM)) && rights && peer) {
        rights->position = position;
        rights->end = socket->type == SOCK_STREAM ? position + (uint32_t)ret : position + 1;
        struct unix_rights **tail = &peer->rights;
        while (*tail) tail = &(*tail)->next;
        *tail = rights;
    } else unix_rights_free(rights);
    if (ret == -LEONOS_EPIPE && !(flags & MSG_NOSIGNAL)) sched_signal_user_task(task->pid, 13);
    return ret;
}

static uint64_t unix_put_credentials(struct msghdr *message, uint64_t capacity,
                                      const struct ucred *credentials)
{
    uint64_t length = CMSG_LEN(sizeof(*credentials));
    if (capacity < length) message->msg_flags |= MSG_CTRUNC;
    if (capacity < CMSG_LEN(0)) return 0;
    if (length > capacity) length = capacity;
    struct cmsghdr header = {.cmsg_len = length, .cmsg_level = SOL_SOCKET,
                            .cmsg_type = SCM_CREDENTIALS};
    uint8_t *out = message->msg_control;
    for (unsigned i = 0; i < sizeof(header); ++i) out[i] = ((uint8_t *)&header)[i];
    for (unsigned i = 0; i < length - CMSG_LEN(0); ++i)
        out[CMSG_LEN(0) + i] = ((const uint8_t *)credentials)[i];
    uint64_t used = CMSG_SPACE(sizeof(*credentials));
    return used < capacity ? used : capacity;
}

static int unix_recvmsg(struct task *task, struct task_file *file,
                        const struct msghdr *user_msg, uint32_t flags)
{
    if (!user_range_writable((uintptr_t)user_msg, sizeof(*user_msg))) return -LEONOS_EFAULT;
    if (flags & ~(MSG_DONTWAIT | MSG_PEEK | MSG_TRUNC | MSG_WAITALL | MSG_CMSG_CLOEXEC)) return -LINUX_EOPNOTSUPP;
    struct msghdr message = *user_msg;
    if (task->socket_receive_message == (uintptr_t)user_msg && task->socket_receive_file) {
        message.msg_controllen = task->socket_receive_control_capacity;
        message.msg_namelen = task->socket_receive_name_capacity;
    }
    uint64_t total;
    int ret = unix_message_vectors(&message, 1, &total);
    if (ret) return ret;
    uint64_t already = task->socket_receive_done;
    if (already > total) { task_socket_cancel_receive(task); return -LEONOS_EINVAL; }
    if (message.msg_controllen && !user_range_writable((uintptr_t)message.msg_control, message.msg_controllen))
        return -LEONOS_EFAULT;
    if (message.msg_name && message.msg_namelen &&
        !user_range_writable((uintptr_t)message.msg_name, message.msg_namelen)) return -LEONOS_EFAULT;
    struct unix_socket *socket = unix_from_file(file);
    uint32_t maximum = socket && socket->type != SOCK_STREAM ? 65536 : UNIX_SOCKET_RX_CAP;
    bool passcred = socket && socket->passcred;
    uint64_t remaining = total - already;
    uint32_t length = remaining > maximum ? maximum : (uint32_t)remaining;
    uint8_t *data = length ? kernel_malloc(length) : NULL;
    if (length && !data) return -LEONOS_ENOMEM;
    struct unix_rights *rights = NULL;
    struct unix_receive_result metadata;
    ret = unix_read_data(file, data ? data : (uint8_t *)"", length, flags, &rights, &metadata);
    if (already) metadata.credentials = (struct ucred){task->socket_receive_pid,
                                  task->socket_receive_uid, task->socket_receive_gid};
    if (ret < 0) {
        if (data) kernel_free(data);
        return (int)unix_receive_progress(task, file, total, ret, flags, &metadata);
    }
    uint32_t copied = 0;
    uint32_t available = (uint32_t)ret < length ? (uint32_t)ret : length;
    uint64_t skip = already;
    for (uint64_t i = 0; i < message.msg_iovlen && copied < available; ++i) {
        uint64_t n = message.msg_iov[i].iov_len;
        if (skip >= n) { skip -= n; continue; }
        uint64_t offset = skip;
        n -= skip;
        skip = 0;
        if (n > available - copied) n = available - copied;
        for (uint64_t j = 0; j < n; ++j) ((uint8_t *)message.msg_iov[i].iov_base)[offset + j] = data[copied++];
    }
    if (data) kernel_free(data);
    ret = (int)unix_receive_progress(task, file, total, ret, flags, &metadata);
    if (ret == KERNEL_SYSCALL_BLOCKED) {
        task->socket_receive_message = (uintptr_t)user_msg;
        task->socket_receive_control_capacity = message.msg_controllen;
        task->socket_receive_name_capacity = message.msg_namelen;
        if (task->socket_receive_done) {
            message.msg_controllen = 0;
            message.msg_namelen = 0;
            message.msg_flags = metadata.flags;
            /* Linux's interrupted Unix WAITALL path returns copied bytes
             * before scm_recv_unix; control data is delivered on completion. */
            *(struct msghdr *)(uintptr_t)user_msg = message;
        }
        return ret;
    }
    uint64_t capacity = message.msg_controllen;
    message.msg_controllen = 0;
    message.msg_flags = metadata.flags;
    if (message.msg_name && metadata.path_len) {
        uint32_t name_length = metadata.path_len + 2;
        uint32_t n = message.msg_namelen < name_length ? message.msg_namelen : name_length;
        unsigned char bytes[111] = {AF_UNIX, 0};
        for (unsigned i = 0; i < metadata.path_len; ++i) bytes[i + 2] = metadata.path[i];
        for (unsigned i = 0; i < n; ++i) ((unsigned char *)message.msg_name)[i] = bytes[i];
        message.msg_namelen = name_length;
    } else message.msg_namelen = 0;
    if (passcred) message.msg_controllen = unix_put_credentials(&message, capacity, &metadata.credentials);
    uint8_t *control = (uint8_t *)message.msg_control + message.msg_controllen;
    capacity -= message.msg_controllen;
    if (rights) {
        uint32_t count = 0;
        uint64_t limit = capacity >= CMSG_LEN(0) ? (capacity - CMSG_LEN(0)) / sizeof(int) : 0;
        while (count < rights->count && count < limit) {
            int fd = unix_clone_fd_to_task(task, rights->files[count]);
            if (fd < 0) break;
            if (flags & MSG_CMSG_CLOEXEC) task_descriptor_for_fd(task, fd)->fd_flags = LEONOS_FD_CLOEXEC;
            uint8_t *slot = control + CMSG_LEN(0) + count * sizeof(int);
            for (unsigned i = 0; i < sizeof(int); ++i) slot[i] = ((uint8_t *)&fd)[i];
            ++count;
        }
        if (count) {
            struct cmsghdr header = {.cmsg_len = CMSG_LEN(count * sizeof(int)),
                .cmsg_level = SOL_SOCKET, .cmsg_type = SCM_RIGHTS};
            for (unsigned i = 0; i < sizeof(header); ++i)
                control[i] = ((uint8_t *)&header)[i];
            uint64_t size = CMSG_SPACE(count * sizeof(int));
            message.msg_controllen += size < capacity ? size : capacity;
        }
        if (count < rights->count) message.msg_flags |= MSG_CTRUNC;
        if (!(flags & MSG_PEEK)) unix_rights_free(rights);
    }
    *(struct msghdr *)(uintptr_t)user_msg = message;
    return ret;
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
    uint32_t type = (uint32_t)a1 & 15;
    if (type == SOCK_RAW) type = SOCK_DGRAM;
    if (number == __NR_socket) {
        if (a1 & ~(uint32_t)(15 | SOCK_NONBLOCK | SOCK_CLOEXEC)) return -LEONOS_EINVAL;
        if ((int)a0 != AF_UNIX) return -LINUX_EAFNOSUPPORT;
        if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) return -LINUX_ESOCKTNOSUPPORT;
        if (a2 != 0 && a2 != AF_UNIX) return -LINUX_EPROTONOSUPPORT;
        socket = unix_alloc(task->pid, type);
        if (!socket) return -LEONOS_ENOMEM;
        {
            int fd = unix_alloc_fd(task, socket);
            if (fd < 0) {
                unix_release_handle(socket->handle);
                return fd;
            }
            /* The allocation starts with an owner reference and adds one for the fd. */
            unix_release_handle(socket->handle);
            struct task_file *created = task_file_for_fd(task, fd);
            if (a1 & SOCK_NONBLOCK) created->flags |= LEONOS_O_NONBLOCK;
            if (a1 & SOCK_CLOEXEC) created->fd_flags = 1;
            return fd;
        }
    }
    if (number == __NR_socketpair) {
        struct unix_socket *left;
        struct unix_socket *right;
        int left_fd;
        int right_fd;
        if (!user_range_writable(a3, sizeof(int) * 2u)) return -LEONOS_EFAULT;
        if ((int)a0 != AF_UNIX || (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) ||
            (a2 != 0 && a2 != AF_UNIX) || (a1 & ~(uint32_t)(15 | SOCK_NONBLOCK | SOCK_CLOEXEC))) {
            return -LEONOS_EINVAL;
        }
        left = unix_alloc(task->pid, type);
        if (!left) return -LEONOS_ENOMEM;
        right = unix_alloc(task->pid, type);
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
        left->peer_credentials = right->credentials;
        right->peer_credentials = left->credentials;
        left_fd = unix_alloc_fd(task, left);
        right_fd = unix_alloc_fd(task, right);
        unix_release_handle(left->handle);
        unix_release_handle(right->handle);
        if (left_fd < 0 || right_fd < 0) {
            if (left_fd >= 0) task_discard_file_fd(task, left_fd);
            if (right_fd >= 0) task_discard_file_fd(task, right_fd);
            return -LEONOS_EMFILE;
        }
        ((int *)(uintptr_t)a3)[0] = left_fd;
        ((int *)(uintptr_t)a3)[1] = right_fd;
        for (unsigned i = 0; i < 2; ++i) {
            struct task_file *created = task_file_for_fd(task, i ? right_fd : left_fd);
            if (a1 & SOCK_NONBLOCK) created->flags |= LEONOS_O_NONBLOCK;
            if (a1 & SOCK_CLOEXEC) created->fd_flags = 1;
        }
        return 0;
    }
    file = task_file_for_io(task, (int)a0);
    socket = unix_from_file(file);
    if (!socket) return -LEONOS_EBADF;
    if (number == __NR_bind) {
        char path[109], resolved[LEONOS_FS_PATH_LEN];
        uint32_t length;
        if (socket->path_len) return -LEONOS_EINVAL;
        int ret = unix_socket_path((const void *)(uintptr_t)a1, (uint32_t)a2, path, &length, resolved);
        if (ret < 0) return ret;
        if (!length) { unix_autobind(socket); return 0; }
        if (resolved[0]) {
            struct storage_node node;
            ret = fs_permissions_parent(task, resolved, false);
            if (ret < 0) return ret;
            ret = storage_create_socket(resolved, &node);
            if (ret < 0) return ret == -LEONOS_EEXIST ? -LEONOS_EADDRINUSE : ret;
            ret = fs_permissions_create(task, resolved, &node, 0777);
            if (ret < 0) { (void)storage_unlink(resolved); return ret; }
            unsigned i = 0;
            do { socket->bound_path[i] = resolved[i]; } while (resolved[i++]);
        } else if (unix_find_path(path, length, resolved)) return -LEONOS_EADDRINUSE;
        for (unsigned i = 0; i < length; ++i) socket->path[i] = path[i];
        socket->path_len = length;
        socket->bound = true;
        return 0;
    }
    if (number == __NR_listen) {
        if (socket->type == SOCK_DGRAM) return -LINUX_EOPNOTSUPP;
        if (!socket->path_len || socket->state == UNIX_SOCKET_CONNECTED) return -LEONOS_EINVAL;
        socket->backlog = (uint32_t)a1 >= UNIX_SOCKET_BACKLOG_MAX ? UNIX_SOCKET_BACKLOG_MAX - 1 : (uint32_t)a1;
        socket->state = UNIX_SOCKET_LISTEN;
        socket->credentials = (struct ucred){(int32_t)sched_task_tgid(task), task->euid, task->egid};
        return 0;
    }
    if (number == __NR_connect) {
        if (socket->passcred) unix_autobind(socket);
        if (socket->type == SOCK_DGRAM) {
            if (a2 < 2 || !user_range_ok(a1, 2)) return -LEONOS_EINVAL;
            if (*(const sa_family_t *)(uintptr_t)a1 == AF_UNSPEC) {
                socket->peer_handle = 0;
                socket->peer_path_len = 0;
                socket->state = UNIX_SOCKET_OPEN;
                return 0;
            }
            struct unix_socket *peer;
            int ret = unix_packet_peer(socket, (const void *)(uintptr_t)a1, (uint32_t)a2, &peer);
            if (ret < 0) return ret;
            socket->peer_handle = peer->handle;
            socket->peer_path_len = peer->path_len;
            for (unsigned i = 0; i < peer->path_len; ++i) socket->peer_path[i] = peer->path[i];
            socket->state = UNIX_SOCKET_CONNECTED;
            return 0;
        }
        char path[109], resolved[LEONOS_FS_PATH_LEN];
        uint32_t length;
        struct unix_socket *listener, *server;
        if (socket->state != UNIX_SOCKET_OPEN) return -LEONOS_EISCONN;
        int ret = unix_socket_path((const void *)(uintptr_t)a1, (uint32_t)a2, path, &length, resolved);
        if (ret < 0) return ret;
        if (!length) return -LEONOS_EINVAL;
        if (resolved[0]) {
            struct leonos_permissions permissions;
            ret = fs_permissions_check(task, resolved, FS_ACCESS_WRITE, false);
            if (ret < 0) return ret;
            ret = fs_permissions_get(resolved, NULL, &permissions);
            if (ret < 0) return ret;
            struct storage_node node;
            ret = storage_lookup_path(resolved, &node);
            if (ret < 0) return ret;
            if (node.type != LEONOS_FS_TYPE_SOCKET && (permissions.mode & LINUX_S_IFMT) != LINUX_S_IFSOCK)
                return -LINUX_ECONNREFUSED;
        }
        listener = unix_find_path(path, length, resolved);
        if (listener && listener->type != socket->type) return -LINUX_EPROTOTYPE;
        if (!listener || listener->state != UNIX_SOCKET_LISTEN) return -LINUX_ECONNREFUSED;
        kernel_wait_queue_remove(&listener->wait_connect, task);
        if (listener->pending_count > listener->backlog) {
            return unix_block(socket, file, &listener->wait_connect, false, 0);
        }
        server = unix_alloc(listener->owner_pid, listener->type);
        if (!server) return -LEONOS_ENOMEM;
        server->state = UNIX_SOCKET_CONNECTED;
        server->passcred = listener->passcred;
        server->unaccepted = 1;
        socket->state = UNIX_SOCKET_CONNECTED;
        socket->peer_handle = server->handle;
        socket->peer_pid = listener->owner_pid;
        server->peer_handle = socket->handle;
        server->peer_pid = task->pid;
        server->peer_credentials = (struct ucred){(int32_t)sched_task_tgid(task), task->euid, task->egid};
        socket->peer_credentials = listener->credentials;
        server->credentials = listener->credentials;
        server->path_len = listener->path_len;
        for (unsigned i = 0; i < listener->path_len; ++i) server->path[i] = listener->path[i];
        socket->peer_path_len = server->path_len;
        for (unsigned i = 0; i < server->path_len; ++i) socket->peer_path[i] = server->path[i];
        server->peer_path_len = socket->path_len;
        for (unsigned i = 0; i < socket->path_len; ++i) server->peer_path[i] = socket->path[i];
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
        if (a1 && !user_range_writable(a2, sizeof(socklen_t))) return -LEONOS_EFAULT;
        kernel_wait_queue_remove(&socket->wait_accept, task);
        if (socket->state != UNIX_SOCKET_LISTEN) return -LEONOS_EINVAL;
        if (!socket->pending_count) {
            return unix_block(socket, file, &socket->wait_accept, true, 0);
        }
        uint32_t handle = socket->pending[0];
        struct unix_socket *accepted = unix_from_handle(handle);
        if (a1) {
            int ret = unix_address_out(unix_from_handle(accepted->peer_handle), a1, a2);
            if (ret < 0) return ret;
        }
        int fd = unix_alloc_fd(task, accepted);
        if (fd < 0) return fd;
        for (uint32_t i = 1; i < socket->pending_count; ++i) socket->pending[i - 1] = socket->pending[i];
        --socket->pending_count;
        {
            struct task_file *accepted_file = task_file_for_fd(task, fd);
            if (accepted_file) {
                if (accept4_flags & SOCK_NONBLOCK) accepted_file->flags |= LEONOS_O_NONBLOCK;
                if (accept4_flags & SOCK_CLOEXEC) accepted_file->fd_flags |= 1u;
            }
        }
        accepted->passcred = socket->passcred;
        accepted->unaccepted = 0;
        unix_release_handle(handle); /* drop the listener's pending reference */
        (void)kernel_wait_queue_wake_one(&socket->wait_connect);
        return fd;
    }
    if (number == __NR_getsockname || number == __NR_getpeername) {
        if (number == __NR_getpeername) {
            if (socket->state != UNIX_SOCKET_CONNECTED) return -LINUX_ENOTCONN;
            return unix_name_out(socket->peer_path, socket->peer_path_len, a1, a2);
        }
        return unix_address_out(socket, a1, a2);
    }
    if (number == __NR_setsockopt || number == __NR_getsockopt) return -LINUX_ENOPROTOOPT;
    if (number == __NR_shutdown) {
        if (a1 != SHUT_RD && a1 != SHUT_WR && a1 != SHUT_RDWR) return -LEONOS_EINVAL;
        if (a1 == SHUT_RD || a1 == SHUT_RDWR) socket->shutdown_read = 1;
        if (a1 == SHUT_WR || a1 == SHUT_RDWR) socket->shutdown_write = 1;
        struct unix_socket *peer = unix_from_handle(socket->peer_handle);
        if (peer && socket->type != SOCK_DGRAM) {
            if (a1 == SHUT_RD || a1 == SHUT_RDWR) peer->shutdown_write = 1;
            if (a1 == SHUT_WR || a1 == SHUT_RDWR) peer->shutdown_read = 1;
        }
        (void)kernel_wait_queue_wake_all(&socket->wait_rx);
        (void)kernel_wait_queue_wake_all(&socket->wait_tx);
        unix_wake_peer(socket);
        return 0;
    }
    return -LEONOS_ENOSYS;
}

static int unix_socket_option(struct unix_socket *socket, bool setting, int level,
                               int option, uint64_t address, uint64_t length)
{
    if (setting) {
        if ((int32_t)length < 0) return -LEONOS_EINVAL;
        if (level != SOL_SOCKET) return -LINUX_ENOPROTOOPT;
        if (option == SO_RCVTIMEO || option == SO_RCVTIMEO_NEW ||
            option == SO_SNDTIMEO || option == SO_SNDTIMEO_NEW) {
            if (length < sizeof(struct linux_timeval)) return -LEONOS_EINVAL;
            if (!user_range_ok(address, sizeof(struct linux_timeval))) return -LEONOS_EFAULT;
            struct linux_timeval timeout = *(const struct linux_timeval *)(uintptr_t)address;
            if (timeout.tv_usec < 0 || timeout.tv_usec >= 1000000) return -LINUX_EDOM;
            uint64_t ticks = UINT64_MAX;
            if (timeout.tv_sec < 0) ticks = 0;
            else if ((timeout.tv_sec || timeout.tv_usec) && timeout.tv_sec < INT64_MAX / NTCLKS_TICK_HZ - 1)
                ticks = (uint64_t)timeout.tv_sec * NTCLKS_TICK_HZ +
                    ((uint64_t)timeout.tv_usec * NTCLKS_TICK_HZ + 999999) / 1000000;
            if (option == SO_RCVTIMEO || option == SO_RCVTIMEO_NEW) socket->receive_timeout = ticks;
            else socket->send_timeout = ticks;
            return 0;
        }
        if (option != SO_PASSCRED && option != SO_RCVLOWAT) return -LINUX_ENOPROTOOPT;
        if (length < sizeof(int32_t)) return -LEONOS_EINVAL;
        if (!user_range_ok(address, sizeof(int32_t))) return -LEONOS_EFAULT;
        int32_t value = *(const int32_t *)(uintptr_t)address;
        if (option == SO_PASSCRED) socket->passcred = value != 0;
        else socket->receive_lowwater = value < 0 ? INT32_MAX : value ? (uint32_t)value : 1;
        return 0;
    }
    if (!user_range_writable(length, sizeof(socklen_t))) return -LEONOS_EFAULT;
    socklen_t capacity = *(socklen_t *)(uintptr_t)length;
    if ((int32_t)capacity < 0) return -LEONOS_EINVAL;
    if (level != SOL_SOCKET) return -LINUX_ENOPROTOOPT;
    int32_t value = 0;
    struct linux_timeval timeout = {0};
    const void *data = &value;
    uint32_t size = sizeof(value);
    switch (option) {
    case SO_PEERCRED: data = &socket->peer_credentials; size = sizeof(socket->peer_credentials); break;
    case SO_TYPE: value = (int32_t)socket->type; break;
    case SO_ERROR: value = socket->error; socket->error = 0; break;
    case SO_PASSCRED: value = (int32_t)socket->passcred; break;
    case SO_RCVLOWAT: value = (int32_t)socket->receive_lowwater; break;
    case SO_SNDLOWAT: value = 1; break;
    case SO_ACCEPTCONN: value = socket->state == UNIX_SOCKET_LISTEN; break;
    case SO_DOMAIN: value = AF_UNIX; break;
    case SO_PROTOCOL: value = 0; break;
    case SO_RCVTIMEO:
    case SO_RCVTIMEO_NEW:
    case SO_SNDTIMEO:
    case SO_SNDTIMEO_NEW: {
        uint64_t ticks = option == SO_RCVTIMEO || option == SO_RCVTIMEO_NEW
            ? socket->receive_timeout : socket->send_timeout;
        if (ticks != UINT64_MAX) {
            timeout.tv_sec = (int64_t)(ticks / NTCLKS_TICK_HZ);
            timeout.tv_usec = (int64_t)((ticks % NTCLKS_TICK_HZ) * (1000000 / NTCLKS_TICK_HZ));
        }
        data = &timeout;
        size = sizeof(timeout);
        break;
    }
    default:
        return -LINUX_ENOPROTOOPT;
    }
    if (size > capacity) size = capacity;
    if (size && !user_range_writable(address, size)) return -LEONOS_EFAULT;
    for (unsigned i = 0; i < size; ++i) ((uint8_t *)(uintptr_t)address)[i] = ((const uint8_t *)data)[i];
    *(socklen_t *)(uintptr_t)length = size;
    return 0;
}

int64_t syscall_socket_dispatch(uint64_t number, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    struct task *task;
    struct task_file *file;
    /* These are int/unsigned int arguments in the native Linux syscall
     * prototypes; pointers and size_t byte counts retain all 64 bits. */
    if (number == __NR_socket || number == __NR_socketpair) {
        a0 = (uint32_t)a0;
        a1 = (uint32_t)a1;
        a2 = (uint32_t)a2;
    }
    if (number == __NR_accept4 || number == __NR_sendto || number == __NR_recvfrom)
        a3 = (uint32_t)a3;
    if (number == __NR_getsockopt || number == __NR_setsockopt) {
        task = sched_current_task();
        file = task_file_for_fd(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        struct unix_socket *socket = unix_from_file(file);
        if (socket) return unix_socket_option(socket, number == __NR_setsockopt,
                                             (int)a1, (int)a2, a3, a4);
        if (file->flags & TASK_FILE_FLAG_SOCKET_INET)
            return inet_socket_dispatch(number, a0, a1, a2, a3, a4);
        return -LINUX_ENOTSOCK;
    }
    if (number == __NR_sendmsg) {
        task = sched_current_task();
        {
            struct task_file *file = task_file_for_io(task, (int)a0);
            if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
            return unix_sendmsg(task, file, (const struct msghdr *)(uintptr_t)a1, (uint32_t)a2);
        }
    }
    if (number == __NR_recvmsg) {
        task = sched_current_task();
        {
            struct task_file *file = task->socket_receive_file ? task->socket_receive_file : task_file_for_io(task, (int)a0);
            if (!file || !(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
            return unix_recvmsg(task, file, (const struct msghdr *)(uintptr_t)a1,
                                (uint32_t)a2);
        }
    }
    if (number == __NR_sendto) {
        struct task *task = sched_current_task();
        struct task_file *file = task_file_for_io(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (a2 && !user_range_ok(a1, a2)) return -LEONOS_EFAULT;
        if (file->flags & TASK_FILE_FLAG_SOCKET_INET) return task_inet_write(file, (const void *)(uintptr_t)a1, (uint32_t)a2);
        if (!(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        if (a3 & ~(MSG_DONTWAIT | MSG_NOSIGNAL | MSG_MORE | MSG_EOR)) return -LINUX_EOPNOTSUPP;
        struct unix_socket *socket = unix_from_file(file);
        if (socket->type != SOCK_STREAM) {
            struct unix_socket *peer;
            int result = unix_packet_peer(socket, (const void *)(uintptr_t)a4, (uint32_t)a5, &peer);
            if (result < 0) return result;
            if (a2 > 65536) return -LINUX_EMSGSIZE;
            int ret = unix_send_packet(file, peer, (const void *)(uintptr_t)a1, (uint32_t)a2, (uint32_t)a3, NULL);
            if (ret == -LEONOS_EPIPE && !(a3 & MSG_NOSIGNAL)) sched_signal_user_task(task->pid, 13);
            return ret;
        }
        if (a4) return -LEONOS_EISCONN;
        struct task_file local = *file;
        if (a3 & MSG_DONTWAIT) local.flags |= LEONOS_O_NONBLOCK;
        int ret = task_socket_write(&local, (const void *)(uintptr_t)a1, (uint32_t)a2);
        if (ret == -LEONOS_EPIPE && !(a3 & MSG_NOSIGNAL)) sched_signal_user_task(task->pid, 13);
        return ret;
    }
    if (number == __NR_recvfrom) {
        struct task *task = sched_current_task();
        struct task_file *file = task->socket_receive_file ? task->socket_receive_file : task_file_for_io(task, (int)a0);
        if (!file) return -LEONOS_EBADF;
        if (a2 && !user_range_writable(a1, a2)) return -LEONOS_EFAULT;
        if (file->flags & TASK_FILE_FLAG_SOCKET_INET) return task_inet_read(file, (void *)(uintptr_t)a1, (uint32_t)a2);
        if (!(file->flags & TASK_FILE_FLAG_SOCKET_UNIX)) return -LEONOS_EBADF;
        if (a3 & ~(MSG_DONTWAIT | MSG_PEEK | MSG_TRUNC | MSG_WAITALL | MSG_CMSG_CLOEXEC)) return -LINUX_EOPNOTSUPP;
        if (a4) {
            if (!user_range_writable(a5, 4)) return -LEONOS_EFAULT;
            uint32_t capacity = *(uint32_t *)(uintptr_t)a5;
            if ((int32_t)capacity < 0) return -LEONOS_EINVAL;
            if (!user_range_writable(a4, capacity < 111 ? capacity : 111)) return -LEONOS_EFAULT;
        }
        struct unix_receive_result metadata;
        uint64_t already = task->socket_receive_done;
        if (already > a2) { task_socket_cancel_receive(task); return -LEONOS_EINVAL; }
        uint64_t remaining = a2 - already;
        int ret = unix_read_data(file, (void *)(uintptr_t)(a1 + already),
                                remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining,
                                (uint32_t)a3, NULL, &metadata);
        ret = (int)unix_receive_progress(task, file, a2, ret, (uint32_t)a3, &metadata);
        if (ret >= 0 && a4) {
            if (metadata.path_len) (void)unix_name_out(metadata.path, metadata.path_len, a4, a5);
            else *(socklen_t *)(uintptr_t)a5 = 0;
        }
        return ret;
    }
    task = sched_current_task();
    if (number == __NR_socket && (int)a0 == AF_INET) return inet_socket_dispatch(number, a0, a1, a2, a3, a4);
    file = task_file_for_fd(task, (int)a0);
    if (number != __NR_socket && file && (file->flags & TASK_FILE_FLAG_SOCKET_INET)) {
        return inet_socket_dispatch(number, a0, a1, a2, a3, a4);
    }
    return unix_socket_dispatch(number, a0, a1, a2, a3);
}
