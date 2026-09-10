#ifndef LEONOS_UAPI_LINUX_SIGNAL_H
#define LEONOS_UAPI_LINUX_SIGNAL_H
#include <stdint.h>

#define LINUX_NSIG 65
#define LINUX_SIGRTMIN 32
#define LINUX_SI_USER 0
#define LINUX_SI_KERNEL 128
#define LINUX_SI_QUEUE (-1)
#define LINUX_SI_TIMER (-2)
#define LINUX_SI_TKILL (-6)
#define LINUX_SEGV_MAPERR 1
#define LINUX_SEGV_ACCERR 2
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
        struct { int32_t pid; uint32_t uid; uint64_t value; } realtime;
        struct { int32_t id, overrun; uint64_t value; int32_t sys_private; } timer;
        struct { int64_t band; int32_t fd; } poll;
        struct { uint64_t address; uint16_t address_lsb; } fault;
        struct { int32_t pid; uint32_t uid; int32_t status, padding; int64_t utime, stime; } child;
        struct { uint64_t call_address; int32_t syscall; uint32_t arch; } sys;
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
_Static_assert(__builtin_offsetof(struct linux_siginfo, fields.realtime.value) == 24,
               "x86-64 queued signal value offset");
_Static_assert(sizeof(struct linux_rt_sigframe) == 440, "x86-64 rt_sigframe size");
#endif
