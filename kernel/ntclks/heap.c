/*
 * LeonOS kernel heap implementation.
 * Uses the physical page allocator while the kernel remains identity mapped.
 */
#include <ntclks/heap.h>
#include <ntclks/mm.h>

#define KERNEL_HEAP_MAGIC 0x4c484541u
#define KERNEL_PAGE_SIZE 4096u

struct kernel_heap_header {
    uint32_t magic;
    uint32_t pages;
    uint64_t size;
};

void kernel_heap_init(void)
{
}

void *kernel_malloc(size_t size)
{
    uint64_t total;
    uint32_t pages;
    uint64_t phys;
    struct kernel_heap_header *header;

    if (!size || size > UINT64_MAX - sizeof(*header)) {
        return NULL;
    }
    total = (uint64_t)size + sizeof(*header);
    pages = (uint32_t)((total + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE);
    if (!pages || (uint64_t)pages * KERNEL_PAGE_SIZE < total) {
        return NULL;
    }
    phys = mm_alloc_pages(pages);
    if (!phys) {
        return NULL;
    }
    header = (struct kernel_heap_header *)(uintptr_t)phys;
    header->magic = KERNEL_HEAP_MAGIC;
    header->pages = pages;
    header->size = size;
    return (void *)(header + 1);
}

void kernel_free(void *memory)
{
    struct kernel_heap_header *header;
    if (!memory) {
        return;
    }
    header = ((struct kernel_heap_header *)memory) - 1;
    if (header->magic != KERNEL_HEAP_MAGIC || !header->pages) {
        return;
    }
    header->magic = 0;
    mm_free_pages((uint64_t)(uintptr_t)header, header->pages);
}
