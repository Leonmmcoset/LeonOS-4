#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <leonos/unix_ipc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int sockets[2];
    uint32_t type, value, length;
    const struct leonos_ipc_frame header = {
        .magic = LEONOS_IPC_MAGIC, .version = LEONOS_IPC_VERSION,
        .length = 2 * sizeof(uint32_t),
    };
    uint8_t frame[sizeof(header) + 2 * sizeof(uint32_t)];
    const uint32_t payload[] = {100, 1234};
    memcpy(frame, &header, sizeof(header));
    memcpy(frame + sizeof(header), payload, sizeof(payload));

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(fcntl(sockets[0], F_SETFL, O_NONBLOCK) == 0);
    assert(leonos_ipc_recv(sockets[0], &type, &value, sizeof(value), &length) == -1);
    assert(errno == EAGAIN);
    assert(write(sockets[1], frame, sizeof(frame)) == (ssize_t)sizeof(frame));
    close(sockets[1]);
    assert(leonos_ipc_recv(sockets[0], &type, &value, sizeof(value), &length) == 0);
    assert(type == 100 && value == 1234 && length == sizeof(value));
    errno = EAGAIN;
    assert(leonos_ipc_recv(sockets[0], &type, &value, sizeof(value), &length) == -1);
    assert(errno == ECONNRESET);
    close(sockets[0]);

    /* EOF must remain fatal at every frame boundary, even with stale errno. */
    for (size_t prefix = 0; prefix < sizeof(frame); ++prefix) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        assert(write(sockets[1], frame, prefix) == (ssize_t)prefix);
        close(sockets[1]);
        errno = EAGAIN;
        assert(leonos_ipc_recv(sockets[0], &type, &value, sizeof(value), &length) == -1);
        assert(errno == ECONNRESET);
        close(sockets[0]);
    }
    errno = EAGAIN;
    assert(leonos_ipc_recv(-1, &type, &value, sizeof(value), &length) == -1);
    assert(errno == EBADF);
    puts("IPC disconnect: queued data drains, EOF and errors remain fatal");
    return 0;
}
