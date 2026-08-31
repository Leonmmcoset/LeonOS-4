/*
 * LeonOS memory syscall handlers: VMA metadata, lazy mappings, page faults,
 * and mmap/mprotect/munmap operations.
 */
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>
#include <ntclks/page_cache.h>
#include <ntclks/sched.h>
#include <ntclks/storage.h>
#include <ntclks/syscall.h>
#include <ntclks/syscall_internal.h>
#include <ntclks/userland.h>

#define PAGE_SIZE 4096ULL
#define LINUX_PROT_READ TASK_VMA_PROT_READ
#define LINUX_PROT_WRITE TASK_VMA_PROT_WRITE
#define LINUX_PROT_EXEC TASK_VMA_PROT_EXEC
#define LINUX_MAP_SHARED 0x01u
#define LINUX_MAP_PRIVATE 0x02u
#define LINUX_MAP_FIXED 0x10u
#define LINUX_MAP_ANONYMOUS 0x20u
#define LINUX_MAP_SUPPORTED (LINUX_MAP_SHARED | LINUX_MAP_PRIVATE | LINUX_MAP_FIXED | LINUX_MAP_ANONYMOUS)

/**
 * Align up page.
 * @param value Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint64_t align_up_page(uint64_t value)
{
    return (value + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
}

/**
 * Align down page.
 * @param value Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint64_t align_down_page(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1ULL);
}

/**
 * Align user len.
 * @param len Maximum number of elements available in the related buffer.
 * @param out Output storage updated by the function.
 * @return The value or status produced by the operation.
 */
static int align_user_len(uint64_t len, uint64_t *out)
{
    if (!len || len > NTCLKS_USER_TOP - NTCLKS_USER_BASE) {
        return -LEONOS_EINVAL;
    }
    if (len > UINT64_MAX - (PAGE_SIZE - 1ULL)) {
        return -LEONOS_EINVAL;
    }
    *out = align_up_page(len);
    return *out ? 0 : -LEONOS_EINVAL;
}

/**
 * Task mmap top.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint64_t task_mmap_top(const struct task *task)
{
    uint64_t stack_top = (task && task->stack_top) ? task->stack_top : NTCLKS_USER_TOP - PAGE_SIZE;
    uint64_t stack_low = stack_top - (uint64_t)NTCLKS_USER_STACK_MAX_PAGES * PAGE_SIZE;
    if (stack_low > NTCLKS_USER_BASE + PAGE_SIZE) {
        return stack_low - PAGE_SIZE;
    }
    return stack_low;
}

/**
 * Task vma free slot.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static struct task_vma *task_vma_free_slot(struct task *task)
{
    if (!task) {
        return NULL;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (vma && !vma->used) {
            return vma;
        }
    }
    return sched_task_vma_at(task, sched_task_vma_capacity(task));
}

/**
 * Task vma attrs match.
 * @param vma Value supplied by the caller.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @return The value or status produced by the operation.
 */
static int task_vma_attrs_match(const struct task_vma *vma, uint64_t prot, uint32_t flags)
{
    return vma && vma->used && vma->prot == (uint32_t)prot && vma->flags == flags;
}

/**
 * Storage nodes equal.
 * @param a Value supplied by the caller.
 * @param b Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int storage_nodes_equal(const struct storage_node *a, const struct storage_node *b)
{
    return a && b &&
           a->type == b->type &&
           a->flags == b->flags &&
           a->first_cluster == b->first_cluster &&
           a->volume_id == b->volume_id &&
           a->size == b->size;
}

/**
 * Task vma file attrs match.
 * @param vma Value supplied by the caller.
 * @param file_node Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_vma_file_attrs_match(const struct task_vma *vma,
                                     const struct storage_node *file_node)
{
    if (!vma || !(vma->flags & TASK_VMA_FLAG_FILE)) {
        return 1;
    }
    return storage_nodes_equal(&vma->file_node, file_node) &&
           file_node && vma->file_limit == file_node->size;
}

/**
 * Task vma clear.
 * @param vma Value supplied by the caller.
 */
static void task_vma_clear(struct task_vma *vma)
{
    if (vma) {
        *vma = (struct task_vma){0};
    }
}

