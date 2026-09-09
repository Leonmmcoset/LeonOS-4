#ifndef LEONOS_INPUTMD_H
#define LEONOS_INPUTMD_H

#include <leonos/inputm.h>
#include <stdint.h>

enum leonos_imd_msg {
    LEONOS_IMD_MSG_HELLO = 10,
    LEONOS_IMD_MSG_ACK = 11,
    LEONOS_IMD_MSG_REGISTER = 20,
    LEONOS_IMD_MSG_UNREGISTER = 21,
    LEONOS_IMD_MSG_KEY_EVENT = 22,
    LEONOS_IMD_MSG_RESULT = 23,
    LEONOS_IMD_MSG_SUBMIT_KEY = 24,
    LEONOS_IMD_MSG_SET_CONTEXT = 25,
    LEONOS_IMD_MSG_SET_ACTIVE = 26,
    LEONOS_IMD_MSG_LIST = 27,
    LEONOS_IMD_MSG_LIST_ACK = 28,
    LEONOS_IMD_MSG_GET_STATE = 29,
    LEONOS_IMD_MSG_STATE_ACK = 30,
    LEONOS_IMD_MSG_NOTIFY_CONFIG = 31,
};

#define LEONOS_IMD_ROLE_APP 1u
#define LEONOS_IMD_ROLE_PROVIDER 2u

struct leonos_imd_hello {
    uint32_t pid;
    uint32_t role;
};

struct leonos_imd_ack {
    int32_t code;
    uint32_t reserved;
};

struct leonos_imd_list {
    uint32_t uid;
    uint32_t capacity;
};

struct leonos_imd_list_ack {
    uint32_t uid;
    uint32_t count;
    uint32_t reserved;
    /* followed by count * struct leonos_inputm_provider */
};

struct leonos_imd_get_state {
    uint32_t uid;
    uint32_t reserved;
};

#endif
