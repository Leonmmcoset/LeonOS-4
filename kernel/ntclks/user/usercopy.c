#include <ntclks/usercopy.h>

#define USER_LOW 0x0000000000400000ULL
#define USER_HIGH 0x0000000001000000ULL

bool user_range_ok(uint64_t ptr, uint64_t len)
{
    if (ptr < USER_LOW || ptr > USER_HIGH) {
        return false;
    }
    if (len > USER_HIGH - ptr) {
        return false;
    }
    return true;
}

size_t user_strlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && user_range_ok((uint64_t)(uintptr_t)(s + n), 1) && s[n]) {
        ++n;
    }
    return n;
}
