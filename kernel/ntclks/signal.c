/*
 * LeonOS kernel signal delivery: installs and restores the user rt_sigframe
 * used by rt_sigaction/rt_sigprocmask/rt_sigreturn.
 */
#include <ntclks/signal.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>
#include <ntclks/paging.h>
#include <ntclks/arch.h>
#include <ntclks/futex.h>
#include <ntclks/wait.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <linux/signal.h>
#include <linux/syscall.h>
#include <linux/time.h>
#include <ntclks/time.h>

static struct kernel_signal_action *signal_action(struct task *task, int sig)
{
    if (!task || sig <= 0 || sig >= LINUX_NSIG) return NULL;
    return &sched_task_actions(task)[sig];
}

static void signal_context_save(struct linux_sigcontext *context,
                                const struct trap_frame *frame)
{
    context->r15 = frame->r15;
    context->r14 = frame->r14;
    context->r13 = frame->r13;
    context->r12 = frame->r12;
    context->r11 = frame->r11;
    context->r10 = frame->r10;
    context->r9 = frame->r9;
    context->r8 = frame->r8;
    context->rbp = frame->rbp;
    context->rdi = frame->rdi;
    context->rsi = frame->rsi;
    context->rdx = frame->rdx;
    context->rcx = frame->rcx;
    context->rbx = frame->rbx;
    context->rax = frame->rax;
    context->vector = frame->vector;
    context->error = frame->error;
    context->rip = frame->rip;
    context->cs = frame->cs;
    context->rflags = frame->rflags;
    context->rsp = frame->rsp;
    context->ss = frame->ss;
}

static void signal_context_restore(struct trap_frame *frame,
                                   const struct linux_sigcontext *context)
{
    frame->r15 = context->r15;
    frame->r14 = context->r14;
    frame->r13 = context->r13;
    frame->r12 = context->r12;
    frame->r11 = context->r11;
    frame->r10 = context->r10;
    frame->r9 = context->r9;
    frame->r8 = context->r8;
    frame->rbp = context->rbp;
    frame->rdi = context->rdi;
    frame->rsi = context->rsi;
    frame->rdx = context->rdx;
    frame->rcx = context->rcx;
    frame->rbx = context->rbx;
    frame->rax = context->rax;
    frame->vector = context->vector;
    frame->error = context->error;
    frame->rip = context->rip;
    frame->cs = 0x23;
    /* User context may change arithmetic flags, TF, DF, RF and AC, never IOPL. */
    frame->rflags = (context->rflags & 0x50dd5ULL) | 0x202ULL;
    frame->rsp = context->rsp;
    frame->ss = 0x1b;
}

static void signal_default_action(struct task *task, int sig)
{
    if (!task || task->state == TASK_EXITED) return;
    switch (sig) {
    case 19: /* SIGSTOP */
    case 20: /* SIGTSTP */
    case 21: /* SIGTTIN */
    case 22: /* SIGTTOU */
        sched_signal_job_control(sched_task_tgid(task), sig);
        return;
    case 18: /* SIGCONT */
        sched_signal_job_control(sched_task_tgid(task), sig);
        return;
    case 23: /* SIGURG  */
    case 17: /* SIGCHLD */
    case 28: /* SIGWINCH */
        return;
    default:
        task->exit_signal = (uint32_t)sig;
        {
            struct task *leader = sched_find(sched_task_tgid(task));
            if (leader) leader->exit_signal = (uint32_t)sig;
            sched_exit_group(sched_task_tgid(task), (uint64_t)(128 + sig));
        }
        return;
    }
}

int kernel_signal_set_action(struct task *task, int signal_number,
                             uint64_t handler, uint64_t mask, uint32_t flags,
                             uint64_t restorer,
                             struct kernel_signal_action *previous)
{
    struct kernel_signal_action *slot;
    if (!task || task->kind != TASK_KIND_USER || task->state == TASK_EXITED ||
        signal_number <= 0 || signal_number >= LINUX_NSIG || signal_number == 9 ||
        signal_number == 19 || (handler != 0 && handler != 1 && !restorer)) {
        return -1;
    }
    slot = signal_action(task, signal_number);
    if (!slot) return -1;
    if (previous) *previous = *slot;
    slot->handler = handler;
    slot->mask = mask & KERNEL_SIGNAL_VALID_MASK;
    slot->flags = flags;
    slot->restorer = restorer;
    slot->reserved = 0;
    if (handler == 1) {
        task->ignored_signals |= 1ULL << (uint32_t)(signal_number - 1);
        sched_signal_discard(task, signal_number);
    } else {
        task->ignored_signals &= ~(1ULL << (uint32_t)(signal_number - 1));
    }
    return 0;
}

