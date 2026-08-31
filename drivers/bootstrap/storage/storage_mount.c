static int storage_path_append_u32(char *path, uint32_t capacity, uint32_t *position,
                                   uint32_t value)
{
    char digits[10];
    uint32_t count = 0;
    if (!path || !position || *position >= capacity) {
        return -22;
    }
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count) {
        if (*position + 1 >= capacity) {
            return -22;
        }
        path[(*position)++] = digits[--count];
    }
    path[*position] = 0;
    return 0;
}

static int storage_set_data_mount_path(struct storage_volume *volume, uint32_t disk_id,
                                       uint32_t partition_index)
{
    uint32_t position;
    if (!volume) {
        return -22;
    }
    storage_copy_text(volume->mount_path, sizeof(volume->mount_path), "/mnt/disk");
    position = storage_strlen(volume->mount_path);
    if (storage_path_append_u32(volume->mount_path, sizeof(volume->mount_path), &position,
                                disk_id) < 0 ||
        position + 2 >= sizeof(volume->mount_path)) {
        return -22;
    }
    volume->mount_path[position++] = 'p';
    volume->mount_path[position] = 0;
    return storage_path_append_u32(volume->mount_path, sizeof(volume->mount_path), &position,
                                   partition_index + 1u);
}

static int storage_mount_runtime_boot(void)
{
    struct storage_volume *root = &g_volumes[STORAGE_VOLUME_ROOT];
    struct storage_volume *boot = &g_volumes[STORAGE_VOLUME_BOOT];
    struct storage_volume *old = g_active_volume;
    int ret;
    if (!root->ready || root->transport == 0 || root->esp_sector_count == 0) {
        return -2;
    }
    storage_memzero(boot, sizeof(*boot));
    *boot = *root;
    boot->volume_id = STORAGE_VOLUME_BOOT;
    boot->ready = false;
    boot->filesystem = STORAGE_FILESYSTEM_NONE;
    boot->data_partition_mount = 0;
    storage_copy_text(boot->mount_path, sizeof(boot->mount_path), "/boot");
    g_active_volume = boot;
    ret = fat32_mount();
    if (ret == 0) {
        boot->ready = true;
    } else {
        storage_memzero(boot, sizeof(*boot));
    }
    g_active_volume = old;
    storage_cache_invalidate();
    return ret;
}

static uint16_t ide_pci_bar_io(uint8_t bus, uint8_t slot, uint8_t function,
                               uint8_t offset, uint16_t fallback)
{
    uint32_t bar = pci_config_read32(bus, slot, function, offset);
    if (!(bar & 1u)) {
        return fallback;
    }
    bar &= ~3u;
    return bar && bar <= 0xffffu ? (uint16_t)bar : fallback;
}

static void storage_copy_disk_transport(struct storage_volume *volume,
                                        const struct install_disk_state *disk)
{
    storage_volume_from_install_disk(volume, disk);
}

