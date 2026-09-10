#include <ntclks/futex.h>
#include <ntclks/sched.h>
#include <ntclks/lock.h>
#include <ntclks/usercopy.h>
#include <ntclks/time.h>
#include <leonos/system.h>
#include <linux/futex.h>
#include <linux/errno.h>
#include <linux/time.h>

static struct kernel_spinlock futex_lock = KERNEL_SPINLOCK_INIT;
static struct task *waiters;

static int futex_key(struct task *task, uint64_t address, uint32_t operation,
                     uint64_t *domain, uint64_t *key)
{
    if (address & 3) return -EINVAL;
    if (!user_range_ok(address, 4)) return -EFAULT;
    if (operation & FUTEX_PRIVATE_FLAG) {
        *domain = sched_task_as(task)->cr3;
        *key = address;
    } else {
        *domain = 0;
        *key = address_space_user_page_phys(sched_task_as(task), address) + (address & 4095);
    }
    return 0;
}

static void dequeue(struct task *task)
{
    for (struct task **p = &waiters; *p; p = &(*p)->futex_next) {
        if (*p != task) continue;
        *p = task->futex_next;
        task->futex_next = NULL;
        return;
    }
}

static bool task_futex_key_matches(const struct task *task, uint64_t domain, uint64_t key)
{
    if (task->futex_waitv_count) {
        for (uint32_t i = 0; i < task->futex_waitv_count; ++i)
            if (task->futex_waitv_domain[i] == domain && task->futex_waitv_key[i] == key)
                return true;
        return false;
    }
    return task->futex_domain == domain && task->futex_key == key;
}

static uint32_t wake(uint64_t domain, uint64_t key, uint32_t count, uint32_t bitset)
{
    uint32_t woken = 0;
    struct task **p = &waiters;
    while (*p && woken < count) {
        struct task *task = *p;
        bool matches = task->futex_waitv_count == 0 &&
                       task->futex_domain == domain && task->futex_key == key &&
                       (task->futex_bitset & bitset);
        if (task->futex_waitv_count) {
            for (uint32_t i = 0; i < task->futex_waitv_count; ++i) {
                if (task->futex_waitv_domain[i] == domain &&
                    task->futex_waitv_key[i] == key) {
                    matches = true;
                    task->futex_waitv_index = i;
                    break;
                }
            }
        }
        if (!matches || (task->futex_deadline && task->futex_deadline <= time_ticks())) {
            p = &task->futex_next;
            continue;
        }
        *p = task->futex_next;
        task->futex_next = NULL;
        task->futex_state = 2;
        sched_mark_ready(task->pid);
        ++woken;
    }
    return woken;
}

void futex_cancel_wait(struct task *task)
{
    uint64_t flags;
    kernel_spin_lock_irqsave(&futex_lock, &flags);
    dequeue(task);
    task->futex_state = 0;
    task->futex_waitv_count = 0;
    kernel_spin_unlock_irqrestore(&futex_lock, flags);
}

static uint64_t timespec_ns(const struct linux_timespec *ts)
{
    uint64_t seconds = (uint64_t)ts->tv_sec;
    if (seconds > INT64_MAX / 1000000000ULL) return INT64_MAX;
    uint64_t value = seconds * 1000000000ULL;
    return (uint64_t)ts->tv_nsec > INT64_MAX - value ? INT64_MAX : value + ts->tv_nsec;
}

