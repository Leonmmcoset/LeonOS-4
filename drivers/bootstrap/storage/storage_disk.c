struct disk_gpt_table {
    struct gpt_header primary;
    struct gpt_header backup;
    uint32_t table_bytes;
    uint32_t table_sectors;
};

static int disk_gpt_load(struct install_disk_state *disk, uint64_t sector_count,
                         struct disk_gpt_table *out_table);
static int disk_gpt_entry_used(const struct gpt_entry *entry);
static int disk_gpt_entries_valid(const struct disk_gpt_table *table);
static int storage_disk_has_data_mount(uint32_t disk_id);

struct disk_block_partition_range {
    uint64_t first_lba;
    uint64_t sector_count;
    uint8_t present;
};

struct disk_block_partition_cache {
    uint64_t disk_sectors;
    struct disk_block_partition_range entries[LEONOS_DISK_MAX_PARTITIONS];
    int32_t status;
    uint8_t valid;
};

static struct disk_block_partition_cache disk_block_partition_cache[STORAGE_MAX_INSTALL_DISKS];

static void disk_block_cache_invalidate(uint32_t disk_id)
{
    if (disk_id < STORAGE_MAX_INSTALL_DISKS) {
        disk_block_partition_cache[disk_id].valid = 0;
    }
}

/* Disk IDs are assigned during every storage probe.  Never carry partition
 * ranges (or a previous probe's error) across that reassignment. */
void storage_disk_block_cache_reset(void)
{
    storage_memzero(disk_block_partition_cache,
                    sizeof(disk_block_partition_cache));
}

static void disk_block_cache_invalidate_disk(const struct install_disk_state *disk)
{
    if (!disk) return;
    for (uint32_t i = 0; i < g_install_disk_count; ++i) {
        if (&g_install_disks[i] == disk) {
            disk_block_cache_invalidate(i);
            return;
        }
    }
}

static int disk_block_cache_load(uint32_t disk_id, struct install_disk_state *disk,
                                 uint64_t sector_count)
{
    struct disk_block_partition_cache *cache;
    struct disk_gpt_table table;
    const struct gpt_entry *entries;
    int ret;
    if (disk_id >= STORAGE_MAX_INSTALL_DISKS || !disk) return -22;
    cache = &disk_block_partition_cache[disk_id];
    if (cache->valid && cache->disk_sectors == sector_count) {
        return cache->status;
    }
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0) {
        /* Only content errors are stable enough to cache. Transport failures
         * such as EIO may be transient (especially on AHCI after reset); a
         * cached EIO made every later /dev/diskNpM lookup fail until reboot. */
        if (ret == -2 || ret == -22) {
            storage_memzero(cache, sizeof(*cache));
            cache->disk_sectors = sector_count;
            cache->status = ret;
            cache->valid = 1;
        }
        return ret;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        /* Entry validation has completed from disk data, so this is a stable
         * malformed-GPT result rather than an in-flight transport failure. */
        storage_memzero(cache, sizeof(*cache));
        cache->disk_sectors = sector_count;
        cache->status = ret;
        cache->valid = 1;
        return ret;
    }
    entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
    storage_memzero(cache, sizeof(*cache));
    cache->disk_sectors = sector_count;
    cache->status = 0;
    for (uint32_t i = 0; i < table.primary.partition_entry_count; ++i) {
        if (!disk_gpt_entry_used(&entries[i])) continue;
        cache->entries[i].first_lba = entries[i].first_lba;
        cache->entries[i].sector_count = entries[i].last_lba - entries[i].first_lba + 1u;
        cache->entries[i].present = 1;
    }
    cache->valid = 1;
    return 0;
}

static int disk_block_range(uint32_t disk_id, struct install_disk_state *disk,
                            uint64_t sector_count, int32_t partition_index,
                            uint64_t *out_first_lba, uint64_t *out_sector_count)
{
    int ret;

    if (!disk || !out_first_lba || !out_sector_count) return -22;
    if (partition_index == -1) {
        *out_first_lba = 0;
        *out_sector_count = sector_count;
        return 0;
    }
    if (partition_index < -1 || (uint32_t)partition_index >= LEONOS_DISK_MAX_PARTITIONS) {
        return -22;
    }
    ret = disk_block_cache_load(disk_id, disk, sector_count);
    if (ret < 0) return ret;
    if (!disk_block_partition_cache[disk_id].entries[partition_index].present) return -2;
    *out_first_lba = disk_block_partition_cache[disk_id].entries[partition_index].first_lba;
    *out_sector_count = disk_block_partition_cache[disk_id].entries[partition_index].sector_count;
    return 0;
}

/**
 * @brief Checks whether a GPT GUID is all zeroes.
 * @param guid GPT type or unique GUID.
 * @return Nonzero when the GUID denotes an unused entry.
 */
