#ifndef LEONOS_UAPI_SYSCALL_ABI_H
#define LEONOS_UAPI_SYSCALL_ABI_H

/* Legacy LeonOS nice extension; native Linux x86-64 syscall 34 is pause.
 * New libc consumers implement nice through getpriority/setpriority. */
#define LEONOS_SYS_NICE 0x10000u

#endif
