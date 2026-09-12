#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { printf("[tty-session] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); return 1; } } while (0)

static int background_read(int slave)
{
    CHECK(setpgid(0, 0) == 0);
    char byte = 0;
    CHECK(read(slave, &byte, 1) == 1 && byte == 'j');
    return 0;
}

static volatile sig_atomic_t received;
static void caught(int sig) { received = sig; }

static int background_operation(int slave, int mode)
{
    CHECK(setpgid(0, 0) == 0);
    sigset_t mask;
    sigemptyset(&mask);
    CHECK(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);
    struct termios attrs;
    CHECK(tcgetattr(slave, &attrs) == 0);
    if (mode == 0) CHECK(write(slave, "w", 1) == 1);
    else if (mode == 1 || mode == 10) CHECK(tcsetattr(slave, TCSANOW, &attrs) == 0);
    else if (mode == 2) CHECK(tcsetpgrp(slave, getpgrp()) == 0);
    else if (mode == 3 || mode == 4) {
        if (mode == 3) {
            sigaddset(&mask, SIGTTIN);
            CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
        } else CHECK(signal(SIGTTIN, SIG_IGN) != SIG_ERR);
        char byte;
        CHECK(read(slave, &byte, 1) == -1 && errno == EIO);
    } else if (mode == 5 || mode == 6) {
        if (mode == 5) {
            sigaddset(&mask, SIGTTOU);
            CHECK(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
        } else CHECK(signal(SIGTTOU, SIG_IGN) != SIG_ERR);
        CHECK(write(slave, "w", 1) == 1);
        CHECK(tcsetattr(slave, TCSANOW, &attrs) == 0);
    } else {
        int sig = mode == 9 ? SIGTTIN : SIGTTOU;
        struct sigaction action = {.sa_handler = caught};
        sigemptyset(&action.sa_mask);
        CHECK(sigaction(sig, &action, NULL) == 0);
        char byte;
        int rc = mode == 7 ? tcsetattr(slave, TCSANOW, &attrs) :
            mode == 8 ? (int)write(slave, "w", 1) : (int)read(slave, &byte, 1);
        CHECK(rc == -1 && errno == EINTR && received == sig);
    }
    return 0;
}

static int background_cases(int master, int slave)
{
    struct termios attrs;
    CHECK(tcgetattr(slave, &attrs) == 0);
    attrs.c_lflag |= TOSTOP;
    CHECK(tcsetattr(slave, TCSANOW, &attrs) == 0);
    for (int mode = 0; mode < 11; ++mode) {
        printf("[tty-session] background mode=%d\n", mode);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) _exit(background_operation(mode == 10 ? master : slave, mode));
        int status;
        if (mode < 3 || mode == 10) {
            CHECK(waitpid(child, &status, WUNTRACED) == child &&
                WIFSTOPPED(status) && WSTOPSIG(status) == SIGTTOU);
            CHECK(tcsetpgrp(slave, child) == 0 && kill(child, SIGCONT) == 0);
        }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(tcsetpgrp(slave, getpgrp()) == 0);
        if (mode == 0 || mode == 5 || mode == 6) {
            char byte;
            CHECK(read(master, &byte, 1) == 1 && byte == 'w');
        }
    }
    return 0;
}

static int orphan_operation(int slave, int control, int report)
{
    alarm(10);
    CHECK(setpgid(0, 0) == 0);
    sigset_t mask;
    sigemptyset(&mask);
    CHECK(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);
    char byte;
    CHECK(read(control, &byte, 1) == 1);
    struct termios attrs;
    CHECK(tcgetattr(slave, &attrs) == 0);
    CHECK(read(slave, &byte, 1) == -1 && errno == EIO);
    CHECK(write(slave, "x", 1) == -1 && errno == EIO);
    CHECK(tcsetattr(slave, TCSANOW, &attrs) == -1 && errno == EIO);
    CHECK(tcsetpgrp(slave, getpgrp()) == -1 && errno == ENOTTY);
    CHECK(write(report, "o", 1) == 1);
    return 0;
}