static int disk_gpt_guid_empty(const uint8_t guid[16])
{
    if (!guid) {
        return 1;
    }
    for (uint32_t i = 0; i < 16; ++i) {
        if (guid[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Tests whether a GPT entry is allocated.
 * @param entry GPT partition entry to inspect.
 * @return Nonzero if the entry has a type GUID.
 */
static int disk_gpt_entry_used(const struct gpt_entry *entry)
{
    return entry && !disk_gpt_guid_empty(entry->type_guid);
}

/**
 * @brief Validates an on-disk GPT header before its table is read or modified.
 * @param header Header copied from the managed disk.
 * @param sector_count Total number of addressable disk sectors.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_header_valid(const struct gpt_header *header, uint64_t sector_count)
{
    struct gpt_header check;
    uint64_t table_bytes;
    uint64_t table_sectors;
    uint64_t table_last_lba;
    if (!header || sector_count < 2u ||
        header->signature != 0x5452415020494645ULL ||
        header->revision < 0x00010000u ||
        header->header_size != sizeof(struct gpt_header) ||
        header->reserved != 0 ||
        header->current_lba >= sector_count || header->backup_lba >= sector_count ||
        header->backup_lba == header->current_lba ||
        header->first_usable_lba > header->last_usable_lba ||
        header->last_usable_lba >= sector_count || header->first_usable_lba < 2u ||
        header->partition_entry_count == 0 ||
        header->partition_entry_count > LEONOS_DISK_MAX_PARTITIONS ||
        header->partition_entry_size != sizeof(struct gpt_entry)) {
        return -22;
    }
    table_bytes = (uint64_t)header->partition_entry_count * header->partition_entry_size;
    table_sectors = (table_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    if (table_bytes > sizeof(storage_cluster_buf) ||
        header->partition_entries_lba < 2u ||
        header->partition_entries_lba >= sector_count ||
        table_sectors > sector_count - header->partition_entries_lba ||
        table_sectors == 0) {
        return -22;
    }
    table_last_lba = header->partition_entries_lba + table_sectors - 1u;
    if ((header->current_lba >= header->first_usable_lba &&
         header->current_lba <= header->last_usable_lba) ||
        (header->backup_lba >= header->first_usable_lba &&
         header->backup_lba <= header->last_usable_lba) ||
        !(table_last_lba < header->first_usable_lba ||
          header->partition_entries_lba > header->last_usable_lba)) {
        return -22;
    }
    check = *header;
    check.header_crc32 = 0;
    return storage_crc32(&check, sizeof(check)) == header->header_crc32 ? 0 : -5;
}

/**
 * @brief Prepares a block disk for a synchronous disk-management operation.
 * @param disk_id Detected AHCI, IDE/PATA, or NVMe disk index.
 * @param out_disk Receives the selected block-disk state.
 * @param out_sector_count Receives the total disk sectors.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_manage_prepare(uint32_t disk_id, struct install_disk_state **out_disk,
                               uint64_t *out_sector_count)
{
    struct install_disk_state *disk;
    uint64_t sector_count;
    int ret;
    if (!out_disk || !out_sector_count || disk_id >= g_install_disk_count ||
        !g_install_disks[disk_id].present) {
        return -2;
    }
    disk = &g_install_disks[disk_id];
    ret = storage_prepare_install_disk(disk);
    if (ret < 0) {
        return ret;
    }
    sector_count = disk->sector_count;
    if (sector_count == 0) {
        ret = storage_install_identify(disk, &sector_count);
        if (ret < 0) {
            return ret;
        }
        disk->sector_count = sector_count;
    }
    *out_disk = disk;
    *out_sector_count = sector_count;
    return 0;
}

/**
 * @brief Reads and validates both GPT headers and the primary partition table.
 * @param disk Prepared block-disk state.
 * @param sector_count Total number of disk sectors.
 * @param out_table Receives validated GPT header metadata.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_load(struct install_disk_state *disk, uint64_t sector_count,
                         struct disk_gpt_table *out_table)
{
    struct gpt_header primary;
    struct gpt_header backup;
    uint32_t table_bytes;
    uint32_t table_sectors;
    int ret;
    if (!disk || !out_table) {
        return -22;
    }
    ret = install_read_sectors(disk, 1u, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memcpy(&primary, storage_scratch, sizeof(primary));
    if (primary.signature != 0x5452415020494645ULL) {
        return -2;
    }
    ret = disk_gpt_header_valid(&primary, sector_count);
    if (ret < 0 || primary.current_lba != 1u) {
        return ret < 0 ? ret : -22;
    }
    table_bytes = primary.partition_entry_count * primary.partition_entry_size;
    table_sectors = (table_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    ret = install_read_sectors(disk, primary.backup_lba, 1u, storage_scratch);
    if (ret == -LEONOS_EAGAIN) {
        /* The AHCI async path still owns the pending command.  Do not treat
         * an incomplete backup-header read as a missing header and submit a
         * second request against the shared command slot. */
        return ret;
    }
    if (ret < 0) {
        storage_memcpy(&backup, &primary, sizeof(backup));
        backup.current_lba = sector_count - 1u;
        backup.backup_lba = primary.current_lba;
        backup.partition_entries_lba = sector_count - table_sectors - 1u;
        backup.header_crc32 = 0;
    } else {
        storage_memcpy(&backup, storage_scratch, sizeof(backup));
    }
    ret = disk_gpt_header_valid(&backup, sector_count);
    if (ret < 0 || backup.current_lba != primary.backup_lba ||
        backup.backup_lba != primary.current_lba ||
        backup.partition_entry_count != primary.partition_entry_count ||
        backup.partition_entry_size != primary.partition_entry_size ||
        storage_memcmp(backup.disk_guid, primary.disk_guid,
                       sizeof(primary.disk_guid)) != 0 ||
        backup.first_usable_lba != primary.first_usable_lba ||
        backup.last_usable_lba != primary.last_usable_lba ||
        backup.partition_entries_crc32 != primary.partition_entries_crc32) {
        storage_memcpy(&backup, &primary, sizeof(backup));
        backup.current_lba = sector_count - 1u;
        backup.backup_lba = primary.current_lba;
        backup.partition_entries_lba = sector_count - table_sectors - 1u;
        backup.header_crc32 = 0;
    }
    ret = install_read_sectors(disk, primary.partition_entries_lba, table_sectors,
                               storage_cluster_buf);
    if (ret < 0) {
        return ret;
    }
    if (storage_crc32(storage_cluster_buf, table_bytes) != primary.partition_entries_crc32) {
        return -5;
    }
    out_table->primary = primary;
    out_table->backup = backup;
    out_table->table_bytes = table_bytes;
    out_table->table_sectors = table_sectors;
    return 0;
}

int storage_disk_block_info(uint32_t disk_id, int32_t partition_index,
                            uint64_t *out_first_lba, uint64_t *out_sector_count)
{
    struct install_disk_state *disk;
    uint64_t sector_count;
    int ret;
    if (!out_first_lba || !out_sector_count) return -22;
    *out_first_lba = 0;
    *out_sector_count = 0;
    ret = storage_acquire_task_io();
    if (ret < 0) return ret;
    ret = disk_manage_prepare(disk_id, &disk, &sector_count);
    if (ret < 0) return ret;
    return disk_block_range(disk_id, disk, sector_count, partition_index,
                            out_first_lba, out_sector_count);
}

int storage_disk_block_read(uint32_t disk_id, int32_t partition_index,
                            uint64_t offset, void *buffer, uint32_t length,
                            uint32_t *out_read)
{
    uint64_t first_lba, sectors;
    uint32_t count;
    uint32_t done = 0;
    uint8_t *destination = (uint8_t *)buffer;
    struct install_disk_state *disk;
    int ret;
    if (out_read) *out_read = 0;
    if (!buffer || (offset & 511u) || (length & 511u)) return -22;
    if (!length) return 0;
    ret = storage_acquire_task_io();
    if (ret < 0) return ret;
    ret = disk_manage_prepare(disk_id, &disk, &sectors);
    if (ret < 0) return ret;
    ret = disk_block_range(disk_id, disk, sectors, partition_index, &first_lba, &sectors);
    if (ret < 0) return ret;
    if (offset / 512u >= sectors || (uint64_t)(length / 512u) > sectors - offset / 512u) return -22;
    count = length / 512u;
    /* Block drivers program DMA with physical kernel addresses.  A raw
     * device syscall receives a user virtual address, so it must never be
     * used directly as a DMA target.  Transfer through the kernel-owned,
     * page-aligned scratch buffer in bounded chunks instead. */
    while (done < count) {
        uint32_t chunk = min_u32(STORAGE_SCRATCH_SECTORS, count - done);
        ret = install_read_sectors(disk, first_lba + offset / 512u + done,
                                   chunk, storage_scratch);
        if (ret < 0) return ret;
        storage_memcpy(destination + (uint64_t)done * SECTOR_SIZE,
                       storage_scratch, chunk * SECTOR_SIZE);
        done += chunk;
    }
    if (out_read) *out_read = length;
    return 0;
}

int storage_disk_block_write(uint32_t disk_id, int32_t partition_index,
                             uint64_t offset, const void *buffer, uint32_t length,
                             uint32_t *out_written)
{
    uint64_t first_lba, sectors;
    uint32_t count;
    uint32_t done = 0;
    const uint8_t *source = (const uint8_t *)buffer;
    struct install_disk_state *disk;
    int ret;
    if (out_written) *out_written = 0;
    if (!buffer || (offset & 511u) || (length & 511u)) return -22;
    if (!length) return 0;
    ret = storage_acquire_task_io();
    if (ret < 0) return ret;
    ret = disk_manage_prepare(disk_id, &disk, &sectors);
    if (ret < 0) return ret;
    ret = disk_block_range(disk_id, disk, sectors, partition_index, &first_lba, &sectors);
    if (ret < 0) return ret;
    if (offset / 512u >= sectors || (uint64_t)(length / 512u) > sectors - offset / 512u) return -22;
    /* The raw node is intentionally not a back door around mounted-volume
     * protection. Installer advanced mode may write an unmounted target; a
     * running system or mounted target must use filesystem operations. */
    if ((disk->boot_root && !storage_installer_root_active()) ||
        disk->target_mounted || storage_disk_has_data_mount(disk_id)) {
        return -16;
    }
    if (partition_index < 0) {
        /* A whole-disk write may modify either GPT copy even when the caller
         * ultimately reports an I/O error.  Drop cached partition extents
         * before issuing the first sector so a subsequent operation cannot
         * use metadata from before a partially completed update. */
        disk_block_cache_invalidate(disk_id);
    }
    count = length / 512u;
    /* See storage_disk_block_read(): source user mappings are not DMA-safe.
     * Copy each sector-aligned request into kernel memory before submitting it
     * to the controller. */
    while (done < count) {
        uint32_t chunk = min_u32(STORAGE_SCRATCH_SECTORS, count - done);
        storage_memcpy(storage_scratch, source + (uint64_t)done * SECTOR_SIZE,
                       chunk * SECTOR_SIZE);
        ret = install_write_sectors(disk, first_lba + offset / 512u + done,
                                    chunk, storage_scratch);
        if (ret < 0) return ret;
        done += chunk;
    }
    /* A write through a partition node cannot change its GPT extent. Keeping
     * that range cached avoids re-reading the table before every 4 KiB mkfs
     * write. Whole-disk raw writes may alter GPT metadata, so they still
     * force rediscovery before the next partition access. */
    if (partition_index < 0) {
        disk_block_cache_invalidate(disk_id);
    }
    if (out_written) *out_written = length;
    return 0;
}

int storage_disk_block_reread(uint32_t disk_id)
{
    struct install_disk_state *disk;
    uint64_t sectors;
    int ret = storage_acquire_task_io();
    if (ret < 0) return ret;
    ret = disk_manage_prepare(disk_id, &disk, &sectors);
    if (ret < 0) return ret;
    if ((disk->boot_root && !storage_installer_root_active()) ||
        disk->target_mounted || storage_disk_has_data_mount(disk_id)) {
        return -16;
    }
    storage_cache_invalidate();
    disk_block_cache_invalidate(disk_id);
    return 0;
}

/**
 * @brief Writes one updated GPT header to a managed disk.
 * @param disk Prepared block-disk state.
 * @param header Header to serialize.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_write_header(struct install_disk_state *disk,
                                 const struct gpt_header *header)
{
    if (!disk || !header) {
        return -22;
    }
    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_memcpy(storage_scratch, header, sizeof(*header));
    return install_write_sectors(disk, header->current_lba, 1u, storage_scratch);
}

/**
 * @brief Commits the GPT table in the crash-tolerant backup-before-primary order.
 * @param disk Prepared block-disk state.
 * @param table Validated table headers for the managed disk.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_write(struct install_disk_state *disk, struct disk_gpt_table *table)
{
    uint32_t table_crc;
    int ret;
    if (!disk || !table || table->table_bytes == 0 ||
        table->table_bytes > sizeof(storage_cluster_buf)) {
        return -22;
    }
    /* A failed or partial metadata update must never leave a stale extent
     * cache available to a later /dev/diskNpM lookup.  Invalidate before the
     * first sector and keep it invalid on every error path; successful writes
     * are invalidated again below for clarity. */
    disk_block_cache_invalidate_disk(disk);
    table_crc = storage_crc32(storage_cluster_buf, table->table_bytes);
    table->backup.partition_entries_crc32 = table_crc;
    table->backup.header_crc32 = 0;
    table->backup.header_crc32 = storage_crc32(&table->backup, sizeof(table->backup));
    table->primary.partition_entries_crc32 = table_crc;
    table->primary.header_crc32 = 0;
    table->primary.header_crc32 = storage_crc32(&table->primary, sizeof(table->primary));
    ret = install_write_sectors(disk, table->backup.partition_entries_lba,
                                table->table_sectors, storage_cluster_buf);
    if (ret < 0) {
        return ret;
    }
    ret = disk_gpt_write_header(disk, &table->backup);
    if (ret < 0) {
        return ret;
    }
    ret = install_write_sectors(disk, table->primary.partition_entries_lba,
                                table->table_sectors, storage_cluster_buf);
    if (ret < 0) {
        return ret;
    }
    ret = disk_gpt_write_header(disk, &table->primary);
    disk_block_cache_invalidate_disk(disk);
    return ret;
}

/**
 * @brief Validates all populated GPT entries and rejects overlapping ranges.
 * @param table Loaded GPT table metadata.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_entries_valid(const struct disk_gpt_table *table)
{
    const struct gpt_entry *entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
    if (!table || !entries) {
        return -22;
    }
    for (uint32_t i = 0; i < table->primary.partition_entry_count; ++i) {
        const struct gpt_entry *entry = &entries[i];
        if (!disk_gpt_entry_used(entry)) {
            continue;
        }
        if (entry->first_lba < table->primary.first_usable_lba ||
            disk_gpt_guid_empty(entry->unique_guid) ||
            entry->last_lba < entry->first_lba ||
            entry->last_lba > table->primary.last_usable_lba) {
            return -22;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const struct gpt_entry *other = &entries[j];
            if (disk_gpt_entry_used(other) &&
                !(entry->last_lba < other->first_lba || entry->first_lba > other->last_lba)) {
                return -22;
            }
        }
    }
    return 0;
}

/**
 * @brief Detects the filesystem superblock at a GPT partition start.
 * @param disk Prepared block-disk state.
 * @param entry Validated GPT partition entry.
 * @param out_filesystem Receives a LEONOS_DISK_FILESYSTEM value.
 * @return Zero when probing completed, including an unknown filesystem; a
 *         negative storage error when the partition could not be read.
 */
static int disk_partition_filesystem(struct install_disk_state *disk,
                                     const struct gpt_entry *entry,
                                     uint32_t *out_filesystem)
{
    uint64_t sectors;
    int ret;
    if (!disk || !entry || !out_filesystem || entry->last_lba < entry->first_lba) {
        return -22;
    }
    *out_filesystem = LEONOS_DISK_FILESYSTEM_UNKNOWN;
    sectors = entry->last_lba - entry->first_lba + 1u;
    ret = install_read_sectors(disk, entry->first_lba, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    if (storage_scratch[510] == 0x55 && storage_scratch[511] == 0xaa &&
        storage_memcmp(storage_scratch + 82u, "FAT32   ", 8u) == 0) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_FAT32;
        return 0;
    }
    if (storage_scratch[510] == 0x55 && storage_scratch[511] == 0xaa &&
        storage_memcmp(storage_scratch + 3u, "EXFAT   ", 8u) == 0 &&
        storage_scratch[108u] == 9u && storage_scratch[110u] == 1u) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_EXFAT;
        return 0;
    }
    if (sectors > 2u) {
        ret = install_read_sectors(disk, entry->first_lba + 2u, 1u, storage_scratch);
        if (ret < 0) {
            return ret;
        }
        const struct ext2_superblock *super =
            (const struct ext2_superblock *)(const void *)storage_scratch;
        if (super->magic == EXT2_SUPER_MAGIC) {
            *out_filesystem = LEONOS_DISK_FILESYSTEM_EXT2;
        }
    }
    return 0;
}

