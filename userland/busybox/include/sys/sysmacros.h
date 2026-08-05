#ifndef LEONOS_BUSYBOX_SYS_SYSMACROS_H
#define LEONOS_BUSYBOX_SYS_SYSMACROS_H

#define major(device) ((unsigned)((device) >> 8))
#define minor(device) ((unsigned)((device) & 0xffU))
#define makedev(major_value, minor_value) (((unsigned long)(major_value) << 8) | (unsigned long)(minor_value))

#endif
