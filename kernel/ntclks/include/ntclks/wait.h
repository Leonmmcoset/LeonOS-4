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

#endif
