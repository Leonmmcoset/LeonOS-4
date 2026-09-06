static int fat32_mount(void)
{
    struct fat32_bpb *bpb = (struct fat32_bpb *)(void *)storage_scratch;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint32_t total_sectors;
    uint32_t data_sectors;
    int ret = storage_read_sectors(g_storage.esp_start_lba, 1, storage_scratch);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (bpb->bytes_per_sector != SECTOR_SIZE || bpb->sectors_per_cluster == 0 ||
        bpb->fat_count == 0 || bpb->fat_size32 == 0 || bpb->root_cluster < 2) {
        return -2;
    }
    fs_info_sector = bpb->fs_info;
    backup_boot_sector = bpb->backup_boot_sector;
    g_storage.bytes_per_sector = bpb->bytes_per_sector;
    g_storage.sectors_per_cluster = bpb->sectors_per_cluster;
    g_storage.cluster_bytes = bpb->bytes_per_sector * bpb->sectors_per_cluster;
    g_storage.fat_count = bpb->fat_count;
    g_storage.fat_start_sector = bpb->reserved_sector_count;
    g_storage.fat_sector_count = bpb->fat_size32;
    g_storage.data_start_sector = bpb->reserved_sector_count + (uint32_t)bpb->fat_count * bpb->fat_size32;
    g_storage.root_cluster = bpb->root_cluster;
    g_storage.next_free_cluster = bpb->root_cluster + 1u;
    g_storage.fat_fs_info_sector = 0;
    g_storage.fat_backup_boot_sector = 0;
    g_storage.fat_free_clusters = 0;
    g_storage.fat_fsinfo_valid = 0;
    total_sectors = bpb->total_sectors32 ? bpb->total_sectors32 : bpb->total_sectors16;
    g_storage.total_sectors = total_sectors;
    if (total_sectors <= g_storage.data_start_sector ||
        (g_storage.esp_sector_count && total_sectors > g_storage.esp_sector_count)) {
        return -2;
    }
    data_sectors = total_sectors - g_storage.data_start_sector;
    g_storage.data_cluster_count = data_sectors / g_storage.sectors_per_cluster;
    if (g_storage.cluster_bytes > sizeof(storage_cluster_buf)) {
        console_printf("[ntclks] storage FAT32 cluster too large=%u\n", g_storage.cluster_bytes);
        return -2;
    }
    if (fs_info_sector > 0 && fs_info_sector < bpb->reserved_sector_count &&
        storage_read_sectors(g_storage.esp_start_lba + fs_info_sector, 1u,
                             storage_scratch) == 0 &&
        storage_get_u32(storage_scratch) == 0x41615252u &&
        storage_get_u32(storage_scratch + 484u) == 0x61417272u &&
        storage_get_u32(storage_scratch + 508u) == 0xaa550000u) {
        uint32_t free_clusters = storage_get_u32(storage_scratch + 488u);
        uint32_t next_free = storage_get_u32(storage_scratch + 492u);
        if (free_clusters <= g_storage.data_cluster_count) {
            g_storage.fat_fs_info_sector = fs_info_sector;
            g_storage.fat_backup_boot_sector = backup_boot_sector;
            g_storage.fat_free_clusters = free_clusters;
            g_storage.fat_fsinfo_valid = 1;
            if (next_free >= 2u && next_free <= g_storage.data_cluster_count + 1u) {
                g_storage.next_free_cluster = next_free;
            }
        }
    }
    /* Installer targets clone the exFAT/ext2 root volume before mounting its
     * ESP as the target ESP. Always set the filesystem kind here so later path
     * lookups do not accidentally dispatch FAT32 paths through another backend. */
    g_storage.filesystem = STORAGE_FILESYSTEM_FAT32;
    return 0;
}

static int fat32_read_fat_entry(uint32_t cluster, uint32_t *out_next)
{
    uint32_t sector = fat_sector_for_cluster(cluster);
    uint32_t offset = fat_offset_for_cluster(cluster);
    uint64_t fat_start = g_storage.esp_start_lba + g_storage.fat_start_sector;
    uint64_t fat_end = fat_start + g_storage.fat_sector_count;
    uint64_t cache_start;
    uint32_t cache_sectors;
    if (!out_next || sector < fat_start || sector >= fat_end) {
        return -22;
    }
    if (!storage_fat_cache.valid || storage_fat_cache.volume != g_active_volume ||
        sector < storage_fat_cache.first_lba ||
        sector >= storage_fat_cache.first_lba + storage_fat_cache.sector_count) {
        cache_start = fat_start +
                      ((sector - fat_start) / STORAGE_FAT_CACHE_SECTORS) *
                          STORAGE_FAT_CACHE_SECTORS;
        cache_sectors = (uint32_t)min_u64(STORAGE_FAT_CACHE_SECTORS,
                                          fat_end - cache_start);
        storage_fat_cache.valid = 0;
        int ret = storage_read_sectors(cache_start, cache_sectors, storage_fat_cache_data);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        storage_fat_cache.volume = g_active_volume;
        storage_fat_cache.first_lba = cache_start;
        storage_fat_cache.sector_count = cache_sectors;
        storage_fat_cache.valid = 1;
    }
    offset += (uint32_t)(sector - storage_fat_cache.first_lba) * SECTOR_SIZE;
    if (offset + sizeof(uint32_t) > storage_fat_cache.sector_count * SECTOR_SIZE) {
        console_printf("[storage] fat entry range cluster=%u sector=%llu offset=%u cache_sectors=%u\n",
                       cluster, (unsigned long long)sector, offset,
                       storage_fat_cache.sector_count);
        return -5;
    }
    uint32_t value = *(const uint32_t *)(const void *)(storage_fat_cache_data + offset);
    *out_next = value & 0x0fffffffu;
    return 0;
}