/**
 * Task vma containing.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static struct task_vma *task_vma_containing(struct task *task, uint64_t start, uint64_t end)
{
    if (!task) {
        return NULL;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (vma && vma->used && start >= vma->start && end <= vma->end) {
            return vma;
        }
    }
    return NULL;
}

/**
 * Task vma left adjacent.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param file_node Value supplied by the caller.
 * @param file_offset Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static struct task_vma *task_vma_left_adjacent(struct task *task, uint64_t start,
                                                uint64_t prot, uint32_t flags,
                                                const struct storage_node *file_node,
                                                uint64_t file_offset)
{
    if (!task) {
        return NULL;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (!vma) continue;
        if (task_vma_attrs_match(vma, prot, flags) &&
            vma->end == start &&
            task_vma_file_attrs_match(vma, file_node) &&
            (!(flags & TASK_VMA_FLAG_FILE) ||
             vma->file_offset + (vma->end - vma->start) == file_offset)) {
            return vma;
        }
    }
    return NULL;
}

/**
 * Task vma right adjacent.
 * @param task Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param file_node Value supplied by the caller.
 * @param file_offset Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 * @return The value or status produced by the operation.
 */
static struct task_vma *task_vma_right_adjacent(struct task *task, uint64_t end,
                                                 uint64_t prot, uint32_t flags,
                                                 const struct storage_node *file_node,
                                                 uint64_t file_offset,
                                                 uint64_t len)
{
    if (!task) {
        return NULL;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (!vma) continue;
        if (task_vma_attrs_match(vma, prot, flags) &&
            vma->start == end &&
            task_vma_file_attrs_match(vma, file_node) &&
            (!(flags & TASK_VMA_FLAG_FILE) ||
             file_offset + len == vma->file_offset)) {
            return vma;
        }
    }
    return NULL;
}

/**
 * Task vma can record mapping.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param file_node Value supplied by the caller.
 * @param file_offset Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_vma_can_record_mapping(struct task *task, uint64_t start, uint64_t end,
                                       uint64_t prot, uint32_t flags,
                                       const struct storage_node *file_node,
                                       uint64_t file_offset)
{
    uint64_t len = end - start;
    return task_vma_left_adjacent(task, start, prot, flags, file_node, file_offset) ||
           task_vma_right_adjacent(task, end, prot, flags, file_node, file_offset, len) ||
           task_vma_free_slot(task);
}

/**
 * Task vma record mapping.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param file_node Value supplied by the caller.
 * @param file_offset Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_vma_record_mapping(struct task *task, uint64_t start, uint64_t end,
                                   uint64_t prot, uint32_t flags,
                                   const struct storage_node *file_node,
                                   uint64_t file_offset)
{
    uint64_t len = end - start;
    struct task_vma *left = task_vma_left_adjacent(task, start, prot, flags, file_node, file_offset);
    struct task_vma *right = task_vma_right_adjacent(task, end, prot, flags, file_node, file_offset, len);
    if (left && right && left != right) {
        left->end = right->end;
        task_vma_clear(right);
        return 0;
    }
    if (left) {
        left->end = end;
        return 0;
    }
    if (right) {
        right->start = start;
        return 0;
    }

    struct task_vma *slot = task_vma_free_slot(task);
    if (!slot) {
        return -LEONOS_ENOMEM;
    }
    slot->used = 1;
    slot->prot = (uint32_t)prot;
    slot->max_prot = (uint32_t)prot;
    slot->flags = flags;
    slot->reserved = 0;
    slot->start = start;
    slot->end = end;
    slot->file_offset = file_offset;
    slot->file_limit = file_node ? file_node->size : 0;
    if (file_node) {
        slot->file_node = *file_node;
    } else {
        slot->file_node = (struct storage_node){0};
    }
    return 0;
}

/**
 * Task vma overlaps.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_vma_overlaps(const struct task *task, uint64_t start, uint64_t end)
{
    if (!task) {
        return 1;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        const struct task_vma *vma = sched_task_vma_at((struct task *)task, i);
        if (vma && vma->used && start < vma->end && end > vma->start) {
            return 1;
        }
    }
    return 0;
}

/**
 * Task user pages free.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_user_pages_free(const struct task *task, uint64_t start, uint64_t end)
{
    if (!task || start < NTCLKS_USER_BASE || start >= end || end > NTCLKS_USER_TOP) {
        return 0;
    }
    if (start < NTCLKS_KERNEL_HOLE_END && end > NTCLKS_KERNEL_HOLE_START) {
        return 0;
    }
    if (task_vma_overlaps(task, start, end)) {
        return 0;
    }
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        if (address_space_user_page_phys(&task->as, page)) {
            return 0;
        }
    }
    return 1;
}

/**
 * Task vma count.
 * @param task Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static uint32_t task_vma_count(const struct task *task)
{
    uint32_t count = 0;
    if (!task) {
        return 0;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        const struct task_vma *vma = sched_task_vma_at((struct task *)task, i);
        if (vma && vma->used) {
            ++count;
        }
    }
    return count;
}

static uint64_t task_vma_total_bytes(const struct task *task)
{
    uint64_t total = 0;
    if (!task) {
        return 0;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        const struct task_vma *vma = sched_task_vma_at((struct task *)task, i);
        if (!vma) continue;
        if (!vma->used || vma->end < vma->start) {
            continue;
        }
        if (UINT64_MAX - total < vma->end - vma->start) {
            return UINT64_MAX;
        }
        total += vma->end - vma->start;
    }
    return total;
}

/**
 * Task find mmap region.
 * @param task Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 * @return The value or status produced by the operation.
 */
