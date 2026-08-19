/*
 * LeonOS read-only page cache interface.
 * Caches file-backed code/data pages by storage identity and page offset.
 */
#ifndef NTCLKS_PAGE_CACHE_H
#define NTCLKS_PAGE_CACHE_H

#include <ntclks/storage.h>
#include <ntclks/types.h>

typedef int (*page_cache_loader)(uint64_t phys, void *context);

void page_cache_init(void);
int page_cache_lookup(const struct storage_node *node, uint64_t offset,
                      uint64_t *phys);
int page_cache_load(const struct storage_node *node, uint64_t offset,
                    page_cache_loader loader, void *context, uint64_t *phys);
void page_cache_release(uint64_t phys);
int page_cache_retain(uint64_t phys);
int page_cache_owns(uint64_t phys);
void page_cache_invalidate_node(const struct storage_node *node);
void page_cache_stats(uint32_t *entries, uint32_t *hits);

#endif