static int storage_try_mount_root_disk(struct install_disk_state *disk)
{
    struct storage_volume *root = &g_volumes[STORAGE_VOLUME_ROOT];
    struct storage_volume *old = g_active_volume;
    int ret;
    if (!disk || !disk->present) {
        return -22;
    }
    storage_memzero(root, sizeof(*root));
    root->volume_id = STORAGE_VOLUME_ROOT;
    storage_copy_disk_transport(root, disk);
    storage_copy_text(root->mount_path, sizeof(root->mount_path), "/");
    g_active_volume = root;
    ret = gpt_find_esp();
    if (ret == 0) {
        if (root->exfat_start_lba) {
            /* A named exFAT root is authoritative. Never silently mount the
             * ESP as / when its metadata is corrupt: that hides the real
             * failure and starts an incomplete system. */
            console_printf("[ntclks] attempting exFAT mount lba=%llu sectors=%u\n",
                           (unsigned long long)root->exfat_start_lba,
                           (unsigned int)root->exfat_sector_count);
            ret = exfat_mount();
            console_printf("[ntclks] exfat_mount returned %d\n", ret);
            if (ret < 0 && root->ext2_start_lba) {
                ret = ext2_mount();
            }
        } else if (root->ext2_start_lba) {
            ret = ext2_mount();
        } else {
            /* A GPT disk with an ESP but no recognized root must fail
             * explicitly. Mounting the ESP as / leaves a seemingly bootable
             * system with no /system tree and hides the real storage error. */
            console_printf("[ntclks] GPT root filesystem not identified; refusing ESP fallback\n");
            ret = -2;
        }
    }
    if (ret == 0) {
        root->ready = true;
        if (storage_mount_runtime_boot() < 0) {
            console_printf("[ntclks] storage boot ESP mount unavailable\n");
        }
        disk->boot_root = 1;
        console_printf("[ntclks] storage ready transport=%s pci=%u:%u.%u unit=%u root=%s\n",
                       storage_transport_name(disk->transport),
                       disk->bus, disk->slot, disk->function,
                       disk->transport == STORAGE_TRANSPORT_NVME ? disk->nvme_nsid : disk->port,
                       root->filesystem == STORAGE_FILESYSTEM_EXFAT ? "exfat" :
                       (root->filesystem == STORAGE_FILESYSTEM_EXT2 ? "ext2" : "fat32"));
    } else {
        storage_memzero(root, sizeof(*root));
    }
    g_active_volume = old;
    storage_cache_invalidate();
    return ret;
}

static uint32_t storage_disk_sort_unit(const struct install_disk_state *disk)
{
    if (disk->transport == STORAGE_TRANSPORT_NVME) {
        return disk->nvme_nsid;
    }
    if (disk->transport == STORAGE_TRANSPORT_IDE_PIO) {
        return (uint32_t)disk->ide_channel * 2u + disk->ide_drive;
    }
    return disk->port;
}

static int storage_disk_precedes(const struct install_disk_state *left,
                                 const struct install_disk_state *right)
{
    if (left->bus != right->bus) return left->bus < right->bus;
    if (left->slot != right->slot) return left->slot < right->slot;
    if (left->function != right->function) return left->function < right->function;
    if (left->transport != right->transport) return left->transport < right->transport;
    return storage_disk_sort_unit(left) < storage_disk_sort_unit(right);
}

static void storage_sort_install_disks(void)
{
    for (uint32_t i = 1; i < g_install_disk_count; ++i) {
        struct install_disk_state value = g_install_disks[i];
        uint32_t j = i;
        while (j && storage_disk_precedes(&value, &g_install_disks[j - 1u])) {
            g_install_disks[j] = g_install_disks[j - 1u];
            --j;
        }
        g_install_disks[j] = value;
    }
}

