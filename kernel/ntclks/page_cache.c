/*
 * LeonOS read-only page cache implementation.
 * Entries retain physical pages until invalidated or replaced while idle.
 */
#include <ntclks/lock.h>
#include <ntclks/mm.h>
#include <ntclks/page_cache.h>

#define PAGE_CACHE_MAX 128u
#define PAGE_CACHE_PAGE_SIZE 4096ULL

struct page_cache_entry {
    uint8_t used;
    uint8_t reserved[3];
    uint32_t refs;
    uint64_t phys;
    uint64_t offset;
    struct storage_node node;
};

static struct page_cache_entry entries[PAGE_CACHE_MAX];
static struct kernel_spinlock cache_lock;
static uint32_t cache_hits;

static int node_equal(const struct storage_node *a, const struct storage_node *b)
{
    return a && b && a->type == b->type && a->flags == b->flags &&
           a->first_cluster == b->first_cluster && a->drive == b->drive &&
           a->size == b->size;
}

static struct page_cache_entry *find_entry(const struct storage_node *node,
                                           uint64_t offset)
{
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        if (entries[i].used && entries[i].offset == offset &&
            node_equal(&entries[i].node, node)) {
            return &entries[i];
        }
    }
    return NULL;
}

void page_cache_init(void)
{
    kernel_spin_init(&cache_lock);
    cache_hits = 0;
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        entries[i] = (struct page_cache_entry){0};
    }
}

int page_cache_lookup(const struct storage_node *node, uint64_t offset,
                      uint64_t *phys)
{
    uint64_t flags;
    struct page_cache_entry *entry;
    if (!node || !phys || (offset & (PAGE_CACHE_PAGE_SIZE - 1ULL))) {
        return -1;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    entry = find_entry(node, offset);
    if (entry) {
        ++entry->refs;
        ++cache_hits;
        *phys = entry->phys;
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
    return entry ? 0 : -1;
}

int page_cache_load(const struct storage_node *node, uint64_t offset,
                    page_cache_loader loader, void *context, uint64_t *phys)
{
    uint64_t flags;
    struct page_cache_entry *entry = NULL;
    uint64_t page;
    if (page_cache_lookup(node, offset, phys) == 0) {
        return 0;
    }
    if (!node || !loader || !phys || (offset & (PAGE_CACHE_PAGE_SIZE - 1ULL))) {
        return -1;
    }
    page = mm_alloc_page();
    if (!page || loader(page, context) < 0) {
        if (page) {
            mm_free_page(page);
        }
        return -1;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    entry = find_entry(node, offset);
    if (!entry) {
        for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
            if (!entries[i].used || entries[i].refs == 0) {
                if (entries[i].used && entries[i].phys) {
                    mm_free_page(entries[i].phys);
                }
                entry = &entries[i];
                break;
            }
        }
        if (entry) {
            entry->used = 1;
            entry->refs = 1;
            entry->phys = page;
            entry->offset = offset;
            entry->node = *node;
            *phys = page;
        }
    } else {
        ++entry->refs;
        *phys = entry->phys;
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
    if (!entry) {
        mm_free_page(page);
        return -1;
    }
    if (entry->phys != page) {
        mm_free_page(page);
    }
    return 0;
}

void page_cache_release(uint64_t phys)
{
    uint64_t flags;
    if (!phys) {
        return;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        if (entries[i].used && entries[i].phys == phys) {
            if (entries[i].refs) {
                --entries[i].refs;
            }
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
}

int page_cache_retain(uint64_t phys)
{
    uint64_t flags;
    if (!phys) {
        return -1;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        if (entries[i].used && entries[i].phys == phys) {
            ++entries[i].refs;
            kernel_spin_unlock_irqrestore(&cache_lock, flags);
            return 0;
        }
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
    return -1;
}

int page_cache_owns(uint64_t phys)
{
    uint64_t flags;
    int found = 0;
    if (!phys) {
        return 0;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        if (entries[i].used && entries[i].phys == phys) {
            found = 1;
            break;
        }
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
    return found;
}

void page_cache_invalidate_node(const struct storage_node *node)
{
    uint64_t flags;
    if (!node) {
        return;
    }
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        if (entries[i].used && node_equal(&entries[i].node, node) && !entries[i].refs) {
            mm_free_page(entries[i].phys);
            entries[i] = (struct page_cache_entry){0};
        }
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
}

void page_cache_stats(uint32_t *count, uint32_t *hits)
{
    uint64_t flags;
    uint32_t used = 0;
    kernel_spin_lock_irqsave(&cache_lock, &flags);
    for (uint32_t i = 0; i < PAGE_CACHE_MAX; ++i) {
        used += entries[i].used != 0;
    }
    if (count) {
        *count = used;
    }
    if (hits) {
        *hits = cache_hits;
    }
    kernel_spin_unlock_irqrestore(&cache_lock, flags);
}
