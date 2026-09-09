#include <assert.h>
#include <stdio.h>
#include "../../kernel/ntclks/syscall_mm.c"

uint32_t sched_task_vma_capacity(const struct task *task) { (void)task; return SCHED_TASK_VMA_MAX; }
struct task_vma *sched_task_vma_at(struct task *task, uint32_t index) { return &task->vmas[index]; }

int main(void)
{
    struct task task = {0};
    uint64_t base = NTCLKS_USER_BASE;
    task.vmas[0] = (struct task_vma){.used = 1, .start = base, .end = base + 8192};
    task.stack_top = NTCLKS_USER_TOP - 4096;
    task.stack_low = task.stack_top - 65536;
    uint64_t used = 8192 + 65536;
    task.limits.as.rlim_cur = used;
    assert(task_vma_total_bytes(&task) == used);
    assert(task_address_space_can_map(&task, base, base + 8192));
    assert(!task_address_space_can_map(&task, base, base + 12288));
    task.limits.as.rlim_cur = used + 4095;
    assert(!task_address_space_can_map(&task, base + 8192, base + 12288));
    task.limits.as.rlim_cur = used + 4096;
    assert(task_address_space_can_map(&task, base + 8192, base + 12288));
    task.address_space.initial_stack_top = task.stack_top;
    task.address_space.initial_stack_low = task.stack_low;
    task.stack_low = base;
    task.stack_top = base + 8192;
    assert(task_vma_total_bytes(&task) == used);
    task.limits.as.rlim_cur = 0;
    assert(!task_address_space_can_map(&task, base, base + 8192));
    task.limits.as.rlim_cur = LINUX_RLIM_INFINITY;
    assert(task_address_space_can_map(&task, base + 8192, base + 12288));
    puts("PASS address-space accounting: shared initial stack, net replacement growth, rounding, zero and infinity");
}
