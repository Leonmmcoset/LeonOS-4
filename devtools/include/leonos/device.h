#ifndef LEONOS_DEVICE_H
#define LEONOS_DEVICE_H

#include <stdint.h>


#define LEONOS_DEVICE_MAX 24U
#define LEONOS_DEVICE_NAME_LEN 32U
#define LEONOS_DEVICE_STATUS_LEN 32U
#define LEONOS_DEVICE_DETAIL_LEN 96U

#define LEONOS_DEVICE_CLASS_SYSTEM 1U
#define LEONOS_DEVICE_CLASS_INPUT 2U
#define LEONOS_DEVICE_CLASS_DISPLAY 3U
#define LEONOS_DEVICE_CLASS_STORAGE 4U
#define LEONOS_DEVICE_CLASS_SERIAL 5U
#define LEONOS_DEVICE_CLASS_NETWORK 6U
#define LEONOS_DEVICE_CLASS_AUDIO 7U

#define LEONOS_DEVICE_FLAG_PRESENT 0x00000001U
#define LEONOS_DEVICE_FLAG_ACTIVE 0x00000002U
#define LEONOS_DEVICE_FLAG_BOOT 0x00000004U
#define LEONOS_DEVICE_FLAG_REMOVABLE 0x00000008U

struct leonos_device_info {
    uint32_t id;
    uint32_t device_class;
    uint32_t flags;
    uint32_t reserved;
    uint64_t value0;
    uint64_t value1;
    char name[LEONOS_DEVICE_NAME_LEN];
    char status[LEONOS_DEVICE_STATUS_LEN];
    char detail[LEONOS_DEVICE_DETAIL_LEN];
};

struct leonos_device_list {
    uint32_t capacity;
    uint32_t count;
    struct leonos_device_info *devices;
};

int leonos_device_list(struct leonos_device_info *devices,
                       uint32_t capacity, uint32_t *out_count);

#endif
