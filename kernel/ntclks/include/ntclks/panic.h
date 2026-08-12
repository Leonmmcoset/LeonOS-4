/*
 * LeonOS panic interface: declares the concise fatal-error entry point.
 * Forwards unrecoverable conditions to the kernel bugcheck subsystem.
 */
#ifndef NTCLKS_PANIC_H
#define NTCLKS_PANIC_H

#include <ntclks/bugcheck.h>

__attribute__((noreturn)) void panic(const char *message);

#endif
