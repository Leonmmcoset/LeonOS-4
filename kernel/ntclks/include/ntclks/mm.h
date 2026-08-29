/*
 * LeonOS physical-memory interface: declares page allocation and reclamation.
 * Provides memory statistics and reference management for kernel mappings.
 */
#ifndef NTCLKS_MM_H
#define NTCLKS_MM_H

#include <leonos/boot_handoff.h>
#include <ntclks/multiboot2.h>
#include <ntclks/types.h>

/**
 * @brief Initialize the physical-memory allocator from the boot map and handoff data.
 */
void mm_init(const struct boot_info *boot, const struct leonos_boot_handoff *handoff);
/**
 * @brief Return total physical RAM in KiB.
 */
uint64_t mm_total_memory_kib(void);
/**
 * @brief Return currently free physical RAM in KiB.
 */
uint64_t mm_free_memory_kib(void);
/**
 * @brief Allocate one physical page; returns its physical address (0 on failure).
 */
uint64_t mm_alloc_page(void);
/**
 * @brief Allocate page_count contiguous physical pages; returns the base address (0 on failure).
 */
uint64_t mm_alloc_pages(uint32_t page_count);
/**
 * @brief Return one physical page (phys) to the allocator.
 */
void mm_free_page(uint64_t phys);
/**
 * @brief Return page_count contiguous physical pages starting at phys.
 */
void mm_free_pages(uint64_t phys, uint32_t page_count);
/**
 * @brief Pin a physical page so it is never returned to the free pool.
 */
void mm_retain_page(uint64_t phys);

#endif
