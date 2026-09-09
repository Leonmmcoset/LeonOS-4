#ifndef LEONOS_UAPI_AUTH_USER_H
#define LEONOS_UAPI_AUTH_USER_H
#include <stdint.h>

#define LEONOS_AUTH_MAX_USERS 32U
#define LEONOS_AUTH_USERNAME_LEN 32U
#define LEONOS_AUTH_PASSWORD_LEN 64U
#define LEONOS_AUTH_HOME_LEN 96U

struct leonos_user_info {
    uint32_t uid;
    uint32_t role;
    uint32_t flags;
    uint32_t reserved;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
};
#endif
