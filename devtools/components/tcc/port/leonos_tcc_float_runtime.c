/*
 * Compiler support shared by the TCC process and its generated programs.
 *
 * Picolibc's strtold() converts a 128-bit intermediate through these
 * compiler-rt entry points.  Keep them separate from both the compiler-host
 * shim and the generated-program POSIX compatibility stubs: their ABI is a
 * property of the x86_64 LeonOS target, not of either process role.
 */

#include <stdint.h>

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
