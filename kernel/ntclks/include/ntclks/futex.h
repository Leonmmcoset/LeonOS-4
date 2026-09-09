#ifndef NTCLKS_FUTEX_H
#define NTCLKS_FUTEX_H
#include <ntclks/types.h>

struct task;
/* Internal scheduler result, never exposed as a Linux errno. */
#define KERNEL_SYSCALL_BLOCKED (-4096LL)

int64_t syscall_futex(uint64_t address, uint64_t operation, uint64_t value,
                      uint64_t timeout, uint64_t address2, uint64_t value3);
void futex_task_exit(struct task *task);
void futex_cancel_wait(struct task *task);

/**
 * @brief Wake matching futex2 waiters, preserving zero-count semantics.
 * @param address Aligned user U32 futex address.
 * @param mask Native unsigned-long U32 bitset.
 * @param count Native int wake limit.
 * @param flags Native unsigned-int futex2 flags.
 * @return Woken count or negative errno.
 */
int64_t syscall_futex_wake2(uint64_t address, uint64_t mask, uint64_t count,
                            uint64_t flags);
/**
 * @brief Wait on a U32 futex with an optional absolute Linux clock deadline.
 * @param address Aligned user U32 futex address.
 * @param value Native unsigned-long expected U32 value.
 * @param mask Native unsigned-long nonzero U32 bitset.
 * @param flags Native unsigned-int futex2 flags.
 * @param timeout Optional user timespec pointer.
 * @param clockid Native clockid_t, ignored without timeout.
 * @return Zero, negative errno, or KERNEL_SYSCALL_BLOCKED while queued.
 */
int64_t syscall_futex_wait2(uint64_t address, uint64_t value, uint64_t mask,
                            uint64_t flags, uint64_t timeout, uint64_t clockid);
/**
 * @brief Compare, wake, and requeue between independently keyed futexes.
 * @param waiters User array of two futex_waitv descriptors.
 * @param flags Native unsigned-int reserved flags, must be zero.
 * @param wake_count Native nonnegative int wake limit.
 * @param requeue_count Native nonnegative int move limit.
 * @return Woken/moved count or negative errno.
 */
int64_t syscall_futex_requeue2(uint64_t waiters, uint64_t flags,
                               uint64_t wake_count, uint64_t requeue_count);

#endif