static uint64_t task_find_mmap_region(struct task *task, uint64_t len)
{
    uint64_t top = task_mmap_top(task);
    if (top <= NTCLKS_USER_MMAP_BASE || len > top - NTCLKS_USER_MMAP_BASE) {
        return 0;
    }
    for (uint64_t start = NTCLKS_USER_MMAP_BASE; start + len <= top; start += PAGE_SIZE) {
        if (task_user_pages_free(task, start, start + len)) {
            return start;
        }
    }
    return 0;
}

/**
 * @brief Picolibc grows its sbrk heap upward from the first anonymous mapping. Keep read-only file mappings at the opposite end of the mmap interval so a large font, dictionary, or other resource cannot occupy the heap's next contiguous extension.
 */
static uint64_t task_find_file_mmap_region(struct task *task, uint64_t len)
{
    uint64_t top = task_mmap_top(task);
    uint64_t start;
    if (top <= NTCLKS_USER_MMAP_BASE || len > top - NTCLKS_USER_MMAP_BASE) {
        return 0;
    }
    start = align_down_page(top - len);
    for (;;) {
        if (task_user_pages_free(task, start, start + len)) {
            return start;
        }
        if (start < NTCLKS_USER_MMAP_BASE + PAGE_SIZE) {
            break;
        }
        start -= PAGE_SIZE;
    }
    return 0;
}

/**
 * Task unmap pages.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 */
static void task_unmap_pages(struct task *task, uint64_t start, uint64_t end)
{
    if (!task) {
        return;
    }
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        struct task_vma *vma = task_vma_containing(task, page, page + PAGE_SIZE);
        uint64_t phys = address_space_unmap_user_page(&task->as, page);
        if (phys) {
            if ((vma && (vma->flags & TASK_VMA_FLAG_DEVICE)) ||
                address_space_user_page_is_device(&task->as, page)) {
                /* Framebuffer mappings borrow reserved VRAM. */
            } else if (vma && (vma->flags & TASK_VMA_FLAG_SHARED_FILE)) {
                page_cache_release(phys);
            } else {
                mm_free_page(phys);
            }
        }
    }
}

/**
 * Zero phys page.
 * @param phys Value supplied by the caller.
 */
static void zero_phys_page(uint64_t phys)
{
    uint8_t *ptr = (uint8_t *)(uintptr_t)phys;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        ptr[i] = 0;
    }
}