static int orphan_case(int slave)
{
    int control[2], report[2];
    CHECK(pipe(control) == 0 && pipe(report) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(control[1]); close(report[0]);
        pid_t grandchild = fork();
        if (!grandchild) _exit(orphan_operation(slave, control[0], report[1]));
        _exit(grandchild < 0);
    }
    close(control[0]); close(report[1]);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(write(control[1], "s", 1) == 1);
    char byte;
    CHECK(read(report[0], &byte, 1) == 1 && byte == 'o');
    close(control[1]); close(report[0]);
    return 0;
}

static int signal_receiver(int slave, int mode, int ready, int control)
{
    alarm(8);
    CHECK(setpgid(0, 0) == 0 && setresuid(2000, 2000, 2000) == 0);
    int sig = mode == 0 ? SIGINT : mode == 1 ? SIGQUIT : mode == 2 ? SIGTSTP : SIGWINCH;
    sigset_t mask;
    sigemptyset(&mask);
    if (mode != 2) sigaddset(&mask, sig);
    CHECK(sigprocmask(SIG_SETMASK, &mask, NULL) == 0);
    CHECK(write(ready, "r", 1) == 1);
    if (mode != 2) {
        siginfo_t info;
        CHECK(sigwaitinfo(&mask, &info) == sig);
        if (mode == 3) {
            struct winsize size;
            CHECK(ioctl(slave, TIOCGWINSZ, &size) == 0);
            CHECK(size.ws_row == 0 && size.ws_col == 0 && size.ws_xpixel == 320 && size.ws_ypixel == 240);
        }
    } else {
        char byte;
        CHECK(read(control, &byte, 1) == 1);
    }
    return 0;
}

static int terminal_signals(int master, int slave)
{
    CHECK(setresuid(1000, 0, 0) == 0);
    struct termios attrs;
    CHECK(tcgetattr(slave, &attrs) == 0);
    attrs.c_lflag |= ISIG;
    CHECK(tcsetattr(slave, TCSANOW, &attrs) == 0);
    for (int mode = 0; mode < 4; ++mode) {
        printf("[tty-session] cross-uid signal mode=%d\n", mode);
        int ready[2], control[2];
        CHECK(pipe(ready) == 0 && pipe(control) == 0);
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) {
            close(ready[0]); close(control[1]);
            _exit(signal_receiver(slave, mode, ready[1], control[0]));
        }
        close(ready[1]); close(control[0]);
        char byte;
        CHECK(read(ready[0], &byte, 1) == 1 && byte == 'r');
        CHECK(tcsetpgrp(slave, child) == 0);
        if (mode == 3) {
            struct winsize size = {.ws_xpixel = 320, .ws_ypixel = 240};
            CHECK(ioctl(master, TIOCSWINSZ, &size) == 0);
        } else {
            byte = (char)attrs.c_cc[mode == 0 ? VINTR : mode == 1 ? VQUIT : VSUSP];
            CHECK(write(master, &byte, 1) == 1);
        }
        int status;
        if (mode == 2) {
            CHECK(waitpid(child, &status, WUNTRACED) == child &&
                WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP);
            CHECK(kill(child, SIGCONT) == 0 && write(control[1], "c", 1) == 1);
        }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(tcsetpgrp(slave, getpgrp()) == 0);
        close(ready[0]); close(control[1]);
    }
    CHECK(setresuid(0, 0, 0) == 0);
    return 0;
}