int kernel_signal_queue_task(struct task *task, int signal_number)
{
    struct kernel_signal_action *action;
    uint64_t bit;
    if (!task || task->pid == 0 || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED || signal_number < 0 || signal_number >= LINUX_NSIG) {
        return -1;
    }
    if (signal_number == 0) return 0;
    bit = 1ULL << (uint32_t)(signal_number - 1);
    action = signal_action(task, signal_number);
    if (signal_number == 18) signal_default_action(task, signal_number);
    if (signal_number >= 19 && signal_number <= 22)
        task->pending_signals &= ~(1ULL << 17);

    if (signal_number != 9 && signal_number != 19 && action && action->handler == 1) {
        task->pending_signals &= ~bit;
        return 0;
    }

    if (signal_number != 9 && signal_number != 19 &&
        (task->blocked_signals & bit) != 0) {
        task->pending_signals |= bit;
        return 0;
    }
    if (signal_number != 9 && signal_number != 19 && action &&
        action->handler != 0 && action->handler != 1) {
        task->pending_signals |= bit;
        /* A blocked task must run before the return-to-user path can install
         * the handler frame. STOPPED tasks stay stopped until SIGCONT. */
        if (task->state == TASK_BLOCKED) {
            task->wake_tick = 0;
            task->wait_window_id = 0;
            task->state = TASK_READY;
        }
        return 0;
    }

    task->pending_signals |= bit;
    signal_default_action(task, signal_number);
    task->pending_signals &= ~bit;
    return 0;
}

static int signal_copy_to_task(struct task *task, uint64_t base,
                                const void *source, uint64_t size)
{
    if (base < NTCLKS_USER_BASE || base >= NTCLKS_USER_TOP ||
        size > NTCLKS_USER_TOP - base) return -14;
    for (uint64_t copied = 0; copied < size;) {
        uint64_t address = base + copied;
        if (!address_space_user_page_writable(sched_task_as(task), address) &&
            !address_space_handle_cow_fault(sched_task_as(task), address)) return -14;
        uint64_t phys = address_space_user_page_phys(sched_task_as(task), address);
        uint64_t offset = address & 4095u;
        uint64_t take = 4096u - offset;
        if (!phys) return -14;
        if (take > size - copied) take = size - copied;
        __builtin_memcpy((void *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + offset),
                         (const uint8_t *)source + copied, take);
        copied += take;
    }
    return 0;
}

static int signal_setup_frame(struct task *task, int sig,
                              struct kernel_signal_action *action,
                              struct trap_frame *frame)
{
    if (!task || !frame || !action || !action->handler ||
        action->handler == 1 || !action->restorer) return -22;
    struct linux_rt_sigframe user_frame;
    uint64_t size = sizeof(user_frame);
    uint64_t base;
    uint64_t bit = 1ULL << (uint32_t)(sig - 1);
    uint8_t fpstate[512] __attribute__((aligned(16)));
    uint64_t fpbase;
    bool on_altstack = task->signal_stack_size && frame->rsp >= task->signal_stack_base &&
        frame->rsp - task->signal_stack_base < task->signal_stack_size;
    uint64_t stack_top = frame->rsp;
    if ((action->flags & LINUX_SA_ONSTACK) && task->signal_stack_size && !on_altstack)
        stack_top = task->signal_stack_base + task->signal_stack_size;

    /* Preserve the interrupted red zone and align the FXSAVE image separately
     * from the ABI's rsp % 16 == 8 function-entry frame. */
    if (stack_top < 128 + sizeof(fpstate) + size + 80) return -14;
    fpbase = (stack_top - 128 - sizeof(fpstate)) & ~63ULL;
    base = ((fpbase - size) & ~15ULL) - 8;
    if (!base || base < NTCLKS_USER_BASE || base > stack_top) {
        return -12;
    }

