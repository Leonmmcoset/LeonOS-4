#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "vfork_linux_edges.c"

#ifndef SYS_vfork
#define SYS_vfork 58
#endif
#ifndef CLONE_VM
#define CLONE_VM 0x00000100
#define CLONE_SIGHAND 0x00000800
#define CLONE_VFORK 0x00004000
#define CLONE_THREAD 0x00010000
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("[vfork-stack] FAIL line=%d %s errno=%d\n", __LINE__, #condition, errno); \
        return 1; \
    } \
} while (0)

static volatile sig_atomic_t shared_marker;
static volatile sig_atomic_t signal_seen;

static void signal_handler(int sig)
{
    (void)sig;
    signal_seen = 1;
}

__attribute__((noreturn)) static void raw_exit(int code)
{
    register long rax __asm__("rax") = SYS_exit;
    register long rdi __asm__("rdi") = code;
    __asm__ volatile("syscall" : "+a"(rax) : "D"(rdi) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static void raw_nanosleep_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    register long rax __asm__("rax") = SYS_nanosleep;
    register long rdi __asm__("rdi") = (long)&delay;
    register long rsi __asm__("rsi") = 0;
    __asm__ volatile("syscall" : "+a"(rax) : "D"(rdi), "S"(rsi)
                     : "rcx", "r11", "memory", "cc");
}

/*
 * Raw CLONE_VM|CLONE_VFORK|SIGCHLD entry with a real user stack.  Unlike a
 * raw syscall through a libc wrapper, the child starts in child_fn() directly,
 * so the ABI return-address convention is not violated after the stack switch.
 */
__attribute__((noinline))
static long raw_clone_vfork(int (*child_fn)(void *), void *argument, void *stack_top)
{
    register long rax __asm__("rax") = SYS_clone;
    register long rdi __asm__("rdi") = CLONE_VM | CLONE_VFORK | SIGCHLD;
    register void *rsi __asm__("rsi") = stack_top;
    register long rdx __asm__("rdx") = 0;
    register long r10 __asm__("r10") = 0;
    register long r8 __asm__("r8") = 0;
    __asm__ volatile(
        "syscall\n\t"
        "test %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "mov %[arg], %%rdi\n\t"
        "call *%[fn]\n\t"
        "mov $60, %%rax\n\t"
        "xor %%rdi, %%rdi\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10), "r"(r8),
          [arg]"r"(argument), [fn]"r"(child_fn)
        : "rcx", "r11", "memory", "cc");
    return rax;
}

static int rlimit_stack_roundtrip(void)
{
    struct rlimit original, current, value, old;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, NULL, &original) == 0);
    CHECK(original.rlim_max >= original.rlim_cur);
    value = original;
    if (value.rlim_cur > 1024 * 1024) value.rlim_cur = 1024 * 1024;
    CHECK(value.rlim_cur <= value.rlim_max);
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, &value, &old) == 0);
    CHECK(old.rlim_cur == original.rlim_cur && old.rlim_max == original.rlim_max);
    CHECK(syscall(SYS_getrlimit, RLIMIT_STACK, &current) == 0);
    CHECK(current.rlim_cur == value.rlim_cur && current.rlim_max == value.rlim_max);
    CHECK(syscall(SYS_getrlimit, (unsigned)-1, &current) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, NULL, (void *)1) == -1 && errno == EFAULT);
    CHECK(syscall(SYS_prlimit64, -1, RLIMIT_STACK, NULL, &old) == -1 && errno == ESRCH);

    struct rlimit *readonly = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(readonly != MAP_FAILED);
    CHECK(syscall(SYS_getrlimit, RLIMIT_STACK, readonly) == -1 && errno == EFAULT);
    CHECK(munmap(readonly, 4096) == 0);

    value.rlim_cur = value.rlim_max > 0 ? value.rlim_max / 2 : 0;
    CHECK(syscall(SYS_setrlimit, RLIMIT_STACK, &value) == 0);
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, NULL, &current) == 0);
    CHECK(current.rlim_cur == value.rlim_cur);
    value.rlim_cur = value.rlim_max;
    CHECK(syscall(SYS_setrlimit, RLIMIT_STACK, &value) == 0);
    value.rlim_cur = value.rlim_max == 0 ? 1 : value.rlim_max + (value.rlim_max == RLIM_INFINITY ? 0 : 1);
    if (value.rlim_cur > value.rlim_max) {
        CHECK(syscall(SYS_setrlimit, RLIMIT_STACK, &value) == -1 && errno == EINVAL);
    }
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, &original, NULL) == 0);
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, NULL, &current) == 0);
    CHECK(current.rlim_cur == original.rlim_cur && current.rlim_max == original.rlim_max);
    return 0;
}

