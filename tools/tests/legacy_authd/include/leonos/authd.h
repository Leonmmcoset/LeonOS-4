/* Historical regression fixture only. Never build or stage into LeonOS. */
#ifndef LEONOS_AUTHD_H
#define LEONOS_AUTHD_H

#include <leonos/auth.h>
#include <leonos/fs.h>
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
    LEONOS_AUTHD_MSG_POWER = 29,
    /* Privileged execution. authd verifies the target account, then forks and
     * execs on the caller's behalf: only uid 0 can change identity, so a
     * privileged broker is the one path that needs no kernel change. */
    LEONOS_AUTHD_MSG_RUN = 30,
    LEONOS_AUTHD_MSG_WAIT = 31,
    LEONOS_AUTHD_MSG_SUDO_KILL = 32,
    LEONOS_AUTHD_MSG_SUDO_CHECK = 33,
    LEONOS_AUTHD_MSG_FILEOP = 34,
    /* Verify an administrator password and open the credential window without
     * running anything. Used by callers that only need the authorization (for
     * example an in-process privileged operation). */
    LEONOS_AUTHD_MSG_SUDO_VERIFY = 35,
    LEONOS_AUTHD_MSG_RUN_FD = 36,
    LEONOS_AUTHD_MSG_RUN_SIGNAL = 37,
};

/* Privileged file-operation verbs served by the fixed sudod worker. */
#define LEONOS_FILEOP_LIST 1U
#define LEONOS_FILEOP_MKDIR 2U
#define LEONOS_FILEOP_RENAME 3U
#define LEONOS_FILEOP_UNLINK 4U

/* Removing a directory is unrecoverable, so UNLINK requires this word in
 * path2 when the target is a directory. */
#define LEONOS_FILEOP_CONFIRM "DELETE"

#define LEONOS_AUTHD_RUN_MAX_ARGS 8U
#define LEONOS_AUTHD_RUN_ARG_LEN 192U

/* Set in leonos_authd_run.flags to require an administrator target.
 *
 * This is a statement about the operation, not about the caller's identity:
 * the caller's identity always comes from SO_PEERCRED. `sudo` sets it because
 * it always elevates, so it can never be aimed at an ordinary account; `su`
 * clears it because switching to a normal user is a legitimate operation that
 * still requires that account's own password. */
#define LEONOS_AUTHD_RUN_REQUIRE_ADMIN 0x00000001U
#define LEONOS_AUTHD_RUN_LOGIN 0x00000002U

struct leonos_authd_run {
    char username[LEONOS_AUTH_USERNAME_LEN]; /* empty selects root */
    char password[LEONOS_AUTH_PASSWORD_LEN]; /* empty uses only the cache */
    uint32_t argc;
    uint32_t flags;
    char argv[LEONOS_AUTHD_RUN_MAX_ARGS][LEONOS_AUTHD_RUN_ARG_LEN];
    char cwd[LEONOS_FS_PATH_LEN];
    char term[64];
};

struct leonos_authd_run_fd {
    uint32_t index;
};

struct leonos_authd_run_ack {
    int32_t code;
    uint32_t child_pid;
};

struct leonos_authd_verify {
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
};

struct leonos_authd_wait {
    uint32_t child_pid;
    uint32_t reserved;
};

struct leonos_authd_run_signal {
    uint32_t child_pid;
    uint32_t signal_number;
};

struct leonos_authd_wait_ack {
    int32_t code;   /* 0 exited, -EAGAIN still running, -ESRCH unknown child */
    int32_t status; /* wait4-style status, valid when code == 0 */
};

struct leonos_authd_fileop {
    char username[LEONOS_AUTH_USERNAME_LEN];
    char password[LEONOS_AUTH_PASSWORD_LEN];
    uint32_t op;
    uint32_t reserved;
    char path1[LEONOS_FS_PATH_LEN];
    char path2[LEONOS_FS_PATH_LEN];
};

struct leonos_authd_fileop_ack {
    int32_t code;       /* 0 or a negative errno */
    uint32_t child_pid; /* valid when code == 0 */
    char path[LEONOS_FS_PATH_LEN]; /* the daemon-chosen result file */
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

struct leonos_authd_power {
    uint32_t command;
    uint32_t reserved;
};

int leonos_auth_request_power(uint32_t command);

#endif
