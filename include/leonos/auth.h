#ifndef LEONOS_AUTH_H
#define LEONOS_AUTH_H

#include <stdint.h>

#define LEONOS_AUTH_MAX_USERS 32U
#define LEONOS_AUTH_USERNAME_LEN 32U
#define LEONOS_AUTH_PASSWORD_LEN 64U
#define LEONOS_AUTH_HOME_LEN 96U

#define LEONOS_AUTH_ROLE_NONE 0U
#define LEONOS_AUTH_ROLE_USER 1U
#define LEONOS_AUTH_ROLE_ADMIN 2U

#define LEONOS_AUTH_USER_DISABLED 0x00000001U

#define LEONOS_AUTH_IOCTL_STATUS 0x4c415553UL
#define LEONOS_AUTH_IOCTL_CURRENT 0x4c415543UL
#define LEONOS_AUTH_IOCTL_LIST_USERS 0x4c41554cUL
#define LEONOS_AUTH_IOCTL_LOGIN 0x4c415547UL
#define LEONOS_AUTH_IOCTL_LOGOUT 0x4c41554fUL
#define LEONOS_AUTH_IOCTL_CREATE_USER 0x4c415541UL
#define LEONOS_AUTH_IOCTL_UPDATE_USER 0x4c415555UL
#define LEONOS_AUTH_IOCTL_CHANGE_PASSWORD 0x4c415550UL

#define LEONOS_AUTH_OP_STATUS 1U
#define LEONOS_AUTH_OP_LIST_USERS 2U
#define LEONOS_AUTH_OP_LOGIN 3U
#define LEONOS_AUTH_OP_CREATE_USER 4U
#define LEONOS_AUTH_OP_UPDATE_USER 5U
#define LEONOS_AUTH_OP_CHANGE_PASSWORD 6U
#define LEONOS_AUTH_OP_AUTHORIZE 7U

#define LEONOS_AUTH_UPDATE_ROLE 0x00000001U
#define LEONOS_AUTH_UPDATE_FLAGS 0x00000002U

#define LEONOS_AUTHZ_READ 1U
#define LEONOS_AUTHZ_WRITE 2U
#define LEONOS_AUTHZ_EXEC 3U
#define LEONOS_AUTHZ_USER_ADMIN 4U
#define LEONOS_AUTHZ_INSTALL 5U
#define LEONOS_AUTHZ_KILL_TASK 6U

struct leonos_user_info {
    uint32_t uid;
    uint32_t role;
    uint32_t flags;
    uint32_t reserved;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
};

struct leonos_auth_status {
    uint32_t user_count;
    uint32_t has_admin;
    uint32_t reserved0;
    uint32_t reserved1;
};

struct leonos_user_list {
    uint32_t actor_uid;
    uint32_t actor_role;
    uint32_t include_disabled;
    uint32_t capacity;
    uint32_t count;
    uint32_t reserved;
    struct leonos_user_info *users;
};

struct leonos_auth_login {
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
    struct leonos_user_info user;
};

struct leonos_auth_create {
    uint32_t actor_uid;
    uint32_t actor_role;
    uint32_t role;
    uint32_t reserved;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
    struct leonos_user_info user;
};

struct leonos_auth_update {
    uint32_t actor_uid;
    uint32_t actor_role;
    uint32_t uid;
    uint32_t mask;
    uint32_t role;
    uint32_t flags;
};

struct leonos_auth_password {
    uint32_t actor_uid;
    uint32_t actor_role;
    uint32_t uid;
    uint32_t reserved;
    char old_password[LEONOS_AUTH_PASSWORD_LEN];
    char new_password[LEONOS_AUTH_PASSWORD_LEN];
};

struct leonos_authz_request {
    uint32_t uid;
    uint32_t role;
    uint32_t session_id;
    uint32_t op;
    uint32_t target_uid;
    uint32_t target_role;
    uint32_t allowed;
    uint32_t reserved;
    char username[LEONOS_AUTH_USERNAME_LEN];
    char home[LEONOS_AUTH_HOME_LEN];
    char path[256];
};

int leonos_auth_status(struct leonos_auth_status *status);
int leonos_auth_current(struct leonos_user_info *user);
int leonos_auth_list_users(struct leonos_user_info *users, uint32_t capacity,
                           uint32_t include_disabled, uint32_t *out_count);
int leonos_auth_login(const char *username, const char *password,
                      struct leonos_user_info *user);
int leonos_auth_logout(void);
int leonos_auth_create_user(const char *username, const char *password,
                            uint32_t role, struct leonos_user_info *user);
int leonos_auth_update_user(uint32_t uid, uint32_t mask, uint32_t role,
                            uint32_t flags);
int leonos_auth_change_password(uint32_t uid, const char *old_password,
                                const char *new_password);

#endif
