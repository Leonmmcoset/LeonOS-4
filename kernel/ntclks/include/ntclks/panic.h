#ifndef NTCLKS_PANIC_H
#define NTCLKS_PANIC_H

#include <ntclks/bugcheck.h>

__attribute__((noreturn)) void panic(const char *message);

#endif
