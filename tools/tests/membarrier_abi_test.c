#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <linux/membarrier.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define MB_CHECK(condition) do { if (!(condition)) { \
    printf("membarrier FAIL line=%d errno=%d\n", __LINE__, errno); return 1; \
} } while (0)

static long mb_call(unsigned long cmd, unsigned long flags)
{
    return syscall(SYS_membarrier, cmd, flags, 0);
}

static int membarrier_exec_check(void)
{
    MB_CHECK(mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0) == -1 && errno == EPERM);
    return 0;
}

static void *mb_register_thread(void *unused)
{
    (void)unused;
    if (mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) != MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED)
        return (void *)1;
    return (void *)(uintptr_t)(mb_call(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0) != 0);
}

struct mb_litmus {
    atomic_int epoch, done, x, y, observed;
};

static void *mb_litmus_thread(void *arg)
{
    struct mb_litmus *state = arg;
    for (int iteration = 1; iteration <= 2000; ++iteration) {
        while (atomic_load_explicit(&state->epoch, memory_order_acquire) != iteration) sched_yield();
        atomic_store_explicit(&state->x, 1, memory_order_relaxed);
        atomic_signal_fence(memory_order_seq_cst);
        int observed = atomic_load_explicit(&state->y, memory_order_relaxed);
        atomic_store_explicit(&state->observed, observed, memory_order_relaxed);
        atomic_store_explicit(&state->done, iteration, memory_order_release);
    }
    return NULL;
}

static int membarrier_abi_test(void)
{
    const int required = MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED |
        MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED | MEMBARRIER_CMD_PRIVATE_EXPEDITED |
        MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED | MEMBARRIER_CMD_GET_REGISTRATIONS |
        MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE | MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE;
    long supported = mb_call(MEMBARRIER_CMD_QUERY, 0);
    MB_CHECK(supported >= 0 && (supported & required) == required);
    MB_CHECK(mb_call(MEMBARRIER_CMD_QUERY, 1) == -1 && errno == EINVAL);
    MB_CHECK(mb_call(1UL << 30, 0) == -1 && errno == EINVAL);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0) == -1 && errno == EINVAL);
    MB_CHECK(mb_call(MEMBARRIER_CMD_QUERY | (1UL << 32), 1UL << 32) == supported);
    MB_CHECK(membarrier_exec_check() == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0) == -1 && errno == EPERM);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GLOBAL, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) == MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED);
    pthread_t thread;
    void *result;
    MB_CHECK(pthread_create(&thread, NULL, mb_register_thread, NULL) == 0);
    MB_CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    const int inherited = MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED | MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE;
    MB_CHECK(mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) == inherited);
    MB_CHECK(mb_call(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0) == 0);
    pid_t child = fork();
    MB_CHECK(child >= 0);
    if (!child) {
        if (mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) != inherited) _exit(2);
        if (mb_call(MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, 0) != 0) _exit(3);
        const char *self = (const char *)getauxval(AT_EXECFN);
        execl(self, self, "--membarrier-exec", NULL);
        _exit(4);
    }
    int status;
    MB_CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) == inherited);
    MB_CHECK(mb_call(MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, 0) == 0);
    MB_CHECK(mb_call(MEMBARRIER_CMD_GET_REGISTRATIONS, 0) == (inherited | MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED));
    struct mb_litmus state = {0};
    MB_CHECK(pthread_create(&thread, NULL, mb_litmus_thread, &state) == 0);
    int violations = 0;
    for (int iteration = 1; iteration <= 2000; ++iteration) {
        atomic_store_explicit(&state.x, 0, memory_order_relaxed);
        atomic_store_explicit(&state.y, 0, memory_order_relaxed);
        atomic_store_explicit(&state.epoch, iteration, memory_order_release);
        atomic_store_explicit(&state.y, 1, memory_order_relaxed);
        long barrier = mb_call(MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0);
        int observed = atomic_load_explicit(&state.x, memory_order_relaxed);
        while (atomic_load_explicit(&state.done, memory_order_acquire) != iteration) sched_yield();
        if (barrier != 0 || (!observed && !atomic_load_explicit(&state.observed, memory_order_relaxed))) ++violations;
    }
    MB_CHECK(pthread_join(thread, NULL) == 0);
    MB_CHECK(violations == 0);
    return 0;
}

#ifdef MEMBARRIER_EMBEDDED
static int run_membarrier_abi_test(void) { return membarrier_abi_test(); }
#else
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--membarrier-exec")) return membarrier_exec_check();
    return membarrier_abi_test();
}
#endif
