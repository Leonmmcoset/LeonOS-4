/*
 * LeonOS kernel synchronization implementation.
 * Implements compact x86_64 spin locks and interrupt state preservation.
 */
#include <ntclks/lock.h>

void kernel_spin_init(struct kernel_spinlock *lock)
{
    if (lock) {
        lock->state = 0;
    }
}

void kernel_spin_lock(struct kernel_spinlock *lock)
{
    uint32_t busy = 1;
    if (!lock) {
        return;
    }
    for (;;) {
        __asm__ volatile("xchg %0, %1" : "+r"(busy), "+m"(lock->state)
                         : : "memory");
        if (busy == 0) {
            return;
        }
        busy = 1;
        __asm__ volatile("pause" : : : "memory");
    }
}

void kernel_spin_unlock(struct kernel_spinlock *lock)
{
    if (lock) {
        __asm__ volatile("movl $0, %0" : "=m"(lock->state) : : "memory");
    }
}

uint64_t kernel_irq_save(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void kernel_irq_restore(uint64_t flags)
{
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" : : : "memory");
    } else {
        __asm__ volatile("cli" : : : "memory");
    }
}

void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{
    uint64_t saved = kernel_irq_save();
    if (flags) {
        *flags = saved;
    }
    kernel_spin_lock(lock);
}

void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{
    kernel_spin_unlock(lock);
    kernel_irq_restore(flags);
}
