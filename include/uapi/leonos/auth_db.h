#ifndef LEONOS_UAPI_AUTH_DB_H
#define LEONOS_UAPI_AUTH_DB_H
#include <leonos/auth_user.h>

#define LEONOS_AUTH_DB_PATH "/system/config/users.db"
#define LEONOS_AUTH_DB_MAGIC 0x41555331U
#define LEONOS_AUTH_HASH_LEN 16U

/* AUS1 disk layout, also used to migrate ownership of pre-POSIX home files. */
struct leonos_auth_record {
    struct leonos_user_info user;
    uint8_t password_hash[LEONOS_AUTH_HASH_LEN];
};
struct leonos_auth_database {
    uint32_t magic;
    uint32_t count;
    struct leonos_auth_record users[LEONOS_AUTH_MAX_USERS];
};
#endif
