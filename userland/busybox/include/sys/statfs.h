#ifndef LEONOS_BUSYBOX_SYS_STATFS_H
#define LEONOS_BUSYBOX_SYS_STATFS_H

#include <sys/types.h>

struct statfs {
    long f_type;
    long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
};

int statfs(const char *path, struct statfs *buffer);

#endif
