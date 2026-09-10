/* Native x86-64 siginfo queues. Queue ownership follows the pending bitmaps;
 * UID charges outlive credential changes, thread exit and exec leadership changes. */
#include <ntclks/signal.h>
#include <ntclks/sched.h>
#include <ntclks/heap.h>
#include <ntclks/lock.h>
#include <ntclks/usercopy.h>
#include <linux/capability.h>
#include <linux/errno.h>

#define KERNEL_SIGINFO_SIZE 48u

struct kernel_sigqueue_entry {
    struct kernel_sigqueue_entry *next, *account_next;
    uint32_t charged_uid;
    struct linux_siginfo info;
};

/* Lock order is scheduler -> signal queue -> heap. Never acquire the scheduler
 * lock here. The account list counts all pending queues per UID. */
static struct kernel_spinlock signal_queue_lock = KERNEL_SPINLOCK_INIT;
static struct kernel_sigqueue_entry *signal_accounts;

/** @brief Unlink an entry from the UID accounting list before freeing it. */
static void signal_entry_free(struct kernel_sigqueue_entry *entry)
{
    struct kernel_sigqueue_entry **link = &signal_accounts;
    while (*link != entry) link = &(*link)->account_next;
    *link = entry->account_next;
    kernel_free(entry);
}

/** @brief Return whether SIG_DFL discards this native Linux signal. */
bool kernel_signal_default_ignored(int sig)
{
    return sig == 17 || sig == 18 || sig == 23 || sig == 28;
}

int kernel_signal_fatal_pending(const struct task *task)
{
    uint64_t pending = sched_task_pending(task) & ~task->blocked_signals;
    if (sched_task_pending(task) & (1ULL << 8)) return 9;
    /* Linux complete_signal starts group exit for default non-core signals.
     * Core-dumping signals await normal delivery, even in TASK_KILLABLE. */
    const uint64_t core = (1ULL << 2) | (1ULL << 3) | (1ULL << 4) |
        (1ULL << 5) | (1ULL << 6) | (1ULL << 7) | (1ULL << 10) |
        (1ULL << 23) | (1ULL << 24) | (1ULL << 30);
    pending &= ~core;
    while (pending) {
        int sig = __builtin_ctzll(pending) + 1;
        pending &= pending - 1;
        if (!kernel_signal_default_ignored(sig) && !(sig >= 19 && sig <= 22) &&
            !sched_task_actions(task)[sig].handler) return sig;
    }
    return 0;
}

/**
 * @brief Append a siginfo under the queue lock and account it to the receiver UID.
 * @param task Receiver, with lifetime pinned by the caller.
 * @param process Select the shared process queue instead of the private queue.
 * @param sig Valid native signal number (1..64).
 * @param info User signal data, or NULL for SI_KERNEL with allocation fallback.
 * @return Zero after queuing/coalescing, -EAGAIN for RT overflow, or -EINVAL.
 */
