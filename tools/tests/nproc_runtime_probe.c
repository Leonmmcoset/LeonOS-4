#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[nproc] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)
static const char *executable;

static int drop_uid(unsigned uid)
{
    return syscall(SYS_setresuid, uid, uid, uid);
}

static int collect(pid_t pid)
{
    int status;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int processes(void)
{
    struct rlimit limit = {2, 2};
    CHECK(setrlimit(RLIMIT_NPROC, &limit) == 0 && drop_uid(1900) == 0);
    limit.rlim_max = 3;
    CHECK(setrlimit(RLIMIT_NPROC, &limit) == -1 && errno == EPERM);
    int done[2];
    CHECK(pipe(done) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(done[0]);
        _exit(write(done[1], "x", 1) != 1);
    }
    close(done[1]);
    char byte;
    CHECK(read(done[0], &byte, 1) == 1);
    CHECK(read(done[0], &byte, 1) == 0);
    CHECK(close(done[0]) == 0);
    CHECK(fork() == -1 && errno == EAGAIN);
    CHECK(collect(child));
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        CHECK(getrlimit(RLIMIT_NPROC, &limit) == 0 && limit.rlim_cur == 2 && limit.rlim_max == 2);
        _exit(0);
    }
    CHECK(collect(child));
    return 0;
}

static atomic_int release_thread;
static void *hold_thread(void *unused)
{
    (void)unused;
    while (!atomic_load(&release_thread)) usleep(1000);
    return NULL;
}

static int threads(void)
{
    struct rlimit limit = {2, 2};
    CHECK(setrlimit(RLIMIT_NPROC, &limit) == 0 && drop_uid(1901) == 0);
    pthread_t first, second;
    CHECK(pthread_create(&first, NULL, hold_thread, NULL) == 0);
    CHECK(pthread_create(&second, NULL, hold_thread, NULL) == EAGAIN);
    atomic_store(&release_thread, 1);
    CHECK(pthread_join(first, NULL) == 0);
    CHECK(pthread_create(&second, NULL, hold_thread, NULL) == 0);
    CHECK(pthread_join(second, NULL) == 0);
    return 0;
}

static int deferred_exec(void)
{
    int ready[2], proceed[2];
    CHECK(pipe(ready) == 0 && pipe(proceed) == 0);
    pid_t holders[2];
    for (unsigned i = 0; i < 2; ++i) {
        holders[i] = fork();
        CHECK(holders[i] >= 0);
        if (!holders[i]) {
            if (drop_uid(1902) < 0 || write(ready[1], "x", 1) != 1) _exit(1);
            for (;;) pause();
        }
    }
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1 && read(ready[0], &byte, 1) == 1);
    struct rlimit limit = {1, 1};
    CHECK(setrlimit(RLIMIT_NPROC, &limit) == 0);
    /* UID 0 is exempt from fork NPROC, even with a limit below its task count. */
    pid_t worker = fork();
    CHECK(worker >= 0);
    if (!worker) {
        CHECK(drop_uid(1902) == 0);
        char *args[] = {(char *)executable, "--after-exec", NULL};
        execv(executable, args);
        CHECK(errno == EAGAIN && getuid() == 1902);
        CHECK(write(ready[1], "x", 1) == 1 && read(proceed[0], &byte, 1) == 1);
        execv(executable, args);
        _exit(1);
    }
    CHECK(read(ready[0], &byte, 1) == 1);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(kill(holders[i], SIGKILL) == 0);
        int status;
        CHECK(waitpid(holders[i], &status, 0) == holders[i] && WIFSIGNALED(status));
    }
    CHECK(write(proceed[1], "x", 1) == 1 && collect(worker));
    CHECK(close(ready[0]) == 0 && close(ready[1]) == 0);
    CHECK(close(proceed[0]) == 0 && close(proceed[1]) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    executable = argv[0];
    if (argc == 2 && !strcmp(argv[1], "--after-exec")) {
        struct rlimit limit;
        CHECK(getuid() == 1902 && getrlimit(RLIMIT_NPROC, &limit) == 0 && limit.rlim_cur == 1);
        return 0;
    }
    puts("[nproc] BEGIN enforced process/thread limits, inheritance, UID change and exec retry");
    struct { const char *name; int (*run)(void); } cases[] = {
        {"fork/zombie/reap/hard limit", processes}, {"pthread limit/release", threads},
        {"root exemption and deferred exec", deferred_exec},
    };
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (!child) _exit(cases[i].run());
        int ok = child > 0 && collect(child);
        printf("[nproc] %s %s\n", ok ? "PASS" : "FAIL", cases[i].name);
        failures += !ok;
    }
    printf("[nproc] DONE failures=%u\n", failures);
    return failures != 0;
}
