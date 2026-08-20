/*
 * LeonOS paging interface: defines address-space and user-page operations.
 * Exposes page mapping, protection, unmapping, and memory-layout constants.
 */
#ifndef NTCLKS_PAGING_H
#define NTCLKS_PAGING_H

#include <ntclks/types.h>

#define NTCLKS_USER_BASE 0x0000000000400000ULL
/* Keep the user interval below the kernel's low identity-map boundary.  The
 * previous 108 MiB window made large applications and mmap users collide;
 * 256 MiB leaves separate heap, mmap, file-map, and stack regions while still
 * allowing the 512 MiB legacy VM configuration to boot. */
#define NTCLKS_USER_TOP  0x0000000010000000ULL
#define NTCLKS_USER_MMAP_BASE 0x0000000008000000ULL
#define NTCLKS_USER_HEAP_BASE 0x0000000001000000ULL
#define NTCLKS_USER_HEAP_LIMIT NTCLKS_USER_MMAP_BASE
#define NTCLKS_USER_STACK_PAGES 16u
#define NTCLKS_USER_STACK_MAX_PAGES 2048u
/* The current loader places the kernel and middlelayer at 128 MiB.  Keep a
 * supervisor-only hole in every user CR3 so creating user page tables never
 * replaces those identity-mapped kernel PDEs. */
#define NTCLKS_KERNEL_HOLE_START 0x0000000008000000ULL
#define NTCLKS_KERNEL_HOLE_END   0x000000000c000000ULL
#define NTCLKS_USER_PD_BYTES 0x200000ULL
#define NTCLKS_USER_PD_START (NTCLKS_USER_BASE / NTCLKS_USER_PD_BYTES)
#define NTCLKS_USER_PD_COUNT ((NTCLKS_USER_TOP - NTCLKS_USER_BASE) / NTCLKS_USER_PD_BYTES)

#define NTCLKS_PAGE_PRESENT 0x001ULL
#define NTCLKS_PAGE_WRITABLE 0x002ULL
#define NTCLKS_PAGE_USER 0x004ULL
/* x86_64 makes bits 9-11 of a present PTE available to software.  A COW
 * mapping is deliberately read-only; the page-fault handler copies it before
 * restoring write permission for the faulting address space. */
#define NTCLKS_PAGE_COW 0x200ULL
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
 * @brief Clones a user address space using copy-on-write mappings.
 * @param source Existing user address space; writable pages become read-only COW mappings.
 * @param destination Zeroed output address space that receives independent page tables.
 * @return True on success; false after rolling back every destination mapping on failure.
 */
bool address_space_clone_cow(struct address_space *source, struct address_space *destination);
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
bool address_space_map_user_stack_page(struct address_space *as, uint64_t page);
/**
 * @brief Resolves a write protection fault on a copy-on-write user page.
 * @param as Faulting process address space.
 * @param vaddr User virtual address that raised the write fault.
 * @return True when the page was made private and writable, otherwise false.
 */
bool address_space_handle_cow_fault(struct address_space *as, uint64_t vaddr);

#endif
