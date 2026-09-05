static void storage_volume_ide_device(const struct storage_volume *volume,
                                      struct ide_device_info *device)
{
    if (!device) {
        return;
    }
    storage_memzero(device, sizeof(*device));
    if (!volume) {
        return;
    }
    device->present = volume->ready || volume->ide_command_base != 0;
    device->atapi = volume->ide_atapi;
    device->lba48 = volume->ide_lba48;
    device->channel = volume->ide_channel;
    device->drive = volume->ide_drive;
    device->command_base = volume->ide_command_base;
    device->control_base = volume->ide_control_base;
    ide_fill_device(device, volume->ide_channel, volume->ide_drive,
                    volume->ide_command_base, volume->ide_control_base);
    device->present = volume->ready || volume->ide_command_base != 0;
    device->atapi = volume->ide_atapi;
    device->lba48 = volume->ide_lba48;
    /* The volume passes absolute disk LBAs; capacity is validated at the
     * installer-disk layer, so do not mistake a partition length for disk
     * capacity here. */
    device->sector_count = 0;
}

static void storage_disk_ide_device(const struct install_disk_state *disk,
                                    struct ide_device_info *device)
{
    if (!device) {
        return;
    }
    storage_memzero(device, sizeof(*device));
    if (!disk) {
        return;
    }
    ide_fill_device(device, disk->ide_channel, disk->ide_drive,
                    disk->ide_command_base, disk->ide_control_base);
    device->present = disk->present;
    device->atapi = disk->ide_atapi;
    device->lba48 = disk->ide_lba48;
    device->sector_count = disk->sector_count;
    storage_copy_text(device->model, sizeof(device->model), disk->device_model);
}

static uint8_t storage_volume_kind_for_transport(uint8_t transport)
{
    if (transport == STORAGE_TRANSPORT_IDE_PIO) {
        return STORAGE_VOLUME_IDE;
    }
    if (transport == STORAGE_TRANSPORT_NVME) {
        return STORAGE_VOLUME_NVME;
    }
    return STORAGE_VOLUME_AHCI;
}

static const char *storage_transport_name(uint8_t transport)
{
    if (transport == STORAGE_TRANSPORT_IDE_PIO) {
        return "ide-pio";
    }
    if (transport == STORAGE_TRANSPORT_NVME) {
        return "nvme";
    }
    return "ahci";
}

static void storage_volume_from_install_disk(struct storage_volume *volume,
                                             const struct install_disk_state *disk)
{
    if (!volume || !disk) {
        return;
    }
    volume->kind = storage_volume_kind_for_transport(disk->transport);
    volume->transport = disk->transport;
    volume->bus = disk->bus;
    volume->slot = disk->slot;
    volume->function = disk->function;
    volume->port = disk->port;
    volume->ide_channel = disk->ide_channel;
    volume->ide_drive = disk->ide_drive;
    volume->ide_atapi = disk->ide_atapi;
    volume->ide_lba48 = disk->ide_lba48;
    volume->ide_command_base = disk->ide_command_base;
    volume->ide_control_base = disk->ide_control_base;
    volume->abar = disk->abar;
    volume->hba_port = disk->hba_port;
    volume->nvme = disk->nvme;
    volume->nvme_nsid = disk->nvme_nsid;
    storage_copy_text(volume->device_model, sizeof(volume->device_model), disk->device_model);
}

static int storage_prepare_install_disk(struct install_disk_state *disk)
{
    if (!disk || !disk->present) {
        return -22;
    }
    if (disk->transport == STORAGE_TRANSPORT_IDE_PIO) {
        return disk->ide_atapi ? -30 : 0;
    }
    if (disk->transport == STORAGE_TRANSPORT_NVME) {
        return disk->nvme && disk->nvme->ready && disk->nvme_nsid ? 0 : -5;
    }
    if (disk->transport != STORAGE_TRANSPORT_AHCI || !disk->hba_port) {
        return -22;
    }
    /* AHCI command state is shared with the resumable syscall path.  The
     * scanner initializes each port before publishing the disk, and the
     * controller recovery path reinitializes it after a real failure.  Do
     * not reset the port for every open/read/ioctl: a retry can be polling a
     * live command, and clearing PxCI/its command table here would make the
     * still-running DMA look complete with stale data (notably an invalid
     * GPT header). */
    return 0;
}