/**
 * @brief Converts a GPT UTF-16 name into the ASCII-safe public ABI field.
 * @param source GPT UTF-16 name field.
 * @param target Destination text buffer.
 * @param capacity Destination capacity in bytes.
 */
static void disk_gpt_name_to_text(const uint16_t source[36], char *target, uint32_t capacity)
{
    uint32_t out = 0;
    if (!target || capacity == 0) {
        return;
    }
    if (source) {
        for (uint32_t i = 0; i < 36u && source[i] != 0 && out + 1u < capacity; ++i) {
            uint16_t ch = source[i];
            target[out++] = ch >= 32u && ch <= 126u ? (char)ch : '?';
        }
    }
    target[out] = 0;
}

/**
 * @brief Builds public metadata for one validated GPT partition.
 * @param disk_id Block-disk identifier.
 * @param disk Prepared block-disk state.
 * @param entry_index GPT entry index.
 * @param entry GPT partition entry.
 * @param out Destination public partition record.
 */
static int disk_partition_export(uint32_t disk_id, struct install_disk_state *disk,
                                 uint32_t entry_index, const struct gpt_entry *entry,
                                 struct leonos_disk_partition *out)
{
    if (!disk || !entry || !out) {
        return -22;
    }
    storage_memzero(out, sizeof(*out));
    out->disk_id = disk_id;
    out->index = entry_index;
    out->sector_count = entry->last_lba - entry->first_lba + 1u;
    out->first_lba = entry->first_lba;
    storage_memcpy(out->type_guid, entry->type_guid, sizeof(out->type_guid));
    disk_gpt_name_to_text(entry->name, out->name, sizeof(out->name));
    if (storage_memcmp(entry->type_guid, esp_guid, sizeof(entry->type_guid)) == 0) {
        out->flags |= LEONOS_DISK_PARTITION_FLAG_ESP;
    }
    if (disk->boot_root) {
        out->flags |= LEONOS_DISK_PARTITION_FLAG_BOOT_ROOT |
                      LEONOS_DISK_PARTITION_FLAG_PROTECTED;
    }
    if (disk->target_mounted) {
        out->flags |= LEONOS_DISK_PARTITION_FLAG_TARGET_MOUNTED |
                      LEONOS_DISK_PARTITION_FLAG_PROTECTED;
    }
    if (storage_disk_partition_mount_path(disk_id, entry_index, out->mount_path,
                                          sizeof(out->mount_path)) == 0) {
        out->flags |= LEONOS_DISK_PARTITION_FLAG_MOUNTED;
    }
    return disk_partition_filesystem(disk, entry, &out->filesystem);
}

