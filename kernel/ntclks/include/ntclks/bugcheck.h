#ifndef NTCLKS_BUGCHECK_H
#define NTCLKS_BUGCHECK_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

__attribute__((noreturn)) void bugcheck_panic(const char *message);
__attribute__((noreturn)) void bugcheck_exception(uint64_t vector, uint64_t error,
                                                  uint64_t rip, uint64_t cs,
                                                  uint64_t rflags, uint64_t rsp,
                                                  uint64_t ss, uint64_t cr2);
__attribute__((noreturn)) void bugcheck_trap(const char *reason, const struct trap_frame *frame,
                                             uint64_t cr2);

#endif