static int storage_read_cache_lookup(uint32_t cluster, uint32_t *out_offset,
                                     uint32_t *out_clusters)
{
    uint64_t lba;
    uint32_t sector_offset;
    uint32_t sectors_left;
    if (!storage_read_cache.valid || storage_read_cache.volume != g_active_volume ||
        !out_offset || !out_clusters || cluster < 2) {
        return 0;
    }
    lba = cluster_to_lba(cluster);
    if (lba < storage_read_cache.first_lba ||
        lba >= storage_read_cache.first_lba + storage_read_cache.sector_count) {
        return 0;
    }
    sector_offset = (uint32_t)(lba - storage_read_cache.first_lba);
    sectors_left = storage_read_cache.sector_count - sector_offset;
    if (sector_offset % g_storage.sectors_per_cluster ||
        sectors_left < g_storage.sectors_per_cluster) {
        return 0;
    }
    *out_offset = sector_offset * SECTOR_SIZE;
    *out_clusters = sectors_left / g_storage.sectors_per_cluster;
    return 1;
}

static int storage_read_contiguous_clusters(uint32_t first_cluster,
                                            uint32_t max_clusters,
                                            uint32_t *out_clusters,
                                            uint32_t *out_next)
{
    uint32_t cluster = first_cluster;
    uint32_t count = 1;
    uint32_t next = FAT32_EOC;
    if (!out_clusters || !out_next || first_cluster < 2 || max_clusters == 0) {
        return -22;
    }
    for (;;) {
        int ret = fat32_read_fat_entry(cluster, &next);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        if (count >= max_clusters || next >= FAT32_EOC || next != cluster + 1u) {
            *out_clusters = count;
            *out_next = next;
            return 0;
        }
        cluster = next;
        ++count;
    }
}

static int storage_read_cache_fill(uint32_t first_cluster, uint32_t clusters)
{
    uint32_t sectors;
    uint64_t lba;
    if (first_cluster < 2 || clusters == 0 ||
        clusters > STORAGE_READAHEAD_SECTORS / g_storage.sectors_per_cluster) {
        return -22;
    }
    sectors = clusters * g_storage.sectors_per_cluster;
    lba = cluster_to_lba(first_cluster);
    storage_read_cache.valid = 0;
    int ret = storage_read_sectors(lba, sectors, storage_read_cache_data);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    storage_read_cache.volume = g_active_volume;
    storage_read_cache.first_lba = lba;
    storage_read_cache.sector_count = sectors;
    storage_read_cache.valid = 1;
    return 0;
}

static int fat32_read_lookup_cluster(uint32_t cluster, void *buffer)
{
    if (cluster < 2 || !buffer || g_storage.cluster_bytes == 0 ||
        g_storage.cluster_bytes > sizeof(storage_dir_lookup_cache_data)) {
        return -22;
    }
    if (storage_dir_lookup_cache.valid &&
        storage_dir_lookup_cache.volume == g_active_volume &&
        storage_dir_lookup_cache.cluster == cluster) {
        storage_memcpy(buffer, storage_dir_lookup_cache_data, g_storage.cluster_bytes);
        return 0;
    }
    int ret = storage_read_sectors(cluster_to_lba(cluster), g_storage.sectors_per_cluster,
                                   storage_dir_lookup_cache_data);
    if (ret < 0) {
        storage_dir_lookup_cache.valid = 0;
        return storage_read_failure(ret);
    }
    storage_dir_lookup_cache.volume = g_active_volume;
    storage_dir_lookup_cache.cluster = cluster;
    storage_dir_lookup_cache.valid = 1;
    storage_memcpy(buffer, storage_dir_lookup_cache_data, g_storage.cluster_bytes);
    return 0;
}

static int fat32_write_fat_entry(uint32_t cluster, uint32_t value)
{
    uint32_t sector_offset = (cluster * 4u) / g_storage.bytes_per_sector;
    uint32_t offset = fat_offset_for_cluster(cluster);
    uint32_t masked = value & 0x0fffffffu;
    uint64_t first_fat_lba = g_storage.esp_start_lba + g_storage.fat_start_sector +
                             sector_offset;
    /* The sector read below supplies data for a persistent FAT update. Once
     * this point is reached the operation must finish synchronously: replaying
     * a partially updated FAT chain after EAGAIN is not safe. */
    storage_begin_mutation();
    for (uint32_t fat = 0; fat < g_storage.fat_count; ++fat) {
        uint64_t lba = g_storage.esp_start_lba + g_storage.fat_start_sector +
                       (uint64_t)fat * g_storage.fat_sector_count + sector_offset;
        int ret = storage_read_sectors(lba, 1, storage_scratch);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        *(uint32_t *)(void *)(storage_scratch + offset) =
            (*(uint32_t *)(void *)(storage_scratch + offset) & 0xf0000000u) | masked;
        ret = storage_write_sectors(lba, 1, storage_scratch);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        /* Keep the cache associated with the primary FAT, and only publish
         * the new contents after the media write succeeded.  Publishing the
         * pre-write buffer makes the allocator see an entry as free again;
         * publishing the backup FAT would associate primary-FAT metadata with
         * the wrong sector when the two copies differ. */
        if (fat == 0u) {
            storage_memcpy(storage_fat_cache_data, storage_scratch, SECTOR_SIZE);
        }
    }
    /* storage_write_sectors() invalidates the read cache. Re-seed the FAT
     * cache with the just-written sector so freeing/appending a long chain
     * does not issue a disk read for every adjacent FAT entry. */
    if (g_storage.fat_count == 0u) {
        return -22;
    }
    storage_fat_cache.volume = g_active_volume;
    storage_fat_cache.first_lba = first_fat_lba;
    storage_fat_cache.sector_count = 1;
    storage_fat_cache.valid = 1;
    return 0;
}

