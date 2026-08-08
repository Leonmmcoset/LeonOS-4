/* LeonOS declaration shim for BusyBox applets needing sched_yield(). */
#ifndef LEONOS_BUSYBOX_SCHED_H
#define LEONOS_BUSYBOX_SCHED_H

/*
 * Picolibc exposes this POSIX function only when its thread scheduling
 * feature macros are defined.  LeonOS implements the syscall independently
 * of those optional interfaces, and BusyBox less uses it to avoid spinning
 * while it waits for terminal input.
 */
int sched_yield(void);

#endif
