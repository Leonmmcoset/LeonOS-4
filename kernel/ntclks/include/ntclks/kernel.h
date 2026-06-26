#ifndef NTCLKS_KERNEL_H
#define NTCLKS_KERNEL_H

#include <leonos/boot_handoff.h>

void kernel_entry(const struct leonos_boot_handoff *handoff);
void kernel_idle_loop(void) __attribute__((noreturn));

#endif
