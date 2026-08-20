/*
 * LeonOS wait queue implementation.
 * Wakeup changes task state through the scheduler's public ready operation.
 */
#include <ntclks/sched.h>
#include <ntclks/wait.h>

void kernel_wait_queue_init(struct kernel_wait_queue *queue)
{
    if (!queue) {
        return;
    }
    kernel_spin_init(&queue->lock);
    queue->count = 0;
    for (uint32_t i = 0; i < KERNEL_WAIT_QUEUE_MAX; ++i) {
        queue->waiters[i] = NULL;
    }
}

int kernel_wait_queue_add(struct kernel_wait_queue *queue, struct task *task)
{
    uint64_t flags;
    if (!queue || !task) {
        return -1;
    }
    kernel_spin_lock_irqsave(&queue->lock, &flags);
    for (uint32_t i = 0; i < queue->count; ++i) {
        if (queue->waiters[i] == task) {
            kernel_spin_unlock_irqrestore(&queue->lock, flags);
            return 0;
        }
    }
    if (queue->count >= KERNEL_WAIT_QUEUE_MAX) {
        kernel_spin_unlock_irqrestore(&queue->lock, flags);
        return -1;
    }
    queue->waiters[queue->count++] = task;
    kernel_spin_unlock_irqrestore(&queue->lock, flags);
    return 0;
}

void kernel_wait_queue_remove(struct kernel_wait_queue *queue, struct task *task)
{
    uint64_t flags;
    if (!queue || !task) {
        return;
    }
    kernel_spin_lock_irqsave(&queue->lock, &flags);
    for (uint32_t i = 0; i < queue->count; ++i) {
        if (queue->waiters[i] == task) {
            for (uint32_t j = i + 1; j < queue->count; ++j) {
                queue->waiters[j - 1] = queue->waiters[j];
            }
            queue->waiters[--queue->count] = NULL;
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&queue->lock, flags);
}

uint32_t kernel_wait_queue_wake_one(struct kernel_wait_queue *queue)
{
    uint64_t flags;
    struct task *task = NULL;
    if (!queue) {
        return 0;
    }
    kernel_spin_lock_irqsave(&queue->lock, &flags);
    if (queue->count) {
        task = queue->waiters[0];
        for (uint32_t i = 1; i < queue->count; ++i) {
            queue->waiters[i - 1] = queue->waiters[i];
        }
        queue->waiters[--queue->count] = NULL;
    }
    kernel_spin_unlock_irqrestore(&queue->lock, flags);
    if (task) {
        sched_mark_ready(task->pid);
        return 1;
    }
    return 0;
}

uint32_t kernel_wait_queue_wake_all(struct kernel_wait_queue *queue)
{
    uint32_t count = 0;
    while (kernel_wait_queue_wake_one(queue)) {
        ++count;
    }
    return count;
}