static int fat32_write_fsinfo(void)
{
    uint64_t primary_lba;
    if (!g_storage.fat_fsinfo_valid) {
        return 0;
    }
    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_put_u32(storage_scratch, 0x41615252u);
    storage_put_u32(storage_scratch + 484u, 0x61417272u);
    storage_put_u32(storage_scratch + 488u, g_storage.fat_free_clusters);
    storage_put_u32(storage_scratch + 492u, g_storage.next_free_cluster);
    storage_put_u32(storage_scratch + 508u, 0xaa550000u);
    primary_lba = g_storage.esp_start_lba + g_storage.fat_fs_info_sector;
    int ret = storage_write_sectors(primary_lba, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    if (g_storage.fat_backup_boot_sector &&
        (uint32_t)g_storage.fat_backup_boot_sector + g_storage.fat_fs_info_sector <
            g_storage.fat_start_sector &&
        (ret = storage_write_sectors(g_storage.esp_start_lba + g_storage.fat_backup_boot_sector +
                                     g_storage.fat_fs_info_sector,
                                     1u, storage_scratch)) < 0) {
        return ret;
    }
    return 0;
}

static int fat32_note_allocated(void)
{
    if (g_storage.fat_fsinfo_valid && g_storage.fat_free_clusters > 0) {
        --g_storage.fat_free_clusters;
    }
    return fat32_write_fsinfo();
}

static int fat32_note_freed(void)
{
    if (g_storage.fat_fsinfo_valid &&
        g_storage.fat_free_clusters < g_storage.data_cluster_count) {
        ++g_storage.fat_free_clusters;
    }
    return fat32_write_fsinfo();
}

static int fat32_read_cluster(uint32_t cluster, void *buffer)
{
    uint32_t cache_offset;
    uint32_t cache_clusters;
    if (cluster < 2) {
        return -2;
    }
    if (!buffer) {
        return -22;
    }
    if (storage_read_cache_lookup(cluster, &cache_offset, &cache_clusters)) {
        storage_memcpy(buffer, storage_read_cache_data + cache_offset,
                       g_storage.cluster_bytes);
        return 0;
    }
    return storage_read_sectors(cluster_to_lba(cluster), g_storage.sectors_per_cluster, buffer);
}

static int fat32_write_cluster(uint32_t cluster, const void *buffer)
{
    if (cluster < 2 || !buffer) {
        return -2;
    }
    return storage_write_sectors(cluster_to_lba(cluster), g_storage.sectors_per_cluster, buffer);
}

static int storage_install_identify(struct install_disk_state *disk, uint64_t *out_sectors)
{
    uint16_t *id = (uint16_t *)storage_scratch;
    int ret;
    if (!disk || !out_sectors) {
        return -22;
    }
    if (disk->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_disk_ide_device(disk, &device);
        kernel_spin_lock(&storage_transport_lock);
        ret = ide_identify_device(&device);
        if (ret < 0 || device.atapi || !device.sector_count) {
            kernel_spin_unlock(&storage_transport_lock);
            return ret < 0 ? ret : -95;
        }
        disk->ide_lba48 = device.lba48;
        disk->ide_atapi = device.atapi;
        disk->ide_command_base = device.command_base;
        disk->ide_control_base = device.control_base;
        disk->sector_count = device.sector_count;
        storage_copy_text(disk->device_model, sizeof(disk->device_model), device.model);
        *out_sectors = device.sector_count;
        kernel_spin_unlock(&storage_transport_lock);
        return 0;
    }
    if (disk->transport == STORAGE_TRANSPORT_NVME) {
        if (!disk->nvme || !disk->nvme->ready || !disk->nvme_nsid || !disk->sector_count) {
            return -95;
        }
        *out_sectors = disk->sector_count;
        return 0;
    }
    if (disk->transport != STORAGE_TRANSPORT_AHCI || !disk->hba_port) {
        return -22;
    }
    kernel_spin_lock(&storage_transport_lock);
    ret = ahci_wait_idle(disk->hba_port);
    if (ret < 0) {
        ret = storage_read_failure(ret);
        goto out_unlock;
    }
    disk->hba_port->is = 0xffffffffu;
    storage_memzero(ahci_cmd_headers, sizeof(ahci_cmd_headers));
    storage_memzero(ahci_received_fis, sizeof(ahci_received_fis));
    storage_memzero(ahci_cmd_table_buf, sizeof(ahci_cmd_table_buf));
    ahci_cmd_headers[0].flags = (uint16_t)((sizeof(struct fis_reg_h2d) / sizeof(uint32_t)) & 0x1f);
    ahci_cmd_headers[0].prdtl = 1;
    ahci_cmd_headers[0].ctba = (uint32_t)(uintptr_t)ahci_cmd_table_buf;
    {
        struct ahci_cmd_table *tbl = (struct ahci_cmd_table *)(void *)ahci_cmd_table_buf;
        struct fis_reg_h2d *fis = (struct fis_reg_h2d *)(void *)tbl->cfis;
        storage_memzero(tbl, sizeof(ahci_cmd_table_buf));
        tbl->prdt[0].dba = (uint32_t)(uintptr_t)storage_scratch;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc = SECTOR_SIZE - 1u;
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = ATA_CMD_IDENTIFY_DEVICE;
        fis->device = 0;
        fis->countl = 1;
        ret = ahci_wait_cmd_slot(disk->hba_port);
        if (ret < 0) {
            ret = storage_read_failure(ret);
            goto out_unlock;
        }
        ahci_memory_barrier();
        disk->hba_port->ci = 1u;
        for (uint32_t i = 0; i < AHCI_WAIT_SPINS; ++i) {
            if ((disk->hba_port->ci & 1u) == 0) {
                if (disk->hba_port->is & AHCI_PORT_IS_TFES) {
                    ret = -5;
                    goto out_unlock;
                }
                *out_sectors = ((uint64_t)id[103] << 48) |
                               ((uint64_t)id[102] << 32) |
                               ((uint64_t)id[101] << 16) |
                               id[100];
                if (*out_sectors == 0) {
                    *out_sectors = ((uint64_t)id[61] << 16) | id[60];
                }
                ret = *out_sectors ? 0 : -5;
                goto out_unlock;
            }
            ahci_cpu_relax();
        }
    }
    ret = -5;
out_unlock:
    kernel_spin_unlock(&storage_transport_lock);
    return ret;
}

static int fat32_is_short_compatible_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-';
}

