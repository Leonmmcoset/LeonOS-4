#ifndef LEONOS_UAPI_PERMISSIONS_H
#define LEONOS_UAPI_PERMISSIONS_H

#include <stdint.h>
#include <leonos/fs_abi.h>

/* Kernel/middle-layer metadata exchange. Applications use stat/chmod/chown. */
#define LEONOS_AUTH_OP_POSIX_PERMISSIONS 9U
#define LEONOS_PERMISSIONS_GET 1U
#define LEONOS_PERMISSIONS_SET 2U

struct leonos_permissions {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
};

struct leonos_permissions_request {
    uint32_t action;
    struct leonos_permissions value;
    char path[LEONOS_FS_PATH_LEN];
};

#endif
