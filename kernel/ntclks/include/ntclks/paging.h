/*
 * LeonOS paging interface: defines address-space and user-page operations.
 * Exposes page mapping, protection, unmapping, and memory-layout constants.
 */
#ifndef NTCLKS_PAGING_H
#define NTCLKS_PAGING_H

#include <ntclks/types.h>

#define NTCLKS_USER_BASE 0x0000000000400000ULL
/* Keep the user interval below the kernel's identity-mapped 128 MiB image
 * base. The extra range leaves room for large file mappings and application
 * heaps without replacing the kernel mapping in a user CR3. */
#define NTCLKS_USER_TOP  0x0000000007000000ULL
#define NTCLKS_USER_MMAP_BASE 0x0000000002000000ULL
#define NTCLKS_USER_STACK_PAGES 16u
#define NTCLKS_USER_PD_BYTES 0x200000ULL
#define NTCLKS_USER_PD_START (NTCLKS_USER_BASE / NTCLKS_USER_PD_BYTES)
#define NTCLKS_USER_PD_COUNT ((NTCLKS_USER_TOP - NTCLKS_USER_BASE) / NTCLKS_USER_PD_BYTES)

#define NTCLKS_PAGE_PRESENT 0x001ULL
#define NTCLKS_PAGE_WRITABLE 0x002ULL
#define NTCLKS_PAGE_USER 0x004ULL
#define NTCLKS_PAGE_NOEXEC (1ULL << 63)
#define NTCLKS_PHYS_ADDR_MASK 0x000ffffffffff000ULL

struct address_space {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd[4];
    uint64_t *user_pt[NTCLKS_USER_PD_COUNT];
    uint64_t cr3;
    uint32_t user_page_count;
};

/**
 * @brief Coordinates the paging init user identity operation.
 */
void paging_init_user_identity(void);
/**
 * @brief Coordinates the paging kernel cr3 operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t paging_kernel_cr3(void);
/**
 * @brief Coordinates the paging load cr3 operation.
 * @param cr3 Input or output value used by this operation.
 */
void paging_load_cr3(uint64_t cr3);

/**
 * @brief Coordinates the address space create operation.
 * @param as Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_create(struct address_space *as);
/**
 * @brief Coordinates the address space destroy operation.
 * @param as Input or output value used by this operation.
 */
void address_space_destroy(struct address_space *as);
/**
 * @brief Coordinates the address space prepare user range operation.
 * @param as Input or output value used by this operation.
 * @param start Input or output value used by this operation.
 * @param end Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_prepare_user_range(struct address_space *as, uint64_t start,
                                      uint64_t end);
/**
 * @brief Coordinates the address space map user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param phys Input or output value used by this operation.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_map_user_page(struct address_space *as, uint64_t vaddr,
                                 uint64_t phys, uint64_t flags);
/**
 * @brief Coordinates the address space protect user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @param flags Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_protect_user_page(struct address_space *as, uint64_t vaddr,
                                     uint64_t flags);
/**
 * @brief Coordinates the address space unmap user page operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
uint64_t address_space_unmap_user_page(struct address_space *as, uint64_t vaddr);
/**
 * @brief Coordinates the address space user page phys operation.
 * @param as Input or output value used by this operation.
 * @param vaddr Address used by this operation; its address-space interpretation follows the API.
 * @return Result, status, or value defined by this API.
 */
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr);
/**
 * @brief Coordinates the address space user memory kib operation.
 * @param as Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t address_space_user_memory_kib(const struct address_space *as);
/**
 * @brief Coordinates the address space map user stack operation.
 * @param as Input or output value used by this operation.
 * @param stack_top Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
bool address_space_map_user_stack(struct address_space *as, uint64_t stack_top);

#endif
