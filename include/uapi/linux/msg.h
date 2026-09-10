#ifndef LEONOS_UAPI_LINUX_MSG_H
#define LEONOS_UAPI_LINUX_MSG_H

#include <linux/ipc.h>

#define LINUX_MSG_NOERROR 010000
#define LINUX_MSG_EXCEPT 020000
#define LINUX_MSG_COPY 040000
#define LINUX_MSG_STAT 11
#define LINUX_MSG_INFO 12
#define LINUX_MSG_STAT_ANY 13
#define LINUX_MSGMNI 32000
#define LINUX_MSGMAX 8192
#define LINUX_MSGMNB 16384

struct linux_msqid64_ds {
    struct linux_ipc64_perm msg_perm;
    __s64 msg_stime, msg_rtime, msg_ctime;
    __u64 msg_cbytes, msg_qnum, msg_qbytes;
    __s32 msg_lspid, msg_lrpid;
    __u64 unused4, unused5;
};

struct linux_msginfo {
    __s32 msgpool, msgmap, msgmax, msgmnb, msgmni, msgssz, msgtql;
    __u16 msgseg, pad;
};

#endif