static int session(void)
{
    signal(SIGHUP, SIG_IGN);
    CHECK(setsid() == getpid());
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    char path[128];
    CHECK(ptsname_r(master, path, sizeof(path)) == 0);
    int slave = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    CHECK(slave >= 0);
    CHECK(tcgetpgrp(slave) == -1 && errno == ENOTTY);
    CHECK(tcgetsid(slave) == -1 && errno == ENOTTY);
    CHECK(tcgetpgrp(master) == 0);
    CHECK(tcgetsid(master) == -1 && errno == ENOTTY);
    CHECK(dup2(slave, 0) == 0);
    CHECK(tcgetpgrp(0) == -1 && errno == ENOTTY);
    CHECK(ioctl(slave, TIOCSCTTY, 0) == 0);
    CHECK(tcgetpgrp(slave) == getpgrp() && tcgetsid(slave) == getsid(0));
    CHECK(tcgetpgrp(master) == getpgrp() && tcgetsid(master) == getsid(0));
    CHECK(tcsetpgrp(slave, getpgrp()) == 0);
    CHECK(tcsetpgrp(slave, -1) == -1 && errno == EINVAL);
    CHECK(tcsetpgrp(slave, 0) == -1 && errno == ESRCH);
    CHECK(tcsetpgrp(slave, 0x7fffffff) == -1 && errno == ESRCH);
    CHECK(tcsetpgrp(slave, getppid()) == -1 && errno == EPERM);
    int controlling = open("/dev/tty", O_RDWR | O_CLOEXEC);
    CHECK(controlling >= 0 && tcgetsid(controlling) == getpid());
    CHECK(close(controlling) == 0);
    CHECK(fchmod(slave, 0000) == 0);
    pid_t unprivileged = fork();
    CHECK(unprivileged >= 0);
    if (!unprivileged) {
        CHECK(setresgid(1000, 1000, 1000) == 0 && setresuid(1000, 1000, 1000) == 0);
        CHECK(open(path, O_RDWR | O_NOCTTY) == -1 && errno == EACCES);
        int alias = open("/dev/tty", O_RDWR | O_CLOEXEC);
        CHECK(alias >= 0 && tcgetsid(alias) == getsid(0));
        CHECK(close(alias) == 0);
        _exit(0);
    }
    int alias_status;
    CHECK(waitpid(unprivileged, &alias_status, 0) == unprivileged &&
          WIFEXITED(alias_status) && WEXITSTATUS(alias_status) == 0);
    CHECK(fchmod(slave, 0620) == 0);
    struct termios mode;
    CHECK(tcgetattr(slave, &mode) == 0);
    cfmakeraw(&mode);
    CHECK(tcsetattr(slave, TCSANOW, &mode) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) _exit(background_read(slave));
    int status;
    CHECK(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status) && WSTOPSIG(status) == SIGTTIN);
    CHECK(tcsetpgrp(slave, child) == 0);
    CHECK(write(master, "j", 1) == 1 && kill(child, SIGCONT) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTTOU);
    CHECK(sigprocmask(SIG_BLOCK, &blocked, NULL) == 0);
    CHECK(tcsetpgrp(slave, getpgrp()) == 0);
    CHECK(background_cases(master, slave) == 0);
    CHECK(orphan_case(slave) == 0);
    CHECK(terminal_signals(master, slave) == 0);
    CHECK(ioctl(master, TIOCNOTTY) == -1 && errno == ENOTTY);
    CHECK(ioctl(slave, TIOCNOTTY) == 0);
    CHECK(tcgetpgrp(slave) == -1 && errno == ENOTTY);
    CHECK(tcgetsid(slave) == -1 && errno == ENOTTY);
    CHECK(open("/dev/tty", O_RDWR) == -1 && errno == ENXIO);
    CHECK(write(master, "u", 1) == 1);
    char byte;
    CHECK(read(slave, &byte, 1) == 1 && byte == 'u');
    int automatic = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(automatic >= 0 && tcgetsid(automatic) == getpid());
    CHECK(close(automatic) == 0 && close(0) == 0 && close(slave) == 0 && close(master) == 0);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(20);
    puts("[tty-session] BEGIN controlling tty, foreground ownership and background read");
    pid_t child = fork();
    if (!child) { alarm(15); _exit(session()); }
    int status, failures = 1;
    if (child > 0 && waitpid(child, &status, 0) == child)
        failures = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    printf("[tty-session] DONE failures=%d\n", failures);
    return failures;
}
