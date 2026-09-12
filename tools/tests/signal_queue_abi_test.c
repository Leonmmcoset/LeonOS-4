#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SIGQ_CHECK(expr) do { if (!(expr)) { \
    printf("sigqueue ABI: line %d: %s errno=%d\n", __LINE__, #expr, errno); return 1; \
} } while (0)

struct sigq_thread_state { pthread_barrier_t barrier; pid_t tid; };
static void *sigq_thread(void *data)
{
    struct sigq_thread_state *state = data;
    state->tid = syscall(SYS_gettid);
    pthread_barrier_wait(&state->barrier);
    pthread_barrier_wait(&state->barrier);
    return NULL;
}

static int signal_queue_syscalls(void)
{
    const int sig = SIGRTMIN + 3;
    uint64_t mask = (1ULL << (sig - 1)) | (1ULL << (SIGUSR2 - 1)), oldmask;
    const struct timespec zero = {0};
    siginfo_t in = {0}, out;
    pid_t pid = getpid(), tid = syscall(SYS_gettid);
    SIGQ_CHECK(syscall(SYS_rt_sigprocmask, SIG_BLOCK, &mask, &oldmask, 8) == 0);
    in.si_code = SI_QUEUE;
    in.si_pid = 123;
    in.si_uid = 456;
    in.si_errno = 7;
    in.si_signo = SIGUSR1; /* The syscall's signal argument is authoritative. */
    in.si_value.sival_ptr = (void *)(uintptr_t)0x123456789abcdef0ULL;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, sig, &in) == 0);
    in.si_value.sival_int = 22;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, sig, &in) == 0);
    in.si_value.sival_int = 33;
    SIGQ_CHECK(syscall(SYS_rt_tgsigqueueinfo, pid, tid, sig, &in) == 0);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == sig && out.si_value.sival_int == 33);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == sig);
    SIGQ_CHECK(out.si_signo == sig && out.si_code == SI_QUEUE && out.si_errno == 7 &&
        out.si_pid == 123 && out.si_uid == 456 && (uintptr_t)out.si_value.sival_ptr == 0x123456789abcdef0ULL);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == sig && out.si_value.sival_int == 22);
    for (unsigned i = 48; i < sizeof(out); ++i) SIGQ_CHECK(((unsigned char *)&out)[i] == 0);
    in.si_value.sival_int = 44;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, SIGUSR2, &in) == 0);
    in.si_value.sival_int = 55;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, SIGUSR2, &in) == 0);
    struct timespec bad = {.tv_nsec = -1};
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &bad, 8) == -1 && errno == EINVAL);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == SIGUSR2 && out.si_value.sival_int == 44);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == -1 && errno == EAGAIN);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, NULL, NULL, NULL, 128) == -1 && errno == EINVAL);
    SIGQ_CHECK(syscall(SYS_rt_tgsigqueueinfo, 0, 0, sig, NULL) == -1 && errno == EFAULT);
    SIGQ_CHECK(syscall(SYS_rt_tgsigqueueinfo, 0, 0, sig, &in) == -1 && errno == EINVAL);
    SIGQ_CHECK(syscall(SYS_rt_tgsigqueueinfo, pid + 1, tid, sig, &in) == -1 && errno == ESRCH);
    in.si_code = SI_TKILL;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, 0, 0, &in) == -1 && errno == EPERM);
    in.si_code = -99;
    ((unsigned char *)&in)[127] = 1;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, 0, &in) == -1 && errno == E2BIG);
    ((unsigned char *)&in)[127] = 0;
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, 0, &in) == 0);
    size_t pagesize = sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, 2 * pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    SIGQ_CHECK(guard != MAP_FAILED && mprotect(guard + pagesize, pagesize, PROT_NONE) == 0);
    in.si_code = SI_QUEUE;
    memcpy(guard + pagesize - 48, &in, 48);
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, 0, guard + pagesize - 48) == 0);
    in.si_code = -99;
    memcpy(guard + pagesize - 48, &in, 48);
    SIGQ_CHECK(syscall(SYS_rt_sigqueueinfo, pid, 0, guard + pagesize - 48) == -1 && errno == EFAULT);
    SIGQ_CHECK(munmap(guard, 2 * pagesize) == 0);
    in.si_code = SI_QUEUE;
    struct rlimit saved, lowered;
    SIGQ_CHECK(syscall(SYS_getrlimit, RLIMIT_SIGPENDING, &saved) == 0);
    lowered = saved;
    lowered.rlim_cur = 0;
    SIGQ_CHECK(syscall(SYS_setrlimit, RLIMIT_SIGPENDING, &lowered) == 0);
    long full = syscall(SYS_rt_sigqueueinfo, pid, sig, &in);
    int full_errno = errno;
    SIGQ_CHECK(syscall(SYS_setrlimit, RLIMIT_SIGPENDING, &saved) == 0);
    SIGQ_CHECK(full == -1 && full_errno == EAGAIN);
    struct sigq_thread_state state;
    pthread_t thread;
    SIGQ_CHECK(pthread_barrier_init(&state.barrier, NULL, 2) == 0);
    SIGQ_CHECK(pthread_create(&thread, NULL, sigq_thread, &state) == 0);
    pthread_barrier_wait(&state.barrier);
    in.si_value.sival_int = 66;
    long via_tid = syscall(SYS_rt_sigqueueinfo, state.tid, sig, &in);
    pthread_barrier_wait(&state.barrier);
    SIGQ_CHECK(pthread_join(thread, NULL) == 0 && pthread_barrier_destroy(&state.barrier) == 0);
    SIGQ_CHECK(via_tid == 0);
    SIGQ_CHECK(syscall(SYS_rt_sigtimedwait, &mask, &out, &zero, 8) == sig && out.si_value.sival_int == 66);
    SIGQ_CHECK(syscall(SYS_rt_sigprocmask, SIG_SETMASK, &oldmask, NULL, 8) == 0);
    puts("PASS native sigqueue ABI: raw send/wait, FIFO, TID targeting, payload, limits, user-copy errors");
    return 0;
}

#ifndef SIGNAL_QUEUE_EMBEDDED
int main(void) { return signal_queue_syscalls(); }
#endif
