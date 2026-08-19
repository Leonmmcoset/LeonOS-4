/*
 * LeonOS x86_64 paging: builds kernel and per-process address spaces.
 * Maps user pages, enforces NX/W^X permissions, and handles page protection.
 */
#include <ntclks/mm.h>
#include <ntclks/page_cache.h>
#include <ntclks/paging.h>

#define PAGE_SIZE 4096ULL
#define PAGE_SIZE_2M 0x200000ULL
#define PAGE_SIZE_FLAG 0x080ULL
#define USER_PDPT_INDEX 0
#define LOW_PD_INDEX 0
#define KERNEL_PD_COUNT 16u /* 16 GiB identity map using 2 MiB leaves. */
#define X86_EFER_MSR 0xc0000080u
#define X86_EFER_NXE (1ULL << 11)

static uint64_t kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[512] __attribute__((aligned(4096)));
static uint64_t kernel_pd[KERNEL_PD_COUNT][512] __attribute__((aligned(4096)));

extern void x86_64_load_cr3(uint64_t cr3);
extern void x86_64_invlpg(uint64_t addr);

static bool nx_enabled;

/**
 * @brief Copies one physical page without relying on the user virtual mapping.
 * @param destination Allocated physical destination page.
 * @param source Existing physical source page.
 */
static void copy_page(uint64_t destination, uint64_t source)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)destination;
    const uint8_t *src = (const uint8_t *)(uintptr_t)source;
    for (uint32_t i = 0; i < PAGE_SIZE; ++i) {
        dst[i] = src[i];
    }
}

/**
 * @brief Coordinates the paging enable nx operation.
 */
static void paging_enable_nx(void)
{
    uint32_t max_extended = 0;
    uint32_t regs[4];
    __asm__ volatile("cpuid"
                     : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
                     : "a"(0x80000000u), "c"(0));
    max_extended = regs[0];
    if (max_extended < 0x80000001u) {
        return;
    }
    __asm__ volatile("cpuid"
                     : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
                     : "a"(0x80000001u), "c"(0));
    if (!(regs[3] & (1u << 20))) {
        return;
    }
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(X86_EFER_MSR));
    uint64_t efer = ((uint64_t)hi << 32) | lo;
    efer |= X86_EFER_NXE;
    lo = (uint32_t)efer;
    hi = (uint32_t)(efer >> 32);
    __asm__ volatile("wrmsr" : : "c"(X86_EFER_MSR), "a"(lo), "d"(hi));
    nx_enabled = true;
}

/**
 * @brief Coordinates the align down operation.
 * @param value Input or output value used by this operation.
 * @param align Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

/**
 * @brief Coordinates the zero table operation.
 * @param table Input or output value used by this operation.
 */
static void zero_table(uint64_t *table)
{
    for (uint32_t i = 0; i < 512; ++i) {
        table[i] = 0;
    }
}

/**
 * @brief Coordinates the kernel page flags for operation.
 * @param addr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
static uint64_t kernel_page_flags_for(uint64_t addr)
{
    (void)addr;
    return NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | PAGE_SIZE_FLAG;
}

/**
 * @brief Coordinates the paging init user identity operation.
 */
void paging_init_user_identity(void)
{
    paging_enable_nx();
    zero_table(kernel_pml4);
    zero_table(kernel_pdpt);

    for (uint64_t table = 0; table < KERNEL_PD_COUNT; ++table) {
        for (uint64_t i = 0; i < 512; ++i) {
            uint64_t addr = (table << 30) + i * PAGE_SIZE_2M;
            uint64_t flags = kernel_page_flags_for(addr);
            kernel_pd[table][i] = flags ? addr | flags : 0;
        }
    }

    kernel_pml4[0] = (uint64_t)(uintptr_t)kernel_pdpt | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE;
    for (uint64_t i = 0; i < KERNEL_PD_COUNT; ++i) {
        kernel_pdpt[i] = (uint64_t)(uintptr_t)kernel_pd[i] | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE;
    }

    x86_64_load_cr3((uint64_t)(uintptr_t)kernel_pml4);
}

/**
 * @brief Coordinates the paging kernel cr3 operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t paging_kernel_cr3(void)
{
    return (uint64_t)(uintptr_t)kernel_pml4;
}

/**
 * @brief Coordinates the paging load cr3 operation.
 * @param cr3 Input or output value used by this operation.
 */
void paging_load_cr3(uint64_t cr3)
{
    x86_64_load_cr3(cr3 ? cr3 : paging_kernel_cr3());
}

