/*
 * exFAT storage backend.
 *
 * This deliberately keeps filesystem code independent of the transport.  All
 * media accesses below use storage_read_sectors()/storage_write_sectors(), so
 * AHCI, IDE PIO and NVMe share one locking and recovery path.
 */

#include "storage_exfat_upcase.inc"

static uint16_t storage_exfat_upcase_cache[STORAGE_MAX_VOLUMES][65536];
static uint8_t storage_exfat_upcase_ready[STORAGE_MAX_VOLUMES];
/* The upcase stream is at most 65536 UTF-16 code units (the standard table
 * is much smaller).  Keep one raw copy while mounting so decoding never
 * performs hundreds of tiny device reads against a freshly formatted target.
 * The buffer is also aligned for transports which require DMA-safe memory. */
static uint8_t storage_exfat_upcase_data[65536u * sizeof(uint16_t)]
    __attribute__((aligned(4096)));

/* exFAT metadata is accessed much more frequently than ordinary file data:
 * directory walks follow FAT links and allocation updates touch one bit at a
 * time. Keep a small write-through cache for each metadata stream. The cache
 * is deliberately private to exFAT so the generic FAT32 cache invalidation
 * cannot evict it halfway through a cluster allocation transaction. */
#define EXFAT_META_CACHE_SECTORS 8u
#define EXFAT_READ_BATCH_SECTORS 8u
static uint8_t exfat_fat_cache_data[EXFAT_META_CACHE_SECTORS * SECTOR_SIZE]
    __attribute__((aligned(4096)));
static struct storage_volume *exfat_fat_cache_volume;
static uint64_t exfat_fat_cache_lba;
static uint32_t exfat_fat_cache_count;
static uint8_t exfat_fat_cache_valid;

static uint8_t exfat_bitmap_cache_data[EXFAT_META_CACHE_SECTORS * SECTOR_SIZE]
    __attribute__((aligned(4096)));
static struct storage_volume *exfat_bitmap_cache_volume;
static uint64_t exfat_bitmap_cache_offset;
static uint32_t exfat_bitmap_cache_bytes;
static uint8_t exfat_bitmap_cache_valid;
static uint8_t exfat_bitmap_cache_dirty;

static uint8_t exfat_dir_cache_data[64u * SECTOR_SIZE]
    __attribute__((aligned(4096)));
static uint8_t exfat_zero_data[64u * SECTOR_SIZE]
    __attribute__((aligned(4096)));
struct exfat_chain_hint {
    struct storage_volume *volume;
    uint32_t first;
    uint32_t cluster;
    uint64_t index;
    uint8_t nofat;
    uint8_t valid;
};
static struct exfat_chain_hint exfat_chain_hint;
static struct storage_volume *exfat_dir_cache_volume;
static uint32_t exfat_dir_cache_cluster;
static uint8_t exfat_dir_cache_nofat;
static uint64_t exfat_dir_cache_offset;
static uint32_t exfat_dir_cache_bytes;
static uint8_t exfat_dir_cache_valid;

static int exfat_bitmap_flush(void);

/*
 * AHCI has an optional syscall-level asynchronous path.  A filesystem read
 * can, however, consist of several transport requests (for example a read
 * crossing a cluster boundary).  Replaying the syscall from its beginning
 * while a later request is pending makes those requests alternate forever:
 * the caller asks for request A while the controller is still completing B.
 * Keep exFAT's metadata and stream operations atomic with respect to that
 * retry protocol.  The transport still serializes access and has bounded
 * waits; only the restartable async shortcut is disabled for this operation.
 */
static int exfat_transport_read(uint64_t lba, uint32_t sectors, void *buffer)
{
    bool saved_async = storage_io_async_context;
    bool saved_write_started = storage_io_write_started;
    int ret;
    /* exFAT requests are often chained (FAT -> directory -> payload).  A
     * single syscall retry must never replay only part of that chain while an
     * earlier AHCI command is still pending.  Force every metadata/data
     * request through the synchronous transport, even when the caller did not
     * come through the normal syscall async context (page faults and boot
     * helpers can enter here directly). */
    storage_io_async_context = false;
    storage_io_write_started = true;
    ret = storage_read_sectors(lba, sectors, buffer);
    storage_io_async_context = saved_async;
    storage_io_write_started = saved_write_started;
    return ret;
}

static int exfat_transport_write(uint64_t lba, uint32_t sectors,
                                 const void *buffer)
{
    bool saved_async = storage_io_async_context;
    bool saved_write_started = storage_io_write_started;
    int ret;
    storage_io_async_context = false;
    storage_io_write_started = true;
    ret = storage_write_sectors(lba, sectors, buffer);
    storage_io_async_context = saved_async;
    storage_io_write_started = saved_write_started;
    return ret;
}

/* Keep metadata reads within the same small request size that the installer
 * transport has already proven reliable.  A failed multi-sector request is
 * retried one sector at a time so a transient AHCI/NVMe/IDE completion error
 * does not turn a valid directory into a metadata-corruption report. */
static int exfat_read_sectors_resilient(uint64_t lba, uint32_t sectors,
                                        uint8_t *data)
{
    if (!data || !sectors) return -22;
    while (sectors) {
        uint32_t chunk = min_u32(sectors, EXFAT_READ_BATCH_SECTORS);
        int ret = exfat_transport_read(lba, chunk, data);
        if (ret == -LEONOS_EAGAIN) return ret;
        if (ret < 0 && chunk > 1u) {
            console_printf("[ntclks] exfat transport read batch failed lba=%llu sectors=%u ret=%d; retrying sectors\n",
                           (unsigned long long)lba, chunk, ret);
            ret = 0;
            for (uint32_t i = 0; i < chunk; ++i) {
                int one = exfat_transport_read(lba + i, 1u,
                                               data + (size_t)i * SECTOR_SIZE);
                if (one == -LEONOS_EAGAIN) return one;
                if (one < 0) {
                    console_printf("[ntclks] exfat transport read sector failed lba=%llu ret=%d\n",
                                   (unsigned long long)(lba + i), one);
                    return one;
                }
            }
        }
        if (ret < 0) return ret;
        lba += chunk;
        sectors -= chunk;
        data += (size_t)chunk * SECTOR_SIZE;
    }
    return 0;
}

/* Some legacy AHCI implementations intermittently reject a multi-sector
 * write while accepting the exact same sectors as individual commands.  The
 * filesystem must not lose an installation because of that transport quirk:
 * retry the request one sector at a time after the normal transport retry
 * path has failed.  Each sector write is idempotent, so this is safe even if
 * the controller completed part of the original request before reporting an
 * error. */
static int exfat_write_sectors_resilient(uint64_t lba, uint32_t sectors,
                                         const uint8_t *data)
{
    int ret;
    if (!data || !sectors) return -22;
    ret = exfat_transport_write(lba, sectors, data);
    if (ret >= 0 || sectors == 1u) return ret;
    console_printf("[ntclks] exfat write batch fallback lba=%llu sectors=%u ret=%d\n",
                   (unsigned long long)lba, sectors, ret);
    for (uint32_t i = 0; i < sectors; ++i) {
        storage_memcpy(storage_scratch, data + (size_t)i * SECTOR_SIZE, SECTOR_SIZE);
        ret = exfat_transport_write(lba + i, 1u, storage_scratch);
        if (ret < 0) return ret;
    }
    return 0;
}

static void exfat_cache_invalidate(void)
{
    /* A dirty bitmap only exists during a completed allocation/free operation;
     * flush it before dropping the cache so a volume switch cannot lose bits. */
    if (exfat_bitmap_cache_dirty) {
        (void)exfat_bitmap_flush();
    }
    exfat_fat_cache_valid = 0;
    exfat_fat_cache_volume = 0;
    exfat_fat_cache_count = 0;
    exfat_bitmap_cache_valid = 0;
    exfat_bitmap_cache_volume = 0;
    exfat_bitmap_cache_bytes = 0;
    exfat_bitmap_cache_dirty = 0;
    exfat_dir_cache_volume = 0;
    exfat_dir_cache_cluster = 0;
    exfat_dir_cache_offset = 0;
    exfat_dir_cache_bytes = 0;
    exfat_dir_cache_valid = 0;
    exfat_chain_hint.valid = 0;
}
struct exfat_dir_ref {
    uint32_t directory_cluster;
    uint32_t first_entry;
    uint8_t directory_nofat;
    uint8_t secondary_count;
};

static int exfat_grow_directory(uint32_t directory_cluster, uint8_t directory_nofat,
                                const struct exfat_dir_ref *self_ref);
static uint16_t exfat_name_hash(const uint16_t *name, uint32_t length);

static uint16_t exfat_get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void exfat_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint64_t exfat_get_u64(const uint8_t *p)
{
    return (uint64_t)storage_get_u32(p) | ((uint64_t)storage_get_u32(p + 4u) << 32);
}

static void exfat_put_u64(uint8_t *p, uint64_t value)
{
    storage_put_u32(p, (uint32_t)value);
    storage_put_u32(p + 4u, (uint32_t)(value >> 32));
}

static int exfat_cluster_valid(uint32_t cluster)
{
    return cluster >= 2u && cluster - 2u < g_storage.exfat_cluster_count;
}

static uint64_t exfat_cluster_lba(uint32_t cluster)
{
    return g_storage.exfat_start_lba + g_storage.exfat_cluster_heap_offset +
           (uint64_t)(cluster - 2u) * g_storage.sectors_per_cluster;
}

static uint16_t exfat_checksum(const uint8_t *entries, uint32_t count)
{
    uint16_t sum = 0;
    uint32_t bytes = count * EXFAT_ENTRY_SIZE;
    for (uint32_t i = 0; i < bytes; ++i) {
        if (i == 2u || i == 3u) {
            continue;
        }
        sum = (uint16_t)((sum << 15) | (sum >> 1));
        sum = (uint16_t)(sum + entries[i]);
    }
    return sum;
}

static int exfat_boot_checksum(uint64_t base_lba, uint32_t *out_checksum)
{
    uint32_t sum = 0;
    if (!out_checksum) return -22;
    for (uint32_t sector = 0; sector < 11u; ++sector) {
        int ret = exfat_read_sectors_resilient(base_lba + sector, 1u,
                                               storage_scratch);
        if (ret < 0) {
            console_printf("[ntclks] exfat boot checksum read failed lba=%llu sector=%u ret=%d\n",
                           (unsigned long long)(base_lba + sector), sector, ret);
            return ret;
        }
        for (uint32_t byte = 0; byte < SECTOR_SIZE; ++byte) {
            if (sector == 0u && (byte == 106u || byte == 107u || byte == 112u)) {
                continue;
            }
            sum = (sum << 31) | (sum >> 1);
            sum += storage_scratch[byte];
        }
    }
    *out_checksum = sum;
    return 0;
}

static int exfat_boot_region_valid(uint64_t base_lba, uint8_t boot_sector[SECTOR_SIZE])
{
    uint32_t checksum;
    int read_ret = exfat_read_sectors_resilient(base_lba, 1u, storage_scratch);
    if (read_ret == 0) {
        storage_memcpy(boot_sector, storage_scratch, SECTOR_SIZE);
    }
    console_printf("[ntclks] exfat_boot_region_valid: storage_read_sectors(lba=%llu) returned %d\n",
                   (unsigned long long)base_lba, read_ret);
    if (read_ret < 0) {
        console_printf("[ntclks] exfat_boot_region_valid: read failed at lba=%llu\n",
                       (unsigned long long)base_lba);
        return read_ret;
    }
    console_printf("[ntclks] exfat_boot_region_valid: signature bytes: %c%c%c%c%c%c%c%c\n",
                   boot_sector[3], boot_sector[4], boot_sector[5], boot_sector[6],
                   boot_sector[7], boot_sector[8], boot_sector[9], boot_sector[10]);
    if (storage_memcmp(boot_sector + 3u, "EXFAT   ", 8u) != 0) {
        console_printf("[ntclks] exfat_boot_region_valid: signature mismatch\n");
        return -2;
    }
    if (boot_sector[510] != 0x55u || boot_sector[511] != 0xaau) {
        console_printf("[ntclks] exfat_boot_region_valid: boot signature invalid: %x %x\n",
                       boot_sector[510], boot_sector[511]);
        return -2;
    }
    /* Every extended boot sector in the region has its own signature.  The
     * final two sectors are reserved and must remain zero.  Checking these
     * bytes prevents accepting a boot checksum copied over a damaged region. */
    for (uint32_t sector = 1u; sector <= 10u; ++sector) {
        read_ret = exfat_read_sectors_resilient(base_lba + sector, 1u,
                                                storage_scratch);
        if (read_ret < 0) {
            console_printf("[ntclks] exfat_boot_region_valid: read failed at sector %u ret=%d\n",
                           sector, read_ret);
            return read_ret;
        }
        if (sector <= 8u) {
            if (storage_scratch[510] != 0x55u || storage_scratch[511] != 0xaau) {
                console_printf("[ntclks] exfat_boot_region_valid: extended boot sector %u signature invalid: %x %x\n",
                               sector, storage_scratch[510], storage_scratch[511]);
                return -5;
            }
        } else {
            for (uint32_t byte = 0; byte < SECTOR_SIZE; ++byte) {
                if (storage_scratch[byte] != 0u) {
                    console_printf("[ntclks] exfat_boot_region_valid: reserved sector %u not zero at byte %u\n",
                                   sector, byte);
                    return -5;
                }
            }
        }
    }
    read_ret = exfat_boot_checksum(base_lba, &checksum);
    if (read_ret < 0) {
        console_printf("[ntclks] exfat_boot_region_valid: checksum read failed ret=%d\n",
                       read_ret);
        return read_ret;
    }
    console_printf("[ntclks] exfat_boot_region_valid: computed checksum = 0x%08x\n", checksum);
    read_ret = exfat_read_sectors_resilient(base_lba + 11u, 1u, storage_scratch);
    if (read_ret < 0) {
        console_printf("[ntclks] exfat_boot_region_valid: failed to read checksum sector ret=%d\n",
                       read_ret);
        return read_ret;
    }
    for (uint32_t off = 0; off < SECTOR_SIZE; off += 4u) {
        uint32_t stored = storage_get_u32(storage_scratch + off);
        if (stored != checksum) {
            console_printf("[ntclks] exfat_boot_region_valid: checksum mismatch at offset %u: stored=0x%08x computed=0x%08x\n",
                           off, stored, checksum);
            return -5;
        }
    }
    console_printf("[ntclks] exfat_boot_region_valid: all checks passed\n");
    return 0;
}

