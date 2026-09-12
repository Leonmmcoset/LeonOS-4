#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MMSG_CHECK(expr) do { if (!(expr)) { \
    printf("mmsg ABI: line %d: %s errno=%d\n", __LINE__, #expr, errno); return 1; \
} } while (0)

static int socket_batch_syscalls(void)
{
    int pair[2], pipefd[2], error;
    socklen_t error_size = sizeof(error);
    char outgoing[3][4] = {"one", "two", "end"}, incoming[3][4] = {{0}};
    struct iovec sendvec[3], recvvec[3];
    struct mmsghdr sent[3] = {0}, received[3] = {0};
    MMSG_CHECK(syscall(SYS_socketpair, AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, pair) == 0);
    for (unsigned i = 0; i < 3; ++i) {
        sendvec[i] = (struct iovec){outgoing[i], 3};
        recvvec[i] = (struct iovec){incoming[i], sizeof(incoming[i])};
        sent[i].msg_hdr.msg_iov = &sendvec[i]; sent[i].msg_hdr.msg_iovlen = 1;
        received[i].msg_hdr.msg_iov = &recvvec[i]; received[i].msg_hdr.msg_iovlen = 1;
    }
    MMSG_CHECK(syscall(SYS_sendmmsg, -1, NULL, 0, 0) == -1 && errno == EBADF);
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], NULL, 0, 0) == 0);
    MMSG_CHECK(syscall(SYS_recvmmsg, -1, NULL, 0, 0, (void *)1) == -1 && errno == EFAULT);
    MMSG_CHECK(syscall(SYS_recvmmsg, -1, NULL, 0, 0x80000000u, (void *)1) == -1 && errno == EINVAL);
    struct timespec bad = {.tv_nsec = -1};
    MMSG_CHECK(syscall(SYS_recvmmsg, -1, NULL, 0, 0, &bad) == -1 && errno == EINVAL);
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], sent, 3, MSG_NOSIGNAL) == 3);
    for (unsigned i = 0; i < 3; ++i) MMSG_CHECK(sent[i].msg_len == 3);
    struct timespec zero = {0};
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 3, 0, &zero) == 1);
    MMSG_CHECK(received[0].msg_len == 3 && !memcmp(incoming[0], "one", 3));
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received + 1, 2, MSG_WAITFORONE, NULL) == 2);
    MMSG_CHECK(!memcmp(incoming[1], "two", 3) && !memcmp(incoming[2], "end", 3));
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 3, MSG_DONTWAIT, NULL) == -1 && errno == EAGAIN);
    sendvec[1].iov_base = (void *)1;
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], sent, 3, MSG_NOSIGNAL) == 1);
    sendvec[1].iov_base = outgoing[1];
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 3, MSG_DONTWAIT, NULL) == 1);
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], sent, 2, MSG_NOSIGNAL) == 2);
    recvvec[1].iov_base = (void *)1;
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 2, MSG_DONTWAIT, NULL) == 1);
    MMSG_CHECK(syscall(SYS_getsockopt, pair[1], SOL_SOCKET, SO_ERROR, &error, &error_size) == 0 && error == EFAULT);
    recvvec[1].iov_base = incoming[1];
    /* Message length output faults follow the actual send/receive operation. */
    size_t page = sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    MMSG_CHECK(guard != MAP_FAILED && mprotect(guard + page, page, PROT_NONE) == 0);
    struct mmsghdr *edge = (void *)(guard + page - sizeof(struct msghdr));
    memcpy(edge, &sent[0].msg_hdr, sizeof(struct msghdr));
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], edge, 1, MSG_NOSIGNAL) == -1 && errno == EFAULT);
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 3, MSG_DONTWAIT, NULL) == 1);
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], sent, 1, MSG_NOSIGNAL) == 1);
    memcpy(edge, &received[0].msg_hdr, sizeof(struct msghdr));
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], edge, 1, MSG_DONTWAIT, NULL) == -1 && errno == EFAULT);
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 1, MSG_DONTWAIT, NULL) == -1 && errno == EAGAIN);
    MMSG_CHECK(munmap(guard, 2 * page) == 0);
    /* Per-message SCM_RIGHTS retains the OFD and applies CLOEXEC on receipt. */
    MMSG_CHECK(syscall(SYS_pipe2, pipefd, O_CLOEXEC) == 0);
    union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0}, result = {0};
    sent[0].msg_hdr.msg_control = control.bytes;
    sent[0].msg_hdr.msg_controllen = sizeof(control.bytes);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&sent[0].msg_hdr);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS; cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &pipefd[0], sizeof(int));
    MMSG_CHECK(syscall(SYS_sendmmsg, pair[0], sent, 1, MSG_NOSIGNAL) == 1);
    MMSG_CHECK(close(pipefd[0]) == 0);
    received[0].msg_hdr.msg_control = result.bytes;
    received[0].msg_hdr.msg_controllen = sizeof(result.bytes);
    MMSG_CHECK(syscall(SYS_recvmmsg, pair[1], received, 1, MSG_CMSG_CLOEXEC, NULL) == 1);
    cmsg = CMSG_FIRSTHDR(&received[0].msg_hdr);
    MMSG_CHECK(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS);
    MMSG_CHECK(received[0].msg_hdr.msg_flags & MSG_CMSG_CLOEXEC);
    int retained;
    memcpy(&retained, CMSG_DATA(cmsg), sizeof(retained));
    MMSG_CHECK(syscall(SYS_fcntl, retained, F_GETFD) == FD_CLOEXEC);
    MMSG_CHECK(write(pipefd[1], "x", 1) == 1 && read(retained, incoming[0], 1) == 1 && incoming[0][0] == 'x');
    MMSG_CHECK(close(retained) == 0 && close(pipefd[1]) == 0);
    MMSG_CHECK(close(pair[0]) == 0 && close(pair[1]) == 0);
    puts("PASS raw Linux mmsg ABI: message counts, partial errors, SO_ERROR, output faults, timeout, SCM_RIGHTS");
    return 0;
}

#ifndef SOCKET_BATCH_EMBEDDED
int main(void) { return socket_batch_syscalls(); }
#endif