/**
 * @brief Checks whether an operation may mutate a disk-management partition.
 * @param disk Block-disk state that owns the partition.
 * @return Zero if the disk is not a startup or mounted installer target.
 */
static int disk_partition_mutable(const struct install_disk_state *disk)
{
    if (!disk) {
        return -22;
    }
    /* When booted from the installer ISO, / is the writable RAM root and the
     * physical disks are installation targets, even if probing them first
     * found an existing LeonOS root. Keep the boot-disk guard for normal disk
     * boots while allowing the ISO's advanced environment to manage targets. */
    return ((disk->boot_root && !storage_installer_root_active()) ||
            disk->target_mounted) ? -16 : 0;
}

/**
 * @brief Checks whether a runtime data mount belongs to a block disk.
 * @param disk_id Block-disk identifier to test.
 * @return Nonzero when a dynamic data mount references @p disk_id.
 */
static int storage_disk_has_data_mount(uint32_t disk_id)
{
    for (uint32_t volume_id = STORAGE_VOLUME_TARGET_ROOT;
         volume_id < STORAGE_MAX_VOLUMES; ++volume_id) {
        const struct storage_volume *volume = &g_volumes[volume_id];
        if (volume->ready && volume->data_partition_mount &&
            volume->source_disk_id == disk_id) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Checks whether installer-reserved mount slots are occupied.
 * @return Nonzero when /target or /target/boot cannot be reused by the installer.
 */
static int storage_installer_mounts_busy(void)
{
    for (uint32_t volume_id = STORAGE_VOLUME_TARGET_ROOT;
         volume_id <= STORAGE_VOLUME_BOOT; ++volume_id) {
        const struct storage_volume *volume = &g_volumes[volume_id];
        if (volume->ready) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Tests a user-visible filesystem selection accepted by the formatter.
 * @param filesystem LEONOS_DISK_FILESYSTEM value.
 * @return Nonzero when the selection is FAT32, exFAT, or ext2.
 */
static int disk_filesystem_format_supported(uint32_t filesystem)
{
    return filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ||
           filesystem == LEONOS_DISK_FILESYSTEM_EXT2 ||
           filesystem == LEONOS_DISK_FILESYSTEM_EXFAT;
}

/**
 * @brief Formats a validated data extent using the selected LeonOS filesystem.
 * @param disk Prepared block-disk state.
 * @param entry GPT partition entry defining the data extent.
 * @param filesystem Selected LEONOS_DISK_FILESYSTEM value.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_format_entry(struct install_disk_state *disk, const struct gpt_entry *entry,
                             uint32_t filesystem)
{
    uint64_t sector_count;
    if (!disk || !entry || entry->last_lba < entry->first_lba ||
        !disk_filesystem_format_supported(filesystem)) {
        return -22;
    }
    sector_count = entry->last_lba - entry->first_lba + 1u;
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        return install_format_fat32(disk, entry->first_lba, sector_count);
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_EXFAT) {
        return install_format_exfat(disk, entry->first_lba, sector_count);
    }
    return install_format_ext2(disk, entry->first_lba, sector_count);
}

/**
 * @brief Assigns a standard GPT type GUID for a selected data filesystem.
 * @param entry Writable GPT entry.
 * @param filesystem Selected LEONOS_DISK_FILESYSTEM value.
 */
static void disk_gpt_set_filesystem_type(struct gpt_entry *entry, uint32_t filesystem)
{
    if (!entry) {
        return;
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_UNKNOWN ||
        filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ||
        filesystem == LEONOS_DISK_FILESYSTEM_EXFAT) {
        storage_memcpy(entry->type_guid, basic_data_guid, sizeof(entry->type_guid));
    } else if (filesystem == LEONOS_DISK_FILESYSTEM_EXT2) {
        storage_memcpy(entry->type_guid, linux_filesystem_guid, sizeof(entry->type_guid));
    }
}

static int disk_gpt_set_partition_type(struct gpt_entry *entry, uint32_t type)
{
    if (!entry) {
        return -22;
    }
    if (type == LEONOS_DISK_PARTITION_TYPE_BASIC_DATA) {
        storage_memcpy(entry->type_guid, basic_data_guid, sizeof(entry->type_guid));
    } else if (type == LEONOS_DISK_PARTITION_TYPE_ESP) {
        storage_memcpy(entry->type_guid, esp_guid, sizeof(entry->type_guid));
    } else if (type == LEONOS_DISK_PARTITION_TYPE_LINUX) {
        storage_memcpy(entry->type_guid, linux_filesystem_guid, sizeof(entry->type_guid));
    } else {
        return -22;
    }
    return 0;
}

/**
 * @brief Aligns a logical block address upward to the standard GPT 1 MiB boundary.
 * @param lba Logical block address to align.
 * @return Aligned LBA, or zero when alignment would overflow.
 */
static uint64_t disk_gpt_align_lba(uint64_t lba)
{
    if (lba > UINT64_MAX - 2047u) {
        return 0;
    }
    return (lba + 2047u) & ~2047ULL;
}

/**
 * @brief Finds the first aligned free GPT range large enough for a request.
 * @param table Loaded and validated GPT metadata.
 * @param required_sectors Number of aligned sectors requested.
 * @param out_first Receives the first sector in the free range.
 * @return Zero on success or a negative errno-style storage error.
 */
static int disk_gpt_find_free_range(const struct disk_gpt_table *table,
                                    uint64_t required_sectors, uint64_t *out_first)
{
    const struct gpt_entry *entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
    uint64_t cursor;
    if (!table || !entries || !out_first || required_sectors == 0) {
        return -22;
    }
    cursor = table->primary.first_usable_lba;
    while (cursor <= table->primary.last_usable_lba) {
        uint64_t candidate = disk_gpt_align_lba(cursor);
        uint64_t next = table->primary.last_usable_lba + 1u;
        uint64_t next_end = 0;
        if (candidate == 0 || candidate > table->primary.last_usable_lba) {
            return -28;
        }
        for (uint32_t i = 0; i < table->primary.partition_entry_count; ++i) {
            const struct gpt_entry *entry = &entries[i];
            if (disk_gpt_entry_used(entry) && entry->first_lba >= cursor &&
                entry->first_lba < next) {
                next = entry->first_lba;
                next_end = entry->last_lba;
            }
        }
        if (candidate <= next && required_sectors <= next - candidate) {
            *out_first = candidate;
            return 0;
        }
        if (next > table->primary.last_usable_lba || next_end == UINT64_MAX) {
            return -28;
        }
        cursor = next_end + 1u;
    }
    return -28;
}

/**
 * @brief Writes an ASCII-safe GPT name from a disk-management create request.
 * @param destination Writable GPT UTF-16 name field.
 * @param source User-selected partition label.
 */
static void disk_gpt_set_name(uint16_t destination[36], const char *source)
{
    uint32_t i = 0;
    if (!destination) {
        return;
    }
    for (; i < 36u && source && source[i]; ++i) {
        uint8_t ch = (uint8_t)source[i];
        destination[i] = ch >= 32u && ch <= 126u ? ch : '?';
    }
    for (; i < 36u; ++i) {
        destination[i] = 0;
    }
}

/**
 * @brief Lists GPT partitions for an administrator-facing disk-management client.
 * @param disk_id Detected block-disk identifier.
 * @param partitions Caller buffer receiving public partition records.
 * @param capacity Number of records available in @p partitions.
 * @param out_count Receives the total number of used GPT entries.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_list_partitions(uint32_t disk_id,
                                 struct leonos_disk_partition *partitions,
                                 uint32_t capacity, uint32_t *out_count)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    const struct gpt_entry *entries;
    uint64_t sector_count;
    uint32_t count = 0;
    if (!out_count || (capacity && !partitions)) {
        return -22;
    }
    *out_count = 0;
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (capacity > LEONOS_DISK_MAX_PARTITIONS) {
        capacity = LEONOS_DISK_MAX_PARTITIONS;
    }
    ret = disk_manage_prepare(disk_id, &disk, &sector_count);
    if (ret < 0) {
        return ret;
    }
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0) {
        if (ret == -2) {
            *out_count = 0;
            return 0;
        }
        return ret;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
    for (uint32_t i = 0; i < table.primary.partition_entry_count; ++i) {
        if (!disk_gpt_entry_used(&entries[i])) {
            continue;
        }
        if (count < capacity) {
            ret = disk_partition_export(disk_id, disk, i, &entries[i], &partitions[count]);
            if (ret < 0) {
                return ret;
            }
        }
        ++count;
    }
    *out_count = count;
    return 0;
}

/**
 * @brief Initializes a protective MBR and empty primary/backup GPT pair.
 *
 * This is intentionally separate from the normal GPT editor.  The editor
 * requires a valid table, while this operation is the explicit destructive
 * entry point used by the installer ISO's gptinit utility.
 */
int storage_disk_initialize_gpt(const struct leonos_disk_gpt_initialize *request)
{
    struct install_disk_state *disk;
    struct gpt_header *header;
    uint8_t disk_guid[16];
    uint64_t sector_count;
    uint64_t last_lba;
    uint64_t backup_entries_lba;
    uint64_t first_usable = 2048u;
    uint64_t last_usable;
    uint32_t table_bytes = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE;
    uint32_t table_sectors = table_bytes / SECTOR_SIZE;
    uint32_t table_crc;
    int ret;

    ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!request || (request->flags & ~LEONOS_DISK_GPT_INITIALIZE_FORCE) != 0) {
        return -22;
    }
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0) {
        return ret;
    }
    ret = disk_partition_mutable(disk);
    if (ret < 0) {
        return ret;
    }
    /* This operation intentionally replaces all partition metadata.  Drop
     * any extents learned from a previous table before clearing sector 0 and
     * the GPT copies, including when a later write fails. */
    disk_block_cache_invalidate_disk(disk);
    if (sector_count < 655360ULL) {
        return -28;
    }

    /* Refuse to replace a valid table unless the caller explicitly opted in. */
    {
        struct disk_gpt_table existing;
        ret = disk_gpt_load(disk, sector_count, &existing);
        if (ret == 0 && !(request->flags & LEONOS_DISK_GPT_INITIALIZE_FORCE)) {
            return -17;
        }
    }

    last_lba = sector_count - 1u;
    backup_entries_lba = last_lba - table_sectors;
    last_usable = backup_entries_lba - 1u;
    if (last_usable <= first_usable) {
        return -28;
    }
    storage_make_guid(disk_guid, 0x4c3447494e495447ULL, disk, sector_count);

    /* Clear old metadata, including stale backup headers and partition rows. */
    ret = install_clear_sectors(disk, 0, first_usable);
    if (ret < 0) {
        return ret;
    }
    ret = install_clear_sectors(disk, backup_entries_lba, table_sectors + 1u);
    if (ret < 0) {
        return ret;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_scratch[446 + 4] = 0xee;
    storage_put_u32(storage_scratch + 446 + 8, 1u);
    storage_put_u32(storage_scratch + 446 + 12,
                    sector_count - 1u > 0xffffffffULL
                        ? 0xffffffffu : (uint32_t)(sector_count - 1u));
    storage_scratch[510] = 0x55;
    storage_scratch[511] = 0xaa;
    ret = install_write_sectors(disk, 0, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }

    storage_memzero(storage_cluster_buf, table_bytes);
    table_crc = storage_crc32(storage_cluster_buf, table_bytes);
    ret = install_write_sectors(disk, 2u, table_sectors, storage_cluster_buf);
    if (ret < 0) {
        return ret;
    }
    ret = install_write_sectors(disk, backup_entries_lba, table_sectors,
                                storage_cluster_buf);
    if (ret < 0) {
        return ret;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    header = (struct gpt_header *)(void *)storage_scratch;
    header->signature = 0x5452415020494645ULL;
    header->revision = 0x00010000u;
    header->header_size = sizeof(struct gpt_header);
    header->current_lba = 1u;
    header->backup_lba = last_lba;
    header->first_usable_lba = first_usable;
    header->last_usable_lba = last_usable;
    storage_memcpy(header->disk_guid, disk_guid, sizeof(header->disk_guid));
    header->partition_entries_lba = 2u;
    header->partition_entry_count = GPT_ENTRY_COUNT;
    header->partition_entry_size = GPT_ENTRY_SIZE;
    header->partition_entries_crc32 = table_crc;
    header->header_crc32 = storage_crc32(header, header->header_size);
    ret = install_write_sectors(disk, 1u, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }

    header->header_crc32 = 0;
    header->current_lba = last_lba;
    header->backup_lba = 1u;
    header->partition_entries_lba = backup_entries_lba;
    header->header_crc32 = storage_crc32(header, header->header_size);
    ret = install_write_sectors(disk, last_lba, 1u, storage_scratch);
    if (ret == 0) disk_block_cache_invalidate_disk(disk);
    return ret;
}

/**
 * @brief Formats an existing unprotected GPT partition as FAT32, exFAT, or ext2.
 * @param request Validated disk-management format request.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_format_partition(const struct leonos_disk_partition_format *request)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    struct gpt_entry entry;
    struct gpt_entry *entries;
    uint64_t sector_count;
    uint32_t mounted_volume;
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!request || !disk_filesystem_format_supported(request->filesystem)) {
        return -22;
    }
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0) return ret;
    ret = disk_partition_mutable(disk);
    if (ret < 0) return ret;
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0 || request->partition_index >= table.primary.partition_entry_count) {
        return ret < 0 ? ret : -22;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    entries = (struct gpt_entry *)(void *)storage_cluster_buf;
    if (!disk_gpt_entry_used(&entries[request->partition_index])) {
        return -2;
    }
    if (storage_disk_partition_volume_id(request->disk_id, request->partition_index,
                                         &mounted_volume) == 0) {
        return -LEONOS_EBUSY;
    }
    entry = entries[request->partition_index];
    ret = disk_format_entry(disk, &entry, request->filesystem);
    if (ret < 0) {
        return ret;
    }
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0) {
        return ret;
    }
    entries = (struct gpt_entry *)(void *)storage_cluster_buf;
    if (request->partition_index >= table.primary.partition_entry_count ||
        !disk_gpt_entry_used(&entries[request->partition_index])) {
        return -5;
    }
    disk_gpt_set_filesystem_type(&entries[request->partition_index], request->filesystem);
    return disk_gpt_write(disk, &table);
}

/**
 * @brief Deletes an unprotected GPT partition entry while leaving its data intact.
 * @param request Validated disk-management delete request.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_delete_partition(const struct leonos_disk_partition_delete *request)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    struct gpt_entry *entries;
    uint64_t sector_count;
    uint32_t mounted_volume;
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!request) {
        return -22;
    }
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0) return ret;
    ret = disk_partition_mutable(disk);
    if (ret < 0) return ret;
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0 || request->partition_index >= table.primary.partition_entry_count) {
        return ret < 0 ? ret : -22;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    entries = (struct gpt_entry *)(void *)storage_cluster_buf;
    if (!disk_gpt_entry_used(&entries[request->partition_index])) {
        return -2;
    }
    if (storage_disk_partition_volume_id(request->disk_id, request->partition_index,
                                         &mounted_volume) == 0) {
        return -LEONOS_EBUSY;
    }
    storage_memzero(&entries[request->partition_index], sizeof(entries[request->partition_index]));
    return disk_gpt_write(disk, &table);
}

/** Edit standard GPT type and/or printable partition name in both GPT copies. */
int storage_disk_edit_partition(const struct leonos_disk_partition_edit *request)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    struct gpt_entry *entries;
    uint64_t sector_count;
    uint32_t mounted_volume;
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!request || request->edit_mask == 0 ||
        (request->edit_mask & ~(LEONOS_DISK_PARTITION_EDIT_TYPE |
                                LEONOS_DISK_PARTITION_EDIT_NAME)) != 0) {
        return -22;
    }
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0) return ret;
    ret = disk_partition_mutable(disk);
    if (ret < 0) return ret;
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0 || request->partition_index >= table.primary.partition_entry_count) {
        return ret < 0 ? ret : -22;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    entries = (struct gpt_entry *)(void *)storage_cluster_buf;
    if (!disk_gpt_entry_used(&entries[request->partition_index])) {
        return -2;
    }
    if (storage_disk_partition_volume_id(request->disk_id, request->partition_index,
                                         &mounted_volume) == 0) {
        return -LEONOS_EBUSY;
    }
    if (request->edit_mask & LEONOS_DISK_PARTITION_EDIT_TYPE) {
        ret = disk_gpt_set_partition_type(&entries[request->partition_index], request->type);
        if (ret < 0) {
            return ret;
        }
    }
    if (request->edit_mask & LEONOS_DISK_PARTITION_EDIT_NAME) {
        disk_gpt_set_name(entries[request->partition_index].name, request->name);
    }
    return disk_gpt_write(disk, &table);
}

/**
 * @brief Creates and formats a data partition in the first suitable free GPT range.
 * @param request Validated disk-management create request.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_create_partition(const struct leonos_disk_partition_create *request)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    struct gpt_entry *entries;
    uint64_t sector_count;
    uint64_t required_sectors;
    uint64_t first_lba;
    uint32_t free_index = LEONOS_DISK_MAX_PARTITIONS;
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!request || request->size_mib == 0 ||
        (request->filesystem != LEONOS_DISK_FILESYSTEM_UNKNOWN &&
         !disk_filesystem_format_supported(request->filesystem))) {
        return -22;
    }
    required_sectors = (uint64_t)request->size_mib * 2048u;
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0) return ret;
    ret = disk_partition_mutable(disk);
    if (ret < 0) return ret;
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0) {
        return ret;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    entries = (struct gpt_entry *)(void *)storage_cluster_buf;
    for (uint32_t i = 0; i < table.primary.partition_entry_count; ++i) {
        if (!disk_gpt_entry_used(&entries[i])) {
            free_index = i;
            break;
        }
    }
    if (free_index == LEONOS_DISK_MAX_PARTITIONS ||
        disk_gpt_find_free_range(&table, required_sectors, &first_lba) < 0 ||
        first_lba > table.primary.last_usable_lba ||
        required_sectors > table.primary.last_usable_lba - first_lba + 1u) {
        return -28;
    }
    storage_memzero(&entries[free_index], sizeof(entries[free_index]));
    entries[free_index].first_lba = first_lba;
    entries[free_index].last_lba = first_lba + required_sectors - 1u;
    storage_make_guid(entries[free_index].unique_guid,
                      0x4c34475041525431ULL ^ ((uint64_t)free_index << 32) ^ first_lba,
                      disk, sector_count);
    disk_gpt_set_filesystem_type(&entries[free_index], request->filesystem);
    disk_gpt_set_name(entries[free_index].name,
                      request->name[0] ? request->name : "LeonOS Data");
    ret = disk_gpt_write(disk, &table);
    if (ret < 0) {
        return ret;
    }
    if (request->filesystem == LEONOS_DISK_FILESYSTEM_UNKNOWN) {
        return 0;
    }
    return disk_format_entry(disk, &entries[free_index], request->filesystem);
}

int storage_disk_partition_volume_id(uint32_t disk_id, uint32_t partition_index,
                                     uint32_t *out_volume_id)
{
    if (!out_volume_id || partition_index >= LEONOS_DISK_MAX_PARTITIONS) {
        return -22;
    }
    for (uint32_t volume_id = STORAGE_VOLUME_TARGET_ROOT;
         volume_id < STORAGE_MAX_VOLUMES; ++volume_id) {
        const struct storage_volume *volume = &g_volumes[volume_id];
        if (volume->ready && volume->data_partition_mount &&
            volume->source_disk_id == disk_id &&
            volume->source_partition_index == partition_index) {
            *out_volume_id = volume_id;
            return 0;
        }
    }
    return -2;
}

int storage_disk_partition_mount_path(uint32_t disk_id, uint32_t partition_index,
                                      char *out_path, uint32_t capacity)
{
    uint32_t volume_id;
    if (!out_path || capacity == 0) {
        return -22;
    }
    if (storage_disk_partition_volume_id(disk_id, partition_index, &volume_id) < 0) {
        out_path[0] = 0;
        return -2;
    }
    storage_copy_text(out_path, capacity, g_volumes[volume_id].mount_path);
    return 0;
}

static uint32_t storage_next_data_mount_volume(void)
{
    for (uint32_t volume_id = STORAGE_VOLUME_DYNAMIC_FIRST;
         volume_id < STORAGE_MAX_VOLUMES; ++volume_id) {
        if (!g_volumes[volume_id].ready) {
            return volume_id;
        }
    }
    return STORAGE_MAX_VOLUMES;
}

static int storage_mount_path_allowed(const char *path)
{
    uint32_t length;
    if (!path || path[0] != '/') {
        return -22;
    }
    length = storage_strlen(path);
    if (length < 2u || length >= LEONOS_FS_PATH_LEN || path[length] != 0) {
        return -22;
    }
    /* Never let a user mount over a kernel-managed namespace. /target and
     * /target/boot are the installer ISO's explicit standard mount points. */
    if (storage_text_eq_ci(path, "/boot") || storage_text_eq_ci(path, "/dev")) {
        return -16;
    }
    if ((storage_text_eq_ci(path, "/target") ||
         storage_text_eq_ci(path, "/target/boot")) && !storage_installer_root_active()) {
        return -16;
    }
    for (uint32_t i = STORAGE_VOLUME_TARGET_ROOT; i < STORAGE_MAX_VOLUMES; ++i) {
        if (g_volumes[i].ready && storage_text_eq_ci(g_volumes[i].mount_path, path)) {
            return -16;
        }
    }
    return 0;
}

