/* Unix domain socket framing client used by every migrated LeonOS service. */
#include <leonos/unix_ipc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int leonos_ipc_connect(const char *path)
{
    struct sockaddr_un address;
    int fd;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
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
    struct sockaddr_un address;
    int fd;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1u);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (bind(fd, (struct sockaddr *)&address,
             (socklen_t)(sizeof(sa_family_t) + strlen(address.sun_path) + 1u)) < 0 ||
        listen(fd, backlog > 0 ? backlog : 8) < 0) {
        int saved = errno;
        close(fd);
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

static int read_exact(int fd, void *buffer, uint32_t length)
{
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0;
    while (done < length) {
        ssize_t got = recv(fd, dst + done, length - done, 0);
        if (got < 0) {
            if (errno == EAGAIN) return 0;
            return -1;
        }
        if (got == 0) return -1;
        done += (uint32_t)got;
    }
    return 1;
}

static int write_exact(int fd, const void *buffer, uint32_t length)
{
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t done = 0;
    while (done < length) {
        ssize_t put = send(fd, src + done, length - done, 0);
        if (put < 0) {
            if (errno == EAGAIN) return 0;
            return -1;
        }
        if (put == 0) return -1;
        done += (uint32_t)put;
    }
    return 1;
}

int leonos_ipc_send_fd(int fd, uint32_t type, const void *payload,
                       uint32_t length, int send_fd)
{
    struct leonos_ipc_frame frame = {
        .magic = LEONOS_IPC_MAGIC,
        .version = LEONOS_IPC_VERSION,
        .length = sizeof(type) + length,
    };
    if (write_exact(fd, &frame, sizeof(frame)) <= 0) return -1;
    if (write_exact(fd, &type, sizeof(type)) <= 0) return -1;
    if (length && write_exact(fd, payload, length) <= 0) return -1;
    if (send_fd >= 0) {
        char control[CMSG_SPACE(sizeof(int))];
        char byte = 0;
        struct iovec vector = {.iov_base = &byte, .iov_len = 1};
        struct msghdr message;
        struct cmsghdr *header;
        memset(control, 0, sizeof(control));
        memset(&message, 0, sizeof(message));
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        header = (struct cmsghdr *)control;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        *(int *)CMSG_DATA(header) = send_fd;
        if (sendmsg(fd, &message, 0) != 1) return -1;
    }
    return 0;
}

int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length)
{
    return leonos_ipc_send_fd(fd, type, payload, length, -1);
}

int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd)
{
    struct leonos_ipc_frame frame;
    uint32_t message_type = 0;
    uint32_t want;
    uint32_t done = 0;
    if (received_fd) *received_fd = -1;
    if (length) *length = 0;
    if (read_exact(fd, &frame, sizeof(frame)) <= 0) {
        errno = EAGAIN;
        return -1;
    }
    if (frame.magic != LEONOS_IPC_MAGIC || frame.version != LEONOS_IPC_VERSION ||
        frame.length < sizeof(message_type) || frame.length > 1024u * 1024u) {
        errno = EPROTO;
        return -1;
    }
    if (read_exact(fd, &message_type, sizeof(message_type)) <= 0) return -1;
    want = frame.length - sizeof(message_type);
    if (capacity < want) {
        uint8_t sink[256];
        while (done < want) {
            uint32_t chunk = want - done;
            if (chunk > sizeof(sink)) chunk = sizeof(sink);
            if (read_exact(fd, sink, chunk) <= 0) return -1;
            done += chunk;
        }
        errno = EMSGSIZE;
        return -1;
    }
    while (done < want) {
        uint8_t *dst = (uint8_t *)payload + done;
        uint32_t chunk = want - done;
        if (read_exact(fd, dst, chunk) <= 0) return -1;
        done += chunk;
    }
    if (type) *type = message_type;
    if (length) *length = want;
    if (received_fd) {
        char control[CMSG_SPACE(sizeof(int))];
        struct iovec vector;
        struct msghdr message;
        struct cmsghdr *header;
        char byte;
        ssize_t got;
        memset(control, 0, sizeof(control));
        memset(&message, 0, sizeof(message));
        vector.iov_base = &byte;
        vector.iov_len = 1;
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        got = recvmsg(fd, &message, MSG_PEEK);
        if (got > 0 && message.msg_controllen >= sizeof(struct cmsghdr)) {
            header = (struct cmsghdr *)control;
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
                got = recvmsg(fd, &message, 0);
                (void)got;
                if (header->cmsg_len >= CMSG_LEN(sizeof(int))) {
                    *received_fd = *(int *)CMSG_DATA(header);
                }
            }
        }
    }
    return 0;
}

int leonos_ipc_recv(int fd, uint32_t *type, void *payload, uint32_t capacity,
                    uint32_t *length)
{
    return leonos_ipc_recv_fd(fd, type, payload, capacity, length, 0);
}

int leonos_ipc_close(int fd)
{
    return fd >= 0 ? close(fd) : -1;
}
