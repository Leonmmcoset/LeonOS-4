#ifndef NTCLKS_PAGING_H
#define NTCLKS_PAGING_H

#include <ntclks/types.h>

#define NTCLKS_USER_BASE 0x0000000000400000ULL
#define NTCLKS_USER_TOP  0x0000000004000000ULL
#define NTCLKS_USER_MMAP_BASE 0x0000000002000000ULL
#define NTCLKS_USER_STACK_PAGES 16u
#define NTCLKS_USER_PD_BYTES 0x200000ULL
#define NTCLKS_USER_PD_START (NTCLKS_USER_BASE / NTCLKS_USER_PD_BYTES)
#define NTCLKS_USER_PD_COUNT ((NTCLKS_USER_TOP - NTCLKS_USER_BASE) / NTCLKS_USER_PD_BYTES)

#define NTCLKS_PAGE_PRESENT 0x001ULL
#define NTCLKS_PAGE_WRITABLE 0x002ULL
#define NTCLKS_PAGE_USER 0x004ULL

struct address_space {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd[4];
    uint64_t *user_pt[NTCLKS_USER_PD_COUNT];
    uint64_t cr3;
    uint32_t user_page_count;
};

void paging_init_user_identity(void);
uint64_t paging_kernel_cr3(void);
void paging_load_cr3(uint64_t cr3);

bool address_space_create(struct address_space *as);
void address_space_destroy(struct address_space *as);
bool address_space_map_user_page(struct address_space *as, uint64_t vaddr,
                                 uint64_t phys, uint64_t flags);
uint64_t address_space_unmap_user_page(struct address_space *as, uint64_t vaddr);
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr);
uint32_t address_space_user_memory_kib(const struct address_space *as);
bool address_space_map_user_stack(struct address_space *as, uint64_t stack_top);

#endif