static int storage_filesystem_from_name(const char *filesystem, uint32_t *out_filesystem)
{
    if (!out_filesystem) {
        return -22;
    }
    if (!filesystem || !filesystem[0] || storage_text_eq_ci(filesystem, "auto")) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_UNKNOWN;
        return 0;
    }
    if (storage_text_eq_ci(filesystem, "fat") || storage_text_eq_ci(filesystem, "vfat") ||
        storage_text_eq_ci(filesystem, "fat32")) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_FAT32;
        return 0;
    }
    if (storage_text_eq_ci(filesystem, "ext2")) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_EXT2;
        return 0;
    }
    if (storage_text_eq_ci(filesystem, "exfat")) {
        *out_filesystem = LEONOS_DISK_FILESYSTEM_EXFAT;
        return 0;
    }
    return -95;
}

static uint32_t storage_mount_volume_for_target(const char *target)
{
    if (target && storage_text_eq_ci(target, "/target")) {
        return STORAGE_VOLUME_TARGET_ROOT;
    }
    if (target && storage_text_eq_ci(target, "/target/boot")) {
        return STORAGE_VOLUME_BOOT;
    }
    return storage_next_data_mount_volume();
}

/** Mount a GPT partition through the standard block-device and mount ABI. */
int storage_mount_block_partition(uint32_t disk_id, uint32_t partition_index,
                                  const char *target, const char *filesystem_name,
                                  uint64_t flags, uint32_t *out_volume_id)
{
    struct install_disk_state *disk;
    struct disk_gpt_table table;
    struct gpt_entry entry;
    struct storage_volume *volume;
    struct storage_volume *old_volume;
    uint64_t sector_count;
    uint32_t filesystem;
    uint32_t volume_id;
    int ret;
    char mounted_path[LEONOS_FS_PATH_LEN];
    uint32_t requested_filesystem;

    console_printf("[storage] mount enter disk=%u part=%u target=%s fs=%s flags=%llu\n",
                   disk_id, partition_index, target ? target : "(auto)",
                   filesystem_name ? filesystem_name : "auto",
                   (unsigned long long)flags);
    if (flags != 0 || storage_filesystem_from_name(filesystem_name, &requested_filesystem) < 0) {
        console_printf("[storage] mount invalid arguments flags=%llu fs_name=%s\n",
                       (unsigned long long)flags,
                       filesystem_name ? filesystem_name : "(null)");
        return -22;
    }
    if (target && target[0]) {
        ret = storage_mount_path_allowed(target);
        if (ret < 0) {
            console_printf("[storage] mount path not allowed target=%s ret=%d\n",
                           target, ret);
            return ret;
        }
    }
    ret = storage_acquire_task_io();
    if (ret < 0) {
        console_printf("[storage] mount acquire io failed ret=%d\n", ret);
        return ret;
    }
    ret = disk_manage_prepare(disk_id, &disk, &sector_count);
    if (ret < 0) {
        console_printf("[storage] mount disk prepare failed disk=%u ret=%d\n", disk_id, ret);
        return ret;
    }
    ret = disk_partition_mutable(disk);
    if (ret < 0) {
        console_printf("[storage] mount disk immutable disk=%u ret=%d\n", disk_id, ret);
        return ret;
    }
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0 || partition_index >= table.primary.partition_entry_count) {
        console_printf("[storage] mount gpt load failed disk=%u part=%u ret=%d\n",
                       disk_id, partition_index, ret < 0 ? ret : -22);
        return ret < 0 ? ret : -22;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        console_printf("[storage] mount gpt entries invalid disk=%u part=%u ret=%d\n",
                       disk_id, partition_index, ret);
        return ret;
    }
    {
        const struct gpt_entry *entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
        if (!disk_gpt_entry_used(&entries[partition_index])) {
            return -2;
        }
        entry = entries[partition_index];
    }
    if (storage_disk_partition_mount_path(disk_id, partition_index,
                                          mounted_path, sizeof(mounted_path)) == 0) {
        if (target && !storage_text_eq_ci(target, mounted_path)) {
            return -16;
        }
        if (out_volume_id) {
            (void)storage_disk_partition_volume_id(disk_id, partition_index,
                                                   out_volume_id);
        }
        return 0;
    }
    ret = disk_partition_filesystem(disk, &entry, &filesystem);
    if (ret < 0) {
        console_printf("[storage] mount filesystem probe failed disk=%u part=%u ret=%d\n",
                       disk_id, partition_index, ret);
        return ret;
    }
    if (!disk_filesystem_format_supported(filesystem)) {
        console_printf("[storage] mount unsupported filesystem disk=%u part=%u fs=%u\n",
                       disk_id, partition_index, filesystem);
        return -95;
    }
    if (requested_filesystem && requested_filesystem != filesystem) {
        console_printf("[storage] mount filesystem mismatch disk=%u part=%u detected=%u requested=%u\n",
                       disk_id, partition_index, filesystem, requested_filesystem);
        return -22;
    }
    volume_id = storage_mount_volume_for_target(target);
    if (volume_id == STORAGE_MAX_VOLUMES) {
        return -28;
    }
    if (g_volumes[volume_id].ready) {
        return -16;
    }

    volume = &g_volumes[volume_id];
    old_volume = g_active_volume;
    storage_cache_invalidate();
    storage_memzero(volume, sizeof(*volume));
    volume->volume_id = (uint8_t)volume_id;
    storage_volume_from_install_disk(volume, disk);
    volume->source_disk_id = disk_id;
    volume->source_partition_index = partition_index;
    if (!target || !target[0]) {
        if (storage_set_data_mount_path(volume, disk_id, partition_index) < 0) {
            storage_memzero(volume, sizeof(*volume));
            return -22;
        }
    } else {
        storage_copy_text(volume->mount_path, sizeof(volume->mount_path), target);
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        volume->esp_start_lba = entry.first_lba;
        volume->esp_sector_count = entry.last_lba - entry.first_lba + 1u;
    } else if (filesystem == LEONOS_DISK_FILESYSTEM_EXFAT) {
        volume->exfat_start_lba = entry.first_lba;
        volume->exfat_sector_count = entry.last_lba - entry.first_lba + 1u;
    } else {
        volume->ext2_start_lba = entry.first_lba;
        volume->ext2_sector_count = entry.last_lba - entry.first_lba + 1u;
    }
    g_active_volume = volume;
    ret = filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ? fat32_mount() :
          (filesystem == LEONOS_DISK_FILESYSTEM_EXFAT ? exfat_mount() : ext2_mount());
    if (ret == 0 &&
        ((filesystem == LEONOS_DISK_FILESYSTEM_FAT32 &&
          volume->filesystem != STORAGE_FILESYSTEM_FAT32) ||
         (filesystem == LEONOS_DISK_FILESYSTEM_EXFAT &&
          volume->filesystem != STORAGE_FILESYSTEM_EXFAT) ||
         (filesystem == LEONOS_DISK_FILESYSTEM_EXT2 &&
          volume->filesystem != STORAGE_FILESYSTEM_EXT2))) {
        ret = -5;
    }
    if (ret == 0) {
        volume->data_partition_mount = 1;
        volume->ready = true;
        console_printf("[ntclks] storage mounted data partition disk=%u entry=%u path=%s fs=%s\\n",
                       disk_id, partition_index, volume->mount_path,
                       filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ? "fat32" :
                       (filesystem == LEONOS_DISK_FILESYSTEM_EXFAT ? "exfat" : "ext2"));
        if (out_volume_id) {
            *out_volume_id = volume_id;
        }
    } else {
        storage_memzero(volume, sizeof(*volume));
    }
    g_active_volume = old_volume;
    storage_cache_invalidate();
    return ret;
}

