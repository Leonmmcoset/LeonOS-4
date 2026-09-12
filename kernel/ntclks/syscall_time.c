#include <ntclks/syscall_internal.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/futex.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/signal.h>

struct linux_kernel_sigevent {
    uint64_t value;
    int32_t signo;
    int32_t notify;
    int32_t notify_thread_id;
    int32_t reserved;
};

int64_t syscall_itimer(bool setting, int32_t which, uint64_t value, uint64_t old_value)
{
    struct linux_itimerval requested = {0}, old;
    if (setting && value) {
        if (!user_range_ok(value, sizeof(requested))) return -LINUX_EFAULT;
        requested = *(const struct linux_itimerval *)(uintptr_t)value;
        if (requested.it_value.tv_sec < 0 || requested.it_interval.tv_sec < 0 ||
            (uint64_t)requested.it_value.tv_usec >= 1000000 ||
            (uint64_t)requested.it_interval.tv_usec >= 1000000) return -LINUX_EINVAL;
    }
    if (which < LINUX_ITIMER_REAL || which > LINUX_ITIMER_PROF) return -LINUX_EINVAL;
    if (which != LINUX_ITIMER_REAL) return -LINUX_ENOSYS;
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    sched_itimer_task(task, setting ? &requested : NULL, &old);
    uint64_t output = setting ? old_value : value;
    /* Linux commits setitimer before attempting the optional old-value copy. */
    if (!setting || output) {
        if (!user_range_writable(output, sizeof(old))) return -LINUX_EFAULT;
        *(struct linux_itimerval *)(uintptr_t)output = old;
    }
    return 0;
}

static uint64_t timespec_ticks(struct linux_timespec value)
{
    const uint64_t maximum_ns = INT64_MAX;
    uint64_t ns;
    if ((uint64_t)value.tv_sec > maximum_ns / 1000000000ULL)
        ns = maximum_ns;
    else {
        ns = (uint64_t)value.tv_sec * 1000000000ULL;
        ns = maximum_ns - ns < (uint64_t)value.tv_nsec ? maximum_ns : ns + value.tv_nsec;
    }
    const uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
    return ns / tick_ns + (ns % tick_ns != 0);
}

int64_t syscall_nanosleep(int32_t clock, uint32_t flags, uint64_t request, uint64_t remaining)
{
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    if (clock == LINUX_CLOCK_MONOTONIC_RAW || clock == LINUX_CLOCK_REALTIME_COARSE ||
        clock == LINUX_CLOCK_MONOTONIC_COARSE) return -LINUX_EOPNOTSUPP;
    if (clock != LINUX_CLOCK_REALTIME && clock != LINUX_CLOCK_MONOTONIC &&
        clock != LINUX_CLOCK_BOOTTIME) return -LINUX_EINVAL;
    if (flags & ~LINUX_TIMER_ABSTIME) return -LINUX_EINVAL;
    if (!task->nanosleep_deadline) {
        if (!user_range_ok(request, sizeof(struct linux_timespec))) return -LINUX_EFAULT;
        struct linux_timespec value = *(const struct linux_timespec *)(uintptr_t)request;
        if (value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000) return -LINUX_EINVAL;
        uint64_t duration = timespec_ticks(value);
        if (!duration) return 0;
        task->nanosleep_clock = flags & LINUX_TIMER_ABSTIME ? clock : LINUX_CLOCK_MONOTONIC;
        task->nanosleep_deadline = flags & LINUX_TIMER_ABSTIME ? duration : time_ticks() + duration;
        task->nanosleep_remaining = flags & LINUX_TIMER_ABSTIME ? 0 : remaining;
    }
    struct linux_timespec value;
    int ret = time_clock_get(task->nanosleep_clock, &value);
    if (ret < 0) { task->nanosleep_deadline = task->nanosleep_remaining = 0; return ret; }
    uint64_t now = timespec_ticks(value);
    if (now >= task->nanosleep_deadline) {
        task->nanosleep_deadline = task->nanosleep_remaining = 0;
        return 0;
    }
    if (!(sched_task_pending(task) & ~task->blocked_signals))
        sched_sleep_current_until(time_ticks() + task->nanosleep_deadline - now);
    return KERNEL_SYSCALL_BLOCKED;
}

int64_t syscall_timer_create(uint64_t clockid, uint64_t sevp, uint64_t timerid)
{
    struct task *task = sched_current_task();
    struct linux_kernel_sigevent event = {.signo = 14, .notify = 0};
    int32_t id;
    if (!task || !user_range_writable(timerid, sizeof(id))) return -LINUX_EFAULT;
    if (sevp) {
        if (!user_range_ok(sevp, sizeof(event))) return -LINUX_EFAULT;
        event = *(const struct linux_kernel_sigevent *)(uintptr_t)sevp;
    }
    if (event.notify == 1) event.signo = 0;
    if (event.notify != 0 && event.notify != 1 && event.notify != 4)
        return -LINUX_EINVAL;
    if (event.notify != 1 && (event.signo <= 0 || event.signo >= LINUX_NSIG))
        return -LINUX_EINVAL;
    int ret = sched_timer_create(task, (int32_t)clockid, event.notify,
                                 event.signo, event.notify_thread_id,
                                 event.value, &id);
    if (ret < 0) return ret;
    *(int32_t *)(uintptr_t)timerid = id;
    return 0;
}

