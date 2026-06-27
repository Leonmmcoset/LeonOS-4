#ifndef NTCLKS_POWER_H
#define NTCLKS_POWER_H

void power_reboot(void) __attribute__((noreturn));
void power_shutdown(void) __attribute__((noreturn));

#endif
