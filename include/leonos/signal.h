#ifndef LEONOS_SIGNAL_H
#define LEONOS_SIGNAL_H

#include <stdint.h>

/* Minimal process-disposition ABI used by the shared POSIX signal wrappers. */
#define LEONOS_SIGNAL_IOCTL_ACTION 0x4c534947UL
#define LEONOS_SIGNAL_ACTION_GET 1U
#define LEONOS_SIGNAL_ACTION_SET 2U
#define LEONOS_SIGNAL_DISPOSITION_DEFAULT 0U
#define LEONOS_SIGNAL_DISPOSITION_IGNORE 1U

/* LeonOS rt_sigframe ABI.  The kernel writes this frame below the interrupted
 * user stack and jumps to the handler with rsp pointing at restorer.  When
 * the handler returns, it returns into leonos_rt_sigreturn_trampoline(); the
 * trampoline issues rt_sigreturn with rsp = frame_base + 8, so the kernel can
 * recover frame_base as rsp - 8. */
#define LEONOS_SIGFRAME_MAGIC 0x4c534746U
#define LEONOS_SIGFRAME_VERSION 1U

/* Keep this register order identical to struct trap_frame in the kernel. */
struct leonos_sigcontext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};

struct leonos_sigframe_info {
    uint32_t signo;
    uint32_t errno_value;
    uint32_t code;
    uint32_t reserved;
    uint64_t address;
};

struct leonos_rt_sigframe {
    uint64_t restorer;
    uint32_t magic;
    uint32_t version;
    uint64_t saved_mask;
    uint32_t signal_number;
    uint32_t flags;
    struct leonos_sigcontext context;
    struct leonos_sigframe_info info;
};

/* Legacy disposition ioctl record, retained only for the pre-rt_sigaction
 * transition path. New consumers must use rt_sigaction. */
struct leonos_signal_action {
    uint32_t operation;
    uint32_t signal_number;
    uint32_t disposition;
    uint32_t previous_disposition;
};

/* Kernel/libc private syscall record.  The restorer field is the user address
 * of the libc trampoline that issues rt_sigreturn. */
struct leonos_linux_sigaction {
    uint64_t handler;
    uint64_t mask;
    uint32_t flags;
    uint32_t reserved;
    uint64_t restorer;
};

void leonos_rt_sigreturn_trampoline(void);

#endif
