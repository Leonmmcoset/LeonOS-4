static void storage_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t storage_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static int install_write_sectors(struct install_disk_state *disk, uint64_t lba,
                                 uint32_t sector_count, const void *buffer)
{
    const uint8_t *src = (const uint8_t *)buffer;
    if (!disk || !disk->present || !buffer) {
        return -22;
    }
    /* Installer formatting is also a filesystem mutation: do not make its
     * multi-sector GPT/FAT update replayable after an async yield. */
    storage_begin_mutation();
    storage_cache_invalidate();
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, STORAGE_WRITE_MAX_SECTORS);
        int ret = storage_write_install_disk(disk, lba, chunk, src);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        src += chunk * SECTOR_SIZE;
    }
    return 0;
}

/**
 * @brief Reads sectors directly from a managed block disk.
 * @param disk Managed AHCI, IDE/PATA, or NVMe disk state.
 * @param lba First sector to read.
 * @param sector_count Number of sectors to read.
 * @param buffer Destination buffer.
 * @return Zero on success or a negative storage error.
 */
static int install_read_sectors(struct install_disk_state *disk, uint64_t lba,
                                uint32_t sector_count, void *buffer)
{
    uint8_t *dst = (uint8_t *)buffer;
    if (!disk || !disk->present || !buffer) {
        return -22;
    }
    while (sector_count) {
        uint32_t chunk = min_u32(sector_count, STORAGE_WRITE_MAX_SECTORS);
        int ret = storage_read_install_disk(disk, lba, chunk, dst);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
        dst += chunk * SECTOR_SIZE;
    }
    return 0;
}

static int install_clear_sectors(struct install_disk_state *disk, uint64_t lba,
                                 uint64_t sector_count)
{
    storage_memzero(storage_scratch, sizeof(storage_scratch));
    while (sector_count) {
        uint32_t chunk = (uint32_t)min_u64(sector_count, STORAGE_SCRATCH_SECTORS);
        int ret = install_write_sectors(disk, lba, chunk, storage_scratch);
        if (ret < 0) {
            return ret;
        }
        lba += chunk;
        sector_count -= chunk;
    }
    return 0;
}

static uint32_t install_choose_fat32_spc(uint64_t total_sectors)
{
    uint64_t bytes = total_sectors * SECTOR_SIZE;
    /* The ESP is intentionally small but must still meet the FAT32 minimum
     * cluster-count rule. One KiB clusters keep a 128 MiB ESP standards-sized;
     * the normal high-volume data path uses the separate exFAT root. */
    if (bytes <= 128ULL * 1024ULL * 1024ULL) return 2u;
    if (bytes <= 512ULL * 1024ULL * 1024ULL) return 4u;
    return 8u;
}

