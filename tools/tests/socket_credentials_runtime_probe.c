#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <linux/capability.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[socket-credentials] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int receive_credentials(int fd, struct ucred *credentials)
{
    char byte, control[CMSG_SPACE(sizeof(*credentials))];
    struct iovec vector = {&byte, 1};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(fd, &message, 0) == 1 && !(message.msg_flags & MSG_CTRUNC));
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    CHECK(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_CREDENTIALS);
    CHECK(header->cmsg_len == CMSG_LEN(sizeof(*credentials)));
    memcpy(credentials, CMSG_DATA(header), sizeof(*credentials));
    return 0;
}

static int explicit_case(unsigned cap, int field, int expected_error)
{
    int pair[2], enabled = 1;
    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2] = {{0}};
    if (cap < 64) data[cap / 32].effective = data[cap / 32].permitted = 1u << (cap % 32);
    CHECK(syscall(SYS_capset, &header, data) == 0);
    struct ucred sent = {getpid(), getuid(), getgid()}, received;
    if (field == 0) sent.pid = getppid();
    if (field == 1) sent.uid = 1337;
    if (field == 2) sent.gid = 1337;
    if (field == 3) sent.uid = (uid_t)-1;
    char byte = 'x', control[CMSG_SPACE(sizeof(sent))] = {0};
    struct iovec vector = {&byte, 1};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_CREDENTIALS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(sent));
    memcpy(CMSG_DATA(cmsg), &sent, sizeof(sent));
    ssize_t result = sendmsg(pair[0], &message, 0);
    if (expected_error) CHECK(result == -1 && errno == expected_error);
    else {
        CHECK(result == 1 && receive_credentials(pair[1], &received) == 0);
        CHECK(!memcmp(&sent, &received, sizeof(sent)));
    }
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[socket-credentials] BEGIN inherited connection, sender identity and separate capabilities");
    int pair[2], enabled = 1, status;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    CHECK(setsockopt(pair[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    struct ucred peer, sender;
    socklen_t size = sizeof(peer);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.pid == getpid());
    pid_t child = fork();
    if (!child) {
        if (setresgid(1000, 1000, 1000) || setresuid(1000, 1000, 1000)) _exit(1);
        _exit(write(pair[0], "x", 1) != 1);
    }
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(receive_credentials(pair[1], &sender) == 0);
    CHECK(sender.pid == child && sender.uid == 1000 && sender.gid == 1000);
    size = sizeof(peer);
    CHECK(getsockopt(pair[1], SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0);
    CHECK(peer.pid == getpid() && peer.uid == 0 && peer.gid == 0);
    CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    const struct { unsigned capability; int field, error; } cases[] = {
        {64, 0, EPERM}, {64, 1, EPERM}, {64, 2, EPERM},
        {CAP_SYS_ADMIN, 0, 0}, {CAP_SETUID, 1, 0}, {CAP_SETGID, 2, 0},
        {CAP_SETUID, 2, EPERM}, {CAP_SETGID, 1, EPERM}, {CAP_SETUID, 3, EINVAL}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        child = fork();
        if (!child) _exit(explicit_case(cases[i].capability, cases[i].field, cases[i].error));
        CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    puts("[socket-credentials] DONE failures=0");
    return 0;
}