/**
 * Task map anonymous pages.
 * @param task Value supplied by the caller.
 * @param start Value supplied by the caller.
 * @param end Value supplied by the caller.
 * @param page_flags Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
static int task_map_anonymous_pages(struct task *task, uint64_t start, uint64_t end,
                                    uint64_t page_flags)
{
    /* Anonymous mappings have no executable provenance.  An application that
     * needs JIT code must explicitly map RX after producing it; W+X is never
     * accepted by the syscall. */
    page_flags |= NTCLKS_PAGE_NOEXEC;
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
        uint64_t phys = mm_alloc_page();
        if (!phys || !address_space_map_user_page(&task->as, page, phys, page_flags)) {
            uint64_t mapped_kib = (page - start) / 1024ULL;
            console_printf("[ntclks] anonymous mmap failed pid=%u range=0x%llx-0x%llx "
                           "at=0x%llx mapped=%llu KiB free=%llu KiB vmas=%u cause=%s\n",
                           task ? task->pid : 0,
                           (unsigned long long)start,
                           (unsigned long long)end,
                           (unsigned long long)page,
                           (unsigned long long)mapped_kib,
                           (unsigned long long)mm_free_memory_kib(),
                           task_vma_count(task),
                           phys ? "page-table" : "physical-memory");
            if (phys) {
                mm_free_page(phys);
            }
            task_unmap_pages(task, start, page);
            return -LEONOS_ENOMEM;
        }
    }
    return 0;
}

/**
 * Load file cache page.
 * @param task Value supplied by the caller.
 * @param vma Value supplied by the caller.
 * @param page Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
struct file_page_load_context {
    const struct storage_node *node;
    uint64_t offset;
    uint64_t limit;
};

static int load_file_cache_page(uint64_t phys, void *opaque)
{
    struct file_page_load_context *context = (struct file_page_load_context *)opaque;
    uint64_t offset;
    uint32_t want;
    uint32_t got = 0;
    if (!context || !context->node) {
        return -LEONOS_EINVAL;
    }
    zero_phys_page(phys);
    offset = context->offset;
    if (offset >= context->limit) {
        return 0;
    }
    want = context->limit - offset < PAGE_SIZE
               ? (uint32_t)(context->limit - offset)
               : (uint32_t)PAGE_SIZE;
    if (storage_read_node(context->node, offset, (void *)(uintptr_t)phys,
                          want, &got) < 0 || got != want) {
        return -LEONOS_EIO;
    }
    return 0;
}

static int task_map_file_vma_page(struct task *task, const struct task_vma *vma,
                                  uint64_t page)
{
    if (!task || !vma || !(vma->flags & TASK_VMA_FLAG_FILE)) {
        return -LEONOS_EINVAL;
    }
    uint64_t phys = 0;
    int cached = (vma->flags & TASK_VMA_FLAG_SHARED_FILE) != 0;
    struct file_page_load_context context;
    if (cached) {
        context.node = &vma->file_node;
        context.offset = vma->file_offset + (page - vma->start);
        context.limit = vma->file_limit;
        if (page_cache_load(&vma->file_node, context.offset,
                            load_file_cache_page, &context, &phys) < 0) {
            return -LEONOS_EIO;
        }
    } else {
        phys = mm_alloc_page();
    }
    if (!phys) {
        return -LEONOS_ENOMEM;
    }
    if (!cached) {
        zero_phys_page(phys);
    }

    uint64_t file_offset = vma->file_offset + (page - vma->start);
    if (!cached && file_offset < vma->file_limit) {
        uint64_t available = vma->file_limit - file_offset;
        uint32_t want = available < PAGE_SIZE ? (uint32_t)available : (uint32_t)PAGE_SIZE;
        uint32_t got = 0;
        int ret;
        /**
 * @brief A user page fault is not a restartable filesystem syscall. Force this backing-page read through the synchronous AHCI path so an in-flight async storage operation cannot surface as EAGAIN and turn a valid lazy mapping into a process-killing fault.
 */
        storage_set_io_async_context(false);
        ret = storage_read_node(&vma->file_node, file_offset,
                                (void *)(uintptr_t)phys, want, &got);
        storage_set_io_async_context(false);
        if (ret < 0 || got != want) {
            if (cached) page_cache_release(phys);
            else mm_free_page(phys);
            return ret < 0 ? storage_errno(ret) : -LEONOS_EINVAL;
        }
    }

    uint64_t page_flags = (vma->prot & LINUX_PROT_WRITE) ? NTCLKS_PAGE_WRITABLE : 0;
    if (!(vma->prot & LINUX_PROT_EXEC)) {
        page_flags |= NTCLKS_PAGE_NOEXEC;
    }
    if (!address_space_map_user_page(&task->as, page, phys, page_flags)) {
        if (cached) page_cache_release(phys);
        else mm_free_page(phys);
        return -LEONOS_ENOMEM;
    }
    return 0;
}

