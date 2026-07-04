#ifndef LEONOS_SYSTEM_H
#define LEONOS_SYSTEM_H

#include <stdint.h>

#define LEONOS_IOCTL_SYSTEM_INFO 0x4c535953UL
#define LEONOS_IOCTL_PERF_INFO 0x4c504552UL
#define LEONOS_IOCTL_TIME_INFO 0x4c54494dUL

#define LEONOS_SYSTEM_NAME_LEN 32U
#define LEONOS_SYSTEM_VERSION_LEN 32U
#define LEONOS_SYSTEM_TIME_LEN 32U
#define LEONOS_SYSTEM_COPYRIGHT_LEN 96U

struct leonos_system_info {
    char kernel_name[LEONOS_SYSTEM_NAME_LEN];
    char kernel_version[LEONOS_SYSTEM_VERSION_LEN];
    char middlelayer_name[LEONOS_SYSTEM_NAME_LEN];
    char build_time[LEONOS_SYSTEM_TIME_LEN];
    char copyright[LEONOS_SYSTEM_COPYRIGHT_LEN];
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t build_number;
    uint32_t copyright_year;
};

struct leonos_perf_info {
    uint64_t uptime_ms;
    uint64_t total_memory_kib;
    uint64_t free_memory_kib;
    uint64_t busy_ticks;
    uint64_t idle_ticks;
    uint32_t task_count;
    uint32_t running_tasks;
    uint32_t ready_tasks;
    uint32_t sleeping_tasks;
};

struct leonos_time_info {
    uint64_t unix_seconds;
    uint64_t uptime_ms;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint32_t valid;
    uint32_t reserved;
};

int leonos_system_info(struct leonos_system_info *info);
int leonos_perf_info(struct leonos_perf_info *info);
int leonos_time_info(struct leonos_time_info *info);
int leonos_system_reboot(void);
int leonos_system_shutdown(void);

#endif
