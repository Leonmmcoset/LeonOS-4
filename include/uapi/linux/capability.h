#ifndef LEONOS_UAPI_LINUX_CAPABILITY_H
#define LEONOS_UAPI_LINUX_CAPABILITY_H

#include <linux/types.h>

#define _LINUX_CAPABILITY_VERSION_1 0x19980330u
#define _LINUX_CAPABILITY_U32S_1 1u
#define _LINUX_CAPABILITY_VERSION_2 0x20071026u
#define _LINUX_CAPABILITY_U32S_2 2u
#define _LINUX_CAPABILITY_VERSION_3 0x20080522u
#define _LINUX_CAPABILITY_U32S_3 2u

typedef struct __user_cap_header_struct {
    __u32 version;
    __s32 pid;
} *cap_user_header_t;

struct __user_cap_data_struct {
    __u32 effective;
    __u32 permitted;
    __u32 inheritable;
};

typedef struct __user_cap_data_struct *cap_user_data_t;

#define CAP_CHOWN 0
#define CAP_DAC_OVERRIDE 1
#define CAP_DAC_READ_SEARCH 2
#define CAP_FOWNER 3
#define CAP_FSETID 4
#define CAP_KILL 5
#define CAP_SETGID 6
#define CAP_SETUID 7
#define CAP_SETPCAP 8
#define CAP_LINUX_IMMUTABLE 9
#define CAP_NET_BIND_SERVICE 10
#define CAP_NET_ADMIN 12
#define CAP_NET_RAW 13
#define CAP_IPC_OWNER 15
#define CAP_SYS_CHROOT 18
#define CAP_SYS_PTRACE 19
#define CAP_SYS_ADMIN 21
#define CAP_SYS_BOOT 22
#define CAP_SYS_RESOURCE 24
#define CAP_SYS_TIME 25
#define CAP_MKNOD 27
#define CAP_AUDIT_WRITE 29
#define CAP_SETFCAP 31
#define CAP_MAC_OVERRIDE 32
#define CAP_LAST_CAP 40

#endif
