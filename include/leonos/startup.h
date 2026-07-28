#ifndef LEONOS_STARTUP_H
#define LEONOS_STARTUP_H

#include <stdint.h>

#define LEONOS_STARTUP_IOCTL_REQUEST 0x4c535251UL
#define LEONOS_STARTUP_IOCTL_REQUEST_STATUS 0x4c535253UL
#define LEONOS_STARTUP_IOCTL_DIALOG_GET 0x4c534447UL
#define LEONOS_STARTUP_IOCTL_DIALOG_RESOLVE 0x4c534452UL
#define LEONOS_STARTUP_IOCTL_LIST 0x4c534c53UL
#define LEONOS_STARTUP_IOCTL_SET_ENABLED 0x4c535345UL
#define LEONOS_STARTUP_IOCTL_REMOVE 0x4c53524dUL
#define LEONOS_STARTUP_IOCTL_LAUNCH_CURRENT 0x4c53544cUL

#define LEONOS_STARTUP_MAX_ENTRIES 16U
#define LEONOS_STARTUP_MAX_ARGS 7U
#define LEONOS_STARTUP_ARG_LEN 64U

#define LEONOS_STARTUP_STATUS_PENDING 1U
#define LEONOS_STARTUP_STATUS_APPROVED 2U
#define LEONOS_STARTUP_STATUS_DENIED 3U
#define LEONOS_STARTUP_STATUS_DENIED_REMEMBERED 4U
#define LEONOS_STARTUP_STATUS_EXISTS 5U
#define LEONOS_STARTUP_STATUS_CANCELLED 6U
#define LEONOS_STARTUP_STATUS_FAILED 7U

#define LEONOS_STARTUP_DECISION_ALLOW 1U
#define LEONOS_STARTUP_DECISION_DENY 2U
#define LEONOS_STARTUP_DECISION_DENY_REMEMBERED 3U

/* args excludes argv[0]; the executable path is always argv[0]. */
struct leonos_startup_command {
    uint32_t argc;
    uint32_t reserved;
    char path[256];
    char args[LEONOS_STARTUP_MAX_ARGS][LEONOS_STARTUP_ARG_LEN];
};

struct leonos_startup_request {
    struct leonos_startup_command command;
    uint32_t request_id;
    uint32_t status;
};

struct leonos_startup_request_status {
    uint32_t request_id;
    uint32_t status;
};

struct leonos_startup_dialog_request {
    uint32_t request_id;
    uint32_t uid;
    char requester_path[256];
    struct leonos_startup_command command;
};

struct leonos_startup_dialog_resolution {
    uint32_t request_id;
    uint32_t decision;
};

struct leonos_startup_entry {
    uint32_t id;
    uint32_t enabled;
    struct leonos_startup_command command;
};

struct leonos_startup_list {
    uint32_t uid;
    uint32_t capacity;
    uint32_t count;
    uint32_t reserved;
    struct leonos_startup_entry *entries;
};

struct leonos_startup_update {
    uint32_t uid;
    uint32_t entry_id;
    uint32_t enabled;
    uint32_t reserved;
};

int leonos_startup_request(const struct leonos_startup_command *command,
                           uint32_t *out_request_id);
int leonos_startup_request_status(uint32_t request_id, uint32_t *out_status);
int leonos_startup_dialog_get(struct leonos_startup_dialog_request *request);
int leonos_startup_dialog_resolve(uint32_t request_id, uint32_t decision);
int leonos_startup_list(uint32_t uid, struct leonos_startup_entry *entries,
                        uint32_t capacity, uint32_t *out_count);
int leonos_startup_set_enabled(uint32_t uid, uint32_t entry_id, uint32_t enabled);
int leonos_startup_remove(uint32_t uid, uint32_t entry_id);
int leonos_startup_launch_current_user(void);

#endif
