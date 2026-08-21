#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/*
 * TCC uses realpath() only to compare #pragma-once include files.  LeonOS
 * paths have one canonical root-directory form already; returning a separately
 * allocated copy preserves the POSIX ownership contract without depending on
 * Picolibc's host-filesystem implementation.
 */
char *realpath(const char *path, char *resolved_path)
{
    size_t length;
    char *result;
    if (!path || !path[0]) {
        return 0;
    }
    length = strlen(path);
    result = resolved_path ? resolved_path : malloc(length + 1U);
    if (!result) {
        return 0;
    }
    memcpy(result, path, length + 1U);
    return result;
}

/* -bench is useful, but LeonOS does not expose a POSIX wall-clock syscall. */
int gettimeofday(struct timeval *time_value, void *timezone)
{
    (void)timezone;
    if (time_value) {
        time_value->tv_sec = 0;
        time_value->tv_usec = 0;
    }
    return 0;
}

/*
 * Picolibc's strtold() converts its 128-bit intermediate through these two
 * compiler-rt entry points.  TinyCC uses strtold() while tokenising decimal
 * literals, but LeonOS does not link a host compiler runtime into user
 * programs.  Build the conversion out of two native 64-bit x87 conversions
 * instead, so the compiler remains self-contained.  The kernel saves and
 * restores the x87/SSE task state for user processes.
 */
typedef unsigned __int128 leonos_tcc_uint128_t;
typedef __int128 leonos_tcc_int128_t;

long double __floatuntixf(leonos_tcc_uint128_t value)
{
    const long double two_to_64 = 18446744073709551616.0L;
    uint64_t high = (uint64_t)(value >> 64U);
    uint64_t low = (uint64_t)value;

    return (long double)high * two_to_64 + (long double)low;
}

long double __floattixf(leonos_tcc_int128_t value)
{
    if (value < 0) {
        /* Avoid signed overflow for the smallest negative 128-bit value. */
        return -__floatuntixf((leonos_tcc_uint128_t)(-(value + 1)) + 1U);
    }
    return __floatuntixf((leonos_tcc_uint128_t)value);
}