static int exfat_fat_cache_load(uint64_t lba)
{
    uint64_t fat_start = g_storage.exfat_start_lba + g_storage.exfat_fat_offset;
    uint64_t fat_end = fat_start + g_storage.exfat_fat_length;
    uint64_t first;
    uint32_t count;
    if (lba < fat_start || lba >= fat_end) return -5;
    if (exfat_fat_cache_valid && exfat_fat_cache_volume == g_active_volume &&
        lba >= exfat_fat_cache_lba &&
        lba < exfat_fat_cache_lba + exfat_fat_cache_count) return 0;
    first = fat_start + ((lba - fat_start) / EXFAT_META_CACHE_SECTORS) *
            EXFAT_META_CACHE_SECTORS;
    count = (uint32_t)min_u64(EXFAT_META_CACHE_SECTORS, fat_end - first);
    {
        int ret = exfat_read_sectors_resilient(first, count, exfat_fat_cache_data);
        if (ret < 0) {
            console_printf("[ntclks] exfat FAT cache read failed volume=%u lba=%llu sectors=%u ret=%d\n",
                           g_storage.volume_id, (unsigned long long)first, count, ret);
            exfat_fat_cache_valid = 0;
            exfat_fat_cache_volume = 0;
            return ret;
        }
    }
    exfat_fat_cache_lba = first;
    exfat_fat_cache_count = count;
    exfat_fat_cache_volume = g_active_volume;
    exfat_fat_cache_valid = 1;
    return 0;
}

static int exfat_read_fat(uint32_t cluster, uint32_t *next)
{
    uint64_t lba;
    uint32_t offset;
    if (!next || !exfat_cluster_valid(cluster)) return -22;
    lba = g_storage.exfat_start_lba + g_storage.exfat_fat_offset +
          ((uint64_t)cluster * 4u) / SECTOR_SIZE;
    {
        int ret = exfat_fat_cache_load(lba);
        if (ret < 0) return ret;
    }
    offset = (cluster * 4u) % SECTOR_SIZE +
             (uint32_t)(lba - exfat_fat_cache_lba) * SECTOR_SIZE;
    if (offset + sizeof(uint32_t) > exfat_fat_cache_count * SECTOR_SIZE) return -5;
    *next = storage_get_u32(exfat_fat_cache_data + offset);
    return 0;
}

static int exfat_write_fat(uint32_t cluster, uint32_t next)
{
    uint64_t lba;
    uint32_t offset;
    uint32_t sector_offset;
    if (!exfat_cluster_valid(cluster) ||
        (next < 2u && next != 0u) ||
        (next >= 2u && next < EXFAT_EOC && !exfat_cluster_valid(next))) return -22;
    lba = g_storage.exfat_start_lba + g_storage.exfat_fat_offset +
          ((uint64_t)cluster * 4u) / SECTOR_SIZE;
    {
        int ret = exfat_fat_cache_load(lba);
        if (ret < 0) return ret;
    }
    sector_offset = (uint32_t)(lba - exfat_fat_cache_lba);
    offset = sector_offset * SECTOR_SIZE + (cluster * 4u) % SECTOR_SIZE;
    if (offset + sizeof(uint32_t) > exfat_fat_cache_count * SECTOR_SIZE) return -5;
    storage_put_u32(exfat_fat_cache_data + offset, next);
    /* Write-through keeps crash behavior identical to the old path while the
     * read cache removes the redundant sector read for adjacent entries. */
    {
        int ret = exfat_write_sectors_resilient(
            lba, 1u, exfat_fat_cache_data + sector_offset * SECTOR_SIZE);
        if (ret < 0) {
            exfat_fat_cache_valid = 0;
            exfat_fat_cache_volume = 0;
            return ret;
        }
    }
    return 0;
}

static int exfat_cluster_at(uint32_t first, uint8_t nofat, uint64_t index,
                            uint32_t *out_cluster)
{
    uint32_t cluster = first;
    uint64_t remaining = index;
    if (!out_cluster || !exfat_cluster_valid(first)) {
        return -22;
    }
    if (nofat) {
        if (index >= g_storage.exfat_cluster_count ||
            first > UINT32_MAX - (uint32_t)index ||
            !exfat_cluster_valid(first + (uint32_t)index)) {
            return -5;
        }
        *out_cluster = first + (uint32_t)index;
        return 0;
    }
    if (index > g_storage.exfat_cluster_count) {
        return -5;
    }
    if (!nofat && exfat_chain_hint.valid && exfat_chain_hint.volume == g_active_volume &&
        exfat_chain_hint.first == first && exfat_chain_hint.nofat == nofat &&
        index >= exfat_chain_hint.index) {
        cluster = exfat_chain_hint.cluster;
        remaining = index - exfat_chain_hint.index;
    }
    while (remaining--) {
        int ret = exfat_read_fat(cluster, &cluster);
        if (ret < 0) {
            return ret;
        }
        if (cluster < 2u || cluster >= EXFAT_EOC || !exfat_cluster_valid(cluster)) {
            return -5;
        }
    }
    *out_cluster = cluster;
    if (!nofat) {
        exfat_chain_hint.volume = g_active_volume;
        exfat_chain_hint.first = first;
        exfat_chain_hint.cluster = cluster;
        exfat_chain_hint.index = index;
        exfat_chain_hint.nofat = nofat;
        exfat_chain_hint.valid = 1u;
    }
    return 0;
}

/* System directory entries (allocation bitmap and upcase table) do not have
 * a stream-extension flags byte.  Implementations commonly store them as a
 * contiguous extent with FAT[first] == EOC, while the installer formatter
 * uses an ordinary FAT chain.  Detect the representation from the on-disk
 * chain and reject ambiguous/corrupt layouts rather than assuming one form. */
static int exfat_system_stream_mode(uint32_t first, uint64_t length, uint8_t *out_nofat)
{
    uint64_t count;
    uint32_t cluster;
    if (!out_nofat || !exfat_cluster_valid(first) || !length) return -22;
    count = (length + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes;
    if (count == 0 || count > g_storage.exfat_cluster_count) return -5;
    cluster = first;
    /* A system stream has no stream-extension flags.  For a one-cluster
     * stream the FAT value is inherently ambiguous, but either access mode
     * addresses the same first cluster.  Prefer the contiguous interpretation
     * when the following physical clusters are valid and the first FAT entry
     * is an end marker; this is what mkfs.exfat and the installer formatter
     * emit for bitmap/upcase extents. */
    if (count == 1u) {
        uint32_t next;
        int ret = exfat_read_fat(first, &next);
        if (ret < 0) return ret;
        if (next < EXFAT_EOC) {
            return -5;
        }
        *out_nofat = 1u;
        return 0;
    }
    for (uint64_t index = 1u; index < count; ++index) {
        uint32_t next;
        {
            int ret = exfat_read_fat(cluster, &next);
            if (ret < 0) return ret;
        }
        if (next >= EXFAT_EOC) {
            if (first > UINT32_MAX - (uint32_t)(count - 1u) ||
                !exfat_cluster_valid(first + (uint32_t)(count - 1u))) return -5;
            *out_nofat = 1u;
            return 0;
        }
        if (!exfat_cluster_valid(next)) return -5;
        /* A FAT loop would otherwise make a short system file appear valid
         * while repeatedly reading the same cluster.  The system streams are
         * small, so a bounded duplicate check is preferable to trusting the
         * chain blindly. */
        for (uint64_t prior = 0u; prior < index; ++prior) {
            uint32_t seen;
            {
                int ret = exfat_cluster_at(first, 0u, prior, &seen);
                if (ret < 0) return ret;
                if (seen == next) return -5;
            }
        }
        cluster = next;
    }
    {
        uint32_t next;
        {
            int ret = exfat_read_fat(cluster, &next);
            if (ret < 0) return ret;
            if (next < EXFAT_EOC) return -5;
        }
    }
    *out_nofat = 0u;
    return 0;
}

static int exfat_validate_fat_chain(uint32_t first)
{
    uint32_t cluster = first;
    for (uint32_t count = 0u; count < g_storage.exfat_cluster_count; ++count) {
        uint32_t next;
        if (!exfat_cluster_valid(cluster)) return -5;
        {
            int ret = exfat_read_fat(cluster, &next);
            if (ret < 0) return ret;
        }
        if (next >= EXFAT_EOC) return 0;
        if (!exfat_cluster_valid(next)) return -5;
        cluster = next;
    }
    return -5;
}

static int exfat_fat_chain_contains(uint32_t first, uint32_t wanted)
{
    uint32_t cluster = first;
    for (uint32_t count = 0u; count < g_storage.exfat_cluster_count; ++count) {
        uint32_t next;
        if (!exfat_cluster_valid(cluster)) return -5;
        {
            int ret = exfat_read_fat(cluster, &next);
            if (ret < 0) return ret;
        }
        if (cluster == wanted) return 1;
        if (next >= EXFAT_EOC) return 0;
        if (!exfat_cluster_valid(next)) return -5;
        cluster = next;
    }
    return -5;
}

static int exfat_stream_contains_cluster(uint32_t first, uint8_t nofat,
                                          uint64_t length, uint32_t wanted)
{
    uint64_t count = (length + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes;
    if (!exfat_cluster_valid(wanted) || count == 0u || count > g_storage.exfat_cluster_count) {
        return -22;
    }
    for (uint64_t index = 0u; index < count; ++index) {
        uint32_t cluster;
        {
            int ret = exfat_cluster_at(first, nofat, index, &cluster);
            if (ret < 0) return ret;
        }
        if (cluster == wanted) return 1;
    }
    return 0;
}

/* Read arbitrary bytes from an allocated exFAT cluster stream. */
static int exfat_read_stream(uint32_t first, uint8_t nofat, uint64_t offset,
                             void *buffer, uint32_t len)
{
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0;
    uint64_t cluster_index;
    uint32_t cluster;
    if ((!buffer && len) || (len && !exfat_cluster_valid(first)) ||
        offset > UINT64_MAX - len) {
        return -22;
    }
    if (!len) return 0;
    cluster_index = offset / g_storage.cluster_bytes;
    {
        int ret = exfat_cluster_at(first, nofat, cluster_index, &cluster);
        if (ret < 0) {
        console_printf("[ntclks] exfat stream cluster lookup failed first=%u nofat=%u index=%llu\n",
                       first, nofat, (unsigned long long)cluster_index);
            return ret;
        }
    }
    while (done < len) {
        uint64_t pos = offset + done;
        uint32_t cluster_offset = (uint32_t)(pos % g_storage.cluster_bytes);
        uint32_t take;
        uint32_t sector_offset;
        uint64_t lba;
        int ret;
        take = min_u32(len - done, g_storage.cluster_bytes - cluster_offset);
        sector_offset = cluster_offset % SECTOR_SIZE;
        lba = exfat_cluster_lba(cluster) + cluster_offset / SECTOR_SIZE;
        if (sector_offset) {
            uint32_t part = min_u32(take, SECTOR_SIZE - sector_offset);
            ret = exfat_read_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) return ret;
            storage_memcpy(dst + done, storage_scratch + sector_offset, part);
            done += part;
            /* A partial-sector read can consume the final bytes of a cluster.
             * Move along the FAT chain before the next iteration; otherwise a
             * 512-byte read straddling a 32 KiB boundary would restart at the
             * old cluster's offset zero and duplicate the previous cluster. */
            if (done < len && cluster_offset + part == g_storage.cluster_bytes) {
                uint32_t next;
                if (nofat) {
                    if (!exfat_cluster_valid(cluster + 1u)) {
                        console_printf("[ntclks] exfat stream contiguous chain ended first=%u cluster=%u done=%u len=%u\n",
                                       first, cluster, done, len);
                        return -5;
                    }
                    cluster = cluster + 1u;
                } else {
                    int fat_ret = exfat_read_fat(cluster, &next);
                    if (fat_ret < 0) {
                        console_printf("[ntclks] exfat stream FAT read failed first=%u cluster=%u done=%u len=%u ret=%d\n",
                                       first, cluster, done, len, fat_ret);
                        return fat_ret;
                    }
                    if (next < 2u || next >= EXFAT_EOC || !exfat_cluster_valid(next)) {
                        console_printf("[ntclks] exfat stream FAT chain ended first=%u cluster=%u done=%u len=%u next=%u\n",
                                       first, cluster, done, len, next);
                        return -5;
                    }
                    cluster = next;
                }
            }
            continue;
        }
        while (take >= SECTOR_SIZE) {
            uint32_t sectors = min_u32(take / SECTOR_SIZE,
                                        (uint32_t)(sizeof(storage_cluster_buf) / SECTOR_SIZE));
            /* The caller may be a Ring-3 buffer.  Transport DMA addresses are
             * physical addresses, while user pointers are process virtual
             * addresses and are not guaranteed to be contiguous.  Always DMA
             * into the aligned kernel scratch and copy out afterwards. */
            ret = exfat_read_sectors_resilient(lba, sectors, storage_cluster_buf);
            if (ret < 0) return ret;
            storage_memcpy(dst + done, storage_cluster_buf, sectors * SECTOR_SIZE);
            lba += sectors;
            done += sectors * SECTOR_SIZE;
            take -= sectors * SECTOR_SIZE;
        }
        if (take) {
            ret = exfat_read_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) return ret;
            storage_memcpy(dst + done, storage_scratch, take);
            done += take;
        }
        if (done < len) {
            uint32_t next;
            if (nofat) {
                if (!exfat_cluster_valid(cluster + 1u)) {
                    console_printf("[ntclks] exfat stream contiguous chain ended first=%u cluster=%u done=%u len=%u\n",
                                   first, cluster, done, len);
                    return -5;
                }
                cluster = cluster + 1u;
            } else {
                int fat_ret = exfat_read_fat(cluster, &next);
                if (fat_ret < 0) {
                    console_printf("[ntclks] exfat stream FAT read failed first=%u cluster=%u done=%u len=%u ret=%d\n",
                                   first, cluster, done, len, fat_ret);
                    return fat_ret;
                }
                if (next < 2u || next >= EXFAT_EOC || !exfat_cluster_valid(next)) {
                    console_printf("[ntclks] exfat stream FAT chain ended first=%u cluster=%u done=%u len=%u next=%u\n",
                                   first, cluster, done, len, next);
                    return -5;
                }
                cluster = next;
            }
        }
    }
    return 0;
}

