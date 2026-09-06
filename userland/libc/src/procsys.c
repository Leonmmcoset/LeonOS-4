/* procfs-backed system/task compatibility exports. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/syscall.h>
#include <leonos/system.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void ps_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t i = 0;
    if (!dst || !capacity) return;
    while (src && src[i] && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int ps_read_file(const char *path, char *buffer, uint32_t capacity)
{
    int fd = open(path, LEONOS_O_RDONLY, 0);
    uint32_t len = 0;
    if (fd < 0) return -1;
    buffer[0] = 0;
    while (len + 1u < capacity) {
        long got = read(fd, buffer + len, capacity - len - 1u);
        if (got < 0) { close(fd); return -1; }
        if (!got) break;
        len += (uint32_t)got;
    }
    close(fd);
    buffer[len] = 0;
    return 0;
}

static uint64_t ps_parse_number(const char *text, uint32_t *position)
{
    uint64_t value = 0;
    if (!text || !position) return 0;
    while (text[*position] == ' ' || text[*position] == '\t') ++(*position);
    while (text[*position] >= '0' && text[*position] <= '9') {
        value = value * 10u + (uint64_t)(text[*position] - '0');
        ++(*position);
    }
    return value;
}

int leonos_system_info(struct leonos_system_info *info)
{
    struct utsname uts;
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    if (uname(&uts) == 0) {
        ps_copy(info->kernel_name, sizeof(info->kernel_name), uts.sysname);
        ps_copy(info->kernel_version, sizeof(info->kernel_version), uts.release);
        ps_copy(info->architecture, sizeof(info->architecture), uts.machine);
    }
    ps_copy(info->middlelayer_name, sizeof(info->middlelayer_name), "osmlayer");
    info->version_major = 4;
    info->version_minor = 0;
    info->version_patch = 0;
    info->copyright_year = 2026;
    return 0;
}

static uint32_t ps_count_proc_tasks(void)
{
    int fd = open("/proc", LEONOS_O_RDONLY, 0);
    struct leonos_dir_entry entry;
    uint32_t count = 0;
    if (fd < 0) return 0;
    while (leonos_readdir(fd, &entry) > 0) {
        if (entry.name[0] >= '0' && entry.name[0] <= '9') ++count;
    }
    close(fd);
    return count;
}

int leonos_perf_info(struct leonos_perf_info *info)
{
    char text[256];
    uint32_t pos = 0;
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    info->uptime_ms = leonos_uptime_ms();
    if (ps_read_file("/proc/meminfo", text, sizeof(text)) == 0) {
        /* Minimal parser: first number is MemTotal, second is MemFree. */
        while (text[pos] && (text[pos] < '0' || text[pos] > '9')) ++pos;
        info->total_memory_kib = ps_parse_number(text, &pos);
        while (text[pos] && (text[pos] < '0' || text[pos] > '9')) ++pos;
        info->free_memory_kib = ps_parse_number(text, &pos);
    }
    info->task_count = ps_count_proc_tasks();
    info->running_tasks = 0;
    info->ready_tasks = 0;
    info->sleeping_tasks = 0;
    info->cpu_count = 1;
    info->online_cpu_count = 1;
    return 0;
}

int leonos_time_info(struct leonos_time_info *info)
{
    struct timeval tv;
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    if (gettimeofday(&tv, 0) == 0) {
        info->unix_seconds = (uint64_t)tv.tv_sec;
        info->valid = 1;
    }
    info->uptime_ms = leonos_uptime_ms();
    return 0;
}

int leonos_time_ntp_sync(uint32_t timeout_ms, struct leonos_time_sync *result)
{
    if (!result) return -1;
    memset(result, 0, sizeof(*result));
    result->timeout_ms = timeout_ms;
    result->status = LEONOS_NET_STATUS_DNS_NO_ANSWER;
    return 0;
}

int leonos_machine_identity(struct leonos_machine_identity *identity)
{
    char id[40];
    if (!identity) return -1;
    memset(identity, 0, sizeof(*identity));
    identity->version = 1;
    ps_copy(identity->source, sizeof(identity->source), "/proc/machine-id");
    if (ps_read_file("/proc/machine-id", id, sizeof(id)) == 0) {
        ps_copy(identity->platform_uuid, sizeof(identity->platform_uuid), id);
    }
    return 0;
}

static int ps_parse_stat(const char *path, struct leonos_task_info *task)
{
    char text[512];
    uint32_t pos = 0;
    if (ps_read_file(path, text, sizeof(text)) < 0) return -1;
    memset(task, 0, sizeof(*task));
    task->pid = (uint32_t)ps_parse_number(text, &pos);
    while (text[pos] && text[pos] != '(') ++pos;
    if (text[pos] == '(') ++pos;
    {
        uint32_t start = pos;
        while (text[pos] && text[pos] != ')') ++pos;
        uint32_t len = pos - start;
        if (len >= sizeof(task->name)) len = sizeof(task->name) - 1u;
        memcpy(task->name, text + start, len);
        task->name[len] = 0;
    }
    while (text[pos] && text[pos] != ')') ++pos;
    if (text[pos] == ')') ++pos;
    task->state = (uint32_t)ps_parse_number(text, &pos);
    task->parent_pid = (uint32_t)ps_parse_number(text, &pos);
    (void)ps_parse_number(text, &pos); /* pgrp */
    (void)ps_parse_number(text, &pos); /* session */
    task->cpu_ticks = ps_parse_number(text, &pos);
    task->uid = (uint32_t)ps_parse_number(text, &pos);
    task->role = (uint32_t)ps_parse_number(text, &pos);
    task->flags = (uint32_t)ps_parse_number(text, &pos);
    task->kind = 1;
    return 0;
}

int leonos_task_snapshot(struct leonos_task_info *tasks, uint32_t capacity,
                         uint64_t *tick)
{
    int fd;
    struct leonos_dir_entry entry;
    uint32_t count = 0;
    if (tick) *tick = leonos_uptime_ms();
    if (!tasks || !capacity) return 0;
    fd = open("/proc", LEONOS_O_RDONLY, 0);
    if (fd < 0) return -1;
    while (leonos_readdir(fd, &entry) > 0 && count < capacity) {
        char path[LEONOS_FS_PATH_LEN];
        if (entry.name[0] < '0' || entry.name[0] > '9') continue;
        {
            uint32_t pos = 0;
            ps_copy(path, sizeof(path), "/proc/");
            while (path[pos]) ++pos;
            ps_copy(path + pos, sizeof(path) - pos, entry.name);
            pos += (uint32_t)strlen(entry.name);
            ps_copy(path + pos, sizeof(path) - pos, "/stat");
        }
        if (ps_parse_stat(path, &tasks[count]) == 0) ++count;
    }
    close(fd);
    return (int)count;
}
