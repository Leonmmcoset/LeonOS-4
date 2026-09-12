#ifndef LEONOS_ELF_ABI_H
#define LEONOS_ELF_ABI_H

/* Historical private ABI identification used to diagnose old binaries.
 * Current musl executables use the Linux initial stack and do not require
 * this note. The old loader/runtime are no longer distributed. */
#define LEONOS_ELF_NOTE_NAME "LeonOS"
#define LEONOS_ELF_NOTE_TYPE 0x4c4f5341u /* "LOSA" */
#define LEONOS_ELF_ABI_MAJOR 1u
#define LEONOS_ELF_ABI_MINOR 0u

#include <leonos/layout.h>

#define LEONOS_ELF_INTERP_PATH LEONOS_PATH_OLD_NATIVE_INTERP
#define LEONOS_MUSL_INTERP_PATH LEONOS_PATH_MUSL_INTERP
#define LEONOS_ELF_RUNTIME_SONAME "libleonos.so.1"
#define LEONOS_ELF_RUNTIME_PATH LEONOS_PATH_LIBLEONOS_COMPAT

struct leonos_elf_abi_note {
    unsigned int major;
    unsigned int minor;
};

/* Passed by the kernel to ld-leonos in r8.  The normal LeonOS argc/argv/envp
 * register ABI remains unchanged in rdi/rsi/rdx. */
struct leonos_dynamic_launch {
    unsigned long long main_base;
    unsigned long long main_entry;
    unsigned long long main_phdr;
    unsigned long long interp_base;
    unsigned long long interp_entry;
    unsigned int abi_major;
    unsigned int reserved;
    unsigned char random[16];
    char main_path[260];
};

#endif
