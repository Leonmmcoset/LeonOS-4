/*
 * LeonOS time interface: declares kernel tick and clock services.
 * Provides monotonic time and timer coordination for scheduling and devices.
 */
#ifndef NTCLKS_TIME_H
#define NTCLKS_TIME_H

#include <ntclks/types.h>

#define NTCLKS_TICK_HZ 100ULL

struct leonos_time_info;

/**
 * @brief Initialize the tick counter and read the RTC wall clock.
 */
void time_init(void);
/**
 * @brief Advance the tick counter, poll USB input, and drive the scheduler.
 */
void time_on_tick(void);
/**
 * @brief Return the number of ticks since boot.
 */
uint64_t time_ticks(void);
/**
 * @brief Return milliseconds elapsed since boot.
 */
uint64_t time_uptime_ms(void);
/**
 * @brief Return microseconds elapsed since boot, interpolated within a PIT tick.
 */
uint64_t time_uptime_us(void);
/**
 * @brief Publish the active PIT divisor for sub-tick uptime interpolation.
 */
void time_set_pit_divisor(uint16_t divisor);
/**
 * @brief Fill info with the current wall-clock time; 0 on success.
 */
int time_wall_clock(struct leonos_time_info *info);
/**
 * @brief Set the wall clock to unix_seconds (Unix epoch); 0 on success.
 */
int time_set_wall_clock(uint64_t unix_seconds);
/**
 * @brief Busy-sleep the current CPU for ms milliseconds.
 */
void time_sleep_ms(uint64_t ms);

#endif
