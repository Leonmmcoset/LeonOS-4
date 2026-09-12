#ifndef LEONOS_NANO_REGEX_RANGE_H
#define LEONOS_NANO_REGEX_RANGE_H
#include <regex.h>
#include <string.h>

/**
 * @brief Search the suffix of a nano line with musl POSIX regex.
 * Nano's search expression never uses REG_NEWLINE; its range always ends at
 * the string terminator. Keep ^ anchored to the original line and translate
 * subexpression offsets back to that line. This is not a libc REG_STARTEND API.
 * @param regex Compiled nano search expression.
 * @param line NUL-terminated editor line.
 * @param count Number of output matches, at least one.
 * @param matches Input start/end range and output subexpression offsets.
 * @return POSIX regex result, or REG_NOMATCH for an invalid nano range.
 */
static inline int leonos_nano_regex_suffix(const regex_t *regex, const char *line,
                                         size_t count, regmatch_t matches[])
{
    regoff_t start = matches[0].rm_so;
    if (start < 0 || matches[0].rm_eo != (regoff_t)strlen(line) ||
        start > matches[0].rm_eo)
        return REG_NOMATCH;
    int result = regexec(regex, line + start, count, matches, start ? REG_NOTBOL : 0);
    if (!result)
        for (size_t i = 0; i < count; ++i)
            if (matches[i].rm_so >= 0) {
                matches[i].rm_so += start;
                matches[i].rm_eo += start;
            }
    return result;
}
#endif
