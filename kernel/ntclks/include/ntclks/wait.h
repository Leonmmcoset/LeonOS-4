/*
 * LeonOS wait queue interface.
 * Provides the common waiter registration and wakeup primitive used by IPC,
 * device, and filesystem code.
 */
#ifndef NTCLKS_WAIT_H
#define NTCLKS_WAIT_H

#include <ntclks/lock.h>
#include <ntclks/types.h>

struct task;

#define KERNEL_WAIT_QUEUE_MAX 64u

struct kernel_wait_queue {
    struct kernel_spinlock lock;
    struct task *waiters[KERNEL_WAIT_QUEUE_MAX];
    uint32_t count;
};

void kernel_wait_queue_init(struct kernel_wait_queue *queue);
int kernel_wait_queue_add(struct kernel_wait_queue *queue, struct task *task);
void kernel_wait_queue_remove(struct kernel_wait_queue *queue, struct task *task);
uint32_t kernel_wait_queue_wake_one(struct kernel_wait_queue *queue);
uint32_t kernel_wait_queue_wake_all(struct kernel_wait_queue *queue);
/**
 * @brief Register the current task on queue and block it without a deadline.
 *
 * The caller must have already determined that no data/space/pending event is
 * available under the global syscall execution lock. The task is left
 * registered until the next retry of the same syscall; callers remove it on
 * entry to avoid stale registrations after a timer wakeup.
 */
void kernel_wait_queue_block_current(struct kernel_wait_queue *queue);

#endif