static char fat32_upper_char(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

static int fat32_make_short_name(const char *name, uint8_t out[11])
{
    uint32_t dot = 0xffffffffu;
    uint32_t len = 0;
    if (!name || !name[0] || !out) {
        return -22;
    }
    while (name[len]) {
        if (name[len] == '.') {
            if (dot != 0xffffffffu) {
                return -22;
            }
            dot = len;
        } else if (!fat32_is_short_compatible_char(name[len])) {
            return -22;
        }
        ++len;
    }
    if (dot == 0 || len == 0) {
        return -22;
    }
    if (dot == 0xffffffffu) {
        dot = len;
    }
    if (dot > 8 || (len - dot - (dot < len ? 1u : 0u)) > 3) {
        return -22;
    }
    for (uint32_t i = 0; i < 11; ++i) {
        out[i] = ' ';
    }
    for (uint32_t i = 0; i < dot; ++i) {
        out[i] = (uint8_t)fat32_upper_char(name[i]);
    }
    if (dot < len) {
        for (uint32_t i = dot + 1, j = 8; i < len; ++i, ++j) {
            out[j] = (uint8_t)fat32_upper_char(name[i]);
        }
    }
    return 0;
}

static int fat32_short_name_eq(const uint8_t lhs[11], const uint8_t rhs[11])
{
    return storage_memcmp(lhs, rhs, 11) == 0;
}

static int fat32_find_free_cluster(uint32_t *out_cluster)
{
    uint32_t max_cluster;
    uint32_t start;
    if (!out_cluster) {
        return -22;
    }
    max_cluster = g_storage.data_cluster_count + 1u;
    start = g_storage.next_free_cluster;
    if (start < 2 || start > max_cluster) {
        start = 2;
    }
    for (uint32_t pass = 0; pass < 2; ++pass) {
        uint32_t begin = pass == 0 ? start : 2;
        uint32_t end = pass == 0 ? max_cluster : (start > 2 ? start - 1u : 1u);
        for (uint32_t cluster = begin; cluster <= end; ++cluster) {
            uint32_t value = 0;
            int ret = fat32_read_fat_entry(cluster, &value);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (value == 0) {
                *out_cluster = cluster;
                g_storage.next_free_cluster = cluster < max_cluster ? cluster + 1u : 2u;
                return 0;
            }
        }
    }
    return -28;
}

static int fat32_collect_chain(uint32_t first_cluster, uint32_t chain[FAT32_MAX_FILE_CLUSTERS], uint32_t *out_count)
{
    uint32_t count = 0;
    uint32_t cluster = first_cluster;
    if (!out_count || !chain) {
        return -22;
    }
    *out_count = 0;
    if (cluster < 2) {
        return 0;
    }
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = 0;
        if (count >= FAT32_MAX_FILE_CLUSTERS) {
            return -28;
        }
        chain[count++] = cluster;
        int ret = fat32_read_fat_entry(cluster, &next);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        if (next >= FAT32_EOC) {
            break;
        }
        cluster = next;
    }
    *out_count = count;
    return 0;
}

static int fat32_free_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    uint32_t count = 0;
    if (first_cluster >= 2 &&
        (g_storage.next_free_cluster < 2 || first_cluster < g_storage.next_free_cluster)) {
        g_storage.next_free_cluster = first_cluster;
    }
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = 0;
        if (count++ >= FAT32_MAX_FILE_CLUSTERS) {
            return -28;
        }
        int ret = fat32_read_fat_entry(cluster, &next);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        ret = fat32_write_fat_entry(cluster, 0);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        ret = fat32_note_freed();
        if (ret < 0) {
            return ret;
        }
        if (next >= FAT32_EOC) {
            break;
        }
        cluster = next;
    }
    return 0;
}
static int fat32_name_match_short(const struct fat32_dirent *de, const char *name)
{
    char short_name[LEONOS_FS_NAME_LEN];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 8 && de->name[i] != ' '; ++i) {
        char ch = (char)de->name[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (pos + 1 < sizeof(short_name)) {
            short_name[pos++] = ch;
        }
    }
    if (de->name[8] != ' ') {
        if (pos + 1 < sizeof(short_name)) {
            short_name[pos++] = '.';
        }
        for (uint32_t i = 8; i < 11 && de->name[i] != ' '; ++i) {
            char ch = (char)de->name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(short_name)) {
                short_name[pos++] = ch;
            }
        }
    }
    short_name[pos] = 0;
    return storage_text_eq_ci(short_name, name);
}

static int fat32_dirent_is_acl_metadata(const struct fat32_dirent *de)
{
    if (!de || de->attr == FAT32_ATTR_LFN ||
        (de->attr & FAT32_ATTR_DIRECTORY) != 0 ||
        (de->attr & 0x08u) != 0) {
        return 0;
    }
    return fat32_name_match_short(de, "LEONACL.SYS");
}

static void fat32_lfn_extract_utf16(const struct fat32_lfn *lfn, uint16_t *dst,
                                    uint32_t *len, uint32_t cap)
{
    const uint16_t *parts[3] = {lfn->name1, lfn->name2, lfn->name3};
    const uint32_t counts[3] = {5, 6, 2};
    for (uint32_t p = 0; p < 3; ++p) {
        for (uint32_t i = 0; i < counts[p]; ++i) {
            uint16_t ch = parts[p][i];
            if (ch == 0x0000 || ch == 0xffff) {
                return;
            }
            if (*len < cap) {
                dst[(*len)++] = ch;
            }
        }
    }
}

