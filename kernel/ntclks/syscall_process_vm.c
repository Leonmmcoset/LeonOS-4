/* Linux v6.12 mm/process_vm_access.c, native x86-64 vector and copy ordering. */
#include <ntclks/syscall_internal.h>
#include <ntclks/syscall.h>
#include <ntclks/usercopy.h>
#include <ntclks/paging.h>
#include <ntclks/mm.h>
#include <ntclks/page_cache.h>
#include <ntclks/heap.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/uio.h>

#define PVM_PAGE_SIZE 4096ULL
#define PVM_MAX_RW_COUNT 0x7ffff000ULL
#define PVM_MAX_PIN_PAGES 1024u

/** @brief Check native access_ok without faulting or checking page permissions. */
static bool process_vm_access_ok(uint64_t address, uint64_t size)
{
    const uint64_t limit = (1ULL << 47) - PVM_PAGE_SIZE;
    return address <= limit && size <= limit - address;
}

/** @brief Import lengths before addresses and keep Linux's input-error ordering. */
static int process_vm_import(uint64_t pointer, uint64_t count, bool local,
                              struct iovec **out, struct iovec fast[8], uint64_t *total)
{
    if (local) count = (uint32_t)count;
    if (count > 1024) return -LINUX_EINVAL;
    *out = fast;
    *total = 0;
    if (!count) return 0;
    if (count > 8) {
        *out = kernel_malloc(count * sizeof(**out));
        if (!*out) return -LINUX_ENOMEM;
    }
    if (!process_vm_access_ok(pointer, count * sizeof(**out))) return -LINUX_EFAULT;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t item = pointer + i * sizeof(**out), length, address;
        if (!user_range_ok(item + 8, 8)) return -LINUX_EFAULT;
        __builtin_memcpy(&length, (const void *)(uintptr_t)(item + 8), 8);
        if (!user_range_ok(item, 8)) return -LINUX_EFAULT;
        __builtin_memcpy(&address, (const void *)(uintptr_t)item, 8);
        if ((int64_t)length < 0) return -LINUX_EINVAL;
        (*out)[i] = (struct iovec){(void *)(uintptr_t)address, length};
    }
    if (!local) return 0;
    if (count == 1 && (*out)[0].iov_len > PVM_MAX_RW_COUNT) (*out)[0].iov_len = PVM_MAX_RW_COUNT;
    for (uint64_t i = 0; i < count; ++i) {
        struct iovec *v = &(*out)[i];
        if (!process_vm_access_ok((uintptr_t)v->iov_base, v->iov_len)) return -LINUX_EFAULT;
        if (v->iov_len > PVM_MAX_RW_COUNT - *total) v->iov_len = PVM_MAX_RW_COUNT - *total;
        *total += v->iov_len;
    }
    return 0;
}

/** @brief Apply mm_access(ATTACH_REALCREDS) and commoncap for the current user namespace. */
static bool process_vm_may_access(struct task *caller, struct task *target)
{
    if (sched_task_mm(caller) == sched_task_mm(target) ||
        sched_task_tgid(caller) == sched_task_tgid(target)) return true;
    bool privileged = (caller->cap_effective & (1ULL << CAP_SYS_PTRACE)) != 0;
    if (!privileged && (caller->uid != target->uid || caller->uid != target->euid ||
        caller->uid != target->suid || caller->gid != target->gid || caller->gid != target->egid ||
        caller->gid != target->sgid || sched_task_mm(target)->nondumpable)) return false;
    return privileged || !(target->cap_permitted & ~caller->cap_permitted);
}

