#ifndef NTCLKS_RANDOM_H
#define NTCLKS_RANDOM_H

#include <ntclks/types.h>

/* Hardware DRBG output only. Failure never substitutes time/PID/counter data. */
int kernel_random_fill(void *buffer, size_t length);

#endif
