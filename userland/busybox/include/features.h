#ifndef LEONOS_BUSYBOX_FEATURES_H
#define LEONOS_BUSYBOX_FEATURES_H

/*
 * BusyBox includes <features.h> for glibc/newlib feature probes whenever
 * Picolibc exposes _NEWLIB_VERSION.  There is no glibc feature header in the
 * freestanding LeonOS sysroot, and the selected applets need none of it.
 */
#define __GLIBC_PREREQ(major, minor) 0

#endif
