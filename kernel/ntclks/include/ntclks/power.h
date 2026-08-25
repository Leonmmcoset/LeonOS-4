/*
 * LeonOS power interface: declares shutdown and reboot operations.
 * Allows platform code and system calls to request controlled power changes.
 */
#ifndef NTCLKS_POWER_H
#define NTCLKS_POWER_H

struct boot_info;

/**
 * @brief Locate ACPI tables and record the S5 shutdown control state for later use.
 */
void power_init(const struct boot_info *boot);
/* Return a validated ACPI table discovered during power_init, or NULL. */
const void *power_acpi_find_table(const char signature[4]);
void power_reboot(void) __attribute__((noreturn));
void power_shutdown(void) __attribute__((noreturn));

#endif