/** Legacy ABI adapter retained until all callers have moved to mount(2). */
int storage_disk_mount_partition(struct leonos_disk_partition_mount *request)
{
    uint32_t volume_id;
    int ret;
    if (!request) {
        return -22;
    }
    ret = storage_mount_block_partition(request->disk_id, request->partition_index,
                                        request->mount_path[0] ? request->mount_path : NULL,
                                        NULL, 0, &volume_id);
    if (ret == 0 && volume_id < STORAGE_MAX_VOLUMES) {
        storage_copy_text(request->mount_path, sizeof(request->mount_path),
                          g_volumes[volume_id].mount_path);
    }
    return ret;
}

int storage_mount_path_volume_id(const char *target, uint32_t *out_volume_id)
{
    if (!target || !out_volume_id) {
        return -22;
    }
    for (uint32_t volume_id = STORAGE_VOLUME_TARGET_ROOT;
         volume_id < STORAGE_MAX_VOLUMES; ++volume_id) {
        const struct storage_volume *volume = &g_volumes[volume_id];
        if (volume->ready && volume->data_partition_mount &&
            storage_text_eq_ci(volume->mount_path, target)) {
            *out_volume_id = volume_id;
            return 0;
        }
    }
    return -2;
}

int storage_unmount_path(const char *target, uint32_t *out_volume_id)
{
    uint32_t volume_id;
    uint32_t target_length;
    int ret;
    if (!target) {
        return -22;
    }
    ret = storage_mount_path_volume_id(target, &volume_id);
    if (ret < 0) {
        return ret;
    }
    target_length = storage_strlen(target);
    for (uint32_t i = STORAGE_VOLUME_TARGET_ROOT; i < STORAGE_MAX_VOLUMES; ++i) {
        const struct storage_volume *volume = &g_volumes[i];
        if (i != volume_id && volume->ready &&
            storage_memcmp(volume->mount_path, target, target_length) == 0 &&
            volume->mount_path[target_length] == '/') {
            return -16;
        }
    }
    ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (g_active_volume == &g_volumes[volume_id]) {
        g_active_volume = &g_volumes[STORAGE_VOLUME_ROOT];
    }
    console_printf("[ntclks] storage unmounted data partition disk=%u entry=%u path=%s\\n",
                   g_volumes[volume_id].source_disk_id,
                   g_volumes[volume_id].source_partition_index, target);
    storage_memzero(&g_volumes[volume_id], sizeof(g_volumes[volume_id]));
    storage_cache_invalidate();
    if (out_volume_id) {
        *out_volume_id = volume_id;
    }
    return 0;
}

