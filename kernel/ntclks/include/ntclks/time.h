/*
 * LeonOS time interface: declares kernel tick and clock services.
 * Provides monotonic time and timer coordination for scheduling and devices.
 */
#ifndef NTCLKS_TIME_H
#define NTCLKS_TIME_H

#include <ntclks/types.h>

#define NTCLKS_TICK_HZ 100ULL

struct leonos_time_info;

void time_init(void);
void time_on_tick(void);
uint64_t time_ticks(void);
uint64_t time_uptime_ms(void);
int time_wall_clock(struct leonos_time_info *info);
int time_set_wall_clock(uint64_t unix_seconds);
void time_sleep_ms(uint64_t ms);

#endif
