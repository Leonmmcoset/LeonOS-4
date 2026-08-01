#ifndef NTCLKS_ARCH_H
#define NTCLKS_ARCH_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

#define NTCLKS_KERNEL_CS 0x08
#define NTCLKS_KERNEL_DS 0x10
#define NTCLKS_USER_DS 0x1b
#define NTCLKS_USER_CS 0x23

void arch_init(void);
void arch_userland_init(void *kernel_stack_top);
void arch_fpu_init(void);
void arch_fpu_task_init(void *state);
void arch_fpu_save(void *state);
void arch_fpu_restore(const void *state);
void irq_init(void);
__attribute__((noreturn)) void arch_enter_user(uint64_t entry, uint64_t user_stack);
__attribute__((noreturn)) void arch_enter_user_frame(struct trap_frame *frame, uint64_t cr3);

#endif
