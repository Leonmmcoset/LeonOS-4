#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <linux/sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CLONE3_EMBEDDED
#define CHECK(condition) do { if (!(condition)) { \
    printf("[clone3] FAIL line=%d %s errno=%d\n", __LINE__, #condition, errno); \
    return 1; } } while (0)
#endif

static void clone3_handler(int sig) { (void)sig; }

/* The child records its registers and exits without using its parent's TLS. */
static long clone3_child_stack(struct clone_args *args, uint64_t *observed)
{
    register long result __asm__("rax") = SYS_clone3;
    register struct clone_args *input __asm__("rdi") = args;
    register long size __asm__("rsi") = sizeof(*args);
    __asm__ volatile("syscall; test %%rax, %%rax; jnz 1f; "
        "mov %%rsp, (%%rdx); mov $0x1003, %%edi; lea 8(%%rdx), %%rsi; "
        "mov $158, %%eax; syscall; mov $186, %%eax; syscall; mov %%rax, 16(%%rdx); "
        "xor %%edi, %%edi; mov $60, %%eax; syscall; ud2; 1:"
        : "+a"(result), "+D"(input), "+S"(size)
        : "d"(observed) : "rcx", "r11", "memory", "cc");
    return result;
}

static int thread_clone3(void)
{
    _Static_assert(sizeof(struct clone_args) == 88, "Linux clone_args size");
    struct { struct clone_args args; uint64_t tail; } input = {0};
    struct clone_args *args = &input.args;
    CHECK(syscall(SYS_clone3, NULL, 4097) == -1 && errno == E2BIG);
    CHECK(syscall(SYS_clone3, NULL, 63) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_clone3, (void *)1, sizeof(*args)) == -1 && errno == EFAULT);
    input.tail = 1;
    CHECK(syscall(SYS_clone3, args, sizeof(input)) == -1 && errno == E2BIG);
    input.tail = 0;
    args->exit_signal = SIGCHLD;
    const uint64_t invalid_flags[] = {SIGCHLD, CLONE_DETACHED, 1ULL << 63,
        CLONE_THREAD, CLONE_PARENT, CLONE_SIGHAND | CLONE_CLEAR_SIGHAND};
    for (unsigned i = 0; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++i) {
        args->flags = invalid_flags[i];
        CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    }
    args->flags = 0;
    args->exit_signal = (1ULL << 32) | SIGCHLD;
    CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    args->exit_signal = 65;
    CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    args->exit_signal = SIGCHLD;
    args->stack_size = 4096;
    CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    args->stack = UINT64_MAX - 2047;
    CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    args->stack = args->stack_size = 0;
    args->set_tid_size = 1;
    CHECK(syscall(SYS_clone3, args, sizeof(*args)) == -1 && errno == EINVAL);
    args->set_tid_size = 0;
    const size_t sizes[] = {CLONE_ARGS_SIZE_VER0, CLONE_ARGS_SIZE_VER1,
                            CLONE_ARGS_SIZE_VER2, sizeof(input)};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        long child = syscall(SYS_clone3, args, sizes[i]);
        CHECK(child >= 0);
        if (!child) _exit(23);
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 23);
    }
    struct sigaction action = {.sa_handler = clone3_handler}, previous, ignored;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    action.sa_handler = SIG_IGN;
    CHECK(sigaction(SIGUSR2, &action, &ignored) == 0);
    args->flags = CLONE_CLEAR_SIGHAND;
    long child = syscall(SYS_clone3, args, sizeof(*args));
    CHECK(child >= 0);
    if (!child) {
        struct sigaction first, second;
        int ret = sigaction(SIGUSR1, NULL, &first) || sigaction(SIGUSR2, NULL, &second);
        _exit(ret || first.sa_handler != SIG_DFL || second.sa_handler != SIG_IGN);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(sigaction(SIGUSR1, NULL, &action) == 0 && action.sa_handler == clone3_handler);
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0 && sigaction(SIGUSR2, &ignored, NULL) == 0);
    char *stack = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    uint64_t observed[3] = {0};
    uint32_t parent_tid = 0, child_tid = 0xabcdef;
    *args = (struct clone_args){.flags = CLONE_VM | CLONE_SETTLS | CLONE_PARENT_SETTID |
        CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID, .exit_signal = SIGCHLD,
        .parent_tid = (uintptr_t)&parent_tid, .child_tid = (uintptr_t)&child_tid,
        .stack = (uintptr_t)stack, .stack_size = 16384, .tls = 0x100000000ULL};
    child = clone3_child_stack(args, observed);
    CHECK(child > 0);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(parent_tid == (uint32_t)child && child_tid == 0);
    CHECK(observed[0] == (uintptr_t)stack + 16384 && observed[1] == args->tls && observed[2] == (uint64_t)child);
    CHECK(munmap(stack, 16384) == 0);
    return 0;
}

#ifndef CLONE3_EMBEDDED
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int result = thread_clone3();
    printf("[clone3] %s\n", result ? "FAIL" : "PASS");
    return result;
}
#endif
