#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[pty-ofd] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int pair(int *master, int *slave)
{
    *master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (*master < 0 || grantpt(*master) || unlockpt(*master)) return -1;
    char path[128];
    if (ptsname_r(*master, path, sizeof(path))) return -1;
    *slave = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (*slave < 0) return -1;
    struct termios mode;
    if (tcgetattr(*slave, &mode)) return -1;
    cfmakeraw(&mode);
    return tcsetattr(*slave, TCSANOW, &mode);
}

static int send_fd(int socket, int fd)
{
    char byte = 'f', control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {&byte, 1};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(fd));
    memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    return sendmsg(socket, &message, 0) == 1 ? 0 : -1;
}

static int receive_fd(int socket)
{
    char byte, control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec vector = {&byte, 1};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    if (recvmsg(socket, &message, MSG_CMSG_CLOEXEC) != 1 || message.msg_flags & MSG_CTRUNC) return -1;
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(sizeof(int))) return -1;
    int fd;
    memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    return fd;
}

static int shared_flags(void)
{
    int master, slave;
    CHECK(pair(&master, &slave) == 0);
    int duplicate = dup(slave);
    CHECK(duplicate >= 0);
    CHECK(fcntl(slave, F_GETFD) == FD_CLOEXEC && fcntl(duplicate, F_GETFD) == 0);
    CHECK(fcntl(duplicate, F_SETFL, O_NONBLOCK | O_APPEND) == 0);
    CHECK((fcntl(slave, F_GETFL) & (O_NONBLOCK | O_APPEND)) == (O_NONBLOCK | O_APPEND));
    char path[128];
    CHECK(ptsname_r(master, path, sizeof(path)) == 0);
    int independent = open(path, O_RDWR | O_NOCTTY);
    CHECK(independent >= 0 && !(fcntl(independent, F_GETFL) & (O_NONBLOCK | O_APPEND)));
    CHECK(close(independent) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(fcntl(slave, F_SETFL, O_APPEND) != 0 || fcntl(slave, F_SETFD, 0) != 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK((fcntl(duplicate, F_GETFL) & (O_NONBLOCK | O_APPEND)) == O_APPEND);
    CHECK(fcntl(slave, F_GETFD) == FD_CLOEXEC);
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    CHECK(send_fd(sockets[0], slave) == 0);
    CHECK(fcntl(duplicate, F_SETFL, O_NONBLOCK) == 0);
    CHECK(close(slave) == 0 && close(duplicate) == 0);
    slave = receive_fd(sockets[1]);
    CHECK(slave >= 0 && fcntl(slave, F_GETFD) == FD_CLOEXEC);
    CHECK((fcntl(slave, F_GETFL) & (O_NONBLOCK | O_APPEND)) == O_NONBLOCK);
    CHECK(write(master, "q", 1) == 1);
    char byte;
    CHECK(read(slave, &byte, 1) == 1 && byte == 'q');
    CHECK(send_fd(sockets[0], master) == 0 && close(master) == 0);
    CHECK(write(slave, "r", 1) == 1);
    master = receive_fd(sockets[1]);
    CHECK(master >= 0 && read(master, &byte, 1) == 1 && byte == 'r');
    CHECK(close(master) == 0);
    CHECK(write(slave, "x", 1) == -1 && errno == EIO);
    CHECK(read(slave, &byte, 1) == 0);
    CHECK(close(slave) == 0 && close(sockets[0]) == 0 && close(sockets[1]) == 0);
    return 0;
}

static atomic_int reading, finished;
static ssize_t read_result;
static char read_byte;

static int queued_owner_exit(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        int master, slave;
        close(sockets[1]);
        if (pair(&master, &slave) || send_fd(sockets[0], master) || send_fd(sockets[0], slave)) _exit(1);
        _exit(0);
    }
    close(sockets[0]);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    int master = receive_fd(sockets[1]), slave = receive_fd(sockets[1]);
    CHECK(master >= 0 && slave >= 0);
    CHECK(write(master, "o", 1) == 1);
    char byte;
    CHECK(read(slave, &byte, 1) == 1 && byte == 'o');
    CHECK(close(master) == 0 && close(slave) == 0 && close(sockets[1]) == 0);
    return 0;
}

static int discarded_rights(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        int master, slave, sockets[2];
        CHECK(pair(&master, &slave) == 0);
        CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
        CHECK(send_fd(sockets[0], master) == 0 && close(master) == 0);
        if (mode == 2) {
            CHECK(close(sockets[1]) == 0);
        } else {
            struct rlimit limit, small;
            CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
            small = limit;
            small.rlim_cur = 3;
            if (mode == 1) CHECK(setrlimit(RLIMIT_NOFILE, &small) == 0);
            char byte, control[CMSG_SPACE(sizeof(int))] = {0};
            struct iovec vector = {&byte, 1};
            struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1,
                .msg_control = mode ? control : NULL, .msg_controllen = mode ? sizeof(control) : 0};
            CHECK(recvmsg(sockets[1], &message, MSG_CMSG_CLOEXEC) == 1);
            CHECK(message.msg_flags & MSG_CTRUNC);
            CHECK(message.msg_controllen == 0);
            if (mode == 1) CHECK(setrlimit(RLIMIT_NOFILE, &limit) == 0);
            CHECK(close(sockets[1]) == 0);
        }
        CHECK(write(slave, "d", 1) == -1 && errno == EIO);
        CHECK(close(slave) == 0 && close(sockets[0]) == 0);
    }
    return 0;
}

