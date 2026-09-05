#ifndef LEONOS_SYS_MOUNT_H
#define LEONOS_SYS_MOUNT_H

#include <linux/mount.h>

int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data);
int umount2(const char *target, int flags);
int umount(const char *target);

#endif
