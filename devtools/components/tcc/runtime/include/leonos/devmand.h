#ifndef LEONOS_DEVMAND_H
#define LEONOS_DEVMAND_H

#include <leonos/device.h>
#include <leonos/driver.h>
#include <stdint.h>

enum leonos_devmand_msg {
    LEONOS_DEVMAND_MSG_HELLO = 10,
    LEONOS_DEVMAND_MSG_ACK = 11,
    LEONOS_DEVMAND_MSG_DEVICE_LIST = 20,
    LEONOS_DEVMAND_MSG_DRIVER_LIST = 21,
    LEONOS_DEVMAND_MSG_DRIVER_CONTROL = 22,
};

struct leonos_devmand_hello {
    uint32_t pid;
    uint32_t uid;
    uint32_t reserved;
};

struct leonos_devmand_ack {
    int32_t code;
    uint32_t count;
};

struct leonos_devmand_list_request {
    uint32_t capacity;
    uint32_t reserved;
};

#endif
