#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <leonos/system.h>

/* TCC uses realpath() to compare #pragma-once include files. */
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

/* TCC's -bench path needs a clock; use the guest RTC service when available. */
int gettimeofday(struct timeval *time_value, void *timezone)
{
    struct leonos_time_info info = {0};

    (void)timezone;
    if (!time_value || leonos_time_info(&info) != 0 || !info.valid) {
        return -1;
    }
    time_value->tv_sec = (time_t)info.unix_seconds;
    time_value->tv_usec = 0;
    return 0;
}