static void fat32_build_lfn_name(const uint16_t lfn_parts[20][13], uint32_t lfn_count,
                                 char *dst, uint32_t cap)
{
    uint16_t utf16[260];
    uint32_t len = 0;
    struct leonos_unicode_utf16_to_utf8 cmd;
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = 0;
    for (uint32_t i = 0; i < lfn_count; ++i) {
        for (uint32_t j = 0; j < 13 && len < sizeof(utf16) / sizeof(utf16[0]); ++j) {
            uint16_t ch = lfn_parts[i][j];
            if (ch == 0) {
                break;
            }
            utf16[len++] = ch;
        }
    }
    cmd.utf16 = utf16;
    cmd.utf16_len = len;
    cmd.utf8 = dst;
    cmd.utf8_capacity = cap;
    cmd.utf8_len = 0;
    if (osmlayer_unicode_utf16le_to_utf8(&cmd) < 0) {
        dst[0] = 0;
        return;
    }
    if (cmd.utf8_len >= cap) {
        dst[cap - 1] = 0;
    }
}

static int fat32_is_lfn_char_valid(char ch)
{
    unsigned char uch = (unsigned char)ch;
    if (uch < 32) {
        return 0;
    }
    switch (ch) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return 0;
    default:
        return 1;
    }
}

static int fat32_validate_name(const char *name)
{
    uint32_t len = (uint32_t)storage_strlen(name);
    uint16_t utf16[260];
    struct leonos_unicode_utf8_to_utf16 cmd;
    if (!name || !name[0] || len >= LEONOS_FS_NAME_LEN) {
        return -22;
    }
    if (storage_text_eq(name, ".") || storage_text_eq(name, "..")) {
        return -22;
    }
    if (name[len - 1] == ' ' || name[len - 1] == '.') {
        return -22;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (!fat32_is_lfn_char_valid(name[i])) {
            return -22;
        }
    }
    cmd.utf8 = name;
    cmd.utf8_len = len;
    cmd.utf16 = utf16;
    cmd.utf16_capacity = sizeof(utf16) / sizeof(utf16[0]);
    cmd.utf16_len = 0;
    if (osmlayer_unicode_utf8_to_utf16le(&cmd) < 0 ||
        cmd.utf16_len == 0 || cmd.utf16_len > 255u) {
        return -22;
    }
    return 0;
}

static void fat32_name_split(const char *name,
                             const char **base_start, uint32_t *base_len,
                             const char **ext_start, uint32_t *ext_len)
{
    uint32_t len = (uint32_t)storage_strlen(name);
    uint32_t dot = 0xffffffffu;
    for (uint32_t i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = i;
        }
    }
    *base_start = name;
    *base_len = len;
    *ext_start = 0;
    *ext_len = 0;
    if (dot != 0xffffffffu && dot != 0 && dot + 1u < len) {
        *base_len = dot;
        *ext_start = name + dot + 1u;
        *ext_len = len - dot - 1u;
    }
}

static void fat32_collect_short_fragment(const char *src, uint32_t len,
                                         char *dst, uint32_t cap)
{
    uint32_t out = 0;
    if (!dst || cap == 0) {
        return;
    }
    for (uint32_t i = 0; i < len && out + 1 < cap; ++i) {
        char ch = src[i];
        if (fat32_is_short_compatible_char(ch)) {
            dst[out++] = fat32_upper_char(ch);
        }
    }
    dst[out] = 0;
}

static int fat32_short_name_exists_in_dir(uint32_t dir_cluster, const uint8_t short_name[11])
{
    uint32_t cluster = dir_cluster;
    for (;;) {
        int ret = fat32_read_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return 0;
            }
            if (de->name[0] == 0xe5 || de->attr == FAT32_ATTR_LFN) {
                continue;
            }
            if (fat32_short_name_eq(de->name, short_name)) {
                return 1;
            }
        }
        {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC) {
                return 0;
            }
            cluster = next;
        }
    }
}

