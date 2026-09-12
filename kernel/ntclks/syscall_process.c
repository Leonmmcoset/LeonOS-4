/*
 * LeonOS process syscall handlers: process identity, groups, signals,
 * priorities, and resource limits.
 */
#include <ntclks/sched.h>
#include <ntclks/signal.h>
#include <ntclks/console.h>
#include <ntclks/power.h>
#include <ntclks/mm.h>
#include <ntclks/osmlayer.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/time.h>
#include <ntclks/usercopy.h>
#include <ntclks/version.h>
#include <ntclks/uts.h>
#include <ntclks/arch.h>
#include <ntclks/smp.h>
#include <linux/arch_prctl.h>
#include <linux/reboot.h>
#include <linux/utsname.h>
#include <linux/futex.h>
#include <linux/sched.h>
#include <linux/rseq.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <leonos/signal.h>
#include <linux/signal.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <leonos/auth.h>
#include <leonos/system.h>
#include <stdint.h>


#define LEONOS_MEMBARRIER_SUPPORTED \
    (MEMBARRIER_CMD_GLOBAL | MEMBARRIER_CMD_GLOBAL_EXPEDITED | \
     MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED | MEMBARRIER_CMD_PRIVATE_EXPEDITED | \
     MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED | MEMBARRIER_CMD_GET_REGISTRATIONS | \
     MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE | MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE)

/**
 * @brief Implement Linux get/set/prlimit ordering and process-wide limit updates.
 * Input is captured before an overlapping old-limit output is written. Linux
 * commits the update before reporting an old-limit copy fault; preserve that.
 */
static int64_t process_resource_limit(uint64_t number, uint64_t a0, uint64_t a1,
                                      uint64_t a2, uint64_t a3)
{
    struct task *caller = sched_current_task();
    bool combined = number == LINUX_SYS_PRLIMIT64;
    uint32_t resource = (uint32_t)(combined ? a1 : a0);
    uint64_t new_pointer = combined ? a2 : number == LINUX_SYS_SETRLIMIT ? a1 : 0;
    uint64_t old_pointer = combined ? a3 : number == LINUX_SYS_GETRLIMIT ? a1 : 0;
    bool setting = new_pointer || number == LINUX_SYS_SETRLIMIT;
    struct linux_rlimit64 next, previous;
    if (setting) {
        if (!user_range_ok(new_pointer, sizeof(next))) return -LINUX_EFAULT;
        /* User buffers need not have natural alignment. */
        for (uint32_t i = 0; i < sizeof(next); ++i)
            ((uint8_t *)&next)[i] = ((const uint8_t *)(uintptr_t)new_pointer)[i];
    }
    struct task *target = combined && (uint32_t)a0 ? sched_find((uint32_t)a0) : caller;
    if (!target) return -LINUX_ESRCH;
    if (combined && caller != target && !(caller->cap_effective & (1ULL << CAP_SYS_RESOURCE)) &&
        (caller->uid != target->uid || caller->uid != target->euid || caller->uid != target->suid ||
         caller->gid != target->gid || caller->gid != target->egid || caller->gid != target->sgid))
        return -LINUX_EPERM;
    if (resource >= LINUX_RLIM_NLIMITS) return -LINUX_EINVAL;
    if (setting && next.rlim_cur > next.rlim_max) return -LINUX_EINVAL;
    if (setting && resource == LINUX_RLIMIT_NOFILE && next.rlim_max > SCHED_NR_OPEN)
        return -LINUX_EPERM;
    struct task_rlimit_state *limits = sched_task_limits(target);
    struct linux_rlimit64 *stored;
    if (resource == LINUX_RLIMIT_NOFILE) stored = &limits->nofile;
    else if (resource == LINUX_RLIMIT_AS) stored = &limits->as;
    else if (resource == LINUX_RLIMIT_SIGPENDING) stored = &limits->sigpending;
    else if (resource == LINUX_RLIMIT_STACK) stored = &limits->stack;
    else if (resource == LINUX_RLIMIT_NPROC) stored = &limits->nproc;
    /* Core-file generation is unavailable, as on Linux CONFIG_COREDUMP=n.
     * The process limit still has normal query, update and inheritance rules. */
    else if (resource == LINUX_RLIMIT_CORE) stored = &limits->core;
    else return -LINUX_ENOSYS;
    if (setting && next.rlim_max > stored->rlim_max &&
        !(caller->cap_effective & (1ULL << CAP_SYS_RESOURCE))) return -LINUX_EPERM;
    previous = *stored;
    if (setting) *stored = next;
    if (old_pointer || number == LINUX_SYS_GETRLIMIT) {
        if (!user_range_writable(old_pointer, sizeof(previous))) return -LINUX_EFAULT;
        for (uint32_t i = 0; i < sizeof(previous); ++i)
            ((uint8_t *)(uintptr_t)old_pointer)[i] = ((const uint8_t *)&previous)[i];
    }
    return 0;
}

/* Linux v6.12 security/commoncap.c:cap_emulate_setxuid. */
static void process_commit_uids(struct task *task, uint32_t real, uint32_t effective,
                                 uint32_t saved)
{
    uint64_t permitted = task->cap_permitted;
    uint64_t caps = task->cap_effective;
    if (!(task->securebits & SECBIT_NO_SETUID_FIXUP)) {
        if ((!task->uid || !task->euid || !task->suid) && real && effective && saved) {
            if (!(task->securebits & SECBIT_KEEP_CAPS)) permitted = caps = 0;
            task->cap_ambient = 0;
        }
        if (!task->euid && effective) caps = 0;
        if (task->euid && !effective) caps = permitted;
    }
    if (task->uid != real)
        task->nproc_exceeded = real && sched_user_task_count(real) > sched_task_limits(task)->nproc.rlim_cur;
    task_credentials_prepare(task, effective, task->egid, effective, task->fsgid, permitted);
    task->uid = real;
    task->euid = task->fsuid = effective;
    task->suid = saved;
    task->cap_permitted = permitted;
    task->cap_effective = caps;
}

static void process_commit_gids(struct task *task, uint32_t real, uint32_t effective,
                                 uint32_t saved)
{
    task_credentials_prepare(task, task->euid, effective, task->fsuid, effective,
                              task->cap_permitted);
    task->gid = real;
    task->egid = task->fsgid = effective;
    task->sgid = saved;
}

