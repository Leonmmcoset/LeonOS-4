#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[wait-runtime] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int lifecycle(void)
{
    int ready[2], control[2];
    CHECK(pipe(ready) == 0 && pipe(control) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        alarm(10);
        CHECK(setpgid(0, 0) == 0 && setresuid(1001, 1001, 1001) == 0);
        CHECK(write(ready[1], "r", 1) == 1);
        char byte;
        CHECK(read(control[0], &byte, 1) == 1);
        _exit(23);
    }
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1 && kill(child, SIGSTOP) == 0);
    siginfo_t info;
    for (int i = 0; i < 2; ++i) {
        CHECK(waitid(P_PID, child, &info, WSTOPPED | WNOWAIT) == 0);
        CHECK(info.si_signo == SIGCHLD && info.si_pid == child && info.si_uid == 1001 &&
            info.si_code == CLD_STOPPED && info.si_status == SIGSTOP);
    }
    int status;
    CHECK(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status));
    memset(&info, 0xff, sizeof(info));
    CHECK(waitid(P_PID, child, &info, WSTOPPED | WNOHANG) == 0 && info.si_pid == 0 && info.si_signo == 0);
    CHECK(kill(child, SIGCONT) == 0);
    for (int i = 0; i < 2; ++i) {
        CHECK(waitid(P_PGID, child, &info, WCONTINUED | WNOWAIT) == 0);
        CHECK(info.si_pid == child && info.si_uid == 1001 &&
            info.si_code == CLD_CONTINUED && info.si_status == SIGCONT);
    }
    CHECK(waitpid(child, &status, WCONTINUED) == child && WIFCONTINUED(status));
    CHECK(write(control[1], "x", 1) == 1);
    for (int i = 0; i < 2; ++i) {
        CHECK(waitid(P_PID, child, &info, WEXITED | WNOWAIT) == 0);
        CHECK(info.si_pid == child && info.si_uid == 1001 &&
            info.si_code == CLD_EXITED && info.si_status == 23);
        CHECK(kill(-child, 0) == 0);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 23);
    memset(&info, 0xff, sizeof(info));
    CHECK(waitid(P_PID, child, &info, WEXITED) == -1 && errno == ECHILD && info.si_pid == 0);
    close(ready[0]); close(ready[1]); close(control[0]); close(control[1]);
    return 0;
}

static int copy_faults(void)
{
    void *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    for (int mode = 0; mode < 3; ++mode) {
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) _exit(42);
        siginfo_t info;
        CHECK(waitid(P_PID, child, &info, WEXITED | WNOWAIT) == 0);
        if (mode == 0) CHECK(waitpid(child, readonly, 0) == -1 && errno == EFAULT);
        else CHECK(waitid(P_PID, child, readonly, WEXITED | (mode == 2 ? WNOWAIT : 0)) == -1 && errno == EFAULT);
        int status;
        if (mode == 2) CHECK(waitpid(child, &status, 0) == child && WEXITSTATUS(status) == 42);
        else CHECK(waitpid(child, &status, 0) == -1 && errno == ECHILD);
    }
    CHECK(munmap(readonly, 4096) == 0);
    return 0;
}

int main(void)
{
    alarm(20);
    setvbuf(stdout, NULL, _IONBF, 0);
    int failures = lifecycle() + copy_faults();
    printf("[wait-runtime] DONE failures=%d\n", failures);
    return failures;
}
