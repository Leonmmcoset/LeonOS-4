#ifndef LEONOS_ROOTFS_H
#define LEONOS_ROOTFS_H

/* Canonical non-usr-merge rootfs skeleton, based on Alpine baselayout 3.7.2.
 * Real directories have root:root ownership. These mount points do not imply
 * that Linux sysfs/tmpfs/OpenRC facilities are implemented by LeonOS.
 * Keep the X(path, value) rows machine-readable for tools/leonos_layout.py.
 */
#define LEONOS_ROOTFS_DIRECTORIES(X) \
    X("/bin", 0755) \
    X("/boot", 0755) \
    X("/dev", 0755) \
    X("/dev/pts", 0755) \
    X("/dev/shm", 01777) \
    X("/etc", 0755) \
    X("/etc/crontabs", 0755) \
    X("/etc/leonos", 0755) \
    X("/etc/modprobe.d", 0755) \
    X("/etc/modules-load.d", 0755) \
    X("/etc/network", 0755) \
    X("/etc/network/if-down.d", 0755) \
    X("/etc/network/if-post-down.d", 0755) \
    X("/etc/network/if-pre-up.d", 0755) \
    X("/etc/network/if-up.d", 0755) \
    X("/etc/opt", 0755) \
    X("/etc/periodic", 0755) \
    X("/etc/periodic/15min", 0755) \
    X("/etc/periodic/daily", 0755) \
    X("/etc/periodic/hourly", 0755) \
    X("/etc/periodic/monthly", 0755) \
    X("/etc/periodic/weekly", 0755) \
    X("/etc/profile.d", 0755) \
    X("/etc/ssl/certs", 0755) \
    X("/etc/sysctl.d", 0755) \
    X("/home", 0755) \
    X("/lib", 0755) \
    X("/lib/firmware", 0755) \
    X("/lib/modules-load.d", 0755) \
    X("/lib/sysctl.d", 0755) \
    X("/media", 0755) \
    X("/media/cdrom", 0755) \
    X("/media/floppy", 0755) \
    X("/media/usb", 0755) \
    X("/mnt", 0755) \
    X("/opt", 0755) \
    X("/proc", 0755) \
    X("/root", 0700) \
    X("/run", 0755) \
    X("/run/leonos", 0755) \
    X("/run/lock", 0755) \
    X("/sbin", 0755) \
    X("/srv", 0755) \
    X("/sys", 0755) \
    X("/tmp", 01777) \
    X("/usr", 0755) \
    X("/usr/bin", 0755) \
    X("/usr/include", 0755) \
    X("/usr/lib", 0755) \
    X("/usr/lib/leonos", 0755) \
    X("/usr/lib/leonos/apps", 0755) \
    X("/usr/lib/leonos/drivers", 0755) \
    X("/usr/lib/leonos/tests", 0755) \
    X("/usr/lib/modules-load.d", 0755) \
    X("/usr/lib/sysctl.d", 0755) \
    X("/usr/local", 0755) \
    X("/usr/local/bin", 0755) \
    X("/usr/local/include", 0755) \
    X("/usr/local/lib", 0755) \
    X("/usr/local/sbin", 0755) \
    X("/usr/local/share", 0755) \
    X("/usr/local/share/man", 0755) \
    X("/usr/sbin", 0755) \
    X("/usr/share", 0755) \
    X("/usr/share/doc/leonos", 0755) \
    X("/usr/share/fonts/leonos", 0755) \
    X("/usr/share/leonos/resources", 0755) \
    X("/usr/share/licenses", 0755) \
    X("/usr/share/man", 0755) \
    X("/usr/share/misc", 0755) \
    X("/usr/src", 0755) \
    X("/var", 0755) \
    X("/var/cache", 0755) \
    X("/var/cache/leonos", 0755) \
    X("/var/cache/misc", 0755) \
    X("/var/empty", 0555) \
    X("/var/lib", 0755) \
    X("/var/lib/leonos", 0750) \
    X("/var/lib/misc", 0755) \
    X("/var/local", 0755) \
    X("/var/log", 0755) \
    X("/var/mail", 0755) \
    X("/var/opt", 0755) \
    X("/var/spool", 0755) \
    X("/var/spool/cron", 0755) \
    X("/var/tmp", 01777)

#define LEONOS_ROOTFS_SYMLINKS(X) \
    X("/var/run", "../run") \
    X("/var/lock", "../run/lock") \
    X("/var/spool/mail", "../mail") \
    X("/var/spool/cron/crontabs", "../../../etc/crontabs") \
    X("/etc/mtab", "../proc/mounts")

#define LEONOS_DEFAULT_PATH "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

#endif
