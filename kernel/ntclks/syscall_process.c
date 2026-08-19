/*
 * LeonOS process syscall handlers: process identity, groups, signals,
 * priorities, and resource limits.
 */
#include <ntclks/sched.h>
#include <ntclks/syscall.h>
#include <ntclks/usercopy.h>

int64_t syscall_process_control(uint64_t number, uint64_t a0,
                                       uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)a3;
    if (number == LINUX_SYS_GETPID) {
        return (int64_t)sched_current_pid();
    }
    if (number == LINUX_SYS_GETPPID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->parent_pid : 0;
    }
    if (number == LINUX_SYS_GETPGRP) {
        return sched_get_process_group(0);
    }
    if (number == LINUX_SYS_GETPGID) {
        struct task *current = sched_current_task();
        struct task *target = sched_find((uint32_t)a0);
        if (a0 == 0) {
            return sched_get_process_group(0);
        }
        if (!current || !target ||
            (current->uid != 0 && current->uid != target->uid)) {
            return -LEONOS_EPERM;
        }
        return sched_get_process_group((uint32_t)a0);
    }
    if (number == LINUX_SYS_SETPGID) {
        int result = sched_set_process_group(sched_current_pid(), (uint32_t)a0,
                                             (uint32_t)a1);
        return result == 0 ? 0 : (result == -2 ? -LEONOS_ENOENT : -LEONOS_EPERM);
    }
    if (number == LINUX_SYS_SETSID) {
        int64_t result = sched_create_process_session(sched_current_pid());
        return result > 0 ? result : -LEONOS_EPERM;
    }
    if (number == LINUX_SYS_KILL) {
        int signal_number = (int)a1;
        struct task *current = sched_current_task();
        int32_t requested_pid = (int32_t)a0;
        struct task *target;
        if (!current || requested_pid == -1) {
            return -LEONOS_EINVAL;
        }
        if (requested_pid <= 0) {
            uint32_t process_group = requested_pid == 0
                                         ? current->process_group
                                         : (uint32_t)(-(int64_t)requested_pid);
            int result = sched_signal_process_group(current->pid, process_group,
                                                    signal_number);
            return result >= 0 ? 0 : -LEONOS_EPERM;
        }
        target = sched_find((uint32_t)requested_pid);
        if (!current || !target ||
            ((uint32_t)requested_pid != current->pid && current->uid != 0 &&
             current->uid != target->uid)) {
            return -LEONOS_EPERM;
        }
        return sched_signal_user_task((uint32_t)requested_pid, signal_number) == 0
                   ? 0 : -LEONOS_EINVAL;
    }
    if (number == LINUX_SYS_NICE) {
        struct task *task = sched_current_task();
        int current = task ? task->priority : 0;
        int next = current + (int)a0;
        int priority;
        if (next < -20) next = -20;
        if (next > 19) next = 19;
        priority = sched_task_priority(sched_current_pid(), next, 1);
        if (priority < -20 || priority > 19) {
            return -LEONOS_EINVAL;
        }
        /* A raw syscall cannot return a negative successful value. Encode
         * POSIX's -20..19 priority range as Linux does for getpriority. */
        return priority + 20;
    }
    if (number == LINUX_SYS_GETPRIORITY || number == LINUX_SYS_SETPRIORITY) {
        struct task *current = sched_current_task();
        uint32_t target = (uint32_t)a1;
        if (a0 != 0) {
            return -LEONOS_EINVAL;
        }
        if (target == 0 && current) {
            target = current->pid;
        }
        if (!current || (number == LINUX_SYS_SETPRIORITY &&
                         target != current->pid && current->uid != 0)) {
            return -LEONOS_EPERM;
        }
        {
            int priority = sched_task_priority(target, (int)a2,
                                                number == LINUX_SYS_SETPRIORITY);
            if (priority < -20 || priority > 19) {
                return -LEONOS_ENOENT;
            }
            if (number == LINUX_SYS_SETPRIORITY) {
                return 0;
            }
            /* Preserve the negative-errno syscall convention for callers
             * while exposing the POSIX priority through libc. */
            return priority + 20;
        }
    }
    if (number == LINUX_SYS_GETRLIMIT || number == LINUX_SYS_SETRLIMIT) {
        struct task *task = sched_current_task();
        uint64_t *limit = (uint64_t *)(uintptr_t)a1;
        uint64_t current_limit;
        uint64_t maximum_limit;
        if (!task || !limit || !user_range_ok(a1, 16)) return -LEONOS_EFAULT;
        if (a0 == 5) {
            current_limit = task->rlimit_nofile;
            maximum_limit = SCHED_TASK_FILE_LIMIT;
        } else if (a0 == 6) {
            maximum_limit = NTCLKS_USER_TOP - NTCLKS_USER_BASE;
            current_limit = task->rlimit_as ? task->rlimit_as : maximum_limit;
        } else return -LEONOS_ENOSYS;
        if (number == LINUX_SYS_GETRLIMIT) {
            limit[0] = current_limit;
            limit[1] = maximum_limit;
            return 0;
        }
        if (limit[0] > maximum_limit || limit[1] > maximum_limit ||
            limit[0] > limit[1]) return -LEONOS_EPERM;
        if (a0 == 5) task->rlimit_nofile = limit[0];
        else task->rlimit_as = limit[0] == maximum_limit ? 0 : limit[0];
        return 0;
    }
    return -LEONOS_ENOSYS;
}