/**
 * @brief Allocates table.
 * @return Result, status, or value defined by this API.
 */
static uint64_t alloc_table(void)
{
    return mm_alloc_page();
}

/**
 * @brief Coordinates the address space create operation.
 * @param as Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_create(struct address_space *as)
{
    if (!as) {
        return false;
    }
    for (size_t i = 0; i < sizeof(*as); ++i) {
        ((uint8_t *)as)[i] = 0;
    }

    uint64_t pml4_phys = alloc_table();
    uint64_t pdpt_phys = alloc_table();
    if (!pml4_phys || !pdpt_phys) {
        if (pml4_phys) {
            mm_free_page(pml4_phys);
        }
        if (pdpt_phys) {
            mm_free_page(pdpt_phys);
        }
        return false;
    }

    as->pml4 = (uint64_t *)(uintptr_t)pml4_phys;
    as->pdpt = (uint64_t *)(uintptr_t)pdpt_phys;
    as->cr3 = pml4_phys;
    for (uint32_t i = 0; i < 512; ++i) {
        as->pml4[i] = kernel_pml4[i];
        as->pdpt[i] = kernel_pdpt[i];
    }
    as->pml4[USER_PDPT_INDEX] = pdpt_phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_USER;

    uint64_t low_pd_phys = alloc_table();
    if (!low_pd_phys) {
        address_space_destroy(as);
        return false;
    }
    as->pd[LOW_PD_INDEX] = (uint64_t *)(uintptr_t)low_pd_phys;
    for (uint32_t i = 0; i < 512; ++i) {
        as->pd[LOW_PD_INDEX][i] = kernel_pd[0][i];
    }
    as->pdpt[USER_PDPT_INDEX] = low_pd_phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_USER;

    return true;
}

/**
 * @brief Clones populated user mappings and turns private writable pages into COW pages.
 * @param source Parent address space whose writable mappings are write-protected in place.
 * @param destination Empty output address space with independently allocated page tables.
 * @return True if all mappings were cloned; false after releasing the partial destination.
 */
