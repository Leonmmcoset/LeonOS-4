#ifndef LEONOS_UAPI_LINUX_SIGNAL_H
#define LEONOS_UAPI_LINUX_SIGNAL_H
#include <stdint.h>

#define LINUX_NSIG 65
#define LINUX_SA_NOCLDSTOP 0x00000001u
#define LINUX_SA_NOCLDWAIT 0x00000002u
#define LINUX_SA_SIGINFO   0x00000004u
#define LINUX_SA_RESTORER  0x04000000u
#define LINUX_SA_ONSTACK   0x08000000u
#define LINUX_SA_RESTART   0x10000000u
#define LINUX_SA_NODEFER   0x40000000u
#define LINUX_SA_RESETHAND 0x80000000u

struct linux_sigaction {
    uint64_t handler, flags, restorer, mask;
};

struct linux_sigaltstack {
    uint64_t sp;
    int32_t flags;
    uint32_t padding;
    uint64_t size;
};

/* Native x86-64 sigcontext and kernel ucontext, Linux v6.12. */
struct linux_sigcontext {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp;
    uint64_t rip, rflags;
    uint16_t cs, gs, fs, ss;
    uint64_t error, vector, oldmask, cr2, fpstate;
    uint64_t reserved[8];
};

struct linux_ucontext {
    uint64_t flags, link;
    struct linux_sigaltstack stack;
    struct linux_sigcontext context;
    uint64_t mask;
};

struct linux_siginfo {
    int32_t signo, error, code, padding;
    union {
        struct { int32_t pid; uint32_t uid; } sender;
        uint64_t address;
        unsigned char payload[112];
    } fields;
};

struct linux_rt_sigframe {
    uint64_t restorer;
    struct linux_ucontext uc;
    struct linux_siginfo info;
};

_Static_assert(sizeof(struct linux_sigcontext) == 256, "x86-64 sigcontext size");
_Static_assert(sizeof(struct linux_ucontext) == 304, "x86-64 kernel ucontext size");
_Static_assert(sizeof(struct linux_siginfo) == 128, "Linux siginfo size");
_Static_assert(sizeof(struct linux_rt_sigframe) == 440, "x86-64 rt_sigframe size");
#endif
