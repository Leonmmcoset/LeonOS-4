#include <ntclks/console.h>
#include <ntclks/mm.h>

static uint64_t total_kib;
static uint64_t page_alloc_next;
static uint64_t page_alloc_end;
static uint64_t free_pages[4096];
static uint32_t free_page_count;

#define PAGE_SIZE 4096ULL
#define PAGE_ALLOC_MIN 0x02000000ULL
#define PAGE_ALLOC_LIMIT 0x100000000ULL
#define FALLBACK_MEMORY_KIB (512ULL * 1024ULL)

static int efi_memory_usable(uint32_t type)
{
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 7;
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static void consider_usable_range(uint64_t addr, uint64_t len, uint64_t highest_module_end)
{
    uint64_t start = align_up(addr, PAGE_SIZE);
    uint64_t end = addr + len;
    if (end > PAGE_ALLOC_LIMIT) {
        end = PAGE_ALLOC_LIMIT;
    }
    uint64_t min_start = PAGE_ALLOC_MIN;
    if (highest_module_end > min_start) {
        min_start = align_up(highest_module_end, PAGE_SIZE);
    }
    if (start < min_start) {
        start = min_start;
    }
    if (start + PAGE_SIZE <= end && (!page_alloc_next || end > page_alloc_end)) {
        page_alloc_next = start;
        page_alloc_end = end;
    }
}

static void zero_page(uint64_t phys)
{
    uint8_t *p = (uint8_t *)(uintptr_t)phys;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        p[i] = 0;
    }
}

void mm_init(const struct boot_info *boot)
{
    total_kib = boot->memory_lower_kib + boot->memory_upper_kib;
    uint64_t highest_module_end = 0;
    for (uint32_t i = 0; i < boot->module_count; ++i) {
        if (boot->modules[i].end > highest_module_end) {
            highest_module_end = boot->modules[i].end;
        }
    }

    if (boot->mmap_addr && boot->mmap_entry_count) {
        total_kib = 0;
        const uint8_t *cursor = (const uint8_t *)(uintptr_t)boot->mmap_addr;
        for (uint32_t i = 0; i < boot->mmap_entry_count; ++i) {
            const struct multiboot2_mmap_entry *entry =
                (const struct multiboot2_mmap_entry *)(cursor + (uint64_t)i * boot->mmap_entry_size);
            if (entry->type == 1) {
                total_kib += entry->len / 1024;
                consider_usable_range(entry->addr, entry->len, highest_module_end);
            }
        }
    }
    if (!page_alloc_next && boot->efi_mmap_addr && boot->efi_mmap_entry_count) {
        total_kib = 0;
        const uint8_t *cursor = (const uint8_t *)(uintptr_t)boot->efi_mmap_addr;
        for (uint32_t i = 0; i < boot->efi_mmap_entry_count; ++i) {
            const struct efi_memory_descriptor *entry =
                (const struct efi_memory_descriptor *)(cursor + (uint64_t)i * boot->efi_mmap_entry_size);
            uint64_t len = entry->number_of_pages * PAGE_SIZE;
            if (efi_memory_usable(entry->type)) {
                total_kib += len / 1024;
                consider_usable_range(entry->physical_start, len, highest_module_end);
            }
        }
    }
    if (!page_alloc_next) {
        if (!total_kib) {
            total_kib = FALLBACK_MEMORY_KIB;
        }
        uint64_t fallback_pages = total_kib / 4;
        if (fallback_pages > 8192) {
            fallback_pages = 8192;
        }
        page_alloc_next = PAGE_ALLOC_MIN;
        if (highest_module_end > page_alloc_next) {
            page_alloc_next = align_up(highest_module_end, PAGE_SIZE);
        }
        page_alloc_end = PAGE_ALLOC_MIN + fallback_pages * PAGE_SIZE;
    }
    console_printf("[ntclks] mm initialized usable=%llu KiB mmap_entries=%u efi_mmap_entries=%u\n",
                   (unsigned long long)total_kib,
                   boot->mmap_entry_count,
                   boot->efi_mmap_entry_count);
    console_printf("[ntclks] page allocator range=0x%llx-0x%llx module_end=0x%llx\n",
                   (unsigned long long)page_alloc_next,
                   (unsigned long long)page_alloc_end,
                   (unsigned long long)highest_module_end);
}

uint64_t mm_total_memory_kib(void)
{
    return total_kib;
}

uint64_t mm_alloc_page(void)
{
    uint64_t phys = 0;
    if (free_page_count) {
        phys = free_pages[--free_page_count];
    } else {
        if (!page_alloc_next || page_alloc_next + PAGE_SIZE > page_alloc_end) {
            return 0;
        }
        phys = page_alloc_next;
        page_alloc_next += PAGE_SIZE;
    }
    zero_page(phys);
    return phys;
}

uint64_t mm_alloc_pages(uint32_t page_count)
{
    if (!page_count) {
        return 0;
    }
    uint64_t bytes = (uint64_t)page_count * PAGE_SIZE;
    if (!page_alloc_next || page_alloc_next + bytes > page_alloc_end) {
        return 0;
    }
    uint64_t phys = page_alloc_next;
    page_alloc_next += bytes;
    for (uint64_t p = phys; p < phys + bytes; p += PAGE_SIZE) {
        zero_page(p);
    }
    return phys;
}

void mm_free_page(uint64_t phys)
{
    if (!phys || (phys & (PAGE_SIZE - 1)) || free_page_count >= 4096) {
        return;
    }
    free_pages[free_page_count++] = phys;
}

void mm_free_pages(uint64_t phys, uint32_t page_count)
{
    if (!phys || !page_count) {
        return;
    }
    for (uint32_t i = 0; i < page_count; ++i) {
        mm_free_page(phys + (uint64_t)i * PAGE_SIZE);
    }
}
