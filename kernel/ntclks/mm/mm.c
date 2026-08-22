/*
 * LeonOS physical memory manager: tracks usable and reserved page ranges.
 * Allocates, frees, zeroes, and reference-counts physical memory pages.
 */
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/mm.h>
#include <ntclks/paging.h>

static uint64_t total_kib;

#define PAGE_SIZE 4096ULL
#define PAGE_ALLOC_MIN NTCLKS_USER_TOP
#define PAGE_ALLOC_LIMIT 0x400000000ULL
#define FALLBACK_ALLOC_START 0x0a000000ULL
#define FALLBACK_MEMORY_KIB (512ULL * 1024ULL)
#define MM_MAX_FREE_RANGES 256u
#define MM_MAX_RESERVED_RANGES 64u
#define MM_PAGE_COUNT (PAGE_ALLOC_LIMIT / PAGE_SIZE)

struct phys_range {
    uint64_t start;
    uint64_t end;
    const char *name;
};

static struct phys_range free_ranges[MM_MAX_FREE_RANGES];
static uint32_t free_range_count;
static struct phys_range reserved_ranges[MM_MAX_RESERVED_RANGES];
static uint32_t reserved_range_count;
/* The allocator tracks the first 16 GiB.  A compact 16-bit count uses 8 MiB
 * and is large enough for the supported physical address range while still
 * allowing COW and shared file pages to use the same ownership accounting. */
static uint16_t page_refs[MM_PAGE_COUNT];

/**
 * @brief Convert a physical address to its index in the page_refs array.
 */
static uint32_t page_index(uint64_t phys)
{
    return (uint32_t)(phys / PAGE_SIZE);
}

/**
 * @brief True for EFI memory types the allocator may hand out (conventional, boot, loader, etc.).
 */
static int efi_memory_usable(uint32_t type)
{
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 7;
}

/**
 * @brief Round value down to a multiple of align, which must be a power of two.
 */
static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

/**
 * @brief Round value up to a multiple of align, which must be a power of two.
 */
static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

/**
 * @brief Zero the full 4 KiB page starting at phys (identity-mapped).
 */
static void zero_page(uint64_t phys)
{
    uint8_t *p = (uint8_t *)(uintptr_t)phys;
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        p[i] = 0;
    }
}

/**
 * @brief Zero page_count consecutive pages starting at phys.
 */
static void zero_pages(uint64_t phys, uint32_t page_count)
{
    for (uint32_t i = 0; i < page_count; ++i) {
        zero_page(phys + (uint64_t)i * PAGE_SIZE);
    }
}

/**
 * @brief Clamp and page-align [start,end) into the allocator window, then append it as a free range.
 */
static void add_free_range(uint64_t start, uint64_t end)
{
    if (end > PAGE_ALLOC_LIMIT) {
        end = PAGE_ALLOC_LIMIT;
    }
    start = align_up(start, PAGE_SIZE);
    end = align_down(end, PAGE_SIZE);
    /**
 * @brief User address spaces replace the identity mapping above their lower boundary. Physical pages below that boundary cannot safely be touched while a user CR3 is active, so keep allocator pages outside the alias.
 */
    if (start < PAGE_ALLOC_MIN) {
        start = PAGE_ALLOC_MIN;
    }
    if (start >= end || free_range_count >= MM_MAX_FREE_RANGES) {
        return;
    }
    free_ranges[free_range_count++] = (struct phys_range){start, end, "free"};
}

/**
 * @brief Grow [start,end) outward to page boundaries and append it to the reserved table.
 */
static void add_reserved_range(uint64_t start, uint64_t end, const char *name)
{
    start = align_down(start, PAGE_SIZE);
    end = align_up(end, PAGE_SIZE);
    if (start >= end || reserved_range_count >= MM_MAX_RESERVED_RANGES) {
        return;
    }
    reserved_ranges[reserved_range_count++] = (struct phys_range){start, end, name};
}

/**
 * @brief Drop free_ranges[index] and shift the remaining entries left to close the gap.
 */
static void remove_free_range(uint32_t index)
{
    if (index >= free_range_count) {
        return;
    }
    for (uint32_t i = index + 1; i < free_range_count; ++i) {
        free_ranges[i - 1] = free_ranges[i];
    }
    --free_range_count;
}

/**
 * @brief Order free ranges by ascending start address (simple selection sort).
 */
static void sort_free_ranges(void)
{
    for (uint32_t i = 0; i < free_range_count; ++i) {
        for (uint32_t j = i + 1; j < free_range_count; ++j) {
            if (free_ranges[j].start < free_ranges[i].start) {
                struct phys_range tmp = free_ranges[i];
                free_ranges[i] = free_ranges[j];
                free_ranges[j] = tmp;
            }
        }
    }
}

