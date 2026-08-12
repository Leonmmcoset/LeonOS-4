/*
 * LeonOS power interface: declares shutdown and reboot operations.
 * Allows platform code and system calls to request controlled power changes.
 */
#ifndef NTCLKS_POWER_H
#define NTCLKS_POWER_H

struct boot_info;

void power_init(const struct boot_info *boot);
void power_reboot(void) __attribute__((noreturn));
void power_shutdown(void) __attribute__((noreturn));

#endif
