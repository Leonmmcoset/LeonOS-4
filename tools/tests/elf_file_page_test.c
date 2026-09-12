#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ntclks/lock.h>
#include "../../kernel/ntclks/syscall_mm.c"

static unsigned char file_bytes[8192];
static uint64_t mapped;
static unsigned allocations;

uint64_t mm_alloc_page(void)
{
    void *p = aligned_alloc(4096, 4096);
    assert(p);
    memset(p, 0, 4096);
    ++allocations;
    return (uintptr_t)p;
}
void mm_free_page(uint64_t p) { assert(allocations); --allocations; free((void *)(uintptr_t)p); }
int storage_read_node(const struct storage_node *node, uint64_t offset,
                      void *buffer, uint32_t size, uint32_t *got)
{
    assert(node->size == sizeof(file_bytes) && offset + size <= node->size);
    memcpy(buffer, file_bytes + offset, size);
    *got = size;
    return 0;
}
void storage_set_io_async_context(bool enabled) { (void)enabled; }
int storage_errno(int status) { return status; }
bool address_space_map_user_page(struct address_space *as, uint64_t va,
                                 uint64_t phys, uint64_t flags)
{ (void)as; (void)va; (void)flags; mapped = phys; return true; }
void kernel_spin_init(struct kernel_spinlock *lock) { (void)lock; }
void kernel_spin_lock_irqsave(struct kernel_spinlock *lock, uint64_t *flags)
{ (void)lock; *flags = 0; }
void kernel_spin_unlock_irqrestore(struct kernel_spinlock *lock, uint64_t flags)
{ (void)lock; (void)flags; }

int main(void)
{
    struct task task = {0};
    struct task_vma prefix = {
        .used = 1, .prot = LINUX_PROT_READ,
        .flags = TASK_VMA_FLAG_FILE | TASK_VMA_FLAG_LAZY | TASK_VMA_FLAG_SHARED_FILE,
        .start = 0x5000000, .end = 0x5001000, .file_offset = 0,
        .file_limit = 0x800, .file_node = {.size = sizeof(file_bytes)}
    };
    struct task_vma text = prefix;
    text.start += 4096;
    text.end += 4096;
    text.prot |= LINUX_PROT_EXEC;
    text.file_limit = sizeof(file_bytes);
    memset(file_bytes, 0x53, sizeof(file_bytes));
    page_cache_init();
    assert(task_map_file_vma_page(&task, &prefix, prefix.start) == 0);
    uint64_t prefix_page = mapped;
    assert(((unsigned char *)(uintptr_t)prefix_page)[0] == 0x53);
    assert(((unsigned char *)(uintptr_t)prefix_page)[0x900] == 0);
    assert(task_map_file_vma_page(&task, &text, text.start) == 0);
    uint64_t text_page = mapped;
    assert(((unsigned char *)(uintptr_t)text_page)[0x900] == 0x53);
    assert(text_page != prefix_page);
    assert(task_map_file_vma_page(&task, &text, text.start) == 0);
    assert(mapped == text_page);
    page_cache_release(text_page);
    page_cache_release(text_page);
    if (page_cache_owns(prefix_page)) page_cache_release(prefix_page);
    else mm_free_page(prefix_page);
    struct storage_node resized = prefix.file_node;
    resized.size += 4096;
    page_cache_invalidate_node(&resized);
    assert(!allocations);
    puts("PASS ELF segments sharing a file page: independent zero-fill and intact code cache");
}
