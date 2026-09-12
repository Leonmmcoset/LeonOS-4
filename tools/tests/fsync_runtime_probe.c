#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[fsync] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[fsync] BEGIN file, directory, fdatasync, syncfs and rejected descriptors");
    int fd = open("/tmp/fsync-data", O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "persist", 7) == 7 && fsync(fd) == 0 && fdatasync(fd) == 0);
    CHECK(syncfs(fd) == 0 && close(fd) == 0);
    int directory = open("/tmp", O_RDONLY | O_DIRECTORY);
    CHECK(directory >= 0 && fsync(directory) == 0 && close(directory) == 0);
    fd = open("/tmp/fsync-data", O_PATH);
    CHECK(fd >= 0 && fsync(fd) == -1 && errno == EBADF && close(fd) == 0);
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    CHECK(fsync(pipefd[0]) == -1 && errno == EINVAL);
    CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    fd = open("/dev/null", O_WRONLY);
    CHECK(fd >= 0 && fsync(fd) == -1 && errno == EINVAL && close(fd) == 0);
    fd = open("/proc/self/status", O_RDONLY);
    CHECK(fd >= 0 && fsync(fd) == -1 && errno == EINVAL && close(fd) == 0);
    CHECK(unlink("/tmp/fsync-data") == 0);
    sync();
    puts("[fsync] DONE failures=0");
    return 0;
}
