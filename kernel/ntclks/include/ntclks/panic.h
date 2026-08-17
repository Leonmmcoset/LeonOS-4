/*
 * LeonOS panic interface: declares the concise fatal-error entry point.
 * Forwards unrecoverable conditions to the kernel bugcheck subsystem.
 */
#ifndef NTCLKS_PANIC_H
#define NTCLKS_PANIC_H

#include <ntclks/bugcheck.h>

/**
 * @brief Coordinates the panic operation.
 * @param message Input or output value used by this operation.
 */
__attribute__((noreturn)) void panic(const char *message);

#endif
