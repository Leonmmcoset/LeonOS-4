#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef FUTEX2_EMBEDDED
#define CHECK(condition) do { if (!(condition)) { \
    printf("[futex2] FAIL line=%d %s errno=%d\n", __LINE__, #condition, errno); \
    return 1; } } while (0)
#endif

static const unsigned futex2_private = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE;
static uint32_t futex2_source, futex2_target;
static volatile sig_atomic_t futex2_signals;

static struct timespec futex2_limit(long milliseconds)
{
    struct timespec result;
    clock_gettime(CLOCK_MONOTONIC, &result);
    result.tv_sec += milliseconds / 1000;
    result.tv_nsec += milliseconds % 1000 * 1000000;
    if (result.tv_nsec >= 1000000000) {
        ++result.tv_sec;
        result.tv_nsec -= 1000000000;
    }
    return result;
}

static void *futex2_park(void *mask)
{
    struct timespec limit = futex2_limit(3000);
    long ret = syscall(SYS_futex_wait, &futex2_source, 0, (uintptr_t)mask,
                       futex2_private, &limit, CLOCK_MONOTONIC);
    return (void *)(intptr_t)(ret ? errno : 0);
}

static void futex2_signal_handler(int sig)
{
    if (sig == SIGUSR1) ++futex2_signals;
}

static void futex2_alarm_handler(int sig)
{
    if (sig == SIGALRM) ++futex2_signals;
}

static void *futex2_interrupt(void *target)
{
    struct futex_waitv pair[2] = {
        {.uaddr = (uintptr_t)&futex2_source, .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE},
        {.uaddr = (uintptr_t)&futex2_source, .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE},
    };
    for (unsigned i = 0; i < 1000; ++i) {
        long parked = syscall(SYS_futex_requeue, pair, 0, 0, 1);
        if (parked < 0) return (void *)(intptr_t)errno;
        if (parked) return (void *)(intptr_t)pthread_kill(*(pthread_t *)target, SIGUSR1);
        usleep(1000);
    }
    return (void *)(intptr_t)ETIMEDOUT;
}

