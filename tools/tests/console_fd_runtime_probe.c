#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[console-fd] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[console-fd] BEGIN standard descriptors and inherited status");
    for (int fd = 0; fd < 3; ++fd) {
        CHECK(fcntl(fd, F_GETFL) >= 0);
        struct stat st;
        CHECK(fstat(fd, &st) == 0);
    }
    int copy = dup(1), original = fcntl(1, F_GETFL);
    CHECK(copy >= 3 && original >= 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(setsid() >= 0);
        CHECK(fcntl(copy, F_SETFL, original | O_NONBLOCK) == 0);
        CHECK(close(1) == 0);
        CHECK(fcntl(1, F_GETFL) == -1 && errno == EBADF);
        CHECK(open("/dev/null", O_RDWR) == 1);
        CHECK(fcntl(copy, F_GETFL) & O_NONBLOCK);
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(fcntl(1, F_GETFL) & O_NONBLOCK);
    CHECK(fcntl(1, F_SETFL, original) == 0);
    CHECK(close(copy) == 0);
    puts("[console-fd] DONE failures=0");
    return 0;
}