/**
 * Syscall handle user page fault.
 * @param fault_addr Value supplied by the caller.
 * @param error Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int syscall_handle_user_page_fault(uint64_t fault_addr, uint64_t error)
{
    struct task *task = sched_current_task();
    if (!task || task->kind != TASK_KIND_USER) {
        return 0;
    }
    uint64_t page = align_down_page(fault_addr);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return 0;
    }
    /**
 * @brief A write through either Ring-3 code or a kernel syscall helper can touch the calling process's present, read-only COW PTE. Resolve only that precise protection fault; reserved-bit and instruction-fetch faults remain fatal and ordinary kernel faults never reach this path.
 */
    if ((error & 0x3ULL) == 0x3ULL && !(error & 0x18ULL)) {
        return address_space_handle_cow_fault(&task->as, page);
    }
    if (error & 0x9ULL) {
        return 0;
    }
    if (address_space_user_page_phys(&task->as, page)) {
        return 0;
    }

    /**
 * @brief Grow the anonymous user stack on demand. The initial image maps a small working set; accesses below it consume pages down to the fixed maximum and leave one unmapped guard page below the stack.
 */
    if (!(error & 0x1ULL) && (error & 0x4ULL) && task->stack_top &&
        task->stack_low && page < task->stack_low &&
        page >= task->stack_top - (uint64_t)NTCLKS_USER_STACK_MAX_PAGES * PAGE_SIZE &&
        page >= NTCLKS_USER_BASE + PAGE_SIZE) {
        uint64_t guard = task->stack_top -
                         (uint64_t)NTCLKS_USER_STACK_MAX_PAGES * PAGE_SIZE - PAGE_SIZE;
        if (page > guard && address_space_map_user_stack_page(&task->as, page)) {
            task->stack_low = page;
            return 1;
        }
        return 0;
    }
    struct task_vma *vma = task_vma_containing(task, page, page + PAGE_SIZE);
    if (!vma || !(vma->flags & TASK_VMA_FLAG_LAZY) || !(vma->flags & TASK_VMA_FLAG_FILE)) {
        return 0;
    }
    if ((error & 0x2ULL) && !(vma->prot & LINUX_PROT_WRITE)) {
        return 0;
    }
    if ((error & 0x10ULL) && !(vma->prot & LINUX_PROT_EXEC)) {
        return 0;
    }
    {
        uint64_t loader_flags;
        userland_loader_lock(&loader_flags);
        int ret = task_map_file_vma_page(task, vma, page);
        userland_loader_unlock(loader_flags);
        if (ret < 0) {
            console_printf("[ntclks] lazy file map failed pid=%u page=0x%llx "
                           "fault=0x%llx error=0x%llx vma=0x%llx-0x%llx "
                           "flags=0x%x file_off=0x%llx file_limit=0x%llx ret=%d\n",
                           task->pid, (unsigned long long)page,
                           (unsigned long long)fault_addr,
                           (unsigned long long)error,
                           (unsigned long long)vma->start,
                           (unsigned long long)vma->end, vma->flags,
                           (unsigned long long)vma->file_offset,
                           (unsigned long long)vma->file_limit, ret);
        }
        return ret == 0;
    }
}

