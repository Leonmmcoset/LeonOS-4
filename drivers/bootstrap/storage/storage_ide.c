struct ide_device_info {
    uint8_t present;
    uint8_t atapi;
    uint8_t lba48;
    uint8_t channel;
    uint8_t drive;
    uint16_t command_base;
    uint16_t control_base;
    uint64_t sector_count;
    char model[41];
};

static void ide_io_delay(uint16_t control)
{
    if (control) {
        (void)x86_64_inb(control);
        (void)x86_64_inb(control);
        (void)x86_64_inb(control);
        (void)x86_64_inb(control);
    }
}

static uint8_t ide_status(const struct ide_device_info *device)
{
    return device ? x86_64_inb((uint16_t)(device->command_base + 7u)) : 0xffu;
}

static int ide_select(const struct ide_device_info *device)
{
    if (!device || !device->command_base || device->drive > 1u) {
        return -22;
    }
    x86_64_outb((uint8_t)(0xa0u | (device->drive << 4)),
                (uint16_t)(device->command_base + 6u));
    ide_io_delay(device->control_base);
    return 0;
}

static int ide_wait_ready(const struct ide_device_info *device, uint8_t want_drq)
{
    uint8_t status;
    if (!device) {
        return -22;
    }
    for (uint32_t spin = 0; spin < IDE_WAIT_SPINS; ++spin) {
        status = ide_status(device);
        if (status == 0xffu || status == 0x00u) {
            return -19;
        }
        if (status & IDE_STATUS_ERR) {
            return -5;
        }
        if (status & IDE_STATUS_DF) {
            return -5;
        }
        if (!(status & IDE_STATUS_BSY)) {
            if (want_drq) {
                if (status & IDE_STATUS_DRQ) {
                    return 0;
                }
            } else if (!(status & IDE_STATUS_DRQ)) {
                return 0;
            }
        }
        __asm__ volatile("pause");
    }
    return -110;
}

static int ide_soft_reset(const struct ide_device_info *device)
{
    uint8_t status;
    if (!device || !device->control_base) {
        return -22;
    }
    x86_64_outb(IDE_CMD_SRST, device->control_base);
    for (uint32_t i = 0; i < 10000u; ++i) {
        __asm__ volatile("pause");
    }
    x86_64_outb(0, device->control_base);
    for (uint32_t i = 0; i < IDE_WAIT_SPINS / 16u; ++i) {
        status = ide_status(device);
        if (!(status & IDE_STATUS_BSY)) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -110;
}

static void ide_copy_model(char out[41], const uint16_t *identify)
{
    uint32_t pos = 0;
    uint32_t start = 0;
    if (!out || !identify) {
        return;
    }
    for (uint32_t word = 27; word <= 46 && pos + 1u < 41u; ++word) {
        uint16_t value = identify[word];
        char hi = (char)(value >> 8);
        char lo = (char)(value & 0xffu);
        out[pos++] = hi ? hi : ' ';
        if (pos + 1u < 41u) {
            out[pos++] = lo ? lo : ' ';
        }
    }
    while (start < pos && out[start] == ' ') {
        ++start;
    }
    while (pos && out[pos - 1u] == ' ') {
        --pos;
    }
    if (start && start < pos) {
        uint32_t write = 0;
        while (start < pos) {
            out[write++] = out[start++];
        }
        pos = write;
    }
    out[pos] = 0;
}

static int ide_identify_device(struct ide_device_info *device)
{
    uint16_t identify[256];
    uint8_t status;
    uint8_t lba_mid;
    uint8_t lba_high;
    if (!device || !device->command_base) {
        return -22;
    }
    if (ide_select(device) < 0) {
        return -22;
    }
    status = ide_status(device);
    if (status == 0xffu || status == 0x00u) {
        return -19;
    }
    if (ide_wait_ready(device, 0) < 0) {
        return -19;
    }
    x86_64_outb(0, (uint16_t)(device->command_base + 2u));
    x86_64_outb(0, (uint16_t)(device->command_base + 3u));
    x86_64_outb(0, (uint16_t)(device->command_base + 4u));
    x86_64_outb(0, (uint16_t)(device->command_base + 5u));
    x86_64_outb(ATA_CMD_IDENTIFY_DEVICE, (uint16_t)(device->command_base + 7u));
    status = ide_status(device);
    if (status == 0x00u || status == 0xffu) {
        return -19;
    }
    for (uint32_t spin = 0; spin < IDE_WAIT_SPINS; ++spin) {
        status = ide_status(device);
        if (status & IDE_STATUS_ERR) {
            break;
        }
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRQ)) {
            for (uint32_t i = 0; i < 256u; ++i) {
                identify[i] = x86_64_inw(device->command_base);
            }
            device->present = 1;
            device->atapi = 0;
            device->lba48 = (identify[83] & (1u << 10)) != 0;
            if ((identify[106] & (1u << 14)) && (identify[106] & (1u << 12))) {
                uint32_t logical_words = ((uint32_t)identify[118] << 16) | identify[117];
                if (logical_words && logical_words != (SECTOR_SIZE / 2u)) {
                    return -95;
                }
            }
            device->sector_count = device->lba48
                                       ? ((uint64_t)identify[103] << 48) |
                                             ((uint64_t)identify[102] << 32) |
                                             ((uint64_t)identify[101] << 16) | identify[100]
                                       : ((uint64_t)identify[61] << 16) | identify[60];
            if (!device->sector_count) {
                return -5;
            }
            ide_copy_model(device->model, identify);
            return 0;
        }
        __asm__ volatile("pause");
    }

    /* A packet device reports the ATAPI signature after the IDENTIFY probe. */
    lba_mid = x86_64_inb((uint16_t)(device->command_base + 4u));
    lba_high = x86_64_inb((uint16_t)(device->command_base + 5u));
    if (lba_mid != IDE_ATAPI_SIG_MID || lba_high != IDE_ATAPI_SIG_HIGH) {
        return -5;
    }
    if (ide_select(device) < 0) {
        return -22;
    }
    x86_64_outb(IDE_CMD_IDENTIFY_PACKET, (uint16_t)(device->command_base + 7u));
    if (ide_wait_ready(device, 1) < 0) {
        return -5;
    }
    for (uint32_t i = 0; i < 256u; ++i) {
        identify[i] = x86_64_inw(device->command_base);
    }
    device->present = 1;
    device->atapi = 1;
    device->lba48 = 0;
    device->sector_count = 0;
    ide_copy_model(device->model, identify);
    return 0;
}