static int fat32_make_short_alias(uint32_t dir_cluster, const char *name, uint8_t out[11])
{
    const char *base_start = 0;
    const char *ext_start = 0;
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    char base_part[LEONOS_FS_NAME_LEN];
    char ext_part[4];
    fat32_name_split(name, &base_start, &base_len, &ext_start, &ext_len);
    fat32_collect_short_fragment(base_start, base_len, base_part, sizeof(base_part));
    fat32_collect_short_fragment(ext_start, ext_len, ext_part, sizeof(ext_part));
    for (uint32_t ord = 1; ord <= 999999u; ++ord) {
        uint32_t suffix_len = 0;
        uint32_t value = ord;
        char digits[6];
        uint32_t digits_len = 0;
        char tmp[8];
        uint32_t base_limit;
        uint32_t base_copy = 0;
        int exists;

        while (value > 0 && digits_len < sizeof(digits)) {
            digits[digits_len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        tmp[suffix_len++] = '~';
        while (digits_len) {
            tmp[suffix_len++] = digits[--digits_len];
        }
        if (suffix_len >= 8) {
            continue;
        }
        base_limit = 8u - suffix_len;
        for (uint32_t i = 0; i < 11; ++i) {
            out[i] = ' ';
        }
        if (!base_part[0]) {
            storage_copy_text(base_part, sizeof(base_part), "FILE");
        }
        while (base_part[base_copy] && base_copy < base_limit) {
            out[base_copy] = (uint8_t)base_part[base_copy];
            ++base_copy;
        }
        for (uint32_t i = 0; i < suffix_len; ++i) {
            out[base_copy + i] = (uint8_t)tmp[i];
        }
        for (uint32_t i = 0; ext_part[i] && i < 3; ++i) {
            out[8 + i] = (uint8_t)ext_part[i];
        }
        exists = fat32_short_name_exists_in_dir(dir_cluster, out);
        if (exists < 0) {
            return exists;
        }
        if (!exists) {
            return 0;
        }
    }
    return -28;
}

static uint8_t fat32_short_name_checksum(const uint8_t short_name[11])
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 11; ++i) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

static uint32_t fat32_utf16_name(const char *name, uint16_t *utf16, uint32_t cap)
{
    struct leonos_unicode_utf8_to_utf16 cmd = {
        .utf8 = name,
        .utf8_len = (uint32_t)storage_strlen(name),
        .utf16 = utf16,
        .utf16_capacity = cap,
        .utf16_len = 0,
    };
    if (osmlayer_unicode_utf8_to_utf16le(&cmd) < 0) {
        return 0;
    }
    return cmd.utf16_len;
}

static uint32_t fat32_lfn_entry_count(const char *name)
{
    uint16_t utf16[260];
    uint32_t len = fat32_utf16_name(name, utf16, sizeof(utf16) / sizeof(utf16[0]));
    return (len + 12u) / 13u;
}

static void fat32_fill_lfn_entry(struct fat32_lfn *lfn,
                                 const uint16_t *name, uint32_t name_len,
                                 uint32_t part_index, uint32_t part_count,
                                 uint8_t checksum)
{
    uint16_t *slots[3] = {lfn->name1, lfn->name2, lfn->name3};
    const uint32_t slot_counts[3] = {5, 6, 2};
    uint32_t cursor = part_index * 13u;
    int terminated = 0;

    storage_memzero(lfn, sizeof(*lfn));
    lfn->order = (uint8_t)(part_index + 1u);
    if (part_index + 1u == part_count) {
        lfn->order |= 0x40u;
    }
    lfn->attr = FAT32_ATTR_LFN;
    lfn->type = 0;
    lfn->checksum = checksum;
    lfn->zero = 0;

    for (uint32_t part = 0; part < 3; ++part) {
        for (uint32_t i = 0; i < slot_counts[part]; ++i) {
            uint16_t value = 0xffffu;
            if (cursor < name_len) {
                value = name[cursor++];
            } else if (!terminated) {
                value = 0x0000u;
                terminated = 1;
            }
            slots[part][i] = value;
        }
    }
}

static int fat32_find_in_dir(uint32_t dir_cluster, const char *name, struct storage_node *out)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    if (storage_dir_index_lookup(dir_cluster, name, out)) {
        return 0;
    }
    for (;;) {
        int ret = fat32_read_lookup_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                uint32_t len = 0;
                fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            int matched = 0;
            struct storage_node candidate = {
                .type = (de->attr & FAT32_ATTR_DIRECTORY) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE,
                .flags = 0,
                .first_cluster = ((uint32_t)de->first_cluster_hi << 16) | de->first_cluster_lo,
                .volume_id = g_storage.volume_id,
                .size = de->size,
            };
            if (lfn_count) {
                char full[LEONOS_FS_NAME_LEN];
                fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                storage_dir_index_store(dir_cluster, full, &candidate);
                matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
            } else {
                matched = fat32_name_match_short(de, name);
            }
            lfn_count = 0;
            if (!matched) {
                continue;
            }
            if (out) {
                *out = candidate;
            }
            return 0;
        }
        uint32_t next = 0;
        ret = fat32_read_fat_entry(cluster, &next);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        if (next >= FAT32_EOC) {
            return -2;
        }
        cluster = next;
    }
}

static int fat32_find_dirent_ref_in_dir(uint32_t dir_cluster, const char *name, struct fat32_dir_ref *out)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    for (;;) {
        int ret = fat32_read_lookup_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                {
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                }
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            {
                int matched = 0;
                if (lfn_count) {
                    char full[LEONOS_FS_NAME_LEN];
                    fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                    matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
                } else {
                    matched = fat32_name_match_short(de, name);
                }
                lfn_count = 0;
                if (!matched) {
                    continue;
                }
                if (out) {
                    out->entry_cluster = cluster;
                    out->entry_offset = off;
                    out->dirent = *de;
                }
                return 0;
            }
        }
        {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC) {
                return -2;
            }
            cluster = next;
        }
    }
}

static int fat32_update_dirent(const struct fat32_dir_ref *ref)
{
    int ret;
    if (!ref || ref->entry_cluster < 2 || ref->entry_offset + sizeof(struct fat32_dirent) > g_storage.cluster_bytes) {
        return -22;
    }
    ret = fat32_read_cluster(ref->entry_cluster, storage_cluster_buf);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    *(struct fat32_dirent *)(void *)(storage_cluster_buf + ref->entry_offset) = ref->dirent;
    ret = fat32_write_cluster(ref->entry_cluster, storage_cluster_buf);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    return 0;
}

static int fat32_name_needs_lfn(const char *name, uint8_t short_name[11], uint8_t *need_lfn)
{
    int ret = fat32_make_short_name(name, short_name);
    if (!need_lfn) {
        return -22;
    }
    *need_lfn = 0;
    if (ret == 0) {
        char rendered[LEONOS_FS_NAME_LEN];
        uint32_t pos = 0;
        for (uint32_t i = 0; i < 8 && short_name[i] != ' '; ++i) {
            char ch = (char)short_name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(rendered)) {
                rendered[pos++] = ch;
            }
        }
        if (short_name[8] != ' ' && pos + 1 < sizeof(rendered)) {
            rendered[pos++] = '.';
        }
        for (uint32_t i = 8; i < 11 && short_name[i] != ' '; ++i) {
            char ch = (char)short_name[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch - 'A' + 'a');
            }
            if (pos + 1 < sizeof(rendered)) {
                rendered[pos++] = ch;
            }
        }
        rendered[pos] = 0;
        *need_lfn = storage_text_eq(name, rendered) ? 0u : 1u;
        return 0;
    }
    *need_lfn = 1;
    return 1;
}

