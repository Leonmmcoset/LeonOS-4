/*
 * Lua's standard time helpers backed by the shared LeonOS system-time ABI.
 */

#include <time.h>

#include <leonos/system.h>

time_t time(time_t *seconds)
{
    struct leonos_time_info info = {0};
    time_t value = (time_t)-1;

    if (leonos_time_info(&info) == 0 && info.valid) {
        value = (time_t)info.unix_seconds;
    }
    if (seconds) {
        *seconds = value;
    }
    return value;
}

clock_t clock(void)
{
    struct leonos_time_info info = {0};
    uint64_t whole_seconds;
    uint64_t remaining_milliseconds;
    uint64_t ticks;

    if (leonos_time_info(&info) != 0) {
        return (clock_t)-1;
    }
    whole_seconds = info.uptime_ms / 1000U;
    remaining_milliseconds = info.uptime_ms % 1000U;
    ticks = whole_seconds * (uint64_t)CLOCKS_PER_SEC
          + (remaining_milliseconds * (uint64_t)CLOCKS_PER_SEC) / 1000U;
    return (clock_t)ticks;
}