int64_t syscall_timer_delete(uint64_t timerid)
{
    struct task *task = sched_current_task();
    return task ? sched_timer_delete(task, (int32_t)timerid) : -LINUX_ESRCH;
}

int64_t syscall_timer_settime(uint64_t timerid, uint64_t flags, uint64_t value, uint64_t old_value)
{
    struct task *task = sched_current_task();
    struct linux_itimerspec requested, old;
    if (!task || !user_range_ok(value, sizeof(requested))) return -LINUX_EFAULT;
    requested = *(const struct linux_itimerspec *)(uintptr_t)value;
    int ret = sched_timer_settime(task, (int32_t)timerid, (uint32_t)flags,
                                  &requested, old_value ? &old : NULL);
    if (ret < 0) return ret;
    if (old_value) {
        if (!user_range_writable(old_value, sizeof(old))) return -LINUX_EFAULT;
        *(struct linux_itimerspec *)(uintptr_t)old_value = old;
    }
    return 0;
}

int64_t syscall_timer_gettime(uint64_t timerid, uint64_t value)
{
    struct task *task = sched_current_task();
    struct linux_itimerspec result;
    if (!task || !user_range_writable(value, sizeof(result))) return -LINUX_EFAULT;
    int ret = sched_timer_gettime(task, (int32_t)timerid, &result);
    if (ret < 0) return ret;
    *(struct linux_itimerspec *)(uintptr_t)value = result;
    return 0;
}

int64_t syscall_timer_getoverrun(uint64_t timerid)
{
    struct task *task = sched_current_task();
    return task ? sched_timer_getoverrun(task, (int32_t)timerid) : -LINUX_ESRCH;
}

int64_t syscall_rt_sigtimedwait(uint64_t mask, uint64_t info, uint64_t timeout,
                                uint64_t sigset_size)
{
    struct task *task = sched_current_task();
    uint64_t requested, ticks = 0;
    bool resuming = task && task->sigwait_active;
    if (sigset_size != sizeof(uint64_t)) return -LINUX_EINVAL;
    if (!task) return -LINUX_EFAULT;
    if (!resuming) {
        if (!user_range_ok(mask, sizeof(requested))) return -LINUX_EFAULT;
        __builtin_memcpy(&requested, (const void *)(uintptr_t)mask, sizeof(requested));
        requested &= ~((1ULL << 8) | (1ULL << 18));
        task->sigwait_mask = task->sigwait_deadline = 0;
        if (timeout) {
            struct linux_timespec value;
            if (!user_range_ok(timeout, sizeof(value))) return -LINUX_EFAULT;
            __builtin_memcpy(&value, (const void *)(uintptr_t)timeout, sizeof(value));
            if (value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000LL)
                return -LINUX_EINVAL;
            /* timespec64_to_ktime saturates at KTIME_MAX on Linux. */
            uint64_t seconds = (uint64_t)value.tv_sec;
            uint64_t ns = seconds > INT64_MAX / 1000000000ULL ? INT64_MAX :
                seconds * 1000000000ULL + (uint64_t)value.tv_nsec;
            if (ns > INT64_MAX) ns = INT64_MAX;
            const uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
            ticks = ns / tick_ns + (ns % tick_ns != 0);
        }
    } else requested = task->sigwait_mask;
    struct linux_siginfo result;
    int signal = kernel_signal_dequeue(task, requested, &result);
    if (signal) {
        task->sigwait_deadline = task->sigwait_mask = 0;
        task->sigwait_active = 0;
        if (info) {
            /* Linux copies kernel_siginfo before clearing the expansion.
             * A fault in the latter leaves the first 48 bytes visible. */
            if (!user_range_writable(info, 48)) return -LINUX_EFAULT;
            __builtin_memcpy((void *)(uintptr_t)info, &result, 48);
            if (info > UINT64_MAX - sizeof(result) ||
                !user_range_writable(info + 48, sizeof(result) - 48)) return -LINUX_EFAULT;
            __builtin_memset((void *)(uintptr_t)(info + 48), 0, sizeof(result) - 48);
        }
        return signal;
    }
    if ((!resuming && timeout && !ticks) ||
        (resuming && task->sigwait_deadline && time_ticks() >= task->sigwait_deadline)) {
        task->sigwait_deadline = task->sigwait_mask = 0;
        task->sigwait_active = 0;
        return -LINUX_EAGAIN;
    }
    if (sched_task_pending(task) & ~task->blocked_signals & ~requested) {
        task->sigwait_deadline = task->sigwait_mask = 0;
        task->sigwait_active = 0;
        return -LINUX_EINTR;
    }
    task->sigwait_mask = requested;
    task->sigwait_active = 1;
    if (!resuming && timeout) {
        uint64_t now = time_ticks();
        task->sigwait_deadline = ticks > UINT64_MAX - now ? UINT64_MAX : now + ticks;
    }
    sched_signal_wait_current(task->sigwait_deadline);
    return KERNEL_SYSCALL_BLOCKED;
}