static int deadline(uint64_t timeout, uint32_t operation, uint64_t *out)
{
    *out = 0;
    if (!timeout) return 0;
    if (!user_range_ok(timeout, sizeof(struct linux_timespec))) return -EFAULT;
    struct linux_timespec ts = *(const struct linux_timespec *)(uintptr_t)timeout;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000) return -EINVAL;
    uint64_t ns = timespec_ns(&ts);
    uint64_t now = time_ticks();
    const uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
    const uint64_t max_ticks = (INT64_MAX + tick_ns - 1) / tick_ns;
    if ((operation & FUTEX_CMD_MASK) == FUTEX_WAIT) {
        uint64_t delta = (ns + tick_ns - 1) / tick_ns;
        *out = now >= max_ticks || delta > max_ticks - now ? max_ticks : now + delta;
    } else if (operation & FUTEX_CLOCK_REALTIME) {
        struct linux_timespec wall;
        int ret = time_clock_get(LINUX_CLOCK_REALTIME, &wall);
        if (ret) return ret;
        uint64_t wall_ns = timespec_ns(&wall);
        uint64_t delta = ns > wall_ns ? (ns - wall_ns + tick_ns - 1) / tick_ns : 0;
        *out = now >= max_ticks || delta > max_ticks - now ? max_ticks : now + delta;
    } else *out = (ns + tick_ns - 1) / tick_ns;
    return 0;
}

static int atomic_wake_op(uint64_t address, uint32_t encoded)
{
    uint32_t op = (encoded >> 28) & 7;
    int32_t argument = (int32_t)(encoded << 8) >> 20;
    int32_t comparison = (int32_t)(encoded << 20) >> 20;
    /* Linux v6.12 masks even out-of-range shift counts instead of rejecting them. */
    if (encoded & ((uint32_t)FUTEX_OP_OPARG_SHIFT << 28)) argument = (int32_t)(1U << (argument & 31));
    uint32_t *word = (uint32_t *)(uintptr_t)address;
    int32_t old;
    switch (op) {
    case FUTEX_OP_SET: old = (int32_t)__atomic_exchange_n(word, (uint32_t)argument, __ATOMIC_SEQ_CST); break;
    case FUTEX_OP_ADD: old = (int32_t)__atomic_fetch_add(word, (uint32_t)argument, __ATOMIC_SEQ_CST); break;
    case FUTEX_OP_OR: old = (int32_t)__atomic_fetch_or(word, (uint32_t)argument, __ATOMIC_SEQ_CST); break;
    case FUTEX_OP_ANDN: old = (int32_t)__atomic_fetch_and(word, ~(uint32_t)argument, __ATOMIC_SEQ_CST); break;
    case FUTEX_OP_XOR: old = (int32_t)__atomic_fetch_xor(word, (uint32_t)argument, __ATOMIC_SEQ_CST); break;
    default: return -ENOSYS;
    }
    switch ((encoded >> 24) & 15) {
    case FUTEX_OP_CMP_EQ: return old == comparison;
    case FUTEX_OP_CMP_NE: return old != comparison;
    case FUTEX_OP_CMP_LT: return old < comparison;
    case FUTEX_OP_CMP_LE: return old <= comparison;
    case FUTEX_OP_CMP_GT: return old > comparison;
    case FUTEX_OP_CMP_GE: return old >= comparison;
    default: return -ENOSYS;
    }
}

