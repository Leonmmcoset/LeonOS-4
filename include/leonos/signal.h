#ifndef LEONOS_SIGNAL_H
#define LEONOS_SIGNAL_H

#include <stdint.h>

/* Minimal process-disposition ABI used by the shared POSIX signal wrappers. */
#define LEONOS_SIGNAL_IOCTL_ACTION 0x4c534947UL
#define LEONOS_SIGNAL_ACTION_GET 1U
#define LEONOS_SIGNAL_ACTION_SET 2U
#define LEONOS_SIGNAL_DISPOSITION_DEFAULT 0U
#define LEONOS_SIGNAL_DISPOSITION_IGNORE 1U

struct leonos_signal_action {
    uint32_t operation;
    uint32_t signal_number;
    uint32_t disposition;
    uint32_t previous_disposition;
};

/* Linux-compatible rt_sigaction payload used by the freestanding ABI.
 * Handler invocation is mediated by libleonos at syscall return boundaries,
 * so a pending signal never requires the kernel to trust a writable user
 * stack frame. */
struct leonos_rt_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t mask;
    uint64_t restorer;
};

#define LEONOS_SIGMASK_SET 0
#define LEONOS_SIGMASK_BLOCK 1
#define LEONOS_SIGMASK_UNBLOCK 2

#endif
