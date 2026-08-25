struct disk_gpt_table {
    struct gpt_header primary;
    struct gpt_header backup;
    uint32_t table_bytes;
    uint32_t table_sectors;
};

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
        header->header_size != sizeof(struct gpt_header) ||
        header->current_lba >= sector_count || header->backup_lba >= sector_count ||
        header->backup_lba == header->current_lba ||
        header->first_usable_lba > header->last_usable_lba ||
        header->last_usable_lba >= sector_count ||
        header->partition_entry_count == 0 ||
        header->partition_entry_count > LEONOS_DISK_MAX_PARTITIONS ||
        header->partition_entry_size != sizeof(struct gpt_entry)) {
        return -22;
    }
    table_bytes = (uint64_t)header->partition_entry_count * header->partition_entry_size;
    table_sectors = (table_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    table_last_lba = header->partition_entries_lba + table_sectors - 1u;
    if (table_bytes > sizeof(storage_cluster_buf) ||
        header->partition_entries_lba < 2u ||
        header->partition_entries_lba >= sector_count ||
        table_sectors > sector_count - header->partition_entries_lba ||
        (header->current_lba >= header->first_usable_lba &&
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
 * @brief Prepares an AHCI disk for a synchronous disk-management operation.
 * @param disk_id Detected AHCI disk index.
 * @param out_disk Receives the selected AHCI disk state.
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
        return -22;
    }
    disk = &g_install_disks[disk_id];
    if (disk->transport != STORAGE_TRANSPORT_IDE_PIO &&
        ahci_setup_port(disk->hba_port) < 0) {
        return -5;
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
 * @param disk Prepared AHCI disk state.
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
    ret = install_read_sectors(disk, primary.backup_lba, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memcpy(&backup, storage_scratch, sizeof(backup));
    ret = disk_gpt_header_valid(&backup, sector_count);
    if (ret < 0 || backup.current_lba != primary.backup_lba ||
        backup.backup_lba != primary.current_lba ||
        backup.partition_entry_count != primary.partition_entry_count ||
        backup.partition_entry_size != primary.partition_entry_size ||
        backup.first_usable_lba != primary.first_usable_lba ||
        backup.last_usable_lba != primary.last_usable_lba ||
        backup.partition_entries_crc32 != primary.partition_entries_crc32) {
        return ret < 0 ? ret : -22;
    }
    table_bytes = primary.partition_entry_count * primary.partition_entry_size;
    table_sectors = (table_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
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

/**
 * @brief Writes one updated GPT header to a managed disk.
 * @param disk Prepared AHCI disk state.
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
 * @param disk Prepared AHCI disk state.
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
    return disk_gpt_write_header(disk, &table->primary);
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
 * @param disk Prepared AHCI disk state.
 * @param entry Validated GPT partition entry.
 * @return A LEONOS_DISK_FILESYSTEM value.
 */
static uint32_t disk_partition_filesystem(struct install_disk_state *disk,
                                          const struct gpt_entry *entry)
{
    uint64_t sectors;
    if (!disk || !entry || entry->last_lba < entry->first_lba) {
        return LEONOS_DISK_FILESYSTEM_UNKNOWN;
    }
    sectors = entry->last_lba - entry->first_lba + 1u;
    if (install_read_sectors(disk, entry->first_lba, 1u, storage_scratch) < 0) {
        return LEONOS_DISK_FILESYSTEM_UNKNOWN;
    }
    if (storage_scratch[510] == 0x55 && storage_scratch[511] == 0xaa &&
        storage_memcmp(storage_scratch + 82u, "FAT32   ", 8u) == 0) {
        return LEONOS_DISK_FILESYSTEM_FAT32;
    }
    if (sectors > 2u && install_read_sectors(disk, entry->first_lba + 2u, 1u,
                                              storage_scratch) == 0) {
        const struct ext2_superblock *super =
            (const struct ext2_superblock *)(const void *)storage_scratch;
        if (super->magic == EXT2_SUPER_MAGIC) {
            return LEONOS_DISK_FILESYSTEM_EXT2;
        }
    }
    return LEONOS_DISK_FILESYSTEM_UNKNOWN;
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
 * @param disk_id AHCI disk identifier.
 * @param disk Prepared AHCI disk state.
 * @param entry_index GPT entry index.
 * @param entry GPT partition entry.
 * @param out Destination public partition record.
 */
static void disk_partition_export(uint32_t disk_id, const struct install_disk_state *disk,
                                  uint32_t entry_index, const struct gpt_entry *entry,
                                  struct leonos_disk_partition *out)
{
    if (!disk || !entry || !out) {
        return;
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
    out->filesystem = disk_partition_filesystem((struct install_disk_state *)disk, entry);
}

/**
 * @brief Checks whether an operation may mutate a disk-management partition.
 * @param disk AHCI disk state that owns the partition.
 * @return Zero if the disk is not a startup or mounted installer target.
 */
static int disk_partition_mutable(const struct install_disk_state *disk)
{
    if (!disk) {
        return -22;
    }
    return disk->boot_root || disk->target_mounted ? -1 : 0;
}

/**
 * @brief Checks whether a runtime data mount belongs to an AHCI disk.
 * @param disk_id AHCI disk identifier to test.
 * @return Nonzero when a dynamic data mount references @p disk_id.
 */
static int storage_disk_has_data_mount(uint32_t disk_id)
{
    for (uint32_t volume_id = STORAGE_VOLUME_DYNAMIC_FIRST;
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
 * @return Nonzero when the selection is FAT32 or ext2.
 */
static int disk_filesystem_format_supported(uint32_t filesystem)
{
    return filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ||
           filesystem == LEONOS_DISK_FILESYSTEM_EXT2;
}

/**
 * @brief Formats a validated data extent using the selected LeonOS filesystem.
 * @param disk Prepared AHCI disk state.
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
    return filesystem == LEONOS_DISK_FILESYSTEM_FAT32
               ? install_format_fat32(disk, entry->first_lba, sector_count)
               : install_format_ext2(disk, entry->first_lba, sector_count);
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
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        storage_memcpy(entry->type_guid, basic_data_guid, sizeof(entry->type_guid));
    } else if (filesystem == LEONOS_DISK_FILESYSTEM_EXT2) {
        storage_memcpy(entry->type_guid, linux_filesystem_guid, sizeof(entry->type_guid));
    }
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
 * @param disk_id Detected AHCI disk identifier.
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
    int ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    if (!out_count || (capacity && !partitions)) {
        return -22;
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
        return ret == -2 ? 0 : ret;
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
            disk_partition_export(disk_id, disk, i, &entries[i], &partitions[count]);
        }
        ++count;
    }
    *out_count = count;
    return 0;
}

/**
 * @brief Formats an existing unprotected GPT partition as FAT32 or ext2.
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
    if (ret < 0 || disk_partition_mutable(disk) < 0) {
        return ret < 0 ? ret : -1;
    }
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
    if (ret < 0 || disk_partition_mutable(disk) < 0) {
        return ret < 0 ? ret : -1;
    }
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
    if (!request || !disk_filesystem_format_supported(request->filesystem) ||
        request->size_mib == 0) {
        return -22;
    }
    required_sectors = (uint64_t)request->size_mib * 2048u;
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0 || disk_partition_mutable(disk) < 0) {
        return ret < 0 ? ret : -1;
    }
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
    return disk_format_entry(disk, &entries[free_index], request->filesystem);
}

int storage_disk_partition_volume_id(uint32_t disk_id, uint32_t partition_index,
                                     uint32_t *out_volume_id)
{
    if (!out_volume_id || partition_index >= LEONOS_DISK_MAX_PARTITIONS) {
        return -22;
    }
    for (uint32_t volume_id = STORAGE_VOLUME_DYNAMIC_FIRST;
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

/** Mounts one supported data partition at its deterministic Unix path. */
int storage_disk_mount_partition(struct leonos_disk_partition_mount *request)
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

    if (!request || request->mount_path[0] != 0) {
        return -22;
    }
    ret = storage_acquire_task_io();
    if (ret < 0) {
        return ret;
    }
    ret = disk_manage_prepare(request->disk_id, &disk, &sector_count);
    if (ret < 0 || disk_partition_mutable(disk) < 0) {
        return ret < 0 ? ret : -1;
    }
    ret = disk_gpt_load(disk, sector_count, &table);
    if (ret < 0 || request->partition_index >= table.primary.partition_entry_count) {
        return ret < 0 ? ret : -22;
    }
    ret = disk_gpt_entries_valid(&table);
    if (ret < 0) {
        return ret;
    }
    {
        const struct gpt_entry *entries = (const struct gpt_entry *)(const void *)storage_cluster_buf;
        if (!disk_gpt_entry_used(&entries[request->partition_index])) {
            return -2;
        }
        entry = entries[request->partition_index];
    }
    if (storage_disk_partition_mount_path(request->disk_id, request->partition_index,
                                          request->mount_path,
                                          sizeof(request->mount_path)) == 0) {
        return 0;
    }
    filesystem = disk_partition_filesystem(disk, &entry);
    if (!disk_filesystem_format_supported(filesystem)) {
        return -95;
    }
    volume_id = storage_next_data_mount_volume();
    if (volume_id == STORAGE_MAX_VOLUMES) {
        return -28;
    }

    volume = &g_volumes[volume_id];
    old_volume = g_active_volume;
    storage_cache_invalidate();
    storage_memzero(volume, sizeof(*volume));
    volume->volume_id = (uint8_t)volume_id;
    volume->kind = disk->transport == STORAGE_TRANSPORT_IDE_PIO
                       ? STORAGE_VOLUME_IDE : STORAGE_VOLUME_AHCI;
    volume->bus = disk->bus;
    volume->slot = disk->slot;
    volume->function = disk->function;
    volume->port = disk->port;
    volume->transport = disk->transport;
    volume->ide_channel = disk->ide_channel;
    volume->ide_drive = disk->ide_drive;
    volume->ide_atapi = disk->ide_atapi;
    volume->ide_lba48 = disk->ide_lba48;
    volume->ide_command_base = disk->ide_command_base;
    volume->ide_control_base = disk->ide_control_base;
    storage_copy_text(volume->device_model, sizeof(volume->device_model), disk->device_model);
    volume->abar = disk->abar;
    volume->hba_port = disk->hba_port;
    volume->source_disk_id = request->disk_id;
    volume->source_partition_index = request->partition_index;
    if (storage_set_data_mount_path(volume, request->disk_id, request->partition_index) < 0) {
        storage_memzero(volume, sizeof(*volume));
        return -22;
    }
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        volume->esp_start_lba = entry.first_lba;
        volume->esp_sector_count = entry.last_lba - entry.first_lba + 1u;
    } else {
        volume->ext2_start_lba = entry.first_lba;
        volume->ext2_sector_count = entry.last_lba - entry.first_lba + 1u;
    }
    g_active_volume = volume;
    ret = filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ? fat32_mount() : ext2_mount();
    if (ret == 0 &&
        ((filesystem == LEONOS_DISK_FILESYSTEM_FAT32 &&
          volume->filesystem != STORAGE_FILESYSTEM_FAT32) ||
         (filesystem == LEONOS_DISK_FILESYSTEM_EXT2 &&
          volume->filesystem != STORAGE_FILESYSTEM_EXT2))) {
        ret = -5;
    }
    if (ret == 0) {
        volume->data_partition_mount = 1;
        volume->ready = true;
        storage_copy_text(request->mount_path, sizeof(request->mount_path), volume->mount_path);
        console_printf("[ntclks] storage mounted data partition disk=%u entry=%u path=%s fs=%s\\n",
                       request->disk_id, request->partition_index, volume->mount_path,
                       filesystem == LEONOS_DISK_FILESYSTEM_FAT32 ? "fat32" : "ext2");
    } else {
        storage_memzero(volume, sizeof(*volume));
    }
    g_active_volume = old_volume;
    storage_cache_invalidate();
    return ret;
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
    if (volume_id < STORAGE_VOLUME_DYNAMIC_FIRST || volume_id >= STORAGE_MAX_VOLUMES ||
        !g_volumes[volume_id].data_partition_mount) {
        return -2;
    }
    if (g_active_volume == &g_volumes[volume_id]) {
        g_active_volume = &g_volumes[0];
    }
    storage_memzero(&g_volumes[volume_id], sizeof(g_volumes[volume_id]));
    storage_cache_invalidate();
    console_printf("[ntclks] storage unmounted data partition disk=%u entry=%u\\n",
                   request->disk_id, request->partition_index);
    return 0;
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
        disks[i].port = src->port;
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
                              ? "IDE/PATA Disk" : "SATA/AHCI Disk");
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
    if (disk->transport != STORAGE_TRANSPORT_IDE_PIO &&
        ahci_setup_port(disk->hba_port) < 0) {
        return -5;
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
    ret = install_format_ext2(disk, root_lba, root_sectors);
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
    return 0;
}

/**
 * @brief Preserves the historical installer formatting ABI.
 * @param disk_id Installer disk identifier.
 * @return Zero after formatting the ESP plus ext2 root, or a negative error.
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
    target->kind = disk->transport == STORAGE_TRANSPORT_IDE_PIO
                       ? STORAGE_VOLUME_IDE : STORAGE_VOLUME_AHCI;
    target->bus = disk->bus;
    target->slot = disk->slot;
    target->function = disk->function;
    target->port = disk->port;
    target->transport = disk->transport;
    target->ide_channel = disk->ide_channel;
    target->ide_drive = disk->ide_drive;
    target->ide_atapi = disk->ide_atapi;
    target->ide_lba48 = disk->ide_lba48;
    target->ide_command_base = disk->ide_command_base;
    target->ide_control_base = disk->ide_control_base;
    storage_copy_text(target->device_model, sizeof(target->device_model), disk->device_model);
    target->abar = disk->abar;
    target->hba_port = disk->hba_port;
    storage_copy_text(target->mount_path, sizeof(target->mount_path), "/target");
    if (disk->transport != STORAGE_TRANSPORT_IDE_PIO &&
        ahci_setup_port(disk->hba_port) < 0) {
        g_active_volume = old;
        return -5;
    }
    g_active_volume = target;
    int ret = gpt_find_esp();
    if (ret == 0 && target->ext2_start_lba) ret = ext2_mount();
    if (ret == 0 && target->filesystem == STORAGE_FILESYSTEM_EXT2) {
        target->ready = true;
        disk->target_mounted = 1;
        storage_memzero(esp, sizeof(*esp));
        *esp = *target;
        esp->volume_id = STORAGE_VOLUME_BOOT;
        esp->ready = false;
        storage_copy_text(esp->mount_path, sizeof(esp->mount_path), "/target/boot");
        g_active_volume = esp;
        ret = fat32_mount();
        if (ret == 0) esp->ready = true;
    }
    if (ret == 0 && target->filesystem == STORAGE_FILESYSTEM_EXT2) {
        console_printf("[ntclks] installer target mounted root=/target ext2_lba=%llu esp=/target/boot esp_lba=%llu disk=%u port=%u\n",
                       (unsigned long long)target->ext2_start_lba,
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

