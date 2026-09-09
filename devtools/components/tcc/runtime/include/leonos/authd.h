#ifndef LEONOS_AUTHD_H
#define LEONOS_AUTHD_H

#include <leonos/auth.h>
#include <stdint.h>

enum leonos_authd_msg {
    LEONOS_AUTHD_MSG_HELLO = 10,
    LEONOS_AUTHD_MSG_ACK = 11,
    LEONOS_AUTHD_MSG_STATUS = 20,
    LEONOS_AUTHD_MSG_LIST = 21,
    LEONOS_AUTHD_MSG_LOGIN = 22,
    LEONOS_AUTHD_MSG_ELEVATE = 23,
    LEONOS_AUTHD_MSG_CURRENT = 24,
    LEONOS_AUTHD_MSG_LOGOUT = 25,
    LEONOS_AUTHD_MSG_CREATE = 26,
    LEONOS_AUTHD_MSG_UPDATE = 27,
    LEONOS_AUTHD_MSG_CHANGE_PASSWORD = 28,
};

struct leonos_authd_hello {
    uint32_t pid;
    uint32_t reserved;
};

struct leonos_authd_ack {
    int32_t code;
    uint32_t reserved;
};

struct leonos_authd_list {
    uint32_t include_disabled;
    uint32_t capacity;
};

struct leonos_authd_list_ack {
    uint32_t count;
    uint32_t reserved;
    /* followed by count * struct leonos_user_info */
};

struct leonos_authd_create {
    uint32_t role;
    uint32_t reserved;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
};

struct leonos_authd_update {
    uint32_t uid;
    uint32_t mask;
    uint32_t role;
    uint32_t flags;
};

struct leonos_authd_password {
    uint32_t uid;
    uint32_t reserved;
    char old_password[LEONOS_AUTH_PASSWORD_LEN];
    char new_password[LEONOS_AUTH_PASSWORD_LEN];
};

#endif
