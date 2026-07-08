#ifndef NTCLKS_PLATFORM_H
#define NTCLKS_PLATFORM_H

#include <leonos/system.h>
#include <ntclks/multiboot2.h>

void platform_identity_init(const struct boot_info *boot);
void platform_machine_identity(struct leonos_machine_identity *identity);

#endif