/** @brief Resolve a page in either address space; remote GUP cannot access MMIO or grow stacks. */
static uint64_t process_vm_page(struct task *task, uint64_t address, bool write, bool remote)
{
    if (address < NTCLKS_USER_BASE || address >= NTCLKS_USER_TOP) return 0;
    struct address_space *as = sched_task_as(task);
    if (remote) {
        if (address_space_user_page_is_device(as, address)) return 0;
        struct task_vma *vma = NULL;
        for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
            struct task_vma *candidate = sched_task_vma_at(task, i);
            if (candidate && candidate->used && address >= candidate->start && address < candidate->end) {
                vma = candidate;
                break;
            }
        }
        if (vma) {
            if ((vma->flags & TASK_VMA_FLAG_DEVICE) ||
                !(vma->prot & (write ? TASK_VMA_PROT_WRITE : TASK_VMA_PROT_READ))) return 0;
        } else if (!address_space_user_page_phys(as, address)) {
            struct task_address_space_state *mm = sched_task_mm(task);
            uint64_t low = mm->initial_stack_low ? mm->initial_stack_low : task->stack_low;
            uint64_t top = mm->initial_stack_top ? mm->initial_stack_top : task->stack_top;
            if (!low || address < low || address >= top) return 0;
        }
    }
    bool accessible = write ? address_space_user_page_writable(as, address)
                            : address_space_user_page_readable(as, address);
    if (!accessible) {
        bool cow = write && address_space_handle_cow_fault(as, address);
        if (!cow && !syscall_handle_task_page_fault(task, address, write ? 6 : 4)) return 0;
    }
    if (write ? !address_space_user_page_writable(as, address)
              : !address_space_user_page_readable(as, address)) return 0;
    return address_space_user_page_phys(as, address);
}

/** @brief Copy a pinned remote page across local vector/page boundaries, counting partial writes. */
static int process_vm_copy_page(struct task *caller, uint64_t remote_phys, uint64_t offset,
                                uint64_t size, const struct iovec *local, uint32_t count,
                                uint32_t *index, uint64_t *position, uint64_t *remaining, bool write)
{
    uint64_t copied = 0;
    while (copied < size && *remaining) {
        while (*index < count && *position == local[*index].iov_len) {
            ++*index;
            *position = 0;
        }
        if (*index == count) return -LINUX_EFAULT;
        uint64_t address = (uintptr_t)local[*index].iov_base + *position;
        uint64_t local_phys = process_vm_page(caller, address, !write, false);
        if (!local_phys) return -LINUX_EFAULT;
        uint64_t page_offset = address & (PVM_PAGE_SIZE - 1);
        uint64_t take = PVM_PAGE_SIZE - page_offset;
        if (take > size - copied) take = size - copied;
        if (take > local[*index].iov_len - *position) take = local[*index].iov_len - *position;
        void *here = (void *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + local_phys + page_offset);
        void *there = (void *)(uintptr_t)(NTCLKS_KERNEL_DIRECT_MAP_BASE + remote_phys + offset + copied);
        if (write) __builtin_memcpy(there, here, take);
        else __builtin_memcpy(here, there, take);
        copied += take;
        *position += take;
        *remaining -= take;
    }
    return 0;
}

/**
 * @brief Transfer bytes between native iovecs using independently validated address spaces.
 * @param pid Target TID/PID, not a process-group selector.
 * @param local_pointer User address of the caller's vectors.
 * @param local_count Native unsigned long; import_iovec truncates to unsigned int.
 * @param remote_pointer User address of the target-address vectors, stored in the caller.
 * @param remote_count Native unsigned long, checked before truncation.
 * @param flags Must be zero.
 * @param write True for process_vm_writev, false for process_vm_readv.
 * @return Transferred bytes or Linux errno; prior transfer wins over a later copy fault.
 * The native dispatch execution lock pins task/MM lifetime and serializes mapping changes.
 */
