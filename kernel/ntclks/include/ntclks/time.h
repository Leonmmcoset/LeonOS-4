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
 * @brief Coordinates the time init operation.
 */
void time_init(void);
/**
 * @brief Coordinates the time on tick operation.
 */
void time_on_tick(void);
/**
 * @brief Coordinates the time ticks operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t time_ticks(void);
/**
 * @brief Coordinates the time uptime ms operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t time_uptime_ms(void);
/**
 * @brief Coordinates the time wall clock operation.
 * @param info Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int time_wall_clock(struct leonos_time_info *info);
/**
 * @brief Coordinates the time set wall clock operation.
 * @param unix_seconds Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int time_set_wall_clock(uint64_t unix_seconds);
/**
 * @brief Coordinates the time sleep ms operation.
 * @param ms Input or output value used by this operation.
 */
void time_sleep_ms(uint64_t ms);

#endif
