#include <leonos/auth.h>

int leonos_auth_password_valid(const char *password, uint32_t capacity)
{
    uint32_t offset = 0, count = 0;
    if (!password) return 0;
    while (offset < capacity) {
        uint32_t codepoint = (unsigned char)password[offset++];
        if (!codepoint) return count >= LEONOS_AUTH_PASSWORD_MIN_CHARS;
        if (++count > LEONOS_AUTH_PASSWORD_MAX_CHARS) return 0;
        if (codepoint >= 0x80) {
            unsigned remaining;
            uint32_t minimum;
            if (codepoint >= 0xc2 && codepoint <= 0xdf) {
                remaining = 1; minimum = 0x80; codepoint &= 0x1f;
            } else if (codepoint >= 0xe0 && codepoint <= 0xef) {
                remaining = 2; minimum = 0x800; codepoint &= 0x0f;
            } else if (codepoint >= 0xf0 && codepoint <= 0xf4) {
                remaining = 3; minimum = 0x10000; codepoint &= 0x07;
            } else return 0;
            while (remaining--) {
                if (offset >= capacity) return 0;
                unsigned char next = password[offset++];
                if ((next & 0xc0) != 0x80) return 0;
                codepoint = (codepoint << 6) | (next & 0x3f);
            }
            if (codepoint < minimum || codepoint > 0x10ffff ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
        }
        if (codepoint == ' ' || (codepoint >= 9 && codepoint <= 13) ||
            codepoint == 0x85 || codepoint == 0xa0 || codepoint == 0x1680 ||
            (codepoint >= 0x2000 && codepoint <= 0x200a) || codepoint == 0x2028 ||
            codepoint == 0x2029 || codepoint == 0x202f || codepoint == 0x205f ||
            codepoint == 0x3000) return 0;
    }
    return 0;
}
