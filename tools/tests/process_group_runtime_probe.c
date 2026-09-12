#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[process-group] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int worker_result;
static void *worker(void *arg)
{
    long mode = (long)arg;
    pid_t tgid = getpid();
    if (mode == 2) worker_result = setsid() != tgid || getsid(0) != tgid || getpgrp() != tgid;
    else if (mode == 3) worker_result = setpgid(0, 0) || getpgrp() != tgid;
    else worker_result = syscall(SYS_setpgid, syscall(SYS_gettid), 0) != -1 || errno != EINVAL;
    return NULL;
}

static int exec_boundary(void)
{
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    CHECK(n > 0);
    path[n] = 0;
    int ready[2], control[2];
    CHECK(pipe(ready) == 0 && pipe(control) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(ready[0]); close(control[1]);
        char out[24], in[24];
        snprintf(out, sizeof(out), "%d", ready[1]);
        snprintf(in, sizeof(in), "%d", control[0]);
        char *args[] = {path, "--exec-child", out, in, NULL};
        execv(path, args);
        _exit(127);
    }
    close(ready[1]); close(control[0]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1 && byte == 'r');
    CHECK(setpgid(child, child) == -1 && errno == EACCES);
    CHECK(write(control[1], "x", 1) == 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(ready[0]); close(control[1]);
    return 0;
}

static int run_case(int mode)
{
    if (mode == 0) {
        pid_t old = getpgrp();
        CHECK(setpgid(0, 0) == 0 && getpgrp() == getpid());
        CHECK(setpgid(0, old) == 0 && getpgrp() == old);
        CHECK(setsid() == getpid());
        CHECK(setpgid(0, 0) == -1 && errno == EPERM);
        CHECK(setsid() == -1 && errno == EPERM);
    } else if (mode == 1) {
        CHECK(setpgid(0, -1) == -1 && errno == EINVAL);
        CHECK(setpgid(-1, 1) == -1 && errno == ESRCH);
        CHECK(setpgid(-1, 0) == -1 && errno == EINVAL);
        CHECK(setpgid(getppid(), 0) == -1 && errno == ESRCH);
        CHECK(setpgid(0, 0x7fffffff) == -1 && errno == EPERM);
        CHECK(getpgid(-1) == -1 && errno == ESRCH);
        CHECK(getsid(-1) == -1 && errno == ESRCH);
    } else if (mode < 5) {
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, worker, (void *)(long)mode) == 0);
        CHECK(pthread_join(thread, NULL) == 0 && worker_result == 0);
        if (mode == 2) CHECK(getsid(0) == getpid() && getpgrp() == getpid());
        if (mode == 3) CHECK(getpgrp() == getpid());
    } else CHECK(exec_boundary() == 0);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(15);
    if (argc == 4 && !strcmp(argv[1], "--exec-child")) {
        char byte;
        CHECK(write(atoi(argv[2]), "r", 1) == 1);
        CHECK(read(atoi(argv[3]), &byte, 1) == 1);
        return 0;
    }
    /* The minimal Linux init starts in the special initial process group 0. */
    if (getpgrp() != getpid()) CHECK(setpgid(0, 0) == 0);
    int failures = 0;
    for (int mode = 0; mode < 6; ++mode) {
        pid_t child = fork();
        if (!child) { alarm(10); _exit(run_case(mode)); }
        int status = 0;
        int failed = child < 0 || waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status);
        printf("[process-group] mode=%d failed=%d\n", mode, failed);
        failures += failed;
    }
    printf("[process-group] DONE failures=%d\n", failures);
    return failures;
}
