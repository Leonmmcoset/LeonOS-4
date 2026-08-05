#ifndef LEONOS_BUSYBOX_SYS_WAIT_H
#define LEONOS_BUSYBOX_SYS_WAIT_H

#include_next <sys/wait.h>

/* LeonOS has no core-dump signal path. */
#ifndef WCOREDUMP
#define WCOREDUMP(status) 0
#endif

#endif
