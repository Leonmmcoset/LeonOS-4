/*
 * LeonOS platform interface: declares firmware and machine identification.
 * Provides platform setup and system-information hooks to kernel services.
 */
#ifndef NTCLKS_PLATFORM_H
#define NTCLKS_PLATFORM_H

#include <leonos/system.h>
#include <ntclks/multiboot2.h>

/**
 * @brief Record the machine identity (firmware vendor, UUID) parsed from boot info.
 */
void platform_identity_init(const struct boot_info *boot);
/**
 * @brief Copy the cached machine identity into identity.
 */
void platform_machine_identity(struct leonos_machine_identity *identity);

#endif
