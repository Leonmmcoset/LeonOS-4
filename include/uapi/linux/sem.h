#ifndef LEONOS_UAPI_LINUX_SEM_H
#define LEONOS_UAPI_LINUX_SEM_H

#include <linux/ipc.h>

#define LINUX_SEM_UNDO 0x1000
#define LINUX_GETPID 11
#define LINUX_GETVAL 12
#define LINUX_GETALL 13
#define LINUX_GETNCNT 14
#define LINUX_GETZCNT 15
#define LINUX_SETVAL 16
#define LINUX_SETALL 17
#define LINUX_SEM_STAT 18
#define LINUX_SEM_INFO 19
#define LINUX_SEM_STAT_ANY 20
#define LINUX_SEMMNI 32000
#define LINUX_SEMMSL 32000
#define LINUX_SEMMNS (LINUX_SEMMNI * LINUX_SEMMSL)
#define LINUX_SEMOPM 500
#define LINUX_SEMVMX 32767

/* Linux arch/x86/include/uapi/asm/sembuf.h, not asm-generic/sembuf.h. */
struct linux_semid64_ds {
    struct linux_ipc64_perm sem_perm;
    __s64 sem_otime;
    __u64 unused1;
    __s64 sem_ctime;
    __u64 unused2, sem_nsems, unused3, unused4;
};

struct linux_sembuf {
    __u16 sem_num;
    __s16 sem_op, sem_flg;
};

struct linux_seminfo {
    __s32 semmap, semmni, semmns, semmnu, semmsl;
    __s32 semopm, semume, semusz, semvmx, semaem;
};

#endif
