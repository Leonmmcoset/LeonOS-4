#include "detection/memory/memory.h"
#include "detection/os/os.h"
#include "detection/processes/processes.h"
#include "detection/uptime/uptime.h"

#include <leonos/system.h>

#include <stdint.h>
#include <string.h>

static char leonos_cpu_name[49];

static void leonos_cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
    uint32_t a = leaf, b, c = subleaf, d;
    __asm__ volatile("cpuid"
                     : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

const char* ffLeonOSCPUName(void)
{
    uint32_t max_leaf = 0;
    uint32_t eax, ebx, ecx, edx;
    memset(leonos_cpu_name, 0, sizeof(leonos_cpu_name));
    leonos_cpuid(0x80000000U, 0, &max_leaf, 0, 0, 0);
    if (max_leaf >= 0x80000004U) {
        uint32_t *words = (uint32_t *)(void *)leonos_cpu_name;
        leonos_cpuid(0x80000002U, 0, &eax, &ebx, &ecx, &edx);
        words[0] = eax; words[1] = ebx; words[2] = ecx; words[3] = edx;
        leonos_cpuid(0x80000003U, 0, &eax, &ebx, &ecx, &edx);
        words[4] = eax; words[5] = ebx; words[6] = ecx; words[7] = edx;
        leonos_cpuid(0x80000004U, 0, &eax, &ebx, &ecx, &edx);
        words[8] = eax; words[9] = ebx; words[10] = ecx; words[11] = edx;
        leonos_cpu_name[48] = '\0';
        size_t end = strlen(leonos_cpu_name);
        while (end > 0 && (leonos_cpu_name[end - 1] == ' ' ||
                           leonos_cpu_name[end - 1] == '\t')) {
            leonos_cpu_name[--end] = '\0';
        }
    }
    if (!leonos_cpu_name[0]) {
        strcpy(leonos_cpu_name, "x86_64 processor");
    }
    return leonos_cpu_name;
}

static FFOSResult os_result;
static bool os_initialized;

static void ensure_os_result(void)
{
    if (os_initialized) {
        return;
    }
    ffStrbufInit(&os_result.name);
    ffStrbufInit(&os_result.prettyName);
    ffStrbufInit(&os_result.id);
    ffStrbufInit(&os_result.idLike);
    ffStrbufInit(&os_result.variant);
    ffStrbufInit(&os_result.variantID);
    ffStrbufInit(&os_result.version);
    ffStrbufInit(&os_result.versionID);
    ffStrbufInit(&os_result.codename);
    ffStrbufInit(&os_result.buildID);
    os_initialized = true;
}

const FFOSResult* ffDetectOS(void)
{
    struct leonos_system_info info = {0};

    ensure_os_result();
    ffStrbufSetS(&os_result.name, "LeonOS");
    ffStrbufSetS(&os_result.id, "leonos");
    if (leonos_system_info(&info) == 0) {
        ffStrbufSetF(&os_result.prettyName, "LeonOS %u.%u.%u",
                      (unsigned)info.version_major, (unsigned)info.version_minor,
                      (unsigned)info.version_patch);
        ffStrbufSetF(&os_result.version, "%u.%u.%u",
                      (unsigned)info.version_major, (unsigned)info.version_minor,
                      (unsigned)info.version_patch);
        ffStrbufSetF(&os_result.buildID, "%u", (unsigned)info.build_number);
    } else {
        ffStrbufSetS(&os_result.prettyName, "LeonOS");
    }
    return &os_result;
}

const char* ffDetectUptime(FFUptimeResult* result)
{
    struct leonos_perf_info perf = {0};
    struct leonos_time_info time = {0};

    if (!result || leonos_perf_info(&perf) < 0) {
        return "LeonOS performance information is unavailable";
    }
    result->uptime = perf.uptime_ms;
    if (leonos_time_info(&time) == 0 && time.valid && time.unix_seconds >= perf.uptime_ms / 1000U) {
        result->bootTime = time.unix_seconds * 1000U - perf.uptime_ms;
    } else {
        result->bootTime = 0;
    }
    return nullptr;
}

const char* ffDetectMemory(FFMemoryResult* result)
{
    struct leonos_perf_info perf = {0};

    if (!result || leonos_perf_info(&perf) < 0) {
        return "LeonOS performance information is unavailable";
    }
    result->bytesTotal = perf.total_memory_kib * 1024U;
    result->bytesUsed = perf.total_memory_kib > perf.free_memory_kib
        ? (perf.total_memory_kib - perf.free_memory_kib) * 1024U
        : 0;
    return nullptr;
}

const char* ffDetectProcesses(uint32_t* result)
{
    struct leonos_perf_info perf = {0};

    if (!result || leonos_perf_info(&perf) < 0) {
        return "LeonOS performance information is unavailable";
    }
    *result = perf.task_count;
    return nullptr;
}