int64_t syscall_futex(uint64_t address, uint64_t operation, uint64_t value,
                      uint64_t timeout, uint64_t address2, uint64_t value3)
{
    struct task *task = sched_current_task();
    uint32_t op = (uint32_t)operation & FUTEX_CMD_MASK;
    uint64_t domain, key, flags;
    if (!task) return -ESRCH;
    if ((operation & FUTEX_CLOCK_REALTIME) && op != FUTEX_WAIT_BITSET) return -ENOSYS;
    if (op != FUTEX_WAIT && op != FUTEX_WAIT_BITSET && op != FUTEX_WAKE &&
        op != FUTEX_WAKE_BITSET && op != FUTEX_REQUEUE && op != FUTEX_CMP_REQUEUE && op != FUTEX_WAKE_OP)
        return -ENOSYS;
    uint32_t bitset = op == FUTEX_WAIT_BITSET || op == FUTEX_WAKE_BITSET
        ? (uint32_t)value3 : FUTEX_BITSET_MATCH_ANY;
    if (!bitset) return -EINVAL;
    /* A wake completes the original wait even if the value was changed or
     * its mapping was removed before this thread next runs. */
    if (op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET) {
        kernel_spin_lock_irqsave(&futex_lock, &flags);
        if (task->futex_state) {
            int64_t result = KERNEL_SYSCALL_BLOCKED;
            if (task->futex_state == 2) result = 0;
            else if (task->futex_deadline && task->futex_deadline <= time_ticks()) result = -ETIMEDOUT;
            else if (sched_task_pending(task) & ~task->blocked_signals) result = -EINTR;
            if (result != KERNEL_SYSCALL_BLOCKED) {
                dequeue(task);
                task->futex_state = 0;
            } else if (task->futex_deadline) sched_sleep_current_until(task->futex_deadline);
            else sched_block_current();
            kernel_spin_unlock_irqrestore(&futex_lock, flags);
            return result;
        }
        kernel_spin_unlock_irqrestore(&futex_lock, flags);
    }
    int ret = futex_key(task, address, (uint32_t)operation, &domain, &key);
    if (ret) return ret;
    if (op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET) {
        uint64_t until;
        ret = deadline(timeout, (uint32_t)operation, &until);
        if (ret) return ret;
        kernel_spin_lock_irqsave(&futex_lock, &flags);
        if (__atomic_load_n((uint32_t *)(uintptr_t)address, __ATOMIC_SEQ_CST) != (uint32_t)value)
            ret = -EAGAIN;
        else if (timeout && until <= time_ticks()) ret = -ETIMEDOUT;
        else {
            task->futex_domain = domain;
            task->futex_key = key;
            task->futex_address = address;
            task->futex_bitset = bitset;
            task->futex_deadline = until;
            task->futex_state = 1;
            task->futex_next = NULL;
            struct task **tail = &waiters;
            while (*tail) tail = &(*tail)->futex_next;
            *tail = task;
            if (until) sched_sleep_current_until(until);
            else sched_block_current();
            ret = KERNEL_SYSCALL_BLOCKED;
        }
        kernel_spin_unlock_irqrestore(&futex_lock, flags);
        return ret;
    }
    uint64_t domain2 = 0, key2 = 0;
    if (op == FUTEX_REQUEUE || op == FUTEX_CMP_REQUEUE) {
        if ((int32_t)value < 0 || (int32_t)timeout < 0) return -EINVAL;
        ret = futex_key(task, address2, (uint32_t)operation, &domain2, &key2);
        if (ret) return ret;
    } else if (op == FUTEX_WAKE_OP) {
        if (address2 & 3) return -EINVAL;
        /* Resolve COW before deriving the shared physical key. */
        if (!user_range_writable(address2, 4)) return -EFAULT;
        ret = futex_key(task, address2, (uint32_t)operation, &domain2, &key2);
        if (ret) return ret;
    }
    kernel_spin_lock_irqsave(&futex_lock, &flags);
    if (op == FUTEX_WAKE_OP) {
        ret = atomic_wake_op(address2, (uint32_t)value3);
        if (ret >= 0) {
            int matched = ret;
            ret = (int)wake(domain, key, (int32_t)value > 0 ? (uint32_t)value : 1, bitset);
            if (matched) ret += (int)wake(domain2, key2, (int32_t)timeout > 0 ? (uint32_t)timeout : 1, bitset);
        }
        kernel_spin_unlock_irqrestore(&futex_lock, flags);
        return ret;
    }
    if (op == FUTEX_CMP_REQUEUE &&
        __atomic_load_n((uint32_t *)(uintptr_t)address, __ATOMIC_SEQ_CST) != (uint32_t)value3) {
        kernel_spin_unlock_irqrestore(&futex_lock, flags);
        return -EAGAIN;
    }
    /* Classic wake (unlike requeue) wakes one waiter even for a nonpositive count. */
    uint32_t count = (op == FUTEX_WAKE || op == FUTEX_WAKE_BITSET) && (int32_t)value <= 0
        ? 1 : (uint32_t)value;
    ret = (int)wake(domain, key, count, bitset);
    if (op == FUTEX_REQUEUE || op == FUTEX_CMP_REQUEUE) {
        uint32_t moved = 0;
        for (struct task *p = waiters; p && moved < (uint32_t)timeout; p = p->futex_next) {
            if (p->futex_domain == domain && p->futex_key == key) {
                p->futex_domain = domain2;
                p->futex_key = key2;
                ++moved;
            }
        }
        ret += (int)moved;
    }
    kernel_spin_unlock_irqrestore(&futex_lock, flags);
    return ret;
}

