/*
 * LeonOS platform interface: declares firmware and machine identification.
 * Provides platform setup and system-information hooks to kernel services.
 */
#ifndef NTCLKS_PLATFORM_H
#define NTCLKS_PLATFORM_H

#include <leonos/system.h>
#include <ntclks/multiboot2.h>

/**
 * @brief Coordinates the platform identity init operation.
 * @param boot Boot information supplied by the loader.
 */
void platform_identity_init(const struct boot_info *boot);
/**
 * @brief Coordinates the platform machine identity operation.
 * @param identity Input or output value used by this operation.
 */
void platform_machine_identity(struct leonos_machine_identity *identity);

#endif
