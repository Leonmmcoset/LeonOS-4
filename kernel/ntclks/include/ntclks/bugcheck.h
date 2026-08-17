/*
 * LeonOS bugcheck interface: declares fatal kernel diagnostic entry points.
 * Supplies panic, trap, and crash-reporting APIs for unrecoverable faults.
 */
#ifndef NTCLKS_BUGCHECK_H
#define NTCLKS_BUGCHECK_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

/**
 * @brief Coordinates the bugcheck panic operation.
 * @param message Input or output value used by this operation.
 */
__attribute__((noreturn)) void bugcheck_panic(const char *message);
/**
 * @brief Coordinates the bugcheck exception operation.
 * @param vector Input or output value used by this operation.
 * @param error Input or output value used by this operation.
 * @param rip Input or output value used by this operation.
 * @param cs Input or output value used by this operation.
 * @param rflags Input or output value used by this operation.
 * @param rsp Input or output value used by this operation.
 * @param ss Input or output value used by this operation.
 * @param cr2 Input or output value used by this operation.
 */
__attribute__((noreturn)) void bugcheck_exception(uint64_t vector, uint64_t error,
                                                  uint64_t rip, uint64_t cs,
                                                  uint64_t rflags, uint64_t rsp,
                                                  uint64_t ss, uint64_t cr2);
/**
 * @brief Coordinates the bugcheck trap operation.
 * @param reason Input or output value used by this operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param cr2 Input or output value used by this operation.
 */
__attribute__((noreturn)) void bugcheck_trap(const char *reason, const struct trap_frame *frame,
                                             uint64_t cr2);

#endif
