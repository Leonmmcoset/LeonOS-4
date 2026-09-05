#include <assert.h>
#include <stdio.h>
#include <ntclks/usercopy.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>

static struct task current;
static uint64_t flags[3];
static unsigned cow_copies;
struct task *sched_current_task(void) { return &current; }
static unsigned index_for(uint64_t address) { return (address - NTCLKS_USER_BASE) / 4096; }
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t address)
{
    (void)as;
    unsigned index = index_for(address);
    return index < 3 && (flags[index] & NTCLKS_PAGE_PRESENT) ? 0x1000 : 0;
}
bool address_space_user_page_writable(const struct address_space *as, uint64_t address)
{
    (void)as;
    unsigned index = index_for(address);
    return index < 3 && (flags[index] & NTCLKS_PAGE_WRITABLE);
}
bool address_space_handle_cow_fault(struct address_space *as, uint64_t address)
{
    (void)as;
    unsigned index = index_for(address);
    if (index >= 3 || !(flags[index] & NTCLKS_PAGE_COW)) return false;
    flags[index] = NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | NTCLKS_PAGE_WRITABLE;
    ++cow_copies;
    return true;
}
int syscall_handle_user_page_fault(uint64_t address, uint64_t error)
{
    (void)address; (void)error;
    return 0;
}
int main(void)
{
    current.kind = TASK_KIND_USER;
    flags[0] = NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | NTCLKS_PAGE_WRITABLE;
    flags[1] = NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | NTCLKS_PAGE_COW;
    flags[2] = NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER;
    assert(user_range_writable(NTCLKS_USER_BASE + 1, 0));
    assert(!user_range_writable(0, 4));
    assert(!user_range_writable(UINT64_MAX - 2, 16));
    assert(user_range_writable(NTCLKS_USER_BASE, 4096));
    assert(cow_copies == 0);
    assert(user_range_writable(NTCLKS_USER_BASE + 4090, 12));
    assert(cow_copies == 1);
    assert(!user_range_writable(NTCLKS_USER_BASE + 8190, 12));
    assert(user_range_ok(NTCLKS_USER_BASE + 8192, 4));
    assert(!user_range_writable(NTCLKS_USER_BASE + 8192, 4));
    assert(!user_range_writable(NTCLKS_USER_BASE + 12288, 4));
    puts("GPU copy-out tests passed: readonly pages, COW, cross-page and overflow ranges");
    return 0;
}
