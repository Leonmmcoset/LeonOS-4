/*
 * LeonOS kernel synchronization primitives.
 * Provides interrupt-safe spin locks for short non-sleeping critical sections.
 */
#ifndef NTCLKS_LOCK_H
#define NTCLKS_LOCK_H

#include <ntclks/types.h>

struct kernel_spinlock {
    volatile uint32_t state;
};

#define KERNEL_SPINLOCK_INIT { 0u }

void kernel_spin_init(struct kernel_spinlock *lock);
void kernel_spin_lock(struct kernel_spinlock *lock);
void kernel_spin_unlock(struct kernel_spinlock *lock);
uint64_t kernel_irq_save(void);
void kernel_irq_restore(uint64_t flags);
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags);
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags);

#endif