/**
 * @brief Unmounts a runtime data partition after the kernel has checked task usage.
 * @param request Disk and GPT-entry selector.
 * @return Zero on success or a negative errno-style storage error.
 */
int storage_disk_unmount_partition(const struct leonos_disk_partition_unmount *request)
{
    uint32_t volume_id;
    int ret;
    if (!request || request->reserved0 != 0 || request->reserved1 != 0) {
        return -22;
    }
    ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    ret = storage_disk_partition_volume_id(request->disk_id, request->partition_index,
                                           &volume_id);
    if (ret < 0) {
        return ret;
    }
    if (volume_id >= STORAGE_MAX_VOLUMES || !g_volumes[volume_id].data_partition_mount) {
        return -2;
    }
    return storage_unmount_path(g_volumes[volume_id].mount_path, NULL);
}

int storage_install_list_disks(struct leonos_install_disk *disks,
                               uint32_t capacity, uint32_t *out_count)
{
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!out_count) {
        return -22;
    }
    *out_count = g_install_disk_count;
    if (capacity > g_install_disk_count) {
        capacity = g_install_disk_count;
    }
    if (capacity && !disks) {
        return -22;
    }
    for (uint32_t i = 0; i < capacity; ++i) {
        struct install_disk_state *src = &g_install_disks[i];
        if (src->present && src->sector_count == 0) {
            uint64_t sectors = 0;
            if (storage_install_identify(src, &sectors) == 0) {
                src->sector_count = sectors;
            }
        }
        disks[i].id = i;
        disks[i].port = src->transport == STORAGE_TRANSPORT_NVME
                            ? src->nvme_nsid : src->port;
        disks[i].sector_size = SECTOR_SIZE;
        disks[i].flags = 0;
        if (src->boot_root) {
            disks[i].flags |= LEONOS_INSTALL_DISK_FLAG_BOOT_ROOT;
        }
        if (src->target_mounted) {
            disks[i].flags |= LEONOS_INSTALL_DISK_FLAG_TARGET_MOUNTED;
        }
        disks[i].sector_count = src->sector_count;
        storage_copy_text(disks[i].name, sizeof(disks[i].name),
                          src->transport == STORAGE_TRANSPORT_IDE_PIO
                              ? "IDE/PATA Disk"
                              : (src->transport == STORAGE_TRANSPORT_NVME
                                     ? "NVMe Namespace" : "SATA/AHCI Disk"));
    }
    return 0;
}

