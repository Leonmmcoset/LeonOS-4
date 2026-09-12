#include <ntclks/usercopy.h>
#include <ntclks/sched.h>
#include <ntclks/paging.h>
#include <ntclks/syscall.h>
#include <linux/errno.h>

/**
 * @brief Copy through the target's page tables rather than the current CR3.
 * @param task Pinned destination task under the kernel execution lock.
 * @param address Start of the destination user range.
 * @param source Kernel bytes to copy.
 * @param size Number of bytes; earlier pages remain written if a later page fails.
 * @return Zero, or -EFAULT for range, demand paging or COW failure.
 */
int user_copy_to_task(struct task *task, uint64_t address, const void *source, uint64_t size)
{
    if (!size) return 0;
    if (!task || address < NTCLKS_USER_BASE || address >= NTCLKS_USER_TOP ||
        size > NTCLKS_USER_TOP - address) return -LINUX_EFAULT;
    for (uint64_t copied = 0; copied < size;) {
        uint64_t destination = address + copied;
        if (!address_space_user_page_writable(sched_task_as(task), destination)) {
            if (!address_space_handle_cow_fault(sched_task_as(task), destination) &&
                !syscall_handle_task_page_fault(task, destination, 6)) return -LINUX_EFAULT;
            if (!address_space_user_page_writable(sched_task_as(task), destination)) return -LINUX_EFAULT;
        }
        uint64_t phys = address_space_user_page_phys(sched_task_as(task), destination);
        uint64_t offset = destination & 4095u, take = 4096u - offset;
        if (!phys) return -LINUX_EFAULT;
        if (take > size - copied) take = size - copied;
        __builtin_memcpy((void *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + phys + offset),
                         (const uint8_t *)source + copied, take);
        copied += take;
    }
    return 0;
}