static void storage_scan_ide_controller(uint8_t bus, uint8_t slot, uint8_t function,
                                         uint32_t *optical_slot, uint32_t *optical_index,
                                         uint8_t *root_ready)
{
    uint8_t native = (uint8_t)(pci_config_read32(bus, slot, function, 0x08) >> 8);
    uint16_t command[2];
    uint16_t control[2];
    command[0] = (native & 1u) ? ide_pci_bar_io(bus, slot, function, 0x10,
                                                IDE_PRIMARY_CMD_DEFAULT)
                               : IDE_PRIMARY_CMD_DEFAULT;
    control[0] = (native & 1u) ? ide_pci_bar_io(bus, slot, function, 0x14,
                                                IDE_PRIMARY_CTRL_DEFAULT)
                               : IDE_PRIMARY_CTRL_DEFAULT;
    command[1] = (native & 4u) ? ide_pci_bar_io(bus, slot, function, 0x18,
                                                IDE_SECONDARY_CMD_DEFAULT)
                               : IDE_SECONDARY_CMD_DEFAULT;
    control[1] = (native & 4u) ? ide_pci_bar_io(bus, slot, function, 0x1c,
                                                IDE_SECONDARY_CTRL_DEFAULT)
                               : IDE_SECONDARY_CTRL_DEFAULT;
    console_printf("[ntclks] IDE controller detected pci=%u:%u.%u primary=0x%x/0x%x secondary=0x%x/0x%x\n",
                   bus, slot, function, command[0], control[0], command[1], control[1]);
    for (uint8_t channel = 0; channel < 2u; ++channel) {
        for (uint8_t drive = 0; drive < 2u; ++drive) {
            struct ide_device_info device;
            struct install_disk_state *disk;
            ide_fill_device(&device, channel, drive, command[channel], control[channel]);
            if (ide_identify_device(&device) < 0) {
                continue;
            }
            console_printf("[ntclks] IDE %s detected channel=%s drive=%s model=\"%s\" sectors=%llu lba48=%u\n",
                           device.atapi ? "ATAPI" : "disk",
                           channel ? "secondary" : "primary",
                           drive ? "slave" : "master", device.model,
                           (unsigned long long)device.sector_count, device.lba48);
            if (device.atapi) {
                if (*optical_slot >= STORAGE_MAX_VOLUMES) {
                    continue;
                }
                struct storage_volume *optical = &g_volumes[*optical_slot];
                storage_memzero(optical, sizeof(*optical));
                optical->volume_id = (uint8_t)*optical_slot;
                optical->kind = STORAGE_VOLUME_IDE;
                optical->transport = STORAGE_TRANSPORT_IDE_PIO;
                optical->ide_channel = channel;
                optical->ide_drive = drive;
                optical->ide_atapi = 1;
                optical->ide_command_base = command[channel];
                optical->ide_control_base = control[channel];
                optical->bus = bus;
                optical->slot = slot;
                optical->function = function;
                optical->port = (uint8_t)(channel * 2u + drive);
                storage_copy_text(optical->device_model, sizeof(optical->device_model), device.model);
                g_active_volume = optical;
                if (iso9660_mount() < 0) {
                    storage_memzero(optical, sizeof(*optical));
                    continue;
                }
                storage_copy_text(optical->mount_path, sizeof(optical->mount_path), "/media/cdrom");
                uint32_t position = storage_strlen(optical->mount_path);
                if (storage_path_append_u32(optical->mount_path, sizeof(optical->mount_path),
                                            &position, *optical_index) < 0) {
                    storage_memzero(optical, sizeof(*optical));
                    continue;
                }
                optical->ready = true;
                console_printf("[ntclks] storage auto-mounted iso9660 path=%s transport=ide-pio\n",
                               optical->mount_path);
                ++*optical_slot;
                ++*optical_index;
                continue;
            }
            if (g_install_disk_count >= STORAGE_MAX_INSTALL_DISKS) {
                continue;
            }
            disk = &g_install_disks[g_install_disk_count++];
            storage_memzero(disk, sizeof(*disk));
            disk->present = true;
            disk->bus = bus;
            disk->slot = slot;
            disk->function = function;
            disk->port = (uint8_t)(channel * 2u + drive);
            disk->transport = STORAGE_TRANSPORT_IDE_PIO;
            disk->ide_channel = channel;
            disk->ide_drive = drive;
            disk->ide_lba48 = device.lba48;
            disk->ide_command_base = command[channel];
            disk->ide_control_base = control[channel];
            disk->sector_count = device.sector_count;
            storage_copy_text(disk->device_model, sizeof(disk->device_model), device.model);
            if (!*root_ready && storage_try_mount_root_disk(disk) == 0) {
                *root_ready = 1;
            }
        }
    }
}

