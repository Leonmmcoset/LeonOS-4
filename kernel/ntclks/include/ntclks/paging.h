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
 * @brief Build the shared low identity mapping used as the base of every address space.
 */
void paging_init_user_identity(void);
/**
 * @brief Return the physical address of the kernel page-table root (the CR3 value).
 */
uint64_t paging_kernel_cr3(void);
/**
 * @brief Switch the CPU's page table to the root given by cr3.
 */
void paging_load_cr3(uint64_t cr3);

/**
 * @brief Allocate and initialize an empty address space; true on success.
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
 * @brief Free every page table and page owned by as.
 */
void address_space_destroy(struct address_space *as);
/**
 * @brief Allocate page-table structures covering the user range [start, end); true on success.
 */
bool address_space_prepare_user_range(struct address_space *as, uint64_t start,
                                      uint64_t end);
/**
 * @brief Map user vaddr to phys with the given present/writable/user/noexec flags; true on success.
 */
bool address_space_map_user_page(struct address_space *as, uint64_t vaddr,
                                 uint64_t phys, uint64_t flags);
/**
 * @brief Replace the protection flags of the user mapping at vaddr; true on success.
 */
bool address_space_protect_user_page(struct address_space *as, uint64_t vaddr,
                                     uint64_t flags);
/**
 * @brief Remove the user mapping at vaddr and return the physical page it held.
 */
uint64_t address_space_unmap_user_page(struct address_space *as, uint64_t vaddr);
/**
 * @brief Return the physical address backing user vaddr, or 0 if unmapped.
 */
uint64_t address_space_user_page_phys(const struct address_space *as, uint64_t vaddr);
/**
 * @brief Return how much user memory, in KiB, is currently mapped in as.
 */
uint32_t address_space_user_memory_kib(const struct address_space *as);
/**
 * @brief Map the initial user stack pages ending at stack_top; true on success.
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
