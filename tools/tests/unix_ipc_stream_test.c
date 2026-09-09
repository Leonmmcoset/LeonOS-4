#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <leonos/unix_ipc.h>

int main(void)
{
    int pair[2], first[2], second[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0);
    assert(pipe(first) == 0 && pipe(second) == 0);
    struct {
        struct leonos_ipc_frame frame;
        uint32_t type;
        char data[4];
    } encoded = {{LEONOS_IPC_MAGIC, LEONOS_IPC_VERSION, 8}, 17, "one"};
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {&encoded, 5};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &first[0], sizeof(int));
    assert(sendmsg(pair[0], &message, 0) == 5);
    assert(close(first[0]) == 0);
    char output[8];
    uint32_t type, size;
    int received = -1;
    assert(leonos_ipc_recv_fd(pair[1], &type, output, sizeof(output), &size, &received) == -1 && errno == EAGAIN);
    assert(received == -1);
    assert(write(pair[0], (char *)&encoded + 5, sizeof(encoded) - 5) == sizeof(encoded) - 5);
    assert(leonos_ipc_send_fd(pair[0], 18, "two", 4, second[0]) == 0);
    assert(close(second[0]) == 0);
    assert(leonos_ipc_recv_fd(pair[1], &type, output, sizeof(output), &size, &received) == 0);
    assert(type == 17 && size == 4 && !strcmp(output, "one") && received >= 0);
    assert(fcntl(received, F_GETFD) & FD_CLOEXEC);
    assert(write(first[1], "a", 1) == 1 && read(received, output, 1) == 1 && output[0] == 'a');
    close(received);
    assert(leonos_ipc_recv_fd(pair[1], &type, output, sizeof(output), &size, &received) == 0);
    assert(type == 18 && size == 4 && !strcmp(output, "two") && received >= 0);
    assert(write(second[1], "b", 1) == 1 && read(received, output, 1) == 1 && output[0] == 'b');
    close(received);
    assert(leonos_ipc_close(pair[0]) == 0 && leonos_ipc_close(pair[1]) == 0);
    close(first[1]); close(second[1]);
    char path[108];
    snprintf(path, sizeof(path), "/tmp/leonos-ipc-host-%ld.sock", (long)getpid());
    int listener = leonos_ipc_bind_listen(path, 2);
    assert(listener >= 0);
    struct stat node;
    assert(stat(path, &node) == 0 && (node.st_mode & 0777) == 0600);
    assert(leonos_ipc_bind_listen(path, 2) == -1 && errno == EADDRINUSE);
    close(listener);
    mode_t previous_mask = umask(0077);
    listener = leonos_ipc_bind_listen_mode(path, 2, 0666);
    assert(listener >= 0);
    assert(stat(path, &node) == 0 && (node.st_mode & 0777) == 0666);
    assert(umask(previous_mask) == 0077);
    close(listener);
    unlink(path);
    puts("PASS IPC on Linux: partial headers, exact frame/SCM_RIGHTS association, CLOEXEC, restart, active-listener protection");
}