/**
 * Syscall mm mmap.
 * @param addr Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 * @param prot Value supplied by the caller.
 * @param flags Identifier or flags controlling the operation.
 * @param fd Value supplied by the caller.
 * @param offset Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int64_t syscall_mm_mmap(uint64_t addr, uint64_t len, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset)
{
    struct task *task = sched_current_task();
    struct task_file *file = NULL;
    uint64_t mapped_len;
    uint64_t start;
    uint64_t end;
    uint64_t page_flags = 0;
    uint32_t vma_flags = TASK_VMA_FLAG_PRIVATE;
    int anonymous;
    int device_mapping = 0;
    uint64_t device_phys = 0;
    int ret;

    if (!task || task->kind != TASK_KIND_USER) {
        return -LEONOS_EINVAL;
    }
    if (align_user_len(len, &mapped_len) < 0) {
        return -LEONOS_EINVAL;
    }
    if (task->rlimit_as) {
        uint64_t used = task_vma_total_bytes(task);
        if (used > task->rlimit_as || mapped_len > task->rlimit_as - used) {
            return -LEONOS_ENOMEM;
        }
    }
    if ((prot & ~(uint64_t)(LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC)) != 0 ||
        (prot & (LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC)) == 0) {
        return -LEONOS_EINVAL;
    }
    if ((prot & (LINUX_PROT_WRITE | LINUX_PROT_EXEC)) ==
        (LINUX_PROT_WRITE | LINUX_PROT_EXEC)) {
        return -LEONOS_EACCES;
    }
    if ((flags & ~((uint64_t)LINUX_MAP_SUPPORTED)) != 0) {
        return -LEONOS_EINVAL;
    }
    anonymous = (flags & LINUX_MAP_ANONYMOUS) != 0;
    if (anonymous && !(flags & LINUX_MAP_PRIVATE)) {
        return -LEONOS_EINVAL;
    }
    /* File-backed mappings remain private until page-cache ownership is
     * complete for fork/COW and address-space teardown.  This keeps dynamic
     * loader mmap() pages on the same well-tested lazy private path as ELF
     * PT_LOAD segments. */
    vma_flags |= anonymous ? TASK_VMA_FLAG_ANON :
        (TASK_VMA_FLAG_FILE | TASK_VMA_FLAG_LAZY | TASK_VMA_FLAG_PRIVATE);
    if (anonymous) {
        if (fd != UINT64_MAX || offset != 0) {
            return -LEONOS_EINVAL;
        }
    } else {
        if (offset & (PAGE_SIZE - 1ULL)) {
            return -LEONOS_EINVAL;
        }
        if (offset > UINT64_MAX - mapped_len) {
            return -LEONOS_EINVAL;
        }
        if (!(prot & LINUX_PROT_READ)) {
            return -LEONOS_EINVAL;
        }
        if (fd > INT32_MAX) {
            return -LEONOS_EBADF;
        }
        file = task_file_for_fd(task, (int)fd);
        if (!file || !file_can_read(file)) {
            return -LEONOS_EBADF;
        }
        if (file->flags & TASK_FILE_FLAG_DEV_NODE) {
            const struct framebuffer *fb;
            uint64_t bytes;
            if (file->node.first_cluster != STORAGE_DEV_KIND_FB0) {
                return -LEONOS_EINVAL;
            }
            fb = framebuffer_get();
            if (!fb || !fb->available || !fb->pixels) {
                return -LEONOS_EINVAL;
            }
            bytes = (uint64_t)fb->pitch * fb->height;
            if (offset > bytes || mapped_len > bytes - offset) {
                return -LEONOS_EINVAL;
            }
            device_phys = (uint64_t)(uintptr_t)fb->pixels + offset;
            if (device_phys & (PAGE_SIZE - 1ULL)) {
                return -LEONOS_EINVAL;
            }
            device_mapping = 1;
            vma_flags = TASK_VMA_FLAG_PRIVATE | TASK_VMA_FLAG_DEVICE;
        } else if (file->node.type != LEONOS_FS_TYPE_FILE ||
                   !(flags & LINUX_MAP_PRIVATE)) {
            return -LEONOS_EINVAL;
        }
    }

    if (flags & LINUX_MAP_FIXED) {
        if ((addr & (PAGE_SIZE - 1ULL)) != 0) {
            return -LEONOS_EINVAL;
        }
        start = addr;
    } else {
        start = anonymous ? task_find_mmap_region(task, mapped_len)
                          : task_find_file_mmap_region(task, mapped_len);
        if (!start) {
            console_printf("[ntclks] mmap virtual range unavailable pid=%u bytes=%llu "
                           "top=0x%llx vmas=%u\n",
                           task->pid,
                           (unsigned long long)mapped_len,
                           (unsigned long long)task_mmap_top(task),
                           task_vma_count(task));
            return -LEONOS_ENOMEM;
        }
    }
    if (start < NTCLKS_USER_BASE || start >= NTCLKS_USER_TOP || mapped_len > NTCLKS_USER_TOP - start) {
        return -LEONOS_EINVAL;
    }
    end = start + mapped_len;
    if (end > task_mmap_top(task) || !task_user_pages_free(task, start, end)) {
        return (flags & LINUX_MAP_FIXED) ? -LEONOS_EINVAL : -LEONOS_ENOMEM;
    }
    if (!task_vma_can_record_mapping(task, start, end, prot, vma_flags,
                                     file ? &file->node : NULL, offset)) {
        console_printf("[ntclks] mmap VMA slots exhausted pid=%u bytes=%llu vmas=%u\n",
                       task->pid,
                       (unsigned long long)mapped_len,
                       task_vma_count(task));
        return -LEONOS_ENOMEM;
    }

    /**
 * @brief File mappings are lazy: reserve the page-table range now so a first instruction/data fault can replace the inherited kernel huge-page identity mapping even when the CPU reports the fault as present.
 */
    if (!anonymous && !device_mapping && !address_space_prepare_user_range(&task->as, start, end)) {
        return -LEONOS_ENOMEM;
    }

    if (prot & LINUX_PROT_WRITE) {
        page_flags |= NTCLKS_PAGE_WRITABLE;
    }
    if (!(prot & LINUX_PROT_EXEC)) {
        page_flags |= NTCLKS_PAGE_NOEXEC;
    }
    if (device_mapping) {
        uint64_t phys = device_phys;
        for (uint64_t page = start; page < end; page += PAGE_SIZE, phys += PAGE_SIZE) {
            if (!address_space_map_user_page(&task->as, page, phys,
                                             page_flags | NTCLKS_PAGE_DEVICE)) {
                task_unmap_pages(task, start, page);
                return -LEONOS_ENOMEM;
            }
        }
        ret = 0;
    } else if (anonymous) {
        ret = task_map_anonymous_pages(task, start, end, page_flags);
    } else {
        ret = 0;
    }
    if (ret < 0) {
        return ret;
    }

    if (task_vma_record_mapping(task, start, end, prot, vma_flags,
                                file ? &file->node : NULL, offset) < 0) {
        task_unmap_pages(task, start, end);
        return -LEONOS_ENOMEM;
    }
    return (int64_t)start;
}

