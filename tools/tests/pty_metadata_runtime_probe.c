#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[pty-metadata] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int unprivileged(const char *path, unsigned uid, unsigned gid)
{
    CHECK(syscall(SYS_setgroups, 0, NULL) == 0);
    CHECK(syscall(SYS_setresgid, gid, gid, gid) == 0);
    CHECK(syscall(SYS_setresuid, uid, uid, uid) == 0);
    int fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (gid == 1000) {
        CHECK(fd >= 0);
        CHECK(write(fd, "x", 1) == -1 && errno == EBADF);
        char byte;
        CHECK(read(fd, &byte, 0) == 0);
        CHECK(close(fd) == 0);
    }
    else CHECK(fd == -1 && errno == EACCES);
    CHECK(open(path, O_WRONLY | O_NOCTTY | O_NONBLOCK) == -1 && errno == EACCES);
    CHECK(chmod(path, 0666) == -1 && errno == EPERM);
    CHECK(chown(path, uid, gid) == -1 && errno == EPERM);
    return 0;
}

static int run(void)
{
    CHECK(geteuid() == 0);
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    char path[128];
    CHECK(ptsname_r(master, path, sizeof(path)) == 0);
    struct stat before, after;
    CHECK(stat(path, &before) == 0 && S_ISCHR(before.st_mode));
    CHECK(before.st_uid == geteuid());
    int slave = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    CHECK(slave >= 0);
    CHECK(chown(path, 1000, 1000) == 0);
    CHECK(chmod(path, 0640) == 0);
    CHECK(fstat(slave, &after) == 0 && S_ISCHR(after.st_mode));
    CHECK(after.st_uid == 1000 && after.st_gid == 1000 && (after.st_mode & 07777) == 0640);
    CHECK(before.st_ino == after.st_ino && before.st_dev == after.st_dev && before.st_rdev == after.st_rdev);
    for (unsigned gid = 1000; gid < 1002; ++gid) {
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) _exit(unprivileged(path, 1001, gid));
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(fchown(slave, 1002, 1002) == 0 && fchmod(slave, 0600) == 0);
    CHECK(stat(path, &after) == 0 && after.st_uid == 1002 && after.st_gid == 1002 && (after.st_mode & 07777) == 0600);
    CHECK(open(path, O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
    CHECK(open(path, O_RDWR | O_CREAT | O_EXCL, 0600) == -1 && errno == EEXIST);
    CHECK(close(master) == 0);
    CHECK(stat(path, &after) == -1 && errno == ENOENT);
    CHECK(fstat(slave, &after) == 0 && after.st_uid == 1002);
    CHECK(close(slave) == 0);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[pty-metadata] BEGIN path/fd metadata, ownership, DAC and hangup lifetime");
    int failure = run();
    printf("[pty-metadata] DONE failures=%d\n", failure);
    return failure;
}
