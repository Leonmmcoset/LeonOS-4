/* Identical Linux ABI cases included by the host reference and musl guest. */
#include <sched.h>
#include <poll.h>
#include <ucontext.h>

#define EDGE_CHECK(c) do { if (!(c)) { \
    printf("[vfork-edge] FAIL line=%d %s errno=%d\n", __LINE__, #c, errno); \
    return 1; } } while (0)

static volatile sig_atomic_t edge_marker, edge_signal_early;
static stack_t edge_alt;
static void *edge_fault_address;
static int edge_fault_code;
static int edge_stack_fault;

static void edge_sleep(long ms)
{
    struct timespec t = {ms / 1000, ms % 1000 * 1000000};
    while (syscall(SYS_nanosleep, &t, &t) < 0 && errno == EINTR) {}
}

static void *edge_stack(void)
{
    void *p = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static int edge_delayed_child(void *arg)
{
    edge_sleep((long)arg);
    edge_marker = 1;
    return 0;
}

static int edge_alt_child(void *arg)
{
    (void)arg;
    stack_t inherited;
    if (syscall(SYS_sigaltstack, NULL, &inherited) ||
        inherited.ss_sp != edge_alt.ss_sp || inherited.ss_size != edge_alt.ss_size ||
        (inherited.ss_flags & SS_DISABLE)) return 3;
    return 0;
}

static int edge_alt_inheritance(void)
{
    void *stack = edge_stack();
    EDGE_CHECK(stack);
    edge_alt = (stack_t){.ss_sp = edge_stack(), .ss_size = 65536};
    EDGE_CHECK(edge_alt.ss_sp && !sigaltstack(&edge_alt, NULL));
    pid_t child = clone(edge_alt_child, (char *)stack + 65536,
                        CLONE_VM | CLONE_VFORK | SIGCHLD, NULL);
    EDGE_CHECK(child > 0);
    int status;
    EDGE_CHECK(waitpid(child, &status, 0) == child);
    EDGE_CHECK(WIFEXITED(status) && !WEXITSTATUS(status));
    stack_t off = {.ss_flags = SS_DISABLE};
    EDGE_CHECK(!sigaltstack(&off, NULL));
    munmap(edge_alt.ss_sp, 65536);
    munmap(stack, 65536);
    return 0;
}

static int edge_stop_continue(void)
{
    void *stack = edge_stack();
    EDGE_CHECK(stack);
    pid_t parent = getpid(), sender = fork();
    EDGE_CHECK(sender >= 0);
    if (!sender) {
        edge_sleep(100);
        kill(parent, SIGSTOP);
        edge_sleep(100);
        kill(parent, SIGCONT);
        _exit(0);
    }
    edge_marker = 0;
    pid_t child = clone(edge_delayed_child, (char *)stack + 65536,
                        CLONE_VM | CLONE_VFORK | SIGCHLD, (void *)500L);
    int early = !edge_marker, status;
    EDGE_CHECK(child > 0 && waitpid(child, &status, 0) == child);
    EDGE_CHECK(waitpid(sender, &status, 0) == sender);
    EDGE_CHECK(!early);
    munmap(stack, 65536);
    return 0;
}

static void edge_timer_handler(int sig)
{
    (void)sig;
    edge_signal_early = edge_marker ? 1 : 2;
}

static int edge_thread_timer(void)
{
    void *stack = edge_stack();
    EDGE_CHECK(stack);
    struct sigaction action = {.sa_handler = edge_timer_handler};
    sigemptyset(&action.sa_mask);
    EDGE_CHECK(!sigaction(SIGUSR1, &action, NULL));
    /* Native sigevent is 64 bytes; the THREAD_ID member starts at offset 16. */
    struct { uint64_t value; int signo, notify, tid; unsigned char pad[44]; } ev =
        {.signo = SIGUSR1, .notify = 4, .tid = (int)syscall(SYS_gettid)};
    int timer;
    EDGE_CHECK(!syscall(SYS_timer_create, CLOCK_MONOTONIC, &ev, &timer));
    struct itimerspec spec = {.it_value = {0, 50000000}};
    EDGE_CHECK(!syscall(SYS_timer_settime, timer, 0, &spec, NULL));
    edge_marker = edge_signal_early = 0;
    pid_t child = clone(edge_delayed_child, (char *)stack + 65536,
                        CLONE_VM | CLONE_VFORK | SIGCHLD, (void *)200L);
    int early = !edge_marker, status;
    EDGE_CHECK(child > 0 && waitpid(child, &status, 0) == child);
    EDGE_CHECK(!syscall(SYS_timer_delete, timer));
    EDGE_CHECK(!early && edge_signal_early == 1);
    munmap(stack, 65536);
    return 0;
}

static int edge_clear_tid(void)
{
    void *stack = edge_stack();
    EDGE_CHECK(stack);
    int tid = 123;
    extern long edge_clone_raw(int (*)(void *), void *, unsigned, void *, int *);
    long child = edge_clone_raw(edge_delayed_child, (char *)stack + 65536,
        CLONE_VM | CLONE_VFORK | CLONE_CHILD_CLEARTID | SIGCHLD, (void *)10L, &tid);
    int cleared = __atomic_load_n(&tid, __ATOMIC_SEQ_CST) == 0, status;
    EDGE_CHECK(child > 0 && waitpid(child, &status, 0) == child);
    EDGE_CHECK(cleared);
    munmap(stack, 65536);
    return 0;
}

/* musl's public clone rejects CHILD_CLEARTID before entering the kernel. */
__asm__(".text\n.type edge_clone_raw,@function\nedge_clone_raw:\n"
        "and $-16,%rsi\nsub $16,%rsi\nmov %rdi,(%rsi)\nmov %rcx,8(%rsi)\n"
        "mov %r8,%r10\nmov %edx,%edi\nxor %edx,%edx\nxor %r8d,%r8d\n"
        "mov $56,%eax\nsyscall\ntest %rax,%rax\njz 1f\nret\n"
        "1: pop %rax\npop %rdi\ncall *%rax\nmov %eax,%edi\nmov $60,%eax\nsyscall\nud2\n"
        ".size edge_clone_raw,.-edge_clone_raw\n");

static int edge_fatal_child(void *argument)
{
    int fd = (int)(intptr_t)argument;
    if (syscall(SYS_write, fd, "S", 1) != 1) return 3;
    edge_sleep(1200);
    if (syscall(SYS_write, fd, "D", 1) != 1) return 4;
    return 0;
}

static int edge_parent_sigterm(void)
{
    int fds[2];
    EDGE_CHECK(!pipe(fds));
    pid_t victim = fork();
    EDGE_CHECK(victim >= 0);
    if (!victim) {
        void *stack = edge_stack();
        if (!stack) _exit(2);
        pid_t child = clone(edge_fatal_child, (char *)stack + 65536,
            CLONE_VM | CLONE_VFORK | SIGCHLD, (void *)(intptr_t)fds[1]);
        _exit(child > 0 ? 3 : 4);
    }
    char marker;
    EDGE_CHECK(read(fds[0], &marker, 1) == 1 && marker == 'S');
    EDGE_CHECK(!kill(victim, SIGTERM));
    int status, reaped = 0;
    for (unsigned i = 0; i < 100; ++i) {
        pid_t result = waitpid(victim, &status, WNOHANG);
        if (result == victim) { reaped = 1; break; }
        edge_sleep(5);
    }
    struct pollfd ready = {.fd = fds[0], .events = POLLIN};
    int before_completion = poll(&ready, 1, 0) == 0;
    if (!reaped) {
        kill(victim, SIGKILL);
        waitpid(victim, &status, 0);
    }
    /* Let the orphan finish using its shared mm before the case exits. */
    EDGE_CHECK(read(fds[0], &marker, 1) == 1 && marker == 'D');
    close(fds[0]);
    close(fds[1]);
    EDGE_CHECK(reaped && before_completion && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    return 0;
}

static void edge_fault_handler(int sig, siginfo_t *info, void *context)
{
    volatile char local;
    uintptr_t sp = (uintptr_t)&local;
    ucontext_t *uc = context;
    int ok = sig == SIGSEGV && info->si_code == edge_fault_code &&
        (edge_stack_fault || info->si_addr == edge_fault_address) &&
        sp >= (uintptr_t)edge_alt.ss_sp && sp < (uintptr_t)edge_alt.ss_sp + edge_alt.ss_size &&
        uc->uc_mcontext.gregs[REG_TRAPNO] == 14 &&
        (uintptr_t)uc->uc_mcontext.gregs[REG_CR2] == (uintptr_t)info->si_addr;
    _exit(ok ? 42 : 43);
}

__attribute__((noinline)) static int edge_overflow(unsigned depth)
{
    volatile char *page = __builtin_alloca(4096);
    for (unsigned i = 0; i < 4096; i += 64) page[i] = (char)depth;
    int result = depth < 512 ? edge_overflow(depth + 1) : 0;
    return result + page[0];
}

static int edge_fault_signals(void)
{
    for (int mapped = 0; mapped < 3; ++mapped) {
        pid_t child = fork();
        EDGE_CHECK(child >= 0);
        if (!child) {
            edge_alt = (stack_t){.ss_sp = edge_stack(), .ss_size = 65536};
            struct sigaction action = {.sa_sigaction = edge_fault_handler,
                .sa_flags = SA_SIGINFO | SA_ONSTACK};
            sigemptyset(&action.sa_mask);
            if (!edge_alt.ss_sp || sigaltstack(&edge_alt, NULL) ||
                sigaction(SIGSEGV, &action, NULL)) _exit(4);
            if (mapped == 2) {
                struct rlimit limit;
                if (getrlimit(RLIMIT_STACK, &limit)) _exit(8);
                limit.rlim_cur = 256 * 1024;
                if (setrlimit(RLIMIT_STACK, &limit)) _exit(9);
                edge_stack_fault = 1;
                edge_fault_code = SEGV_MAPERR;
                (void)edge_overflow(0);
                _exit(10);
            }
            edge_fault_address = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (edge_fault_address == MAP_FAILED) _exit(5);
            edge_fault_code = mapped ? SEGV_ACCERR : SEGV_MAPERR;
            if (!mapped && munmap(edge_fault_address, 4096)) _exit(6);
            *(volatile char *)edge_fault_address = 1;
            _exit(7);
        }
        int status;
        EDGE_CHECK(waitpid(child, &status, 0) == child);
        EDGE_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    }
    return 0;
}

static int vfork_linux_edges(void)
{
    struct { const char *name; int (*run)(void); } cases[] = {
        {"altstack-inheritance", edge_alt_inheritance},
        {"stop-continue", edge_stop_continue},
        {"thread-timer", edge_thread_timer},
        {"clear-child-tid", edge_clear_tid},
        {"parent-sigterm", edge_parent_sigterm},
        {"fault-siginfo-altstack", edge_fault_signals},
    };
    int failures = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pid_t child = fork();
        if (child < 0) { ++failures; continue; }
        if (!child) _exit(cases[i].run());
        int status;
        int failed = waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status);
        printf("[vfork-edge] %s %s\n", cases[i].name, failed ? "FAIL" : "PASS");
        failures += failed;
    }
    return failures;
}
