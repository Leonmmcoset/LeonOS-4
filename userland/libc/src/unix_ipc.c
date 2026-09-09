/* Unix domain socket framing client used by every migrated LeonOS service. */
#include <leonos/unix_ipc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

int leonos_ipc_connect(const char *path)
{
    struct sockaddr_un address;
    int fd;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    if (strlen(path) >= sizeof(address.sun_path)) { errno = ENAMETOOLONG; return -1; }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1u);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&address,
                (socklen_t)(sizeof(sa_family_t) + strlen(address.sun_path) + 1u)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int leonos_ipc_bind_listen(const char *path, int backlog)
{
    return leonos_ipc_bind_listen_mode(path, backlog, 0600);
}

int leonos_ipc_bind_listen_mode(const char *path, int backlog, uint32_t mode)
{
    struct sockaddr_un address;
    int fd;
    if (!path || !path[0] || (mode & ~0777u)) {
        errno = EINVAL;
        return -1;
    }
    if (strlen(path) >= sizeof(address.sun_path)) { errno = ENAMETOOLONG; return -1; }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1u);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    socklen_t address_length = (socklen_t)(sizeof(sa_family_t) + strlen(address.sun_path) + 1u);
    int bound = bind(fd, (struct sockaddr *)&address, address_length);
    if (bound < 0 && errno == EADDRINUSE) {
        struct stat node;
        if (stat(path, &node) == 0 && S_ISSOCK(node.st_mode) &&
            connect(fd, (struct sockaddr *)&address, address_length) < 0 && errno == ECONNREFUSED &&
            unlink(path) == 0) bound = bind(fd, (struct sockaddr *)&address, address_length);
        else errno = EADDRINUSE;
    }
    if (bound < 0 || chmod(path, (mode_t)mode) < 0 || listen(fd, backlog > 0 ? backlog : 8) < 0) {
        int saved = errno;
        close(fd);
        if (bound == 0) unlink(path);
        errno = saved;
        return -1;
    }
    return fd;
}

int leonos_ipc_accept(int listen_fd, struct ucred *peer)
{
    int fd = accept(listen_fd, 0, 0);
    if (fd >= 0 && peer) {
        (void)leonos_ipc_peer_credentials(fd, peer);
    }
    return fd;
}

int leonos_ipc_set_nonblock(int fd, int enabled)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

int leonos_ipc_peer_credentials(int fd, struct ucred *credentials)
{
    socklen_t length = sizeof(*credentials);
    if (!credentials) {
        errno = EINVAL;
        return -1;
    }
    memset(credentials, 0, sizeof(*credentials));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, credentials, &length) < 0) {
        return -1;
    }
    return 0;
}

int leonos_ipc_send_fd(int fd, uint32_t type, const void *payload,
                       uint32_t length, int send_fd)
{
    uint8_t frame_buffer[LEONOS_IPC_ATOMIC_FRAME_CAP];
    uint32_t offset = 0;
    if (length > LEONOS_IPC_ATOMIC_FRAME_CAP - sizeof(struct leonos_ipc_frame) -
                     sizeof(uint32_t)) {
        errno = EMSGSIZE;
        return -1;
    }
    {
        struct leonos_ipc_frame frame = {
            .magic = LEONOS_IPC_MAGIC,
            .version = LEONOS_IPC_VERSION,
            .length = sizeof(uint32_t) + length,
        };
        memcpy(frame_buffer + offset, &frame, sizeof(frame));
        offset += (uint32_t)sizeof(frame);
        memcpy(frame_buffer + offset, &type, sizeof(type));
        offset += (uint32_t)sizeof(type);
        if (length) {
            memcpy(frame_buffer + offset, payload, length);
            offset += length;
        }
    }
    uint32_t done = 0;
    while (done < offset) {
        char control[CMSG_SPACE(sizeof(int))];
        struct iovec vector = {.iov_base = frame_buffer + done, .iov_len = offset - done};
        struct msghdr message;
        struct cmsghdr *header;
        memset(control, 0, sizeof(control));
        memset(&message, 0, sizeof(message));
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        if (send_fd >= 0 && !done) {
            message.msg_control = control;
            message.msg_controllen = sizeof(control);
        }
        header = (struct cmsghdr *)control;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        *(int *)CMSG_DATA(header) = send_fd;
        ssize_t sent = sendmsg(fd, &message, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) {
                if (!done) return -1;
                struct pollfd ready = {.fd = fd, .events = POLLOUT};
                int result = poll(&ready, 1, 3000);
                if (result > 0) continue;
                if (!result) errno = ETIMEDOUT;
            }
            return -1;
        }
        if (!sent) { errno = EPIPE; return -1; }
        done += (uint32_t)sent;
    }
    return 0;
}

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    return leonos_ipc_send_fd(fd, type, payload, length, -1);
}

