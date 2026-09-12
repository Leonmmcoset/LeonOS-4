#ifndef LEONOS_SESSIOND_H
#define LEONOS_SESSIOND_H

#include <leonos/startup.h>
#include <stdint.h>

enum leonos_sessiond_msg {
    LEONOS_SESSIOND_MSG_HELLO = 10,
    LEONOS_SESSIOND_MSG_ACK = 11,
    LEONOS_SESSIOND_MSG_REQUEST = 20,
    LEONOS_SESSIOND_MSG_REQUEST_STATUS = 21,
    LEONOS_SESSIOND_MSG_DIALOG_GET = 22,
    LEONOS_SESSIOND_MSG_DIALOG = 23,
    LEONOS_SESSIOND_MSG_DIALOG_RESOLVE = 24,
    LEONOS_SESSIOND_MSG_LIST = 25,
    LEONOS_SESSIOND_MSG_SET_ENABLED = 26,
    LEONOS_SESSIOND_MSG_REMOVE = 27,
    LEONOS_SESSIOND_MSG_LAUNCH_CURRENT = 28,
};

struct leonos_sessiond_hello {
    uint32_t pid;
    uint32_t uid;
    uint32_t reserved;
};

struct leonos_sessiond_ack {
    int32_t code;
    uint32_t value;
};

struct leonos_sessiond_list_ack {
    uint32_t uid;
    uint32_t count;
    uint32_t reserved;
    /* followed by count * struct leonos_startup_entry */
};

#endif
