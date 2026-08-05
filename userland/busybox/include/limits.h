#ifndef LEONOS_BUSYBOX_LIMITS_H
#define LEONOS_BUSYBOX_LIMITS_H

#include_next <limits.h>

#ifndef ULONG_MAX
#define ULONG_MAX (~0UL)
#endif

#ifndef UCHAR_MAX
#define UCHAR_MAX 255U
#endif

/* Picolibc publishes these only when its ISO-C visibility level is C99. */
#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif

#ifndef LLONG_MIN
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX (~0ULL)
#endif

#endif
