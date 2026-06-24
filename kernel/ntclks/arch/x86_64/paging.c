#include <ntclks/mm.h>
#include <ntclks/paging.h>

#define PAGE_SIZE 4096ULL
#define PAGE_SIZE_2M 0x200000ULL
#define PAGE_SIZE_FLAG 0x080ULL
#define USER_PDPT_INDEX 0
#define USER_PD_START 2
#define USER_PD_COUNT 6
#define LOW_PD_INDEX 0

static uint64_t kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[512] __attribute__((aligned(4096)));
static uint64_t kernel_pd[4][512] __attribute__((aligned(4096)));

extern void x86_64_load_cr3(uint64_t cr3);

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static void zero_table(uint64_t *table)
{
    for (uint32_t i = 0; i < 512; ++i) {
        table[i] = 0;
    }
}

static uint64_t kernel_page_flags_for(uint64_t addr)
{
    (void)addr;
    return NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | PAGE_SIZE_FLAG;
}

void paging_init_user_identity(void)
{
    zero_table(kernel_pml4);
    zero_table(kernel_pdpt);

    for (uint64_t table = 0; table < 4; ++table) {
        for (uint64_t i = 0; i < 512; ++i) {
            uint64_t addr = (table << 30) + i * PAGE_SIZE_2M;
            uint64_t flags = kernel_page_flags_for(addr);
            kernel_pd[table][i] = flags ? addr | flags : 0;
        }
    }

    kernel_pml4[0] = (uint64_t)(uintptr_t)kernel_pdpt | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE;
    for (uint64_t i = 0; i < 4; ++i) {
        kernel_pdpt[i] = (uint64_t)(uintptr_t)kernel_pd[i] | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE;
    }

    x86_64_load_cr3((uint64_t)(uintptr_t)kernel_pml4);
}

uint64_t paging_kernel_cr3(void)
{
    return (uint64_t)(uintptr_t)kernel_pml4;
}

void paging_load_cr3(uint64_t cr3)
{
    x86_64_load_cr3(cr3 ? cr3 : paging_kernel_cr3());
}

static uint64_t alloc_table(void)
{
    return mm_alloc_page();
}

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

    for (uint32_t i = 0; i < USER_PD_COUNT; ++i) {
        uint64_t pt_phys = alloc_table();
        if (!pt_phys) {
            if (pt_phys) {
                mm_free_page(pt_phys);
            }
            address_space_destroy(as);
            return false;
        }
        as->user_pt[i] = (uint64_t *)(uintptr_t)pt_phys;
        as->pd[LOW_PD_INDEX][USER_PD_START + i] =
            pt_phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_WRITABLE | NTCLKS_PAGE_USER;
    }

    return true;
}

void address_space_destroy(struct address_space *as)
{
    if (!as) {
        return;
    }
    for (uint32_t table = 0; table < USER_PD_COUNT; ++table) {
        if (as->user_pt[table]) {
            for (uint32_t i = 0; i < 512; ++i) {
                uint64_t entry = as->user_pt[table][i];
                if (entry & NTCLKS_PAGE_PRESENT) {
                    mm_free_page(entry & ~0xfffULL);
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

bool address_space_map_user_page(struct address_space *as, uint64_t vaddr,
                                 uint64_t phys, uint64_t flags)
{
    if (!as || !phys || (phys & (PAGE_SIZE - 1))) {
        return false;
    }
    uint64_t page = align_down(vaddr, PAGE_SIZE);
    if (page < NTCLKS_USER_BASE || page >= NTCLKS_USER_TOP) {
        return false;
    }
    uint64_t index = (page - NTCLKS_USER_BASE) / PAGE_SIZE;
    uint64_t table = index / 512;
    uint64_t slot = index % 512;
    if (table >= USER_PD_COUNT || !as->user_pt[table]) {
        return false;
    }
    if (as->user_pt[table][slot] & NTCLKS_PAGE_PRESENT) {
        return false;
    }
    as->user_pt[table][slot] = phys | NTCLKS_PAGE_PRESENT | NTCLKS_PAGE_USER | flags;
    return true;
}

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
    if (table >= USER_PD_COUNT || !as->user_pt[table]) {
        return 0;
    }
    uint64_t entry = as->user_pt[table][slot];
    return (entry & NTCLKS_PAGE_PRESENT) ? (entry & ~0xfffULL) : 0;
}

bool address_space_map_user_stack(struct address_space *as, uint64_t stack_top)
{
    uint64_t first = stack_top - (uint64_t)NTCLKS_USER_STACK_PAGES * PAGE_SIZE;
    for (uint32_t i = 0; i < NTCLKS_USER_STACK_PAGES; ++i) {
        uint64_t phys = mm_alloc_page();
        if (!phys || !address_space_map_user_page(as, first + (uint64_t)i * PAGE_SIZE,
                                                  phys, NTCLKS_PAGE_WRITABLE)) {
            if (phys) {
                mm_free_page(phys);
            }
            return false;
        }
    }
    return true;
}
