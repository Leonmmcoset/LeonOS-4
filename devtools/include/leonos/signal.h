#ifndef LEONOS_SIGNAL_H
#define LEONOS_SIGNAL_H

#include <stdint.h>
#include <linux/signal.h>

/* Minimal process-disposition ABI used by the shared POSIX signal wrappers. */
#define LEONOS_SIGNAL_ACTION_GET 1U
#define LEONOS_SIGNAL_ACTION_SET 2U
#define LEONOS_SIGNAL_DISPOSITION_DEFAULT 0U
#define LEONOS_SIGNAL_DISPOSITION_IGNORE 1U

/* Historical source alias. Native frames and records
 * have one owner in UAPI; the former magic/version frame is no longer used. */
#define leonos_linux_sigaction linux_sigaction

void leonos_rt_sigreturn_trampoline(void);

#endif
