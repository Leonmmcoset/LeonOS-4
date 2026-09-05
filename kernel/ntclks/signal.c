/*
 * LeonOS kernel signal delivery: installs and restores the user rt_sigframe
 * used by rt_sigaction/rt_sigprocmask/rt_sigreturn.
 */
#include <ntclks/signal.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>
#include <leonos/signal.h>

#define LEONOS_SA_NODEFER    0x00000040u
#define LEONOS_SA_RESETHAND  0x00000004u
#define LEONOS_SA_SIGINFO    0x00000010u

static struct kernel_signal_action *signal_action(struct task *task, int sig)
{
    if (!task || sig <= 0 || sig >= 32) return NULL;
    return &task->signal_actions[sig];
}

static void signal_context_save(struct leonos_sigcontext *context,
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
                                   const struct leonos_sigcontext *context)
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
    frame->cs = context->cs;
    frame->rflags = context->rflags;
    frame->rsp = context->rsp;
    frame->ss = context->ss;
}

static void signal_default_action(struct task *task, int sig)
{
    if (!task || task->state == TASK_EXITED) return;
    switch (sig) {
    case 17: /* SIGSTOP */
    case 18: /* SIGTSTP */
    case 21: /* SIGTTIN */
    case 22: /* SIGTTOU */
        task->wake_tick = 0;
        task->wait_window_id = 0;
        task->state = TASK_STOPPED;
        task->stop_signal = (uint32_t)sig;
        task->child_event = TASK_CHILD_EVENT_STOPPED;
        return;
    case 19: /* SIGCONT */
        task->pending_signals &= ~((1u << 17) | (1u << 18));
        if (task->state == TASK_STOPPED) {
            task->wake_tick = 0;
            task->wait_window_id = 0;
            task->state = TASK_READY;
            task->stop_signal = 0;
            task->child_event = TASK_CHILD_EVENT_CONTINUED;
        }
        return;
    case 16: /* SIGURG  */
    case 20: /* SIGCHLD */
    case 23: /* SIGIO   */
    case 28: /* SIGWINCH */
        return;
    default:
        task->exit_signal = (uint32_t)sig;
        task->state = TASK_EXITED;
        task->exit_code = (uint64_t)(128 + sig);
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
        signal_number <= 0 || signal_number >= 32 || signal_number == 9 ||
        signal_number == 17 || (handler != 0 && handler != 1 && !restorer)) {
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
        task->ignored_signals |= 1u << (uint32_t)signal_number;
        task->pending_signals &= ~(1u << (uint32_t)signal_number);
    } else {
        task->ignored_signals &= ~(1u << (uint32_t)signal_number);
    }
    return 0;
}