static void stack_overflow_handler(int sig)
{
    (void)sig;
    _exit(42);
}

__attribute__((noinline)) static int grow_stack(int depth)
{
    volatile char *page = __builtin_alloca(4096);
    for (int offset = 0; offset < 4096; offset += 64) page[offset] = (char)depth;
    int result = depth < 8192 ? grow_stack(depth + 1) : depth;
    return result + page[0] + page[4095];
}

static int stack_growth_limit(void)
{
    struct rlimit original;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, NULL, &original) == 0);
    struct rlimit small = original;
    small.rlim_cur = 256 * 1024;
    if (small.rlim_cur > small.rlim_max) small.rlim_cur = small.rlim_max;
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, &small, NULL) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        stack_t altstack;
        altstack.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (altstack.ss_sp == MAP_FAILED) _exit(3);
        altstack.ss_size = SIGSTKSZ;
        altstack.ss_flags = 0;
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = stack_overflow_handler;
        action.sa_flags = SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        if (sigaltstack(&altstack, NULL) || sigaction(SIGSEGV, &action, NULL)) _exit(4);
        _exit(grow_stack(0) == 0);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK((WIFEXITED(status) && WEXITSTATUS(status) == 42) ||
          (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV));
    CHECK(syscall(SYS_prlimit64, 0, RLIMIT_STACK, &original, NULL) == 0);
    return 0;
}

static int marker_child(void *argument)
{
    (void)argument;
    shared_marker = 1234;
    raw_exit(7);
}

