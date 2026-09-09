#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    struct utsname version;
    if (uname(&version) == 0) printf("[linux-reference] kernel=%s\n", version.release);
    if (mount("proc", "/proc", "proc", 0, NULL) ||
        mount("devtmpfs", "/dev", "devtmpfs", 0, NULL)) {
        printf("[linux-reference] mount failed errno=%d\n", errno);
        return 1;
    }
    mkdir("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0, NULL)) {
        printf("[linux-reference] devpts failed errno=%d\n", errno);
        return 1;
    }
    const char *cases[] = {
#ifdef REFERENCE_CASE
        REFERENCE_CASE
#else
        "heap", "pty", "pty_fork", "process_sessions", "stdio_reopen", "threads", "thread_contention", "thread_exit_lifecycle",
        "descriptor_allocation", "descriptor_limits",
        "descriptor_boundaries",
        "thread_affinity",
        "resource_limits", "resource_address_space",
        "thread_tls_control", "thread_tid_registration", "thread_tid_exit",
        "thread_cancellation", "native_signals", "thread_signal_restart",
        "thread_exec", "thread_timed_interrupts", "unix_streams", "unix_rights", "unix_rights_boundaries",
        "unix_rights_cycles", "unix_packets", "unix_waitall", "unix_credentials",
        "unix_close_during_io", "unix_vectors", "unix_timeouts", "thread_futex_ops",
        "unix_lowwater", "thread_process_signals", "thread_alarm",
        "unix_shutdown_poll", "unix_inq", "unix_reset", "poll_interrupts", "poll_files",
        "unix_nonblock_ioctl", "proc_status"
#endif
    };
    int failures = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (child == 0) {
            execl("/probe", "/probe", "--case", cases[i], NULL);
            _exit(127);
        }
        int status;
        if (child < 0 || waitpid(child, &status, 0) != child) {
            printf("[linux-reference] fork/wait failed errno=%d\n", errno);
            ++failures;
            break;
        }
        int result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        printf("[linux-reference] %s exit=%d\n", cases[i], result);
        failures += result != 0;
    }
    printf("[linux-reference] DONE failures=%d\n", failures);
    sync();
    reboot(RB_POWER_OFF);
    for (;;) pause();
}
