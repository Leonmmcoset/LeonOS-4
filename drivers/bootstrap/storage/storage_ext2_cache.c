/* Clean, write-through blocks only. Storage transactions serialize access. */
#define EXT2_CACHE_ENTRIES 128u
#define EXT2_CACHE_BYTES 4096u
struct ext2_cache_entry {
    struct storage_volume *volume;
    uint64_t lba;
    uint32_t sectors;
    uint64_t age;
    uint8_t data[EXT2_CACHE_BYTES];
};
static struct ext2_cache_entry *ext2_cache;
static uint64_t ext2_cache_age;

static void ext2_cache_reset(struct storage_volume *volume)
{
    if (storage_read_cache.volume == volume) storage_read_cache.valid = 0;
    if (!ext2_cache) return;
    for (unsigned i = 0; i < EXT2_CACHE_ENTRIES; ++i)
        if (ext2_cache[i].volume == volume) ext2_cache[i].sectors = 0;
}

static void ext2_cache_invalidate_range(uint64_t lba, uint32_t sectors)
{
    /* Invalidate aliases too: raw disk writes can use a temporary volume
     * object instead of the mounted partition's identity. Extra eviction on
     * another disk at the same LBA is harmless; stale aliases are not. */
    if (storage_read_cache.valid &&
        (lba <= storage_read_cache.first_lba
            ? storage_read_cache.first_lba - lba < sectors
            : lba - storage_read_cache.first_lba < storage_read_cache.sector_count))
        storage_read_cache.valid = 0;
    if (!ext2_cache) return;
    for (unsigned i = 0; i < EXT2_CACHE_ENTRIES; ++i) {
        if (ext2_cache[i].sectors &&
            (lba <= ext2_cache[i].lba
                ? ext2_cache[i].lba - lba < sectors
                : lba - ext2_cache[i].lba < ext2_cache[i].sectors))
            ext2_cache[i].sectors = 0;
    }
}

static int ext2_cache_read(uint64_t lba, uint32_t sectors, void *buffer)
{
    if (!ext2_cache) return 0;
    for (unsigned i = 0; i < EXT2_CACHE_ENTRIES; ++i) {
        if (ext2_cache[i].volume == g_active_volume && ext2_cache[i].sectors &&
            lba >= ext2_cache[i].lba && lba - ext2_cache[i].lba <= ext2_cache[i].sectors &&
            sectors <= ext2_cache[i].sectors - (lba - ext2_cache[i].lba)) {
            storage_memcpy(buffer, ext2_cache[i].data + (lba - ext2_cache[i].lba) * SECTOR_SIZE,
                           sectors * SECTOR_SIZE);
            ext2_cache[i].age = ++ext2_cache_age;
            return 1;
        }
    }
    return 0;
}

static void ext2_cache_store(uint64_t lba, uint32_t sectors, const void *buffer)
{
    unsigned victim = 0;
    if (!sectors || sectors > EXT2_CACHE_BYTES / SECTOR_SIZE) return;
    if (!ext2_cache) {
        /* Keep the bounded cache outside the fixed kernel image load region.
         * Allocation failure only disables caching for this I/O. */
        size_t bytes = sizeof(*ext2_cache) * EXT2_CACHE_ENTRIES;
        uint64_t phys = mm_alloc_pages((uint32_t)((bytes + 4095u) / 4096u));
        if (!phys) return;
        ext2_cache = (struct ext2_cache_entry *)(uintptr_t)phys;
        storage_memzero(ext2_cache, bytes);
    }
    for (unsigned i = 0; i < EXT2_CACHE_ENTRIES; ++i) {
        if (!ext2_cache[i].sectors) { victim = i; break; }
        if (ext2_cache[i].age < ext2_cache[victim].age) victim = i;
    }
    storage_memcpy(ext2_cache[victim].data, buffer, sectors * SECTOR_SIZE);
    ext2_cache[victim].volume = g_active_volume;
    ext2_cache[victim].lba = lba;
    ext2_cache[victim].age = ++ext2_cache_age;
    ext2_cache[victim].sectors = sectors;
}