/* Write arbitrary bytes into already-reserved clusters. */
static int exfat_write_stream(uint32_t first, uint8_t nofat, uint64_t offset,
                              const void *buffer, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t done = 0;
    uint64_t cluster_index;
    uint32_t cluster;
    if ((!buffer && len) || (len && !exfat_cluster_valid(first)) ||
        offset > UINT64_MAX - len) {
        return -22;
    }
    if (!len) return 0;
    cluster_index = offset / g_storage.cluster_bytes;
    {
        int ret = exfat_cluster_at(first, nofat, cluster_index, &cluster);
        if (ret < 0) return ret;
    }
    while (done < len) {
        uint64_t pos = offset + done;
        uint32_t cluster_offset = (uint32_t)(pos % g_storage.cluster_bytes);
        uint32_t take;
        uint32_t sector_offset;
        uint64_t lba;
        int ret;
        take = min_u32(len - done, g_storage.cluster_bytes - cluster_offset);
        sector_offset = cluster_offset % SECTOR_SIZE;
        lba = exfat_cluster_lba(cluster) + cluster_offset / SECTOR_SIZE;
        if (sector_offset) {
            uint32_t part = min_u32(take, SECTOR_SIZE - sector_offset);
            ret = exfat_read_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) {
                console_printf("[ntclks] exfat stream read-modify-read failed first=%u nofat=%u lba=%llu ret=%d\n",
                               first, nofat, (unsigned long long)lba, ret);
                return ret;
            }
            storage_memcpy(storage_scratch + sector_offset, src + done, part);
            ret = exfat_write_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) {
                console_printf("[ntclks] exfat stream read-modify-write failed first=%u nofat=%u lba=%llu ret=%d\n",
                               first, nofat, (unsigned long long)lba, ret);
                return ret;
            }
            done += part;
            /* Keep the write path consistent with reads: a partial-sector
             * update may finish a cluster, so follow the chain before the
             * next iteration instead of restarting at the old cluster. */
            if (done < len && cluster_offset + part == g_storage.cluster_bytes) {
                uint32_t next;
                if (nofat) {
                    if (!exfat_cluster_valid(cluster + 1u)) return -5;
                    cluster = cluster + 1u;
                } else {
                    int fat_ret = exfat_read_fat(cluster, &next);
                    if (fat_ret < 0) return fat_ret;
                    if (next < 2u || next >= EXFAT_EOC || !exfat_cluster_valid(next)) {
                        return -5;
                    }
                    cluster = next;
                }
            }
            continue;
        }
        while (take >= SECTOR_SIZE) {
            /* Keep each filesystem transaction within the transport's proven
             * AHCI request size.  The larger staging buffer remains useful,
             * while the generic block layer handles the request atomically. */
            uint32_t sectors = min_u32(
                min_u32(take / SECTOR_SIZE, STORAGE_WRITE_MAX_SECTORS),
                (uint32_t)(sizeof(storage_cluster_buf) / SECTOR_SIZE));
            /* As with reads, stage writes so a user virtual buffer is never
             * interpreted as a device physical address. */
            storage_memcpy(storage_cluster_buf, src + done, sectors * SECTOR_SIZE);
            ret = exfat_write_sectors_resilient(lba, sectors, storage_cluster_buf);
            if (ret < 0) {
                console_printf("[ntclks] exfat stream bulk write failed first=%u nofat=%u lba=%llu sectors=%u done=%u ret=%d\n",
                               first, nofat, (unsigned long long)lba, sectors, done, ret);
                return ret;
            }
            lba += sectors;
            done += sectors * SECTOR_SIZE;
            take -= sectors * SECTOR_SIZE;
        }
        if (take) {
            ret = exfat_read_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) {
                console_printf("[ntclks] exfat stream tail read failed first=%u nofat=%u lba=%llu ret=%d\n",
                               first, nofat, (unsigned long long)lba, ret);
                return ret;
            }
            storage_memcpy(storage_scratch, src + done, take);
            ret = exfat_write_sectors_resilient(lba, 1u, storage_scratch);
            if (ret < 0) {
                console_printf("[ntclks] exfat stream tail write failed first=%u nofat=%u lba=%llu ret=%d\n",
                               first, nofat, (unsigned long long)lba, ret);
                return ret;
            }
            done += take;
        }
        if (done < len) {
            uint32_t next;
            if (nofat) {
                if (!exfat_cluster_valid(cluster + 1u)) return -5;
                cluster = cluster + 1u;
            } else {
                int fat_ret = exfat_read_fat(cluster, &next);
                if (fat_ret < 0) return fat_ret;
                if (next < 2u || next >= EXFAT_EOC || !exfat_cluster_valid(next)) {
                    return -5;
                }
            }
            if (!nofat) cluster = next;
        }
    }
    return 0;
}

static int exfat_bitmap_cache_load(uint64_t byte_offset)
{
    uint64_t aligned;
    uint64_t end;
    uint32_t bytes;
    if (byte_offset >= g_storage.exfat_bitmap_length) return -5;
    if (exfat_bitmap_cache_valid && exfat_bitmap_cache_volume == g_active_volume &&
        byte_offset >= exfat_bitmap_cache_offset &&
        byte_offset < exfat_bitmap_cache_offset + exfat_bitmap_cache_bytes) return 0;
    if (exfat_bitmap_cache_dirty) {
        int ret = exfat_bitmap_flush();
        if (ret < 0) return ret;
    }
    aligned = (byte_offset / SECTOR_SIZE) * SECTOR_SIZE;
    end = min_u64(g_storage.exfat_bitmap_length,
                  aligned + EXFAT_META_CACHE_SECTORS * SECTOR_SIZE);
    bytes = (uint32_t)(end - aligned);
    {
        int ret = exfat_read_stream(g_storage.exfat_bitmap_cluster, g_storage.exfat_bitmap_nofat,
                                    aligned, exfat_bitmap_cache_data, bytes);
        if (ret < 0) {
            console_printf("[ntclks] exfat bitmap cache read failed volume=%u offset=%llu bytes=%u ret=%d\n",
                           g_storage.volume_id, (unsigned long long)aligned, bytes, ret);
            exfat_bitmap_cache_valid = 0;
            exfat_bitmap_cache_volume = 0;
            return ret;
        }
    }
    exfat_bitmap_cache_offset = aligned;
    exfat_bitmap_cache_bytes = bytes;
    exfat_bitmap_cache_volume = g_active_volume;
    exfat_bitmap_cache_valid = 1;
    exfat_bitmap_cache_dirty = 0;
    return 0;
}

static int exfat_bitmap_flush(void)
{
    if (!exfat_bitmap_cache_valid || !exfat_bitmap_cache_dirty) return 0;
    {
        int ret = exfat_write_stream(g_storage.exfat_bitmap_cluster, g_storage.exfat_bitmap_nofat,
                                     exfat_bitmap_cache_offset, exfat_bitmap_cache_data,
                                     exfat_bitmap_cache_bytes);
        if (ret < 0) {
            console_printf("[ntclks] exfat bitmap flush failed volume=%u offset=%llu bytes=%u ret=%d\n",
                           g_storage.volume_id,
                           (unsigned long long)exfat_bitmap_cache_offset,
                           exfat_bitmap_cache_bytes, ret);
            return ret;
        }
    }
    exfat_bitmap_cache_dirty = 0;
    return 0;
}

static int exfat_bitmap_get_cached(uint32_t cluster, uint8_t *allocated)
{
    uint64_t bit;
    uint64_t byte;
    if (!allocated || !exfat_cluster_valid(cluster)) return -22;
    bit = cluster - 2u;
    byte = bit / 8u;
    if (byte >= g_storage.exfat_bitmap_length) return -5;
    {
        int ret = exfat_bitmap_cache_load(byte);
        if (ret < 0) return ret;
    }
    *allocated = (exfat_bitmap_cache_data[byte - exfat_bitmap_cache_offset] >>
                  (bit & 7u)) & 1u;
    return 0;
}

static int exfat_bitmap_set_cached(uint32_t cluster, uint8_t allocated)
{
    uint64_t bit;
    uint64_t byte;
    uint8_t *value;
    if (!exfat_cluster_valid(cluster)) return -22;
    bit = cluster - 2u;
    byte = bit / 8u;
    if (byte >= g_storage.exfat_bitmap_length) return -5;
    {
        int ret = exfat_bitmap_cache_load(byte);
        if (ret < 0) return ret;
    }
    value = &exfat_bitmap_cache_data[byte - exfat_bitmap_cache_offset];
    if (allocated) *value |= (uint8_t)(1u << (bit & 7u));
    else *value &= (uint8_t)~(1u << (bit & 7u));
    exfat_bitmap_cache_dirty = 1;
    return 0;
}

static int exfat_read_bitmap_bit(uint32_t cluster, uint8_t *allocated)
{
    return exfat_bitmap_get_cached(cluster, allocated);
}

static int exfat_load_upcase_table(void)
{
    uint16_t *table;
    uint64_t offset = 0;
    uint32_t codepoint = 0;
    uint32_t checksum = 0;
    uint32_t raw_length;
    if (g_storage.volume_id >= STORAGE_MAX_VOLUMES || !g_storage.exfat_upcase_length ||
        g_storage.exfat_upcase_length > sizeof(storage_exfat_upcase_data) ||
        (g_storage.exfat_upcase_length & 1u) != 0u ||
        !exfat_cluster_valid(g_storage.exfat_upcase_cluster)) {
        console_printf("[ntclks] exfat upcase parameters invalid volume=%u cluster=%u length=%llu count=%u\n",
                       g_storage.volume_id, g_storage.exfat_upcase_cluster,
                       (unsigned long long)g_storage.exfat_upcase_length,
                       g_storage.exfat_cluster_count);
        return -22;
    }
    table = storage_exfat_upcase_cache[g_storage.volume_id];
    raw_length = (uint32_t)g_storage.exfat_upcase_length;
    {
        int ret = exfat_read_stream(g_storage.exfat_upcase_cluster, g_storage.exfat_upcase_nofat,
                                    0, storage_exfat_upcase_data, raw_length);
        if (ret < 0) {
        console_printf("[ntclks] exfat upcase read failed offset=0 length=%u first=%u nofat=%u\n",
                       raw_length, g_storage.exfat_upcase_cluster,
                       g_storage.exfat_upcase_nofat);
            return ret;
        }
    }
    for (uint32_t i = 0; i < raw_length; ++i) {
        checksum = (checksum << 31) | (checksum >> 1);
        checksum += storage_exfat_upcase_data[i];
    }
    if (checksum != g_storage.exfat_upcase_checksum) {
        console_printf("[ntclks] exfat upcase checksum mismatch calculated=%u stored=%u first=%u,%u,%u,%u nofat=%u\n",
                       checksum, g_storage.exfat_upcase_checksum,
                       storage_exfat_upcase_data[0], storage_exfat_upcase_data[1],
                       storage_exfat_upcase_data[2], storage_exfat_upcase_data[3],
                       g_storage.exfat_upcase_nofat);
        return -5;
    }
    offset = 0;
    while (offset + 2u <= g_storage.exfat_upcase_length && codepoint < 65536u) {
        uint16_t value;
        value = exfat_get_u16(storage_exfat_upcase_data + offset);
        offset += 2u;
        /* 0xFFFF is the compressed-run marker only when another UTF-16
         * count follows it.  The standard table legitimately ends with a
         * literal U+FFFF mapping, so accept a terminal marker as one value. */
        if (value != 0xffffu || offset >= g_storage.exfat_upcase_length) {
            table[codepoint++] = value;
            continue;
        }
        if (offset + 2u > g_storage.exfat_upcase_length) {
            return -5;
        }
        value = exfat_get_u16(storage_exfat_upcase_data + offset);
        offset += 2u;
        if (value == 0u || value > 65536u - codepoint) {
            console_printf("[ntclks] exfat upcase decode run invalid value=%u codepoint=%u\n",
                           value, codepoint);
            return -5;
        }
        while (value--) table[codepoint] = (uint16_t)codepoint, ++codepoint;
    }
    if (codepoint != 65536u) {
        console_printf("[ntclks] exfat upcase decode incomplete codepoint=%u offset=%llu\n",
                       codepoint, (unsigned long long)offset);
        return -5;
    }
    storage_exfat_upcase_ready[g_storage.volume_id] = 1u;
    return 0;
}

static uint16_t exfat_upcase(uint16_t value)
{
    if (g_storage.volume_id < STORAGE_MAX_VOLUMES &&
        storage_exfat_upcase_ready[g_storage.volume_id]) {
        return storage_exfat_upcase_cache[g_storage.volume_id][value];
    }
    return value >= 'a' && value <= 'z' ? (uint16_t)(value - 'a' + 'A') : value;
}

static uint16_t exfat_name_hash(const uint16_t *name, uint32_t length)
{
    uint16_t hash = 0;
    for (uint32_t i = 0; i < length; ++i) {
        uint16_t value = exfat_upcase(name[i]);
        uint8_t lo = (uint8_t)value;
        uint8_t hi = (uint8_t)(value >> 8);
        hash = (uint16_t)((hash << 15) | (hash >> 1));
        hash = (uint16_t)(hash + lo);
        hash = (uint16_t)((hash << 15) | (hash >> 1));
        hash = (uint16_t)(hash + hi);
    }
    return hash;
}

static int exfat_utf8_name(const char *name, uint16_t out[255], uint32_t *out_length)
{
    struct leonos_unicode_utf8_to_utf16 command;
    uint32_t length;
    if (!name || !name[0] || !out || !out_length) return -22;
    length = (uint32_t)storage_strlen(name);
    if (length >= LEONOS_FS_NAME_LEN || storage_text_eq(name, ".") ||
        storage_text_eq(name, "..")) return -22;
    for (uint32_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20u || name[i] == '"' || name[i] == '*' || name[i] == '/' ||
            name[i] == ':' || name[i] == '<' || name[i] == '>' || name[i] == '?' ||
            name[i] == '\\' || name[i] == '|') return -22;
    }
    command.utf8 = name;
    command.utf8_len = length;
    command.utf16 = out;
    command.utf16_capacity = 255u;
    command.utf16_len = 0;
    if (osmlayer_unicode_utf8_to_utf16le(&command) < 0 || command.utf16_len == 0 ||
        command.utf16_len > 255u) return -22;
    *out_length = command.utf16_len;
    return 0;
}