int kernel_signal_enqueue(struct task *task, bool process, int sig,
                           const struct linux_siginfo *info)
{
    if (!task || sig <= 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    uint64_t flags, bit = 1ULL << (sig - 1);
    uint64_t *pending = process ? sched_task_process_pending(task) : &task->pending_signals;
    struct kernel_sigqueue *queue = process ? sched_task_process_signal_queue(task) : &task->signal_queue;
    kernel_spin_lock_irqsave(&signal_queue_lock, &flags);
    if (sig < LINUX_SIGRTMIN && (__atomic_load_n(pending, __ATOMIC_SEQ_CST) & bit)) {
        kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
        return 0;
    }
    struct kernel_sigqueue_entry *entry = NULL;
    if (sig != 9) {
        uint64_t count = 0;
        for (struct kernel_sigqueue_entry *q = signal_accounts; q; q = q->account_next)
            if (q->charged_uid == task->uid) ++count;
        bool override = sig < LINUX_SIGRTMIN && (!info || info->code >= 0);
        if (override || count < sched_task_limits(task)->sigpending.rlim_cur)
            entry = kernel_malloc(sizeof(*entry));
        if (!entry && sig >= LINUX_SIGRTMIN && info && info->code != LINUX_SI_USER) {
            kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
            return -LINUX_EAGAIN;
        }
    }
    if (entry) {
        *entry = (struct kernel_sigqueue_entry){.charged_uid = task->uid,
            .info = {.signo = sig, .code = LINUX_SI_KERNEL}};
        if (info) __builtin_memcpy(&entry->info, info, KERNEL_SIGINFO_SIZE);
        entry->info.signo = sig;
        entry->account_next = signal_accounts;
        signal_accounts = entry;
        if (queue->tail) queue->tail->next = entry;
        else queue->head = entry;
        queue->tail = entry;
    }
    __atomic_fetch_or(pending, bit, __ATOMIC_SEQ_CST);
    kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
    return 0;
}

/**
 * @brief Remove selected queued signals, their bits and their original UID charges.
 * @param task Receiver whose lifetime is pinned by the caller.
 * @param process Select shared rather than private pending state.
 * @param mask Signals to discard, with bit zero representing signal one.
 */
void kernel_signal_flush(struct task *task, bool process, uint64_t mask)
{
    uint64_t flags;
    uint64_t *pending = process ? sched_task_process_pending(task) : &task->pending_signals;
    struct kernel_sigqueue *queue = process ? sched_task_process_signal_queue(task) : &task->signal_queue;
    kernel_spin_lock_irqsave(&signal_queue_lock, &flags);
    struct kernel_sigqueue_entry **link = &queue->head, *previous = NULL;
    while (*link) {
        struct kernel_sigqueue_entry *entry = *link;
        if (mask & (1ULL << (entry->info.signo - 1))) {
            *link = entry->next;
            if (queue->tail == entry) queue->tail = previous;
            signal_entry_free(entry);
        } else {
            previous = entry;
            link = &entry->next;
        }
    }
    __atomic_fetch_and(pending, ~mask, __ATOMIC_SEQ_CST);
    kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
}

/**
 * @brief Consume one signal with Linux private/shared, synchronous and FIFO ordering.
 * @param task Receiver whose lifetime is pinned by the caller.
 * @param allowed Signals eligible for this wait or handler delivery.
 * @param info Receives the native payload, with the ABI expansion zeroed.
 * @return Signal number consumed, or zero when none is eligible.
 */
int kernel_signal_dequeue(struct task *task, uint64_t allowed, struct linux_siginfo *info)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&signal_queue_lock, &flags);
    for (unsigned process = 0; process < 2; ++process) {
        uint64_t *pending = process ? sched_task_process_pending(task) : &task->pending_signals;
        struct kernel_sigqueue *queue = process ? sched_task_process_signal_queue(task) : &task->signal_queue;
        uint64_t ready = __atomic_load_n(pending, __ATOMIC_SEQ_CST) & allowed;
        if (!ready) continue;
        const uint64_t synchronous = (1ULL << 3) | (1ULL << 4) | (1ULL << 6) |
            (1ULL << 7) | (1ULL << 10) | (1ULL << 30);
        if (ready & synchronous) ready &= synchronous;
        int sig = __builtin_ctzll(ready) + 1;
        uint64_t bit = 1ULL << (sig - 1);
        struct kernel_sigqueue_entry **link = &queue->head, *previous = NULL;
        while (*link && (*link)->info.signo != sig) {
            previous = *link;
            link = &(*link)->next;
        }
        *info = (struct linux_siginfo){.signo = sig, .code = LINUX_SI_USER};
        if (*link) {
            struct kernel_sigqueue_entry *entry = *link;
            *info = entry->info;
            *link = entry->next;
            if (queue->tail == entry) queue->tail = previous;
            signal_entry_free(entry);
        } else {
            /* Preserve legacy timer notifications until their producer moves
             * to preallocated, per-timer siginfo records. */
            struct task *owner = process ? sched_find(sched_task_tgid(task)) : task;
            if (owner && (owner->timer_pending_signals & bit)) {
                info->code = LINUX_SI_TIMER;
                owner->timer_pending_signals &= ~bit;
            }
        }
        bool more = false;
        for (struct kernel_sigqueue_entry *entry = queue->head; entry; entry = entry->next)
            if (entry->info.signo == sig) { more = true; break; }
        if (!more) __atomic_fetch_and(pending, ~bit, __ATOMIC_SEQ_CST);
        kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
        if (process && sig == 14) sched_alarm_rearm(task);
        return sig;
    }
    kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
    return 0;
}

