static bool ext2_statfs_super_group(uint32_t group, bool sparse)
{
    if (!sparse || group <= 1) return true;
    const uint32_t bases[] = {3, 5, 7};
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t n = group;
        while (n > 1 && n % bases[i] == 0) n /= bases[i];
        if (n == 1) return true;
    }
    return false;
}

int storage_statfs(const struct storage_node *node, struct linux_statfs_abi *value)
{
    struct storage_volume *previous = NULL;
    uint64_t flags;
    if (!node || !value) return -22;
    *value = (struct linux_statfs_abi){0};
    value->f_namelen = LEONOS_FS_NAME_LEN - 1;
    value->f_flags = LINUX_ST_VALID;
    if (node->flags & (STORAGE_NODE_FLAG_DEV_NODE | STORAGE_NODE_FLAG_DEV_DIR | STORAGE_NODE_FLAG_DEV_FB0)) {
        value->f_type = 0x858458f6; /* Linux ramfs/simple_statfs for synthetic devfs. */
        value->f_bsize = value->f_frsize = 4096;
        return 0;
    }
    kernel_execution_lock_irqsave(&flags);
    int ret = storage_select_node_volume(node, &previous);
    if (ret < 0) goto out;
    value->f_fsid[0] = node->volume_id + 1;
    switch (g_storage.filesystem) {
    case STORAGE_FILESYSTEM_FAT32:
        value->f_type = 0x4d44;
        value->f_bsize = g_storage.cluster_bytes;
        value->f_blocks = g_storage.data_cluster_count;
        if (g_storage.fat_fsinfo_valid) value->f_bfree = g_storage.fat_free_clusters;
        else for (uint32_t i = 0; i < g_storage.data_cluster_count; ++i) {
            uint32_t entry;
            ret = fat32_read_fat_entry(i + 2, &entry);
            if (ret < 0) goto restore;
            if (!entry) ++value->f_bfree;
        }
        value->f_bavail = value->f_bfree;
        break;
    case STORAGE_FILESYSTEM_EXFAT:
        value->f_type = 0x2011bab0;
        value->f_bsize = g_storage.cluster_bytes;
        value->f_blocks = g_storage.exfat_cluster_count;
        for (uint32_t i = 0; i < g_storage.exfat_cluster_count; ++i) {
            uint8_t allocated;
            ret = exfat_bitmap_get_cached(i + 2, &allocated);
            if (ret < 0) goto restore;
            if (!allocated) ++value->f_bfree;
        }
        value->f_bavail = value->f_bfree;
        break;
    case STORAGE_FILESYSTEM_EXT2: {
        struct ext2_superblock super;
        ret = storage_read_sectors(g_storage.ext2_start_lba + 2, 2, storage_scratch);
        if (ret < 0) goto restore;
        storage_memcpy(&super, storage_scratch, sizeof(super));
        if (super.magic != EXT2_SUPER_MAGIC) { ret = -5; goto restore; }
        value->f_type = EXT2_SUPER_MAGIC;
        value->f_bsize = g_storage.ext2_block_size;
        uint64_t overhead = super.first_data_block;
        uint32_t tables = (g_storage.ext2_group_count * sizeof(struct ext2_group_desc) + value->f_bsize - 1) / value->f_bsize;
        uint64_t inode_blocks = ((uint64_t)g_storage.ext2_inodes_per_group * g_storage.ext2_inode_size + value->f_bsize - 1) / value->f_bsize;
        for (uint32_t i = 0; i < g_storage.ext2_group_count; ++i) {
            struct ext2_group_desc group;
            ret = ext2_group_desc(i, &group);
            if (ret < 0) goto restore;
            value->f_bfree += group.free_blocks_count;
            value->f_ffree += group.free_inodes_count;
            if (ext2_statfs_super_group(i, super.feature_ro_compat & 1)) overhead += 1 + tables;
        }
        overhead += g_storage.ext2_group_count * (2 + inode_blocks);
        if (overhead > super.blocks_count) { ret = -5; goto restore; }
        value->f_blocks = super.blocks_count - overhead;
        value->f_bavail = value->f_bfree > super.reserved_blocks_count ? value->f_bfree - super.reserved_blocks_count : 0;
        value->f_files = super.inodes_count;
        value->f_fsid[0] = storage_get_u32(super.uuid) ^ storage_get_u32(super.uuid + 8);
        value->f_fsid[1] = storage_get_u32(super.uuid + 4) ^ storage_get_u32(super.uuid + 12);
        if (g_storage.kind == STORAGE_VOLUME_RAM) value->f_flags |= LINUX_ST_RDONLY;
        break;
    }
    case STORAGE_FILESYSTEM_ISO9660:
        value->f_type = 0x9660;
        value->f_bsize = g_storage.iso_block_size;
        value->f_blocks = g_storage.iso_sector_count;
        value->f_flags |= LINUX_ST_RDONLY;
        break;
    default:
        ret = -19;
        break;
    }
    value->f_frsize = value->f_bsize;
restore:
    storage_restore_volume(previous);
out:
    kernel_execution_unlock_irqrestore(flags);
    return ret;
}