static int thread_futex2(void)
{
    _Static_assert(sizeof(struct futex_waitv) == 24, "Linux futex_waitv size");
    struct futex_waitv invalid_waitv = {
        .uaddr = (uintptr_t)&futex2_source,
        .val = 1,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };
    CHECK(syscall(SYS_futex_waitv, &invalid_waitv, 0, 0, NULL, CLOCK_MONOTONIC) == -1 &&
          errno == EINVAL);
    struct timespec waitv_past = {0, 0};
    futex2_source = 1;
    CHECK(syscall(SYS_futex_waitv, &invalid_waitv, 1, 0, &waitv_past, CLOCK_MONOTONIC) == -1 &&
          errno == ETIMEDOUT);
    const unsigned invalid_flags[] = {0, 1, 3, FUTEX2_SIZE_U32 | FUTEX2_NUMA,
                                      FUTEX2_SIZE_U32 | FUTEX_CLOCK_REALTIME};
    uint32_t word = 1;
    for (unsigned i = 0; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++i) {
        CHECK(syscall(SYS_futex_wake, &word, 1, 0, invalid_flags[i]) == -1 && errno == EINVAL);
        CHECK(syscall(SYS_futex_wait, &word, 0, 1, invalid_flags[i], NULL, 0) == -1 && errno == EINVAL);
    }
    CHECK(syscall(SYS_futex_wake, &word, 1, 1, futex2_private | (1ULL << 32)) == 0);
    CHECK(syscall(SYS_futex_wait, &word, 0, UINT32_MAX, futex2_private, NULL, 99) == -1 && errno == EAGAIN);
    CHECK(syscall(SYS_futex_wait, &word, 1ULL << 32, 1, futex2_private, NULL, 0) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_wake, &word, 1ULL << 32, 0, futex2_private) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_wake, &word, 0, 0, futex2_private) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_wake, (void *)4, 1, 0, FUTEX2_SIZE_U32) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_futex_wait, (char *)&word + 1, 1, 1, futex2_private, NULL, 0) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_requeue, NULL, 0, 0, 0) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_requeue, (void *)1, 0, 0, 0) == -1 && errno == EFAULT);
    struct timespec past = {0}, invalid = {0, 1000000000};
    CHECK(syscall(SYS_futex_wait, &word, 1, 1, futex2_private, &invalid, CLOCK_MONOTONIC) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_wait, &word, 1, 1, futex2_private, &past, 99) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_wait, &word, 1, 1, futex2_private, (void *)1, CLOCK_MONOTONIC) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_futex_wait, &word, 1, 1, futex2_private, &past,
                  (1ULL << 32) | CLOCK_MONOTONIC) == -1 && errno == ETIMEDOUT);
    CHECK(syscall(SYS_futex_wait, &word, 1, 1, futex2_private, &past, CLOCK_REALTIME) == -1 && errno == ETIMEDOUT);
    CHECK(!past.tv_sec && !past.tv_nsec);
    struct futex_waitv pair[2] = {
        {.val = 1, .uaddr = (uintptr_t)&futex2_source, .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE},
        {.uaddr = (uintptr_t)&futex2_target, .flags = FUTEX2_SIZE_U32},
    };
    CHECK(syscall(SYS_futex_requeue, pair, 0, 0, 1) == -1 && errno == EAGAIN);
    pair[0].val = 0;
    pair[1].__reserved = 1;
    CHECK(syscall(SYS_futex_requeue, pair, 0, 0, 1) == -1 && errno == EINVAL);
    pair[1].__reserved = 0;
    CHECK(syscall(SYS_futex_requeue, pair, 0, -1, 1) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_futex_requeue, pair, 0, 0, -1) == -1 && errno == EINVAL);
    pthread_t workers[2];
    CHECK(pthread_create(&workers[0], NULL, futex2_park, (void *)1) == 0);
    CHECK(pthread_create(&workers[1], NULL, futex2_park, (void *)2) == 0);
    long moved = 0;
    for (unsigned i = 0; i < 1000 && moved != 2; ++i) {
        long count = syscall(SYS_futex_requeue, pair, 1ULL << 32, 1ULL << 32,
                             (1ULL << 32) | (2 - moved));
        CHECK(count >= 0);
        moved += count;
        if (moved != 2) usleep(1000);
    }
    CHECK(moved == 2);
    CHECK(syscall(SYS_futex_wake, &futex2_source, UINT32_MAX, 2, futex2_private) == 0);
    CHECK(syscall(SYS_futex_wake, &futex2_target, UINT32_MAX, 2, futex2_private) == 0);
    CHECK(syscall(SYS_futex_wake, &futex2_target, UINT32_MAX, 0, FUTEX2_SIZE_U32) == 0);
    CHECK(syscall(SYS_futex_wake, &futex2_target, 4, 2, FUTEX2_SIZE_U32) == 0);
    CHECK(syscall(SYS_futex_wake, &futex2_target, 1, (1ULL << 32) | 1, FUTEX2_SIZE_U32) == 1);
    CHECK(syscall(SYS_futex_wake, &futex2_target, 1, 1, FUTEX2_SIZE_U32) == 0);
    CHECK(syscall(SYS_futex_wake, &futex2_target, 2, -1, FUTEX2_SIZE_U32) == 1);
    for (unsigned i = 0; i < 2; ++i) {
        void *result = (void *)1;
        CHECK(pthread_join(workers[i], &result) == 0 && result == NULL);
    }
    struct sigaction action = {.sa_handler = futex2_signal_handler}, saved;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, NULL, &saved) == 0);
    pthread_t self = pthread_self();
    for (unsigned restart = 0; restart < 2; ++restart) {
        action.sa_flags = restart ? SA_RESTART : 0;
        CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
        futex2_signals = 0;
        CHECK(pthread_create(&workers[0], NULL, futex2_interrupt, &self) == 0);
        struct timespec limit = futex2_limit(300), before = limit;
        long ret = syscall(SYS_futex_wait, &futex2_source, 0, 1, futex2_private, &limit, CLOCK_MONOTONIC);
        int error = errno;
        void *result = (void *)1;
        CHECK(pthread_join(workers[0], &result) == 0 && result == NULL);
        CHECK(ret == -1 && error == (restart ? ETIMEDOUT : EINTR));
        CHECK(futex2_signals == 1);
        CHECK(limit.tv_sec == before.tv_sec && limit.tv_nsec == before.tv_nsec);
        CHECK(syscall(SYS_futex_wake, &futex2_source, UINT32_MAX, 1, futex2_private) == 0);
    }
    CHECK(sigaction(SIGUSR1, &saved, NULL) == 0);

    /* futex_waitv must follow the same signal interruption contract as the
     * single-word futex wait: EINTR without SA_RESTART and a resumed wait
     * with SA_RESTART.  This catches omissions in the kernel restart table. */
    struct sigaction alarm_action = {.sa_handler = futex2_alarm_handler}, alarm_saved;
    sigemptyset(&alarm_action.sa_mask);
    CHECK(sigaction(SIGALRM, NULL, &alarm_saved) == 0);
    struct futex_waitv waitv = {
        .uaddr = (uintptr_t)&futex2_source,
        .val = 0,
        .flags = FUTEX2_SIZE_U32 | FUTEX2_PRIVATE,
    };
    futex2_source = 0;
    for (unsigned restart = 0; restart < 2; ++restart) {
        alarm_action.sa_flags = restart ? SA_RESTART : 0;
        CHECK(sigaction(SIGALRM, &alarm_action, NULL) == 0);
        futex2_signals = 0;
        alarm(1);
        struct timespec limit = futex2_limit(2500);
        long ret = syscall(SYS_futex_waitv, &waitv, 1, 0, &limit, CLOCK_MONOTONIC);
        int error = errno;
        CHECK(ret == -1 && error == (restart ? ETIMEDOUT : EINTR));
        CHECK(futex2_signals == 1);
    }
    alarm(0);
    CHECK(sigaction(SIGALRM, &alarm_saved, NULL) == 0);
    return 0;
}

#ifndef FUTEX2_EMBEDDED
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int result = thread_futex2();
    printf("[futex2] %s\n", result ? "FAIL" : "PASS");
    return result;
}
#endif