    __builtin_memset(&user_frame, 0, size);
    user_frame.restorer = action->restorer;
    user_frame.uc.flags = 6; /* UC_SIGCONTEXT_SS | UC_STRICT_RESTORE_SS */
    user_frame.uc.stack.sp = task->signal_stack_base;
    user_frame.uc.stack.size = task->signal_stack_size;
    user_frame.uc.stack.flags = task->signal_stack_size
        ? task->signal_stack_flags | (on_altstack ? 1 : 0) : 2;
    user_frame.uc.mask = task->sigsuspend_active
                                ? task->sigsuspend_saved_mask
                                : task->blocked_signals;
    user_frame.info.signo = sig;
    user_frame.info.code = 0;
    if (task->restart_syscall) {
        uint64_t nr = task->restart_syscall - 1;
        bool sleeping = nr == __NR_nanosleep || nr == __NR_clock_nanosleep;
        bool interruptible = nr == __NR_read || nr == __NR_write || nr == __NR_readv ||
            nr == __NR_writev || nr == __NR_recvfrom || nr == __NR_sendto ||
            nr == __NR_recvmsg || nr == __NR_sendmsg || nr == __NR_accept ||
            nr == __NR_accept4 || nr == __NR_connect || nr == __NR_futex || nr == __NR_futex_wait ||
            nr == __NR_poll || nr == __NR_select || nr == __NR_wait4 || sleeping;
        bool restart = (action->flags & LINUX_SA_RESTART) &&
            nr != __NR_poll && nr != __NR_select && !sleeping &&
            !(nr == __NR_futex && frame->r10) && !task->socket_io_timed;
        uint64_t received = task_socket_cancel_receive(task);
        task_release_syscall_file(task);
        if (received || (interruptible && !restart)) {
            frame->rip += 2;
            frame->rax = received ? received : (uint64_t)-4LL;
            task->poll_deadline_ticks = 0;
        }
        if (sleeping) {
            uint64_t now = time_ticks();
            uint64_t ticks = task->nanosleep_deadline > now ? task->nanosleep_deadline - now : 0;
            struct linux_timespec remaining = {(int64_t)(ticks / NTCLKS_TICK_HZ),
                (int64_t)((ticks % NTCLKS_TICK_HZ) * (1000000000ULL / NTCLKS_TICK_HZ))};
            if (task->nanosleep_remaining &&
                signal_copy_to_task(task, task->nanosleep_remaining, &remaining, sizeof(remaining)) < 0)
                frame->rax = (uint64_t)-14LL;
            task->nanosleep_deadline = task->nanosleep_remaining = 0;
        }
        if (task->waiting_queue) kernel_wait_queue_remove(task->waiting_queue, task);
        if (nr == __NR_futex || nr == __NR_futex_wait) futex_cancel_wait(task);
        task->restart_syscall = 0;
    }
    signal_context_save(&user_frame.uc.context, frame);
    user_frame.uc.context.oldmask = task->blocked_signals;
    user_frame.uc.context.fpstate = fpbase;
    __builtin_memcpy(fpstate, task->fpu_state, sizeof(fpstate));
    /* FXSAVE's software-reserved tail must not advertise an XSAVE extension. */
    __builtin_memset(fpstate + 464, 0, sizeof(fpstate) - 464);
    if (signal_copy_to_task(task, fpbase, fpstate, sizeof(fpstate)) < 0 ||
        signal_copy_to_task(task, base, &user_frame, size) < 0) return -14;
    if (task->signal_stack_flags & 0x80000000u) {
        task->signal_stack_size = 0;
        task->signal_stack_base = 0;
        task->signal_stack_flags = 0;
    }
    task->sigsuspend_active = 0;

    /* The handler runs with the delivered signal and the action mask blocked;
     * SA_NODEFER omits only the automatic current-signal block. */
    task->blocked_signals |= action->mask & KERNEL_SIGNAL_VALID_MASK;
    if ((action->flags & LINUX_SA_NODEFER) == 0) {
        task->blocked_signals |= bit;
    }
    task->blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
    frame->rsp = base;
    frame->rip = action->handler;
    frame->rdi = (uint64_t)sig;
    frame->rsi = (action->flags & LINUX_SA_SIGINFO)
                     ? (base + __builtin_offsetof(struct linux_rt_sigframe, info))
                     : 0;
    frame->rdx = (action->flags & LINUX_SA_SIGINFO)
                     ? (base + __builtin_offsetof(struct linux_rt_sigframe, uc))
                     : 0;
    frame->rax = 0;
    return 0;
}

