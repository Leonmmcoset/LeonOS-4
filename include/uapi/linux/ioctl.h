#ifndef LEONOS_UAPI_LINUX_IOCTL_H
#define LEONOS_UAPI_LINUX_IOCTL_H

/* Linux ioctl encoding, shared by device drivers and applications. */
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) \
    (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | \
     ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))
#define _IOC_DIR(nr) (((nr) >> _IOC_DIRSHIFT) & ((1U << _IOC_DIRBITS) - 1U))
#define _IOC_TYPE(nr) (((nr) >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1U))
#define _IOC_NR(nr) (((nr) >> _IOC_NRSHIFT) & ((1U << _IOC_NRBITS) - 1U))
#define _IOC_SIZE(nr) (((nr) >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1U))
#define _IO(type, nr) _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, data) _IOC(_IOC_READ, (type), (nr), sizeof(data))
#define _IOW(type, nr, data) _IOC(_IOC_WRITE, (type), (nr), sizeof(data))
#define _IOWR(type, nr, data) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(data))

#endif
