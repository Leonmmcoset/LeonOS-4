#ifndef LEONOS_SYSTEM_H
#define LEONOS_SYSTEM_H

#include <stdint.h>

#define LEONOS_IOCTL_SYSTEM_INFO 0x4c535953UL

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

int leonos_system_info(struct leonos_system_info *info);
int leonos_system_reboot(void);
int leonos_system_shutdown(void);

#endif