int storage_install_format_target(uint32_t disk_id)
{
    uint64_t sector_count;
    uint64_t esp_lba = 0;
    uint64_t esp_sectors = 0;
    uint64_t root_lba = 0;
    uint64_t root_sectors = 0;
    int ret;
    ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (disk_id >= g_install_disk_count || !g_install_disks[disk_id].present) {
        return -2;
    }
    struct install_disk_state *disk = &g_install_disks[disk_id];
    if (disk->target_mounted || storage_disk_has_data_mount(disk_id) ||
        storage_installer_mounts_busy()) {
        return -LEONOS_EBUSY;
    }
    ret = storage_prepare_install_disk(disk);
    if (ret < 0) {
        return ret;
    }
    sector_count = disk->sector_count;
    if (sector_count == 0) {
        ret = storage_install_identify(disk, &sector_count);
        if (ret < 0) {
            return ret;
        }
        disk->sector_count = sector_count;
    }
    ret = install_write_gpt(disk, sector_count, &esp_lba, &esp_sectors,
                            &root_lba, &root_sectors);
    if (ret < 0) {
        return ret;
    }
    ret = install_format_fat32(disk, esp_lba, esp_sectors);
    if (ret < 0) {
        return ret;
    }
    ret = install_format_exfat(disk, root_lba, root_sectors);
    if (ret < 0) {
        return ret;
    }
    disk->target_mounted = 0;
    storage_memzero(&g_volumes[STORAGE_VOLUME_TARGET_ROOT],
                    sizeof(g_volumes[STORAGE_VOLUME_TARGET_ROOT]));
    storage_memzero(&g_volumes[STORAGE_VOLUME_BOOT], sizeof(g_volumes[STORAGE_VOLUME_BOOT]));
    if (g_active_volume == &g_volumes[STORAGE_VOLUME_TARGET_ROOT] ||
        g_active_volume == &g_volumes[STORAGE_VOLUME_BOOT]) {
        g_active_volume = &g_volumes[STORAGE_VOLUME_ROOT];
    }
    disk_block_cache_invalidate(disk_id);
    return 0;
}

/**
 * @brief Preserves the historical installer formatting ABI.
 * @param disk_id Installer disk identifier.
 * @return Zero after formatting the ESP plus exFAT root, or a negative error.
 */
int storage_install_format_esp(uint32_t disk_id)
{
    return storage_install_format_target(disk_id);
}

int storage_install_mount_target(uint32_t disk_id)
{
    int ownership_ret = storage_acquire_task_io();
    if (ownership_ret < 0) {
        return ownership_ret;
    }
    if (disk_id >= g_install_disk_count || !g_install_disks[disk_id].present) {
        return -2;
    }
    struct install_disk_state *disk = &g_install_disks[disk_id];
    if (disk->target_mounted) {
        return 0;
    }
    if (storage_disk_has_data_mount(disk_id) || storage_installer_mounts_busy()) {
        return -LEONOS_EBUSY;
    }
    struct storage_volume *target = &g_volumes[STORAGE_VOLUME_TARGET_ROOT];
    struct storage_volume *esp = &g_volumes[STORAGE_VOLUME_BOOT];
    struct storage_volume *old = g_active_volume;
    storage_cache_invalidate();
    storage_memzero(target, sizeof(*target));
    target->volume_id = STORAGE_VOLUME_TARGET_ROOT;
    storage_volume_from_install_disk(target, disk);
    storage_copy_text(target->mount_path, sizeof(target->mount_path), "/target");
    int ret = storage_prepare_install_disk(disk);
    if (ret < 0) {
        g_active_volume = old;
        return ret;
    }
    g_active_volume = target;
    ret = gpt_find_esp();
    if (ret == 0 && target->exfat_start_lba) {
        ret = exfat_mount();
        console_printf("[ntclks] installer exFAT target mount returned %d\n", ret);
    }
    if (ret == 0 && target->filesystem != STORAGE_FILESYSTEM_EXFAT && target->ext2_start_lba) {
        ret = ext2_mount();
    }
    if (ret == 0 && (target->filesystem == STORAGE_FILESYSTEM_EXFAT ||
                     target->filesystem == STORAGE_FILESYSTEM_EXT2)) {
        target->ready = true;
        disk->target_mounted = 1;
        storage_memzero(esp, sizeof(*esp));
        *esp = *target;
        esp->volume_id = STORAGE_VOLUME_BOOT;
        esp->ready = false;
        storage_copy_text(esp->mount_path, sizeof(esp->mount_path), "/target/boot");
        g_active_volume = esp;
        ret = fat32_mount();
        if (ret < 0) {
            console_printf("[ntclks] installer ESP mount failed ret=%d lba=%llu sectors=%llu\n",
                           ret, (unsigned long long)esp->esp_start_lba,
                           (unsigned long long)esp->esp_sector_count);
        }
        if (ret == 0) esp->ready = true;
    }
    if (ret == 0 && (target->filesystem == STORAGE_FILESYSTEM_EXFAT ||
                     target->filesystem == STORAGE_FILESYSTEM_EXT2)) {
        console_printf("[ntclks] installer target mounted root=/target %s_lba=%llu esp=/target/boot esp_lba=%llu disk=%u port=%u\n",
                       target->filesystem == STORAGE_FILESYSTEM_EXFAT ? "exfat" : "ext2",
                       (unsigned long long)(target->filesystem == STORAGE_FILESYSTEM_EXFAT
                                            ? target->exfat_start_lba : target->ext2_start_lba),
                       (unsigned long long)esp->esp_start_lba,
                       disk_id,
                       disk->port);
    } else {
        storage_memzero(target, sizeof(*target));
        storage_memzero(esp, sizeof(*esp));
        disk->target_mounted = 0;
    }
    g_active_volume = old;
    return ret;
}