static int fat32_find_free_dirent_span(uint32_t dir_cluster, uint32_t slots,
                                       struct fat32_dir_span *out)
{
    uint32_t cluster = dir_cluster;
    uint32_t per_cluster = g_storage.cluster_bytes / sizeof(struct fat32_dirent);
    if (!out || slots == 0 || slots > per_cluster) {
        return -22;
    }
    for (;;) {
        uint32_t run = 0;
        uint32_t run_start = 0;
        int ret = fat32_read_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00 || de->name[0] == 0xe5) {
                if (run == 0) {
                    run_start = off;
                }
                ++run;
                if (run >= slots) {
                    out->entry_cluster = cluster;
                    out->entry_offset = run_start;
                    return 0;
                }
            } else {
                run = 0;
            }
        }
        {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC) {
                uint32_t new_cluster = 0;
                ret = fat32_find_free_cluster(&new_cluster);
                if (ret < 0) {
                    return storage_read_failure(ret);
                }
                ret = fat32_write_fat_entry(cluster, new_cluster);
                if (ret < 0) {
                    return storage_read_failure(ret);
                }
                ret = fat32_write_fat_entry(new_cluster, FAT32_EOC);
                if (ret < 0) {
                    return storage_read_failure(ret);
                }
                ret = fat32_note_allocated();
                if (ret < 0) {
                    return ret;
                }
                storage_memzero(storage_cluster_buf, g_storage.cluster_bytes);
                ret = fat32_write_cluster(new_cluster, storage_cluster_buf);
                if (ret < 0) {
                    return storage_read_failure(ret);
                }
                out->entry_cluster = new_cluster;
                out->entry_offset = 0;
                return 0;
            }
            cluster = next;
        }
    }
}

static int fat32_create_dirent(uint32_t parent_cluster, const char *name, uint8_t attr,
                               uint32_t first_cluster, uint32_t size)
{
    struct fat32_dir_span span;
    struct fat32_dirent dirent;
    uint8_t short_name[11];
    uint8_t need_lfn = 0;
    uint32_t lfn_count = 0;
    int ret;

    ret = fat32_name_needs_lfn(name, short_name, &need_lfn);
    if (ret < 0) {
        return ret;
    }
    if (need_lfn) {
        ret = fat32_make_short_alias(parent_cluster, name, short_name);
        if (ret < 0) {
            return ret;
        }
        lfn_count = fat32_lfn_entry_count(name);
        if (lfn_count > 20u) {
            return -22;
        }
    }
    ret = fat32_find_free_dirent_span(parent_cluster, lfn_count + 1u, &span);
    if (ret < 0) {
        return ret;
    }
    storage_memzero(&dirent, sizeof(dirent));
    storage_memcpy(dirent.name, short_name, 11);
    dirent.attr = attr;
    dirent.first_cluster_hi = (uint16_t)(first_cluster >> 16);
    dirent.first_cluster_lo = (uint16_t)(first_cluster & 0xffffu);
    dirent.size = size;

    ret = fat32_read_cluster(span.entry_cluster, storage_cluster_buf);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (need_lfn) {
        uint16_t utf16_name[260];
        uint32_t name_len = fat32_utf16_name(name, utf16_name,
                                             sizeof(utf16_name) / sizeof(utf16_name[0]));
        uint8_t checksum = fat32_short_name_checksum(short_name);
        for (uint32_t i = 0; i < lfn_count; ++i) {
            struct fat32_lfn *lfn = (struct fat32_lfn *)(void *)(storage_cluster_buf +
                span.entry_offset + i * sizeof(struct fat32_dirent));
            uint32_t part = lfn_count - i - 1u;
            fat32_fill_lfn_entry(lfn, utf16_name, name_len, part, lfn_count, checksum);
        }
    }
    *(struct fat32_dirent *)(void *)(storage_cluster_buf +
        span.entry_offset + lfn_count * sizeof(struct fat32_dirent)) = dirent;
    ret = fat32_write_cluster(span.entry_cluster, storage_cluster_buf);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    return 0;
}

static int fat32_delete_dirent(uint32_t dir_cluster, const char *name,
                               struct fat32_dirent *deleted)
{
    uint32_t cluster = dir_cluster;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    uint32_t lfn_start = 0xffffffffu;
    for (;;) {
        int ret = fat32_read_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                lfn_start = 0xffffffffu;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (lfn_start == 0xffffffffu) {
                    lfn_start = off;
                }
                if (order == 0 || order > 20) {
                    lfn_count = 0;
                    continue;
                }
                storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                {
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                }
                if (order > lfn_count) {
                    lfn_count = order;
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                lfn_start = 0xffffffffu;
                continue;
            }
            {
                int matched;
                if (lfn_count) {
                    char full[LEONOS_FS_NAME_LEN];
                    fat32_build_lfn_name(lfn_parts, lfn_count, full, sizeof(full));
                    matched = storage_text_eq_ci(full, name) || fat32_name_match_short(de, name);
                } else {
                    matched = fat32_name_match_short(de, name);
                }
                if (!matched) {
                    lfn_count = 0;
                    lfn_start = 0xffffffffu;
                    continue;
                }
                if (deleted) {
                    *deleted = *de;
                }
                uint32_t start = lfn_start == 0xffffffffu ? off : lfn_start;
                for (uint32_t clear = start; clear <= off; clear += sizeof(struct fat32_dirent)) {
                    struct fat32_dirent *clear_de =
                        (struct fat32_dirent *)(void *)(storage_cluster_buf + clear);
                    clear_de->name[0] = 0xe5u;
                }
                ret = fat32_write_cluster(cluster, storage_cluster_buf);
                if (ret < 0) {
                    return storage_read_failure(ret);
                }
                return 0;
            }
        }
        {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC) {
                return -2;
            }
            cluster = next;
        }
    }
}

