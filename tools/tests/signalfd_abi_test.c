#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#define SFD_CHECK(expr) do { if (!(expr)) { \
    printf("signalfd ABI line %d: %s errno=%d\n", __LINE__, #expr, errno); return 1; \
} } while (0)

static int signalfd_syscalls(void)
{
    int sig = SIGRTMIN + 4;
    uint64_t mask = (1ULL << (sig - 1)) | (1ULL << (SIGUSR1 - 1)), old;
    SFD_CHECK(syscall(SYS_rt_sigprocmask, SIG_BLOCK, &mask, &old, 8) == 0);
    SFD_CHECK(syscall(SYS_signalfd4, -2, (void *)1, 128, ~0u) == -1 && errno == EINVAL);
    SFD_CHECK(syscall(SYS_signalfd4, -2, (void *)1, 8, ~0u) == -1 && errno == EFAULT);
    SFD_CHECK(syscall(SYS_signalfd4, -2, &mask, 8, ~0u) == -1 && errno == EINVAL);
    SFD_CHECK(syscall(SYS_signalfd4, -2, &mask, 8, 0) == -1 && errno == EBADF);
    int fd = syscall(SYS_signalfd4, -1, &mask, 8, SFD_NONBLOCK | SFD_CLOEXEC);
    SFD_CHECK(fd >= 0);
    int duplicate = syscall(SYS_dup, fd);
    SFD_CHECK(duplicate >= 0 && syscall(SYS_fcntl, duplicate, F_GETFD) == 0);
    SFD_CHECK(syscall(SYS_signalfd, duplicate, &mask, 8) == duplicate);
    SFD_CHECK(syscall(SYS_fcntl, fd, F_GETFD) == FD_CLOEXEC);
    SFD_CHECK(syscall(SYS_fcntl, fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
    int nonblock = 0;
    SFD_CHECK(syscall(SYS_ioctl, duplicate, FIONBIO, &nonblock) == 0);
    SFD_CHECK(syscall(SYS_fcntl, fd, F_GETFL) == O_RDWR);
    nonblock = 1;
    SFD_CHECK(syscall(SYS_ioctl, fd, FIONBIO, &nonblock) == 0);
    SFD_CHECK(syscall(SYS_ioctl, duplicate, FIOCLEX, 0) == 0);
    SFD_CHECK(syscall(SYS_fcntl, duplicate, F_GETFD) == FD_CLOEXEC);
    SFD_CHECK(syscall(SYS_ioctl, duplicate, FIONCLEX, 0) == 0);
    struct signalfd_siginfo output[3];
    SFD_CHECK(syscall(SYS_read, fd, output, 0) == -1 && errno == EINVAL);
    SFD_CHECK(syscall(SYS_read, fd, (void *)1, 128) == -1 && errno == EAGAIN);
    struct iovec oversized[2] = {{(void *)1, INT64_MAX}, {NULL, 0}};
    SFD_CHECK(syscall(SYS_readv, fd, oversized, 1) == -1 && errno == EAGAIN);
    SFD_CHECK(syscall(SYS_readv, fd, oversized, 2) == -1 && errno == EFAULT);
    SFD_CHECK(syscall(SYS_write, fd, (void *)1, 128) == -1 && errno == EINVAL);
    SFD_CHECK(syscall(SYS_pread64, fd, output, 128, 0) == -1 && errno == ESPIPE);
    SFD_CHECK(syscall(SYS_lseek, fd, -9, SEEK_SET) == 0);
    SFD_CHECK(syscall(SYS_lseek, fd, -9L, (1ULL << 32) | SEEK_END) == 0);
    SFD_CHECK(syscall(SYS_lseek, fd, 4, 99) == -1 && errno == EINVAL);
    siginfo_t info = {0};
    info.si_code = SI_QUEUE; info.si_pid = 123; info.si_uid = 456; info.si_errno = 7;
    info.si_value.sival_ptr = (void *)(uintptr_t)0x123456789abcdef0ULL;
    SFD_CHECK(syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0);
    info.si_value.sival_ptr = (void *)2;
    SFD_CHECK(syscall(SYS_rt_tgsigqueueinfo, getpid(), syscall(SYS_gettid), sig, &info) == 0);
    struct pollfd poller = {fd, POLLIN | POLLOUT, 0};
    SFD_CHECK(syscall(SYS_poll, &poller, 1, 0) == 1 && poller.revents == POLLIN);
    int ep = syscall(SYS_epoll_create1, EPOLL_CLOEXEC);
    SFD_CHECK(ep >= 0);
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = 0x1234};
    SFD_CHECK(syscall(SYS_epoll_ctl, ep, EPOLL_CTL_ADD, fd, &event) == 0);
    SFD_CHECK(syscall(SYS_epoll_wait, ep, &event, 1, 0) == 1 && event.data.u64 == 0x1234);
    struct iovec vectors[3] = {{output, 3}, {(char *)output + 3, 20}, {(char *)output + 23, 233}};
    SFD_CHECK(syscall(SYS_readv, duplicate, vectors, 3) == 256);
    SFD_CHECK(output[0].ssi_ptr == 2 && output[1].ssi_ptr == 0x123456789abcdef0ULL);
    SFD_CHECK(output[1].ssi_pid == 123 && output[1].ssi_uid == 456 && output[1].ssi_errno == 7);
    SFD_CHECK(output[1].ssi_int == (int32_t)0x9abcdef0u);
    SFD_CHECK(syscall(SYS_readv, fd, NULL, 0) == 0);
    SFD_CHECK(syscall(SYS_preadv2, fd, vectors, 3, -1L, 0, 8) == -1 && errno == EAGAIN);
    info.si_value.sival_ptr = (void *)3;
    SFD_CHECK(syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0);
    SFD_CHECK(syscall(SYS_read, fd, (void *)1, 128) == -1 && errno == EFAULT);
    SFD_CHECK(syscall(SYS_read, fd, output, 128) == -1 && errno == EAGAIN);
    size_t page = sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    SFD_CHECK(guard != MAP_FAILED && mprotect(guard + page, page, PROT_NONE) == 0);
    SFD_CHECK(syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0);
    SFD_CHECK(syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0);
    SFD_CHECK(syscall(SYS_read, fd, guard + page - 160, 256) == 128);
    SFD_CHECK(syscall(SYS_read, fd, output, 128) == -1 && errno == EAGAIN);
    SFD_CHECK(munmap(guard, page * 2) == 0);
    /* A fork shares the mask/OFD but reads the child's pending queues. */
    info.si_value.sival_ptr = (void *)4;
    SFD_CHECK(syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0);
    pid_t child = fork();
    SFD_CHECK(child >= 0);
    if (!child) {
        int ok = syscall(SYS_read, fd, output, 128) == -1 && errno == EAGAIN;
        info.si_value.sival_ptr = (void *)5;
        ok = ok && syscall(SYS_rt_sigqueueinfo, getpid(), sig, &info) == 0;
        ok = ok && syscall(SYS_read, fd, output, 128) == 128 && output[0].ssi_ptr == 5;
        _exit(ok ? 0 : 1);
    }
    int status;
    SFD_CHECK(waitpid(child, &status, 0) == child && status == 0);
    SFD_CHECK(syscall(SYS_read, fd, output, 128) == 128 && output[0].ssi_ptr == 4);
    SFD_CHECK(close(ep) == 0 && close(duplicate) == 0 && close(fd) == 0);
    SFD_CHECK(syscall(SYS_rt_sigprocmask, SIG_SETMASK, &old, NULL, 8) == 0);
    puts("PASS raw Linux signalfd: native ABI, readv, fault consumption, poll/epoll, shared fd and fork queues");
    return 0;
}

#ifndef SIGNALFD_EMBEDDED
int main(void) { return signalfd_syscalls(); }
#endif
