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

bool user_range_writable(uint64_t ptr, uint64_t len)
{ return ptr >= STACK_ADDRESS && ptr + len <= STACK_ADDRESS + sizeof(target_pages); }
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr)
{
    assert(as == &target.as);
    assert(vaddr >= STACK_ADDRESS && vaddr < STACK_ADDRESS + sizeof(target_pages));
    ++translated_pages;
    return (uintptr_t)target_pages + ((vaddr - STACK_ADDRESS) & ~4095ULL);
}

int main(void)
{
    unsigned char *previous_stack = mmap((void *)STACK_ADDRESS, sizeof(target_pages),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(previous_stack == (void *)STACK_ADDRESS);
    memset(previous_stack, 0xa5, sizeof(target_pages));
    target.kind = TASK_KIND_USER;
    target.state = TASK_READY;
    target.stack_low = STACK_ADDRESS;
    target.pending_signals = 1u << 2;
    target.signal_actions[2] = (struct kernel_signal_action){
        .handler = 0x410000, .restorer = 0x420000};
    struct trap_frame frame = {.rsp = STACK_ADDRESS + 4096 + 128,
                                .rip = 0x430000, .cs = 0x23};
    uint64_t saved_rsp = frame.rsp;
    assert(kernel_signal_deliver_pending(&target, &frame) == 1);
    struct leonos_rt_sigframe saved;
    memcpy(&saved, target_pages + frame.rsp - STACK_ADDRESS, sizeof(saved));
    assert(saved.restorer == 0x420000 && saved.context.rip == 0x430000);
    assert(saved.context.rsp == saved_rsp && frame.rip == 0x410000);
    assert(translated_pages == 2);
    assert(!target.pending_signals && (target.blocked_signals & (1u << 2)));
    for (unsigned i = 0; i < sizeof(target_pages); ++i) assert(previous_stack[i] == 0xa5);
    munmap(previous_stack, sizeof(target_pages));
    puts("Signal delivery writes both target stack pages without touching the previous address space");
    return 0;
}
