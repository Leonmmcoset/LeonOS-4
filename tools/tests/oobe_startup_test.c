#include <assert.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define wait4 leonos_decl_wait4
#include "../../userland/apps/desktop/input.c"
#undef wait4

uint8_t desktop_startup_launched;
static int completion[2];

int leonos_decl_wait4(int pid, int *status, int options, void *usage)
{
    assert(options == WNOHANG && !usage);
    return waitpid(pid, status, options);
}

int leonos_startup_launch_current_user(void)
{
    /* Model a session service occupied by a slow operation. */
    usleep(350000);
    assert(write(completion[1], "x", 1) == 1);
    return 0;
}

int main(void)
{
    struct timespec before, after;
    char marker;
    int status;
    assert(pipe(completion) == 0);
    clock_gettime(CLOCK_MONOTONIC, &before);
    desktop_launch_startup_apps();
    desktop_launch_startup_apps();
    desktop_reap_startup_worker();
    clock_gettime(CLOCK_MONOTONIC, &after);
    double elapsed = (after.tv_sec - before.tv_sec) +
                     (after.tv_nsec - before.tv_nsec) / 1e9;
    assert(elapsed < 0.1 && desktop_startup_launched);
    assert(read(completion[0], &marker, 1) == 1 && marker == 'x');
    for (int i = 0; i < 100 && startup_worker_pid; ++i) {
        usleep(1000);
        desktop_reap_startup_worker();
    }
    assert(startup_worker_pid == 0);
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    close(completion[0]);
    close(completion[1]);
    puts("OOBE startup: desktop stays responsive and requests startup only once");
    return 0;
}