static int vfork_exit_wait(void)
{
    size_t stack_size = 64 * 1024;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    for (int iteration = 0; iteration < 16; ++iteration) {
        shared_marker = 0;
        long child = raw_clone_vfork(marker_child, NULL, (char *)stack + stack_size);
        CHECK(child > 0);
        CHECK(shared_marker == 1234);
        int status = 0;
        CHECK(waitpid((pid_t)child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
    }
    CHECK(munmap(stack, stack_size) == 0);
    return 0;
}

static int killed_child(void *argument)
{
    volatile pid_t *shared_child = argument;
    *shared_child = (pid_t)syscall(SYS_getpid);
    raw_nanosleep_ms(2000);
    raw_exit(0);
}

static int vfork_parent_killed(void)
{
    volatile pid_t *shared_child = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(shared_child != MAP_FAILED);
    *shared_child = 0;
    pid_t victim = fork();
    CHECK(victim >= 0);
    if (!victim) {
        size_t stack_size = 64 * 1024;
        void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stack == MAP_FAILED) _exit(2);
        long child = raw_clone_vfork(killed_child, (void *)shared_child,
                                     (char *)stack + stack_size);
        if (child > 0) _exit(3); /* blocked until the sleeping child exits */
        _exit(4);
    }
    usleep(200000);
    CHECK(kill(victim, SIGKILL) == 0);
    int status = 0;
    CHECK(waitpid(victim, &status, 0) == victim && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    for (unsigned attempt = 0; attempt < 200 && !*shared_child; ++attempt) usleep(1000);
    CHECK(*shared_child > 0);
    CHECK(kill(*shared_child, SIGKILL) == 0);
    CHECK(munmap((void *)shared_child, 4096) == 0);
    return 0;
}

static int delayed_marker_child(void *argument)
{
    (void)argument;
    raw_nanosleep_ms(400);
    shared_marker = 4321;
    raw_exit(9);
}

static int vfork_handled_signal_suppressed(void)
{
    CHECK(signal(SIGUSR1, signal_handler) != SIG_ERR);
    signal_seen = 0;
    shared_marker = 0;
    pid_t self = (pid_t)syscall(SYS_getpid);
    pid_t sender = fork();
    CHECK(sender >= 0);
    if (!sender) {
        raw_nanosleep_ms(100);
        kill(self, SIGUSR1);
        raw_exit(0);
    }
    size_t stack_size = 64 * 1024;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    long child = raw_clone_vfork(delayed_marker_child, NULL, (char *)stack + stack_size);
    CHECK(child > 0);
    /* TASK_KILLABLE ignores a caught non-fatal signal until the child exits. */
    CHECK(shared_marker == 4321);
    CHECK(signal_seen == 1);
    int status = 0;
    CHECK(waitpid((pid_t)child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 9);
    CHECK(waitpid(sender, &status, 0) == sender && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(munmap(stack, stack_size) == 0);
    return 0;
}

static int clone_flag_validation(void)
{
    size_t stack_size = 64 * 1024;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    errno = 0;
    CHECK(syscall(SYS_clone, SIGCHLD | CLONE_THREAD, (char *)stack + stack_size, 0, 0, 0) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(syscall(SYS_clone, SIGCHLD | CLONE_SIGHAND, (char *)stack + stack_size, 0, 0, 0) == -1 && errno == EINVAL);
    CHECK(munmap(stack, stack_size) == 0);
    return 0;
}

static int vfork_exec_main(const char *self);

struct vfork_exec_args {
    int write_fd;
    const char *self;
};

static int exec_child(void *argument)
{
    struct vfork_exec_args *args = argument;
    char marker = 'A';
    if (write(args->write_fd, &marker, 1) != 1) raw_exit(112);
    vfork_exec_main(args->self);
    raw_exit(111);
}

static int vfork_exec_main(const char *self)
{
    char *const argv[] = {(char *)self, (char *)"--vfork-exec-child", NULL};
    char *const envp[] = {NULL};
    execve(self, argv, envp);
    return 111;
}

static int vfork_exec_wait(const char *self)
{
    int pipefd[2];
    CHECK(pipe(pipefd) == 0);
    size_t stack_size = 64 * 1024;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED);
    struct vfork_exec_args args = {.write_fd = pipefd[1], .self = self};
    long child = raw_clone_vfork(exec_child, &args, (char *)stack + stack_size);
    CHECK(child > 0);
    char marker = 0;
    CHECK(read(pipefd[0], &marker, 1) == 1 && marker == 'A');
    int status = 0;
    /* Wake-up happens at exec commit; the execed child is still sleeping. */
    CHECK(waitpid((pid_t)child, &status, WNOHANG) == 0);
    CHECK(waitpid((pid_t)child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    CHECK(munmap(stack, stack_size) == 0);
    return 0;
}

static long raw_vfork_exit5(void)
{
    register long rax __asm__("rax") = SYS_vfork;
    __asm__ volatile(
        "syscall\n\t"
        "test %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "mov $60, %%rax\n\t"     /* child: SYS_exit(5) without a libc return */
        "mov $5, %%rdi\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "+a"(rax) : : "rcx", "r11", "memory", "cc");
    return rax;
}

static int vfork_syscall_exit_wait(void)
{
    long child = raw_vfork_exit5();
    CHECK(child >= 0);
    int status = 0;
    CHECK(waitpid((pid_t)child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 5);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--vfork-exec-child")) {
        raw_nanosleep_ms(2500);
        return 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    int failed = 0;
    failed += vfork_linux_edges();
    failed += rlimit_stack_roundtrip();
    failed += stack_growth_limit();
    failed += clone_flag_validation();
    failed += vfork_exit_wait();
    failed += vfork_syscall_exit_wait();
    failed += vfork_handled_signal_suppressed();
    failed += vfork_parent_killed();
    failed += vfork_exec_wait(argv[0]);
    if (failed) {
        printf("[vfork-stack] FAIL failures=%d\n", failed);
        return 1;
    }
    printf("[vfork-stack] PASS host raw clone/vfork and RLIMIT_STACK\n");
    return 0;
}
