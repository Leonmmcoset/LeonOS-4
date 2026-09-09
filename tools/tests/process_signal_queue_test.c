#include <assert.h>
#include <stdio.h>
#include "../../kernel/ntclks/sched/sched.c"
#include "../../kernel/ntclks/syscall_time.c"

static struct task members[3];
static struct task *table[] = {&members[0], &members[1], &members[2]};
static unsigned direct_calls;
uint32_t smp_current_cpu(void) { return 0; }
bool user_range_ok(uint64_t address, uint64_t size)
{ return address >= 4096 && address + size >= address; }
bool user_range_writable(uint64_t address, uint64_t size) { return user_range_ok(address, size); }
int kernel_signal_queue_task(struct task *task, int sig)
{ assert(task && sig); ++direct_calls; return 0; }
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{ assert(!lock->state); lock->state = 1; *flags = 0; }
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{ (void)flags; assert(lock->state); lock->state = 0; }

int main(void)
{
    tasks = table;
    task_count = 3;
    for (unsigned i = 0; i < 3; ++i) {
        members[i].pid = 10 + i;
        members[i].tgid = 10;
        members[i].kind = TASK_KIND_USER;
        members[i].state = TASK_BLOCKED;
        members[i].signal_actions[12].handler = 0x410000;
        if (i) members[i].shared_process_pending = &members[0].process_pending_signals;
    }
    const uint64_t bit = 1ULL << 11;
    members[0].blocked_signals = bit;
    members[1].blocked_signals = bit;
    assert(sched_signal_user_process(10, 12) == 0);
    assert(members[0].state == TASK_BLOCKED && members[1].state == TASK_BLOCKED);
    assert(members[2].state == TASK_READY && !direct_calls);
    for (unsigned i = 0; i < 3; ++i) assert(sched_task_pending(&members[i]) == bit && !members[i].pending_signals);
    members[0].state = TASK_EXITED;
    members[2].state = TASK_BLOCKED;
    assert(sched_signal_user_process(10, 12) == 0 && members[2].state == TASK_READY);
    assert(sched_signal_user_process(11, 12) == -1); /* A TID is not a process PID. */
    members[1].pending_signals = bit;
    sched_signal_discard(&members[2], 12);
    for (unsigned i = 0; i < 3; ++i) assert(!sched_task_pending(&members[i]));
    sched_signal_job_control(10, 19);
    assert(members[0].state == TASK_EXITED && members[1].state == TASK_STOPPED && members[2].state == TASK_STOPPED);
    members[1].pending_signals = 1ULL << 21;
    members[0].process_pending_signals = 1ULL << 18;
    sched_signal_job_control(10, 18);
    assert(members[1].state == TASK_READY && members[2].state == TASK_READY);
    assert(!sched_task_pending(&members[1]));
    scheduler_ticks = 100;
    assert(sched_alarm_task(&members[1], 2) == 0 && members[0].alarm_deadline == 300);
    scheduler_ticks = 140;
    assert(sched_alarm_task(&members[2], 1) == 2 && members[0].alarm_deadline == 240);
    scheduler_ticks = 239;
    assert(sched_alarm_task(&members[1], 0) == 1 && !members[0].alarm_deadline);
    assert(sched_alarm_task(&members[1], 1) == 0);
    members[1].blocked_signals = 1ULL << 13;
    members[1].state = members[2].state = TASK_BLOCKED;
    scheduler_ticks = 339;
    sched_alarm_expire();
    assert(!members[0].alarm_deadline && sched_task_pending(&members[1]) == (1ULL << 13));
    assert(members[1].state == TASK_BLOCKED && members[2].state == TASK_READY);
    assert(!direct_calls); /* Interrupt context only queues, never destroys a task. */
    current_pid[0] = 11;
    struct linux_itimerval timer = {.it_interval.tv_usec = 40000, .it_value.tv_usec = 10000}, old;
    assert(syscall_itimer(true, 0, (uintptr_t)&timer, 1) == -LINUX_EFAULT);
    assert(members[0].alarm_deadline == 340); /* Bad output must not undo the update. */
    assert(syscall_itimer(false, 0, (uintptr_t)&old, 0) == 0);
    assert(old.it_interval.tv_usec == 40000 && old.it_value.tv_usec == 10000);
    timer.it_value.tv_usec = 1000000;
    assert(syscall_itimer(true, 0, (uintptr_t)&timer, 0) == -LINUX_EINVAL);
    assert(members[0].alarm_deadline == 340);
    scheduler_ticks = 349;
    sched_alarm_expire();
    assert(syscall_itimer(false, 0, (uintptr_t)&old, 0) == 0 && !old.it_value.tv_usec);
    sched_alarm_rearm(&members[1]);
    assert(members[0].alarm_deadline == 352);
    assert(syscall_itimer(true, 0, 0, (uintptr_t)&old) == 0);
    assert(!members[0].alarm_deadline && !members[0].alarm_interval_ns);
    assert(syscall_itimer(false, 99, 0, 0) == -LINUX_EINVAL);
    assert(syscall_itimer(false, 0, 0, 0) == -LINUX_EFAULT);
    /* The last exiting thread notifies once on behalf of the zombie leader. */
    members[0].pid = members[0].tgid = 10;
    members[0].state = TASK_EXITED;
    members[0].running_cpu = SCHED_CPU_NONE;
    members[0].parent_pid = 12;
    members[0].parent_exit_signal = 17;
    members[1].state = TASK_READY;
    members[1].running_cpu = SCHED_CPU_NONE;
    members[2].tgid = 12;
    members[2].shared_process_pending = NULL;
    members[2].process_pending_signals = 0;
    members[2].signal_actions[17].handler = 0x410000;
    members[2].state = TASK_BLOCKED;
    sched_notify_parent_exit(&members[0]);
    assert(!members[0].parent_exit_notified && !members[2].process_pending_signals);
    members[1].state = TASK_EXITED;
    members[1].running_cpu = 0;
    sched_notify_parent_exit(&members[0]);
    assert(!members[0].parent_exit_notified);
    members[1].running_cpu = SCHED_CPU_NONE;
    sched_notify_parent_exit(&members[1]);
    assert(members[0].parent_exit_notified && members[2].process_pending_signals == (1ULL << 16));
    assert(members[2].state == TASK_READY);
    members[2].process_pending_signals = 0;
    sched_notify_parent_exit(&members[1]);
    assert(!members[2].process_pending_signals);
    puts("PASS process signals/itimer/exit: shared pending, exited leader, stop/continue, alarm, periodic rearm, final-thread parent notification");
}
