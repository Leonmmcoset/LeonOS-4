#include "detection/memory/memory.h"
#include "detection/os/os.h"
#include "detection/processes/processes.h"
#include "detection/uptime/uptime.h"

#include <leonos/system.h>

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
