#ifndef LEONOS_BUSYBOX_STDLIB_H
#define LEONOS_BUSYBOX_STDLIB_H

/* Picolibc exports three-argument itoa/utoa. BusyBox owns the traditional
 * one-argument helpers, so keep the declarations in separate namespaces. */
#define itoa leonos_picolibc_itoa
#define utoa leonos_picolibc_utoa
#include_next <stdlib.h>
#undef itoa
#undef utoa

int clearenv(void);

#endif