static int storage_read_device(const struct storage_volume *volume, uint64_t lba,
                               uint32_t sector_count, void *buffer)
{
    int ret = 0;
    uint64_t start_lba = lba;
    if (!volume || !buffer || !sector_count) {
        return -22;
    }
    if (volume->kind == STORAGE_VOLUME_RAM) {
        uint64_t offset = lba * SECTOR_SIZE;
        uint64_t bytes = (uint64_t)sector_count * SECTOR_SIZE;
        if (!volume->ram_base || offset + bytes < offset || offset + bytes > volume->ram_bytes) {
            console_printf("[storage] ram read out of range volume=%u lba=%llu sectors=%u ram_bytes=%llu\n",
                           volume->volume_id, (unsigned long long)lba,
                           sector_count, (unsigned long long)volume->ram_bytes);
            return -22;
        }
        storage_memcpy(buffer, volume->ram_base + offset, (size_t)bytes);
        return 0;
    }
    kernel_spin_lock(&storage_transport_lock);
    if (volume->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(volume, &device);
        if (device.atapi) {
            ret = -30;
            goto out;
        }
        uint8_t *dst = (uint8_t *)buffer;
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, IDE_MAX_PIO_SECTORS);
            ret = ide_pio_transfer(&device, lba, chunk, dst, 0);
            if (ret < 0) {
                goto out;
            }
            lba += chunk;
            sector_count -= chunk;
            dst += chunk * SECTOR_SIZE;
        }
        goto out;
    }
    if (volume->transport == STORAGE_TRANSPORT_NVME) {
        uint8_t *dst = (uint8_t *)buffer;
        if (!volume->nvme || !volume->nvme_nsid) {
            ret = -22;
            goto out;
        }
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, NVME_MAX_SECTORS);
            ret = nvme_readwrite(volume->nvme, volume->nvme_nsid, lba, chunk, dst, 0);
            if (ret < 0) {
                goto out;
            }
            lba += chunk;
            sector_count -= chunk;
            dst += chunk * SECTOR_SIZE;
        }
        goto out;
    }
    if (volume->transport != STORAGE_TRANSPORT_AHCI || !volume->hba_port) {
        ret = -95;
        goto out;
    }
    uint8_t *dst = (uint8_t *)buffer;
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        ret = ahci_read_lba_retry(volume->hba_port, lba, chunk, dst);
        if (ret < 0) {
            goto out;
        }
        lba += chunk;
        sector_count -= chunk;
        dst += chunk * SECTOR_SIZE;
    }
out:
    kernel_spin_unlock(&storage_transport_lock);
    if (ret < 0) {
        console_printf("[storage] device read failed volume=%u kind=%u transport=%u lba=%llu sectors=%u ret=%d\n",
                       volume->volume_id, volume->kind, volume->transport,
                       (unsigned long long)start_lba, sector_count, ret);
    }
    return ret;
}

