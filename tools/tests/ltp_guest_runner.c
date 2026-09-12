#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    static const char *tests[] = {"getcwd01", "fcntl01", "fstat02", "mprotect01", "chmod01", "fchmod01", "chown01",
        "pthread_create_1-1", "pthread_join_1-1", "pthread_mutex_lock_1-1", "pthread_cond_wait_1-1",
        "pthread_cancel_1-1", "pthread_key_create_1-1", "pthread_barrier_wait_1-1",
        "pthread_rwlock_rdlock_1-1", "pthread_once_1-1", "pthread_mutex_timedlock_1-1",
        "pthread_cond_timedwait_1-1", "pthread_mutex_trylock_1-1", "sem_timedwait_1-1", "pthread_spin_lock_1-1"};
    unsigned failed = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        char path[128];
        snprintf(path, sizeof(path), "/usr/lib/leonos/tests/%s.elf", tests[i]);
        printf("[ltp-musl] BEGIN %s\n", tests[i]);
        pid_t child = fork();
        if (child == 0) {
            char *args[] = {path, NULL};
            char *env[] = {"PATH=/bin:/sbin:/usr/bin:/usr/sbin", "TMPDIR=/tmp", NULL};
            execve(path, args, env);
            printf("[ltp-musl] exec %s errno=%d\n", tests[i], errno);
            _exit(127);
        }
        int status = 0;
        pid_t result;
        struct timespec started, now;
        int expired = 0;
        if (clock_gettime(CLOCK_MONOTONIC, &started)) return 1;
        for (;;) {
            result = child < 0 ? -1 : waitpid(child, &status, WNOHANG);
            if (result == child || (result < 0 && errno != EINTR)) break;
            if (clock_gettime(CLOCK_MONOTONIC, &now)) return 1;
            if (now.tv_sec - started.tv_sec >= 30) {
                printf("[ltp-musl] TIMEOUT %s after 30 seconds\n", tests[i]);
                expired = 1;
                if (kill(child, SIGKILL)) return 1;
                do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
                break;
            }
            struct timespec delay = {0, 10000000};
            nanosleep(&delay, NULL);
        }
        if (result != child || child < 0) {
            printf("[ltp-musl] runner failure %s errno=%d\n", tests[i], errno);
            return 1;
        }
        int code = expired ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        printf("[ltp-musl] END %s code=%d wait_status=%d\n", tests[i], code, status);
        failed += code != 0;
    }
    printf("[ltp-musl] DONE failures=%u\n", failed);
    return failed != 0;
}
