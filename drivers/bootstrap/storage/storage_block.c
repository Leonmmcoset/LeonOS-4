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

static int storage_read_device(const struct storage_volume *volume, uint64_t lba,
                               uint32_t sector_count, void *buffer)
{
    if (!volume || !buffer || !sector_count) {
        return -22;
    }
    if (volume->kind == STORAGE_VOLUME_RAM) {
        uint64_t offset = lba * SECTOR_SIZE;
        uint64_t bytes = (uint64_t)sector_count * SECTOR_SIZE;
        if (!volume->ram_base || offset + bytes < offset || offset + bytes > volume->ram_bytes) {
            return -22;
        }
        storage_memcpy(buffer, volume->ram_base + offset, (size_t)bytes);
        return 0;
    }
    if (volume->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(volume, &device);
        if (device.atapi) {
            return -30;
        }
        uint8_t *dst = (uint8_t *)buffer;
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, IDE_MAX_PIO_SECTORS);
            int ret = ide_pio_transfer(&device, lba, chunk, dst, 0);
            if (ret < 0) {
                return ret;
            }
            lba += chunk;
            sector_count -= chunk;
            dst += chunk * SECTOR_SIZE;
        }
        return 0;
    }
    uint8_t *dst = (uint8_t *)buffer;
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        int ret = ahci_read_lba_retry(volume->hba_port, lba, chunk, dst);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        dst += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int storage_write_device(const struct storage_volume *volume, uint64_t lba,
                                uint32_t sector_count, const void *buffer)
{
    if (!volume || !buffer || !sector_count || volume->kind == STORAGE_VOLUME_RAM) {
        return -30;
    }
    if (volume->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(volume, &device);
        if (device.atapi) {
            return -30;
        }
        const uint8_t *src = (const uint8_t *)buffer;
        while (sector_count) {
            uint32_t chunk = min_u32(sector_count, IDE_MAX_PIO_SECTORS);
            int ret = ide_pio_transfer(&device, lba, chunk, (void *)src, 1);
            if (ret < 0) {
                return ret;
            }
            lba += chunk;
            sector_count -= chunk;
            src += chunk * SECTOR_SIZE;
        }
        return 0;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, AHCI_MAX_SECTORS);
        int ret = ahci_write_lba_retry(volume->hba_port, lba, chunk, src);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int storage_read_sectors(uint64_t lba, uint32_t sector_count, void *buffer)
{
    return storage_read_device(&g_storage, lba, sector_count, buffer);
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
        while (block_count) {
            uint32_t chunk = min_u32(block_count, IDE_MAX_ATAPI_BLOCKS);
            int ret = ide_atapi_read_blocks(&device, lba, chunk, dst);
            if (ret < 0) {
                return ret;
            }
            lba += chunk;
            block_count -= chunk;
            dst += chunk * ISO9660_BLOCK_SIZE;
        }
        return 0;
    }
    while (block_count) {
        uint32_t chunk = min_u32(block_count, 32u);
        int ret = ahci_read_atapi_blocks_retry(g_storage.hba_port, lba, chunk, dst);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        block_count -= chunk;
        dst += chunk * ISO9660_BLOCK_SIZE;
    }
    return 0;
}

static int storage_write_sectors(uint64_t lba, uint32_t sector_count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *)buffer;
    /* The installer payload is a reserved Multiboot module and must stay immutable. */
    if (g_storage.kind == STORAGE_VOLUME_RAM) {
        return -30;
    }
    /* From this point the caller may have changed filesystem metadata.  Do
     * not return EAGAIN and replay a partially completed mutation. */
    storage_begin_mutation();
    storage_sector_cache_invalidate();
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count,
                                  g_storage.transport == STORAGE_TRANSPORT_IDE_PIO
                                      ? IDE_MAX_PIO_SECTORS : STORAGE_WRITE_MAX_SECTORS);
        int ret = storage_write_device(&g_storage, lba, chunk, src);
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
    int ret = storage_read_sectors(1, 1, storage_scratch);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (hdr->signature != 0x5452415020494645ULL) {
        return -2;
    }
    uint32_t count = hdr->partition_entry_count;
    uint32_t size = hdr->partition_entry_size;
    if (!count || !size || size < sizeof(struct gpt_entry)) {
        return -2;
    }
    uint32_t total_bytes = count * size;
    uint32_t total_sectors = (total_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
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
    for (uint32_t i = 0; i < count; ++i) {
        struct gpt_entry *entry = (struct gpt_entry *)(void *)(table + (uint64_t)i * size);
        if (!entry->first_lba || entry->last_lba < entry->first_lba) {
            continue;
        }
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
        } else if (storage_memcmp(entry->type_guid, linux_filesystem_guid, 16) == 0) {
            g_storage.ext2_start_lba = entry->first_lba;
            g_storage.ext2_sector_count = entry->last_lba - entry->first_lba + 1u;
        }
    }
    mm_free_pages(phys, (total_sectors + 7u) / 8u);
    return esp_found ? 0 : -2;
}