/**
 * @brief Translate the Linux 6.12 supported futex2 flags to queue flags.
 * @param value Native unsigned-int flags; only U32 and PRIVATE are supported by Linux.
 * @param operation Output classic queue flags.
 * @return Zero or -EINVAL for unsupported size/flags.
 */
static int futex2_flags(uint32_t value, uint32_t *operation)
{
    if (value & ~(FUTEX2_SIZE_MASK | FUTEX2_PRIVATE))
        return -EINVAL;
    if ((value & FUTEX2_SIZE_MASK) != FUTEX2_SIZE_U32)
        return -EINVAL;
    *operation = (value & FUTEX2_PRIVATE) ? FUTEX_PRIVATE_FLAG : 0;
    return 0;
}

/**
 * @brief Wake matching waiters, preserving futex2's strict zero-count behavior.
 * @param address Aligned user futex address; validated even for count zero.
 * @param mask Native unsigned-long mask, restricted to nonzero U32.
 * @param count Native int wake limit (negative counts wake one, as in Linux 6.12).
 * @param flags Native unsigned-int futex2 flags.
 * @return Woken count or negative errno.
 */
int64_t syscall_futex_wake2(uint64_t address, uint64_t mask, uint64_t count,
                            uint64_t flags)
{
    uint32_t operation;
    int ret = futex2_flags(flags, &operation);
    if (ret) return ret;
    if (mask > UINT32_MAX || !mask) return -EINVAL;
    struct task *task = sched_current_task();
    uint64_t domain, key, irq_flags;
    if (!task) return -ESRCH;
    ret = futex_key(task, address, operation, &domain, &key);
    if (ret) return ret;
    int32_t nr = (int32_t)count;
    kernel_spin_lock_irqsave(&futex_lock, &irq_flags);
    ret = (int)wake(domain, key, nr < 0 ? 1u : (uint32_t)nr, (uint32_t)mask);
    kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
    return ret;
}

/**
 * @brief Wait with a futex2 mask and optional absolute Linux clock deadline.
 * @param address Aligned user futex address.
 * @param value Expected U32 value carried in a native unsigned long.
 * @param mask Nonzero U32 wake mask carried in a native unsigned long.
 * @param flags Native unsigned-int futex2 flags.
 * @param timeout Optional user __kernel_timespec pointer; never modified.
 * @param clockid Native clockid_t; ignored without a timeout.
 * @return Zero, negative errno, or KERNEL_SYSCALL_BLOCKED while queued.
 */
int64_t syscall_futex_wait2(uint64_t address, uint64_t value, uint64_t mask,
                            uint64_t flags, uint64_t timeout, uint64_t clockid)
{
    uint32_t operation;
    int ret = futex2_flags(flags, &operation);
    if (ret) return ret;
    if (value > UINT32_MAX || mask > UINT32_MAX) return -EINVAL;
    if (timeout) {
        int32_t clock = (int32_t)clockid;
        if (clock != LINUX_CLOCK_REALTIME && clock != LINUX_CLOCK_MONOTONIC)
            return -EINVAL;
        if (clock == LINUX_CLOCK_REALTIME) operation |= FUTEX_CLOCK_REALTIME;
        struct task *task = sched_current_task();
        if (!task || !task->futex_state) {
            uint64_t until;
            ret = deadline(timeout, FUTEX_WAIT_BITSET | operation, &until);
            if (ret) return ret;
        }
    }
    return syscall_futex(address, FUTEX_WAIT_BITSET | operation, value, timeout, 0, mask);
}

/**
 * @brief Copy and validate one futex_waitv descriptor before deriving queue keys.
 * @param address User descriptor address.
 * @param waiter Output snapshot, owned by the caller.
 * @param operation Output classic queue flags.
 * @return Zero, -EFAULT for inaccessible input, or -EINVAL for invalid fields.
 */
