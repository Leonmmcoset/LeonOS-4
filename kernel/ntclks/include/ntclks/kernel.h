/*
 * LeonOS kernel lifecycle interface: declares top-level kernel initialization.
 * Defines the entry point used after architecture and boot setup complete.
 */
#ifndef NTCLKS_KERNEL_H
#define NTCLKS_KERNEL_H

#include <leonos/boot_handoff.h>

/**
 * @brief Coordinates the kernel entry operation.
 * @param handoff Input or output value used by this operation.
 */
void kernel_entry(const struct leonos_boot_handoff *handoff);
void kernel_idle_loop(void) __attribute__((noreturn));

#endif
