#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[password-lock] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--child")) {
        int ready = atoi(argv[2]);
        CHECK(write(ready, "r", 1) == 1 && close(ready) == 0);
        CHECK(lckpwdf() == 0 && ulckpwdf() == 0);
        return 0;
    }
    puts("[password-lock] BEGIN real musl lock, independent processes, persistent inode");
    CHECK(mkdir("/etc", 0755) == 0 || errno == EEXIST);
    CHECK(lckpwdf() == 0);
    struct stat before, after;
    CHECK(stat("/etc/.pwd.lock", &before) == 0 && before.st_uid == 0 && (before.st_mode & 0777) == 0600);
    CHECK(lckpwdf() == -1 && errno == EBUSY);
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    pid_t child = fork();
    if (!child) {
        char descriptor[20];
        snprintf(descriptor, sizeof(descriptor), "%d", pipefd[1]);
        close(pipefd[0]);
        execl(argv[0], argv[0], "--child", descriptor, NULL);
        _exit(127);
    }
    char ready;
    CHECK(child > 0 && close(pipefd[1]) == 0 && read(pipefd[0], &ready, 1) == 1 && close(pipefd[0]) == 0);
    struct timespec delay = {.tv_nsec = 100000000};
    CHECK(nanosleep(&delay, NULL) == 0);
    int status;
    CHECK(waitpid(child, &status, WNOHANG) == 0);
    CHECK(ulckpwdf() == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(stat("/etc/.pwd.lock", &after) == 0 && after.st_ino == before.st_ino);
    for (unsigned i = 0; i < 100; ++i) CHECK(lckpwdf() == 0 && ulckpwdf() == 0);
    CHECK(ulckpwdf() == -1 && errno == EINVAL);
    CHECK(unlink("/etc/.pwd.lock") == 0);
    CHECK(symlink("/etc/passwd", "/etc/.pwd.lock") == 0);
    CHECK(lckpwdf() == -1 && errno == ELOOP);
    CHECK(unlink("/etc/.pwd.lock") == 0);
    puts("[password-lock] DONE failures=0");
    return 0;
}