static int fat32_delete_acl_metadata_file(uint32_t dir_cluster)
{
    struct storage_node meta;
    int ret = fat32_find_in_dir(dir_cluster, "LEONACL.SYS", &meta);
    if (ret == -2) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    if (meta.type != LEONOS_FS_TYPE_FILE) {
        return 0;
    }
    ret = fat32_delete_dirent(dir_cluster, "LEONACL.SYS", 0);
    if (ret < 0) {
        return ret;
    }
    if (meta.first_cluster >= 2) {
        ret = fat32_free_chain(meta.first_cluster);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int fat32_dir_is_empty(uint32_t dir_cluster)
{
    uint32_t cluster = dir_cluster;
    for (;;) {
        int ret = fat32_read_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = 0; off < g_storage.cluster_bytes; off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                return 1;
            }
            if (de->name[0] == 0xe5 || de->attr == FAT32_ATTR_LFN || (de->attr & 0x08u) != 0) {
                continue;
            }
            if (fat32_dirent_is_acl_metadata(de)) {
                continue;
            }
            if (de->name[0] == '.' &&
                (de->name[1] == ' ' || (de->name[1] == '.' && de->name[2] == ' '))) {
                continue;
            }
            return 0;
        }
        {
            uint32_t next = 0;
            ret = fat32_read_fat_entry(cluster, &next);
            if (ret < 0) {
                return storage_read_failure(ret);
            }
            if (next >= FAT32_EOC) {
                return 1;
            }
            cluster = next;
        }
    }
}

static int fat32_iter_dir_entry(uint32_t dir_cluster, uint64_t index, struct leonos_dir_entry *entry)
{
    uint32_t cluster = dir_cluster;
    uint64_t emitted = 0;
    uint16_t lfn_parts[20][13];
    uint32_t lfn_count = 0;
    uint32_t first_offset = 0;
    if (!entry || dir_cluster < 2) {
        return -22;
    }
    if (storage_dir_iter_cache.valid &&
        storage_dir_iter_cache.volume == g_active_volume &&
        storage_dir_iter_cache.first_cluster == dir_cluster &&
        storage_dir_iter_cache.next_index == index) {
        cluster = storage_dir_iter_cache.cluster;
        first_offset = storage_dir_iter_cache.entry_offset;
        emitted = index;
    } else {
        storage_dir_iter_cache.valid = 0;
    }
    for (;;) {
        int ret = fat32_read_cluster(cluster, storage_cluster_buf);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        for (uint32_t off = first_offset; off < g_storage.cluster_bytes;
             off += sizeof(struct fat32_dirent)) {
            struct fat32_dirent *de = (struct fat32_dirent *)(void *)(storage_cluster_buf + off);
            if (de->name[0] == 0x00) {
                storage_dir_iter_cache.valid = 0;
                return -2;
            }
            if (de->name[0] == 0xe5) {
                lfn_count = 0;
                continue;
            }
            if (de->attr == FAT32_ATTR_LFN) {
                const struct fat32_lfn *lfn = (const struct fat32_lfn *)(const void *)de;
                uint8_t order = lfn->order & 0x1fu;
                if (order && order <= 20) {
                    storage_memzero(lfn_parts[order - 1], sizeof(lfn_parts[0]));
                    uint32_t len = 0;
                    fat32_lfn_extract_utf16(lfn, lfn_parts[order - 1], &len, 13);
                    if (order > lfn_count) {
                        lfn_count = order;
                    }
                }
                continue;
            }
            if ((de->attr & 0x08u) != 0) {
                lfn_count = 0;
                continue;
            }
            char name[LEONOS_FS_NAME_LEN];
            if (lfn_count) {
                fat32_build_lfn_name(lfn_parts, lfn_count, name, sizeof(name));
            } else {
                struct storage_node tmp;
                (void)tmp;
                uint32_t pos = 0;
                for (uint32_t i = 0; i < 8 && de->name[i] != ' '; ++i) {
                    char ch = (char)de->name[i];
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (char)(ch - 'A' + 'a');
                    }
                    if (pos + 1 < sizeof(name)) {
                        name[pos++] = ch;
                    }
                }
                if (de->name[8] != ' ' && pos + 1 < sizeof(name)) {
                    name[pos++] = '.';
                }
                for (uint32_t i = 8; i < 11 && de->name[i] != ' '; ++i) {
                    char ch = (char)de->name[i];
                    if (ch >= 'A' && ch <= 'Z') {
                        ch = (char)(ch - 'A' + 'a');
                    }
                    if (pos + 1 < sizeof(name)) {
                        name[pos++] = ch;
                    }
                }
                name[pos] = 0;
            }
            lfn_count = 0;
            if ((name[0] == '.' && name[1] == 0) ||
                (name[0] == '.' && name[1] == '.' && name[2] == 0)) {
                continue;
            }
            if (storage_is_acl_metadata_name(name)) {
                continue;
            }
            if (emitted++ != index) {
                continue;
            }
            entry->type = (de->attr & FAT32_ATTR_DIRECTORY) ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
            storage_copy_text(entry->name, sizeof(entry->name), name);
            storage_dir_iter_cache.volume = g_active_volume;
            storage_dir_iter_cache.first_cluster = dir_cluster;
            storage_dir_iter_cache.cluster = cluster;
            storage_dir_iter_cache.entry_offset = off + sizeof(struct fat32_dirent);
            storage_dir_iter_cache.next_index = index + 1u;
            if (storage_dir_iter_cache.entry_offset >= g_storage.cluster_bytes) {
                uint32_t next = 0;
                if (fat32_read_fat_entry(cluster, &next) < 0 || next >= FAT32_EOC) {
                    storage_dir_iter_cache.valid = 0;
                } else {
                    storage_dir_iter_cache.cluster = next;
                    storage_dir_iter_cache.entry_offset = 0;
                    storage_dir_iter_cache.valid = 1;
                }
            } else {
                storage_dir_iter_cache.valid = 1;
            }
            return 0;
        }
        uint32_t next = 0;
        ret = fat32_read_fat_entry(cluster, &next);
        if (ret < 0) {
            return storage_read_failure(ret);
        }
        if (next >= FAT32_EOC) {
            return -2;
        }
        cluster = next;
        first_offset = 0;
    }
}