int64_t syscall_process_vm(int32_t pid, uint64_t local_pointer, uint64_t local_count,
                           uint64_t remote_pointer, uint64_t remote_count, uint64_t flags, bool write)
{
    if (flags) return -LINUX_EINVAL;
    struct iovec fast_local[8], fast_remote[8], *local = fast_local, *remote = fast_remote;
    uint64_t total = 0, unused;
    int64_t result = process_vm_import(local_pointer, local_count, true, &local, fast_local, &total);
    if (result < 0 || !total) goto done;
    result = process_vm_import(remote_pointer, remote_count, false, &remote, fast_remote, &unused);
    if (result < 0) goto done;
    uint64_t nr_pages = 0;
    for (uint64_t i = 0; i < remote_count; ++i) if (remote[i].iov_len) {
        uint64_t address = (uintptr_t)remote[i].iov_base;
        uint64_t pages = (address + remote[i].iov_len - 1) / PVM_PAGE_SIZE - address / PVM_PAGE_SIZE + 1;
        if (pages > nr_pages) nr_pages = pages;
    }
    if (!nr_pages) goto done;
    uint64_t stack_pages[16], *pages = stack_pages;
    if (nr_pages > 16) {
        uint64_t capacity = nr_pages > PVM_MAX_PIN_PAGES ? PVM_MAX_PIN_PAGES : nr_pages;
        pages = kernel_malloc(capacity * sizeof(*pages));
        if (!pages) { result = -LINUX_ENOMEM; goto done; }
    }
    struct task *caller = sched_current_task(), *target = sched_find((uint32_t)pid);
    result = -LINUX_ESRCH;
    if (!caller || !target || target->kind != TASK_KIND_USER || target->state == TASK_EXITED ||
        !sched_task_as(target)->cr3) goto free_pages;
    result = -LINUX_EPERM;
    if (!process_vm_may_access(caller, target)) goto free_pages;
    uint64_t remaining = total, position = 0;
    uint32_t index = 0;
    result = 0;
    for (uint64_t i = 0; i < remote_count && remaining && !result; ++i) {
        uint64_t address = (uintptr_t)remote[i].iov_base, length = remote[i].iov_len;
        if (!length) continue;
        uint64_t offset = address & (PVM_PAGE_SIZE - 1), page = address - offset;
        nr_pages = (address + length - 1) / PVM_PAGE_SIZE - address / PVM_PAGE_SIZE + 1;
        while (!result && nr_pages && remaining) {
            uint32_t requested = nr_pages > PVM_MAX_PIN_PAGES ? PVM_MAX_PIN_PAGES : (uint32_t)nr_pages;
            uint32_t pinned = 0;
            for (; pinned < requested; ++pinned) {
                uint64_t phys = process_vm_page(target, page + pinned * PVM_PAGE_SIZE, write, true);
                if (!phys) break;
                bool cached = page_cache_retain(phys) == 0;
                if (!cached) mm_retain_page(phys);
                pages[pinned] = phys | (cached ? 1 : 0);
            }
            if (!pinned) { result = -LINUX_EFAULT; break; }
            uint64_t bytes = pinned * PVM_PAGE_SIZE - offset;
            if (bytes > length) bytes = length;
            uint64_t left = bytes;
            for (uint32_t p = 0; p < pinned; ++p) {
                uint64_t take = PVM_PAGE_SIZE - offset;
                if (take > left) take = left;
                if (!result && remaining && take)
                    result = process_vm_copy_page(caller, pages[p] & ~1ULL, offset, take,
                        local, (uint32_t)local_count, &index, &position, &remaining, write);
                if (pages[p] & 1) page_cache_release(pages[p] & ~1ULL);
                else mm_free_page(pages[p]);
                left -= take;
                offset = 0;
            }
            length -= bytes;
            nr_pages -= pinned;
            page += pinned * PVM_PAGE_SIZE;
        }
    }
    if (remaining != total) result = (int64_t)(total - remaining);
free_pages:
    if (pages != stack_pages) kernel_free(pages);
done:
    if (local && local != fast_local) kernel_free(local);
    if (remote && remote != fast_remote) kernel_free(remote);
    return result;
}