/**
 * @brief Sort then merge adjacent or overlapping free ranges into the fewest contiguous spans.
 */
static void coalesce_free_ranges(void)
{
    sort_free_ranges();
    for (uint32_t i = 0; i + 1 < free_range_count;) {
        if (free_ranges[i].end >= free_ranges[i + 1].start) {
            if (free_ranges[i + 1].end > free_ranges[i].end) {
                free_ranges[i].end = free_ranges[i + 1].end;
            }
            remove_free_range(i + 1);
            continue;
        }
        ++i;
    }
}

/**
 * @brief Carve [start,end) out of every free range it overlaps, splitting ranges as needed.
 */
static void reserve_from_free(uint64_t start, uint64_t end)
{
    start = align_down(start, PAGE_SIZE);
    end = align_up(end, PAGE_SIZE);
    if (start >= end) {
        return;
    }
    for (uint32_t i = 0; i < free_range_count;) {
        struct phys_range r = free_ranges[i];
        if (end <= r.start || start >= r.end) {
            ++i;
            continue;
        }
        if (start <= r.start && end >= r.end) {
            remove_free_range(i);
            continue;
        }
        if (start <= r.start) {
            free_ranges[i].start = end < r.end ? end : r.end;
            ++i;
            continue;
        }
        if (end >= r.end) {
            free_ranges[i].end = start > r.start ? start : r.start;
            ++i;
            continue;
        }
        if (free_range_count < MM_MAX_FREE_RANGES) {
            free_ranges[i].end = start;
            free_ranges[free_range_count++] = (struct phys_range){end, r.end, "free"};
        } else {
            free_ranges[i].end = start;
        }
        ++i;
    }
}

/**
 * @brief Record a reserved range and simultaneously remove those pages from the free list.
 */
static void reserve_range(uint64_t start, uint64_t end, const char *name)
{
    add_reserved_range(start, end, name);
    reserve_from_free(start, end);
}

/**
 * @brief True when [start,end) intersects any previously reserved range.
 */
