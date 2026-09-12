#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[group-signal] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int target(int mode, pid_t group, int ready, int control)
{
    alarm(7);
    CHECK(setpgid(0, group) == 0);
    CHECK(setresuid(group ? 1002 : 1001, group ? 1002 : 1001, group ? 1002 : 1003) == 0);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
    CHECK(write(ready, "r", 1) == 1);
    if (mode == 7) return 0;
    if (mode == 6) {
        siginfo_t info;
        CHECK(sigwaitinfo(&mask, &info) == SIGUSR1);
        CHECK(info.si_code == SI_USER && info.si_pid == getppid() && info.si_uid == 1001);
    }
    char byte;
    CHECK(read(control, &byte, 1) == 1);
    return 0;
}

static int run_case(int mode)
{
    int ready[2], control[2];
    CHECK(pipe(ready) == 0 && pipe(control) == 0);
    pid_t first = fork(), second = 0;
    CHECK(first >= 0);
    if (!first) _exit(target(mode, 0, ready[1], control[0]));
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1);
    if (mode == 5) {
        second = fork();
        CHECK(second >= 0);
        if (!second) _exit(target(mode, first, ready[1], control[0]));
        CHECK(read(ready[0], &byte, 1) == 1);
    }
    if (mode == 7) {
        siginfo_t info;
        CHECK(waitid(P_PID, first, &info, WEXITED | WNOWAIT) == 0 && info.si_pid == first);
    }
    if (mode == 0) CHECK(setresuid(0, 1002, 1002) == 0);
    else if (mode == 1) CHECK(setresuid(1002, 1001, 1002) == 0);
    else if (mode == 2) CHECK(setresuid(1002, 0, 0) == 0);
    else if (mode == 3) CHECK(setresuid(1003, 1003, 1003) == 0);
    else if (mode == 4) CHECK(setresuid(1004, 1004, 1004) == 0);
    else CHECK(setresuid(1001, 1001, 1001) == 0);
    if (mode == 0) CHECK(kill(-first, 0) == -1 && errno == EPERM);
    else if (mode == 4) {
        CHECK(kill(-first, 0) == -1 && errno == EPERM);
        CHECK(kill(-first, SIGCONT) == 0);
    } else CHECK(kill(-first, mode == 6 ? SIGUSR1 : 0) == 0);
    if (mode == 5) CHECK(kill(second, 0) == -1 && errno == EPERM);
    if (mode != 7) CHECK(write(control[1], "cc", second ? 2 : 1) == (second ? 2 : 1));
    int status;
    CHECK(waitpid(first, &status, 0) == first && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (second) CHECK(waitpid(second, &status, 0) == second && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(100);
    int failures = 0;
    for (int mode = 0; mode < 8; ++mode) {
        pid_t child = fork();
        if (!child) { alarm(10); _exit(run_case(mode)); }
        int status;
        int failed = child < 0 || waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status);
        printf("[group-signal] mode=%d failed=%d\n", mode, failed);
        failures += failed;
    }
    printf("[group-signal] DONE failures=%d\n", failures);
    return failures;
}