void storage_init(void)
{
    uint32_t optical_slot = STORAGE_VOLUME_DYNAMIC_FIRST;
    uint32_t optical_index = 0;
    uint8_t root_ready = 0;

    storage_memzero(g_volumes, sizeof(g_volumes));
    storage_memzero(g_install_disks, sizeof(g_install_disks));
    g_install_disk_count = 0;
    g_devfs_enabled = 1;
    g_installer_root_active = 0;
    storage_io_async_context = false;
    storage_io_write_started = false;
    ahci_pending_clear();
    nvme_reset_all_controllers();
    g_active_volume = &g_volumes[0];
    storage_cache_invalidate();

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                if (pci_config_read16((uint8_t)bus, slot, func, 0x00) == 0xffffu) {
                    if (func == 0) {
                        break;
                    }
                    continue;
                }
                uint32_t class_reg = pci_config_read32((uint8_t)bus, slot, func, 0x08);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t progif = (uint8_t)(class_reg >> 8);
                if (class_code != ATA_CLASS_MASS_STORAGE) {
                    continue;
                }
                if (subclass == ATA_SUBCLASS_IDE) {
                    storage_scan_ide_controller((uint8_t)bus, slot, func,
                                                &optical_slot, &optical_index, &root_ready);
                    continue;
                }
                if (subclass == ATA_SUBCLASS_NVM && progif == ATA_PROGIF_NVME) {
                    storage_scan_nvme_controller((uint8_t)bus, slot, func, &root_ready);
                    continue;
                }
                if (subclass != ATA_SUBCLASS_SATA || progif != ATA_PROGIF_AHCI) {
                    continue;
                }
                uint32_t abar_lo = pci_config_read32((uint8_t)bus, slot, func, 0x24);
                uint32_t abar_hi = pci_config_read32((uint8_t)bus, slot, func, 0x28);
                uint64_t abar_phys = ((uint64_t)abar_hi << 32) | (abar_lo & ~0x0fu);
                if (!abar_phys) {
                    continue;
                }
                struct ahci_hba_mem *abar = (struct ahci_hba_mem *)(uintptr_t)abar_phys;
                abar->ghc |= AHCI_GHC_AE;
                for (uint8_t port = 0; port < 32; ++port) {
                    if ((abar->pi & (1u << port)) == 0) {
                        continue;
                    }
                    struct ahci_hba_port *p = &abar->ports[port];
                    uint32_t det = p->ssts & 0x0fu;
                    uint32_t ipm = (p->ssts >> 8) & 0x0fu;
                    if (det != AHCI_PORT_DET_PRESENT || ipm != AHCI_PORT_IPM_ACTIVE ||
                        (p->sig != AHCI_PORT_SIG_ATA && p->sig != AHCI_PORT_SIG_ATAPI)) {
                        continue;
                    }
                    if (ahci_setup_port(p) < 0) {
                        continue;
                    }

                    if (p->sig == AHCI_PORT_SIG_ATAPI) {
                        struct storage_volume *optical;
                        if (optical_slot >= STORAGE_MAX_VOLUMES) {
                            continue;
                        }
                        optical = &g_volumes[optical_slot];
                        storage_memzero(optical, sizeof(*optical));
                        optical->volume_id = (uint8_t)optical_slot;
                        optical->kind = STORAGE_VOLUME_AHCI;
                        optical->transport = STORAGE_TRANSPORT_AHCI;
                        optical->bus = (uint8_t)bus;
                        optical->slot = slot;
                        optical->function = func;
                        optical->port = port;
                        optical->abar = abar;
                        optical->hba_port = p;
                        g_active_volume = optical;
                        {
                            int mount_ret = iso9660_mount();
                            if (mount_ret < 0) {
                                console_printf("[ntclks] storage failed to mount iso9660 cdrom=%u ahci=%u:%u.%u port=%u error=%d\\n",
                                               optical_index, bus, slot, func, port, mount_ret);
                                storage_memzero(optical, sizeof(*optical));
                                continue;
                            }
                        }
                        storage_copy_text(optical->mount_path, sizeof(optical->mount_path),
                                          "/media/cdrom");
                        {
                            uint32_t position = storage_strlen(optical->mount_path);
                            if (storage_path_append_u32(optical->mount_path,
                                                        sizeof(optical->mount_path), &position,
                                                        optical_index) < 0) {
                                storage_memzero(optical, sizeof(*optical));
                                continue;
                            }
                        }
                        optical->ready = true;
                        console_printf("[ntclks] storage auto-mounted iso9660 path=%s ahci=%u:%u.%u port=%u blocks=%llu\n",
                                       optical->mount_path,
                                       bus, slot, func, port,
                                       (unsigned long long)optical->iso_sector_count);
                        ++optical_slot;
                        ++optical_index;
                        continue;
                    }

                    if (g_install_disk_count < STORAGE_MAX_INSTALL_DISKS) {
                        struct install_disk_state *disk =
                            &g_install_disks[g_install_disk_count++];
                        storage_memzero(disk, sizeof(*disk));
                        disk->present = true;
                        disk->bus = (uint8_t)bus;
                        disk->slot = slot;
                        disk->function = func;
                        disk->port = port;
                        disk->transport = STORAGE_TRANSPORT_AHCI;
                        disk->abar = abar;
                        disk->hba_port = p;
                        storage_copy_text(disk->device_model, sizeof(disk->device_model),
                                          "SATA/AHCI Disk");
                        if (!root_ready && storage_try_mount_root_disk(disk) == 0) {
                            root_ready = 1;
                        }
                    }
                }
            }
        }
    }
    storage_sort_install_disks();
    g_active_volume = &g_volumes[0];
    if (!root_ready) {
        console_printf("[ntclks] storage init failed: no AHCI/IDE/NVMe GPT ESP/root filesystem found\n");
    }
}

