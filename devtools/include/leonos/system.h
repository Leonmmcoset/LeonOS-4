#ifndef LEONOS_SYSTEM_H
#define LEONOS_SYSTEM_H

#include <leonos/net.h>
#include <leonos/kernel_debug.h>
#include <stdint.h>

#define LEONOS_TASK_AFFINITY_GET 0U
#define LEONOS_TASK_AFFINITY_SET 1U

#define LEONOS_SYSTEM_NAME_LEN 32U
#define LEONOS_SYSTEM_VERSION_LEN 32U
#define LEONOS_SYSTEM_TIME_LEN 32U
#define LEONOS_SYSTEM_COPYRIGHT_LEN 96U
#define LEONOS_SYSTEM_ARCH_LEN 16U
#define LEONOS_MACHINE_IDENTITY_VERSION 1U
#define LEONOS_MACHINE_IDENTITY_SOURCE_LEN 32U
#define LEONOS_MACHINE_IDENTITY_UUID_LEN 37U
#define LEONOS_MACHINE_IDENTITY_VENDOR_LEN 48U

#define LEONOS_MACHINE_IDENTITY_FLAG_PLATFORM_UUID 0x00000001U
#define LEONOS_MACHINE_IDENTITY_FLAG_BOOT_DISK_GUID 0x00000002U
#define LEONOS_MACHINE_IDENTITY_FLAG_BOOT_PARTITION_GUID 0x00000004U
#define LEONOS_PERF_MAX_CPUS 64U

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
    char architecture[LEONOS_SYSTEM_ARCH_LEN];
};

struct leonos_perf_cpu_info {
    uint64_t busy_ticks;
    uint64_t idle_ticks;
    uint32_t online;
    uint32_t apic_id;
    uint32_t current_pid;
    uint32_t runqueue_length;
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
    uint32_t cpu_count;
    uint32_t reserved;
    uint64_t sample_tick;
    uint32_t online_cpu_count;
    uint32_t reserved2;
    struct leonos_perf_cpu_info cpus[LEONOS_PERF_MAX_CPUS];
};

struct leonos_task_affinity {
    uint32_t pid;
    uint32_t operation;
    uint64_t mask;
    uint64_t allowed_mask;
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

struct leonos_time_sync {
    uint32_t timeout_ms;
    uint32_t status;
    uint32_t server_ip;
    uint32_t valid;
    uint64_t unix_seconds;
    char server[LEONOS_NET_HOSTNAME_LEN];
};

struct leonos_machine_identity {
    uint32_t version;
    uint32_t flags;
    char source[LEONOS_MACHINE_IDENTITY_SOURCE_LEN];
    char platform_uuid[LEONOS_MACHINE_IDENTITY_UUID_LEN];
    char boot_disk_guid[LEONOS_MACHINE_IDENTITY_UUID_LEN];
    char boot_partition_guid[LEONOS_MACHINE_IDENTITY_UUID_LEN];
    char firmware_vendor[LEONOS_MACHINE_IDENTITY_VENDOR_LEN];
    uint32_t firmware_revision;
    uint32_t reserved;
};

int leonos_system_info(struct leonos_system_info *info);
int leonos_perf_info(struct leonos_perf_info *info);
int leonos_task_affinity_get(uint32_t pid, uint64_t *mask);
int leonos_task_affinity_set(uint32_t pid, uint64_t mask);
int leonos_time_info(struct leonos_time_info *info);
int leonos_time_ntp_sync(uint32_t timeout_ms, struct leonos_time_sync *result);
int leonos_machine_identity(struct leonos_machine_identity *identity);
int leonos_system_reboot(void);
int leonos_system_shutdown(void);

#endif