/**
 * Syscall mm mprotect.
 * @param addr Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 * @param prot Value supplied by the caller.
 * @return The value or status produced by the operation.
 */
int64_t syscall_mm_mprotect(uint64_t addr, uint64_t len, uint64_t prot)
{
    struct task *task = sched_current_task();
    uint64_t mapped_len;
    uint64_t end;
    struct task_vma *vma;
    struct task_vma original;
    struct task_vma *left = NULL;
    struct task_vma *right = NULL;
    uint64_t page_flags = 0;
    if (!task || task->kind != TASK_KIND_USER || (addr & (PAGE_SIZE - 1ULL)) ||
        align_user_len(len, &mapped_len) < 0 || mapped_len > NTCLKS_USER_TOP - addr ||
        (prot & ~(uint64_t)(LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC)) ||
        !prot || (prot & (LINUX_PROT_WRITE | LINUX_PROT_EXEC)) ==
                     (LINUX_PROT_WRITE | LINUX_PROT_EXEC)) {
        return -LEONOS_EINVAL;
    }
    end = addr + mapped_len;
    vma = task_vma_containing(task, addr, end);
    if (!vma || (prot & ~vma->max_prot)) {
        return -LEONOS_EACCES;
    }
    original = *vma;
    if (addr > original.start) {
        left = task_vma_free_slot(task);
        if (!left) {
            return -LEONOS_ENOMEM;
        }
        /* Reserve this slot while looking for the optional suffix. */
        left->used = 0xffffffffu;
    }
    if (end < original.end) {
        right = task_vma_free_slot(task);
        if (!right) {
            if (left) {
                task_vma_clear(left);
            }
            return -LEONOS_ENOMEM;
        }
        right->used = 0xffffffffu;
    }
    if (left) {
        task_vma_clear(left);
    }
    if (right) {
        task_vma_clear(right);
    }
    if (prot & LINUX_PROT_WRITE) {
        page_flags |= NTCLKS_PAGE_WRITABLE;
    }
    if (!(prot & LINUX_PROT_EXEC)) {
        page_flags |= NTCLKS_PAGE_NOEXEC;
    }
    if (vma->flags & TASK_VMA_FLAG_DEVICE) {
        page_flags |= NTCLKS_PAGE_DEVICE;
    }
    for (uint64_t page = addr; page < end; page += PAGE_SIZE) {
        if (address_space_user_page_phys(&task->as, page) &&
            !address_space_protect_user_page(&task->as, page, page_flags)) {
            return -LEONOS_EACCES;
        }
    }
    /**
 * @brief A partial protection change needs independent VMA metadata. In particular, PT_GNU_RELRO protects only the GOT page while BSS in the same original load segment must remain writable and lazily mappable.
 */
    if (addr > original.start) {
        left = task_vma_free_slot(task);
        if (!left) {
            return -LEONOS_ENOMEM;
        }
        *left = original;
        left->end = addr;
    }
    if (end < original.end) {
        right = task_vma_free_slot(task);
        if (!right) {
            return -LEONOS_ENOMEM;
        }
        *right = original;
        right->start = end;
        if (right->flags & TASK_VMA_FLAG_FILE) {
            right->file_offset += end - original.start;
        }
    }
    *vma = original;
    vma->start = addr;
    vma->end = end;
    if (vma->flags & TASK_VMA_FLAG_FILE) {
        vma->file_offset += addr - original.start;
    }
    vma->prot = (uint32_t)prot;
    return 0;
}

