#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <ntclks/paging.h>
/* Model the kernel direct map with host pointers to the destination pages. */
#undef NTCLKS_KERNEL_DIRECT_MAP_BASE
#define NTCLKS_KERNEL_DIRECT_MAP_BASE 0
#include "../../kernel/ntclks/signal.c"

#define STACK_ADDRESS 0x0f400000ULL
static unsigned char target_pages[8192] __attribute__((aligned(4096)));
static struct task target;
static unsigned translated_pages;
uint64_t time_ticks(void) { return 100; }
void kernel_wait_queue_remove(struct kernel_wait_queue *queue, struct task *task)
{ (void)queue; (void)task; assert(0); }
void futex_cancel_wait(struct task *task) { (void)task; assert(0); }
uint64_t task_socket_cancel_receive(struct task *task) { (void)task; return 0; }
void task_release_syscall_file(struct task *task) { (void)task; }
struct task *sched_find(uint32_t pid) { (void)pid; return &target; }
void sched_exit_group(uint32_t pid, uint64_t code) { (void)pid; (void)code; assert(0); }
void sched_signal_job_control(uint32_t pid, int sig) { (void)pid; (void)sig; assert(0); }
void sched_alarm_rearm(struct task *task) { (void)task; assert(0); }
void sched_signal_discard(struct task *task, int sig)
{ task->pending_signals &= ~(1ULL << (sig - 1)); *sched_task_process_pending(task) &= ~(1ULL << (sig - 1)); }

bool address_space_user_page_writable(const struct address_space *as, uint64_t address)
{ assert(as == &target.as); return user_range_writable(address, 1); }
bool address_space_handle_cow_fault(struct address_space *as, uint64_t address)
{ (void)as; (void)address; return false; }

bool user_range_writable(uint64_t ptr, uint64_t len)
{ return ptr >= STACK_ADDRESS && ptr + len <= STACK_ADDRESS + sizeof(target_pages); }
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr)
{
    assert(as == &target.as);
    assert(vaddr >= STACK_ADDRESS && vaddr < STACK_ADDRESS + sizeof(target_pages));
    translated_pages |= 1u << ((vaddr - STACK_ADDRESS) / 4096);
    return (uintptr_t)target_pages + ((vaddr - STACK_ADDRESS) & ~4095ULL);
}

int main(void)
{
    unsigned char *previous_stack = mmap((void *)STACK_ADDRESS, sizeof(target_pages),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(previous_stack == (void *)STACK_ADDRESS);
    memset(previous_stack, 0xa5, sizeof(target_pages));
    memset(target_pages, 0x5a, sizeof(target_pages));
    target.kind = TASK_KIND_USER;
    target.pid = 1;
    target.state = TASK_READY;
    target.stack_low = STACK_ADDRESS;
    target.signal_actions[2] = (struct kernel_signal_action){
        .handler = 0x410000, .restorer = 0x420000};
    assert(kernel_signal_queue_task(&target, 2) == 0);
    assert(target.pending_signals == (1u << (2 - 1)));
    struct trap_frame frame = {.rsp = STACK_ADDRESS + 4096 + 800,
                                .rip = 0x430000, .cs = 0x23};
    uint64_t saved_rsp = frame.rsp;
    assert(kernel_signal_deliver_pending(&target, &frame) == 1);
    struct linux_rt_sigframe saved;
    memcpy(&saved, target_pages + frame.rsp - STACK_ADDRESS, sizeof(saved));
    assert(saved.restorer == 0x420000 && saved.uc.context.rip == 0x430000);
    assert(saved.uc.context.rsp == saved_rsp && frame.rip == 0x410000);
    assert(translated_pages == 3);
    assert((frame.rsp & 15) == 8 && (saved.uc.context.fpstate & 63) == 0);
    for (unsigned i = saved_rsp - STACK_ADDRESS - 128; i < saved_rsp - STACK_ADDRESS; ++i)
        assert(target_pages[i] == 0x5a);
    assert(!target.pending_signals && (target.blocked_signals & (1u << (2 - 1))));
    target.blocked_signals = 0;
    target.restart_syscall = __NR_nanosleep + 1;
    target.nanosleep_deadline = 120;
    target.nanosleep_remaining = STACK_ADDRESS + 64;
    target.signal_actions[2].flags = LINUX_SA_RESTART;
    frame = (struct trap_frame){.rsp = saved_rsp, .rip = 0x430000, .cs = 0x23};
    assert(kernel_signal_queue_task(&target, 2) == 0);
    assert(kernel_signal_deliver_pending(&target, &frame) == 1);
    memcpy(&saved, target_pages + frame.rsp - STACK_ADDRESS, sizeof(saved));
    assert(saved.uc.context.rip == 0x430002 && (int64_t)saved.uc.context.rax == -4);
    struct linux_timespec remaining;
    memcpy(&remaining, target_pages + 64, sizeof(remaining));
    assert(remaining.tv_sec == 0 && remaining.tv_nsec == 200000000);
    assert(!target.nanosleep_deadline && !target.nanosleep_remaining);
    uint64_t process_pending = 1ULL << 1;
    target.shared_process_pending = &process_pending;
    target.blocked_signals = 0;
    frame = (struct trap_frame){.rsp = saved_rsp, .rip = 0x430000, .cs = 0x23};
    assert(kernel_signal_deliver_pending(&target, &frame) == 1);
    assert(!process_pending && !target.pending_signals);
    process_pending = 1ULL << 1;
    target.pending_signals = 1ULL << 9;
    target.signal_actions[10] = target.signal_actions[2];
    target.blocked_signals = 0;
    frame = (struct trap_frame){.rsp = saved_rsp, .rip = 0x430000, .cs = 0x23};
    assert(kernel_signal_deliver_pending(&target, &frame) == 1 && frame.rdi == 10);
    assert(process_pending == (1ULL << 1) && !target.pending_signals);
    target.blocked_signals = 0;
    frame = (struct trap_frame){.rsp = saved_rsp, .rip = 0x430000, .cs = 0x23};
    assert(kernel_signal_deliver_pending(&target, &frame) == 1 && frame.rdi == 2);
    assert(!process_pending);
    for (unsigned i = 0; i < sizeof(target_pages); ++i) assert(previous_stack[i] == 0xa5);
    munmap(previous_stack, sizeof(target_pages));
    puts("Signal delivery writes both target stack pages without touching the previous address space");
    return 0;
}