struct ipc_receive_state {
    struct ipc_receive_state *next;
    int fd;
    int busy;
    int ancillary;
    uint32_t header_bytes;
    uint32_t body_bytes;
    struct leonos_ipc_frame frame;
    uint8_t *body;
};

static struct ipc_receive_state *receive_states;
static unsigned receive_lock;

static void receive_states_lock(void)
{
    while (__atomic_exchange_n(&receive_lock, 1, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
}

static void receive_states_unlock(void)
{
    __atomic_store_n(&receive_lock, 0, __ATOMIC_RELEASE);
}

static struct ipc_receive_state *receive_acquire(int fd)
{
    receive_states_lock();
    struct ipc_receive_state *state = receive_states;
    while (state && state->fd != fd) state = state->next;
    if (state && state->busy) {
        receive_states_unlock();
        errno = EAGAIN;
        return NULL;
    }
    if (!state) {
        state = calloc(1, sizeof(*state));
        if (state) {
            state->fd = fd;
            state->ancillary = -1;
            state->next = receive_states;
            receive_states = state;
        }
    }
    if (state) state->busy = 1;
    receive_states_unlock();
    return state;
}

static void receive_release(struct ipc_receive_state *state, int retain)
{
    receive_states_lock();
    if (retain) state->busy = 0;
    else {
        struct ipc_receive_state **p = &receive_states;
        while (*p && *p != state) p = &(*p)->next;
        if (*p) *p = state->next;
    }
    receive_states_unlock();
    if (!retain) {
        if (state->ancillary >= 0) close(state->ancillary);
        free(state->body);
        free(state);
    }
}

static int receive_part(struct ipc_receive_state *state, void *destination,
                         uint32_t *done, uint32_t length)
{
    while (*done < length) {
        char control[CMSG_SPACE(4 * sizeof(int))];
        struct iovec vector = {.iov_base = (uint8_t *)destination + *done, .iov_len = length - *done};
        struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control, .msg_controllen = sizeof(control)};
        ssize_t got = recvmsg(state->fd, &message, MSG_CMSG_CLOEXEC);
        if (got < 0) { if (errno == EINTR) continue; return -1; }
        if (!got) { errno = ECONNRESET; return -1; }
        *done += (uint32_t)got;
        for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header;
             header = CMSG_NXTHDR(&message, header)) {
            if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
                header->cmsg_len < CMSG_LEN(0)) continue;
            unsigned count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (unsigned i = 0; i < count; ++i) {
                int fd = ((int *)CMSG_DATA(header))[i];
                if (state->ancillary < 0) state->ancillary = fd;
                else close(fd);
            }
        }
        if (message.msg_flags & MSG_CTRUNC) { errno = EPROTO; return -1; }
    }
    return 0;
}

int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd)
{
    if (received_fd) *received_fd = -1;
    if (length) *length = 0;
    struct ipc_receive_state *state = receive_acquire(fd);
    if (!state) return -1;
    if (receive_part(state, &state->frame, &state->header_bytes, sizeof(state->frame)) < 0) goto error;
    if (state->frame.magic != LEONOS_IPC_MAGIC || state->frame.version != LEONOS_IPC_VERSION ||
        state->frame.length < sizeof(uint32_t) || state->frame.length > 1024u * 1024u) {
        errno = EPROTO;
        goto error;
    }
    if (!state->body) {
        state->body = malloc(state->frame.length);
        if (!state->body) goto error;
    }
    if (receive_part(state, state->body, &state->body_bytes, state->frame.length) < 0) goto error;
    uint32_t want = state->frame.length - sizeof(uint32_t);
    if (want > capacity) { errno = EMSGSIZE; goto error; }
    if (want && !payload) { errno = EINVAL; goto error; }
    if (type) memcpy(type, state->body, sizeof(*type));
    if (want) memcpy(payload, state->body + sizeof(uint32_t), want);
    if (length) *length = want;
    if (received_fd) {
        *received_fd = state->ancillary;
        state->ancillary = -1;
    }
    receive_release(state, 0);
    return 0;
error:;
    int saved = errno;
    receive_release(state, saved == EAGAIN && state->header_bytes != 0);
    errno = saved;
    return -1;
}

int leonos_ipc_recv(int fd, uint32_t *type, void *payload, uint32_t capacity,
                    uint32_t *length)
{
    return leonos_ipc_recv_fd(fd, type, payload, capacity, length, 0);
}

int leonos_ipc_close(int fd)
{
    receive_states_lock();
    struct ipc_receive_state *state = receive_states;
    while (state && state->fd != fd) state = state->next;
    if (state && !state->busy) state->busy = 1;
    else state = NULL;
    receive_states_unlock();
    if (state) receive_release(state, 0);
    return fd >= 0 ? close(fd) : -1;
}
