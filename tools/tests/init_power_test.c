#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#define main init_program_main
#include "../../userland/apps/init/main.c"
#undef main

static unsigned sync_calls, reboot_calls;
static int last_command;
void sync(void) { ++sync_calls; }
int reboot(int command)
{
    assert(sync_calls == reboot_calls + 1);
    ++reboot_calls;
    last_command = command;
    errno = EPERM;
    return -1;
}

int main(void)
{
    sigset_t set, previous;
    assert(sigprocmask(SIG_SETMASK, NULL, &previous) == 0);
    assert(init_block_signals(&set) == 0);
    const int signals[] = {SIGUSR1, SIGUSR2, SIGTERM};
    const int commands[] = {RB_HALT_SYSTEM, RB_POWER_OFF, RB_AUTOBOOT};
    for (unsigned i = 0; i < 3; ++i) {
        assert(kill(getpid(), signals[i]) == 0);
        int received = sigwaitinfo(&set, NULL);
        assert(received == signals[i]);
        assert(init_dispatch_signal(received) == -1 && errno == EPERM);
        assert(reboot_calls == i + 1 && last_command == commands[i]);
    }
    pid_t child = fork();
    assert(child >= 0);
    if (!child) _exit(23);
    assert(sigwaitinfo(&set, NULL) == SIGCHLD);
    assert(init_dispatch_signal(SIGCHLD) == 0 && reboot_calls == 3);
    int status;
    assert(waitpid(child, &status, 0) == child && WEXITSTATUS(status) == 23);
    assert(init_dispatch_signal(SIGWINCH) == 0 && reboot_calls == 3);
    assert(sigprocmask(SIG_SETMASK, &previous, NULL) == 0);
    puts("PASS init handles upstream BusyBox power signals and survives failed reboot");
}
