/*
 * LeonOS architecture interface: declares CPU and user-mode transitions.
 * Provides the architecture-neutral hooks used by scheduler and kernel code.
 */
#ifndef NTCLKS_ARCH_H
#define NTCLKS_ARCH_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

#define NTCLKS_KERNEL_CS 0x08
#define NTCLKS_KERNEL_DS 0x10
#define NTCLKS_USER_DS 0x1b
#define NTCLKS_USER_CS 0x23

/**
 * @brief Coordinates the arch init operation.
 */
void arch_init(void);
/**
 * @brief Coordinates the arch userland init operation.
 * @param kernel_stack_top Input or output value used by this operation.
 */
void arch_userland_init(void *kernel_stack_top);
/**
 * @brief Coordinates the arch fpu init operation.
 */
void arch_fpu_init(void);
/**
 * @brief Coordinates the arch fpu task init operation.
 * @param state Input or output value used by this operation.
 */
void arch_fpu_task_init(void *state);
/**
 * @brief Coordinates the arch fpu save operation.
 * @param state Input or output value used by this operation.
 */
void arch_fpu_save(void *state);
/**
 * @brief Coordinates the arch fpu restore operation.
 * @param state Input or output value used by this operation.
 */
void arch_fpu_restore(const void *state);
/**
 * @brief Coordinates the irq init operation.
 */
void irq_init(void);
/**
 * @brief Coordinates the arch enter user operation.
 * @param entry Input or output value used by this operation.
 * @param user_stack Input or output value used by this operation.
 */
__attribute__((noreturn)) void arch_enter_user(uint64_t entry, uint64_t user_stack);
/**
 * @brief Coordinates the arch enter user frame operation.
 * @param frame Trap or syscall frame supplied by the architecture layer.
 * @param cr3 Input or output value used by this operation.
 */
__attribute__((noreturn)) void arch_enter_user_frame(struct trap_frame *frame, uint64_t cr3);

#endif