static int futex2_waiter(uint64_t address, struct futex_waitv *waiter, uint32_t *operation)
{
    if (!user_range_ok(address, sizeof(*waiter))) return -EFAULT;
    __builtin_memcpy(waiter, (const void *)(uintptr_t)address, sizeof(*waiter));
    if (waiter->__reserved || waiter->val > UINT32_MAX)
        return -EINVAL;
    return futex2_flags(waiter->flags, operation);
}

/**
 * @brief Atomically compare, wake, and move waiters between independent futex keys.
 * @param waiters_ptr User array of exactly two futex_waitv descriptors.
 * @param flags Native unsigned-int reserved flags, must be zero.
 * @param wake_count Native nonnegative int wake limit.
 * @param requeue_count Native nonnegative int requeue limit.
 * @return Total woken/moved or negative errno without queue changes on validation failure.
 */
int64_t syscall_futex_requeue2(uint64_t waiters_ptr, uint64_t flags,
                               uint64_t wake_count, uint64_t requeue_count)
{
    struct futex_waitv entries[2];
    uint32_t source_op, destination_op;
    if ((uint32_t)flags || !waiters_ptr) return -EINVAL;
    int ret = futex2_waiter(waiters_ptr, &entries[0], &source_op);
    if (ret) return ret;
    ret = futex2_waiter(waiters_ptr + sizeof(entries[0]), &entries[1], &destination_op);
    if (ret) return ret;
    wake_count = (uint32_t)wake_count;
    requeue_count = (uint32_t)requeue_count;
    if ((int32_t)wake_count < 0 || (int32_t)requeue_count < 0) return -EINVAL;
    struct task *task = sched_current_task();
    uint64_t domain1, key1, domain2, key2, irq_flags;
    if (!task) return -ESRCH;
    ret = futex_key(task, entries[0].uaddr, source_op, &domain1, &key1);
    if (ret) return ret;
    ret = futex_key(task, entries[1].uaddr, destination_op, &domain2, &key2);
    if (ret) return ret;
    kernel_spin_lock_irqsave(&futex_lock, &irq_flags);
    if (__atomic_load_n((uint32_t *)(uintptr_t)entries[0].uaddr, __ATOMIC_SEQ_CST) !=
        (uint32_t)entries[0].val) {
        kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
        return -EAGAIN;
    }
    ret = (int)wake(domain1, key1, (uint32_t)wake_count, FUTEX_BITSET_MATCH_ANY);
    uint32_t moved = 0;
    for (struct task *p = waiters; p && moved < (uint32_t)requeue_count;
         p = p->futex_next) {
        if (task_futex_key_matches(p, domain1, key1)) {
            if (p->futex_waitv_count) {
                for (uint32_t i = 0; i < p->futex_waitv_count; ++i) {
                    if (p->futex_waitv_domain[i] == domain1 && p->futex_waitv_key[i] == key1) {
                        p->futex_waitv_domain[i] = domain2;
                        p->futex_waitv_key[i] = key2;
                    }
                }
            } else {
                p->futex_domain = domain2;
                p->futex_key = key2;
            }
            ++moved;
        }
    }
    kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
    return ret + (int)moved;
}

