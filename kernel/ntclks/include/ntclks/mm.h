#ifndef NTCLKS_MM_H
#define NTCLKS_MM_H

#include <leonos/boot_handoff.h>
#include <ntclks/multiboot2.h>
#include <ntclks/types.h>

void mm_init(const struct boot_info *boot, const struct leonos_boot_handoff *handoff);
uint64_t mm_total_memory_kib(void);
uint64_t mm_alloc_page(void);
uint64_t mm_alloc_pages(uint32_t page_count);
void mm_free_page(uint64_t phys);
void mm_free_pages(uint64_t phys, uint32_t page_count);

#endif