static int ide_pio_transfer(const struct ide_device_info *device, uint64_t lba,
                            uint32_t sectors, void *buffer, uint8_t write)
{
    uint8_t *bytes = (uint8_t *)buffer;
    uint8_t use_lba48;
    if (!device || !device->present || device->atapi || !buffer || !sectors ||
        sectors > IDE_MAX_PIO_SECTORS || lba + sectors < lba ||
        (device->sector_count && lba + sectors > device->sector_count)) {
        return -22;
    }
    use_lba48 = device->lba48;
    if (use_lba48) {
        if (ide_select(device) < 0 || ide_wait_ready(device, 0) < 0) {
            return -5;
        }
        x86_64_outb(0, (uint16_t)(device->command_base + 2u));
        x86_64_outb((uint8_t)(lba >> 24), (uint16_t)(device->command_base + 3u));
        x86_64_outb((uint8_t)(lba >> 32), (uint16_t)(device->command_base + 4u));
        x86_64_outb((uint8_t)(lba >> 40), (uint16_t)(device->command_base + 5u));
        x86_64_outb((uint8_t)(sectors >> 8), (uint16_t)(device->command_base + 2u));
        x86_64_outb((uint8_t)lba, (uint16_t)(device->command_base + 3u));
        x86_64_outb((uint8_t)(lba >> 8), (uint16_t)(device->command_base + 4u));
        x86_64_outb((uint8_t)(lba >> 16), (uint16_t)(device->command_base + 5u));
        x86_64_outb((uint8_t)(0x40u | (device->drive << 4)),
                    (uint16_t)(device->command_base + 6u));
        x86_64_outb(write ? IDE_CMD_WRITE_PIO_EXT : IDE_CMD_READ_PIO_EXT,
                    (uint16_t)(device->command_base + 7u));
    } else {
        if (lba > 0x0fffffffu || ide_select(device) < 0 || ide_wait_ready(device, 0) < 0) {
            return -22;
        }
        x86_64_outb((uint8_t)sectors, (uint16_t)(device->command_base + 2u));
        x86_64_outb((uint8_t)lba, (uint16_t)(device->command_base + 3u));
        x86_64_outb((uint8_t)(lba >> 8), (uint16_t)(device->command_base + 4u));
        x86_64_outb((uint8_t)(lba >> 16), (uint16_t)(device->command_base + 5u));
        x86_64_outb((uint8_t)(0xe0u | (device->drive << 4) | ((lba >> 24) & 0x0fu)),
                    (uint16_t)(device->command_base + 6u));
        x86_64_outb(write ? IDE_CMD_WRITE_PIO : IDE_CMD_READ_PIO,
                    (uint16_t)(device->command_base + 7u));
    }
    for (uint32_t sector = 0; sector < sectors; ++sector) {
        int wait_ret = ide_wait_ready(device, 1);
        if (wait_ret < 0) {
            (void)ide_soft_reset(device);
            return wait_ret;
        }
        uint16_t *words = (uint16_t *)(void *)(bytes + sector * SECTOR_SIZE);
        for (uint32_t i = 0; i < 256u; ++i) {
            if (write) {
                x86_64_outw(words[i], device->command_base);
            } else {
                words[i] = x86_64_inw(device->command_base);
            }
        }
    }
    {
        int wait_ret = ide_wait_ready(device, 0);
        if (wait_ret < 0) {
            (void)ide_soft_reset(device);
        }
        return wait_ret;
    }
}

