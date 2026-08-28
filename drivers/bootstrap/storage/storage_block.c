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
    int ret;
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
    kernel_spin_lock(&storage_transport_lock);
    ret = ahci_setup_port(disk->hba_port);
    kernel_spin_unlock(&storage_transport_lock);
    return ret;
}

static int storage_read_device(const struct storage_volume *volume, uint64_t lba,
                               uint32_t sector_count, void *buffer)
{
    int ret = 0;
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
    return ret;
}

static int storage_write_device(const struct storage_volume *volume, uint64_t lba,
                                uint32_t sector_count, const void *buffer)
{
    int ret = 0;
    if (!volume || !buffer || !sector_count || volume->kind == STORAGE_VOLUME_RAM) {
        return -30;
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
    int ret = storage_read_sectors(1, 1, storage_scratch);
    if (ret < 0) {
        return storage_read_failure(ret);
    }
    if (hdr->signature != 0x5452415020494645ULL) {
        return -2;
    }
    uint32_t count = hdr->partition_entry_count;
    uint32_t size = hdr->partition_entry_size;
    console_printf("[ntclks] GPT found entries=%u entry_size=%u\n", count, size);
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
            console_printf("[ntclks] GPT basic data partition found lba=%llu name[0]=0x%04x\n",
                           (unsigned long long)entry->first_lba, (unsigned int)entry->name[0]);
            if (entry->name[0] == 'L' && entry->name[1] == 'E' && entry->name[2] == 'O' &&
                entry->name[3] == 'N' && entry->name[4] == 'O' && entry->name[5] == 'S' &&
                entry->name[6] == '4' && entry->name[7] == '_' && entry->name[8] == 'R' &&
                entry->name[9] == 'O' && entry->name[10] == 'O' && entry->name[11] == 'T' &&
                entry->name[12] == 0) {
                g_storage.exfat_start_lba = entry->first_lba;
                g_storage.exfat_sector_count = entry->last_lba - entry->first_lba + 1u;
                console_printf("[ntclks] GPT exFAT root found lba=%llu sectors=%u\n",
                               (unsigned long long)entry->first_lba,
                               (unsigned int)(entry->last_lba - entry->first_lba + 1u));
            }
        } else if (storage_memcmp(entry->type_guid, linux_filesystem_guid, 16) == 0) {
            g_storage.ext2_start_lba = entry->first_lba;
            g_storage.ext2_sector_count = entry->last_lba - entry->first_lba + 1u;
        }
    }
    mm_free_pages(phys, (total_sectors + 7u) / 8u);
    return esp_found ? 0 : -2;
}
