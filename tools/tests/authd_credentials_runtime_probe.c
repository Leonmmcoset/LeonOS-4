#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <leonos/authd.h>
#include <leonos/unix_ipc.h>
#include <poll.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[authd-credentials] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int status_request(int fd)
{
    CHECK(leonos_ipc_send(fd, LEONOS_AUTHD_MSG_STATUS, NULL, 0) == 0);
    struct pollfd ready = {fd, POLLIN, 0};
    CHECK(poll(&ready, 1, 5000) == 1);
    uint32_t type, length;
    struct leonos_auth_status status;
    CHECK(leonos_ipc_recv(fd, &type, &status, sizeof(status), &length) == 0);
    CHECK(type == LEONOS_AUTHD_MSG_STATUS && length == sizeof(status));
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[authd-credentials] BEGIN production authd: valid request, inherited connection rejection");
    int fd = -1;
    for (unsigned i = 0; i < 100 && fd < 0; ++i) {
        fd = leonos_ipc_connect(LEONOS_IPC_SOCK_AUTH);
        if (fd < 0) usleep(100000);
    }
    CHECK(fd >= 0 && status_request(fd) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(leonos_ipc_send(fd, LEONOS_AUTHD_MSG_STATUS, NULL, 0) != 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    struct pollfd ready = {fd, POLLIN, 0};
    CHECK(poll(&ready, 1, 5000) == 1);
    char byte;
    ssize_t result = read(fd, &byte, 1);
    CHECK(result == 0 || (result == -1 && errno == ECONNRESET));
    CHECK(leonos_ipc_close(fd) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (setresgid(1000, 1000, 1000) || setresuid(1000, 1000, 1000)) _exit(1);
        int ordinary = leonos_ipc_connect(LEONOS_IPC_SOCK_AUTH);
        if (ordinary < 0 || status_request(ordinary)) _exit(1);
        _exit(leonos_ipc_close(ordinary) != 0);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    puts("[authd-credentials] DONE failures=0");
    return 0;
}