static int ide_atapi_read_blocks(const struct ide_device_info *device, uint64_t lba,
                                 uint32_t blocks, void *buffer)
{
    uint8_t packet[12] = {0};
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t bytes_left = blocks * ISO9660_BLOCK_SIZE;
    if (!device || !device->present || !device->atapi || !buffer || !blocks ||
        blocks > IDE_MAX_ATAPI_BLOCKS) {
        return -22;
    }
    packet[0] = SCSI_CMD_READ10;
    packet[2] = (uint8_t)(lba >> 24);
    packet[3] = (uint8_t)(lba >> 16);
    packet[4] = (uint8_t)(lba >> 8);
    packet[5] = (uint8_t)lba;
    packet[7] = (uint8_t)(blocks >> 8);
    packet[8] = (uint8_t)blocks;
    if (ide_select(device) < 0 || ide_wait_ready(device, 0) < 0) {
        return -5;
    }
    x86_64_outb(0, (uint16_t)(device->command_base + 1u));
    x86_64_outb((uint8_t)(ISO9660_BLOCK_SIZE & 0xffu), (uint16_t)(device->command_base + 4u));
    x86_64_outb((uint8_t)(ISO9660_BLOCK_SIZE >> 8), (uint16_t)(device->command_base + 5u));
    x86_64_outb(IDE_CMD_PACKET, (uint16_t)(device->command_base + 7u));
    if (ide_wait_ready(device, 1) < 0) {
        return -5;
    }
    for (uint32_t i = 0; i < 6u; ++i) {
        x86_64_outw((uint16_t)packet[i * 2u] | ((uint16_t)packet[i * 2u + 1u] << 8),
                    device->command_base);
    }
    for (uint32_t spin = 0; bytes_left && spin < IDE_WAIT_SPINS; ++spin) {
        uint8_t status = ide_status(device);
        if (status & (IDE_STATUS_ERR | IDE_STATUS_DF)) {
            return -5;
        }
        if (status & IDE_STATUS_BSY) {
            __asm__ volatile("pause");
            continue;
        }
        if (!(status & IDE_STATUS_DRQ)) {
            if (!bytes_left) {
                break;
            }
            return -5;
        }
        uint32_t phase = (uint32_t)x86_64_inb((uint16_t)(device->command_base + 4u)) |
                         ((uint32_t)x86_64_inb((uint16_t)(device->command_base + 5u)) << 8);
        if (!phase || (phase & 1u) || phase > bytes_left) {
            return -5;
        }
        for (uint32_t i = 0; i < phase / 2u; ++i) {
            uint16_t word = x86_64_inw(device->command_base);
            dst[i * 2u] = (uint8_t)word;
            dst[i * 2u + 1u] = (uint8_t)(word >> 8);
        }
        dst += phase;
        bytes_left -= phase;
    }
    if (bytes_left) {
        (void)ide_soft_reset(device);
        return -110;
    }
    return ide_wait_ready(device, 0);
}

static void ide_fill_device(struct ide_device_info *device, uint8_t channel,
                            uint8_t drive, uint16_t command, uint16_t control)
{
    storage_memzero(device, sizeof(*device));
    device->channel = channel;
    device->drive = drive;
    device->command_base = command;
    device->control_base = control;
}

static uint32_t fat_offset_for_cluster(uint32_t cluster)
{
    return (cluster * 4u) % g_storage.bytes_per_sector;
}

