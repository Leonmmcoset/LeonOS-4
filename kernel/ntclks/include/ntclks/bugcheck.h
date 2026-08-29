/*
 * LeonOS bugcheck interface: declares fatal kernel diagnostic entry points.
 * Supplies panic, trap, and crash-reporting APIs for unrecoverable faults.
 */
#ifndef NTCLKS_BUGCHECK_H
#define NTCLKS_BUGCHECK_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

/**
 * @brief Stop the kernel immediately, print message, and halt.
 */
__attribute__((noreturn)) void bugcheck_panic(const char *message);
/**
 * @brief Report a fatal CPU exception with its vector, error code, and register state, then halt. The arguments mirror the exception frame the CPU pushed.
 */
__attribute__((noreturn)) void bugcheck_exception(uint64_t vector, uint64_t error,
                                                  uint64_t rip, uint64_t cs,
                                                  uint64_t rflags, uint64_t rsp,
                                                  uint64_t ss, uint64_t cr2);
/**
 * @brief Report a fatal trap with a human-readable reason and the saved frame; never returns.
 */
__attribute__((noreturn)) void bugcheck_trap(const char *reason, const struct trap_frame *frame,
                                             uint64_t cr2);

#endif