int kernel_signal_deliver_pending(struct task *task, struct trap_frame *frame)
{
    struct kernel_signal_action *action;
    uint64_t pending;
    int ret;

    if (!task || !frame || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED) {
        return 0;
    }
    /* Linux dequeues thread-directed signals before the process-wide queue. */
    for (unsigned queue = 0; queue < 2; ++queue) {
        uint64_t *source = queue ? sched_task_process_pending(task) : &task->pending_signals;
        pending = *source & ~task->blocked_signals & KERNEL_SIGNAL_VALID_MASK;
        for (int sig = 1; sig < LINUX_NSIG; ++sig) {
            uint64_t bit = 1ULL << (uint32_t)(sig - 1);
            if ((pending & bit) == 0) continue;
            action = signal_action(task, sig);
            if (!action || action->handler == 0 || action->handler == 1) {
                /* A default/ignored signal that became unblocked must be applied
                 * on the return path instead of lingering as pending. */
                *source &= ~bit;
                if (queue && sig == 14) sched_alarm_rearm(task);
                if (!action || action->handler != 1) signal_default_action(task, sig);
                if (task->state == TASK_EXITED || task->state == TASK_STOPPED) return 0;
                continue;
            }
            ret = signal_setup_frame(task, sig, action, frame);
            if (ret < 0) return ret;
            *source &= ~bit;
            if (queue && sig == 14) sched_alarm_rearm(task);
            if ((action->flags & LINUX_SA_RESETHAND) != 0) {
                action->handler = 0;
                action->restorer = 0;
                task->ignored_signals &= ~bit;
            }
            return 1;
        }
    }
    return 0;
}

int64_t kernel_signal_rt_sigreturn(struct task *task, struct trap_frame *frame)
{
    struct linux_rt_sigframe user_frame;
    uint8_t fpstate[512] __attribute__((aligned(16)));
    uint64_t base;
    uint64_t size = sizeof(user_frame);

    if (!task || !frame || task->kind != TASK_KIND_USER || frame->rsp < 8 ||
        frame->rsp < size) {
        return -14;
    }
    base = frame->rsp - 8U;
    if (!user_range_ok(base, size)) return -14;
    __builtin_memcpy(&user_frame, (const void *)(uintptr_t)base, size);
    const struct linux_sigcontext *context = &user_frame.uc.context;
    if ((frame->cs & 3ULL) != 3ULL || context->cs != 0x23 ||
        context->ss != 0x1b || context->rip < NTCLKS_USER_BASE ||
        context->rip >= NTCLKS_USER_TOP || context->rsp >= NTCLKS_USER_TOP) {
        return -22;
    }
    if (context->fpstate) {
        if (!user_range_ok(context->fpstate, sizeof(fpstate))) return -14;
        __builtin_memcpy(fpstate, (const void *)(uintptr_t)context->fpstate, sizeof(fpstate));
        uint32_t mxcsr;
        __builtin_memcpy(&mxcsr, fpstate + 24, sizeof(mxcsr));
        if (mxcsr & ~0xffffU) return -22;
        __builtin_memcpy(task->fpu_state, fpstate, sizeof(fpstate));
        arch_fpu_restore(task->fpu_state);
    }
    signal_context_restore(frame, context);
    task->blocked_signals = user_frame.uc.mask;
    task->blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
    task->sigsuspend_active = 0;
    if (user_frame.uc.stack.flags & 2) {
        task->signal_stack_base = 0;
        task->signal_stack_size = 0;
        task->signal_stack_flags = 0;
    } else if (user_frame.uc.stack.sp < NTCLKS_USER_TOP &&
               user_frame.uc.stack.size <= NTCLKS_USER_TOP - user_frame.uc.stack.sp) {
        task->signal_stack_base = user_frame.uc.stack.sp;
        task->signal_stack_size = user_frame.uc.stack.size;
        task->signal_stack_flags = (uint32_t)user_frame.uc.stack.flags & 0x80000000u;
    }
    return 0;
}

void kernel_signal_reset_handlers(struct task *task)
{
    if (!task) return;
    task->signal_stack_base = 0;
    task->signal_stack_size = 0;
    task->signal_stack_flags = 0;
    task->restart_syscall = 0;
    task->nanosleep_deadline = task->nanosleep_remaining = 0;
    task->sigwait_deadline = task->sigwait_mask = 0;
    for (int sig = 1; sig < LINUX_NSIG; ++sig) {
        if (sched_task_actions(task)[sig].handler != 1)
            sched_task_actions(task)[sig] = (struct kernel_signal_action){0};
    }
    task->sigsuspend_saved_mask = 0;
    task->sigsuspend_active = 0;
}

void kernel_signal_state_snapshot(const struct task *task,
                                  struct kernel_signal_action actions[KERNEL_SIGNAL_ACTION_MAX],
                                  uint64_t *pending, uint64_t *blocked,
                                  uint64_t *ignored)
{
    if (!task) return;
    for (int sig = 0; sig < LINUX_NSIG; ++sig) {
        actions[sig] = sched_task_actions(task)[sig];
    }
    if (pending) *pending = sched_task_pending(task);
    if (blocked) *blocked = task->blocked_signals;
    if (ignored) *ignored = task->ignored_signals;
}