static int exfat_name_equals(const uint16_t *name, uint32_t length, const char *other)
{
    uint16_t other16[255];
    uint32_t other_length;
    if (exfat_utf8_name(other, other16, &other_length) < 0 || other_length != length) return 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (exfat_upcase(name[i]) != exfat_upcase(other16[i])) return 0;
    }
    return 1;
}

static int exfat_dir_cache_load(uint32_t directory_cluster, uint8_t nofat,
                                uint64_t offset)
{
    if (!g_storage.cluster_bytes || (g_storage.cluster_bytes % SECTOR_SIZE) != 0u) {
        return -22;
    }
    uint64_t window = min_u64(sizeof(exfat_dir_cache_data), g_storage.cluster_bytes);
    uint64_t aligned = (offset / window) * window;
    uint64_t cluster_offset = aligned % g_storage.cluster_bytes;
    uint32_t bytes = (uint32_t)min_u64(sizeof(exfat_dir_cache_data),
                                       g_storage.cluster_bytes - cluster_offset);
    uint64_t cluster_index = g_storage.cluster_bytes ?
                             aligned / g_storage.cluster_bytes : 0u;
    uint32_t physical_cluster = 0;
    uint64_t lba = 0;
    int ret;
    if (!exfat_cluster_valid(directory_cluster) || !bytes) return -22;
    ret = exfat_cluster_at(directory_cluster, nofat, cluster_index, &physical_cluster);
    if (ret < 0) {
        exfat_dir_cache_valid = 0;
        console_printf("[ntclks] exfat directory cache cluster lookup failed volume=%u dir=%u nofat=%u offset=%llu index=%llu ret=%d\n",
                       g_storage.volume_id, directory_cluster, nofat,
                       (unsigned long long)aligned,
                       (unsigned long long)cluster_index, ret);
        return ret;
    }
    lba = exfat_cluster_lba(physical_cluster) + cluster_offset / SECTOR_SIZE;
    ret = exfat_read_stream(directory_cluster, nofat, aligned,
                            exfat_dir_cache_data, bytes);
    if (ret < 0) {
        exfat_dir_cache_valid = 0;
        exfat_dir_cache_volume = 0;
        exfat_dir_cache_bytes = 0;
        console_printf("[ntclks] exfat directory cache load failed volume=%u dir=%u nofat=%u index=%llu lba=%llu bytes=%u sectors=%u ret=%d\n",
                       g_storage.volume_id, directory_cluster, nofat,
                       (unsigned long long)cluster_index,
                       (unsigned long long)lba, bytes,
                       (unsigned)((bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE), ret);
        return ret;
    }
    exfat_dir_cache_volume = g_active_volume;
    exfat_dir_cache_cluster = directory_cluster;
    exfat_dir_cache_nofat = nofat;
    exfat_dir_cache_offset = aligned;
    exfat_dir_cache_bytes = bytes;
    exfat_dir_cache_valid = 1u;
    return 0;
}

static int exfat_dir_read_entry(uint32_t directory_cluster, uint8_t nofat,
                                uint32_t index, uint8_t entry[EXFAT_ENTRY_SIZE])
{
    uint64_t offset = (uint64_t)index * EXFAT_ENTRY_SIZE;
    int ret;
    if (!entry || index > UINT32_MAX / EXFAT_ENTRY_SIZE) return -22;
    if (!exfat_dir_cache_valid || exfat_dir_cache_volume != g_active_volume ||
        exfat_dir_cache_cluster != directory_cluster || exfat_dir_cache_nofat != nofat ||
        offset < exfat_dir_cache_offset ||
        offset + EXFAT_ENTRY_SIZE > exfat_dir_cache_offset + exfat_dir_cache_bytes) {
        ret = exfat_dir_cache_load(directory_cluster, nofat, offset);
        if (ret < 0) {
            console_printf("[ntclks] exfat directory entry read failed cluster=%u nofat=%u index=%u ret=%d\n",
                           directory_cluster, nofat, index, ret);
            return ret;
        }
    }
    storage_memcpy(entry, exfat_dir_cache_data + (offset - exfat_dir_cache_offset),
                   EXFAT_ENTRY_SIZE);
    return 0;
}

static int exfat_dir_write_entry(uint32_t directory_cluster, uint8_t nofat,
                                 uint32_t index, const uint8_t entry[EXFAT_ENTRY_SIZE])
{
    uint64_t offset = (uint64_t)index * EXFAT_ENTRY_SIZE;
    int ret;
    if (!entry || index > UINT32_MAX / EXFAT_ENTRY_SIZE) return -22;
    if (!exfat_dir_cache_valid || exfat_dir_cache_volume != g_active_volume ||
        exfat_dir_cache_cluster != directory_cluster || exfat_dir_cache_nofat != nofat ||
        offset < exfat_dir_cache_offset ||
        offset + EXFAT_ENTRY_SIZE > exfat_dir_cache_offset + exfat_dir_cache_bytes) {
        ret = exfat_dir_cache_load(directory_cluster, nofat, offset);
        if (ret < 0) {
            console_printf("[ntclks] exfat directory entry load failed cluster=%u nofat=%u index=%u ret=%d\n",
                           directory_cluster, nofat, index, ret);
            return ret;
        }
    }
    storage_memcpy(exfat_dir_cache_data + (offset - exfat_dir_cache_offset),
                   entry, EXFAT_ENTRY_SIZE);
    ret = exfat_write_stream(directory_cluster, nofat, offset, entry, EXFAT_ENTRY_SIZE);
    if (ret < 0) {
        console_printf("[ntclks] exfat directory entry write failed cluster=%u nofat=%u index=%u ret=%d\n",
                       directory_cluster, nofat, index, ret);
        exfat_dir_cache_valid = 0;
    }
    return ret;
}

/* Merge a committed sector back into the directory window when it is still
 * resident. This prevents a later lookup from resurrecting stale entries. */
static void exfat_dir_cache_merge_sector(uint32_t directory_cluster, uint8_t nofat,
                                         uint64_t sector_offset,
                                         const uint8_t sector[SECTOR_SIZE])
{
    uint64_t sector_end;
    uint64_t overlap_start;
    uint64_t overlap_end;
    if (!sector || !exfat_dir_cache_valid || exfat_dir_cache_volume != g_active_volume ||
        exfat_dir_cache_cluster != directory_cluster || exfat_dir_cache_nofat != nofat) {
        return;
    }
    sector_end = sector_offset + SECTOR_SIZE;
    overlap_start = sector_offset > exfat_dir_cache_offset ?
                    sector_offset : exfat_dir_cache_offset;
    overlap_end = sector_end < exfat_dir_cache_offset + exfat_dir_cache_bytes ?
                  sector_end : exfat_dir_cache_offset + exfat_dir_cache_bytes;
    if (overlap_end > overlap_start) {
        storage_memcpy(exfat_dir_cache_data + (overlap_start - exfat_dir_cache_offset),
                       sector + (overlap_start - sector_offset),
                       (size_t)(overlap_end - overlap_start));
    }
}

static int exfat_read_file_set(uint32_t directory_cluster, uint8_t nofat, uint32_t index,
                               uint8_t entries[20][EXFAT_ENTRY_SIZE], uint8_t *out_count,
                               uint16_t name[255], uint32_t *out_name_length,
                               struct storage_node *out_node)
{
    uint8_t count;
    uint32_t name_length;
    uint32_t name_pos = 0;
    struct leonos_unicode_utf16_to_utf8 convert;
    char rendered[LEONOS_FS_NAME_LEN];
    int ret;
    if (!entries || !out_count || !name || !out_name_length || !out_node) return -22;
    ret = exfat_dir_read_entry(directory_cluster, nofat, index, entries[0]);
    if (ret < 0) return ret;
    if (entries[0][0] != EXFAT_ENTRY_FILE) return -2;
    count = entries[0][1];
    if (count < 2u || count > 19u) return -5;
    for (uint32_t i = 1; i <= count; ++i) {
        ret = exfat_dir_read_entry(directory_cluster, nofat, index + i, entries[i]);
        if (ret < 0) return ret;
    }
    if (exfat_checksum(&entries[0][0], count + 1u) != exfat_get_u16(entries[0] + 2u) ||
        entries[1][0] != EXFAT_ENTRY_STREAM || (entries[1][1] & (uint8_t)~EXFAT_STREAM_NO_FAT_CHAIN)) {
        return -5;
    }
    name_length = entries[1][3];
    if (name_length == 0u || name_length > 255u || count != 1u + (name_length + 14u) / 15u) return -5;
    for (uint32_t i = 2; i <= count; ++i) {
        if (entries[i][0] != EXFAT_ENTRY_NAME) return -5;
        for (uint32_t j = 0; j < 15u && name_pos < name_length; ++j) {
            name[name_pos++] = exfat_get_u16(entries[i] + 2u + j * 2u);
        }
    }
    if (name_pos != name_length) return -5;
    if (exfat_name_hash(name, name_length) != exfat_get_u16(entries[1] + 4u)) return -5;
    convert.utf16 = name;
    convert.utf16_len = name_length;
    convert.utf8 = rendered;
    convert.utf8_capacity = sizeof(rendered);
    convert.utf8_len = 0;
    if (osmlayer_unicode_utf16le_to_utf8(&convert) < 0 || !rendered[0]) return -5;
    storage_memzero(out_node, sizeof(*out_node));
    out_node->type = (exfat_get_u16(entries[0] + 4u) & EXFAT_ATTR_DIRECTORY)
                         ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
    out_node->flags = STORAGE_NODE_FLAG_EXFAT |
                      ((entries[1][1] & EXFAT_STREAM_NO_FAT_CHAIN)
                           ? STORAGE_NODE_FLAG_EXFAT_NOFAT : 0u);
    out_node->first_cluster = storage_get_u32(entries[1] + 20u);
    out_node->volume_id = g_storage.volume_id;
    out_node->size = exfat_get_u64(entries[1] + 24u);
    {
        uint64_t valid_length = exfat_get_u64(entries[1] + 8u);
        uint64_t needed = out_node->size
            ? (out_node->size + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes : 0u;
        if (valid_length > out_node->size ||
            (out_node->size && (!exfat_cluster_valid(out_node->first_cluster) ||
                                needed > g_storage.exfat_cluster_count)) ||
            (out_node->type == LEONOS_FS_TYPE_DIR &&
             (!exfat_cluster_valid(out_node->first_cluster) ||
              out_node->size < g_storage.cluster_bytes))) {
            return -5;
        }
        if (out_node->size && (entries[1][1] & EXFAT_STREAM_NO_FAT_CHAIN) &&
            (out_node->first_cluster > UINT32_MAX - (uint32_t)(needed - 1u) ||
             !exfat_cluster_valid(out_node->first_cluster + (uint32_t)(needed - 1u)))) {
            return -5;
        }
    }
    *out_count = count;
    *out_name_length = name_length;
    return 0;
}

static uint64_t exfat_dir_entry_limit(void)
{
    uint64_t limit = (uint64_t)g_storage.exfat_cluster_count * g_storage.cluster_bytes /
                     EXFAT_ENTRY_SIZE;
    return limit > UINT32_MAX ? UINT32_MAX : limit;
}

static int exfat_find_in_dir_ref(uint32_t directory_cluster, uint8_t nofat, const char *wanted,
                                 struct storage_node *out, struct exfat_dir_ref *out_ref)
{
    uint8_t raw[20][EXFAT_ENTRY_SIZE];
    uint16_t name[255];
    uint8_t count;
    uint32_t length;
    uint64_t limit;
    if (!wanted || !exfat_cluster_valid(directory_cluster)) return -22;
    /* Callers that will rewrite an entry need its physical entry-set
     * reference, so they must bypass the node-only directory cache. */
    if (!out_ref && storage_dir_index_lookup(directory_cluster, wanted, out)) return 0;
    limit = exfat_dir_entry_limit();
    for (uint64_t pos = 0; pos < limit; ) {
        uint8_t first[EXFAT_ENTRY_SIZE];
        int ret = exfat_dir_read_entry(directory_cluster, nofat, (uint32_t)pos, first);
        if (ret < 0) {
            console_printf("[ntclks] exfat lookup failed dir=%u nofat=%u name=%s index=%u ret=%d\n",
                           directory_cluster, nofat, wanted, (uint32_t)pos, ret);
            return ret;
        }
        if (first[0] == 0u) return -2;
        if (first[0] != EXFAT_ENTRY_FILE) {
            ++pos;
            continue;
        }
        {
            struct storage_node node;
            ret = exfat_read_file_set(directory_cluster, nofat, (uint32_t)pos, raw, &count,
                                      name, &length, &node);
            if (ret < 0) {
                console_printf("[ntclks] exfat lookup entry-set invalid dir=%u nofat=%u name=%s index=%u ret=%d\n",
                               directory_cluster, nofat, wanted, (uint32_t)pos, ret);
                return ret;
            }
            if (exfat_name_equals(name, length, wanted)) {
                if (out) *out = node;
                if (out_ref) {
                    out_ref->directory_cluster = directory_cluster;
                    out_ref->directory_nofat = nofat;
                    out_ref->first_entry = (uint32_t)pos;
                    out_ref->secondary_count = count;
                }
                storage_dir_index_store(directory_cluster, wanted, &node);
                return 0;
            }
            pos += count + 1u;
        }
    }
    /* A complete scan without a matching entry is the normal ENOENT case.
     * Callers use -2 to distinguish it from media or metadata corruption and
     * to decide whether O_CREAT should create a new entry-set. */
    return -2;
}

static int exfat_find_in_dir(uint32_t directory_cluster, uint8_t nofat, const char *wanted,
                             struct storage_node *out)
{
    return exfat_find_in_dir_ref(directory_cluster, nofat, wanted, out, 0);
}

/* Return the file-entry reference for the final component as well as its
 * node.  Root has no file entry set, which is represented by a null ref. */
static int exfat_lookup_path_ref(const char *path, struct storage_node *out,
                                 struct exfat_dir_ref *out_ref)
{
    struct storage_node node;
    struct exfat_dir_ref matched_ref;
    char name[LEONOS_FS_NAME_LEN];
    uint32_t length = 0;
    const char *cursor;
    if (!path || path[0] != '/') return -22;
    storage_memzero(&node, sizeof(node));
    node.type = LEONOS_FS_TYPE_DIR;
    node.flags = STORAGE_NODE_FLAG_ROOT | STORAGE_NODE_FLAG_EXFAT;
    node.first_cluster = g_storage.exfat_root_cluster;
    node.volume_id = g_storage.volume_id;
    if (out_ref) storage_memzero(out_ref, sizeof(*out_ref));
    cursor = path + 1;
    while (*cursor) {
        if (*cursor == '/') {
            if (length) {
                name[length] = 0;
                int ret;
                if (node.type != LEONOS_FS_TYPE_DIR) return -20;
                ret = exfat_find_in_dir_ref(node.first_cluster,
                                            (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                            name, &node, &matched_ref);
                if (ret < 0) return ret;
                if (out_ref) *out_ref = matched_ref;
                length = 0;
            }
        } else if (length + 1u < sizeof(name)) {
            name[length++] = *cursor;
        } else return -22;
        ++cursor;
    }
    if (length) {
        name[length] = 0;
        int ret;
        if (node.type != LEONOS_FS_TYPE_DIR) return -20;
        ret = exfat_find_in_dir_ref(node.first_cluster,
                                    (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                    name, &node, &matched_ref);
        if (ret < 0) return ret;
        if (out_ref) *out_ref = matched_ref;
    }
    if (out) *out = node;
    return 0;
}

static int exfat_lookup_path(const char *path, struct storage_node *out)
{
    return exfat_lookup_path_ref(path, out, 0);
}

static int exfat_read_node(const struct storage_node *node, uint64_t offset,
                           void *buffer, uint32_t len, uint32_t *out_read)
{
    if (out_read) *out_read = 0;
    if (!node || !buffer || node->type != LEONOS_FS_TYPE_FILE ||
        node->volume_id != g_storage.volume_id) return -22;
    if (offset >= node->size || len == 0) return 0;
    if (len > node->size - offset) len = (uint32_t)(node->size - offset);
    {
        int ret = exfat_read_stream(node->first_cluster,
                                    (node->flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                    offset, buffer, len);
        if (ret < 0) return ret;
    }
    if (out_read) *out_read = len;
    return 0;
}

static int exfat_zero_stream_range(uint32_t first, uint8_t nofat,
                                   uint64_t offset, uint64_t length)
{
    /* Use a dedicated zero buffer rather than storage_scratch: the stream
     * writer uses storage_scratch for read-modify-write, so passing the same
     * buffer would corrupt a partial-sector zero fill. */
    storage_memzero(exfat_zero_data, sizeof(exfat_zero_data));
    while (length) {
        uint32_t chunk = (uint32_t)min_u64(length, sizeof(exfat_zero_data));
        int ret = exfat_write_stream(first, nofat, offset, exfat_zero_data, chunk);
        if (ret < 0) return ret;
        offset += chunk;
        length -= chunk;
    }
    return 0;
}

static int exfat_write_contiguous_fat(uint32_t first, uint32_t count, uint32_t next_after)
{
    if (!count || !exfat_cluster_valid(first) ||
        first > UINT32_MAX - (count - 1u) ||
        !exfat_cluster_valid(first + count - 1u)) return -22;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t next = i + 1u < count ? first + i + 1u : next_after;
        int ret = exfat_write_fat(first + i, next);
        if (ret < 0) return ret;
    }
    return 0;
}

static int exfat_iter_dir_entry(uint32_t directory_cluster, uint8_t nofat, uint64_t wanted,
                                struct leonos_dir_entry *out)
{
    uint8_t raw[20][EXFAT_ENTRY_SIZE];
    uint16_t name[255];
    uint8_t count;
    uint32_t length;
    uint64_t ordinal = 0;
    uint64_t limit = exfat_dir_entry_limit();
    if (!out || !exfat_cluster_valid(directory_cluster)) return -22;
    for (uint64_t pos = 0; pos < limit; ) {
        uint8_t first[EXFAT_ENTRY_SIZE];
        struct storage_node node;
        struct leonos_unicode_utf16_to_utf8 convert;
        int ret = exfat_dir_read_entry(directory_cluster, nofat, (uint32_t)pos, first);
        if (ret < 0) return ret;
        if (first[0] == 0u) return -2;
        if (first[0] != EXFAT_ENTRY_FILE) {
            ++pos;
            continue;
        }
        ret = exfat_read_file_set(directory_cluster, nofat, (uint32_t)pos, raw, &count,
                                  name, &length, &node);
        if (ret < 0) return ret;
        convert.utf16 = name;
        convert.utf16_len = length;
        convert.utf8 = out->name;
        convert.utf8_capacity = sizeof(out->name);
        convert.utf8_len = 0;
        if (osmlayer_unicode_utf16le_to_utf8(&convert) < 0) return -5;
        if (!storage_is_acl_metadata_name(out->name)) {
            if (ordinal == wanted) {
                out->type = node.type;
                return 0;
            }
            ++ordinal;
        }
        pos += count + 1u;
    }
    return -2;
}

static int exfat_find_free_span(uint32_t directory_cluster, uint8_t nofat,
                                const struct exfat_dir_ref *self_ref,
                                uint32_t needed, uint32_t *out_index)
{
    uint64_t limit = exfat_dir_entry_limit();
    if (!out_index || needed == 0 || !exfat_cluster_valid(directory_cluster)) return -22;
    for (;;) {
        uint32_t run = 0;
        uint8_t grew = 0;
        for (uint64_t pos = 0; pos < limit; ++pos) {
            uint8_t entry[EXFAT_ENTRY_SIZE];
            int ret = exfat_dir_read_entry(directory_cluster, nofat, (uint32_t)pos, entry);
            if (ret < 0) {
                console_printf("[ntclks] exfat find-free-span read failed dir=%u nofat=%u needed=%u pos=%u ret=%d\n",
                               directory_cluster, nofat, needed, (uint32_t)pos, ret);
                return ret;
            }
            if ((entry[0] & 0x80u) != 0u) {
                run = 0;
                continue;
            }
            ++run;
            if (run == needed) {
                *out_index = (uint32_t)(pos + 1u - needed);
                return 0;
            }
            if (entry[0] == 0u) {
                /* Entries after the end marker are free, but may only be used
                 * inside the currently allocated cluster. Grow explicitly if
                 * the complete entry-set would cross that boundary. */
                uint32_t candidate = (uint32_t)(pos + 1u - run);
                uint32_t per_cluster = g_storage.cluster_bytes / EXFAT_ENTRY_SIZE;
                uint32_t in_cluster = per_cluster ? candidate % per_cluster : 0u;
                if (per_cluster && needed <= per_cluster - in_cluster) {
                    *out_index = candidate;
                    return 0;
                }
                if (nofat) return -28;
                ret = exfat_grow_directory(directory_cluster, nofat, self_ref);
                if (ret < 0) {
                    console_printf("[ntclks] exfat find-free-span grow after end failed dir=%u needed=%u pos=%u ret=%d\n",
                                   directory_cluster, needed, (uint32_t)pos, ret);
                    return ret;
                }
                grew = 1;
                break;
            }
        }
        if (grew) continue;
        console_printf("[ntclks] exfat find-free-span exhausted dir=%u needed=%u\n",
                       directory_cluster, needed);
        return -28;
    }
}

static int exfat_zero_clusters(uint32_t first, uint8_t nofat, uint32_t count)
{
    storage_memzero(storage_scratch, sizeof(storage_scratch));
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cluster;
        int ret = exfat_cluster_at(first, nofat, i, &cluster);
        if (ret < 0) return ret;
        for (uint32_t sector = 0; sector < g_storage.sectors_per_cluster; ) {
            uint32_t take = min_u32(g_storage.sectors_per_cluster - sector, STORAGE_SCRATCH_SECTORS);
            ret = exfat_write_sectors_resilient(exfat_cluster_lba(cluster) + sector,
                                                take, storage_scratch);
            if (ret < 0) return ret;
            sector += take;
        }
    }
    return 0;
}

static int exfat_allocate_clusters(uint32_t count, uint32_t *out_first, uint8_t *out_nofat)
{
    /* Cluster numbers are 2..cluster_count+1 inclusive.  Keep the upper
     * bound in this form, but never probe the sentinel beyond the heap. */
    uint32_t max = g_storage.exfat_cluster_count + 1u;
    uint32_t start = g_storage.exfat_next_free_cluster;
    uint32_t found = 0;
    if (!out_first || !out_nofat || count == 0 || count > FAT32_MAX_FILE_CLUSTERS) return -22;
    if (start < 2u || start > max) start = 2u;
    /* Complete a previous metadata transaction before beginning a new scan. */
    {
        int ret = exfat_bitmap_flush();
        if (ret < 0) return ret;
    }
    /* Prefer a contiguous extent so normal writes retain the exFAT NoFatChain
     * fast path. The second pass gives fragmented volumes a usable FAT chain. */
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        uint32_t begin = pass ? 2u : start;
        uint32_t end = pass ? (start > 2u ? start - 1u : 1u) : max;
        uint32_t run = 0;
        for (uint32_t cluster = begin; cluster <= end; ++cluster) {
            if (!exfat_cluster_valid(cluster)) break;
            uint8_t allocated = 1;
            {
                int ret = exfat_read_bitmap_bit(cluster, &allocated);
                if (ret < 0) return ret;
            }
            if (!allocated) {
                if (++run == count) {
                    found = cluster + 1u - count;
                    goto contiguous;
                }
            } else run = 0;
        }
    }
contiguous:
    if (found) {
        for (uint32_t i = 0; i < count; ++i) {
            {
                int ret = exfat_bitmap_set_cached(found + i, 1u);
                if (ret < 0) return ret;
            }
        }
        {
            int ret = exfat_bitmap_flush();
            if (ret < 0) return ret;
        }
        g_storage.exfat_next_free_cluster = found + count <= max ? found + count : 2u;
        *out_first = found;
        *out_nofat = 1u;
        return 0;
    }
    /* Fragmented fallback. Existing FAT pages are updated before an entry set
     * names them, so a crash can leak space but cannot expose a corrupt file. */
    for (uint32_t cluster = 2u; cluster <= max && found < count; ++cluster) {
        if (!exfat_cluster_valid(cluster)) break;
        uint8_t allocated = 1;
        {
            int ret = exfat_read_bitmap_bit(cluster, &allocated);
            if (ret < 0) return ret;
        }
        if (!allocated) storage_new_chain[found++] = cluster;
    }
    if (found != count) return -28;
    for (uint32_t i = 0; i < count; ++i) {
        {
            int ret = exfat_bitmap_set_cached(storage_new_chain[i], 1u);
            if (ret < 0) return ret;
            ret = exfat_write_fat(storage_new_chain[i],
                                  i + 1u < count ? storage_new_chain[i + 1u] : EXFAT_EOC);
            if (ret < 0) return ret;
        }
    }
    {
        int ret = exfat_bitmap_flush();
        if (ret < 0) return ret;
    }
    *out_first = storage_new_chain[0];
    *out_nofat = 0u;
    return 0;
}

static int exfat_free_clusters(uint32_t first, uint8_t nofat, uint64_t bytes)
{
    uint64_t count;
    if (!first || !bytes) return 0;
    if (!exfat_cluster_valid(first)) return -5;
    count = (bytes + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes;
    if (count > g_storage.exfat_cluster_count ||
        count > FAT32_MAX_FILE_CLUSTERS) return -5;
    {
        int ret = exfat_bitmap_flush();
        if (ret < 0) return ret;
    }
    /* Snapshot the complete chain before changing either the bitmap or FAT.
     * Looking up index N from the first cluster after clearing FAT[N-1]
     * traverses a chain that we have already destroyed; that used to make
     * truncating/deleting any multi-cluster file fail after its first cluster
     * and leave the remaining clusters allocated.  A later append could then
     * reuse only the first cluster and create a malformed (sometimes looping)
     * chain, which is particularly visible with large WAD files. */
    for (uint64_t i = 0u; i < count; ++i) {
        uint32_t cluster;
        if (nofat) {
            if (first > UINT32_MAX - (uint32_t)i ||
                !exfat_cluster_valid(first + (uint32_t)i)) {
                return -5;
            }
            cluster = first + (uint32_t)i;
        } else {
            uint32_t next = EXFAT_EOC;
            if (i == 0u) {
                cluster = first;
            } else {
                cluster = storage_new_chain[i - 1u];
                if (exfat_read_fat(cluster, &next) < 0 ||
                    next < 2u || next >= EXFAT_EOC ||
                    !exfat_cluster_valid(next)) {
                    return -5;
                }
                cluster = next;
            }
        }
        for (uint64_t prior = 0u; prior < i; ++prior) {
            if (storage_new_chain[prior] == cluster) return -5;
        }
        storage_new_chain[i] = cluster;
        if (!nofat && i + 1u < count) {
            uint32_t next;
            if (exfat_read_fat(cluster, &next) < 0 ||
                next < 2u || next >= EXFAT_EOC ||
                !exfat_cluster_valid(next)) {
                return -5;
            }
        }
    }
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t cluster = storage_new_chain[i];
        int ret = exfat_bitmap_set_cached(cluster, 0u);
        if (ret < 0) return ret;
        if (!nofat) {
            ret = exfat_write_fat(cluster, 0u);
            if (ret < 0) return ret;
        }
    }
    {
        int ret = exfat_bitmap_flush();
        if (ret < 0) return ret;
    }
    if (first < g_storage.exfat_next_free_cluster) g_storage.exfat_next_free_cluster = first;
    return 0;
}

static int exfat_write_file_set(const struct exfat_dir_ref *ref, uint8_t entries[20][EXFAT_ENTRY_SIZE])
{
    uint8_t total;
    if (!ref || !entries || ref->secondary_count < 2u || ref->secondary_count > 19u) return -22;
    total = ref->secondary_count + 1u;
    exfat_put_u16(entries[0] + 2u, exfat_checksum(&entries[0][0], total));
    for (uint32_t i = 0; i < total; ) {
        uint64_t stream_offset = (uint64_t)(ref->first_entry + i) * EXFAT_ENTRY_SIZE;
        uint32_t sector_offset = (uint32_t)(stream_offset % SECTOR_SIZE);
        uint32_t run = min_u32(total - i, (SECTOR_SIZE - sector_offset) / EXFAT_ENTRY_SIZE);
        uint64_t sector_base = stream_offset - sector_offset;
        int ret;
        if (!run) return -22;
        /* Read the complete sector before changing any 32-byte entries. This
         * preserves unrelated entries and commits all entries in this sector
         * atomically from the filesystem's perspective. */
        ret = exfat_read_stream(ref->directory_cluster, ref->directory_nofat,
                                sector_base, storage_scratch, SECTOR_SIZE);
        if (ret < 0) {
            exfat_dir_cache_valid = 0;
            console_printf("[ntclks] exfat entry-set read-modify-read failed dir=%u nofat=%u index=%u lba-offset=%llu entries=%u ret=%d\n",
                           ref->directory_cluster, ref->directory_nofat,
                           ref->first_entry + i, (unsigned long long)sector_base, run, ret);
            return ret;
        }
        for (uint32_t j = 0; j < run; ++j) {
            storage_memcpy(storage_scratch + sector_offset + j * EXFAT_ENTRY_SIZE,
                           entries[i + j], EXFAT_ENTRY_SIZE);
        }
        ret = exfat_write_stream(ref->directory_cluster, ref->directory_nofat,
                                 sector_base, storage_scratch, SECTOR_SIZE);
        if (ret < 0) {
            exfat_dir_cache_valid = 0;
            console_printf("[ntclks] exfat entry-set sector write failed dir=%u nofat=%u index=%u offset=%llu entries=%u ret=%d\n",
                           ref->directory_cluster, ref->directory_nofat,
                           ref->first_entry + i, (unsigned long long)sector_base, run, ret);
            return ret;
        }
        exfat_dir_cache_merge_sector(ref->directory_cluster, ref->directory_nofat,
                                     sector_base, storage_scratch);
        i += run;
    }
    return 0;
}

static int exfat_create_entry(uint32_t directory_cluster, uint8_t directory_nofat,
                              const struct exfat_dir_ref *self_ref, const char *name,
                              uint8_t directory, uint32_t first_cluster, uint64_t size,
                              uint8_t nofat, struct exfat_dir_ref *out_ref)
{
    uint8_t entries[20][EXFAT_ENTRY_SIZE];
    uint16_t utf16[255];
    uint32_t length;
    uint32_t names;
    uint32_t index;
    struct exfat_dir_ref ref;
    int ret = exfat_utf8_name(name, utf16, &length);
    if (ret < 0) return ret;
    names = (length + 14u) / 15u;
    if (names + 2u > 20u) return -22;
    ret = exfat_find_free_span(directory_cluster, directory_nofat, self_ref,
                               names + 2u, &index);
    if (ret < 0) {
        console_printf("[ntclks] exfat create entry free-span failed dir=%u name=%s needed=%u ret=%d\n",
                       directory_cluster, name, names + 2u, ret);
        return ret;
    }
    storage_memzero(entries, sizeof(entries));
    entries[0][0] = EXFAT_ENTRY_FILE;
    entries[0][1] = (uint8_t)(names + 1u);
    exfat_put_u16(entries[0] + 4u, directory ? EXFAT_ATTR_DIRECTORY : 0u);
    entries[1][0] = EXFAT_ENTRY_STREAM;
    entries[1][1] = nofat ? EXFAT_STREAM_NO_FAT_CHAIN : 0u;
    entries[1][3] = (uint8_t)length;
    exfat_put_u16(entries[1] + 4u, exfat_name_hash(utf16, length));
    exfat_put_u64(entries[1] + 8u, size);
    storage_put_u32(entries[1] + 20u, first_cluster);
    exfat_put_u64(entries[1] + 24u, size);
    for (uint32_t n = 0; n < names; ++n) {
        entries[n + 2u][0] = EXFAT_ENTRY_NAME;
        for (uint32_t i = 0; i < 15u && n * 15u + i < length; ++i) {
            exfat_put_u16(entries[n + 2u] + 2u + i * 2u, utf16[n * 15u + i]);
        }
    }
    ref.directory_cluster = directory_cluster;
    ref.directory_nofat = directory_nofat;
    ref.first_entry = index;
    ref.secondary_count = (uint8_t)(names + 1u);
    ret = exfat_write_file_set(&ref, entries);
    if (ret < 0) {
        console_printf("[ntclks] exfat create entry failed dir=%u name=%s index=%u entries=%u ret=%d\n",
                       directory_cluster, name, index, names + 2u, ret);
    } else {
        console_printf("[ntclks] exfat create entry committed dir=%u name=%s index=%u entries=%u\n",
                       directory_cluster, name, index, names + 2u);
    }
    if (ret == 0 && out_ref) *out_ref = ref;
    return ret;
}

static int exfat_update_entry_data(const struct exfat_dir_ref *ref, uint32_t first_cluster,
                                   uint64_t size, uint8_t nofat)
{
    uint8_t entries[20][EXFAT_ENTRY_SIZE];
    uint16_t name[255];
    uint8_t count;
    uint32_t length;
    struct storage_node node;
    int ret = exfat_read_file_set(ref->directory_cluster, ref->directory_nofat, ref->first_entry,
                                  entries, &count, name, &length, &node);
    if (ret < 0 || count != ref->secondary_count) return ret < 0 ? ret : -5;
    entries[1][1] = nofat ? EXFAT_STREAM_NO_FAT_CHAIN : 0u;
    exfat_put_u64(entries[1] + 8u, size);
    storage_put_u32(entries[1] + 20u, first_cluster);
    exfat_put_u64(entries[1] + 24u, size);
    return exfat_write_file_set(ref, entries);
}

/* Directories created by LeonOS always use FAT chains.  Extending one must
 * update the owning stream extension too; otherwise standard fsck tools see
 * the new cluster as an orphaned allocation. */
static int exfat_grow_directory(uint32_t directory_cluster, uint8_t directory_nofat,
                                const struct exfat_dir_ref *self_ref)
{
    uint32_t tail = directory_cluster;
    uint32_t clusters = 1u;
    uint32_t next;
    uint32_t new_cluster;
    uint8_t new_nofat;
    int ret;
    if (directory_nofat || !exfat_cluster_valid(directory_cluster)) return -28;
    while (clusters <= g_storage.exfat_cluster_count) {
        ret = exfat_read_fat(tail, &next);
        if (ret < 0) return ret;
        if (next >= EXFAT_EOC) break;
        if (!exfat_cluster_valid(next)) return -5;
        tail = next;
        ++clusters;
    }
    if (clusters > g_storage.exfat_cluster_count) return -5;
    ret = exfat_allocate_clusters(1u, &new_cluster, &new_nofat);
    if (ret < 0) return ret;
    ret = exfat_write_fat(new_cluster, EXFAT_EOC);
    if (ret < 0) {
        (void)exfat_free_clusters(new_cluster, new_nofat, g_storage.cluster_bytes);
        console_printf("[ntclks] exfat directory grow stage=link-new cluster=%u ret=%d\n",
                       new_cluster, ret);
        return ret;
    }
    ret = exfat_zero_clusters(new_cluster, new_nofat, 1u);
    if (ret < 0) {
        (void)exfat_free_clusters(new_cluster, new_nofat, g_storage.cluster_bytes);
        console_printf("[ntclks] exfat directory grow stage=zero cluster=%u ret=%d\n",
                       new_cluster, ret);
        return ret;
    }
    ret = exfat_write_fat(tail, new_cluster);
    if (ret < 0) {
        (void)exfat_free_clusters(new_cluster, new_nofat, g_storage.cluster_bytes);
        console_printf("[ntclks] exfat directory grow stage=link-tail tail=%u new=%u ret=%d\n",
                       tail, new_cluster, ret);
        return ret;
    }
    if (self_ref) {
        ret = exfat_update_entry_data(self_ref, directory_cluster,
                                      (uint64_t)(clusters + 1u) * g_storage.cluster_bytes, 0u);
        if (ret < 0) return ret;
    }
    return 0;
}

static int exfat_write_file(const char *path, const void *buffer, uint32_t len)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent;
    struct storage_node old;
    struct exfat_dir_ref parent_ref;
    struct exfat_dir_ref old_ref;
    uint32_t first = 0;
    uint8_t nofat = 0;
    uint8_t existing;
    uint32_t clusters = len ? (len + g_storage.cluster_bytes - 1u) / g_storage.cluster_bytes : 0;
    int ret;
    if ((!buffer && len) || !path) return -22;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = exfat_lookup_path_ref(parent_path, &parent, &parent_ref);
    if (ret < 0) {
        console_printf("[ntclks] exfat write file parent lookup failed path=%s parent=%s ret=%d\n",
                       path, parent_path, ret);
        return ret;
    }
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_find_in_dir_ref(parent.first_cluster,
                                (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                name, &old, &old_ref);
    if (ret != 0 && ret != -2) {
        console_printf("[ntclks] exfat write file lookup failed path=%s parent=%s name=%s ret=%d\n",
                       path, parent_path, name, ret);
    }
    if (ret == 0 && old.type != LEONOS_FS_TYPE_FILE) return -21;
    if (ret != 0 && ret != -2) return ret;
    existing = ret == 0;
    storage_begin_mutation();
    if (clusters) {
        ret = exfat_allocate_clusters(clusters, &first, &nofat);
        if (ret < 0) return ret;
        /* The file payload covers almost every sector. Writing a full zeroed
         * extent first doubled installer I/O; only clear the invisible tail
         * after the payload so later in-place growth cannot expose stale data. */
        ret = exfat_write_stream(first, nofat, 0, buffer, len);
        if (ret < 0 ||
            ((uint64_t)clusters * g_storage.cluster_bytes > len &&
             (ret = exfat_zero_stream_range(first, nofat, len,
                                            (uint64_t)clusters * g_storage.cluster_bytes - len)) < 0)) {
            (void)exfat_free_clusters(first, nofat, len);
            console_printf("[ntclks] exfat write file payload failed path=%s first=%u nofat=%u len=%u ret=%d\n",
                           path, first, nofat, len, ret);
            return ret;
        }
    }
    if (existing) {
        ret = exfat_update_entry_data(&old_ref, first, len, nofat);
        if (ret == 0) ret = exfat_free_clusters(old.first_cluster,
                                                 (old.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                                 old.size);
    } else {
        ret = exfat_create_entry(parent.first_cluster,
                                 (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                 (parent.flags & STORAGE_NODE_FLAG_ROOT) ? 0 : &parent_ref,
                                 name, 0u, first, len, nofat, 0);
    }
    if (ret < 0 && first) (void)exfat_free_clusters(first, nofat, len);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

static int exfat_write_node_path(const char *path, uint64_t offset,
                                 const void *buffer, uint32_t len, uint32_t *out_written)
{
    struct storage_node node;
    struct exfat_dir_ref ref;
    uint64_t length;
    uint32_t old_clusters;
    uint32_t new_clusters;
    uint32_t old_tail = 0;
    uint32_t new_first = 0;
    uint32_t add_clusters;
    uint32_t new_tail = 0;
    uint8_t old_nofat;
    uint8_t new_nofat;
    uint8_t final_nofat;
    uint8_t cached_append;
    int ret;
    if (out_written) *out_written = 0;
    if (!path || (!buffer && len)) return -22;
    ret = exfat_lookup_path_ref(path, &node, &ref);
    if (ret < 0 || node.type != LEONOS_FS_TYPE_FILE) return ret < 0 ? ret : -21;
    length = offset + len;
    if (length < offset || length > 0xffffffffULL || node.size > 0xffffffffULL) return -28;
    if (length < node.size) length = node.size;
    if (!len) return 0;
    old_nofat = (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0;
    old_clusters = node.size ? (uint32_t)((node.size + g_storage.cluster_bytes - 1u) /
                                          g_storage.cluster_bytes) : 0u;
    new_clusters = length ? (uint32_t)((length + g_storage.cluster_bytes - 1u) /
                                       g_storage.cluster_bytes) : 0u;
    if (new_clusters > FAT32_MAX_FILE_CLUSTERS) return -28;

    storage_begin_mutation();
    cached_append = offset == node.size && node.first_cluster >= 2u &&
                    storage_write_chain_cache.valid &&
                    storage_write_chain_cache.volume == g_active_volume &&
                    storage_write_chain_cache.first_cluster == node.first_cluster &&
                    storage_write_chain_cache.size == (uint32_t)node.size &&
                    storage_text_eq(storage_write_chain_cache.path, path) &&
                    storage_write_chain_cache.count == old_clusters;
    if (old_clusters && cached_append) {
        old_tail = storage_write_chain_cache.tail;
    } else if (old_clusters) {
        if (exfat_cluster_at(node.first_cluster, old_nofat, old_clusters - 1u, &old_tail) < 0) {
            return -5;
        }
    }

    final_nofat = old_nofat;
    new_first = node.first_cluster;
    if (new_clusters > old_clusters) {
        add_clusters = new_clusters - old_clusters;
        ret = exfat_allocate_clusters(add_clusters, &new_first, &new_nofat);
        if (ret < 0) {
            console_printf("[ntclks] exfat write path=%s stage=allocate old_clusters=%u new_clusters=%u count=%u ret=%d\n",
                           path, old_clusters, new_clusters, add_clusters, ret);
            return ret;
        }
        if (!old_clusters) {
            final_nofat = new_nofat;
        } else if (old_nofat && new_nofat &&
                   node.first_cluster <= UINT32_MAX - old_clusters &&
                   new_first == node.first_cluster + old_clusters) {
            /* The allocator found the physical continuation of a contiguous
             * NoFatChain extent, so no FAT conversion is needed. */
            new_first = node.first_cluster;
            final_nofat = 1u;
        } else {
            /* A fragmented append must become a normal FAT chain. Existing
             * contiguous clusters and a newly contiguous extent are both
             * materialized only once, at the point where the representation
             * changes. */
            if (old_nofat) {
                ret = exfat_write_contiguous_fat(node.first_cluster, old_clusters, new_first);
            } else {
                ret = exfat_write_fat(old_tail, new_first);
            }
            if (ret < 0) {
                console_printf("[ntclks] exfat write path=%s stage=link-old-chain first=%u tail=%u new=%u ret=%d\n",
                               path, node.first_cluster, old_tail, new_first, ret);
                return ret;
            }
            if (new_nofat) {
                ret = exfat_write_contiguous_fat(new_first, add_clusters, EXFAT_EOC);
                if (ret < 0) {
                    console_printf("[ntclks] exfat write path=%s stage=link-new-chain first=%u count=%u ret=%d\n",
                                   path, new_first, add_clusters, ret);
                    return ret;
                }
            }
            final_nofat = 0u;
            new_first = node.first_cluster;
        }
    }
    if (!new_first && new_clusters) return -5;

    /* A write past EOF creates a zero-filled hole. Newly allocated tail bytes
     * are also cleared so unwritten portions never expose stale disk data. */
    if (offset > node.size &&
        (ret = exfat_zero_stream_range(new_first, final_nofat, node.size,
                                       offset - node.size)) < 0) {
        console_printf("[ntclks] exfat write path=%s stage=hole-zero first=%u nofat=%u offset=%llu length=%llu ret=-5\n",
                       path, new_first, final_nofat, (unsigned long long)node.size,
                       (unsigned long long)(offset - node.size));
        return ret;
    }
    ret = exfat_write_stream(new_first, final_nofat, offset, buffer, len);
    if (ret < 0) {
        console_printf("[ntclks] exfat write path=%s stage=data first=%u nofat=%u offset=%llu length=%u ret=%d\n",
                       path, new_first, final_nofat, (unsigned long long)offset, len, ret);
        return ret;
    }
    if (new_clusters > old_clusters) {
        uint64_t data_end = offset + len;
        uint64_t allocated_bytes = (uint64_t)new_clusters * g_storage.cluster_bytes;
        if (data_end < allocated_bytes &&
            (ret = exfat_zero_stream_range(new_first, final_nofat, data_end,
                                           allocated_bytes - data_end)) < 0) {
            console_printf("[ntclks] exfat write path=%s stage=tail-zero first=%u nofat=%u offset=%llu length=%llu ret=%d\n",
                           path, new_first, final_nofat, (unsigned long long)data_end,
                           (unsigned long long)(allocated_bytes - data_end), ret);
            return ret;
        }
    }
    ret = exfat_update_entry_data(&ref, new_first, length, final_nofat);
    if (ret < 0) {
        console_printf("[ntclks] exfat write path=%s stage=dir-update first=%u nofat=%u size=%llu ret=%d\n",
                       path, new_first, final_nofat, (unsigned long long)length, ret);
        return ret;
    }
    if (new_clusters) {
        if (final_nofat) {
            if (new_first > UINT32_MAX - (new_clusters - 1u)) return -5;
            new_tail = new_first + new_clusters - 1u;
        } else if (exfat_cluster_at(new_first, 0u, new_clusters - 1u, &new_tail) < 0) {
            return -5;
        }
    }
    storage_cache_invalidate();
    storage_write_chain_cache.volume = g_active_volume;
    storage_write_chain_cache.first_cluster = new_first;
    storage_write_chain_cache.size = (uint32_t)length;
    storage_write_chain_cache.count = new_clusters;
    storage_write_chain_cache.tail = new_tail;
    storage_copy_text(storage_write_chain_cache.path,
                      sizeof(storage_write_chain_cache.path), path);
    storage_write_chain_cache.valid = 1u;
    if (!final_nofat && new_clusters) {
        exfat_chain_hint.volume = g_active_volume;
        exfat_chain_hint.first = new_first;
        exfat_chain_hint.cluster = new_tail;
        exfat_chain_hint.index = new_clusters - 1u;
        exfat_chain_hint.nofat = 0u;
        exfat_chain_hint.valid = 1u;
    }
    if (out_written) *out_written = len;
    return 0;
}

static int exfat_truncate_file(const char *path, uint64_t length)
{
    struct storage_node node;
    struct exfat_dir_ref ref;
    uint32_t old_clusters;
    uint32_t new_clusters;
    uint8_t nofat;
    int ret;
    if (!path || length > 0xffffffffULL) return -22;
    ret = exfat_lookup_path_ref(path, &node, &ref);
    if (ret < 0 || node.type != LEONOS_FS_TYPE_FILE) return ret < 0 ? ret : -21;
    if (length == node.size) return 0;
    old_clusters = node.size ? (uint32_t)((node.size + g_storage.cluster_bytes - 1u) /
                                          g_storage.cluster_bytes) : 0u;
    new_clusters = length ? (uint32_t)((length + g_storage.cluster_bytes - 1u) /
                                       g_storage.cluster_bytes) : 0u;
    if (new_clusters > FAT32_MAX_FILE_CLUSTERS) return -28;
    nofat = (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0;
    if (length > node.size) {
        uint64_t remaining = length - node.size;
        uint64_t offset = node.size;
        storage_memzero(exfat_zero_data, sizeof(exfat_zero_data));
        while (remaining) {
            uint32_t chunk = (uint32_t)min_u64(remaining, sizeof(exfat_zero_data));
            uint32_t written = 0;
            ret = exfat_write_node_path(path, offset, exfat_zero_data, chunk, &written);
            if (ret < 0 || written != chunk) return ret < 0 ? ret : -5;
            offset += written;
            remaining -= written;
        }
        return 0;
    }

    storage_begin_mutation();
    if (new_clusters == 0u) {
        /* Hide the old chain from the namespace before returning its space. */
        ret = exfat_update_entry_data(&ref, 0u, 0u, 0u);
        if (ret < 0) return ret;
        ret = exfat_free_clusters(node.first_cluster, nofat, node.size);
    } else if (new_clusters < old_clusters) {
        uint32_t tail;
        uint32_t first = node.first_cluster;
        if (!exfat_cluster_valid(first)) return -5;
        if (nofat) {
            if (first > UINT32_MAX - new_clusters) return -5;
            tail = first + new_clusters;
            ret = exfat_update_entry_data(&ref, first, length, 1u);
            if (ret == 0) ret = exfat_free_clusters(tail, 1u,
                                                     (uint64_t)(old_clusters - new_clusters) *
                                                         g_storage.cluster_bytes);
        } else {
            uint32_t next;
            ret = exfat_cluster_at(first, 0u, new_clusters - 1u, &tail);
            if (ret < 0) return ret;
            ret = exfat_read_fat(tail, &next);
            if (ret < 0) return ret;
            if (next < 2u || next >= EXFAT_EOC || !exfat_cluster_valid(next)) return -5;
            ret = exfat_update_entry_data(&ref, first, length, 0u);
            if (ret == 0 && exfat_write_fat(tail, EXFAT_EOC) < 0) ret = -5;
            if (ret == 0) ret = exfat_free_clusters(next, 0u,
                                                     (uint64_t)(old_clusters - new_clusters) *
                                                         g_storage.cluster_bytes);
        }
    } else {
        /* Only the logical file size changes; the final cluster remains part
         * of the stream and its existing bytes are intentionally preserved. */
        ret = exfat_update_entry_data(&ref, node.first_cluster, length, nofat);
    }
    if (ret == 0) {
        storage_cache_invalidate();
        storage_write_chain_cache.valid = 0;
    }
    return ret;
}

static int exfat_mkdir(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent;
    struct storage_node existing;
    struct exfat_dir_ref parent_ref;
    uint32_t cluster;
    uint8_t nofat;
    int ret;
    if (!path) return -22;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = exfat_lookup_path_ref(parent_path, &parent, &parent_ref);
    if (ret < 0) return ret;
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_lookup_path(path, &existing);
    if (ret == 0) return -17;
    if (ret != -2) return ret;
    storage_begin_mutation();
    ret = exfat_allocate_clusters(1u, &cluster, &nofat);
    if (ret < 0) return ret;
    /* Directories are always FAT chained in LeonOS so that they can grow. */
    if (nofat) {
        ret = exfat_write_fat(cluster, EXFAT_EOC);
        if (ret < 0) {
            (void)exfat_free_clusters(cluster, nofat, g_storage.cluster_bytes);
            console_printf("[ntclks] exfat mkdir stage=link cluster=%u ret=%d\n", cluster, ret);
            return ret;
        }
    }
    ret = exfat_zero_clusters(cluster, 0u, 1u);
    if (ret < 0) {
        (void)exfat_free_clusters(cluster, 0u, g_storage.cluster_bytes);
        console_printf("[ntclks] exfat mkdir stage=zero cluster=%u ret=%d\n", cluster, ret);
        return ret;
    }
    ret = exfat_create_entry(parent.first_cluster,
                             (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                             (parent.flags & STORAGE_NODE_FLAG_ROOT) ? 0 : &parent_ref,
                             name, 1u, cluster, g_storage.cluster_bytes, 0u, 0);
    if (ret < 0) {
        (void)exfat_free_clusters(cluster, 0u, g_storage.cluster_bytes);
        console_printf("[ntclks] exfat mkdir stage=entry name=%s cluster=%u ret=%d\n",
                       name, cluster, ret);
        return ret;
    }
    storage_cache_invalidate();
    return 0;
}

static int exfat_delete_entry(const struct exfat_dir_ref *ref)
{
    uint8_t entry[EXFAT_ENTRY_SIZE];
    if (!ref) return -22;
    for (uint32_t i = 0; i <= ref->secondary_count; ++i) {
        int ret = exfat_dir_read_entry(ref->directory_cluster, ref->directory_nofat,
                                       ref->first_entry + i, entry);
        if (ret < 0) return ret;
        entry[0] &= 0x7fu;
        ret = exfat_dir_write_entry(ref->directory_cluster, ref->directory_nofat,
                                    ref->first_entry + i, entry);
        if (ret < 0) return ret;
    }
    return 0;
}

static int exfat_dir_is_empty(const struct storage_node *node)
{
    uint64_t limit;
    if (!node || node->type != LEONOS_FS_TYPE_DIR) return -22;
    limit = exfat_dir_entry_limit();
    for (uint64_t pos = 0; pos < limit; ++pos) {
        uint8_t entry[EXFAT_ENTRY_SIZE];
        int ret = exfat_dir_read_entry(node->first_cluster,
                                       (node->flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                       (uint32_t)pos, entry);
        if (ret < 0) return ret;
        if (entry[0] == 0u) return 1;
        if (entry[0] == EXFAT_ENTRY_FILE) return 0;
    }
    return -5;
}

static int exfat_unlink(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent, node;
    struct exfat_dir_ref ref;
    int ret;
    if (!path) return -22;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = exfat_lookup_path(parent_path, &parent);
    if (ret < 0) return ret;
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_find_in_dir_ref(parent.first_cluster,
                                (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                name, &node, &ref);
    if (ret < 0) return ret;
    if (node.type != LEONOS_FS_TYPE_FILE) return -21;
    storage_begin_mutation();
    ret = exfat_delete_entry(&ref);
    if (ret == 0) ret = exfat_free_clusters(node.first_cluster,
                                             (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                             node.size);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

static int exfat_rmdir(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent, node;
    struct exfat_dir_ref ref;
    int ret;
    if (!path) return -22;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = exfat_lookup_path(parent_path, &parent);
    if (ret < 0) return ret;
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_find_in_dir_ref(parent.first_cluster,
                                (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                name, &node, &ref);
    if (ret < 0) return ret;
    if (node.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_dir_is_empty(&node);
    if (ret <= 0) return ret < 0 ? ret : -39;
    storage_begin_mutation();
    ret = exfat_delete_entry(&ref);
    if (ret == 0) ret = exfat_free_clusters(node.first_cluster,
                                             (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                             node.size ? node.size : g_storage.cluster_bytes);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

static int exfat_rename(const char *old_path, const char *new_path)
{
    char old_parent_path[LEONOS_FS_PATH_LEN];
    char new_parent_path[LEONOS_FS_PATH_LEN];
    char old_name[LEONOS_FS_NAME_LEN];
    char new_name[LEONOS_FS_NAME_LEN];
    struct storage_node parent, node, existing;
    struct exfat_dir_ref parent_ref;
    struct exfat_dir_ref ref;
    int ret;
    if (!old_path || !new_path) return -22;
    ret = storage_parent_path(old_path, old_parent_path, sizeof(old_parent_path),
                              old_name, sizeof(old_name));
    if (ret < 0) return ret;
    ret = storage_parent_path(new_path, new_parent_path, sizeof(new_parent_path),
                              new_name, sizeof(new_name));
    if (ret < 0) return ret;
    if (!storage_text_eq_ci(old_parent_path, new_parent_path)) return -22;
    if (storage_text_eq_ci(old_name, new_name)) return 0;
    {
        uint16_t converted_name[255];
        uint32_t converted_length = 0;
        ret = exfat_utf8_name(new_name, converted_name, &converted_length);
        if (ret < 0) return ret;
    }
    ret = exfat_lookup_path_ref(old_parent_path, &parent, &parent_ref);
    if (ret < 0) return ret;
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = exfat_find_in_dir_ref(parent.first_cluster,
                                (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                                old_name, &node, &ref);
    if (ret < 0) return ret;
    ret = exfat_find_in_dir(parent.first_cluster,
                            (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0, new_name, &existing);
    if (ret == 0) return -17;
    if (ret != -2) return ret;
    storage_begin_mutation();
    ret = exfat_create_entry(parent.first_cluster, (parent.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0,
                             (parent.flags & STORAGE_NODE_FLAG_ROOT) ? 0 : &parent_ref,
                             new_name, node.type == LEONOS_FS_TYPE_DIR, node.first_cluster,
                             node.size, (node.flags & STORAGE_NODE_FLAG_EXFAT_NOFAT) != 0, 0);
    if (ret == 0) ret = exfat_delete_entry(&ref);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

static int exfat_mount(void)
{
    uint8_t boot[SECTOR_SIZE];
    uint8_t backup_boot[SECTOR_SIZE];
    uint64_t volume_length;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t heap_offset;
    uint8_t spc_shift;
    uint8_t bitmap_found = 0, upcase_found = 0, label_found = 0;
    uint64_t limit;
    int ret;
    if (!g_storage.exfat_start_lba || !g_storage.exfat_sector_count) return -2;
    /* A writable root must not be mounted from only one half of the exFAT
     * boot region. Validate both copies and require identical geometry; a
     * damaged backup is otherwise indistinguishable from a later metadata
     * update and can make recovery tools choose the wrong layout. */
    console_printf("[ntclks] exfat_mount: validating boot region at lba=%llu\n",
                   (unsigned long long)g_storage.exfat_start_lba);
    ret = exfat_boot_region_valid(g_storage.exfat_start_lba, boot);
    if (ret < 0) {
        console_printf("[ntclks] exfat_mount: primary boot region invalid ret=%d\n", ret);
        return ret;
    }
    ret = exfat_boot_region_valid(g_storage.exfat_start_lba + 12u, backup_boot);
    if (ret < 0) {
        console_printf("[ntclks] exfat_mount: backup boot region invalid ret=%d\n", ret);
        return ret;
    }
    for (uint32_t off = 64u; off < 112u; ++off) {
        if (boot[off] != backup_boot[off]) {
            console_printf("[ntclks] exfat_mount: boot/backup mismatch at offset %u\n", off);
            return -5;
        }
    }
    volume_length = exfat_get_u64(boot + 72u);
    fat_offset = storage_get_u32(boot + 80u);
    fat_length = storage_get_u32(boot + 84u);
    heap_offset = storage_get_u32(boot + 88u);
    cluster_count = storage_get_u32(boot + 92u);
    root_cluster = storage_get_u32(boot + 96u);
    spc_shift = boot[109u];
    if (boot[108u] != 9u || spc_shift > 8u || boot[110u] != 1u ||
        exfat_get_u16(boot + 104u) != 0x0100u || (boot[106u] & 1u) != 0u ||
        (boot[112u] != 0xffu && boot[112u] > 100u) ||
        volume_length == 0 || volume_length > g_storage.exfat_sector_count || fat_offset < 24u ||
        fat_length == 0 || heap_offset <= fat_offset ||
        (uint64_t)fat_offset + fat_length > heap_offset || cluster_count == 0 ||
        (uint64_t)heap_offset + ((uint64_t)cluster_count << spc_shift) > volume_length ||
        root_cluster < 2u || root_cluster - 2u >= cluster_count ||
        fat_length < (uint32_t)((((uint64_t)cluster_count + 2u) * 4u + SECTOR_SIZE - 1u) /
                                SECTOR_SIZE)) return -2;
    g_storage.bytes_per_sector = SECTOR_SIZE;
    g_storage.sectors_per_cluster = 1u << spc_shift;
    g_storage.cluster_bytes = SECTOR_SIZE << spc_shift;
    g_storage.exfat_fat_offset = fat_offset;
    g_storage.exfat_fat_length = fat_length;
    g_storage.exfat_cluster_heap_offset = heap_offset;
    g_storage.exfat_cluster_count = cluster_count;
    g_storage.exfat_root_cluster = root_cluster;
    g_storage.exfat_sectors_per_cluster_shift = spc_shift;
    g_storage.exfat_next_free_cluster = root_cluster + 1u;
    ret = exfat_read_sectors_resilient(g_storage.exfat_start_lba + fat_offset,
                                       1u, storage_scratch);
    if (ret < 0) {
        console_printf("[ntclks] exfat_mount: FAT reserved sector read failed ret=%d\n", ret);
        return ret;
    }
    if (storage_get_u32(storage_scratch) != 0xfffffff8u ||
        storage_get_u32(storage_scratch + 4u) != 0xffffffffu) {
        console_printf("[ntclks] exfat_mount: FAT reserved entries invalid\n");
        return -5;
    }
    ret = exfat_validate_fat_chain(root_cluster);
    if (ret < 0) {
        console_printf("[ntclks] exfat_mount: root FAT chain invalid cluster=%u\n", root_cluster);
        return ret;
    }
    limit = exfat_dir_entry_limit();
    for (uint64_t pos = 0; pos < limit; ++pos) {
        uint8_t entry[EXFAT_ENTRY_SIZE];
            ret = exfat_dir_read_entry(root_cluster, 0u, (uint32_t)pos, entry);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: root directory read failed index=%u ret=%d\n",
                               (uint32_t)pos, ret);
                return ret;
            }
        if (entry[0] == 0u) break;
        if (entry[0] == 0x83u) {
            if (label_found || entry[1] > 11u) { console_printf("[ntclks] exfat_mount: invalid volume label entry\n"); return -5; }
            for (uint32_t byte = 2u + entry[1] * 2u; byte < EXFAT_ENTRY_SIZE; ++byte) {
                if (entry[byte] != 0u) { console_printf("[ntclks] exfat_mount: volume label padding nonzero\n"); return -5; }
            }
            label_found = 1u;
        } else if (entry[0] == EXFAT_ENTRY_BITMAP && !bitmap_found) {
            uint32_t cluster = storage_get_u32(entry + 20u);
            uint64_t length = exfat_get_u64(entry + 24u);
            if (entry[1] & (uint8_t)~1u) { console_printf("[ntclks] exfat_mount: bitmap flags invalid=%x\n", entry[1]); return -5; }
            if (!exfat_cluster_valid(cluster) ||
                length < ((uint64_t)cluster_count + 7u) / 8u ||
                length > (uint64_t)cluster_count * g_storage.cluster_bytes) { console_printf("[ntclks] exfat_mount: bitmap geometry invalid cluster=%u length=%llu\n", cluster, (unsigned long long)length); return -5; }
            g_storage.exfat_bitmap_cluster = cluster;
            g_storage.exfat_bitmap_length = length;
            ret = exfat_system_stream_mode(cluster, length, &g_storage.exfat_bitmap_nofat);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: invalid allocation bitmap stream cluster=%u length=%llu\n",
                               cluster, (unsigned long long)length);
                return ret;
            }
            bitmap_found = 1;
        } else if (entry[0] == EXFAT_ENTRY_BITMAP) {
            return -5;
        } else if (entry[0] == EXFAT_ENTRY_UPCASE && !upcase_found) {
            uint32_t cluster = storage_get_u32(entry + 20u);
            uint64_t length = exfat_get_u64(entry + 24u);
            if (entry[1] != 0u || entry[2] != 0u || entry[3] != 0u) { console_printf("[ntclks] exfat_mount: upcase reserved fields invalid\n"); return -5; }
            if (!exfat_cluster_valid(cluster) || length < 2u ||
                length > (uint64_t)cluster_count * g_storage.cluster_bytes) { console_printf("[ntclks] exfat_mount: upcase geometry invalid cluster=%u length=%llu\n", cluster, (unsigned long long)length); return -5; }
            g_storage.exfat_upcase_checksum = storage_get_u32(entry + 4u);
            g_storage.exfat_upcase_cluster = cluster;
            g_storage.exfat_upcase_length = length;
            ret = exfat_system_stream_mode(cluster, length, &g_storage.exfat_upcase_nofat);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: invalid upcase stream cluster=%u length=%llu\n",
                               cluster, (unsigned long long)length);
                return ret;
            }
            upcase_found = 1;
        } else if (entry[0] == EXFAT_ENTRY_UPCASE) {
            console_printf("[ntclks] exfat_mount: duplicate upcase system entry\n"); return -5;
        }
    }
    if (!bitmap_found || !upcase_found) {
        console_printf("[ntclks] exfat_mount: missing system entries bitmap=%u upcase=%u\n",
                       bitmap_found, upcase_found);
        return -2;
    }
    {
        uint8_t allocated;
        if (exfat_read_bitmap_bit(root_cluster, &allocated) < 0 || !allocated) {
            console_printf("[ntclks] exfat_mount: root cluster not allocated cluster=%u\n", root_cluster);
            return -5;
        }
    }
    {
        uint64_t bitmap_clusters = (g_storage.exfat_bitmap_length + g_storage.cluster_bytes - 1u) /
                                   g_storage.cluster_bytes;
        uint64_t upcase_clusters = (g_storage.exfat_upcase_length + g_storage.cluster_bytes - 1u) /
                                   g_storage.cluster_bytes;
        uint64_t bitmap_bits = g_storage.exfat_bitmap_length * 8u;
        if (bitmap_clusters == 0u || upcase_clusters == 0u ||
            g_storage.exfat_bitmap_cluster == g_storage.exfat_root_cluster ||
            g_storage.exfat_upcase_cluster == g_storage.exfat_root_cluster ||
            g_storage.exfat_bitmap_cluster == g_storage.exfat_upcase_cluster ||
            (g_storage.exfat_bitmap_nofat &&
             g_storage.exfat_bitmap_cluster > UINT32_MAX - (uint32_t)(bitmap_clusters - 1u)) ||
            (g_storage.exfat_upcase_nofat &&
             g_storage.exfat_upcase_cluster > UINT32_MAX - (uint32_t)(upcase_clusters - 1u)) ||
            (g_storage.exfat_bitmap_nofat && g_storage.exfat_upcase_nofat &&
             g_storage.exfat_bitmap_cluster <= UINT32_MAX - (uint32_t)(bitmap_clusters - 1u) &&
             g_storage.exfat_upcase_cluster <= UINT32_MAX - (uint32_t)(upcase_clusters - 1u) &&
             g_storage.exfat_bitmap_cluster <=
                 g_storage.exfat_upcase_cluster + (uint32_t)(upcase_clusters - 1u) &&
            g_storage.exfat_upcase_cluster <=
                 g_storage.exfat_bitmap_cluster + (uint32_t)(bitmap_clusters - 1u))) {
            console_printf("[ntclks] exfat_mount: system stream geometry/alias invalid bitmap=%u upcase=%u\n",
                           g_storage.exfat_bitmap_cluster, g_storage.exfat_upcase_cluster);
            return -5;
        }
        /* System streams and the root directory must not alias one another;
         * aliasing would let a normal file write corrupt allocation metadata. */
        if (exfat_fat_chain_contains(root_cluster, g_storage.exfat_bitmap_cluster) == 1 ||
            exfat_fat_chain_contains(root_cluster, g_storage.exfat_upcase_cluster) == 1) {
            console_printf("[ntclks] exfat_mount: system stream aliases root directory\n");
            return -5;
        }
        for (uint64_t i = 0u; i < bitmap_clusters; ++i) {
            uint32_t bitmap_cluster;
            ret = exfat_cluster_at(g_storage.exfat_bitmap_cluster,
                                   g_storage.exfat_bitmap_nofat,
                                   i, &bitmap_cluster);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: bitmap cluster chain invalid index=%u ret=%d\n",
                               (uint32_t)i, ret);
                return ret;
            }
            if (exfat_stream_contains_cluster(g_storage.exfat_upcase_cluster,
                                              g_storage.exfat_upcase_nofat,
                                              g_storage.exfat_upcase_length,
                                              bitmap_cluster) == 1) {
                console_printf("[ntclks] exfat_mount: bitmap overlaps upcase cluster=%u\n", bitmap_cluster);
                return -5;
            }
        }
        for (uint64_t i = 0u; i < upcase_clusters; ++i) {
            uint32_t upcase_cluster;
            ret = exfat_cluster_at(g_storage.exfat_upcase_cluster,
                                   g_storage.exfat_upcase_nofat,
                                   i, &upcase_cluster);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: upcase cluster chain invalid index=%u ret=%d\n",
                               (uint32_t)i, ret);
                return ret;
            }
            {
                uint8_t allocated;
                ret = exfat_read_bitmap_bit(upcase_cluster, &allocated);
                if (ret < 0) {
                    console_printf("[ntclks] exfat_mount: upcase bitmap read failed cluster=%u ret=%d\n",
                                   upcase_cluster, ret);
                    return ret;
                }
                if (!allocated) {
                    console_printf("[ntclks] exfat_mount: upcase cluster unallocated cluster=%u\n", upcase_cluster);
                    return -5;
                }
            }
        }
        for (uint64_t i = 0u; i < bitmap_clusters; ++i) {
            uint32_t bitmap_cluster;
            ret = exfat_cluster_at(g_storage.exfat_bitmap_cluster,
                                   g_storage.exfat_bitmap_nofat,
                                   i, &bitmap_cluster);
            if (ret < 0) {
                console_printf("[ntclks] exfat_mount: bitmap cluster chain invalid (verify) index=%u ret=%d\n",
                               (uint32_t)i, ret);
                return ret;
            }
            {
                uint8_t allocated;
                ret = exfat_read_bitmap_bit(bitmap_cluster, &allocated);
                if (ret < 0) {
                    console_printf("[ntclks] exfat_mount: bitmap allocation read failed cluster=%u ret=%d\n",
                                   bitmap_cluster, ret);
                    return ret;
                }
                if (!allocated) {
                    console_printf("[ntclks] exfat_mount: bitmap cluster unallocated cluster=%u\n", bitmap_cluster);
                    return -5;
                }
            }
        }
        /* Bits past the end of the cluster heap are reserved and must be
         * clear.  This catches truncated or mis-sized allocation bitmaps. */
        if (bitmap_bits > (uint64_t)cluster_count) {
            for (uint64_t bit = cluster_count; bit < bitmap_bits; ++bit) {
                uint8_t value;
                ret = exfat_read_stream(g_storage.exfat_bitmap_cluster,
                                        g_storage.exfat_bitmap_nofat, bit / 8u,
                                        &value, 1u);
                if (ret < 0) return ret;
                if ((value & (uint8_t)(1u << (bit & 7u))) != 0u) {
                    console_printf("[ntclks] exfat_mount: bitmap has reserved bit=%llu set\n",
                                   (unsigned long long)bit);
                    return -5;
                }
            }
        }
    }
    storage_exfat_upcase_ready[g_storage.volume_id] = 0u;
    ret = exfat_load_upcase_table();
    if (ret < 0) {
        console_printf("[ntclks] exfat_mount: upcase table load/checksum failed ret=%d cluster=%u length=%llu\n",
                       ret,
                       g_storage.exfat_upcase_cluster,
                       (unsigned long long)g_storage.exfat_upcase_length);
        return ret;
    }
    g_storage.root_cluster = root_cluster;
    g_storage.filesystem = STORAGE_FILESYSTEM_EXFAT;
    return 0;
}
