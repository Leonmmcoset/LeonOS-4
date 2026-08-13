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
 * @brief Coordinates the mm init operation.
 * @param boot Boot information supplied by the loader.
 * @param handoff Input or output value used by this operation.
 */
void mm_init(const struct boot_info *boot, const struct leonos_boot_handoff *handoff);
/**
 * @brief Coordinates the mm total memory kib operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t mm_total_memory_kib(void);
/**
 * @brief Coordinates the mm free memory kib operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t mm_free_memory_kib(void);
/**
 * @brief Coordinates the mm alloc page operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t mm_alloc_page(void);
/**
 * @brief Coordinates the mm alloc pages operation.
 * @param page_count Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
 */
uint64_t mm_alloc_pages(uint32_t page_count);
/**
 * @brief Coordinates the mm free page operation.
 * @param phys Input or output value used by this operation.
 */
void mm_free_page(uint64_t phys);
/**
 * @brief Coordinates the mm free pages operation.
 * @param phys Input or output value used by this operation.
 * @param page_count Length, size, or element count associated with the operation.
 */
void mm_free_pages(uint64_t phys, uint32_t page_count);
/**
 * @brief Coordinates the mm retain page operation.
 * @param phys Input or output value used by this operation.
 */
void mm_retain_page(uint64_t phys);

#endif