/**
 * @brief Transfer the shared queue and bitmap when exec replaces the group leader.
 * @param task Sole surviving group member, with other members already quiesced.
 */
void kernel_signal_detach_process(struct task *task)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&signal_queue_lock, &flags);
    uint64_t *old_pending = sched_task_process_pending(task);
    task->process_pending_signals = *old_pending;
    if (old_pending != &task->process_pending_signals) *old_pending = 0;
    task->shared_process_pending = NULL;
    struct kernel_sigqueue *old_queue = sched_task_process_signal_queue(task);
    if (old_queue != &task->process_signal_queue) {
        task->process_signal_queue = *old_queue;
        *old_queue = (struct kernel_sigqueue){0};
    }
    task->shared_process_signal_queue = NULL;
    kernel_spin_unlock_irqrestore(&signal_queue_lock, flags);
}

/** @brief Match Linux 6.12 known_siginfo_layout for native x86-64. */
static bool signal_known_layout(int sig, int code)
{
    if (code == LINUX_SI_KERNEL) return true;
    if (code <= 0) return code >= -7 || code == -60;
    int limit = 6;
    switch (sig) {
    case 4: limit = 11; break;
    case 8: limit = 15; break;
    case 11: limit = 10; break;
    case 7: limit = 5; break;
    case 31: limit = 2; break;
    }
    return code <= limit;
}

/**
 * @brief Validate and send a native rt_sigqueueinfo/rt_tgsigqueueinfo request.
 * @param tgid Group constraint for thread sends; target PID/TID for process sends.
 * @param tid Target TID for a thread-directed request.
 * @param sig Linux signal number, including zero for a permission probe.
 * @param pointer User siginfo address, copied as 48 bytes plus checked unknown expansion.
 * @param thread Select rt_tgsigqueueinfo semantics.
 * @return Zero on success or the Linux copy, permission, identity or queue errno.
 */
int64_t kernel_signal_queueinfo(int32_t tgid, int32_t tid, int sig,
                                uint64_t pointer, bool thread)
{
    struct linux_siginfo info = {0};
    struct task *caller = sched_current_task();
    if (!user_range_ok(pointer, KERNEL_SIGINFO_SIZE)) return -LINUX_EFAULT;
    __builtin_memcpy(&info, (const void *)(uintptr_t)pointer, KERNEL_SIGINFO_SIZE);
    info.signo = sig;
    if (!signal_known_layout(sig, info.code)) {
        if (pointer > UINT64_MAX - sizeof(info) ||
            !user_range_ok(pointer + KERNEL_SIGINFO_SIZE, sizeof(info) - KERNEL_SIGINFO_SIZE))
            return -LINUX_EFAULT;
        const unsigned char *extra = (const void *)(uintptr_t)(pointer + KERNEL_SIGINFO_SIZE);
        for (unsigned i = 0; i < sizeof(info) - KERNEL_SIGINFO_SIZE; ++i)
            if (extra[i]) return -LINUX_E2BIG;
    }
    if (thread && (tgid <= 0 || tid <= 0)) return -LINUX_EINVAL;
    if (!caller) return -LINUX_ESRCH;
    int32_t pid = thread ? tid : tgid;
    if ((info.code >= 0 || info.code == LINUX_SI_TKILL) && caller->pid != (uint32_t)pid)
        return -LINUX_EPERM;
    struct task *target = pid > 0 ? sched_find((uint32_t)pid) : NULL;
    if (!target || target->kind != TASK_KIND_USER ||
        (thread && sched_task_tgid(target) != (uint32_t)tgid)) return -LINUX_ESRCH;
    if (sig < 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    if (info.code <= 0 && sched_task_tgid(caller) != sched_task_tgid(target) &&
        caller->euid != target->uid && caller->euid != target->suid &&
        caller->uid != target->uid && caller->uid != target->suid &&
        !(caller->cap_effective & (1ULL << CAP_KILL)) &&
        !(sig == 18 && caller->process_session == target->process_session)) return -LINUX_EPERM;
    if (!sig) return 0;
    if (thread)
        return target->state == TASK_EXITED ? 0 : kernel_signal_queue_task_info(target, sig, &info);
    return sched_signal_user_process_info((uint32_t)tgid, sig, &info);
}
