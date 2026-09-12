/* Sector completion precedes write return. fsync additionally drains the
 * device's volatile cache and propagates errors before publishing account data. */
static int storage_flush_volume(const struct storage_volume *volume)
{
    if (!volume || !volume->ready) return -19;
    if (volume->kind == STORAGE_VOLUME_RAM || volume->ide_atapi || volume->filesystem == STORAGE_FILESYSTEM_ISO9660) return 0;
    if (volume->transport == STORAGE_TRANSPORT_IDE_PIO) {
        struct ide_device_info device;
        storage_volume_ide_device(volume, &device);
        return ide_flush_cache(&device);
    }
    if (volume->transport == STORAGE_TRANSPORT_NVME)
        return nvme_flush_cache(volume->nvme, volume->nvme_nsid);
    if (volume->transport == STORAGE_TRANSPORT_AHCI)
        return ahci_flush_cache(volume->hba_port);
    return -95;
}

int storage_sync_volume(uint32_t volume_id)
{
    if (volume_id >= STORAGE_MAX_VOLUMES) return -19;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    kernel_spin_lock(&storage_transport_lock);
    bool saved_async = storage_io_async_context;
    storage_io_async_context = false;
    int ret = storage_flush_volume(&g_volumes[volume_id]);
    storage_io_async_context = saved_async;
    kernel_spin_unlock(&storage_transport_lock);
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}

int storage_sync_all(void)
{
    int result = 0;
    uint64_t flags;
    kernel_execution_lock_irqsave(&flags);
    for (uint32_t i = 0; i < STORAGE_MAX_VOLUMES; ++i) {
        if (!g_volumes[i].ready) continue;
        int ret = storage_sync_volume(i);
        if (ret < 0 && !result) result = ret;
    }
    kernel_execution_unlock_irqrestore(flags);
    return result;
}
