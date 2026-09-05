#ifndef LEONOS_SYS_IOCTL_H
#define LEONOS_SYS_IOCTL_H

#include <linux/ioctl.h>

int ioctl(int fd, unsigned long request, void *arg);

#endif
