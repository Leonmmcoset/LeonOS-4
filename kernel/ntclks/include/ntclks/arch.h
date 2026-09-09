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

/* Linux TASK_SIZE_MAX for our four-level paging configuration. A TLS base
 * need not name a mapped page; NTCLKS_USER_TOP is not the architectural limit. */
#define NTCLKS_USER_TLS_LIMIT ((1ULL << 47) - 4096)

/**
 * @brief Initialize architecture-specific processor state at early boot.
 */
void arch_init(void);
/**
 * @brief Install GDT/TSS and user page tables so ring-3 code can run; kernel_stack_top becomes the ring-0 stack used when an interrupt arrives.
 */
void arch_userland_init(void *kernel_stack_top);
/** Initialize architecture tables on an application processor. */
void arch_ap_init(uint32_t cpu_index, void *kernel_stack_top);
void arch_set_user_fs(uint64_t base);
uint64_t arch_get_user_fs(void);
/**
 * @brief Enable x87/SSE and capture the default FPU state used for new tasks.
 */
void arch_fpu_init(void);
/**
 * @brief Reset a task's FPU save area to the boot-time default state.
 */
void arch_fpu_task_init(void *state);
/**
 * @brief Store the current x87/SSE state into a task's save area before switching away.
 */
void arch_fpu_save(void *state);
/**
 * @brief Reload x87/SSE state from a task's save area when switching back to it.
 */
void arch_fpu_restore(const void *state);
/**
 * @brief Remap the PIC and start the PIT timer so IRQ0/1/12 can fire.
 */
void irq_init(void);
/**
 * @brief Jump to user-mode code at entry with user_stack as the initial stack; never returns.
 */
__attribute__((noreturn)) void arch_enter_user(uint64_t entry, uint64_t user_stack);
/**
 * @brief Load cr3 as the page table and resume user mode from the saved trap frame; never returns.
 */
__attribute__((noreturn)) void arch_enter_user_frame(struct trap_frame *frame, uint64_t cr3);

#endif