int kernel_signal_queue_task(struct task *task, int signal_number)
{
    struct kernel_signal_action *action;
    uint32_t bit;
    if (!task || task->pid == 0 || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED || signal_number < 0 || signal_number >= 32) {
        return -1;
    }
    if (signal_number == 0) return 0;
    bit = 1u << (uint32_t)signal_number;

    if (signal_number != 9 && signal_number != 17 &&
        (task->blocked_signals & bit) != 0) {
        task->pending_signals |= bit;
        return 0;
    }
    if (signal_number != 9 && signal_number != 17 &&
        (task->ignored_signals & bit) != 0) {
        task->pending_signals &= ~bit;
        return 0;
    }

    action = signal_action(task, signal_number);
    if (signal_number != 9 && signal_number != 17 && action &&
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
    return 0;
}

static int signal_setup_frame(struct task *task, int sig,
                              struct kernel_signal_action *action,
                              struct trap_frame *frame)
{
    struct leonos_rt_sigframe user_frame;
    uint64_t size = sizeof(user_frame);
    uint64_t base;
    uint32_t bit = 1u << (uint32_t)sig;

    if (!task || !frame || !action || !action->handler ||
        action->handler == 1 || !action->restorer) {
        return -22;
    }
    /* Keep the handler's initial rsp at the x86_64 SysV function-entry
     * alignment (rsp % 16 == 8) below the interrupted stack. */
    base = frame->rsp >= size ? (frame->rsp - size) : 0;
    base = (base & ~0x0fULL) - 8ULL;
    if (!base || base < NTCLKS_USER_BASE ||
        (task->stack_low && base < task->stack_low) || base > frame->rsp) {
        return -12;
    }

    __builtin_memset(&user_frame, 0, size);
    user_frame.restorer = action->restorer;
    user_frame.magic = LEONOS_SIGFRAME_MAGIC;
    user_frame.version = LEONOS_SIGFRAME_VERSION;
    user_frame.saved_mask = task->blocked_signals;
    user_frame.signal_number = (uint32_t)sig;
    user_frame.flags = action->flags;
    user_frame.info.signo = (uint32_t)sig;
    user_frame.info.code = 0;
    signal_context_save(&user_frame.context, frame);

    if (!user_range_writable(base, size)) return -14;
    __builtin_memcpy((void *)(uintptr_t)base, &user_frame, size);

    /* The handler runs with the delivered signal and the action mask blocked;
     * SA_NODEFER omits only the automatic current-signal block. */
    task->blocked_signals |= action->mask & KERNEL_SIGNAL_VALID_MASK;
    if ((action->flags & LEONOS_SA_NODEFER) == 0) {
        task->blocked_signals |= bit;
    }
    task->blocked_signals &= ~((1u << 9) | (1u << 17));
    task->pending_signals &= ~bit;

    frame->rsp = base;
    frame->rip = action->handler;
    frame->rdi = (uint64_t)sig;
    frame->rsi = (action->flags & LEONOS_SA_SIGINFO)
                     ? (base + __builtin_offsetof(struct leonos_rt_sigframe, info))
                     : 0;
    frame->rdx = (action->flags & LEONOS_SA_SIGINFO)
                     ? (base + __builtin_offsetof(struct leonos_rt_sigframe, context))
                     : 0;
    frame->rax = 0;
    return 0;
}

int kernel_signal_deliver_pending(struct task *task, struct trap_frame *frame)
{
    struct kernel_signal_action *action;
    uint32_t pending;
    int ret;

    if (!task || !frame || task->kind != TASK_KIND_USER ||
        task->state == TASK_EXITED) {
        return 0;
    }
    pending = task->pending_signals & ~task->blocked_signals &
              KERNEL_SIGNAL_VALID_MASK;
    for (int sig = 1; sig < 32; ++sig) {
        uint32_t bit = 1u << (uint32_t)sig;
        if ((pending & bit) == 0) continue;
        action = signal_action(task, sig);
        if (!action || action->handler == 0 || action->handler == 1) {
            /* A default/ignored signal that became unblocked must be applied
             * on the return path instead of lingering as pending. */
            task->pending_signals &= ~bit;
            signal_default_action(task, sig);
            if (task->state == TASK_EXITED) return 0;
            continue;
        }
        ret = signal_setup_frame(task, sig, action, frame);
        if (ret < 0) return ret;
        if ((action->flags & LEONOS_SA_RESETHAND) != 0) {
            action->handler = 0;
            action->restorer = 0;
            task->ignored_signals &= ~bit;
        }
        return 1;
    }
    return 0;
}

int64_t kernel_signal_rt_sigreturn(struct task *task, struct trap_frame *frame)
{
    struct leonos_rt_sigframe user_frame;
    uint64_t base;
    uint64_t size = sizeof(user_frame);

    if (!task || !frame || task->kind != TASK_KIND_USER || frame->rsp < 8 ||
        frame->rsp < size) {
        return -14;
    }
    base = frame->rsp - 8U;
    if (!user_range_ok(base, size)) return -14;
    __builtin_memcpy(&user_frame, (const void *)(uintptr_t)base, size);
    if (user_frame.magic != LEONOS_SIGFRAME_MAGIC ||
        user_frame.version != LEONOS_SIGFRAME_VERSION ||
        (frame->cs & 3ULL) != 3ULL) {
        return -22;
    }
    signal_context_restore(frame, &user_frame.context);
    task->blocked_signals = (uint32_t)user_frame.saved_mask;
    task->blocked_signals &= ~((1u << 9) | (1u << 17));
    task->pending_signals &= ~(1u << user_frame.signal_number);
    return (int64_t)user_frame.context.rax;
}

void kernel_signal_reset_handlers(struct task *task)
{
    if (!task) return;
    for (int sig = 1; sig < 32; ++sig) {
        task->signal_actions[sig] = (struct kernel_signal_action){0};
    }
    task->ignored_signals = 0;
    task->pending_signals = 0;
}

void kernel_signal_state_snapshot(const struct task *task,
                                  struct kernel_signal_action actions[32],
                                  uint32_t *pending, uint32_t *blocked,
                                  uint32_t *ignored)
{
    if (!task) return;
    for (int sig = 0; sig < 32; ++sig) {
        actions[sig] = task->signal_actions[sig];
    }
    if (pending) *pending = task->pending_signals;
    if (blocked) *blocked = task->blocked_signals;
    if (ignored) *ignored = task->ignored_signals;
}
