#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "../../userland/nano/regex_range.h"

int main(void)
{
    const char *patterns[] = {"a(b)?", "^a", "b$", "", "a.*b", "[ab]+"};
    const char *lines[] = {"ababa", "ab", "", "xxab"};
    for (size_t p = 0; p < sizeof(patterns) / sizeof(*patterns); ++p)
        for (size_t l = 0; l < sizeof(lines) / sizeof(*lines); ++l) {
            regex_t regex;
            assert(!regcomp(&regex, patterns[p], REG_EXTENDED));
            for (size_t start = 0; start <= strlen(lines[l]); ++start) {
                regmatch_t actual[10] = {{start, strlen(lines[l])}};
                regmatch_t expected[10] = {{start, strlen(lines[l])}};
                int ref = regexec(&regex, lines[l], 10, expected, REG_STARTEND);
                int result = leonos_nano_regex_suffix(&regex, lines[l], 10, actual);
                assert(ref == result);
                if (!result)
                    for (size_t i = 0; i < 10; ++i) {
                        assert(actual[i].rm_so == expected[i].rm_so);
                        assert(actual[i].rm_eo == expected[i].rm_eo);
                    }
            }
            regfree(&regex);
        }
    puts("PASS nano suffix regex against host REG_STARTEND: anchors, offsets, optional groups, empty matches");
}