void storage_apply_mount_policy(const struct leonos_mount_policy *policy)
{
    uint32_t count = 0;
    int root_ramdisk_status = 0;
    if (!policy || policy->version != LEONOS_MOUNT_POLICY_VERSION) {
        console_printf("[ntclks] storage using legacy mount policy fallback\n");
        storage_init();
        return;
    }

    console_printf("[ntclks] storage applying middlelayer mount policy entries=%u root=/\n",
                   policy->count);

    storage_init();
    g_devfs_enabled = 0;
    count = policy->count;
    if (count > LEONOS_MOUNT_MAX_ENTRIES) {
        count = LEONOS_MOUNT_MAX_ENTRIES;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const struct leonos_mount_entry *entry = &policy->entries[i];
        switch (entry->kind) {
        case LEONOS_MOUNT_KIND_FAT32_BOOT:
            console_printf("[ntclks] mount policy %s kind=ahci-esp flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        case LEONOS_MOUNT_KIND_EXT2_BOOT:
            console_printf("[ntclks] mount policy %s kind=ahci-ext2 flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        case LEONOS_MOUNT_KIND_EXFAT_BOOT:
            console_printf("[ntclks] mount policy %s kind=block-exfat flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        case LEONOS_MOUNT_KIND_FAT32_RAMDISK:
            if (!storage_text_eq(entry->path, "/") || !entry->module_start || !entry->module_len) {
                root_ramdisk_status = -2;
            } else {
                root_ramdisk_status =
                    storage_mount_ramdisk_root((const void *)(uintptr_t)entry->module_start,
                                               entry->module_len);
            }
            console_printf("[ntclks] mount policy %s kind=ramdisk source=%s ret=%d\n",
                           entry->path,
                           entry->source,
                           root_ramdisk_status);
            if (root_ramdisk_status < 0) {
                storage_memzero(g_volumes, sizeof(g_volumes));
                g_active_volume = &g_volumes[0];
            } else {
                /* Installer mode owns / and later reuses the boot slot for
                 * the selected target ESP at /target/boot. */
                storage_memzero(&g_volumes[STORAGE_VOLUME_BOOT],
                                sizeof(g_volumes[STORAGE_VOLUME_BOOT]));
                g_active_volume = &g_volumes[STORAGE_VOLUME_ROOT];
            }
            break;
        case LEONOS_MOUNT_KIND_DEVFS:
            if (storage_text_eq(entry->path, "/dev")) {
                g_devfs_enabled = 1;
            }
            console_printf("[ntclks] mount policy %s kind=devfs enabled=%u\n",
                           entry->path,
                           g_devfs_enabled);
            break;
        case LEONOS_MOUNT_KIND_TARGET_ESP:
            console_printf("[ntclks] mount policy %s kind=installer-target flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        case LEONOS_MOUNT_KIND_TARGET_ROOT:
            console_printf("[ntclks] mount policy %s kind=installer-root flags=0x%x\n",
                           entry->path,
                           entry->flags);
            break;
        default:
            break;
        }
    }
}

int storage_mount_ramdisk_root(const void *image, uint64_t len)
{
    struct storage_volume *root = &g_volumes[0];
    uint64_t image_phys = (uint64_t)(uintptr_t)image;
    void *image_mapping;
    storage_memzero(root, sizeof(*root));
    g_active_volume = root;
    g_installer_root_active = 0;
    ahci_pending_clear();
    storage_cache_invalidate();
    if (!image || len < SECTOR_SIZE || (len % SECTOR_SIZE) != 0 ||
        !paging_kernel_direct_map_range(image_phys, len)) {
        return -22;
    }
    image_mapping = paging_kernel_direct_map(image_phys);
    if (!image_mapping) {
        return -22;
    }
    /* The installer image is a named Multiboot module.  mm_init() reserves
     * every module range before storage starts.  It is mounted through the
     * supervisor-only high direct map, which remains valid after a user CR3
     * replaces the overlapping low identity pages.  Copying a 400 MiB image
     * into another contiguous allocation would double its RAM requirement.
     * The direct map is writable for the live installer session; changes are
     * intentionally ephemeral and never modify the ISO source. */
    root->volume_id = 0;
    root->kind = STORAGE_VOLUME_RAM;
    root->filesystem = STORAGE_FILESYSTEM_FAT32;
    root->ram_base = (uint8_t *)image_mapping;
    root->ram_bytes = len;
    root->esp_start_lba = 0;
    root->esp_sector_count = len / SECTOR_SIZE;
    storage_copy_text(root->mount_path, sizeof(root->mount_path), "/");
    if (fat32_mount() < 0) {
        storage_memzero(root, sizeof(*root));
        return -2;
    }
    /* storage_init() probes physical disks before the installer module is
     * mounted. Those probes may mark a pre-existing LeonOS disk as boot_root,
     * but an ISO boot is independent of that disk and must be able to repartition
     * it. The normal disk-boot path never enters this RAM-root branch. */
    for (uint32_t i = 0; i < g_install_disk_count; ++i) {
        g_install_disks[i].boot_root = 0;
    }
    root->ready = true;
    g_installer_root_active = 1;
    console_printf("[ntclks] storage installer root ready ramdisk=%p kernel_map=%p bytes=%llu mode=direct fat32_root=%u\n",
                   image,
                   image_mapping,
                   (unsigned long long)len,
                   root->root_cluster);
    return 0;
}

void storage_init_installer_root(const struct boot_info *boot)
{
    int mount_ret = -2;
    bool found = false;
    storage_memzero(g_volumes, sizeof(g_volumes));
    g_active_volume = &g_volumes[0];
    g_installer_root_active = 0;
    storage_cache_invalidate();
    if (boot) {
        for (uint32_t i = 0; i < boot->module_count; ++i) {
            const struct boot_module *mod = &boot->modules[i];
            if (storage_text_eq(mod->name, "leonos-installer-root")) {
                found = true;
                if (mod->end > mod->start) {
                    mount_ret = storage_mount_ramdisk_root(
                        (const void *)(uintptr_t)mod->start,
                        mod->end - mod->start);
                    if (mount_ret == 0) {
                        return;
                    }
                } else {
                    mount_ret = -22;
                }
                console_printf("[ntclks] installer root ramdisk module mount failed ret=%d bytes=%llu\n",
                               mount_ret,
                               (unsigned long long)(mod->end > mod->start
                                   ? mod->end - mod->start : 0));
                return;
            }
        }
    }
    if (!found) {
        console_printf("[ntclks] installer root ramdisk module not found; ensure GRUB had enough RAM to load /install/root.fat\n");
    }
}
