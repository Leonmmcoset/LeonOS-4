/*
 * LeonOS kernel synchronization implementation.
 * Implements compact x86_64 spin locks and interrupt state preservation.
 */
#include <ntclks/lock.h>
#include <ntclks/smp.h>

/* The execution lock may be entered again while the owning CPU resolves a
 * kernel-mode user-memory fault.  A plain spin lock would self-deadlock in
 * that path, so retain a small per-CPU recursion count.  The owner is only
 * inspected lock-free by the owning CPU. A ticket lock makes the global
 * kernel-service transaction FIFO: a new scheduler pass cannot repeatedly
 * steal it from a pending exec or page-in on another CPU. */
static volatile uint32_t execution_next_ticket;
static volatile uint32_t execution_serving_ticket;
static volatile uint32_t execution_owner = UINT32_MAX;
static uint32_t execution_depth[SMP_MAX_CPUS];

static uint32_t execution_cpu_index(void)
{
    uint32_t cpu = smp_current_cpu();
    return cpu < SMP_MAX_CPUS ? cpu : 0;
}

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

void kernel_execution_lock_irqsave(uint64_t *flags)
{
    /* Recursion belongs to an execution context, not merely to a CPU. A
     * timer-driven task switch while this lock is held would otherwise let
     * the newly scheduled task on the same CPU enter the protected region as
     * if it were the owner.  Keep local interrupts disabled for the bounded
     * kernel transaction so ownership cannot cross a scheduler boundary.
     * FIFO admission prevents the high-frequency started-task scheduler path
     * from starving a pending image load on another CPU. */
    uint64_t saved = kernel_irq_save();
    if (flags) {
        *flags = saved;
    }
    uint32_t cpu = execution_cpu_index();
    if (execution_owner == cpu && execution_depth[cpu] != 0) {
        ++execution_depth[cpu];
        return;
    }
    uint32_t ticket = 1;
    __asm__ volatile("lock; xaddl %0, %1"
                     : "+r"(ticket), "+m"(execution_next_ticket)
                     : : "memory");
    while (execution_serving_ticket != ticket) {
        __asm__ volatile("pause" : : : "memory");
    }
    execution_owner = cpu;
    execution_depth[cpu] = 1;
}

void kernel_execution_unlock_irqrestore(uint64_t flags)
{
    uint32_t cpu = execution_cpu_index();
    if (execution_owner == cpu && execution_depth[cpu] > 1) {
        --execution_depth[cpu];
        kernel_irq_restore(flags);
        return;
    }
    if (execution_owner == cpu && execution_depth[cpu] == 1) {
        execution_depth[cpu] = 0;
        execution_owner = UINT32_MAX;
        __asm__ volatile("lock; incl %0" : "+m"(execution_serving_ticket)
                         : : "memory");
    }
    kernel_irq_restore(flags);
}
