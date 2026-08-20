/*
 * LeonOS kernel version module: exposes build and release metadata.
 * Publishes the system information consumed by diagnostics and userland.
 */
#include <ntclks/version.h>

#include <generated/build_info.h>

#if defined(__x86_64__)
#define NTCLKS_ARCHITECTURE "x86_64"
#elif defined(__aarch64__)
#define NTCLKS_ARCHITECTURE "aarch64"
#else
#define NTCLKS_ARCHITECTURE "unknown"
#endif

static const struct leonos_system_info system_info = {
    .kernel_name = LEONOS_KERNEL_NAME,
    .kernel_version = LEONOS_KERNEL_VERSION,
    .middlelayer_name = LEONOS_MIDDLELAYER_NAME,
    .build_time = LEONOS_BUILD_TIME,
    .copyright = LEONOS_COPYRIGHT,
    .version_major = LEONOS_KERNEL_VERSION_MAJOR,
    .version_minor = LEONOS_KERNEL_VERSION_MINOR,
    .version_patch = LEONOS_KERNEL_VERSION_PATCH,
    .build_number = LEONOS_BUILD_NUMBER,
    .copyright_year = LEONOS_COPYRIGHT_YEAR,
    .architecture = NTCLKS_ARCHITECTURE,
};

/**
 * @brief Coordinates the ntclks system info operation.
 * @return Result, status, or value defined by this API.
 */
const struct leonos_system_info *ntclks_system_info(void)
{
    return &system_info;
}