static int overlaps_reserved(uint64_t start, uint64_t end)
{
    for (uint32_t i = 0; i < reserved_range_count; ++i) {
        if (start < reserved_ranges[i].end && end > reserved_ranges[i].start) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Guard against null or overflowing ranges, then reserve [start, start+bytes).
 */
static void reserve_bytes(uint64_t start, uint64_t bytes, const char *name)
{
    if (!start || !bytes || start + bytes < start) {
        return;
    }
    reserve_range(start, start + bytes, name);
}

/**
 * @brief Register a synthetic free range when the loader supplied no usable memory map.
 */
static void add_fallback_free_range(void)
{
    uint64_t end = (total_kib ? total_kib : FALLBACK_MEMORY_KIB) * 1024ULL;
    if (end > PAGE_ALLOC_LIMIT) {
        end = PAGE_ALLOC_LIMIT;
    }
    if (end <= FALLBACK_ALLOC_START) {
        end = FALLBACK_ALLOC_START + 128ULL * 1024ULL * 1024ULL;
    }
    add_free_range(FALLBACK_ALLOC_START, end);
}

/**
 * @brief Populate the free list from the multiboot or EFI memory map, falling back to a guess.
 */
static void seed_free_ranges_from_boot(const struct boot_info *boot)
{
    if (boot->mmap_addr && boot->mmap_entry_count) {
        total_kib = 0;
        const uint8_t *cursor = (const uint8_t *)(uintptr_t)boot->mmap_addr;
        for (uint32_t i = 0; i < boot->mmap_entry_count; ++i) {
            const struct multiboot2_mmap_entry *entry =
                (const struct multiboot2_mmap_entry *)(cursor + (uint64_t)i * boot->mmap_entry_size);
            if (entry->type == 1) {
                add_free_range(entry->addr, entry->addr + entry->len);
            }
        }
        return;
    }
    if (boot->efi_mmap_addr && boot->efi_mmap_entry_count) {
        total_kib = 0;
        const uint8_t *cursor = (const uint8_t *)(uintptr_t)boot->efi_mmap_addr;
        for (uint32_t i = 0; i < boot->efi_mmap_entry_count; ++i) {
            const struct efi_memory_descriptor *entry =
                (const struct efi_memory_descriptor *)(cursor + (uint64_t)i * boot->efi_mmap_entry_size);
            uint64_t len = entry->number_of_pages * PAGE_SIZE;
            if (efi_memory_usable(entry->type)) {
                add_free_range(entry->physical_start, entry->physical_start + len);
            }
        }
        return;
    }
    total_kib = 0;
    add_fallback_free_range();
}

static void recompute_total_allocatable_kib(void)
{
    total_kib = 0;
    for (uint32_t i = 0; i < free_range_count; ++i) {
        if (free_ranges[i].end > free_ranges[i].start) {
            total_kib += (free_ranges[i].end - free_ranges[i].start) / 1024ULL;
        }
    }
}

/**
 * @brief Protect loader structures, framebuffer, modules, and the handoff regions from allocation.
 */
static void reserve_boot_ranges(const struct boot_info *boot,
                                const struct leonos_boot_handoff *handoff)
{
    uint64_t boot_framebuffer_start = 0;
    uint64_t boot_framebuffer_bytes = 0;
    reserve_range(0, PAGE_ALLOC_MIN, "lowmem");
    if (boot->mmap_addr && boot->mmap_entry_count) {
        reserve_bytes(boot->mmap_addr,
                      (uint64_t)boot->mmap_entry_count * boot->mmap_entry_size,
                      "mmap");
    }
    if (boot->efi_mmap_addr && boot->efi_mmap_entry_count) {
        reserve_bytes(boot->efi_mmap_addr,
                      (uint64_t)boot->efi_mmap_entry_count * boot->efi_mmap_entry_size,
                      "efi-mmap");
    }
    if (boot->multiboot_info) {
        const struct multiboot2_info *info =
            (const struct multiboot2_info *)(uintptr_t)boot->multiboot_info;
        uint64_t bytes = info && info->total_size ? info->total_size : 4096;
        reserve_bytes(boot->multiboot_info, bytes, "multiboot");
    }
    if (boot->rsdp_addr) {
        reserve_bytes(boot->rsdp_addr, 4096, "acpi-rsdp");
    }
    if (boot->framebuffer_addr && boot->framebuffer_pitch && boot->framebuffer_height) {
        boot_framebuffer_start = boot->framebuffer_addr;
        boot_framebuffer_bytes = (uint64_t)boot->framebuffer_pitch * boot->framebuffer_height;
        reserve_bytes(boot_framebuffer_start, boot_framebuffer_bytes,
                      "framebuffer");
    }
    {
        const struct framebuffer *fb = framebuffer_get();
        uint64_t reservation_start = fb->reservation_start;
        uint64_t reservation_bytes = fb->reservation_bytes;
        if (!reservation_start && fb->available && fb->pixels && fb->pitch && fb->height) {
            reservation_start = (uint64_t)(uintptr_t)fb->pixels;
            reservation_bytes = (uint64_t)fb->pitch * fb->height;
        }
        if (reservation_start && reservation_bytes &&
            (reservation_start != boot_framebuffer_start ||
             reservation_bytes != boot_framebuffer_bytes)) {
            reserve_bytes(reservation_start, reservation_bytes,
                          "framebuffer-vram");
        }
    }
    for (uint32_t i = 0; i < boot->module_count; ++i) {
        reserve_range(boot->modules[i].start, boot->modules[i].end, "module");
    }
    if (handoff && handoff->magic == LEONOS_BOOT_HANDOFF_MAGIC &&
        handoff->version == LEONOS_BOOT_HANDOFF_VERSION) {
        reserve_range(handoff->loader.start, handoff->loader.end, "loader");
        reserve_range((uint64_t)(uintptr_t)handoff,
                      (uint64_t)(uintptr_t)handoff + sizeof(*handoff),
                      "loader-handoff");
        reserve_range(handoff->kernel.start, handoff->kernel.end, "kernel");
        reserve_range(handoff->middlelayer.start, handoff->middlelayer.end, "middlelayer");
        reserve_range(handoff->installer_root.start, handoff->installer_root.end,
                      "installer-root");
    }
}

/**
 * @brief Dump the free and reserved range tables to the console for diagnostics.
 */
static void print_memory_map(void)
{
    console_printf("[ntclks] physical memory free ranges=%u reserved=%u\n",
                   free_range_count, reserved_range_count);
    for (uint32_t i = 0; i < reserved_range_count; ++i) {
        console_printf("[ntclks] reserved[%u] %s 0x%llx-0x%llx (%llu KiB)\n",
                       i,
                       reserved_ranges[i].name ? reserved_ranges[i].name : "reserved",
                       (unsigned long long)reserved_ranges[i].start,
                       (unsigned long long)reserved_ranges[i].end,
                       (unsigned long long)((reserved_ranges[i].end - reserved_ranges[i].start) / 1024));
    }
    for (uint32_t i = 0; i < free_range_count; ++i) {
        console_printf("[ntclks] free[%u] 0x%llx-0x%llx (%llu KiB)\n",
                       i,
                       (unsigned long long)free_ranges[i].start,
                       (unsigned long long)free_ranges[i].end,
                       (unsigned long long)((free_ranges[i].end - free_ranges[i].start) / 1024));
    }
}

/**
 * @brief Reset accounting, seed free ranges from boot, reserve kernel-owned areas, then coalesce.
 */
void mm_init(const struct boot_info *boot, const struct leonos_boot_handoff *handoff)
{
    total_kib = 0;
    free_range_count = 0;
    reserved_range_count = 0;
    for (uint32_t i = 0; i < MM_PAGE_COUNT; ++i) {
        page_refs[i] = 0;
    }
    seed_free_ranges_from_boot(boot);
    reserve_boot_ranges(boot, handoff);
    coalesce_free_ranges();
    /**
 * @brief Report the same range the allocator can actually serve. Previously this value came from the legacy lower/upper fields and could include low reserved memory or addresses above the allocator limit.
 */
    recompute_total_allocatable_kib();

    console_printf("[ntclks] mm initialized usable=%llu KiB mmap_entries=%u efi_mmap_entries=%u\n",
                   (unsigned long long)total_kib,
                   boot->mmap_entry_count,
                   boot->efi_mmap_entry_count);
    print_memory_map();
}

/**
 * @brief Return the total allocatable memory in KiB, computed once during mm_init.
 */
uint64_t mm_total_memory_kib(void)
{
    return total_kib;
}

/**
 * @brief Sum the current free ranges and report the free byte count in KiB.
 */
uint64_t mm_free_memory_kib(void)
{
    uint64_t free_kib = 0;
    for (uint32_t i = 0; i < free_range_count; ++i) {
        if (free_ranges[i].end > free_ranges[i].start) {
            free_kib += (free_ranges[i].end - free_ranges[i].start) / 1024ULL;
        }
    }
    return free_kib;
}

/**
 * @brief Carve page_count contiguous, zeroed pages off the first fitting free range, mark them referenced, and return the base address; 0 when no range is large enough.
 */
uint64_t mm_alloc_pages(uint32_t page_count)
{
    if (!page_count) {
        return 0;
    }
    uint64_t bytes = (uint64_t)page_count * PAGE_SIZE;
    for (uint32_t i = 0; i < free_range_count; ++i) {
        uint64_t start = align_up(free_ranges[i].start, PAGE_SIZE);
        if (start + bytes < start || start + bytes > free_ranges[i].end) {
            continue;
        }
        uint64_t phys = start;
        free_ranges[i].start = start + bytes;
        if (free_ranges[i].start >= free_ranges[i].end) {
            remove_free_range(i);
        }
        zero_pages(phys, page_count);
        for (uint32_t page = 0; page < page_count; ++page) {
            page_refs[page_index(phys + (uint64_t)page * PAGE_SIZE)] = 1;
        }
        return phys;
    }
    return 0;
}

/**
 * @brief Allocate a single page via mm_alloc_pages(1).
 */
uint64_t mm_alloc_page(void)
{
    return mm_alloc_pages(1);
}

/**
 * @brief Drop one reference per page; a page whose count reaches zero is returned to the free list.
 */
void mm_free_pages(uint64_t phys, uint32_t page_count)
{
    if (!phys || !page_count || (phys & (PAGE_SIZE - 1))) {
        return;
    }
    uint64_t end = phys + (uint64_t)page_count * PAGE_SIZE;
    if (end <= phys || end > PAGE_ALLOC_LIMIT || overlaps_reserved(phys, end)) {
        return;
    }
    for (uint32_t page = 0; page < page_count; ++page) {
        uint64_t current = phys + (uint64_t)page * PAGE_SIZE;
        uint16_t *refs = &page_refs[page_index(current)];
        if (!*refs) {
            continue;
        }
        --*refs;
        if (*refs) {
            continue;
        }
        if (free_range_count >= MM_MAX_FREE_RANGES) {
            /* This should be impossible after normal coalescing.  Keep the
             * page reserved rather than corrupting the free-range table. */
            *refs = 1;
            continue;
        }
        free_ranges[free_range_count++] =
            (struct phys_range){current, current + PAGE_SIZE, "free"};
        coalesce_free_ranges();
    }
}

/**
 * @brief Free a single page via mm_free_pages(phys, 1).
 */
void mm_free_page(uint64_t phys)
{
    mm_free_pages(phys, 1);
}

/**
 * @brief Increment a page's reference count, saturating at UINT16_MAX and ignoring free (zero) pages.
 */
void mm_retain_page(uint64_t phys)
{
    if (!phys || (phys & (PAGE_SIZE - 1)) || phys >= PAGE_ALLOC_LIMIT) {
        return;
    }
    uint16_t *refs = &page_refs[page_index(phys)];
    if (*refs && *refs != UINT16_MAX) {
        ++*refs;
    }
}
