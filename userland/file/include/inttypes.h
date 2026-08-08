#ifndef LEONOS_FILE_INTTYPES_H
#define LEONOS_FILE_INTTYPES_H

#include_next <inttypes.h>

/* Clang's freestanding resource header intentionally omits the 64-bit
 * printf aliases for this target.  libmagic uses them when reporting magic
 * values, and the LeonOS x86_64 ABI represents these values as long. */
#ifndef PRId64
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIo64 "lo"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#endif

#endif
