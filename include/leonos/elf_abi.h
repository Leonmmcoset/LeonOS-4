#ifndef LEONOS_ELF_ABI_H
#define LEONOS_ELF_ABI_H

/* LeonOS user ABI note.  Every dynamically linked executable and shared
 * object carries this note; the loader rejects a different major ABI. */
#define LEONOS_ELF_NOTE_NAME "LeonOS"
#define LEONOS_ELF_NOTE_TYPE 0x4c4f5341u /* "LOSA" */
#define LEONOS_ELF_ABI_MAJOR 1u
#define LEONOS_ELF_ABI_MINOR 0u

#define LEONOS_ELF_INTERP_PATH "0:/system/lib/ld-leonos.elf"
#define LEONOS_ELF_RUNTIME_SONAME "libleonos.so.1"
#define LEONOS_ELF_RUNTIME_PATH "0:/system/lib/libleonos.so.1"

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