static int storage_write_device(const struct storage_volume *volume, uint64_t lba,
                                uint32_t sector_count, const void *buffer)
{
    int ret = 0;
    if (!volume || !buffer || !sector_count) {
        return -30;
    }
    if (volume->kind == STORAGE_VOLUME_RAM) {
        uint64_t offset = lba * SECTOR_SIZE;
        uint64_t bytes = (uint64_t)sector_count * SECTOR_SIZE;
        if (!volume->ram_base || offset + bytes < offset || offset + bytes > volume->ram_bytes) {
            return -22;
        }
        storage_memcpy(volume->ram_base + offset, buffer, (size_t)bytes);
        return 0;
    }
    kernel_spin_lock(&storage_transport_lock);
    if (volume->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(volume, &device);
        if (device.atapi) {
            ret = -30;
            goto out;
        }
        const uint8_t *src = (const uint8_t *)buffer;
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, IDE_MAX_PIO_SECTORS);
            ret = ide_pio_transfer(&device, lba, chunk, (void *)src, 1);
            if (ret < 0) {
                goto out;
            }
            lba += chunk;
            sector_count -= chunk;
            src += chunk * SECTOR_SIZE;
        }
        goto out;
    }
    if (volume->transport == STORAGE_TRANSPORT_NVME) {
        const uint8_t *src = (const uint8_t *)buffer;
        if (!volume->nvme || !volume->nvme_nsid) {
            ret = -22;
            goto out;
        }
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, NVME_MAX_SECTORS);
            ret = nvme_readwrite(volume->nvme, volume->nvme_nsid, lba, chunk,
                                 (void *)src, 1);
            if (ret < 0) {
                goto out;
            }
            lba += chunk;
            sector_count -= chunk;
            src += chunk * SECTOR_SIZE;
        }
        goto out;
    }
    if (volume->transport != STORAGE_TRANSPORT_AHCI || !volume->hba_port) {
        ret = -95;
        goto out;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        ret = ahci_write_lba_retry(volume->hba_port, lba, chunk, src);
        if (ret < 0) {
            goto out;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
out:
    kernel_spin_unlock(&storage_transport_lock);
    return ret;
}

static int storage_read_install_disk(const struct install_disk_state *disk, uint64_t lba,
                                     uint32_t sector_count, void *buffer)
{
    struct storage_volume volume;
    if (!disk || !disk->present) {
        return -22;
    }
    storage_memzero(&volume, sizeof(volume));
    storage_volume_from_install_disk(&volume, disk);
    return storage_read_device(&volume, lba, sector_count, buffer);
}

static int storage_write_install_disk(const struct install_disk_state *disk, uint64_t lba,
                                      uint32_t sector_count, const void *buffer)
{
    struct storage_volume volume;
    if (!disk || !disk->present) {
        return -22;
    }
    storage_memzero(&volume, sizeof(volume));
    storage_volume_from_install_disk(&volume, disk);
    return storage_write_device(&volume, lba, sector_count, buffer);
}

static int storage_read_sectors(uint64_t lba, uint32_t sector_count, void *buffer)
{
    return storage_read_device(&g_storage, lba, sector_count, buffer);
}

static uint32_t storage_gpt_crc32(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int storage_gpt_guid_empty(const uint8_t guid[16])
{
    if (!guid) return 1;
    for (uint32_t i = 0; i < 16u; ++i) {
        if (guid[i] != 0) return 0;
    }
    return 1;
}

/* Validate the primary GPT before allocating or reading its entry array. The
 * implementation supports up to LeonOS's fixed 128-entry in-memory limit,
 * but does not require the table itself to start at LBA 2: valid GPT tools
 * may choose another table position. */
static int storage_gpt_header_valid_for_root(const struct gpt_header *header)
{
    struct gpt_header copy;
    uint64_t table_bytes;
    uint64_t table_sectors;
    uint64_t table_last_lba;
    if (!header || header->signature != 0x5452415020494645ULL ||
        header->revision < 0x00010000u ||
        header->header_size != sizeof(struct gpt_header) ||
        header->reserved != 0 || header->current_lba != 1u ||
        header->backup_lba <= header->current_lba ||
        header->first_usable_lba < 2u ||
        header->first_usable_lba > header->last_usable_lba ||
        header->last_usable_lba >= header->backup_lba ||
        header->partition_entries_lba < 2u ||
        header->partition_entry_count == 0 ||
        header->partition_entry_count > GPT_ENTRY_COUNT ||
        header->partition_entry_size != sizeof(struct gpt_entry)) {
        return -22;
    }
    table_bytes = (uint64_t)header->partition_entry_count *
                  header->partition_entry_size;
    table_sectors = (table_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    if (!table_sectors || table_bytes > sizeof(storage_cluster_buf) ||
        table_sectors > header->first_usable_lba - header->partition_entries_lba) {
        return -22;
    }
    table_last_lba = header->partition_entries_lba + table_sectors - 1u;
    if (table_last_lba >= header->first_usable_lba ||
        table_last_lba >= header->backup_lba) {
        return -22;
    }
    copy = *header;
    copy.header_crc32 = 0;
    return storage_gpt_crc32(&copy, header->header_size) == header->header_crc32
               ? 0 : -5;
}

static int storage_gpt_entries_valid_for_root(const struct gpt_header *header,
                                              const uint8_t *table)
{
    uint32_t count;
    if (!header || !table) return -22;
    count = header->partition_entry_count;
    for (uint32_t i = 0; i < count; ++i) {
        const struct gpt_entry *entry =
            (const struct gpt_entry *)(const void *)(table + (uint64_t)i *
                                                     sizeof(struct gpt_entry));
        if (storage_gpt_guid_empty(entry->type_guid)) continue;
        if (storage_gpt_guid_empty(entry->unique_guid) ||
            entry->first_lba < header->first_usable_lba ||
            entry->last_lba < entry->first_lba ||
            entry->last_lba > header->last_usable_lba) {
            return -22;
        }
        for (uint32_t j = 0; j < i; ++j) {
            const struct gpt_entry *other =
                (const struct gpt_entry *)(const void *)(table +
                    (uint64_t)j * sizeof(struct gpt_entry));
            if (!storage_gpt_guid_empty(other->type_guid) &&
                !(entry->last_lba < other->first_lba ||
                  entry->first_lba > other->last_lba)) {
                return -22;
            }
        }
    }
    return 0;
}

/* GPT names are UTF-16LE on disk, but partition editors commonly differ in
 * ASCII case.  Compare the canonical LeonOS root label without making the
 * filesystem probe depend on a particular editor's spelling. */
static int storage_gpt_name_matches(const uint16_t name[36], const char *ascii)
{
    uint32_t i;
    if (!name || !ascii) {
        return 0;
    }
    for (i = 0; i < 36u && ascii[i]; ++i) {
        uint16_t ch = name[i];
        uint8_t expected = (uint8_t)ascii[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (uint16_t)(ch - ('a' - 'A'));
        }
        if (expected >= 'a' && expected <= 'z') {
            expected = (uint8_t)(expected - ('a' - 'A'));
        }
        if (ch != expected) {
            return 0;
        }
    }
    return ascii[i] == 0 && i < 36u && name[i] == 0;
}

/* A filesystem signature is authoritative; GPT type/name metadata is not.
 * Keep this probe deliberately small because it runs once per GPT entry and
 * the full exFAT boot-region validation is performed by exfat_mount(). */
static int storage_probe_exfat_signature(uint64_t lba)
{
    int ret = storage_read_sectors(lba, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    return storage_memcmp(storage_scratch + 3u, "EXFAT   ", 8u) == 0 &&
           storage_scratch[510] == 0x55u && storage_scratch[511] == 0xaau;
}

static int storage_probe_fat32_signature(uint64_t lba)
{
    int ret = storage_read_sectors(lba, 1u, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    return storage_scratch[510] == 0x55u && storage_scratch[511] == 0xaau &&
           storage_memcmp(storage_scratch + 82u, "FAT32   ", 8u) == 0;
}

static int storage_read_iso_blocks(uint64_t lba, uint32_t block_count, void *buffer)
{
    uint8_t *dst = (uint8_t *)buffer;
    if (!buffer || block_count == 0 || g_storage.filesystem != STORAGE_FILESYSTEM_ISO9660) {
        return -22;
    }
    /* The PVD is read before the volume size is known. Once mounted, keep
     * malformed directory records from issuing reads past the medium. */
    if (g_storage.iso_sector_count != 0 &&
        (lba >= g_storage.iso_sector_count ||
         (uint64_t)block_count > g_storage.iso_sector_count - lba)) {
        return -5;
    }
    if (g_storage.transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(&g_storage, &device);
        kernel_spin_lock(&storage_transport_lock);
        while (block_count) {
            uint32_t chunk = min_u32(block_count, IDE_MAX_ATAPI_BLOCKS);
            int ret = ide_atapi_read_blocks(&device, lba, chunk, dst);
            if (ret < 0) {
                kernel_spin_unlock(&storage_transport_lock);
                return ret;
            }
            lba += chunk;
            block_count -= chunk;
            dst += chunk * ISO9660_BLOCK_SIZE;
        }
        kernel_spin_unlock(&storage_transport_lock);
        return 0;
    }
    if (g_storage.transport != STORAGE_TRANSPORT_AHCI || !g_storage.hba_port) {
        return -95;
    }
    kernel_spin_lock(&storage_transport_lock);
    while (block_count) {
        uint32_t chunk = min_u32(block_count, 32u);
        int ret = ahci_read_atapi_blocks_retry(g_storage.hba_port, lba, chunk, dst);
        if (ret < 0) {
            kernel_spin_unlock(&storage_transport_lock);
            return ret;
        }
        lba += chunk;
        block_count -= chunk;
        dst += chunk * ISO9660_BLOCK_SIZE;
    }
    kernel_spin_unlock(&storage_transport_lock);
    return 0;
}

static int storage_write_sectors(uint64_t lba, uint32_t sector_count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *)buffer;
    /* RAM-backed installer roots are a writable live environment. Changes
     * affect the in-memory FAT image only and disappear when it reboots. */
    /* From this point the caller may have changed filesystem metadata.  Do
     * not return EAGAIN and replay a partially completed mutation. */
    storage_begin_mutation();
    storage_sector_cache_invalidate();
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count,
                                  g_storage.transport == STORAGE_TRANSPORT_IDE_PIO
                                      ? IDE_MAX_PIO_SECTORS : STORAGE_WRITE_MAX_SECTORS);
        int ret = storage_write_device(&g_storage, lba, chunk, src);
        if (ret < 0 && chunk > 1u) {
            /* A few virtual AHCI implementations occasionally fail a
             * multi-sector DMA command even though the media is healthy.
             * Retry the same idempotent payload one sector at a time so a
             * filesystem transaction is not abandoned after a transient
             * controller error. */
            console_printf("[ntclks] storage write batch fallback lba=%llu sectors=%u ret=%d\n",
                           (unsigned long long)lba, chunk, ret);
            ret = 0;
            for (uint32_t i = 0; i < chunk; ++i) {
                int one = storage_write_device(&g_storage, lba + i, 1u,
                                               src + (size_t)i * SECTOR_SIZE);
                if (one < 0) {
                    ret = one;
                    break;
                }
            }
        }
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int gpt_find_esp(void)
{
    struct gpt_header *hdr = (struct gpt_header *)(void *)storage_scratch;
    uint8_t esp_found = 0;
    uint8_t canonical_exfat_root_found = 0;
    uint8_t basic_data_candidate_found = 0;
    uint64_t basic_data_candidate_lba = 0;
    uint8_t fat32_candidate_found = 0;
    uint64_t fat32_candidate_lba = 0;
    int ret = storage_read_sectors(1, 1, storage_scratch);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (hdr->signature != 0x5452415020494645ULL) {
        return -2;
    }
    uint32_t count;
    uint32_t size;
    uint32_t total_bytes;
    uint32_t total_sectors;
    uint32_t table_crc;
    if (storage_gpt_header_valid_for_root(hdr) < 0) {
        console_printf("[ntclks] GPT primary header invalid\n");
        return -22;
    }
    count = hdr->partition_entry_count;
    size = hdr->partition_entry_size;
    console_printf("[ntclks] GPT found entries=%u entry_size=%u\n", count, size);
    total_bytes = count * size;
    total_sectors = (total_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
    uint64_t phys = mm_alloc_pages((total_sectors + 7u) / 8u);
    if (!phys) {
        return -12;
    }
    uint8_t *table = (uint8_t *)(uintptr_t)phys;
    ret = storage_read_sectors(hdr->partition_entries_lba, total_sectors, table);
    if (ret < 0) {
        mm_free_pages(phys, (total_sectors + 7u) / 8u);
        return ret;
    }
    table_crc = storage_gpt_crc32(table, total_bytes);
    if (table_crc != hdr->partition_entries_crc32 ||
        storage_gpt_entries_valid_for_root(hdr, table) < 0) {
        mm_free_pages(phys, (total_sectors + 7u) / 8u);
        console_printf("[ntclks] GPT partition table invalid\n");
        return -5;
    }
    for (uint32_t i = 0; i < count; ++i) {
        struct gpt_entry *entry = (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
        if (!entry->first_lba || entry->last_lba < entry->first_lba) {
            if (i < 3) {
                console_printf("[ntclks] GPT entry %u: first=%llu last=%llu (skip)\n",
                               i, (unsigned long long)entry->first_lba,
                               (unsigned long long)entry->last_lba);
            }
            continue;
        }
        console_printf("[ntclks] GPT entry %u: first=%llu last=%llu guid=%x%x%x%x\n",
                       i, (unsigned long long)entry->first_lba,
                       (unsigned long long)entry->last_lba,
                       (unsigned)entry->type_guid[0], (unsigned)entry->type_guid[1],
                       (unsigned)entry->type_guid[2], (unsigned)entry->type_guid[3]);
        if (storage_memcmp(entry->type_guid, esp_guid, 16) == 0) {
            g_storage.esp_start_lba = entry->first_lba;
            g_storage.esp_sector_count = entry->last_lba - entry->first_lba + 1u;
            storage_memcpy(g_storage.gpt_disk_guid, hdr->disk_guid,
                           sizeof(g_storage.gpt_disk_guid));
            storage_memcpy(g_storage.esp_unique_guid, entry->unique_guid,
                           sizeof(g_storage.esp_unique_guid));
            g_storage.has_gpt_identity =
                storage_guid_valid(g_storage.gpt_disk_guid) &&
                storage_guid_valid(g_storage.esp_unique_guid);
            esp_found = 1;
            console_printf("[ntclks] GPT ESP found lba=%llu\n",
                           (unsigned long long)entry->first_lba);
        } else if (storage_memcmp(entry->type_guid, basic_data_guid, 16) == 0) {
            uint8_t is_exfat = 0;
            uint8_t canonical_name = 0;
            int signature_ret;
            console_printf("[ntclks] GPT basic data partition found lba=%llu name[0]=0x%04x\n",
                           (unsigned long long)entry->first_lba, (unsigned int)entry->name[0]);
            if (!basic_data_candidate_found) {
                basic_data_candidate_found = 1;
                basic_data_candidate_lba = entry->first_lba;
            }
            canonical_name = storage_gpt_name_matches(entry->name, "LEONOS4_ROOT");
            signature_ret = storage_probe_exfat_signature(entry->first_lba);
            is_exfat = signature_ret > 0;
            if (!fat32_candidate_found && storage_probe_fat32_signature(entry->first_lba) > 0) {
                fat32_candidate_found = 1;
                fat32_candidate_lba = entry->first_lba;
            }
            if (is_exfat && !canonical_name && !canonical_exfat_root_found) {
                console_printf("[ntclks] GPT exFAT signature found lba=%llu\n",
                               (unsigned long long)entry->first_lba);
            }
            if (is_exfat && (canonical_name || !canonical_exfat_root_found)) {
                g_storage.exfat_start_lba = entry->first_lba;
                g_storage.exfat_sector_count = entry->last_lba - entry->first_lba + 1u;
                if (canonical_name) {
                    canonical_exfat_root_found = 1;
                }
                console_printf("[ntclks] GPT exFAT root found lba=%llu sectors=%u\n",
                               (unsigned long long)entry->first_lba,
                               (unsigned int)(entry->last_lba - entry->first_lba + 1u));
            }
        } else if (storage_memcmp(entry->type_guid, linux_filesystem_guid, 16) == 0) {
            g_storage.ext2_start_lba = entry->first_lba;
            g_storage.ext2_sector_count = entry->last_lba - entry->first_lba + 1u;
        }
    }
    /* A hand-created GPT may carry a non-standard type or a damaged label.
     * Probe every non-ESP extent before falling back to the first Basic Data
     * entry, so an exFAT root remains discoverable even when its GPT type was
     * edited incorrectly. */
    if (!g_storage.exfat_start_lba) {
        for (uint32_t i = 0; i < count; ++i) {
            struct gpt_entry *candidate =
                (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
            if (!candidate->first_lba || candidate->last_lba < candidate->first_lba ||
                storage_memcmp(candidate->type_guid, esp_guid, 16) == 0) {
                continue;
            }
            if (storage_probe_exfat_signature(candidate->first_lba) > 0) {
                g_storage.exfat_start_lba = candidate->first_lba;
                g_storage.exfat_sector_count =
                    candidate->last_lba - candidate->first_lba + 1u;
                console_printf("[ntclks] GPT exFAT signature found lba=%llu type=non-basic\n",
                               (unsigned long long)candidate->first_lba);
                break;
            }
        }
    }
    /* Formatting a FAT32 ESP through the installer intentionally preserves
     * its bytes but some partition tools reset the GPT type to Basic Data.
     * Recover the ESP from its FAT32 boot signature when no ESP GUID was
     * present, so the root and boot partitions are still usable. */
    if (!esp_found && !fat32_candidate_found) {
        for (uint32_t i = 0; i < count; ++i) {
            struct gpt_entry *candidate =
                (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
            if (!candidate->first_lba || candidate->last_lba < candidate->first_lba ||
                storage_memcmp(candidate->type_guid, esp_guid, 16) == 0) {
                continue;
            }
            if (storage_probe_fat32_signature(candidate->first_lba) > 0) {
                fat32_candidate_found = 1;
                fat32_candidate_lba = candidate->first_lba;
                break;
            }
        }
    }
    if (!esp_found && fat32_candidate_found) {
        g_storage.esp_start_lba = fat32_candidate_lba;
        for (uint32_t i = 0; i < count; ++i) {
            struct gpt_entry *candidate =
                (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
            if (candidate->first_lba == fat32_candidate_lba &&
                candidate->last_lba >= candidate->first_lba) {
                g_storage.esp_sector_count =
                    candidate->last_lba - candidate->first_lba + 1u;
                break;
            }
        }
        esp_found = 1;
        console_printf("[ntclks] GPT ESP signature found without ESP type lba=%llu\n",
                       (unsigned long long)fat32_candidate_lba);
    }
    /* If the signature itself is damaged, retain the first Basic Data extent
     * as a mount candidate so exfat_mount() can report the actual corruption.
     * Never silently mount the ESP as the system root. */
    if (!g_storage.exfat_start_lba && !g_storage.ext2_start_lba && basic_data_candidate_found) {
        g_storage.exfat_start_lba = basic_data_candidate_lba;
        for (uint32_t i = 0; i < count; ++i) {
            struct gpt_entry *candidate =
                (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
            if (candidate->first_lba == basic_data_candidate_lba &&
                candidate->last_lba >= candidate->first_lba) {
                g_storage.exfat_sector_count =
                    candidate->last_lba - candidate->first_lba + 1u;
                break;
            }
        }
        console_printf("[ntclks] GPT exFAT signature not confirmed; trying Basic Data root lba=%llu\n",
                       (unsigned long long)g_storage.exfat_start_lba);
    }
    mm_free_pages(phys, (total_sectors + 7u) / 8u);
    return esp_found ? 0 : -2;
}
