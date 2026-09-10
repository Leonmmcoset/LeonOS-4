#ifndef NTCLKS_TEXT_STREAM_H
#define NTCLKS_TEXT_STREAM_H
#include <ntclks/types.h>
/* Sequential emitters support arbitrarily short proc/sysfs reads without a
 * fixed-size whole-file buffer. The caller supplies synchronization. */
struct text_stream {
    uint64_t position, offset;
    char *buffer;
    uint32_t capacity, written;
};
/** @brief Emit bytes, copying only the requested interval (binary-safe). */
static inline void text_bytes(struct text_stream *s, const void *bytes, uint32_t length)
{
    const char *p = bytes;
    for (uint32_t i = 0; i < length; ++i, ++s->position)
        if (s->position >= s->offset && s->written < s->capacity)
            s->buffer[s->written++] = p[i];
}
/** @brief Emit a NUL-terminated kernel string excluding its terminator. */
static inline void text_string(struct text_stream *s, const char *text)
{
    uint32_t n = 0;
    while (text[n])
        ++n;
    text_bytes(s, text, n);
}
/** @brief Emit unsigned decimal without libc or allocations. */
static inline void text_unsigned(struct text_stream *s, uint64_t value)
{
    char digits[20];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        text_bytes(s, &digits[--n], 1);
}
/** @brief Emit a signed decimal including INT64_MIN. */
static inline void text_signed(struct text_stream *s, int64_t value)
{
    if (value < 0) {
        text_string(s, "-");
        text_unsigned(s, 0 - (uint64_t)value);
    } else
        text_unsigned(s, (uint64_t)value);
}
/** @brief Emit fixed-width lower-case hexadecimal (at most sixteen digits). */
static inline void text_hex(struct text_stream *s, uint64_t value, uint32_t digits)
{
    static const char hex[] = "0123456789abcdef";
    while (digits) {
        char c = hex[(value >> (--digits * 4)) & 15];
        text_bytes(s, &c, 1);
    }
}
#endif
