#include <ntclks/usercopy.h>
#include <ntclks/paging.h>

bool user_range_ok(uint64_t ptr, uint64_t len)
{
    if (ptr < NTCLKS_USER_BASE || ptr > NTCLKS_USER_TOP) {
        return false;
    }
    if (len > NTCLKS_USER_TOP - ptr) {
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
