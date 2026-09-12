#ifndef LEONOS_UAPI_LINUX_SECUREBITS_H
#define LEONOS_UAPI_LINUX_SECUREBITS_H

/* Linux v6.12 include/uapi/linux/securebits.h bit assignments. */
#define SECBIT_NOROOT (1U << 0)
#define SECBIT_NOROOT_LOCKED (1U << 1)
#define SECBIT_NO_SETUID_FIXUP (1U << 2)
#define SECBIT_NO_SETUID_FIXUP_LOCKED (1U << 3)
#define SECBIT_KEEP_CAPS (1U << 4)
#define SECBIT_KEEP_CAPS_LOCKED (1U << 5)
#define SECBIT_NO_CAP_AMBIENT_RAISE (1U << 6)
#define SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED (1U << 7)
#define SECURE_ALL_BITS 0x55U
#define SECURE_ALL_LOCKS 0xaaU

#endif
