#ifndef LEONOS_UAPI_LINUX_IPC_H
#define LEONOS_UAPI_LINUX_IPC_H

#include <linux/types.h>

#define LINUX_IPC_PRIVATE 0
#define LINUX_IPC_CREAT 01000
#define LINUX_IPC_EXCL 02000
#define LINUX_IPC_NOWAIT 04000
#define LINUX_IPC_RMID 0
#define LINUX_IPC_SET 1
#define LINUX_IPC_STAT 2
#define LINUX_IPC_INFO 3

/* Linux v6.12 asm-generic/ipcbuf.h, native x86-64 only. */
struct linux_ipc64_perm {
    __s32 key;
    __u32 uid, gid, cuid, cgid, mode;
    __u16 seq, pad2;
    __u32 pad3;
    __u64 unused1, unused2;
};

#endif
