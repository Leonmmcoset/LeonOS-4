#include <ntclks/types.h>

void *memset(void *dst, int value, size_t len)
{
    unsigned char *p = (unsigned char *)dst;
    while (len--) {
        *p++ = (unsigned char)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

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

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n]) {
        ++n;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

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
