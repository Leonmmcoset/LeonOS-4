#include <ntclks/version.h>

#include <generated/build_info.h>

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
};

const struct leonos_system_info *ntclks_system_info(void)
{
    return &system_info;
}