int64_t syscall_futex_waitv(uint64_t waiters_ptr, uint64_t count, uint64_t flags,
                            uint64_t timeout, uint64_t clockid)
{
    struct task *task = sched_current_task();
    struct futex_waitv entries[SCHED_FUTEX_WAITV_MAX];
    uint64_t domains[SCHED_FUTEX_WAITV_MAX], keys[SCHED_FUTEX_WAITV_MAX];
    uint64_t until = 0, irq_flags;
    uint32_t operation = FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG;
    if (!task) return -ESRCH;
    if (flags || !count || count > SCHED_FUTEX_WAITV_MAX) return -EINVAL;
    if (timeout && clockid != LINUX_CLOCK_MONOTONIC && clockid != LINUX_CLOCK_REALTIME)
        return -EINVAL;
    if (task->futex_state) {
        kernel_spin_lock_irqsave(&futex_lock, &irq_flags);
        int64_t result = KERNEL_SYSCALL_BLOCKED;
        if (task->futex_state == 2) {
            result = task->futex_waitv_count ? (int64_t)task->futex_waitv_index : 0;
        }
        else if (task->futex_deadline && task->futex_deadline <= time_ticks()) result = -ETIMEDOUT;
        else if (sched_task_pending(task) & ~task->blocked_signals) result = -EINTR;
        if (result != KERNEL_SYSCALL_BLOCKED) {
            dequeue(task);
            task->futex_state = 0;
            task->futex_waitv_count = 0;
        } else if (task->futex_deadline) sched_sleep_current_until(task->futex_deadline);
        else sched_block_current();
        kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
        return result;
    }
    if (count > UINT64_MAX / sizeof(struct futex_waitv) ||
        !user_range_ok(waiters_ptr, count * sizeof(struct futex_waitv))) return -EFAULT;
    __builtin_memcpy(entries, (const void *)(uintptr_t)waiters_ptr,
                     count * sizeof(struct futex_waitv));
    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].__reserved || entries[i].val > UINT32_MAX) return -EINVAL;
        uint32_t entry_operation;
        if (futex2_flags(entries[i].flags, &entry_operation) < 0) return -EINVAL;
        int ret = futex_key(task, entries[i].uaddr, entry_operation, &domains[i], &keys[i]);
        if (ret) return ret;
    }
    if (timeout) {
        if (clockid == LINUX_CLOCK_REALTIME) operation |= FUTEX_CLOCK_REALTIME;
        int ret = deadline(timeout, operation, &until);
        if (ret) return ret;
    }
    kernel_spin_lock_irqsave(&futex_lock, &irq_flags);
    for (uint32_t i = 0; i < count; ++i) {
        if (__atomic_load_n((uint32_t *)(uintptr_t)entries[i].uaddr, __ATOMIC_SEQ_CST) !=
            (uint32_t)entries[i].val) {
            kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
            return -EAGAIN;
        }
    }
    if (timeout && until <= time_ticks()) {
        kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
        return -ETIMEDOUT;
    }
    task->futex_waitv_count = (uint32_t)count;
    task->futex_waitv_index = 0;
    for (uint32_t i = 0; i < count; ++i) {
        task->futex_waitv_domain[i] = domains[i];
        task->futex_waitv_key[i] = keys[i];
    }
    task->futex_deadline = until;
    task->futex_state = 1;
    task->futex_next = waiters;
    waiters = task;
    if (until) sched_sleep_current_until(until); else sched_block_current();
    kernel_spin_unlock_irqrestore(&futex_lock, irq_flags);
    return KERNEL_SYSCALL_BLOCKED;
}

static uint32_t *task_word(struct task *task, uint64_t address)
{
    if (address < NTCLKS_USER_BASE || (address & 3) || address >= NTCLKS_USER_TOP) return NULL;
    if (!address_space_user_page_writable(sched_task_as(task), address) &&
        !address_space_handle_cow_fault(sched_task_as(task), address)) return NULL;
    uint64_t phys = address_space_user_page_phys(sched_task_as(task), address);
    return phys ? (uint32_t *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + (address & 4095)) : NULL;
}

static bool task_read(struct task *task, uint64_t address, void *out, uint32_t size)
{
    if (address < NTCLKS_USER_BASE || address >= NTCLKS_USER_TOP || size > NTCLKS_USER_TOP - address) return false;
    for (uint32_t i = 0; i < size;) {
        if (!address_space_user_page_readable(sched_task_as(task), address + i)) return false;
        uint64_t phys = address_space_user_page_phys(sched_task_as(task), address + i);
        if (!phys) return false;
        uint32_t offset = (uint32_t)((address + i) & 4095);
        uint32_t count = 4096 - offset;
        if (count > size - i) count = size - i;
        const uint8_t *source = (const uint8_t *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + offset);
        for (uint32_t j = 0; j < count; ++j) ((uint8_t *)out)[i + j] = source[j];
        i += count;
    }
    return true;
}