static int unshare_files(int *fds)
{
    CHECK(syscall(SYS_close_range, (unsigned)fds[0], (unsigned)fds[0], 2) == 0);
    CHECK(fcntl(fds[0], F_GETFD) == -1 && errno == EBADF);
    CHECK(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
    return 0;
}

static void *unshare_worker(void *argument)
{
    return (void *)(long)unshare_files(argument);
}

static int shared_table_exec(const char *self)
{
    int master, slave;
    CHECK(pair(&master, &slave) == 0);
    int duplicate = dup(slave), fds[] = {slave, duplicate};
    CHECK(duplicate >= 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, unshare_worker, fds) == 0);
    void *result;
    CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    CHECK(fcntl(slave, F_GETFD) == FD_CLOEXEC && fcntl(slave, F_GETFL) & O_NONBLOCK);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        char closed[16], inherited[16];
        snprintf(closed, sizeof(closed), "%d", slave);
        snprintf(inherited, sizeof(inherited), "%d", duplicate);
        execl(self, self, "--after-exec", closed, inherited, NULL);
        _exit(126);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK((fcntl(slave, F_GETFL) & (O_NONBLOCK | O_APPEND)) == O_APPEND);
    CHECK(close(slave) == 0 && close(duplicate) == 0 && close(master) == 0);
    return 0;
}

static void *blocked_reader(void *argument)
{
    atomic_store(&reading, 1);
    read_result = read(*(int *)argument, &read_byte, 1);
    atomic_store(&finished, 1);
    return NULL;
}

static int blocked_lifetime(void)
{
    int master, slave;
    CHECK(pair(&master, &slave) == 0);
    pthread_t thread;
    atomic_store(&reading, 0);
    atomic_store(&finished, 0);
    CHECK(pthread_create(&thread, NULL, blocked_reader, &slave) == 0);
    while (!atomic_load(&reading)) usleep(1000);
    usleep(100000);
    CHECK(!atomic_load(&finished));
    int replacement = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(replacement >= 0 && dup2(replacement, slave) == slave && close(replacement) == 0);
    CHECK(write(master, "b", 1) == 1);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(read_result == 1 && read_byte == 'b');
    CHECK(read(slave, &read_byte, 1) == 0);
    CHECK(close(slave) == 0 && close(master) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 4 && !strcmp(argv[1], "--after-exec")) {
        int closed = atoi(argv[2]), inherited = atoi(argv[3]);
        CHECK(fcntl(closed, F_GETFD) == -1 && errno == EBADF);
        CHECK(fcntl(inherited, F_GETFD) == 0 && fcntl(inherited, F_GETFL) & O_NONBLOCK);
        CHECK(fcntl(inherited, F_SETFL, O_APPEND) == 0);
        return 0;
    }
    alarm(30);
    puts("[pty-ofd] BEGIN dup/fork/SCM flags and blocked read replacement");
    int failures = 0;
    for (unsigned i = 0; i < 24 && !failures; ++i) failures += shared_flags();
    printf("[pty-ofd] shared-flags cycles=24 failures=%d\n", failures);
    failures += blocked_lifetime();
    failures += queued_owner_exit();
    failures += discarded_rights();
    failures += shared_table_exec(argv[0]);
    printf("[pty-ofd] DONE failures=%d\n", failures);
    return failures != 0;
}