int64_t syscall_linux_signal(uint64_t number, uint64_t signal_number,
                             uint64_t action_ptr, uint64_t old_action_ptr,
                             uint64_t mask_ptr, uint64_t sigset_size)
{
    struct task *task = sched_current_task();
    (void)sigset_size;
    if (!task) return -LEONOS_EPERM;

    if (number == LINUX_SYS_SIGALTSTACK) {
        struct linux_sigaltstack old = {task->signal_stack_base, 2, 0, task->signal_stack_size};
        bool on_stack = task->signal_stack_size && task->frame.rsp >= task->signal_stack_base &&
            task->frame.rsp - task->signal_stack_base < task->signal_stack_size;
        if (task->signal_stack_size) old.flags = task->signal_stack_flags | (on_stack ? 1 : 0);
        struct linux_sigaltstack requested;
        if (signal_number) {
            if (!user_range_ok(signal_number, sizeof(requested))) return -LEONOS_EFAULT;
            requested = *(const struct linux_sigaltstack *)(uintptr_t)signal_number;
            if (on_stack) return -LEONOS_EPERM;
            if ((uint32_t)requested.flags & ~(2u | 0x80000000u)) return -LEONOS_EINVAL;
            if (!(requested.flags & 2)) {
                if (requested.size < 2048) return -LEONOS_ENOMEM;
                if (requested.sp >= NTCLKS_USER_TOP || requested.size > NTCLKS_USER_TOP - requested.sp)
                    return -LEONOS_EINVAL;
            }
        }
        if (action_ptr && !user_range_writable(action_ptr, sizeof(old))) return -LEONOS_EFAULT;
        if (signal_number) {
            task->signal_stack_base = requested.flags & 2 ? 0 : requested.sp;
            task->signal_stack_size = requested.flags & 2 ? 0 : requested.size;
            task->signal_stack_flags = (uint32_t)requested.flags & 0x80000000u;
        }
        if (action_ptr) *(struct linux_sigaltstack *)(uintptr_t)action_ptr = old;
        return 0;
    }
    if (number == LINUX_SYS_RT_SIGPENDING) {
        if (action_ptr != 8) return -LEONOS_EINVAL;
        if (!user_range_writable(signal_number, 8)) return -LEONOS_EFAULT;
        *(uint64_t *)(uintptr_t)signal_number = sched_task_pending(task) & task->blocked_signals;
        return 0;
    }
    if (number == LINUX_SYS_RT_SIGSUSPEND) {
        uint64_t requested_mask;
        if (action_ptr != sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (!signal_number || !user_range_ok(signal_number, sizeof(uint64_t))) {
            return -LEONOS_EFAULT;
        }
        requested_mask = *(const uint64_t *)(uintptr_t)signal_number;
        task->sigsuspend_saved_mask = task->blocked_signals;
        task->sigsuspend_active = 1;
        task->blocked_signals = requested_mask;
        task->blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
        if (!(sched_task_pending(task) & ~task->blocked_signals)) sched_block_current();
        return -LEONOS_EINTR;
    }

    if (number == LINUX_SYS_RT_SIGPROCMASK) {
        uint64_t set = 0;
        uint64_t old = task->blocked_signals;
        if (mask_ptr != sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (action_ptr) {
            if (!user_range_ok(action_ptr, sizeof(uint64_t))) return -LEONOS_EFAULT;
            set = *(const uint64_t *)(uintptr_t)action_ptr;
        }
        if (old_action_ptr) {
            if (!user_range_writable(old_action_ptr, sizeof(uint64_t))) return -LEONOS_EFAULT;
            *(uint64_t *)(uintptr_t)old_action_ptr = old;
        }
        /* Linux: SIG_BLOCK=0, SIG_UNBLOCK=1, SIG_SETMASK=2. */
        if (!action_ptr) return 0;
        if (signal_number == 0) task->blocked_signals |= set;
        else if (signal_number == 1) task->blocked_signals &= ~set;
        else if (signal_number == 2) task->blocked_signals = set;
        else return -LEONOS_EINVAL;
        task->blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
        return 0;
    }

    if (number == LINUX_SYS_RT_SIGACTION) {
        struct leonos_linux_sigaction request = {0};
        struct kernel_signal_action previous = {0};
        struct leonos_linux_sigaction *old_action;
        int ret;

        if (signal_number == 0 || signal_number >= LINUX_NSIG || signal_number == 9 ||
            signal_number == 19) return -LEONOS_EINVAL;
        if (mask_ptr != sizeof(uint64_t)) return -LEONOS_EINVAL;
        if (action_ptr && !user_range_ok(action_ptr, sizeof(request))) return -LEONOS_EFAULT;
        if (old_action_ptr && !user_range_writable(old_action_ptr, sizeof(request))) return -LEONOS_EFAULT;
        if (action_ptr) {
            request = *(const struct leonos_linux_sigaction *)(uintptr_t)action_ptr;
            if (request.handler != 0 && request.handler != 1 &&
                !request.restorer) return -LEONOS_EFAULT;
        }
        previous = sched_task_actions(task)[signal_number];
        ret = action_ptr ? kernel_signal_set_action(task, (int)signal_number,
                                       request.handler, request.mask, request.flags,
                                       request.restorer, NULL) : 0;
        if (ret < 0) return -LEONOS_EINVAL;
        if (old_action_ptr) {
            old_action = (struct leonos_linux_sigaction *)(uintptr_t)old_action_ptr;
            old_action->handler = previous.handler;
            old_action->mask = previous.mask;
            old_action->flags = previous.flags;
            old_action->restorer = previous.restorer;
        }
        return 0;
    }
    return -LEONOS_ENOSYS;
}

int64_t syscall_process_prctl(uint64_t option, uint64_t arg2, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5)
{
    struct task *task = sched_current_task();
    if (!task) return -LINUX_ESRCH;
    switch ((uint32_t)option) {
    case LINUX_PR_SET_NAME: {
        char name[16] = {0};
        for (unsigned i = 0; i < sizeof(name) - 1; ++i) {
            if (!user_range_ok(arg2 + i, 1)) return -LEONOS_EFAULT;
            name[i] = *(const char *)(uintptr_t)(arg2 + i);
            if (!name[i]) break;
        }
        __builtin_memset(task->name_storage, 0, sizeof(task->name_storage));
        __builtin_memcpy(task->name_storage, name, sizeof(name));
        task->name = task->name_storage;
        return 0;
    }
    case LINUX_PR_GET_NAME:
        if (!user_range_writable(arg2, 16)) return -LEONOS_EFAULT;
        __builtin_memcpy((void *)(uintptr_t)arg2, task->name_storage, 16);
        return 0;
    case LINUX_PR_GET_DUMPABLE:
        return !sched_task_mm(task)->nondumpable;
    case LINUX_PR_SET_DUMPABLE:
        if (arg2 > 1) return -LEONOS_EINVAL;
        sched_task_mm(task)->nondumpable = !arg2;
        return 0;
    case LINUX_PR_SET_NO_NEW_PRIVS:
        if (arg2 != 1 || arg3 || arg4 || arg5) return -LEONOS_EINVAL;
        task->no_new_privs = true;
        return 0;
    case LINUX_PR_GET_NO_NEW_PRIVS:
        if (arg2 || arg3 || arg4 || arg5) return -LEONOS_EINVAL;
        return task->no_new_privs;
    case LINUX_PR_CAPBSET_READ:
        if (arg2 > CAP_LAST_CAP) return -LINUX_EINVAL;
        return !!(task->cap_bset & (1ULL << arg2));
    case LINUX_PR_CAPBSET_DROP:
        if (!(task->cap_effective & (1ULL << CAP_SETPCAP))) return -LINUX_EPERM;
        if (arg2 > CAP_LAST_CAP) return -LINUX_EINVAL;
        task->cap_bset &= ~(1ULL << arg2);
        return 0;
    case LINUX_PR_GET_SECUREBITS:
        return task->securebits;
    case LINUX_PR_SET_SECUREBITS:
        if ((((task->securebits & SECURE_ALL_LOCKS) >> 1) & (task->securebits ^ arg2)) ||
            (task->securebits & SECURE_ALL_LOCKS & ~arg2) ||
            (arg2 & ~(uint64_t)(SECURE_ALL_BITS | SECURE_ALL_LOCKS)) ||
            !(task->cap_effective & (1ULL << CAP_SETPCAP))) return -LINUX_EPERM;
        task->securebits = (uint32_t)arg2;
        return 0;
    case LINUX_PR_GET_KEEPCAPS:
        return !!(task->securebits & SECBIT_KEEP_CAPS);
    case LINUX_PR_SET_KEEPCAPS:
        if (arg2 > 1) return -LINUX_EINVAL;
        if (task->securebits & SECBIT_KEEP_CAPS_LOCKED) return -LINUX_EPERM;
        if (arg2) task->securebits |= SECBIT_KEEP_CAPS;
        else task->securebits &= ~SECBIT_KEEP_CAPS;
        return 0;
    case LINUX_PR_CAP_AMBIENT:
        if (arg2 == LINUX_PR_CAP_AMBIENT_CLEAR_ALL) {
            if (arg3 || arg4 || arg5) return -LINUX_EINVAL;
            task->cap_ambient = 0;
            return 0;
        }
        if (arg3 > CAP_LAST_CAP || arg4 || arg5) return -LINUX_EINVAL;
        if (arg2 == LINUX_PR_CAP_AMBIENT_IS_SET) return !!(task->cap_ambient & (1ULL << arg3));
        if (arg2 == LINUX_PR_CAP_AMBIENT_LOWER) {
            task->cap_ambient &= ~(1ULL << arg3);
            return 0;
        }
        if (arg2 != LINUX_PR_CAP_AMBIENT_RAISE) return -LINUX_EINVAL;
        if (!(task->cap_permitted & task->cap_inheritable & (1ULL << arg3)) ||
            (task->securebits & SECBIT_NO_CAP_AMBIENT_RAISE)) return -LINUX_EPERM;
        task->cap_ambient |= 1ULL << arg3;
        return 0;
    default:
        return -LEONOS_EINVAL;
    }
}

int64_t syscall_process_control(uint64_t number, uint64_t a0,
                                       uint64_t a1, uint64_t a2, uint64_t a3)
{
    if (number == LINUX_SYS_CAPGET || number == LINUX_SYS_CAPSET) {
        struct task *task = sched_current_task();
        struct __user_cap_header_struct header;
        struct __user_cap_data_struct data[2] = {{0}};
        uint32_t words;
        uint64_t requested;
        if (!task || !a0 || !user_range_ok(a0, sizeof(header))) return -LINUX_EFAULT;
        __builtin_memcpy(&header, (const void *)(uintptr_t)a0, sizeof(header));
        if (header.version == _LINUX_CAPABILITY_VERSION_1) words = 1;
        else if (header.version == _LINUX_CAPABILITY_VERSION_2 ||
                 header.version == _LINUX_CAPABILITY_VERSION_3) words = 2;
        else {
            if (!user_range_writable(a0, sizeof(header.version))) return -LINUX_EFAULT;
            header.version = _LINUX_CAPABILITY_VERSION_3;
            __builtin_memcpy((void *)(uintptr_t)a0, &header.version, sizeof(header.version));
            if (number == LINUX_SYS_CAPGET && !a1) return 0;
            return -LINUX_EINVAL;
        }
        if (number == LINUX_SYS_CAPGET) {
            if (!a1) return 0;
            if (header.pid < 0) return -LINUX_EINVAL;
            if (header.pid && (uint32_t)header.pid != task->pid) {
                task = sched_find((uint32_t)header.pid);
                if (!task) return -LINUX_ESRCH;
            }
            if (!user_range_writable(a1, words * sizeof(data[0]))) return -LINUX_EFAULT;
            data[0].effective = (uint32_t)task->cap_effective;
            data[0].permitted = (uint32_t)task->cap_permitted;
            data[0].inheritable = (uint32_t)task->cap_inheritable;
            if (words == 2) {
                data[1].effective = (uint32_t)(task->cap_effective >> 32);
                data[1].permitted = (uint32_t)(task->cap_permitted >> 32);
                data[1].inheritable = (uint32_t)(task->cap_inheritable >> 32);
            }
            __builtin_memcpy((void *)(uintptr_t)a1, data, words * sizeof(data[0]));
            return 0;
        }
        if (header.pid && (uint32_t)header.pid != task->pid) return -LINUX_EPERM;
        if (!user_range_ok(a1, words * sizeof(data[0]))) return -LINUX_EFAULT;
        __builtin_memcpy(data, (const void *)(uintptr_t)a1, words * sizeof(data[0]));
        requested = (uint64_t)data[0].permitted | ((uint64_t)data[1].permitted << 32);
        uint64_t effective = (uint64_t)data[0].effective | ((uint64_t)data[1].effective << 32);
        uint64_t inheritable = (uint64_t)data[0].inheritable | ((uint64_t)data[1].inheritable << 32);
        const uint64_t supported = (UINT64_C(1) << (CAP_LAST_CAP + 1)) - 1;
        requested &= supported;
        effective &= supported;
        inheritable &= supported;
        /* Linux applies these subset checks to UID 0 too. CAP_SETPCAP only
         * relaxes the inheritable check; it cannot restore permitted bits. */
        if ((requested & ~task->cap_permitted) || (effective & ~requested) ||
            (inheritable & ~(task->cap_inheritable | task->cap_bset)) ||
            (!(task->cap_effective & (1ULL << CAP_SETPCAP)) &&
             (inheritable & ~(task->cap_inheritable | task->cap_permitted))))
            return -LINUX_EPERM;
        task_credentials_prepare(task, task->euid, task->egid, task->fsuid, task->fsgid, requested);
        task->cap_permitted = requested;
        task->cap_effective = effective;
        task->cap_inheritable = inheritable;
        task->cap_ambient &= requested & inheritable;
        return 0;
    }
    if (number == LINUX_SYS_RSEQ) {
        struct task *task = sched_current_task();
        const uint32_t flags = (uint32_t)a2;
        const uint32_t length = (uint32_t)a1;
        const uint32_t signature = (uint32_t)a3;
        if (!task) return -LINUX_ESRCH;
        if (flags & RSEQ_FLAG_UNREGISTER) {
            if (flags & ~RSEQ_FLAG_UNREGISTER) return -LINUX_EINVAL;
            if (!task->rseq_area || task->rseq_area != a0 || task->rseq_len != length)
                return -LINUX_EINVAL;
            if (task->rseq_sig != signature) return -LINUX_EPERM;
            if (!user_range_writable(task->rseq_area, sizeof(struct rseq)))
                return -LINUX_EFAULT;
            struct rseq *area = (struct rseq *)(uintptr_t)task->rseq_area;
            area->cpu_id_start = RSEQ_CPU_ID_UNINITIALIZED;
            area->cpu_id = RSEQ_CPU_ID_UNINITIALIZED;
            area->node_id = 0;
            area->mm_cid = 0;
            task->rseq_area = 0;
            task->rseq_len = 0;
            task->rseq_sig = 0;
            return 0;
        }
        if (flags) return -LINUX_EINVAL;
        if (task->rseq_area) {
            if (task->rseq_area != a0 || task->rseq_len != length)
                return -LINUX_EINVAL;
            if (task->rseq_sig != signature) return -LINUX_EPERM;
            return -LINUX_EBUSY;
        }
        if (length < RSEQ_SIZE ||
            (length == RSEQ_SIZE ? (a0 & (RSEQ_SIZE - 1U)) != 0 :
                                   ((a0 & 31U) != 0 || length < sizeof(struct rseq))))
            return -LINUX_EINVAL;
        if (!user_range_writable(a0, length)) return -LINUX_EFAULT;
        struct rseq *area = (struct rseq *)(uintptr_t)a0;
        task->rseq_area = a0;
        task->rseq_len = length;
        task->rseq_sig = signature;
        area->cpu_id_start = 0;
        area->cpu_id = 0;
        area->rseq_cs = 0;
        area->flags = 0;
        area->node_id = 0;
        area->mm_cid = 0;
        return 0;
    }
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
        if (target == UINT32_MAX) return -LEONOS_EINVAL;
        if (!task) return -LEONOS_EPERM;
        bool privileged = (task->cap_effective & (1ULL << CAP_SETUID)) != 0;
        if (!privileged && target != task->uid && target != task->suid) return -LEONOS_EPERM;
        process_commit_uids(task, privileged ? target : task->uid, target,
                            privileged ? target : task->suid);
        return 0;
    }
    if (number == LINUX_SYS_SETGID) {
        struct task *task = sched_current_task();
        uint32_t target = (uint32_t)a0;
        if (target == UINT32_MAX) return -LEONOS_EINVAL;
        if (!task) return -LEONOS_EPERM;
        bool privileged = (task->cap_effective & (1ULL << CAP_SETGID)) != 0;
        if (!privileged && target != task->gid && target != task->sgid) return -LEONOS_EPERM;
        process_commit_gids(task, privileged ? target : task->gid, target,
                            privileged ? target : task->sgid);
        return 0;
    }
    if (number == LINUX_SYS_GETRESUID || number == LINUX_SYS_GETRESGID) {
        struct task *task = sched_current_task();
        uint32_t values[3];
        if (!task) return -LINUX_ESRCH;
        if (!user_range_writable(a0, sizeof(uint32_t)) ||
            !user_range_writable(a1, sizeof(uint32_t)) ||
            !user_range_writable(a2, sizeof(uint32_t))) return -LEONOS_EFAULT;
        if (number == LINUX_SYS_GETRESUID) {
            values[0] = task->uid;
            values[1] = task->euid;
            values[2] = task->suid;
        } else {
            values[0] = task->gid;
            values[1] = task->egid;
            values[2] = task->sgid;
        }
        __builtin_memcpy((void *)(uintptr_t)a0, &values[0], sizeof(values[0]));
        __builtin_memcpy((void *)(uintptr_t)a1, &values[1], sizeof(values[1]));
        __builtin_memcpy((void *)(uintptr_t)a2, &values[2], sizeof(values[2]));
        return 0;
    }
    if (number == LINUX_SYS_SETREUID || number == LINUX_SYS_SETREGID) {
        struct task *task = sched_current_task();
        uint32_t real = (uint32_t)a0;
        uint32_t effective = (uint32_t)a1;
        uint32_t current_real, current_effective, saved;
        if (!task) return -LINUX_ESRCH;
        if (number == LINUX_SYS_SETREUID) {
            current_real = task->uid;
            current_effective = task->euid;
            saved = task->suid;
        } else {
            current_real = task->gid;
            current_effective = task->egid;
            saved = task->sgid;
        }
        bool privileged = (task->cap_effective & (1ULL <<
            (number == LINUX_SYS_SETREUID ? CAP_SETUID : CAP_SETGID))) != 0;
        if (real != UINT32_MAX && real != current_real && real != current_effective &&
            !privileged) return -LEONOS_EPERM;
        if (effective != UINT32_MAX && effective != current_real &&
            effective != current_effective && effective != saved &&
            !privileged) return -LEONOS_EPERM;
        bool save = real != UINT32_MAX || (effective != UINT32_MAX && effective != current_real);
        if (real != UINT32_MAX) current_real = real;
        if (effective != UINT32_MAX) current_effective = effective;
        if (number == LINUX_SYS_SETREUID) {
            process_commit_uids(task, current_real, current_effective, save ? current_effective : saved);
        } else {
            process_commit_gids(task, current_real, current_effective, save ? current_effective : saved);
        }
        return 0;
    }
    if (number == LINUX_SYS_SETRESUID || number == LINUX_SYS_SETRESGID) {
        struct task *task = sched_current_task();
        uint32_t values[3] = {(uint32_t)a0, (uint32_t)a1, (uint32_t)a2};
        uint32_t current[3];
        if (!task) return -LINUX_ESRCH;
        if (number == LINUX_SYS_SETRESUID) {
            current[0] = task->uid; current[1] = task->euid; current[2] = task->suid;
        } else {
            current[0] = task->gid; current[1] = task->egid; current[2] = task->sgid;
        }
        uint32_t fs = number == LINUX_SYS_SETRESUID ? task->fsuid : task->fsgid;
        if ((values[0] == UINT32_MAX || values[0] == current[0]) &&
            (values[1] == UINT32_MAX || (values[1] == current[1] && values[1] == fs)) &&
            (values[2] == UINT32_MAX || values[2] == current[2])) return 0;
        if (!(task->cap_effective & (1ULL <<
                (number == LINUX_SYS_SETRESUID ? CAP_SETUID : CAP_SETGID)))) {
            for (uint32_t i = 0; i < 3; ++i)
                if (values[i] != UINT32_MAX && values[i] != current[0] &&
                    values[i] != current[1] && values[i] != current[2]) return -LEONOS_EPERM;
        }
        for (uint32_t i = 0; i < 3; ++i)
            if (values[i] != UINT32_MAX) current[i] = values[i];
        if (number == LINUX_SYS_SETRESUID) {
            process_commit_uids(task, current[0], current[1], current[2]);
        } else {
            process_commit_gids(task, current[0], current[1], current[2]);
        }
        return 0;
    }
    if (number == LINUX_SYS_SETFSUID || number == LINUX_SYS_SETFSGID) {
        struct task *task = sched_current_task();
        uint32_t requested = (uint32_t)a0;
        uint32_t old;
        if (!task) return -LINUX_ESRCH;
        if (number == LINUX_SYS_SETFSUID) {
            old = task->fsuid;
            if (requested != UINT32_MAX && ((task->cap_effective & (1ULL << CAP_SETUID)) || requested == task->uid ||
                requested == task->euid || requested == task->suid || requested == old)) {
                task_credentials_prepare(task, task->euid, task->egid, requested,
                                          task->fsgid, task->cap_permitted);
                const uint64_t fs_caps = (1ULL << CAP_CHOWN) | (1ULL << CAP_MKNOD) |
                    (1ULL << CAP_DAC_OVERRIDE) | (1ULL << CAP_DAC_READ_SEARCH) |
                    (1ULL << CAP_FOWNER) | (1ULL << CAP_FSETID) |
                    (1ULL << CAP_MAC_OVERRIDE) | (1ULL << CAP_LINUX_IMMUTABLE);
                if (!(task->securebits & SECBIT_NO_SETUID_FIXUP)) {
                    if (!old && requested) task->cap_effective &= ~fs_caps;
                    if (old && !requested) task->cap_effective |= task->cap_permitted & fs_caps;
                }
                task->fsuid = requested;
            }
        } else {
            old = task->fsgid;
            if (requested != UINT32_MAX && ((task->cap_effective & (1ULL << CAP_SETGID)) || requested == task->gid ||
                requested == task->egid || requested == task->sgid || requested == old)) {
                task_credentials_prepare(task, task->euid, task->egid, task->fsuid,
                                          requested, task->cap_permitted);
                task->fsgid = requested;
            }
        }
        return old;
    }
    if (number == LINUX_SYS_SCHED_GET_PRIORITY_MAX ||
        number == LINUX_SYS_SCHED_GET_PRIORITY_MIN) {
        int32_t policy = (int32_t)a0;
        if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR &&
            policy != SCHED_BATCH && policy != SCHED_IDLE) return -LEONOS_EINVAL;
        if (policy == SCHED_FIFO || policy == SCHED_RR)
            return number == LINUX_SYS_SCHED_GET_PRIORITY_MAX ? 99 : 1;
        return 0;
    }
    if (number == LINUX_SYS_SCHED_GETPARAM || number == LINUX_SYS_SCHED_GETSCHEDULER) {
        struct task *current = sched_current_task();
        struct task *target = a0 ? sched_find((uint32_t)a0) : current;
        if (!target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (number == LINUX_SYS_SCHED_GETSCHEDULER) return SCHED_OTHER;
        if (!user_range_writable(a1, sizeof(int32_t))) return -LINUX_EFAULT;
        *(int32_t *)(uintptr_t)a1 = 0;
        return 0;
    }
    if (number == LINUX_SYS_SCHED_SETPARAM || number == LINUX_SYS_SCHED_SETSCHEDULER) {
        struct task *current = sched_current_task();
        uint32_t pid = (uint32_t)a0;
        struct task *target = pid ? sched_find(pid) : current;
        uint64_t param_ptr = number == LINUX_SYS_SCHED_SETPARAM ? a1 : a2;
        int32_t policy = number == LINUX_SYS_SCHED_SETSCHEDULER ? (int32_t)a1 : SCHED_OTHER;
        int32_t priority;
        if (!current || !target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (policy != SCHED_OTHER) return -LEONOS_EINVAL;
        if (current->euid && current != target && current->euid != target->uid &&
            current->euid != target->euid) return -LEONOS_EPERM;
        if (!user_range_ok(param_ptr, sizeof(priority))) return -LEONOS_EFAULT;
        priority = *(const int32_t *)(uintptr_t)param_ptr;
        if (priority != 0) return -LEONOS_EINVAL;
        /* LeonOS currently schedules every task as SCHED_OTHER.  The native
         * Linux contract for this policy is a zero realtime priority. */
        return number == LINUX_SYS_SCHED_SETSCHEDULER ? SCHED_OTHER : 0;
    }
    if (number == LINUX_SYS_SCHED_RR_GET_INTERVAL) {
        struct task *current = sched_current_task();
        struct task *target = a0 ? sched_find((uint32_t)a0) : current;
        uint64_t tick_ns = 1000000000ULL / NTCLKS_TICK_HZ;
        if (!current || !target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (!user_range_writable(a1, sizeof(struct linux_timespec))) return -LEONOS_EFAULT;
        ((struct linux_timespec *)(uintptr_t)a1)->tv_sec = 0;
        ((struct linux_timespec *)(uintptr_t)a1)->tv_nsec = (int64_t)tick_ns;
        return 0;
    }
    if (number == LINUX_SYS_SCHED_GETATTR) {
        struct task *current = sched_current_task();
        struct task *target = a0 ? sched_find((uint32_t)a0) : current;
        struct sched_attr attr = {0};
        uint32_t user_size = (uint32_t)a2;
        uint32_t copy_size;
        if (!current || !target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (a3 != 0) return -LEONOS_EINVAL;
        if (user_size < SCHED_ATTR_SIZE_VER0) return -LEONOS_EINVAL;
        if (user_size > 4096U) return -LEONOS_E2BIG;
        if (!user_range_writable(a1, user_size)) return -LEONOS_EFAULT;
        attr.size = sizeof(attr);
        attr.sched_policy = SCHED_OTHER;
        attr.sched_nice = target->priority;
        copy_size = user_size < sizeof(attr) ? user_size : sizeof(attr);
        for (uint32_t i = 0; i < copy_size; ++i)
            ((uint8_t *)(uintptr_t)a1)[i] = ((const uint8_t *)&attr)[i];
        /* Linux zero-fills forward-compatible fields when userspace passes a
         * larger buffer. Never expose uninitialized kernel bytes. */
        for (uint32_t i = copy_size; i < user_size; ++i)
            ((uint8_t *)(uintptr_t)a1)[i] = 0;
        return 0;
    }
    if (number == LINUX_SYS_SCHED_SETATTR) {
        struct task *current = sched_current_task();
        struct task *target = a0 ? sched_find((uint32_t)a0) : current;
        struct sched_attr attr = {0};
        uint32_t user_size = 0;
        uint32_t copy_size;
        if (!current || !target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (a2 != 0) return -LEONOS_EINVAL;
        if (!user_range_ok(a1, SCHED_ATTR_SIZE_VER0)) return -LEONOS_EFAULT;
        for (uint32_t i = 0; i < SCHED_ATTR_SIZE_VER0; ++i)
            ((uint8_t *)&attr)[i] = ((const uint8_t *)(uintptr_t)a1)[i];
        user_size = attr.size;
        if (user_size < SCHED_ATTR_SIZE_VER0) return -LEONOS_EINVAL;
        if (user_size > 4096U) return -LEONOS_E2BIG;
        if (!user_range_ok(a1, user_size)) return -LEONOS_EFAULT;
        copy_size = user_size < sizeof(attr) ? user_size : sizeof(attr);
        for (uint32_t i = SCHED_ATTR_SIZE_VER0; i < copy_size; ++i)
            ((uint8_t *)&attr)[i] = ((const uint8_t *)(uintptr_t)a1)[i];
        if (attr.size < SCHED_ATTR_SIZE_VER0 || attr.size > user_size ||
            attr.size > sizeof(attr)) return -LEONOS_E2BIG;
        if (attr.sched_policy != SCHED_OTHER || attr.sched_flags != 0 ||
            attr.sched_priority != 0 || attr.sched_runtime != 0 ||
            attr.sched_deadline != 0 || attr.sched_period != 0 ||
            attr.sched_util_min != 0 || attr.sched_util_max != 0 ||
            attr.sched_nice < -20 || attr.sched_nice > 19) {
            return -LEONOS_EINVAL;
        }
        if (current->euid && current != target &&
            current->euid != target->uid && current->euid != target->euid) {
            return -LEONOS_EPERM;
        }
        target->priority = attr.sched_nice;
        return 0;
    }
    if (number == LINUX_SYS_PERSONALITY) {
        /* LeonOS implements the native x86-64 Linux personality only.  The
         * all-ones query is distinct from setting an unsupported persona. */
        if ((uint32_t)a0 == UINT32_MAX) return 0;
        return (uint32_t)a0 == 0 ? 0 : -LEONOS_EINVAL;
    }
    if (number == LINUX_SYS_PRCTL) {
        return syscall_process_prctl(a0, a1, a2, a3, 0);
    }
    if (number == LINUX_SYS_UNAME) {
        struct utsname info = {0};
        const struct leonos_system_info *system = ntclks_system_info();
        if (!a0 || !user_range_writable(a0, sizeof(info))) return -LEONOS_EFAULT;
        {
            uint32_t i;
            if (system) {
                for (i = 0; i < sizeof(info.sysname) - 1u && system->kernel_name[i]; ++i) {
                    info.sysname[i] = system->kernel_name[i];
                }
                for (i = 0; i < sizeof(info.release) - 1u && system->kernel_version[i]; ++i) {
                    info.release[i] = system->kernel_version[i];
                }
                for (i = 0; i < sizeof(info.version) - 1u && system->build_time[i]; ++i) {
                    info.version[i] = system->build_time[i];
                }
            }
            for (i = 0; i < sizeof(info.machine) - 1u && "x86_64"[i]; ++i) {
                info.machine[i] = "x86_64"[i];
            }
            linux_uts_names(info.nodename, info.domainname);
        }
        *(struct utsname *)(uintptr_t)a0 = info;
        return 0;
    }
    if (number == LINUX_SYS_SETHOSTNAME || number == LINUX_SYS_SETDOMAINNAME) {
        struct task *task = sched_current_task();
        char captured[LEONOS_UTSNAME_LEN] = {0};
        int32_t length = (int32_t)a1; /* native syscall takes int, not size_t */
        if (!task || !(task->cap_effective & (1ULL << CAP_SYS_ADMIN))) return -LEONOS_EPERM;
        if (length < 0 || length >= (int32_t)LEONOS_UTSNAME_LEN) return -LEONOS_EINVAL;
        if (length && !user_range_ok(a0, (uint32_t)length)) return -LEONOS_EFAULT;
        if (length) __builtin_memcpy(captured, (const void *)(uintptr_t)a0, (uint32_t)length);
        return linux_uts_set(captured, (uint32_t)length, number == LINUX_SYS_SETDOMAINNAME);
    }
    if (number == LINUX_SYS_MEMBARRIER) {
        struct task *task = sched_current_task();
        struct task_address_space_state *mm = task ? sched_task_mm(task) : NULL;
        uint32_t command = (uint32_t)a0;
        uint32_t flags = (uint32_t)a1;
        if (!task || !mm) return -LINUX_ESRCH;
        if (command == MEMBARRIER_CMD_QUERY) {
            if (flags) return -LEONOS_EINVAL;
            return LEONOS_MEMBARRIER_SUPPORTED;
        }
        if (command == MEMBARRIER_CMD_GET_REGISTRATIONS) {
            if (flags) return -LEONOS_EINVAL;
            return (int64_t)mm->membarrier_registrations;
        }
        if (flags || (command & ~LEONOS_MEMBARRIER_SUPPORTED) ||
            (command & (command - 1u))) return -LEONOS_EINVAL;
        switch (command) {
        case MEMBARRIER_CMD_GLOBAL:
        case MEMBARRIER_CMD_GLOBAL_EXPEDITED:
            smp_membarrier(false);
            return 0;
        case MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED:
            smp_membarrier(false);
            mm->membarrier_registrations |= MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED;
            return 0;
        case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
            smp_membarrier(false);
            mm->membarrier_registrations |= MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED;
            return 0;
        case MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
            smp_membarrier(true);
            mm->membarrier_registrations |= MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE;
            return 0;
        case MEMBARRIER_CMD_PRIVATE_EXPEDITED:
            if (!(mm->membarrier_registrations & MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED))
                return -LEONOS_EPERM;
            smp_membarrier(false);
            return 0;
        case MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
            if (!(mm->membarrier_registrations & MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE))
                return -LEONOS_EPERM;
            smp_membarrier(true);
            return 0;
        default:
            return -LEONOS_EINVAL;
        }
    }
    if (number == LINUX_SYS_GETTIMEOFDAY) {
        struct linux_timespec value;
        if (a0 && !user_range_writable(a0, 16u)) return -LEONOS_EFAULT;
        if (a1 && !user_range_writable(a1, 8u)) return -LEONOS_EFAULT;
        int ret = time_clock_get(LINUX_CLOCK_REALTIME, &value);
        if (ret < 0) return ret;
        if (a0) {
            ((int64_t *)(uintptr_t)a0)[0] = value.tv_sec;
            ((int64_t *)(uintptr_t)a0)[1] = value.tv_nsec / 1000;
        }
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
    if (number == LINUX_SYS_CLOCK_SETTIME) {
        struct task *task = sched_current_task();
        struct linux_timespec value;
        if (!task || task->uid != 0) return -LEONOS_EPERM;
        if ((int32_t)a0 != LINUX_CLOCK_REALTIME) return -LEONOS_EINVAL;
        if (!a1 || !user_range_ok(a1, sizeof(value))) return -LEONOS_EFAULT;
        value = *(const struct linux_timespec *)(uintptr_t)a1;
        if (value.tv_nsec < 0 || value.tv_nsec >= 1000000000LL || value.tv_sec < 0)
            return -LEONOS_EINVAL;
        return time_set_wall_clock((uint64_t)value.tv_sec) < 0 ? -LEONOS_EINVAL : 0;
    }
    if (number == LINUX_SYS_SCHED_GETAFFINITY || number == LINUX_SYS_SCHED_SETAFFINITY) {
        struct task *current = sched_current_task();
        uint64_t mask = 0;
        uint32_t length = (uint32_t)a1;
        uint32_t pid = (uint32_t)a0;
        int ret;
        if (!current) return -LINUX_ESRCH;
        if (number == LINUX_SYS_SCHED_GETAFFINITY) {
            /* The kernel supports at most 64 CPUs. Linux requires a whole
             * native unsigned-long buffer, then returns the bytes copied;
             * libc is responsible for clearing any larger userspace tail. */
            if (length < sizeof(mask) || (length & (sizeof(mask) - 1)) || !(length * 8u))
                return -LEONOS_EINVAL;
            ret = sched_get_task_affinity(pid, &mask);
            if (ret < 0) return ret == -2 ? -LINUX_ESRCH : -LEONOS_EINVAL;
            if (!user_range_writable(a2, sizeof(mask))) return -LEONOS_EFAULT;
            *(uint64_t *)(uintptr_t)a2 = mask;
            return sizeof(mask);
        }
        /* Short input masks are zero-extended; oversized inputs only copy
         * the kernel mask. In particular, len=0 need not dereference ptr. */
        uint32_t copied = length < sizeof(mask) ? length : sizeof(mask);
        if (copied && !user_range_ok(a2, copied)) return -LEONOS_EFAULT;
        for (uint32_t i = 0; i < copied; ++i)
            ((uint8_t *)&mask)[i] = ((const uint8_t *)(uintptr_t)a2)[i];
        struct task *target = pid ? sched_find(pid) : current;
        if (!target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (current->euid && current->euid != target->uid && current->euid != target->euid)
            return -LEONOS_EPERM;
        ret = sched_set_task_affinity(pid, mask);
        if (ret < 0) return ret == -2 ? -LINUX_ESRCH : -LEONOS_EINVAL;
        return 0;
    }
    if (number == LINUX_SYS_CLOCK_GETTIME) {
        struct linux_timespec value;
        int ret = time_clock_get((int32_t)a0, &value);
        if (ret < 0) return ret;
        if (!user_range_writable(a1, sizeof(value))) return -LEONOS_EFAULT;
        *(struct linux_timespec *)(uintptr_t)a1 = value;
        return 0;
    }
    if (number == LINUX_SYS_CLOCK_GETRES) {
        struct linux_timespec value;
        int ret = time_clock_get((int32_t)a0, &value);
        if (ret < 0) return ret;
        if (!a1) return 0;
        if (!user_range_writable(a1, 16)) return -LEONOS_EFAULT;
        ((int64_t *)(uintptr_t)a1)[0] = 0;
        ((int64_t *)(uintptr_t)a1)[1] = 1000000000ULL / NTCLKS_TICK_HZ;
        return 0;
    }
    if (number == LINUX_SYS_GETTID) {
        return (int64_t)sched_current_pid();
    }
    if (number == LINUX_SYS_SET_ROBUST_LIST) {
        struct task *task = sched_current_task();
        if (!task) return -LINUX_ESRCH;
        if (a1 != sizeof(struct linux_robust_list_head)) return -LINUX_EINVAL;
        task->robust_list = a0;
        return 0;
    }
    if (number == LINUX_SYS_GET_ROBUST_LIST) {
        struct task *caller = sched_current_task();
        struct task *target = a0 ? sched_find((uint32_t)a0) : caller;
        if (!target || target->state == TASK_EXITED) return -LINUX_ESRCH;
        if (!caller || (caller->euid && caller->euid != target->uid)) return -LINUX_EPERM;
        if (!user_range_writable(a1, 8) || !user_range_writable(a2, 8)) return -LINUX_EFAULT;
        *(uint64_t *)(uintptr_t)a1 = target->robust_list;
        *(uint64_t *)(uintptr_t)a2 = sizeof(struct linux_robust_list_head);
        return 0;
    }
    if (number == LINUX_SYS_SET_TID_ADDRESS) {
        struct task *task = sched_current_task();
        if (!task) return -LEONOS_EPERM;
        /* Linux only registers this pointer. Exit performs a fault-tolerant write. */
        task->clear_child_tid = a0;
        return (int64_t)task->pid;
    }
    if (number == LINUX_SYS_ARCH_PRCTL) {
        struct task *task = sched_current_task();
        if (!task) return -LEONOS_EPERM;
        uint32_t option = (uint32_t)a0;
        if (option == LINUX_ARCH_SET_FS) {
            if (a1 >= NTCLKS_USER_TLS_LIMIT) return -LEONOS_EPERM;
            task->fs_base = a1;
            arch_set_user_fs(a1);
            return 0;
        }
        if (option == LINUX_ARCH_GET_FS) {
            if (!user_range_writable(a1, sizeof(uint64_t))) return -LEONOS_EFAULT;
            *(uint64_t *)(uintptr_t)a1 = task->fs_base;
            return 0;
        }
        return -LEONOS_EINVAL;
    }
    if (number == LINUX_SYS_REBOOT) {
        struct task *task = sched_current_task();
        if (!task || !(task->cap_effective & (1ULL << CAP_SYS_BOOT))) return -LEONOS_EPERM;
        /* Linux reboot(int magic1, int magic2, unsigned int cmd, void *arg). */
        uint32_t magic1 = (uint32_t)a0;
        uint32_t magic2 = (uint32_t)a1;
        uint32_t command = (uint32_t)a2;
        if (magic1 != LINUX_REBOOT_MAGIC1 ||
            (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A &&
             magic2 != LINUX_REBOOT_MAGIC2B && magic2 != LINUX_REBOOT_MAGIC2C))
            return -LEONOS_EINVAL;
        if (command != RB_AUTOBOOT && command != RB_HALT_SYSTEM &&
            command != RB_POWER_OFF) {
            return -LEONOS_EINVAL;
        }
        console_printf("[ntclks] reboot(2) requested by pid=%u command=0x%x\n",
                       task->pid, command);
        if (command == RB_AUTOBOOT) power_reboot();
        power_shutdown();
    }
    if (number == LINUX_SYS_GETPID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)sched_task_tgid(task) : 0;
    }
    if (number == LINUX_SYS_GETPPID) {
        struct task *task = sched_current_task();
        return task ? (int64_t)task->parent_pid : 0;
    }
    if (number == LINUX_SYS_GETPGRP) {
        return sched_get_process_group(0);
    }
    if (number == LINUX_SYS_GETPGID) {
        return sched_get_process_group((uint32_t)a0);
    }
    if (number == LINUX_SYS_GETSID) {
        return sched_get_process_session((uint32_t)a0);
    }
    if (number == LINUX_SYS_SETPGID) {
        return sched_set_process_group(sched_current_pid(), (uint32_t)a0, (uint32_t)a1);
    }
    if (number == LINUX_SYS_SETSID) {
        int64_t result = sched_create_process_session(sched_current_pid());
        return result > 0 ? result : -LEONOS_EPERM;
    }
    if (number == LINUX_SYS_RT_SIGQUEUEINFO)
        return kernel_signal_queueinfo((int32_t)a0, 0, (int32_t)a1, a2, false);
    if (number == LINUX_SYS_RT_TGSIGQUEUEINFO)
        return kernel_signal_queueinfo((int32_t)a0, (int32_t)a1, (int32_t)a2, a3, true);
    if (number == LINUX_SYS_TKILL || number == LINUX_SYS_TGKILL) {
        int32_t tid = (int32_t)(number == LINUX_SYS_TKILL ? a0 : a1);
        int32_t sig = (int32_t)(number == LINUX_SYS_TKILL ? a1 : a2);
        if (tid <= 0 || sig < 0 || sig >= LINUX_NSIG ||
            (number == LINUX_SYS_TGKILL && (int32_t)a0 <= 0)) return -LEONOS_EINVAL;
        struct task *sender = sched_current_task();
        struct task *target = sched_find((uint32_t)tid);
        if (!target || target->state == TASK_EXITED ||
            (number == LINUX_SYS_TGKILL && sched_task_tgid(target) != (uint32_t)a0))
            return -LINUX_ESRCH;
        if (!sender || (sched_task_tgid(sender) != sched_task_tgid(target) &&
            !(sender->cap_effective & (1ULL << CAP_KILL)) && sender->uid != target->uid &&
            sender->uid != target->suid && sender->euid != target->uid &&
            sender->euid != target->suid && !(sig == 18 && sender->process_session == target->process_session)))
            return -LEONOS_EPERM;
        struct linux_siginfo info = {.signo = sig, .code = LINUX_SI_TKILL,
            .fields.sender = {.pid = (int32_t)sched_task_tgid(sender), .uid = sender->uid}};
        return kernel_signal_queue_task_info(target, sig, &info);
    }
    if (number == LINUX_SYS_KILL) {
        int signal_number = (int)a1;
        struct task *current = sched_current_task();
        int32_t requested_pid = (int32_t)a0;
        struct task *target;
        if (signal_number < 0 || signal_number >= LINUX_NSIG) return -LEONOS_EINVAL;
        if (!current || requested_pid == -1) {
            return -LEONOS_EINVAL;
        }
        if (requested_pid <= 0) {
            uint32_t process_group = requested_pid == 0
                                         ? current->process_group
                                         : (uint32_t)(-(int64_t)requested_pid);
            int result = sched_signal_process_group(current->pid, process_group,
                                                    signal_number);
            return result >= 0 ? 0 : result;
        }
        target = sched_find((uint32_t)requested_pid);
        if (!target || target->kind != TASK_KIND_USER || sched_task_tgid(target) != (uint32_t)requested_pid)
            return -LINUX_ESRCH;
        if (sched_task_tgid(current) != sched_task_tgid(target) &&
            !(current->cap_effective & (1ULL << CAP_KILL)) &&
            current->uid != target->uid && current->uid != target->suid &&
            current->euid != target->uid && current->euid != target->suid &&
            !(signal_number == 18 && current->process_session == target->process_session)) return -LEONOS_EPERM;
        struct linux_siginfo info = {.signo = signal_number, .code = LINUX_SI_USER,
            .fields.sender = {.pid = (int32_t)sched_task_tgid(current), .uid = current->uid}};
        return sched_signal_user_process_info((uint32_t)requested_pid, signal_number, &info);
    }
    if (number == LEONOS_SYS_NICE) {
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
        return 20 - priority;
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
            return 20 - priority;
        }
    }
    if (number == LINUX_SYS_GETRLIMIT || number == LINUX_SYS_SETRLIMIT || number == LINUX_SYS_PRLIMIT64)
        return process_resource_limit(number, a0, a1, a2, a3);
    return -LEONOS_ENOSYS;
}
