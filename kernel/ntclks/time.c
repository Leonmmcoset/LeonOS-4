#include <ntclks/time.h>
#include <ntclks/sched.h>

static volatile uint64_t ticks;

void time_init(void)
{
    ticks = 0;
}

void time_on_tick(void)
{
    ++ticks;
    sched_on_tick();
}

uint64_t time_ticks(void)
{
    return ticks;
}

uint64_t time_uptime_ms(void)
{
    return (ticks * 1000ULL) / NTCLKS_TICK_HZ;
}

void time_sleep_ms(uint64_t ms)
{
    uint64_t delta = (ms * NTCLKS_TICK_HZ + 999ULL) / 1000ULL;
    if (delta == 0) {
        delta = 1;
    }
    uint64_t end = ticks + delta;
    while (ticks < end) {
        __asm__ volatile("sti; hlt; cli");
    }
}
