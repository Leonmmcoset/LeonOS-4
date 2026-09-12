#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[record-locks] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static int fd;
static volatile sig_atomic_t interrupted;
static void alarm_handler(int number) { (void)number; interrupted = 1; }
static int lock(int command, short type, off_t start, off_t length)
{
    struct flock record = {.l_type = type, .l_whence = SEEK_SET, .l_start = start, .l_len = length};
    return syscall(SYS_fcntl, fd, command, &record);
}
static int conflict(void)
{
    struct flock result = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 15, .l_len = 1};
    CHECK(syscall(SYS_fcntl, fd, F_GETLK, &result) == 0 && result.l_type == F_WRLCK &&
          result.l_start == 10 && result.l_len == 10 && result.l_pid == getppid());
    CHECK(lock(F_SETLK, F_RDLCK, 15, 1) == -1 && (errno == EAGAIN || errno == EACCES));
    CHECK(lock(F_SETLK, F_WRLCK, 20, 5) == 0);
    return 0;
}
static int interrupted_wait(void)
{
    struct sigaction action = {.sa_handler = alarm_handler};
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGALRM, &action, NULL) == 0);
    alarm(1);
    CHECK(lock(F_SETLKW, F_WRLCK, 12, 1) == -1 && errno == EINTR && interrupted);
    return 0;
}
static int available(void) { CHECK(lock(F_SETLK, F_WRLCK, 0, 0) == 0); return 0; }
static int wait_child(int (*test)(void))
{
    pid_t child = fork();
    if (!child) _exit(test());
    int status;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    return 0;
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("[record-locks] BEGIN range conflict, fork, close, split, negative length and EINTR");
    fd = open("/tmp/record-locks", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0 && lock(F_SETLK, F_WRLCK, 10, 10) == 0);
    CHECK(wait_child(conflict) == 0);
    CHECK(wait_child(interrupted_wait) == 0);
    CHECK(lock(F_SETLK, F_UNLCK, 12, 3) == 0);
    CHECK(lock(F_SETLK, F_RDLCK, 15, -5) == 0);
    int other = open("/tmp/record-locks", O_RDONLY);
    CHECK(other >= 0 && close(other) == 0);
    CHECK(wait_child(available) == 0);
    CHECK(lock(F_SETLK, F_WRLCK, 0, 0) == 0);
    int duplicate = dup(fd);
    CHECK(duplicate >= 0 && close(duplicate) == 0);
    CHECK(wait_child(available) == 0);
    CHECK(lock(F_SETLK, F_WRLCK, -1, 1) == -1 && errno == EINVAL);
    CHECK(close(fd) == 0 && unlink("/tmp/record-locks") == 0);
    puts("[record-locks] DONE failures=0");
    return 0;
}
