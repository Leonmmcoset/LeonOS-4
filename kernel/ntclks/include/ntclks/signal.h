/*
 * LeonOS kernel signal delivery: user rt_sigframe setup, restoration and
 * disposition management for the Linux rt_sig* syscall subset.
 */
#ifndef NTCLKS_SIGNAL_H
#define NTCLKS_SIGNAL_H

#include <ntclks/trap.h>
#include <ntclks/types.h>

struct task;

#define KERNEL_SIGNAL_ACTION_MAX 32u
#define KERNEL_SIGNAL_VALID_MASK 0xfffffffeu

struct kernel_signal_action {
    uint64_t handler;
    uint64_t mask;
    uint64_t restorer;
    uint32_t flags;
    uint32_t reserved;
};

/**
 * @brief Queue sig on task according to its current disposition.
 *
 * Blocked signals stay pending. Ignored signals are discarded. User-handler
 * dispositions leave the signal pending and wake a blocked task so the next
 * return-to-user path can install the handler frame. Default dispositions are
 * applied immediately.
 *
 * @return 0 on success, or a negative scheduler-style error.
 */
int kernel_signal_queue_task(struct task *task, int signal_number);

/**
 * @brief Install one user signal action and report the previous action.
 *
 * handler 0 means SIG_DFL, handler 1 means SIG_IGN. restorer must be supplied
 * for a real user handler.
 */
int kernel_signal_set_action(struct task *task, int signal_number,
                             uint64_t handler, uint64_t mask, uint32_t flags,
                             uint64_t restorer,
                             struct kernel_signal_action *previous);

/**
 * @brief Deliver one pending, unblocked user-handler signal by rewriting frame.
 *
 * Called on a kernel path that is about to return to, or select, this task.
 * The caller must not hold the scheduler lock while invoking this function.
 *
 * @return 1 when a user handler frame was installed, 0 when nothing was due,
 *         or a negative errno when the user stack could not be written.
 */
int kernel_signal_deliver_pending(struct task *task, struct trap_frame *frame);

/**
 * @brief Restore the interrupted frame saved by kernel_signal_deliver_pending().
 *
 * The libc restorer issues rt_sigreturn with rsp = frame_base + 8. The saved
 * context and blocked mask are copied back into task/frame.
 */
int64_t kernel_signal_rt_sigreturn(struct task *task, struct trap_frame *frame);

/**
 * @brief Drop all installed user handlers (execve semantics).
 */
void kernel_signal_reset_handlers(struct task *task);

/**
 * @brief Copy the task's currently effective signal state into out.
 */
void kernel_signal_state_snapshot(const struct task *task,
                                  struct kernel_signal_action actions[32],
                                  uint32_t *pending, uint32_t *blocked,
                                  uint32_t *ignored);

#endif
