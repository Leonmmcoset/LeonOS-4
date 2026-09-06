/*
 * LeonOS process syscall handlers: process identity, groups, signals,
 * priorities, and resource limits.
 */
#include <ntclks/sched.h>
#include <ntclks/signal.h>
#include <ntclks/console.h>
#include <ntclks/power.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/version.h>
#include <linux/reboot.h>
#include <linux/utsname.h>
#include <leonos/signal.h>
#include <leonos/system.h>
#include <stdint.h>

int64_t syscall_linux_signal(uint64_t number, uint64_t signal_number,
                             uint64_t action_ptr, uint64_t old_action_ptr,
                             uint64_t mask_ptr, uint64_t sigset_size)
{
    struct task *task = sched_current_task();
    (void)sigset_size;
    if (!task) return -LEONOS_EPERM;

    if (number == LINUX_SYS_RT_SIGSUSPEND) {
        uint64_t requested_mask;
        if (action_ptr != 0 && action_ptr < sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (!signal_number || !user_range_ok(signal_number, sizeof(uint64_t))) {
            return -LEONOS_EFAULT;
        }
        requested_mask = *(const uint64_t *)(uintptr_t)signal_number;
        task->blocked_signals = (uint32_t)requested_mask;
        task->blocked_signals &= ~((1u << 9) | (1u << 17));
        /* The return-to-user path below the syscall dispatcher installs any
         * now-unblocked handler frame before sigsuspend observes EINTR. */
        return -LEONOS_EINTR;
    }

    if (number == LINUX_SYS_RT_SIGPROCMASK) {
        uint64_t set = 0;
        uint64_t old = task->blocked_signals;
        if (mask_ptr != 0 && mask_ptr < sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (action_ptr) {
            if (!user_range_ok(action_ptr, sizeof(uint64_t))) return -LEONOS_EFAULT;
            set = *(const uint64_t *)(uintptr_t)action_ptr;
        }
        if (old_action_ptr) {
            if (!user_range_ok(old_action_ptr, sizeof(uint64_t))) return -LEONOS_EFAULT;
            *(uint64_t *)(uintptr_t)old_action_ptr = old;
        }
        /* Picolibc/libc pass the Linux values directly:
         * SIG_SETMASK=0, SIG_BLOCK=1, SIG_UNBLOCK=2. */
        if (signal_number == 0) task->blocked_signals = (uint32_t)set;
        else if (signal_number == 1) task->blocked_signals |= (uint32_t)set;
        else if (signal_number == 2) task->blocked_signals &= ~(uint32_t)set;
        else return -LEONOS_EINVAL;
        task->blocked_signals &= ~((1u << 9) | (1u << 17));
        return 0;
    }

    if (number == LINUX_SYS_RT_SIGACTION) {
        struct leonos_linux_sigaction request = {0};
        struct kernel_signal_action previous = {0};
        struct leonos_linux_sigaction *old_action;
        int ret;

        if (signal_number == 0 || signal_number >= 32 || signal_number == 9 ||
            signal_number == 17) return -LEONOS_EINVAL;
        if (mask_ptr != 0 && mask_ptr < sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (action_ptr && !user_range_ok(action_ptr, sizeof(request))) return -LEONOS_EFAULT;
        if (old_action_ptr && !user_range_ok(old_action_ptr, sizeof(request))) return -LEONOS_EFAULT;
        if (action_ptr) {
            request = *(const struct leonos_linux_sigaction *)(uintptr_t)action_ptr;
            if (request.handler != 0 && request.handler != 1 &&
                !request.restorer) return -LEONOS_EFAULT;
        }
        ret = kernel_signal_set_action(task, (int)signal_number,
                                       action_ptr ? request.handler : 0,
                                       action_ptr ? request.mask : 0,
                                       action_ptr ? request.flags : 0,
                                       action_ptr ? request.restorer : 0,
                                       old_action_ptr ? &previous : NULL);
        if (ret < 0) return -LEONOS_EINVAL;
        if (old_action_ptr) {
            old_action = (struct leonos_linux_sigaction *)(uintptr_t)old_action_ptr;
            old_action->handler = previous.handler;
            old_action->mask = previous.mask;
            old_action->flags = previous.flags;
            old_action->reserved = 0;
            old_action->restorer = previous.restorer;
        }
        return 0;
    }
    return -LEONOS_ENOSYS;
}

int64_t syscall_process_control(uint64_t number, uint64_t a0,
                                       uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)a3;
    if (number == LINUX_SYS_GETUID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->uid : 0;
    }
    if (number == LINUX_SYS_GETEUID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->euid : 0;
    }
    if (number == LINUX_SYS_GETGID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->gid : 0;
    }
    if (number == LINUX_SYS_GETEGID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->egid : 0;
    }
    if (number == LINUX_SYS_SETUID) {
        struct task *task = sched_current_task();
        uint32_t target = (uint32_t)a0;
        if (!task) return -LEONOS_EPERM;
        /* Only root may assume another identity. A non-root task may only
         * restore its real uid (the identity authd assigned before exec). */
        if (task->uid != 0 && target != task->uid) return -LEONOS_EPERM;
        task->uid = target;
        task->euid = target;
        task->suid = target;
        return 0;
    }
    if (number == LINUX_SYS_SETGID) {
        struct task *task = sched_current_task();
        uint32_t target = (uint32_t)a0;
        if (!task) return -LEONOS_EPERM;
        if (task->uid != 0 && target != task->gid) return -LEONOS_EPERM;
        task->gid = target;
        task->egid = target;
        task->sgid = target;
        return 0;
    }
    if (number == LINUX_SYS_UNAME) {
        struct utsname info = {0};
        const struct leonos_system_info *system = ntclks_system_info();
        if (!a0 || !user_range_ok(a0, sizeof(info))) return -LEONOS_EFAULT;
        {
            uint32_t i;
            for (i = 0; i < sizeof(info.sysname) - 1u && "LeonOS"[i]; ++i) {
                info.sysname[i] = "LeonOS"[i];
            }
            if (system && system->kernel_version[0]) {
                for (i = 0; i < sizeof(info.release) - 1u && system->kernel_version[i]; ++i) {
                    info.release[i] = system->kernel_version[i];
                }
            }
            for (i = 0; i < sizeof(info.version) - 1u && "LeonOS 4"[i]; ++i) {
                info.version[i] = "LeonOS 4"[i];
            }
            for (i = 0; i < sizeof(info.machine) - 1u && "x86_64"[i]; ++i) {
                info.machine[i] = "x86_64"[i];
            }
            for (i = 0; i < sizeof(info.nodename) - 1u && "leonos"[i]; ++i) {
                info.nodename[i] = "leonos"[i];
            }
        }
        *(struct utsname *)(uintptr_t)a0 = info;
        return 0;
    }
    if (number == LINUX_SYS_GETTIMEOFDAY) {
        struct leonos_time_info info;
        uint64_t seconds = 0;
        uint64_t micros;
        if (!a0 || !user_range_ok(a0, 16u)) return -LEONOS_EFAULT;
        if (time_wall_clock(&info) == 0) seconds = info.unix_seconds;
        micros = time_uptime_us() % 1000000ULL;
        ((int64_t *)(uintptr_t)a0)[0] = (int64_t)seconds;
        ((int64_t *)(uintptr_t)a0)[1] = (int64_t)micros;
        if (a1 && !user_range_ok(a1, 8u)) return -LEONOS_EFAULT;
        if (a1) {
            ((int32_t *)(uintptr_t)a1)[0] = 0;
            ((int32_t *)(uintptr_t)a1)[1] = 0;
        }
        return 0;
    }
    if (number == LINUX_SYS_SETTIMEOFDAY) {
        struct task *task = sched_current_task();
        if (!task || task->uid != 0) return -LEONOS_EPERM;
        if (!a0 || !user_range_ok(a0, 16u)) return -LEONOS_EFAULT;
        if (time_set_wall_clock((uint64_t)((const int64_t *)(uintptr_t)a0)[0]) < 0) {
            return -LEONOS_EINVAL;
        }
        return 0;
    }
    if (number == LINUX_SYS_SCHED_GETAFFINITY || number == LINUX_SYS_SCHED_SETAFFINITY) {
        struct task *current = sched_current_task();
        uint64_t mask;
        int ret;
        if (!current || !a2 || !user_range_ok(a2, sizeof(mask))) return -LEONOS_EFAULT;
        if (a1 != sizeof(mask)) return -LEONOS_EINVAL;
        if (number == LINUX_SYS_SCHED_GETAFFINITY) {
            ret = sched_get_task_affinity((uint32_t)a0, &mask);
            if (ret < 0) return ret == -2 ? -LEONOS_ENOENT : -LEONOS_EINVAL;
            *(uint64_t *)(uintptr_t)a2 = mask;
            return 0;
        }
        if (!user_range_ok(a2, sizeof(mask))) return -LEONOS_EFAULT;
        mask = *(const uint64_t *)(uintptr_t)a2;
        ret = sched_set_task_affinity((uint32_t)a0, mask);
        if (ret < 0) return ret == -2 ? -LEONOS_ENOENT : -LEONOS_EPERM;
        return 0;
    }
    if (number == LINUX_SYS_REBOOT) {
        struct task *task = sched_current_task();
        if (!task || task->uid != 0) return -LEONOS_EPERM;
        if (a0 != (uint64_t)RB_AUTOBOOT && a0 != (uint64_t)RB_HALT_SYSTEM &&
            a0 != (uint64_t)RB_POWER_OFF) {
            return -LEONOS_EINVAL;
        }
        console_printf("[ntclks] reboot(2) requested by pid=%u\n", task->pid);
        if (a0 == (uint64_t)RB_AUTOBOOT) power_reboot();
        power_shutdown();
    }
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
