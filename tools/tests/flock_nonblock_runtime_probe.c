#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "[flock-nb] FAIL line=%d errno=%d: %s\n", \
    __LINE__, errno, #c); exit(1); } } while (0)
static void timeout(int signal) { (void)signal; }
static int raw_lock(int fd, int operation) { return syscall(SYS_flock, fd, operation); }

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    struct sigaction action = {.sa_handler = timeout};
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGALRM, &action, NULL) == 0);
    char path[] = "/tmp/flock-nb-XXXXXX";
    int owner = mkstemp(path);
    CHECK(owner >= 0 && raw_lock(owner, LOCK_EX | LOCK_NB) == 0);
    int reader = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(reader >= 0);
    /* Match the GUI marker: separate readonly OFD, not O_NONBLOCK. */
    alarm(2);
    CHECK(raw_lock(reader, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    alarm(0);
    CHECK(raw_lock(reader, LOCK_SH | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    int duplicate = dup(owner);
    CHECK(duplicate >= 0 && close(owner) == 0);
    CHECK(raw_lock(reader, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(close(duplicate) == 0);
        alarm(2);
        CHECK(raw_lock(reader, LOCK_SH | LOCK_NB) == -1 && errno == EWOULDBLOCK);
        /* Blocking flock must still sleep and be interruptible. */
        alarm(1);
        CHECK(raw_lock(reader, LOCK_EX) == -1 && errno == EINTR);
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(raw_lock(duplicate, LOCK_UN) == 0);
    CHECK(raw_lock(reader, LOCK_EX | LOCK_NB) == 0);
    CHECK(close(duplicate) == 0 && close(reader) == 0 && unlink(path) == 0);
    puts("[flock-nb] DONE failures=0");
    return 0;
}