/**
 * Syscall mm munmap.
 * @param addr Value supplied by the caller.
 * @param len Maximum number of elements available in the related buffer.
 * @return The value or status produced by the operation.
 */
int64_t syscall_mm_munmap(uint64_t addr, uint64_t len)
{
    struct task *task = sched_current_task();
    uint64_t mapped_len;
    uint64_t start = addr;
    uint64_t end;
    uint64_t cursor;
    uint32_t split_count = 0;
    uint32_t free_slots = 0;

    if (!task || task->kind != TASK_KIND_USER || (start & (PAGE_SIZE - 1ULL)) != 0 ||
        start < NTCLKS_USER_BASE || start >= NTCLKS_USER_TOP) {
        return -LEONOS_EINVAL;
    }
    if (align_user_len(len, &mapped_len) < 0 || mapped_len > NTCLKS_USER_TOP - start) {
        return -LEONOS_EINVAL;
    }
    end = start + mapped_len;
    /* Validate that the requested span is fully covered before changing any
     * pages.  Dynamic libraries commonly have a RELRO VMA between two parts
     * of the same PT_LOAD, so a valid unmap can legitimately cross VMAs. */
    cursor = start;
    while (cursor < end) {
        struct task_vma *vma = task_vma_containing(task, cursor, cursor + 1);
        uint64_t part_end;
        if (!vma) {
            return -LEONOS_EINVAL;
        }
        part_end = vma->end < end ? vma->end : end;
        if (cursor > vma->start && part_end < vma->end) {
            ++split_count;
        }
        cursor = part_end;
    }
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (vma && !vma->used) {
            ++free_slots;
        }
    }
    if (split_count > free_slots) {
        return -LEONOS_ENOMEM;
    }
    task_unmap_pages(task, start, end);
    for (uint32_t i = 0; i < sched_task_vma_capacity(task); ++i) {
        struct task_vma *vma = sched_task_vma_at(task, i);
        if (!vma) continue;
        struct task_vma original;
        uint64_t remove_start;
        uint64_t remove_end;
        if (!vma->used || vma->end <= start || vma->start >= end) {
            continue;
        }
        original = *vma;
        remove_start = original.start > start ? original.start : start;
        remove_end = original.end < end ? original.end : end;
        if (remove_start == original.start && remove_end == original.end) {
            task_vma_clear(vma);
        } else if (remove_start == original.start) {
            vma->start = remove_end;
            if (vma->flags & TASK_VMA_FLAG_FILE) {
                vma->file_offset += remove_end - original.start;
            }
        } else if (remove_end == original.end) {
            vma->end = remove_start;
        } else {
            struct task_vma *right = task_vma_free_slot(task);
            if (!right) {
                return -LEONOS_ENOMEM;
            }
            *right = original;
            right->start = remove_end;
            if (right->flags & TASK_VMA_FLAG_FILE) {
                right->file_offset += remove_end - original.start;
            }
            vma->end = remove_start;
        }
    }
    return 0;
}