bool address_space_clone_cow(struct address_space *source, struct address_space *destination)
{
    if (!source || !destination || !source->pml4 || !source->pdpt) {
        return false;
    }
    if (!address_space_create(destination)) {
        return false;
    }
    for (uint32_t table = 0; table < NTCLKS_USER_PD_COUNT; ++table) {
        uint64_t base = NTCLKS_USER_BASE + (uint64_t)table * NTCLKS_USER_PD_BYTES;
        if (!source->user_pt[table]) {
            continue;
        }
        for (uint32_t slot = 0; slot < 512; ++slot) {
            uint64_t entry = source->user_pt[table][slot];
            uint64_t flags;
            uint64_t phys;
            uint64_t page;
            int cached;
            if (!(entry & NTCLKS_PAGE_PRESENT)) {
                continue;
            }
            phys = entry & NTCLKS_PHYS_ADDR_MASK;
            flags = entry & (NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_NOEXEC | NTCLKS_PAGE_COW);
            page = base + (uint64_t)slot * PAGE_SIZE;
            if ((entry & NTCLKS_PAGE_WRITABLE) || (entry & NTCLKS_PAGE_COW)) {
                flags &= ~NTCLKS_PAGE_WRITABLE;
                flags |= NTCLKS_PAGE_COW;
                source->user_pt[table][slot] = phys | NTCLKS_PAGE_PRESENT |
                                               NTCLKS_PAGE_USER | flags;
                x86_64_invlpg(page);
            }
            cached = page_cache_retain(phys) == 0;
            if (!cached) {
                mm_retain_page(phys);
            }
            if (!address_space_map_user_page(destination, page, phys, flags)) {
                if (cached) {
                    page_cache_release(phys);
                }
                mm_free_page(phys);
                address_space_destroy(destination);
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Coordinates the address space destroy operation.
 * @param as Input or output value used by this operation.
 */
void address_space_destroy(struct address_space *as)
{
    if (!as) {
        return;
    }
    for (uint32_t table = 0; table < NTCLKS_USER_PD_COUNT; ++table) {
        if (as->user_pt[table]) {
            for (uint32_t i = 0; i < 512; ++i) {
                uint64_t entry = as->user_pt[table][i];
                if (entry & NTCLKS_PAGE_PRESENT) {
                    if (page_cache_owns(entry & NTCLKS_PHYS_ADDR_MASK)) {
                        page_cache_release(entry & NTCLKS_PHYS_ADDR_MASK);
                    } else {
                        mm_free_page(entry & NTCLKS_PHYS_ADDR_MASK);
                    }
                }
            }
            mm_free_page((uint64_t)(uintptr_t)as->user_pt[table]);
        }
    }
    if (as->pd[LOW_PD_INDEX]) {
        mm_free_page((uint64_t)(uintptr_t)as->pd[LOW_PD_INDEX]);
    }
    if (as->pdpt) {
        mm_free_page((uint64_t)(uintptr_t)as->pdpt);
    }
    if (as->pml4) {
        mm_free_page((uint64_t)(uintptr_t)as->pml4);
    }
    for (size_t i = 0; i < sizeof(*as); ++i) {
        ((uint8_t *)as)[i] = 0;
    }
}

/**
 * @brief Coordinates the address space prepare user range operation.
 * @param as Input or output value used by this operation.
 * @param start Input or output value used by this operation.
 * @param end Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_prepare_user_range(struct address_space *as, uint64_t start,
                                      uint64_t end)
{
    uint64_t first_page;
    uint64_t last_page;
    uint64_t first_table;
    uint64_t last_table;

    if (!as || start < NTCLKS_USER_BASE || start >= end || end > NTCLKS_USER_TOP ||
        (start < NTCLKS_KERNEL_HOLE_END && end > NTCLKS_KERNEL_HOLE_START)) {
        return false;
    }
    first_page = align_down(start, PAGE_SIZE);
    last_page = align_down(end - 1ULL, PAGE_SIZE);
    first_table = (first_page - NTCLKS_USER_BASE) / PAGE_SIZE / 512ULL;
    last_table = (last_page - NTCLKS_USER_BASE) / PAGE_SIZE / 512ULL;
    if (last_table >= NTCLKS_USER_PD_COUNT) {
        return false;
    }
    for (uint64_t table = first_table; table <= last_table; ++table) {
        if (!as->user_pt[table]) {
            uint64_t pt_phys = alloc_table();
            if (!pt_phys) {
                return false;
            }
            as->user_pt[table] = (uint64_t *)(uintptr_t)pt_phys;
            as->pd[LOW_PD_INDEX][NTCLKS_USER_PD_START + table] =
                pt_phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_USER;
            x86_64_invlpg(NTCLKS_USER_BASE + table * NTCLKS_USER_PD_BYTES);
        }
    }
    return true;
}

/**
 * @brief Coordinates the address space map user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param phys Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_map_user_page(struct address_space *as, uint64_t vaddr,
                                 uint64_t phys, uint64_t flags)
{
    if (!as || !phys || (phys & (PAGE_SIZE - 1))) {
        return false;
    }
    uint64_t page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP ||
        (page >= NTCLKS_KERNEL_HOLE_START && page < NTCLKS_KERNEL_HOLE_END)) {
        return false;
    }
    uint64_t index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    uint64_t table = index / 512;
    uint64_t slot = index % 512;
    if (table >= NTCLKS_USER_PD_COUNT) {
        return false;
    }
    if (!as->user_pt[table] &&
        !address_space_prepare_user_range(as, page, page + PAGE_SIZE)) {
        return false;
    }
    if (as->user_pt[table][slot] & NTCLKS_PAGE_PRESENT) {
        return false;
    }
    if ((flags & NTCLKS_PAGE_WRITABLE) && !(flags & NTCLKS_PAGE_NOEXEC)) {
        return false;
    }
    if (!nx_enabled) {
        /* Executable user mappings are unsafe without NX because the kernel
         * cannot enforce W^X.  Refuse the process rather than weaken it. */
        return false;
    }
    as->user_pt[table][slot] = phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | flags;
    ++as->user_page_count;
    x86_64_invlpg(page);
    return true;
}

/**
 * @brief Coordinates the address space protect user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_protect_user_page(struct address_space *as, uint64_t vaddr,
                                     uint64_t flags)
{
    uint64_t page;
    uint64_t index;
    uint64_t table;
    uint64_t slot;
    uint64_t entry;
    if (!as || ((flags & NTCLKS_PAGE_WRITABLE) && !(flags & NTCLKS_PAGE_NOEXEC))) {
        return false;
    }
    page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return false;
    }
    index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    table = index / 512;
    slot = index % 512;
    if (table >= NTCLKS_USER_PD_COUNT || !as->user_pt[table]) {
        return false;
    }
    entry = as->user_pt[table][slot];
    if (!(entry & NTCLKS_PAGE_PRESENT)) {
        return false;
    }
    as->user_pt[table][slot] = (entry & NTCLKS_PHYS_ADDR_MASK) |
                                NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | flags;
    x86_64_invlpg(page);
    return true;
}

/**
 * @brief Coordinates the address space unmap user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
uint64_t address_space_unmap_user_page(struct address_space *as, uint64_t vaddr)
{
    if (!as) {
        return 0;
    }
    uint64_t page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return 0;
    }
    uint64_t index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    uint64_t table = index / 512;
    uint64_t slot = index % 512;
    if (table >= NTCLKS_USER_PD_COUNT || !as->user_pt[table]) {
        return 0;
    }
    uint64_t entry = as->user_pt[table][slot];
    if (!(entry & NTCLKS_PAGE_PRESENT)) {
        return 0;
    }
    as->user_pt[table][slot] = 0;
    if (as->user_page_count) {
        --as->user_page_count;
    }
    x86_64_invlpg(page);
    return entry & NTCLKS_PHYS_ADDR_MASK;
}

/**
 * @brief Coordinates the address space user page phys operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr)
{
    if (!as) {
        return 0;
    }
    uint64_t page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return 0;
    }
    uint64_t index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    uint64_t table = index / 512;
    uint64_t slot = index % 512;
    if (table >= NTCLKS_USER_PD_COUNT || !as->user_pt[table]) {
        return 0;
    }
    uint64_t entry = as->user_pt[table][slot];
    return (entry & NTCLKS_PAGE_PRESENT) ? (entry & NTCLKS_PHYS_ADDR_MASK) : 0;
}

/**
 * @brief Coordinates the address space user memory kib operation.
 * @param as Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t address_space_user_memory_kib(const struct address_space *as)
{
    if (!as) {
        return 0;
    }
    return as->user_page_count * 4U;
}

/**
 * @brief Coordinates the address space map user stack operation.
 * @param as Input or output value used by this operation.
 * @param stack_top Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_map_user_stack(struct address_space *as, uint64_t stack_top)
{
    if (!as || stack_top <= (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE ||
        stack_top > NTCLKS_USER_TOP) {
        return false;
    }
    uint64_t first = stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE;
    for (uint32_t i = 0; i < NTCLKS_USER_STACK_PAGES; ++i) {
        uint64_t phys = mm_alloc_page();
        if (!phys || !address_space_map_user_page(as, first + (uint64_t)i * PAGE_SIZE,
                                                  phys, NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_NOEXEC)) {
            if (phys) {
                mm_free_page(phys);
            }
            return false;
        }
    }
    return true;
}

bool address_space_map_user_stack_page(struct address_space *as, uint64_t page)
{
    uint64_t phys;
    if (!as || (page & (PAGE_SIZE - 1ULL)) || page < NTCLKS_USER_BASE ||
        page >= NTCLKS_USER_TOP) {
        return false;
    }
    if (address_space_user_page_phys(as, page)) {
        return true;
    }
    phys = mm_alloc_page();
    if (!phys) {
        return false;
    }
    if (!address_space_map_user_page(as, page, phys,
                                     NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_NOEXEC)) {
        mm_free_page(phys);
        return false;
    }
    return true;
}

/**
 * @brief Resolves a write fault by copying a shared COW page into the faulting address space.
 * @param as Address space that owns the faulting mapping.
 * @param vaddr User address within the affected page.
 * @return True when the page is now writable and private to this address space.
 */
bool address_space_handle_cow_fault(struct address_space *as, uint64_t vaddr)
{
    uint64_t page;
    uint64_t index;
    uint64_t table;
    uint64_t slot;
    uint64_t entry;
    uint64_t old_phys;
    uint64_t new_phys;
    if (!as) {
        return false;
    }
    page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return false;
    }
    index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    table = index / 512;
    slot = index % 512;
    if (table >= NTCLKS_USER_PD_COUNT || !as->user_pt[table]) {
        return false;
    }
    entry = as->user_pt[table][slot];
    if (!(entry & NTCLKS_PAGE_PRESENT) || !(entry & NTCLKS_PAGE_COW)) {
        return false;
    }
    old_phys = entry & NTCLKS_PHYS_ADDR_MASK;
    new_phys = mm_alloc_page();
    if (!new_phys) {
        return false;
    }
    copy_page(new_phys, old_phys);
    as->user_pt[table][slot] = new_phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER |
                               NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_NOEXEC;
    x86_64_invlpg(page);
    mm_free_page(old_phys);
    return true;
}