static int install_format_fat32(struct install_disk_state *disk, uint64_t start_lba,
                                uint64_t sector_count)
{
    const uint32_t reserved = 32;
    const uint32_t fat_count = 2;
    uint32_t total_sectors;
    uint32_t sectors_per_cluster;
    uint32_t fat_size = 1;
    uint32_t data_start;
    uint32_t data_sectors;
    uint32_t clusters = 0;
    if (!disk || sector_count > 0xffffffffULL || sector_count < 65536ULL) {
        return -28;
    }
    total_sectors = (uint32_t)sector_count;
    sectors_per_cluster = install_choose_fat32_spc(sector_count);
    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t next_fat_size;
        if (total_sectors <= reserved + fat_count * fat_size) {
            return -28;
        }
        data_sectors = total_sectors - reserved - fat_count * fat_size;
        clusters = data_sectors / sectors_per_cluster;
        next_fat_size = ((clusters + 2u) * 4u + SECTOR_SIZE - 1u) / SECTOR_SIZE;
        if (next_fat_size == fat_size) {
            break;
        }
        fat_size = next_fat_size;
    }
    data_start = reserved + fat_count * fat_size;
    if (clusters < 2 || data_start >= total_sectors) {
        return -28;
    }

    if (install_clear_sectors(disk, start_lba, (uint64_t)data_start + sectors_per_cluster) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    struct fat32_bpb *bpb = (struct fat32_bpb *)(void *)storage_scratch;
    bpb->jump[0] = 0xeb;
    bpb->jump[1] = 0x58;
    bpb->jump[2] = 0x90;
    storage_memcpy(bpb->oem, "LEONOS4 ", 8);
    bpb->bytes_per_sector = SECTOR_SIZE;
    bpb->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bpb->reserved_sector_count = reserved;
    bpb->fat_count = fat_count;
    bpb->root_entry_count = 0;
    bpb->total_sectors16 = 0;
    bpb->media = 0xf8;
    bpb->fat_size16 = 0;
    bpb->sectors_per_track = 63;
    bpb->head_count = 255;
    bpb->hidden_sectors = (uint32_t)start_lba;
    bpb->total_sectors32 = total_sectors;
    bpb->fat_size32 = fat_size;
    bpb->root_cluster = 2;
    bpb->fs_info = 1;
    bpb->backup_boot_sector = 6;
    bpb->drive_number = 0x80;
    bpb->boot_signature = 0x29;
    bpb->volume_id = 0x4c454f34u;
    storage_memcpy(bpb->volume_label, "LEONOS4    ", 11);
    storage_memcpy(bpb->fs_type, "FAT32   ", 8);
    storage_scratch[510] = 0x55;
    storage_scratch[511] = 0xaa;
    if (install_write_sectors(disk, start_lba, 1, storage_scratch) < 0 ||
        install_write_sectors(disk, start_lba + 6, 1, storage_scratch) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_put_u32(storage_scratch + 0, 0x41615252u);
    storage_put_u32(storage_scratch + 484, 0x61417272u);
    storage_put_u32(storage_scratch + 488, clusters - 1u);
    storage_put_u32(storage_scratch + 492, 3u);
    storage_put_u32(storage_scratch + 508, 0xaa550000u);
    if (install_write_sectors(disk, start_lba + 1, 1, storage_scratch) < 0 ||
        install_write_sectors(disk, start_lba + 7, 1, storage_scratch) < 0) {
        return -5;
    }

    for (uint32_t fat = 0; fat < fat_count; ++fat) {
        uint64_t fat_lba = start_lba + reserved + (uint64_t)fat * fat_size;
        storage_memzero(storage_scratch, SECTOR_SIZE);
        storage_put_u32(storage_scratch + 0, 0x0ffffff8u);
        storage_put_u32(storage_scratch + 4, 0xffffffffu);
        storage_put_u32(storage_scratch + 8, FAT32_EOC);
        if (install_write_sectors(disk, fat_lba, 1, storage_scratch) < 0) {
            return -5;
        }
    }

    /* FAT32 stores the volume label both in the BPB and as a root-directory
     * entry.  Writing both prevents standard repair tools from discarding the
     * label during their consistency pass. */
    storage_memzero(storage_scratch, SECTOR_SIZE);
    {
        struct fat32_dirent *label = (struct fat32_dirent *)(void *)storage_scratch;
        storage_memcpy(label->name, "LEONOS4    ", sizeof(label->name));
        label->attr = FAT32_ATTR_VOLUME;
    }
    return install_write_sectors(disk, start_lba + data_start, 1u, storage_scratch);
}

/**
 * @brief Writes one 4 KiB ext2 block during installer target formatting.
 * @param disk AHCI install target receiving the block.
 * @param start_lba First LBA of the ext2 partition.
 * @param block Zero-based ext2 block number.
 * @param data Complete 4 KiB block contents.
 * @return Zero on success or a negative storage error.
 */
static int install_write_ext2_block(struct install_disk_state *disk, uint64_t start_lba,
                                    uint32_t block, const void *data)
{
    return install_write_sectors(disk, start_lba + (uint64_t)block * 8u, 8u, data);
}

/**
 * @brief Marks a bit in an in-memory ext2 bitmap block.
 * @param bitmap Writable 4 KiB bitmap storage.
 * @param bit Zero-based block or inode bit to mark allocated.
 */
static void install_ext2_set_bit(uint8_t *bitmap, uint32_t bit)
{
    bitmap[bit / 8u] |= (uint8_t)(1u << (bit & 7u));
}

/**
 * @brief Builds one classic ext2 block-group descriptor.
 * @param descriptor Writable descriptor receiving the group's metadata locations.
 * @param group Zero-based block-group index.
 * @param group_blocks Blocks available in the group.
 * @param descriptor_blocks Number of 4 KiB blocks occupied by the descriptor table.
 * @param inode_table_blocks Number of blocks occupied by one inode table.
 * @return Zero when the group has enough space for its metadata, otherwise -ENOSPC.
 */
static int install_ext2_make_group_desc(struct ext2_group_desc *descriptor,
                                        uint32_t group, uint32_t group_blocks,
                                        uint32_t descriptor_blocks,
                                        uint32_t inode_table_blocks)
{
    uint32_t group_start;
    uint32_t metadata_blocks;
    uint32_t allocated_blocks;
    uint32_t allocated_inodes;

    if (!descriptor || descriptor_blocks == 0u) {
        return -22;
    }
    group_start = group * INSTALL_EXT2_BLOCKS_PER_GROUP;
    /* Every group stores a superblock plus a complete backup descriptor table,
     * then its block bitmap, inode bitmap, and inode table. */
    metadata_blocks = 3u + descriptor_blocks + inode_table_blocks;
    allocated_blocks = metadata_blocks + (group == 0u ? 1u : 0u);
    allocated_inodes = group == 0u ? 10u : 0u;
    if (group_blocks <= allocated_blocks) {
        return -28;
    }

    storage_memzero(descriptor, sizeof(*descriptor));
    descriptor->block_bitmap = group_start + 1u + descriptor_blocks;
    descriptor->inode_bitmap = descriptor->block_bitmap + 1u;
    descriptor->inode_table = descriptor->inode_bitmap + 1u;
    descriptor->free_blocks_count = (uint16_t)(group_blocks - allocated_blocks);
    descriptor->free_inodes_count =
        (uint16_t)(INSTALL_EXT2_INODES_PER_GROUP - allocated_inodes);
    descriptor->used_dirs_count = group == 0u ? 1u : 0u;
    return 0;
}

/**
 * @brief Creates a writable classic ext2 filesystem for the installed root.
 * @param disk Block disk containing the target partition.
 * @param start_lba First sector of the ext2 partition.
 * @param sector_count Number of sectors available to ext2.
 * @return Zero on success or a negative errno-style storage status.
 */
static int install_format_ext2(struct install_disk_state *disk, uint64_t start_lba,
                               uint64_t sector_count)
{
    uint32_t blocks;
    uint32_t group_count;
    uint32_t descriptors_per_block;
    uint32_t descriptor_blocks;
    uint32_t inode_table_blocks;
    uint32_t inodes_count;
    uint32_t free_blocks = 0;
    uint32_t free_inodes = 0;
    struct ext2_superblock super;
    int ret;
    if (!disk || sector_count < 262144ULL || sector_count / 8u > 0xffffffffULL) return -28;
    blocks = (uint32_t)(sector_count / 8u);
    group_count = (blocks + INSTALL_EXT2_BLOCKS_PER_GROUP - 1u) /
                  INSTALL_EXT2_BLOCKS_PER_GROUP;
    descriptors_per_block = INSTALL_EXT2_BLOCK_SIZE / sizeof(struct ext2_group_desc);
    descriptor_blocks = (group_count + descriptors_per_block - 1u) / descriptors_per_block;
    inode_table_blocks = (INSTALL_EXT2_INODES_PER_GROUP * 128u) / INSTALL_EXT2_BLOCK_SIZE;
    inodes_count = group_count * INSTALL_EXT2_INODES_PER_GROUP;
    if (!group_count || !descriptor_blocks ||
        blocks < (uint64_t)inode_table_blocks + descriptor_blocks + 4u) return -28;

    storage_memzero(&super, sizeof(super));
    for (uint32_t group = 0; group < group_count; ++group) {
        uint32_t group_start = group * INSTALL_EXT2_BLOCKS_PER_GROUP;
        uint32_t group_blocks = min_u32(INSTALL_EXT2_BLOCKS_PER_GROUP, blocks - group_start);
        struct ext2_group_desc descriptor;
        ret = install_ext2_make_group_desc(&descriptor, group, group_blocks,
                                           descriptor_blocks, inode_table_blocks);
        if (ret < 0) return ret;
        free_blocks += descriptor.free_blocks_count;
        free_inodes += descriptor.free_inodes_count;
    }

    super.inodes_count = inodes_count;
    super.blocks_count = blocks;
    super.free_blocks_count = free_blocks;
    super.free_inodes_count = free_inodes;
    super.first_data_block = 0;
    super.log_block_size = 2u;
    super.log_frag_size = 2;
    super.blocks_per_group = INSTALL_EXT2_BLOCKS_PER_GROUP;
    super.frags_per_group = INSTALL_EXT2_BLOCKS_PER_GROUP;
    super.inodes_per_group = INSTALL_EXT2_INODES_PER_GROUP;
    super.magic = EXT2_SUPER_MAGIC;
    super.state = 1u;
    super.errors = 1u;
    super.rev_level = EXT2_DYNAMIC_REV;
    super.first_ino = 11u;
    super.inode_size = 128u;
    super.feature_incompat = EXT2_FEATURE_INCOMPAT_FILETYPE;
    storage_memcpy(super.volume_name, "LEONOS4-ROOT", 11u);

    /* A non-sparse classic layout keeps a backup superblock and a complete
     * descriptor table at the beginning of every group. Generate one table
     * block at a time: large filesystems need more than the single 4 KiB
     * descriptor block used by the original formatter. */
    for (uint32_t group = 0; group < group_count; ++group) {
        uint32_t group_start = group * INSTALL_EXT2_BLOCKS_PER_GROUP;
        storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
        if (group == 0) {
            storage_memcpy(storage_scratch + EXT2_SUPERBLOCK_OFFSET, &super, sizeof(super));
        } else {
            storage_memcpy(storage_scratch, &super, sizeof(super));
        }
        ret = install_write_ext2_block(disk, start_lba, group_start, storage_scratch);
        if (ret < 0) return ret;
        for (uint32_t table_block = 0; table_block < descriptor_blocks; ++table_block) {
            struct ext2_group_desc *descriptors =
                (struct ext2_group_desc *)(void *)storage_scratch;
            uint32_t first_descriptor = table_block * descriptors_per_block;
            storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
            for (uint32_t slot = 0; slot < descriptors_per_block; ++slot) {
                uint32_t descriptor_group = first_descriptor + slot;
                uint32_t descriptor_start;
                uint32_t descriptor_group_blocks;
                if (descriptor_group >= group_count) {
                    break;
                }
                descriptor_start = descriptor_group * INSTALL_EXT2_BLOCKS_PER_GROUP;
                descriptor_group_blocks = min_u32(INSTALL_EXT2_BLOCKS_PER_GROUP,
                                                   blocks - descriptor_start);
                ret = install_ext2_make_group_desc(&descriptors[slot], descriptor_group,
                                                   descriptor_group_blocks,
                                                   descriptor_blocks, inode_table_blocks);
                if (ret < 0) return ret;
            }
            ret = install_write_ext2_block(disk, start_lba,
                                           group_start + 1u + table_block, storage_scratch);
            if (ret < 0) return ret;
        }
    }

    for (uint32_t group = 0; group < group_count; ++group) {
        uint32_t group_start = group * INSTALL_EXT2_BLOCKS_PER_GROUP;
        uint32_t group_blocks = min_u32(INSTALL_EXT2_BLOCKS_PER_GROUP,
                                        blocks - group_start);
        uint32_t metadata_blocks = 3u + descriptor_blocks + inode_table_blocks;
        uint32_t allocated_blocks = metadata_blocks + (group == 0u ? 1u : 0u);
        uint32_t block_bitmap = group_start + 1u + descriptor_blocks;
        uint32_t inode_bitmap = block_bitmap + 1u;
        uint32_t inode_table = inode_bitmap + 1u;
        storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
        for (uint32_t bit = 0; bit < allocated_blocks; ++bit) install_ext2_set_bit(storage_scratch, bit);
        /* e2fsck requires unavailable tail bits in the final block group to
         * be allocated.  They are outside the free-block counters. */
        for (uint32_t bit = group_blocks;
             bit < INSTALL_EXT2_BLOCK_SIZE * 8u; ++bit) {
            install_ext2_set_bit(storage_scratch, bit);
        }
        ret = install_write_ext2_block(disk, start_lba, block_bitmap, storage_scratch);
        if (ret < 0) return ret;
        storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
        if (group == 0) {
            for (uint32_t bit = 0; bit < 10u; ++bit) install_ext2_set_bit(storage_scratch, bit);
        }
        /* Each bitmap block contains more bits than this filesystem exposes
         * as inodes.  Mark the padding bits allocated in every group. */
        for (uint32_t bit = INSTALL_EXT2_INODES_PER_GROUP;
             bit < INSTALL_EXT2_BLOCK_SIZE * 8u; ++bit) {
            install_ext2_set_bit(storage_scratch, bit);
        }
        ret = install_write_ext2_block(disk, start_lba, inode_bitmap, storage_scratch);
        if (ret < 0) return ret;
        storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
        for (uint32_t table = 0; table < inode_table_blocks; ++table) {
            ret = install_write_ext2_block(disk, start_lba, inode_table + table, storage_scratch);
            if (ret < 0) return ret;
        }
    }

    storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
    {
        struct ext2_inode *root = (struct ext2_inode *)(void *)(storage_scratch + 128u);
        uint32_t root_block = 3u + descriptor_blocks + inode_table_blocks;
        root->mode = EXT2_S_IFDIR | 0755u;
        root->size_lo = INSTALL_EXT2_BLOCK_SIZE;
        root->links_count = 2u;
        root->blocks_512 = 8u;
        root->block[0] = root_block;
    }
    ret = install_write_ext2_block(disk, start_lba, 3u + descriptor_blocks, storage_scratch);
    if (ret < 0) return ret;
    storage_memzero(storage_scratch, INSTALL_EXT2_BLOCK_SIZE);
    {
        struct ext2_dirent *dot = (struct ext2_dirent *)(void *)storage_scratch;
        struct ext2_dirent *dotdot = (struct ext2_dirent *)(void *)(storage_scratch + 12u);
        dot->inode = EXT2_ROOT_INO;
        dot->rec_len = 12u;
        dot->name_len = 1u;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';
        dotdot->inode = EXT2_ROOT_INO;
        dotdot->rec_len = INSTALL_EXT2_BLOCK_SIZE - 12u;
        dotdot->name_len = 2u;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
    }
    return install_write_ext2_block(disk, start_lba,
                                    3u + descriptor_blocks + inode_table_blocks,
                                    storage_scratch);
}

static void install_utf16_name(uint16_t dst[36], const char *name)
{
    uint32_t i = 0;
    for (; i < 36 && name && name[i]; ++i) {
        dst[i] = (uint16_t)(uint8_t)name[i];
    }
    for (; i < 36; ++i) {
        dst[i] = 0;
    }
}

static uint8_t install_choose_exfat_spc_shift(uint64_t sector_count)
{
    uint64_t bytes = sector_count * SECTOR_SIZE;
    if (bytes <= 256ULL * 1024ULL * 1024ULL) return 3u;   /* 4 KiB */
    if (bytes <= 32ULL * 1024ULL * 1024ULL * 1024ULL) return 6u; /* 32 KiB */
    return 8u; /* 128 KiB */
}

static uint32_t install_exfat_boot_checksum(const uint8_t boot[SECTOR_SIZE])
{
    uint32_t sum = 0;
    for (uint32_t sector = 0; sector < 11u; ++sector) {
        for (uint32_t byte = 0; byte < SECTOR_SIZE; ++byte) {
            /* The eight extended boot sectors carry the standard 0x55AA
             * signature.  They are part of the checksum even when their
             * executable payload is intentionally empty. */
            uint8_t value = sector ?
                ((sector <= 8u && byte == 510u) ? 0x55u :
                 (sector <= 8u && byte == 511u) ? 0xaau : 0u) : boot[byte];
            if (sector == 0u && (byte == 106u || byte == 107u || byte == 112u)) continue;
            sum = (sum << 31) | (sum >> 1);
            sum += value;
        }
    }
    return sum;
}

static uint32_t install_exfat_upcase_checksum(uint32_t sum, uint8_t value)
{
    return ((sum << 31) | (sum >> 1)) + value;
}

/*
 * Create the standard single-FAT exFAT subset used by LeonOS.  The upcase
 * file is the Microsoft-recommended compressed UTF-16 table embedded by the
 * storage backend. It is the same standard table used by host mkfs.exfat.
 */
static int install_format_exfat(struct install_disk_state *disk, uint64_t start_lba,
                                uint64_t sector_count)
{
    uint8_t boot[SECTOR_SIZE];
    uint8_t spc_shift;
    uint32_t sectors_per_cluster;
    uint32_t fat_length = 1;
    uint32_t fat_offset = 24u;
    uint32_t heap_offset;
    uint32_t cluster_count = 0;
    uint64_t bitmap_bytes;
    uint32_t bitmap_clusters;
    const uint64_t upcase_bytes = exfat_standard_upcase_len;
    uint32_t upcase_clusters;
    /* Keep the system streams in the layout emitted by mkfs.exfat: the
     * allocation bitmap starts at cluster 2, the upcase stream follows it,
     * and the root directory follows both.  Some firmware/guest storage
     * implementations make assumptions about this canonical ordering. */
    uint32_t bitmap_cluster = 2u;
    uint32_t upcase_cluster;
    uint32_t root_cluster;
    uint32_t checksum = 0;
    if (!disk || sector_count < 262144ULL || sector_count > UINT32_MAX) return -28;
    spc_shift = install_choose_exfat_spc_shift(sector_count);
    sectors_per_cluster = 1u << spc_shift;
    for (uint32_t i = 0; i < 16u; ++i) {
        uint32_t next;
        heap_offset = fat_offset + fat_length;
        if (sector_count <= heap_offset) return -28;
        cluster_count = (uint32_t)((sector_count - heap_offset) / sectors_per_cluster);
        if (cluster_count < 16u) return -28;
        next = (uint32_t)((((uint64_t)cluster_count + 2u) * 4u + SECTOR_SIZE - 1u) /
                          SECTOR_SIZE);
        if (next == fat_length) break;
        fat_length = next;
    }
    heap_offset = fat_offset + fat_length;
    cluster_count = (uint32_t)((sector_count - heap_offset) / sectors_per_cluster);
    bitmap_bytes = ((uint64_t)cluster_count + 7u) / 8u;
    bitmap_clusters = (uint32_t)((bitmap_bytes + ((uint64_t)sectors_per_cluster * SECTOR_SIZE) - 1u) /
                                 ((uint64_t)sectors_per_cluster * SECTOR_SIZE));
    upcase_clusters = (uint32_t)((upcase_bytes + ((uint64_t)sectors_per_cluster * SECTOR_SIZE) - 1u) /
                                 ((uint64_t)sectors_per_cluster * SECTOR_SIZE));
    upcase_cluster = bitmap_cluster + bitmap_clusters;
    root_cluster = upcase_cluster + upcase_clusters;
    if ((uint64_t)root_cluster - 2u >= cluster_count) return -28;

    storage_begin_mutation();
    if (install_clear_sectors(disk, start_lba,
                              (uint64_t)heap_offset +
                              (uint64_t)(1u + bitmap_clusters + upcase_clusters) * sectors_per_cluster) < 0) {
        return -5;
    }

    /* exFAT system files are contiguous extents and do not require FAT links.
     * Initialize every FAT sector nevertheless, including the two reserved
     * entries and the root-directory EOC marker.  Leaving the remainder zero
     * is intentional: the bitmap, not stale FAT entries, owns allocation. */
    for (uint32_t fat_sector = 0u; fat_sector < fat_length; ++fat_sector) {
        storage_memzero(storage_scratch, SECTOR_SIZE);
        if (fat_sector == 0u) {
            storage_put_u32(storage_scratch + 0u, 0xfffffff8u);
            storage_put_u32(storage_scratch + 4u, 0xffffffffu);
        }
        /* System files are contiguous extents. Their first FAT entry is kept
         * at EOC so readers can distinguish NoFatChain streams from a
         * truncated ordinary FAT chain. */
        for (uint32_t marker = 0u; marker < 3u; ++marker) {
            uint32_t cluster = marker == 0u ? root_cluster :
                               (marker == 1u ? bitmap_cluster : upcase_cluster);
            if (cluster / (SECTOR_SIZE / 4u) == fat_sector) {
                storage_put_u32(storage_scratch + (cluster % (SECTOR_SIZE / 4u)) * 4u,
                                EXFAT_EOC);
            }
        }
        if (install_write_sectors(disk, start_lba + fat_offset + fat_sector, 1u,
                                  storage_scratch) < 0) return -5;
    }

    /* Mark root, allocation bitmap and upcase-table clusters allocated. */
    for (uint64_t bitmap_sector = 0u;
         bitmap_sector < (bitmap_bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
         ++bitmap_sector) {
        storage_memzero(storage_scratch, SECTOR_SIZE);
        uint64_t first_bit = bitmap_sector * SECTOR_SIZE * 8u;
        uint64_t last_bit = min_u64((uint64_t)cluster_count,
                                    first_bit + SECTOR_SIZE * 8u);
        for (uint64_t bit = first_bit; bit < last_bit; ++bit) {
            uint32_t cluster = (uint32_t)bit + 2u;
            if (cluster < root_cluster + 1u) {
                storage_scratch[(bit - first_bit) / 8u] |=
                    (uint8_t)(1u << ((bit - first_bit) & 7u));
            }
        }
        if (install_write_sectors(disk, start_lba + heap_offset +
                                  (uint64_t)(bitmap_cluster - 2u) * sectors_per_cluster +
                                  bitmap_sector, 1u, storage_scratch) < 0) return -5;
    }

    /* Write the recommended compressed standard exFAT upcase table. */
    for (uint64_t byte_offset = 0; byte_offset < upcase_bytes; ) {
        uint32_t bytes = (uint32_t)min_u64(sizeof(storage_scratch), upcase_bytes - byte_offset);
        uint32_t sectors = (bytes + SECTOR_SIZE - 1u) / SECTOR_SIZE;
        storage_memzero(storage_scratch, sizeof(storage_scratch));
        storage_memcpy(storage_scratch, exfat_standard_upcase + byte_offset, bytes);
        for (uint32_t off = 0; off < bytes; ++off) {
            checksum = install_exfat_upcase_checksum(checksum, storage_scratch[off]);
        }
        if (install_write_sectors(disk, start_lba + heap_offset +
                                  (uint64_t)(upcase_cluster - 2u) * sectors_per_cluster +
                                  byte_offset / SECTOR_SIZE, sectors, storage_scratch) < 0) return -5;
        byte_offset += bytes;
    }

    /* Root directory system entries: volume label, allocation bitmap, upcase table. */
    storage_memzero(storage_scratch, sizeof(storage_scratch));
    storage_scratch[0] = 0x83u;
    /* exFAT volume labels are limited to 11 UTF-16 units; the GPT partition
     * name remains the full LEONOS4_ROOT identifier. */
    storage_scratch[1] = 11u;
    for (uint32_t i = 0; i < 11u; ++i) exfat_put_u16(storage_scratch + 2u + i * 2u, "LEONOS4ROOT"[i]);
    storage_scratch[32u] = EXFAT_ENTRY_BITMAP;
    storage_put_u32(storage_scratch + 32u + 20u, bitmap_cluster);
    exfat_put_u64(storage_scratch + 32u + 24u, bitmap_bytes);
    storage_scratch[64u] = EXFAT_ENTRY_UPCASE;
    storage_put_u32(storage_scratch + 64u + 4u, checksum);
    storage_put_u32(storage_scratch + 64u + 20u, upcase_cluster);
    exfat_put_u64(storage_scratch + 64u + 24u, upcase_bytes);
    if (install_write_sectors(disk, start_lba + heap_offset +
                              (uint64_t)(root_cluster - 2u) * sectors_per_cluster,
                              STORAGE_SCRATCH_SECTORS,
                              storage_scratch) < 0) return -5;

    storage_memzero(boot, sizeof(boot));
    boot[0] = 0xebu; boot[1] = 0x76u; boot[2] = 0x90u;
    storage_memcpy(boot + 3u, "EXFAT   ", 8u);
    exfat_put_u64(boot + 64u, start_lba);
    exfat_put_u64(boot + 72u, sector_count);
    storage_put_u32(boot + 80u, fat_offset);
    storage_put_u32(boot + 84u, fat_length);
    storage_put_u32(boot + 88u, heap_offset);
    storage_put_u32(boot + 92u, cluster_count);
    storage_put_u32(boot + 96u, root_cluster);
    storage_put_u32(boot + 100u, 0x4c344658u);
    exfat_put_u16(boot + 104u, 0x0100u);
    boot[108u] = 9u;
    boot[109u] = spc_shift;
    boot[110u] = 1u;
    boot[111u] = 0x80u;
    boot[112u] = (uint8_t)(((uint64_t)(1u + bitmap_clusters + upcase_clusters) * 100u) / cluster_count);
    boot[510u] = 0x55u; boot[511u] = 0xaau;
    if (install_write_sectors(disk, start_lba, 1u, boot) < 0 ||
        install_write_sectors(disk, start_lba + 12u, 1u, boot) < 0) return -5;
    storage_memzero(storage_scratch, sizeof(storage_scratch));
    for (uint32_t sector = 1u; sector < 11u; ++sector) {
        if (sector <= 8u) {
            storage_scratch[510u] = 0x55u;
            storage_scratch[511u] = 0xaau;
        }
        if (install_write_sectors(disk, start_lba + sector, 1u, storage_scratch) < 0 ||
            install_write_sectors(disk, start_lba + 12u + sector, 1u, storage_scratch) < 0) return -5;
        storage_scratch[510u] = 0u;
        storage_scratch[511u] = 0u;
    }
    storage_memzero(storage_scratch, SECTOR_SIZE);
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset += 4u) {
        storage_put_u32(storage_scratch + offset, install_exfat_boot_checksum(boot));
    }
    if (install_write_sectors(disk, start_lba + 11u, 1u, storage_scratch) < 0 ||
        install_write_sectors(disk, start_lba + 23u, 1u, storage_scratch) < 0) return -5;
    return 0;
}

static int install_write_gpt(struct install_disk_state *disk, uint64_t sector_count,
                             uint64_t *out_esp_lba, uint64_t *out_esp_sectors,
                             uint64_t *out_root_lba, uint64_t *out_root_sectors)
{
    uint8_t disk_guid[16];
    uint8_t esp_part_guid[16];
    uint8_t root_part_guid[16];
    uint64_t last_lba;
    uint64_t backup_entries_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint32_t table_bytes = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE;
    uint32_t table_sectors = table_bytes / SECTOR_SIZE;
    uint32_t table_crc;
    uint64_t esp_last;
    uint64_t root_first;
    if (!disk || !out_esp_lba || !out_esp_sectors || !out_root_lba || !out_root_sectors ||
        sector_count < 655360ULL) {
        return -28;
    }
    storage_make_guid(disk_guid, 0x4c3447494449534bULL, disk, sector_count);
    storage_make_guid(esp_part_guid, 0x4c34474944504152ULL, disk, sector_count);
    storage_make_guid(root_part_guid, 0x4c34474944525431ULL, disk, sector_count);
    last_lba = sector_count - 1u;
    backup_entries_lba = last_lba - table_sectors;
    first_usable = INSTALL_ESP_FIRST_LBA;
    last_usable = backup_entries_lba - 1u;
    esp_last = first_usable + INSTALL_ESP_SECTORS - 1u;
    root_first = (esp_last + 1u + 2047u) & ~2047ULL;
    if (esp_last >= last_usable || root_first >= last_usable ||
        last_usable - root_first + 1u < 262144ULL) {
        return -28;
    }

    if (install_clear_sectors(disk, 0, first_usable) < 0 ||
        install_clear_sectors(disk, backup_entries_lba, table_sectors + 1u) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    storage_scratch[446 + 4] = 0xee;
    storage_put_u32(storage_scratch + 446 + 8, 1);
    storage_put_u32(storage_scratch + 446 + 12,
                    sector_count - 1u > 0xffffffffULL ? 0xffffffffu : (uint32_t)(sector_count - 1u));
    storage_scratch[510] = 0x55;
    storage_scratch[511] = 0xaa;
    if (install_write_sectors(disk, 0, 1, storage_scratch) < 0) {
        return -5;
    }

    storage_memzero(storage_cluster_buf, table_bytes);
    struct gpt_entry *entry = (struct gpt_entry *)(void *)storage_cluster_buf;
    storage_memcpy(entry->type_guid, esp_guid, sizeof(entry->type_guid));
    storage_memcpy(entry->unique_guid, esp_part_guid, sizeof(entry->unique_guid));
    entry->first_lba = first_usable;
    entry->last_lba = esp_last;
    entry->attrs = 0;
    install_utf16_name(entry->name, "LeonOS 4 ESP");
    ++entry;
    storage_memcpy(entry->type_guid, basic_data_guid, sizeof(entry->type_guid));
    storage_memcpy(entry->unique_guid, root_part_guid, sizeof(entry->unique_guid));
    entry->first_lba = root_first;
    entry->last_lba = last_usable;
    entry->attrs = 0;
    install_utf16_name(entry->name, "LEONOS4_ROOT");
    table_crc = storage_crc32(storage_cluster_buf, table_bytes);
    if (install_write_sectors(disk, 2, table_sectors, storage_cluster_buf) < 0 ||
        install_write_sectors(disk, backup_entries_lba, table_sectors, storage_cluster_buf) < 0) {
        return -5;
    }

    storage_memzero(storage_scratch, SECTOR_SIZE);
    struct gpt_header *hdr = (struct gpt_header *)(void *)storage_scratch;
    hdr->signature = 0x5452415020494645ULL;
    hdr->revision = 0x00010000u;
    hdr->header_size = sizeof(struct gpt_header);
    hdr->current_lba = 1;
    hdr->backup_lba = last_lba;
    hdr->first_usable_lba = first_usable;
    hdr->last_usable_lba = last_usable;
    storage_memcpy(hdr->disk_guid, disk_guid, sizeof(hdr->disk_guid));
    hdr->partition_entries_lba = 2;
    hdr->partition_entry_count = GPT_ENTRY_COUNT;
    hdr->partition_entry_size = GPT_ENTRY_SIZE;
    hdr->partition_entries_crc32 = table_crc;
    hdr->header_crc32 = 0;
    hdr->header_crc32 = storage_crc32(hdr, hdr->header_size);
    if (install_write_sectors(disk, 1, 1, storage_scratch) < 0) {
        return -5;
    }

    hdr->header_crc32 = 0;
    hdr->current_lba = last_lba;
    hdr->backup_lba = 1;
    hdr->partition_entries_lba = backup_entries_lba;
    hdr->header_crc32 = storage_crc32(hdr, hdr->header_size);
    if (install_write_sectors(disk, last_lba, 1, storage_scratch) < 0) {
        return -5;
    }

    *out_esp_lba = first_usable;
    *out_esp_sectors = INSTALL_ESP_SECTORS;
    *out_root_lba = root_first;
    *out_root_sectors = last_usable - root_first + 1u;
    return 0;
}
