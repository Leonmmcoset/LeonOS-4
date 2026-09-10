#ifndef LEONOS_UAPI_AUTH_DB_H
#define LEONOS_UAPI_AUTH_DB_H
#include <leonos/auth_user.h>
#include <leonos/layout.h>

#define LEONOS_AUTH_DB_PATH LEONOS_PATH_USERS_DB
#define LEONOS_AUTH_DB_MAGIC 0x41555332U
#define LEONOS_AUTH_HASH_LEN 128U

/* AUS2: NUL-terminated PBKDF2-HMAC-SHA256 strings with a random 128-bit salt. */
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