static void owner_died(struct task *task, uint64_t address, bool pending)
{
    uint32_t *word = task_word(task, address);
    if (!word) return;
    uint32_t value = __atomic_load_n(word, __ATOMIC_SEQ_CST);
    if (pending && !(value & FUTEX_TID_MASK)) {
        uint64_t flags;
        kernel_spin_lock_irqsave(&futex_lock, &flags);
        uint64_t key = address_space_user_page_phys(sched_task_as(task), address) + (address & 4095);
        wake(0, key, 1, FUTEX_BITSET_MATCH_ANY);
        kernel_spin_unlock_irqrestore(&futex_lock, flags);
        return;
    }
    while ((value & FUTEX_TID_MASK) == task->pid) {
        uint32_t replacement = (value & FUTEX_WAITERS) | FUTEX_OWNER_DIED;
        if (__atomic_compare_exchange_n(word, &value, replacement, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            if (value & FUTEX_WAITERS) {
                uint64_t flags;
                kernel_spin_lock_irqsave(&futex_lock, &flags);
                uint64_t key = address_space_user_page_phys(sched_task_as(task), address) + (address & 4095);
                wake(0, key, 1, FUTEX_BITSET_MATCH_ANY);
                kernel_spin_unlock_irqrestore(&futex_lock, flags);
            }
            return;
        }
    }
}

static void robust_exit(struct task *task)
{
    struct linux_robust_list_head head;
    if (!task_read(task, task->robust_list, &head, sizeof(head))) return;
    uint64_t pending = head.list_op_pending & ~1ULL;
    uint64_t entry = head.next;
    for (uint32_t limit = 0; limit < 2048 && (entry & ~1ULL) != task->robust_list; ++limit) {
        uint64_t next, address = entry & ~1ULL;
        if (!task_read(task, address, &next, sizeof(next))) break;
        if (address != pending && !(entry & 1)) owner_died(task, address + head.futex_offset, false);
        entry = next;
    }
    if (pending && !(head.list_op_pending & 1)) owner_died(task, pending + head.futex_offset, true);
    task->robust_list = 0;
}

/**
 * @brief Clear a registered TID like Linux put_user, including unaligned words.
 * The write can cross physical pages; futex alignment constrains the subsequent
 * wake operation, not this write. Invalid or read-only pointers are ignored.
 */
static bool clear_registered_tid(struct task *task, uint64_t address)
{
    if (address < NTCLKS_USER_BASE || address > NTCLKS_USER_TOP - 4) return false;
    struct address_space *as = sched_task_as(task);
    for (uint64_t page = address & ~4095ULL; page <= ((address + 3) & ~4095ULL); page += 4096) {
        if (!address_space_user_page_writable(as, page) && !address_space_handle_cow_fault(as, page))
            return false;
        if (!address_space_user_page_phys(as, page)) return false;
    }
    if (!(address & 3)) {
        uint32_t *word = task_word(task, address);
        if (!word) return false;
        __atomic_store_n(word, 0, __ATOMIC_SEQ_CST);
    } else {
        for (unsigned i = 0; i < 4; ++i) {
            uint64_t phys = address_space_user_page_phys(as, address + i);
            *(uint8_t *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + ((address + i) & 4095)) = 0;
        }
    }
    return true;
}

void futex_task_exit(struct task *task)
{
    uint64_t flags;
    robust_exit(task);
    /* mm_release only signals a clear_child_tid while another mm user exists. */
    bool cleared = task->shared_mm && task->shared_mm->references > 1 &&
        clear_registered_tid(task, task->clear_child_tid);
    kernel_spin_lock_irqsave(&futex_lock, &flags);
    dequeue(task);
    task->futex_state = 0;
    task->futex_waitv_count = 0;
    if (cleared && !(task->clear_child_tid & 3)) {
        uint64_t key = address_space_user_page_phys(sched_task_as(task), task->clear_child_tid) +
            (task->clear_child_tid & 4095);
        wake(0, key, 1, FUTEX_BITSET_MATCH_ANY);
    }
    task->clear_child_tid = 0;
    kernel_spin_unlock_irqrestore(&futex_lock, flags);
}
