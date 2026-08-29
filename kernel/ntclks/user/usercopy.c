/*
 * LeonOS user-copy implementation: performs checked Ring-3 buffer access.
 * Validates pointers and transfers strings or bytes without kernel overreach.
 */
#include <ntclks/usercopy.h>
#include <ntclks/paging.h>
#include <ntclks/sched.h>
#include <ntclks/syscall.h>

#define PAGE_SIZE 4096ULL

/**
 * @brief Round value down to the previous 4096-byte page boundary.
 */
static uint64_t align_down_page(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1ULL);
}

/**
 * @brief Return true if [ptr, ptr+len) is a valid user range with every page mapped, faulting pages in for the current user task on demand.
 */
bool user_range_ok(uint64_t ptr, uint64_t len)
{
    struct task *task;
    uint64_t end;
    uint64_t page;
    if (len == 0) {
        return true;
    }
    if (ptr < NTCLKS_USER_BASE || ptr > NTCLKS_USER_TOP) {
        return false;
    }
    if (len > NTCLKS_USER_TOP - ptr) {
        return false;
    }
    end = ptr + len;
    task = sched_current_task();
    if (!task || task->kind != TASK_KIND_USER) {
        return true;
    }
    for (page = align_down_page(ptr); page < end; page += PAGE_SIZE) {
        if (!address_space_user_page_phys(&task->as, page) &&
            !syscall_handle_user_page_fault(page, 0)) {
            return false;
        }
        if (!address_space_user_page_phys(&task->as, page)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Return the length of the NUL-terminated string at s, checking each byte is user-accessible and never exceeding max.
 */
size_t user_strlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && user_range_ok((uint64_t)(uintptr_t)(s + n), 1) && s[n]) {
        ++n;
    }
    return n;
}
