/*
 * LeonOS freestanding string primitives: implements basic memory operations.
 * Supplies memset, memcpy, memmove, and related routines to kernel code.
 */
#include <ntclks/types.h>

/**
 * @brief Coordinates the memset operation.
 * @param dst Input or output value used by this operation.
 * @param value Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
void *memset(void *dst, int value, size_t len)
{
    unsigned char *p = (unsigned char *)dst;
    while (len--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}

/**
 * @brief Coordinates the memcpy operation.
 * @param dst Input or output value used by this operation.
 * @param src Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

/**
 * @brief Coordinates the memcmp operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int memcmp(const void *a, const void *b, size_t len)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < len; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

/**
 * @brief Coordinates the strlen operation.
 * @param s Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
}

/**
 * @brief Coordinates the strcmp operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * @brief Coordinates the strncmp operation.
 * @param a Input or output value used by this operation.
 * @param b Input or output value used by this operation.
 * @param len Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
int strncmp(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0 || cb == 0) {
            return (int)ca - (int)cb;
        }
    }
    return 0;
}
