#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[tty-hangup] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int descendant(int slave, int mode, int ready, int report, sigset_t *mask)
{
    alarm(8);
    CHECK(setpgid(0, 0) == 0);
    CHECK(write(ready, "r", 1) == 1);
    if (mode == 2) CHECK(raise(SIGSTOP) == 0);
    CHECK(sigwaitinfo(mask, NULL) == SIGHUP);
    if (mode == 2) CHECK(sigwaitinfo(mask, NULL) == SIGCONT);
    CHECK(tcgetpgrp(slave) == -1 && errno == ENOTTY);
    CHECK(open("/dev/tty", O_RDWR) == -1 && errno == ENXIO);
    CHECK(write(slave, "a", 1) == 1);
    CHECK(write(report, "p", 1) == 1);
    return 0;
}

static int leader(int master, const char *path, int mode, int ready, int report)
{
    alarm(10);
    CHECK(close(master) == 0 && setsid() == getpid());
    int slave = open(path, O_RDWR | O_NOCTTY);
    CHECK(slave >= 0 && ioctl(slave, TIOCSCTTY, 0) == 0);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGCONT);
    CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
    if (mode == 0) {
        CHECK(write(ready, "r", 1) == 1);
        CHECK(sigwaitinfo(&mask, NULL) == SIGHUP);
        CHECK(sigwaitinfo(&mask, NULL) == SIGCONT);
        CHECK(tcgetpgrp(slave) == -1 && errno == EIO);
        CHECK(open("/dev/tty", O_RDWR) == -1 && errno == ENXIO);
        CHECK(write(slave, "a", 1) == -1 && errno == EIO);
        char byte;
        CHECK(read(slave, &byte, 1) == 0);
        CHECK(write(report, "p", 1) == 1);
    } else {
        int child_ready[2];
        CHECK(pipe(child_ready) == 0);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) {
            close(child_ready[0]);
            _exit(descendant(slave, mode, child_ready[1], report, &mask));
        }
        close(child_ready[1]);
        char byte;
        CHECK(read(child_ready[0], &byte, 1) == 1);
        if (mode == 1) CHECK(tcsetpgrp(slave, child) == 0);
        else {
            int status;
            CHECK(waitpid(child, &status, WUNTRACED) == child &&
                WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP);
        }
        CHECK(write(ready, "r", 1) == 1);
    }
    return 0;
}

static int run_case(int mode)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    char path[128];
    CHECK(ptsname_r(master, path, sizeof(path)) == 0);
    int ready[2], report[2];
    CHECK(pipe(ready) == 0 && pipe(report) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(ready[0]); close(report[0]);
        _exit(leader(master, path, mode, ready[1], report[1]));
    }
    close(ready[1]); close(report[1]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1 && byte == 'r');
    if (!mode) CHECK(close(master) == 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(read(report[0], &byte, 1) == 1 && byte == 'p');
    if (mode) {
        CHECK(read(master, &byte, 1) == 1 && byte == 'a');
        CHECK(close(master) == 0);
    }
    close(ready[0]); close(report[0]);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(45);
    int failures = 0;
    for (int mode = 0; mode < 3; ++mode) {
        pid_t child = fork();
        if (!child) { alarm(12); _exit(run_case(mode)); }
        int status;
        int failed = child < 0 || waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status);
        printf("[tty-hangup] mode=%d failed=%d\n", mode, failed);
        failures += failed;
    }
    printf("[tty-hangup] DONE failures=%d\n", failures);
    return failures;
}
