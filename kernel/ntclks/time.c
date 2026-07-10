#include <leonos/system.h>
#include <ntclks/console.h>
#include <ntclks/sched.h>
#include <ntclks/time.h>

#include "arch/x86_64/port.h"

#define CMOS_ADDRESS 0x70u
#define CMOS_DATA 0x71u
#define CMOS_NMI_DISABLE 0x80u
#define CMOS_SECONDS 0x00u
#define CMOS_MINUTES 0x02u
#define CMOS_HOURS 0x04u
#define CMOS_DAY 0x07u
#define CMOS_MONTH 0x08u
#define CMOS_YEAR 0x09u
#define CMOS_STATUS_A 0x0au
#define CMOS_STATUS_B 0x0bu
#define CMOS_CENTURY 0x32u

static volatile uint64_t ticks;
static uint64_t wall_unix_seconds;
static uint64_t wall_subticks;
static uint8_t wall_clock_valid;

static void cmos_wait(void)
{
    x86_64_outb(0, 0x80);
}

static uint8_t cmos_read(uint8_t reg)
{
    x86_64_outb((uint8_t)(CMOS_NMI_DISABLE | reg), CMOS_ADDRESS);
    cmos_wait();
    return x86_64_inb(CMOS_DATA);
}

static int rtc_update_in_progress(void)
{
    return (cmos_read(CMOS_STATUS_A) & 0x80u) != 0;
}

static uint32_t bcd_to_binary(uint8_t value)
{
    return (uint32_t)((value & 0x0fu) + ((value >> 4) * 10u));
}

static int is_leap_year(uint32_t year)
{
    return (year % 4u == 0 && year % 100u != 0) || (year % 400u == 0);
}

static uint32_t days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return days[month - 1];
}

static int rtc_datetime_valid(uint32_t year, uint32_t month, uint32_t day,
                              uint32_t hour, uint32_t minute, uint32_t second)
{
    return year >= 1970 && year <= 9999 &&
           month >= 1 && month <= 12 &&
           day >= 1 && day <= days_in_month(year, month) &&
           hour < 24 && minute < 60 && second < 60;
}

static uint64_t datetime_to_unix(uint32_t year, uint32_t month, uint32_t day,
                                 uint32_t hour, uint32_t minute, uint32_t second)
{
    uint64_t days = 0;
    for (uint32_t y = 1970; y < year; ++y) {
        days += is_leap_year(y) ? 366ULL : 365ULL;
    }
    for (uint32_t m = 1; m < month; ++m) {
        days += days_in_month(year, m);
    }
    days += day - 1;
    return days * 86400ULL + (uint64_t)hour * 3600ULL +
           (uint64_t)minute * 60ULL + second;
}

static void unix_to_datetime(uint64_t unix_seconds, struct leonos_time_info *info)
{
    uint64_t days = unix_seconds / 86400ULL;
    uint32_t rem = (uint32_t)(unix_seconds % 86400ULL);
    uint32_t year = 1970;
    while (1) {
        uint32_t year_days = is_leap_year(year) ? 366u : 365u;
        if (days < year_days) {
            break;
        }
        days -= year_days;
        ++year;
    }
    uint32_t month = 1;
    while (1) {
        uint32_t month_days = days_in_month(year, month);
        if (days < month_days) {
            break;
        }
        days -= month_days;
        ++month;
    }
    info->year = year;
    info->month = month;
    info->day = (uint32_t)days + 1;
    info->hour = rem / 3600u;
    rem %= 3600u;
    info->minute = rem / 60u;
    info->second = rem % 60u;
}

static int rtc_read_unix_seconds(uint64_t *out)
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour_raw;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t status_b;
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if (!rtc_update_in_progress()) {
            break;
        }
    }
    second = cmos_read(CMOS_SECONDS);
    minute = cmos_read(CMOS_MINUTES);
    hour_raw = cmos_read(CMOS_HOURS);
    day = cmos_read(CMOS_DAY);
    month = cmos_read(CMOS_MONTH);
    year = cmos_read(CMOS_YEAR);
    century = cmos_read(CMOS_CENTURY);
    status_b = cmos_read(CMOS_STATUS_B);

    uint8_t pm = hour_raw & 0x80u;
    uint32_t hour = hour_raw & 0x7fu;
    uint32_t full_year;
    if ((status_b & 0x04u) == 0) {
        second = (uint8_t)bcd_to_binary(second);
        minute = (uint8_t)bcd_to_binary(minute);
        hour = bcd_to_binary((uint8_t)hour);
        day = (uint8_t)bcd_to_binary(day);
        month = (uint8_t)bcd_to_binary(month);
        year = (uint8_t)bcd_to_binary(year);
        century = (uint8_t)bcd_to_binary(century);
    }
    if ((status_b & 0x02u) == 0) {
        if (pm && hour < 12) {
            hour += 12;
        } else if (!pm && hour == 12) {
            hour = 0;
        }
    }
    if (century) {
        full_year = (uint32_t)century * 100u + year;
    } else {
        full_year = year >= 70 ? 1900u + year : 2000u + year;
    }
    if (!rtc_datetime_valid(full_year, month, day, hour, minute, second)) {
        return -1;
    }
    *out = datetime_to_unix(full_year, month, day, hour, minute, second);
    return 0;
}

void time_init(void)
{
    ticks = 0;
    wall_unix_seconds = 0;
    wall_subticks = 0;
    wall_clock_valid = 0;
    if (rtc_read_unix_seconds(&wall_unix_seconds) == 0) {
        struct leonos_time_info info;
        wall_clock_valid = 1;
        unix_to_datetime(wall_unix_seconds, &info);
        console_printf("[ntclks] rtc wall clock %u-%u-%u %u:%u:%u\n",
                       info.year, info.month, info.day,
                       info.hour, info.minute, info.second);
    } else {
        console_printf("[ntclks] rtc wall clock unavailable\n");
    }
}

void time_on_tick(void)
{
    ++ticks;
    if (wall_clock_valid) {
        ++wall_subticks;
        if (wall_subticks >= NTCLKS_TICK_HZ) {
            wall_subticks = 0;
            ++wall_unix_seconds;
        }
    }
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

int time_wall_clock(struct leonos_time_info *info)
{
    if (!info || !wall_clock_valid) {
        return -1;
    }
    info->unix_seconds = wall_unix_seconds;
    info->uptime_ms = time_uptime_ms();
    info->valid = 1;
    info->reserved = 0;
    unix_to_datetime(wall_unix_seconds, info);
    return 0;
}

int time_set_wall_clock(uint64_t unix_seconds)
{
    struct leonos_time_info info;
    if (unix_seconds < 1ULL) {
        return -1;
    }
    unix_to_datetime(unix_seconds, &info);
    if (!rtc_datetime_valid(info.year, info.month, info.day, info.hour,
                            info.minute, info.second)) {
        return -1;
    }
    wall_unix_seconds = unix_seconds;
    wall_subticks = 0;
    wall_clock_valid = 1;
    console_printf("[ntclks] wall clock set %u-%u-%u %u:%u:%u\n",
                   info.year, info.month, info.day, info.hour,
                   info.minute, info.second);
    return 0;
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
