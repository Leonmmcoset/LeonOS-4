/*
 * LeonOS has no POSIX signal-delivery or process-accounting ABI. Lua only
 * installs SIGINT handlers around a protected evaluation and only needs a
 * wall-clock timestamp plus elapsed time for its standard OS library. Keep
 * those narrow adapters here rather than linking Picolibc's generic OS
 * fallback library.
 */

#include <signal.h>
#include <time.h>

#include <leonos/system.h>

_sig_func_ptr signal(int signum, _sig_func_ptr handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

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
