#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <leonos/unix_ipc.h>

/* Model LeonOS's byte ring and separate pending SCM_RIGHTS queue. */
static uint8_t stream[256];
static size_t head, tail;
static int pending_fd;

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    (void)fd;
    size_t count = head - tail;
    if (count > length) count = length;
    if (!count && length) {
        errno = EAGAIN;
        return -1;
    }
    memcpy(buffer, stream + tail, count);
    if (!(flags & MSG_PEEK)) tail += count;
    return (ssize_t)count;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    ssize_t count = 0;
    if (message->msg_iovlen) {
        count = recv(fd, message->msg_iov[0].iov_base,
                     message->msg_iov[0].iov_len, flags);
    }
    if (pending_fd >= 0 && message->msg_control) {
        assert(message->msg_controllen >= CMSG_LEN(sizeof(int)));
        struct cmsghdr *control = message->msg_control;
        control->cmsg_len = CMSG_LEN(sizeof(int));
        control->cmsg_level = SOL_SOCKET;
        control->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(control), &pending_fd, sizeof(pending_fd));
        if (!(flags & MSG_PEEK)) pending_fd = -1;
        /* Match the current kernel, including its post-consume length. */
        message->msg_controllen = pending_fd >= 0 ? CMSG_LEN(sizeof(int)) : 0;
        return count < 0 ? 0 : count;
    }
    message->msg_controllen = 0;
    return count;
}

static void queue_frame(uint32_t type, uint32_t value)
{
    struct leonos_ipc_frame frame = {
        .magic = LEONOS_IPC_MAGIC, .version = LEONOS_IPC_VERSION,
        .length = 2 * sizeof(uint32_t),
    };
    memcpy(stream + head, &frame, sizeof(frame));
    head += sizeof(frame);
    memcpy(stream + head, &type, sizeof(type));
    head += sizeof(type);
    memcpy(stream + head, &value, sizeof(value));
    head += sizeof(value);
}

int main(void)
{
    uint32_t type, value, length;
    int received_fd;
    for (int with_following_frame = 0; with_following_frame <= 1; ++with_following_frame) {
        head = tail = 0;
        pending_fd = 42;
        queue_frame(100, 1234);
        if (with_following_frame) queue_frame(200, 5678);
        assert(leonos_ipc_recv_fd(4, &type, &value, sizeof(value), &length,
                                  &received_fd) == 0);
        assert(type == 100 && value == 1234 && length == 4 && received_fd == 42);
        if (with_following_frame) {
            int ret = leonos_ipc_recv(4, &type, &value, sizeof(value), &length);
            if (ret != 0) {
                fprintf(stderr, "following input frame corrupted: errno=%d tail=%zu\n",
                        errno, tail);
                return 1;
            }
            assert(type == 200 && value == 5678 && length == 4);
        }
        assert(tail == head);
    }
    puts("IPC frame tests passed: descriptor-only receive preserves following input");
    return 0;
}
