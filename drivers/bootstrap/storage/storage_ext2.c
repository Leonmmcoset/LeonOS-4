/* ext2 implementation is included by the storage facade after the AHCI and common
 * storage helpers. It deliberately supports the classic on-disk format only:
 * 1/2/4 KiB blocks, 128+ byte inodes, direct and indirect block pointers,
 * and no journal, extents, encryption, or metadata checksums. */

/** @brief Returns the on-disk byte length of an ext2 inode. */
static uint64_t ext2_inode_size(const struct ext2_inode *inode)
{
    return (uint64_t)inode->size_lo | ((uint64_t)inode->size_high << 32);
}

/** @brief Writes a bounded 64-bit file size back to a classic ext2 inode. */
static void ext2_set_inode_size(struct ext2_inode *inode, uint64_t size)
{
    inode->size_lo = (uint32_t)size;
    inode->size_high = (uint32_t)(size >> 32);
}

/* A virtual AHCI device can reject a multi-sector DMA command while accepting
 * the same sectors as individual commands. Keep the recovery bounded and
 * rate-limit diagnostics so a damaged volume does not flood the boot log. */
static uint32_t ext2_read_recovery_logs;

/** @brief Reads sectors as a block, then retries one sector at a time. */
static int ext2_read_sectors_resilient(uint64_t lba, uint32_t sectors,
                                       void *buffer)
{
    uint8_t *dst = (uint8_t *)buffer;
    int ret = storage_read_sectors(lba, sectors, buffer);
    if (ret >= 0 || ret == -LEONOS_EAGAIN) {
        return ret;
    }
    if (ext2_read_recovery_logs < 8u) {
        console_printf("[ntclks] ext2 bulk read failed lba=%llu sectors=%u ret=%d; "
                       "retrying sectors\n", (unsigned long long)lba, sectors, ret);
        ++ext2_read_recovery_logs;
    }
    for (uint32_t i = 0; i < sectors; ++i) {
        ret = storage_read_sectors(lba + i, 1u, dst + i * SECTOR_SIZE);
        if (ret < 0) {
            if (ret == -LEONOS_EAGAIN) {
                return ret;
            }
            if (ext2_read_recovery_logs < 8u) {
                console_printf("[ntclks] ext2 sector read failed lba=%llu ret=%d\n",
                               (unsigned long long)(lba + i), ret);
                ++ext2_read_recovery_logs;
            }
            return ret;
        }
    }
    if (ext2_read_recovery_logs < 8u) {
        console_printf("[ntclks] ext2 sector fallback recovered lba=%llu sectors=%u\n",
                       (unsigned long long)lba, sectors);
        ++ext2_read_recovery_logs;
    }
    return 0;
}

/**
 * @brief Reads one ext2 filesystem block through the shared read-ahead cache.
 * @param block Physical ext2 block number on the active volume.
 * @param buffer Writable destination with at least one ext2 block available.
 * @return Zero on success or a negative storage error.
 */
static int ext2_read_block(uint32_t block, void *buffer)
{
    uint32_t sectors;
    uint32_t cache_sectors;
    uint64_t lba;
    uint64_t cache_end;
    uint64_t remaining_sectors;
    uint32_t cache_offset;
    int ret;
    if (!buffer || !g_storage.ext2_block_size || block >= g_storage.ext2_blocks_count) {
        return -22;
    }
    sectors = g_storage.ext2_block_size / SECTOR_SIZE;
    lba = g_storage.ext2_start_lba + (uint64_t)block * sectors;
    cache_end = storage_read_cache.first_lba + storage_read_cache.sector_count;
    if (storage_read_cache.valid && storage_read_cache.volume == g_active_volume &&
        lba >= storage_read_cache.first_lba && lba + sectors <= cache_end) {
        cache_offset = (uint32_t)(lba - storage_read_cache.first_lba) * SECTOR_SIZE;
        storage_memcpy(buffer, storage_read_cache_data + cache_offset,
                       g_storage.ext2_block_size);
        return 0;
    }

    /* Header-heavy workloads such as TinyCC open thousands of small files.
     * Fill the same bounded cache used by FAT32, but index it by physical LBA
     * so ext2 direct and indirect blocks need no filesystem-specific cache. */
    remaining_sectors = (uint64_t)(g_storage.ext2_blocks_count - block) * sectors;
    cache_sectors = remaining_sectors > STORAGE_READAHEAD_SECTORS
        ? STORAGE_READAHEAD_SECTORS : (uint32_t)remaining_sectors;
    storage_read_cache.valid = 0;
    ret = ext2_read_sectors_resilient(lba, cache_sectors, storage_read_cache_data);
    if (ret < 0) {
        return ret;
    }
    storage_read_cache.volume = g_active_volume;
    storage_read_cache.first_lba = lba;
    storage_read_cache.sector_count = cache_sectors;
    storage_read_cache.valid = 1;
    storage_memcpy(buffer, storage_read_cache_data, g_storage.ext2_block_size);
    return 0;
}

/** @brief Writes one ext2 filesystem block to the currently selected volume. */
static int ext2_write_block(uint32_t block, const void *buffer)
{
    uint32_t sectors;
    if (!buffer || !g_storage.ext2_block_size || block >= g_storage.ext2_blocks_count ||
        g_storage.kind == STORAGE_VOLUME_RAM) {
        return -30;
    }
    sectors = g_storage.ext2_block_size / SECTOR_SIZE;
    return storage_write_sectors(g_storage.ext2_start_lba + (uint64_t)block * sectors,
                                 sectors, buffer);
}

/** @brief Reads one group descriptor from the classic ext2 descriptor table. */
static int ext2_group_desc(uint32_t group, struct ext2_group_desc *out)
{
    uint32_t per_block;
    uint32_t block;
    uint32_t offset;
    int ret;
    if (!out || group >= g_storage.ext2_group_count) {
        return -22;
    }
    per_block = g_storage.ext2_block_size / sizeof(*out);
    block = g_storage.ext2_block_size == 1024u ? 2u : 1u;
    block += group / per_block;
    offset = (group % per_block) * sizeof(*out);
    ret = ext2_read_block(block, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memcpy(out, storage_scratch + offset, sizeof(*out));
    return 0;
}

/** @brief Stores one group descriptor after changing its allocation metadata. */
static int ext2_write_group_desc(uint32_t group, const struct ext2_group_desc *in)
{
    uint32_t per_block;
    uint32_t block;
    uint32_t offset;
    int ret;
    if (!in || group >= g_storage.ext2_group_count) {
        return -22;
    }
    per_block = g_storage.ext2_block_size / sizeof(*in);
    block = g_storage.ext2_block_size == 1024u ? 2u : 1u;
    block += group / per_block;
    offset = (group % per_block) * sizeof(*in);
    ret = ext2_read_block(block, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memcpy(storage_scratch + offset, in, sizeof(*in));
    return ext2_write_block(block, storage_scratch);
}

/** @brief Reads an inode by its one-based ext2 inode number. */
static int ext2_read_inode(uint32_t number, struct ext2_inode *out)
{
    struct ext2_group_desc group;
    uint32_t index;
    uint32_t table_block;
    uint32_t offset;
    int ret;
    if (!out || number == 0 || number > g_storage.ext2_inodes_per_group * g_storage.ext2_group_count) {
        return -22;
    }
    index = number - 1u;
    ret = ext2_group_desc(index / g_storage.ext2_inodes_per_group, &group);
    if (ret < 0) {
        return ret;
    }
    index %= g_storage.ext2_inodes_per_group;
    table_block = group.inode_table + (index * g_storage.ext2_inode_size) / g_storage.ext2_block_size;
    offset = (index * g_storage.ext2_inode_size) % g_storage.ext2_block_size;
    ret = ext2_read_block(table_block, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memzero(out, sizeof(*out));
    storage_memcpy(out, storage_scratch + offset,
                   min_u32(sizeof(*out), g_storage.ext2_inode_size));
    return 0;
}

/** @brief Persists an inode while preserving unsupported trailing inode bytes. */
static int ext2_write_inode(uint32_t number, const struct ext2_inode *in)
{
    struct ext2_group_desc group;
    uint32_t index;
    uint32_t table_block;
    uint32_t offset;
    int ret;
    if (!in || number == 0 || number > g_storage.ext2_inodes_per_group * g_storage.ext2_group_count) {
        return -22;
    }
    index = number - 1u;
    ret = ext2_group_desc(index / g_storage.ext2_inodes_per_group, &group);
    if (ret < 0) {
        return ret;
    }
    index %= g_storage.ext2_inodes_per_group;
    table_block = group.inode_table + (index * g_storage.ext2_inode_size) / g_storage.ext2_block_size;
    offset = (index * g_storage.ext2_inode_size) % g_storage.ext2_block_size;
    ret = ext2_read_block(table_block, storage_scratch);
    if (ret < 0) {
        return ret;
    }
    storage_memcpy(storage_scratch + offset, in,
                   min_u32(sizeof(*in), g_storage.ext2_inode_size));
    return ext2_write_block(table_block, storage_scratch);
}

/** @brief Converts ext2 inode mode bits to the public LeonOS node kind. */
static uint32_t ext2_node_type(const struct ext2_inode *inode)
{
    return (inode->mode & EXT2_S_IFMT) == EXT2_S_IFDIR ? LEONOS_FS_TYPE_DIR : LEONOS_FS_TYPE_FILE;
}

/** @brief Fills a public storage node from an inode already read from ext2. */
static void ext2_fill_node(uint32_t ino, const struct ext2_inode *inode,
                           struct storage_node *out)
{
    out->type = ext2_node_type(inode);
    out->flags = STORAGE_NODE_FLAG_EXT2 | (ino == EXT2_ROOT_INO ? STORAGE_NODE_FLAG_ROOT : 0u);
    out->first_cluster = ino;
    out->volume_id = g_storage.volume_id;
    out->size = ext2_inode_size(inode);
}

/**
 * @brief Resolves a logical file block to an ext2 physical block.
 * @param inode Mutable inode containing direct and indirect block pointers.
 * @param index Zero-based logical block index.
 * @param create Nonzero to allocate missing pointer/data blocks.
 * @param out_block Receives the physical block number, or zero for a hole.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_get_block(struct ext2_inode *inode, uint32_t index,
                          uint8_t create, uint32_t *out_block);
static int ext2_allocate_block(uint32_t *out_block);
static int ext2_dirent_is_acl_metadata(const struct ext2_dirent *entry);

/** @brief Finds a named directory entry in an ext2 directory inode. */
static int ext2_find_in_dir(uint32_t directory_ino, const char *name,
                            struct storage_node *out, uint32_t *out_offset)
{
    struct ext2_inode directory;
    uint64_t size;
    uint32_t offset = 0;
    uint32_t block_size = g_storage.ext2_block_size;
    if (storage_dir_index_lookup(directory_ino, name, out)) {
        return 0;
    }
    int ret = ext2_read_inode(directory_ino, &directory);
    if (ret < 0 || ext2_node_type(&directory) != LEONOS_FS_TYPE_DIR || !name || !name[0]) {
        return ret < 0 ? ret : -20;
    }
    size = ext2_inode_size(&directory);
    while (offset < size) {
        uint32_t file_block = offset / block_size;
        uint32_t physical = 0;
        uint32_t pos;
        uint32_t block_limit;
        ret = ext2_get_block(&directory, file_block, 0, &physical);
        if (ret < 0) return ret;
        if (!physical) {
            return -5;
        }
        ret = ext2_read_block(physical, storage_cluster_buf);
        if (ret < 0) {
            return ret;
        }
        pos = offset % block_size;
        block_limit = min_u32(block_size, (uint32_t)(size - offset + pos));
        while (pos + 8u <= block_limit) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)(storage_cluster_buf + pos);
            if (entry->rec_len < 8u || (entry->rec_len & 3u) != 0 || pos + entry->rec_len > block_size ||
                entry->name_len > entry->rec_len - 8u) {
                return -5;
            }
            if (entry->inode && entry->name_len && storage_strlen(name) == entry->name_len) {
                uint32_t i;
                int match = 1;
                for (i = 0; i < entry->name_len; ++i) {
                    char a = entry->name[i];
                    char b = name[i];
                    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                    if (a != b) { match = 0; break; }
                }
                if (match) {
                    struct ext2_inode child;
                    ret = ext2_read_inode(entry->inode, &child);
                    if (ret < 0) return ret;
                    {
                        struct storage_node found;
                        ext2_fill_node(entry->inode, &child, &found);
                        storage_dir_index_store(directory_ino, name, &found);
                        if (out) *out = found;
                    }
                    if (out_offset) *out_offset = offset + (pos - offset % block_size);
                    return 0;
                }
            }
            pos += entry->rec_len;
        }
        offset = (file_block + 1u) * block_size;
    }
    return -2;
}

/** @brief Resolves a normalized filesystem-local path through ext2 directories. */
static int ext2_lookup_path(const char *path, struct storage_node *out)
{
    struct storage_node node;
    const char *cursor;
    char name[LEONOS_FS_NAME_LEN];
    uint32_t length = 0;
    struct ext2_inode root;
    int ret;
    if (!path || path[0] != '/') return -22;
    ret = ext2_read_inode(EXT2_ROOT_INO, &root);
    if (ret < 0) return ret;
    ext2_fill_node(EXT2_ROOT_INO, &root, &node);
    cursor = path + 1;
    while (*cursor) {
        if (*cursor == '/') {
            if (length) {
                name[length] = 0;
                ret = ext2_find_in_dir(node.first_cluster, name, &node, 0);
                if (ret < 0) return ret;
                length = 0;
            }
        } else {
            if (length + 1u >= sizeof(name)) return -22;
            name[length++] = *cursor;
        }
        ++cursor;
    }
    if (length) {
        name[length] = 0;
        ret = ext2_find_in_dir(node.first_cluster, name, &node, 0);
        if (ret < 0) return ret;
    }
    if (out) *out = node;
    return 0;
}

/** @brief Reads a regular ext2 file range into a caller buffer. */
static int ext2_read_node(const struct storage_node *node, uint64_t offset,
                          void *buffer, uint32_t len, uint32_t *out_read)
{
    struct ext2_inode inode;
    uint8_t *dst = buffer;
    uint32_t done = 0;
    int ret;
    if (out_read) *out_read = 0;
    if (!node || !buffer || node->type != LEONOS_FS_TYPE_FILE) return -22;
    ret = ext2_read_inode(node->first_cluster, &inode);
    if (ret < 0) {
        if (ret != -LEONOS_EAGAIN) {
            console_printf("[ntclks] ext2 inode read failed inode=%u offset=%llu ret=%d\n",
                           node->first_cluster, (unsigned long long)offset, ret);
        }
        return ret;
    }
    if (offset >= ext2_inode_size(&inode)) return 0;
    if (len > ext2_inode_size(&inode) - offset) len = (uint32_t)(ext2_inode_size(&inode) - offset);
    while (done < len) {
        uint32_t logical = (uint32_t)((offset + done) / g_storage.ext2_block_size);
        uint32_t block_offset = (uint32_t)((offset + done) % g_storage.ext2_block_size);
        uint32_t block = 0;
        uint32_t take = min_u32(g_storage.ext2_block_size - block_offset, len - done);
        ret = ext2_get_block(&inode, logical, 0, &block);
        if (ret < 0) {
            if (ret != -LEONOS_EAGAIN) {
                console_printf("[ntclks] ext2 block map failed inode=%u offset=%llu "
                               "logical=%u ret=%d\n", node->first_cluster,
                               (unsigned long long)(offset + done), logical, ret);
            }
            return ret;
        }
        if (block) {
            ret = ext2_read_block(block, storage_cluster_buf);
            if (ret < 0) {
                if (ret != -LEONOS_EAGAIN) {
                    console_printf("[ntclks] ext2 data read failed inode=%u offset=%llu "
                                   "logical=%u block=%u ret=%d\n", node->first_cluster,
                                   (unsigned long long)(offset + done), logical, block, ret);
                }
                return ret;
            }
            storage_memcpy(dst + done, storage_cluster_buf + block_offset, take);
        } else {
            storage_memzero(dst + done, take);
        }
        done += take;
    }
    if (out_read) *out_read = done;
    return 0;
}

/** @brief Allocates an ext2 data block by setting a free bit in a group bitmap. */
static int ext2_alloc_block(uint32_t *out_block)
{
    uint32_t groups = g_storage.ext2_group_count;
    for (uint32_t group_index = 0; group_index < groups; ++group_index) {
        struct ext2_group_desc group;
        uint32_t group_start = g_storage.ext2_first_data_block + group_index * g_storage.ext2_blocks_per_group;
        uint32_t group_blocks;
        if (group_start >= g_storage.ext2_blocks_count) continue;
        group_blocks = min_u32(g_storage.ext2_blocks_per_group,
                               g_storage.ext2_blocks_count - group_start);
        int ret = ext2_group_desc(group_index, &group);
        if (ret < 0) return ret;
        ret = ext2_read_block(group.block_bitmap, storage_scratch);
        if (ret < 0) return ret;
        for (uint32_t bit = 0; bit < group_blocks; ++bit) {
            if ((storage_scratch[bit / 8u] & (1u << (bit & 7u))) == 0) {
                storage_scratch[bit / 8u] |= (uint8_t)(1u << (bit & 7u));
                ret = ext2_write_block(group.block_bitmap, storage_scratch);
                if (ret < 0) return ret;
                *out_block = group_start + bit;
                storage_memzero(storage_cluster_buf, g_storage.ext2_block_size);
                return ext2_write_block(*out_block, storage_cluster_buf);
            }
        }
    }
    return -28;
}

/** @brief Releases a data block allocation bit after unlinking an ext2 inode. */
static void ext2_free_block(uint32_t block)
{
    uint32_t relative;
    uint32_t group_index;
    struct ext2_group_desc group;
    if (block < g_storage.ext2_first_data_block || block >= g_storage.ext2_blocks_count) return;
    relative = block - g_storage.ext2_first_data_block;
    group_index = relative / g_storage.ext2_blocks_per_group;
    if (ext2_group_desc(group_index, &group) < 0 || ext2_read_block(group.block_bitmap, storage_scratch) < 0) return;
    relative %= g_storage.ext2_blocks_per_group;
    storage_scratch[relative / 8u] &= (uint8_t)~(1u << (relative & 7u));
    (void)ext2_write_block(group.block_bitmap, storage_scratch);
}

/** @brief Allocates a non-reserved inode from the ext2 inode bitmaps. */
static int ext2_alloc_inode(uint32_t *out_inode)
{
    for (uint32_t group_index = 0; group_index < g_storage.ext2_group_count; ++group_index) {
        struct ext2_group_desc group;
        int ret = ext2_group_desc(group_index, &group);
        if (ret < 0) return ret;
        ret = ext2_read_block(group.inode_bitmap, storage_scratch);
        if (ret < 0) return ret;
        for (uint32_t bit = 0; bit < g_storage.ext2_inodes_per_group; ++bit) {
            uint32_t ino = group_index * g_storage.ext2_inodes_per_group + bit + 1u;
            if (ino <= 10u || (storage_scratch[bit / 8u] & (1u << (bit & 7u)))) continue;
            storage_scratch[bit / 8u] |= (uint8_t)(1u << (bit & 7u));
            ret = ext2_write_block(group.inode_bitmap, storage_scratch);
            if (ret < 0) return ret;
            *out_inode = ino;
            return 0;
        }
    }
    return -28;
}

/** @brief Marks an ext2 inode reusable after its links and data are removed. */
static void ext2_free_inode(uint32_t inode)
{
    struct ext2_group_desc group;
    uint32_t index;
    if (!inode || inode <= 10u) return;
    index = inode - 1u;
    if (ext2_group_desc(index / g_storage.ext2_inodes_per_group, &group) < 0 ||
        ext2_read_block(group.inode_bitmap, storage_scratch) < 0) return;
    index %= g_storage.ext2_inodes_per_group;
    storage_scratch[index / 8u] &= (uint8_t)~(1u << (index & 7u));
    (void)ext2_write_block(group.inode_bitmap, storage_scratch);
}

/** @brief Resolves direct, singly indirect, and doubly indirect ext2 blocks. */
static int ext2_get_block(struct ext2_inode *inode, uint32_t index,
                          uint8_t create, uint32_t *out_block)
{
    uint32_t per = g_storage.ext2_block_size / sizeof(uint32_t);
    uint32_t *table = (uint32_t *)(void *)storage_fat_cache_data;
    uint32_t *child_table = (uint32_t *)(void *)storage_cluster_buf;
    uint32_t pointer_index;
    uint32_t child_pointer_block;
    int ret;
    if (!inode || !out_block) return -22;
    if (index < 12u) {
        if (!inode->block[index] && create) {
            ret = ext2_allocate_block(&inode->block[index]);
            if (ret < 0) return ret;
            inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
        }
        *out_block = inode->block[index];
        return 0;
    }
    index -= 12u;
    if (index < per) {
        if (!inode->block[12] && create) {
            ret = ext2_allocate_block(&inode->block[12]);
            if (ret < 0) return ret;
            inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
        }
        if (!inode->block[12]) { *out_block = 0; return 0; }
        ret = ext2_read_block(inode->block[12], table);
        if (ret < 0) return ret;
        if (!table[index] && create) {
            ret = ext2_allocate_block(&table[index]);
            if (ret < 0) return ret;
            ret = ext2_write_block(inode->block[12], table);
            if (ret < 0) return ret;
            inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
        }
        *out_block = table[index];
        return 0;
    }
    index -= per;
    if (index >= per * per) return -28;
    if (!inode->block[13] && create) {
        ret = ext2_allocate_block(&inode->block[13]);
        if (ret < 0) return ret;
        inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
    }
    if (!inode->block[13]) { *out_block = 0; return 0; }
    ret = ext2_read_block(inode->block[13], table);
    if (ret < 0) return ret;
    pointer_index = index / per;
    if (!table[pointer_index] && create) {
        ret = ext2_allocate_block(&table[pointer_index]);
        if (ret < 0) return ret;
        ret = ext2_write_block(inode->block[13], table);
        if (ret < 0) return ret;
        inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
    }
    if (!table[pointer_index]) { *out_block = 0; return 0; }
    child_pointer_block = table[pointer_index];
    ret = ext2_read_block(child_pointer_block, child_table);
    if (ret < 0) return ret;
    pointer_index = index % per;
    if (!child_table[pointer_index] && create) {
        uint32_t data_block;
        /* ext2_allocate_block() clears storage_cluster_buf for the new data
         * block. Reload this child pointer table before recording its entry. */
        ret = ext2_allocate_block(&data_block);
        if (ret < 0) return ret;
        ret = ext2_read_block(child_pointer_block, child_table);
        if (ret < 0) return ret;
        child_table[pointer_index] = data_block;
        ret = ext2_write_block(child_pointer_block, child_table);
        if (ret < 0) return ret;
        inode->blocks_512 += g_storage.ext2_block_size / SECTOR_SIZE;
    }
    *out_block = child_table[pointer_index];
    return 0;
}

/** @brief Enumerates a public directory entry by ordinal from an ext2 directory. */
static int ext2_iter_dir_entry(uint32_t directory_ino, uint64_t wanted,
                               struct leonos_dir_entry *out)
{
    struct ext2_inode directory;
    uint64_t size;
    uint64_t ordinal = 0;
    uint32_t offset = 0;
    int ret = ext2_read_inode(directory_ino, &directory);
    if (ret < 0 || ext2_node_type(&directory) != LEONOS_FS_TYPE_DIR || !out) return ret < 0 ? ret : -20;
    size = ext2_inode_size(&directory);
    while (offset < size) {
        uint32_t logical = offset / g_storage.ext2_block_size;
        uint32_t block = 0;
        uint32_t pos;
        ret = ext2_get_block(&directory, logical, 0, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -5;
        ret = ext2_read_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        for (pos = 0; pos + 8u <= g_storage.ext2_block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)(storage_cluster_buf + pos);
            if (entry->rec_len < 8u || (entry->rec_len & 3u) || pos + entry->rec_len > g_storage.ext2_block_size ||
                entry->name_len > entry->rec_len - 8u) return -5;
            if (entry->inode && entry->name_len && !ext2_dirent_is_acl_metadata(entry)) {
                if (ordinal == wanted) {
                    struct ext2_inode child;
                    uint32_t take = min_u32(entry->name_len, LEONOS_FS_NAME_LEN - 1u);
                    ret = ext2_read_inode(entry->inode, &child);
                    if (ret < 0) return ret;
                    out->type = ext2_node_type(&child);
                    storage_memcpy(out->name, entry->name, take);
                    out->name[take] = 0;
                    return 0;
                }
                ++ordinal;
            }
            pos += entry->rec_len;
        }
        offset += g_storage.ext2_block_size;
    }
    return -2;
}

/** @brief Mounts a validated classic ext2 partition selected by ext2_start_lba. */
static int ext2_mount(void)
{
    struct ext2_superblock *super = (struct ext2_superblock *)(void *)(storage_scratch + EXT2_SUPERBLOCK_OFFSET);
    uint32_t block_size;
    uint64_t groups;
    int ret;
    if (!g_storage.ext2_start_lba || g_storage.ext2_sector_count < 8u) return -2;
    ret = storage_read_sectors(g_storage.ext2_start_lba, 8u, storage_scratch);
    if (ret < 0) return ret;
    if (super->magic != EXT2_SUPER_MAGIC || super->log_block_size > 2u ||
        !super->blocks_count || !super->blocks_per_group || !super->inodes_per_group ||
        (super->rev_level != EXT2_GOOD_OLD_REV && super->rev_level != EXT2_DYNAMIC_REV) ||
        (super->feature_incompat & ~EXT2_FEATURE_INCOMPAT_FILETYPE) != 0) return -2;
    block_size = 1024u << super->log_block_size;
    if (block_size > sizeof(storage_scratch) || block_size < 1024u || block_size % SECTOR_SIZE) return -2;
    groups = (super->blocks_count - super->first_data_block + super->blocks_per_group - 1u) /
             super->blocks_per_group;
    if (!groups || groups > 0xffffffffu) return -2;
    g_storage.ext2_block_size = block_size;
    g_storage.ext2_blocks_count = super->blocks_count;
    g_storage.ext2_blocks_per_group = super->blocks_per_group;
    g_storage.ext2_inodes_per_group = super->inodes_per_group;
    g_storage.ext2_inode_size = super->rev_level == EXT2_GOOD_OLD_REV ? 128u : super->inode_size;
    g_storage.ext2_first_data_block = super->first_data_block;
    g_storage.ext2_group_count = (uint32_t)groups;
    if (g_storage.ext2_inode_size < sizeof(struct ext2_inode) ||
        g_storage.ext2_inode_size > block_size || block_size % g_storage.ext2_inode_size) return -2;
    if ((uint64_t)super->blocks_count * (block_size / SECTOR_SIZE) >
        g_storage.ext2_sector_count) return -2;
    g_storage.filesystem = STORAGE_FILESYSTEM_EXT2;
    return 0;
}

/**
 * @brief Updates ext2 allocator accounting after one bitmap mutation.
 * @param group_index Block group whose descriptor is adjusted.
 * @param block_delta Signed count added to free blocks.
 * @param inode_delta Signed count added to free inodes.
 * @param dir_delta Signed count added to used directory inodes.
 * @return Zero after both the descriptor and superblock commit, or a negative error.
 */
static int ext2_adjust_counts(uint32_t group_index, int block_delta, int inode_delta,
                              int dir_delta)
{
    struct ext2_group_desc group;
    struct ext2_superblock *super = (struct ext2_superblock *)(void *)storage_scratch;
    int ret = ext2_group_desc(group_index, &group);
    if (ret < 0) return ret;
    if ((block_delta < 0 && group.free_blocks_count < (uint16_t)-block_delta) ||
        (inode_delta < 0 && group.free_inodes_count < (uint16_t)-inode_delta) ||
        (dir_delta < 0 && group.used_dirs_count < (uint16_t)-dir_delta)) return -5;
    group.free_blocks_count = (uint16_t)(group.free_blocks_count + block_delta);
    group.free_inodes_count = (uint16_t)(group.free_inodes_count + inode_delta);
    group.used_dirs_count = (uint16_t)(group.used_dirs_count + dir_delta);
    ret = ext2_write_group_desc(group_index, &group);
    if (ret < 0) return ret;
    ret = storage_read_sectors(g_storage.ext2_start_lba + 2u, 2u, storage_scratch);
    if (ret < 0) return ret;
    if (super->magic != EXT2_SUPER_MAGIC ||
        (block_delta < 0 && super->free_blocks_count < (uint32_t)-block_delta) ||
        (inode_delta < 0 && super->free_inodes_count < (uint32_t)-inode_delta)) return -5;
    super->free_blocks_count = (uint32_t)((int64_t)super->free_blocks_count + block_delta);
    super->free_inodes_count = (uint32_t)((int64_t)super->free_inodes_count + inode_delta);
    return storage_write_sectors(g_storage.ext2_start_lba + 2u, 2u, storage_scratch);
}

/**
 * @brief Allocates a block and commits both its bitmap and free-block counts.
 * @param out_block Receives a newly allocated and zero-filled physical block.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_allocate_block(uint32_t *out_block)
{
    uint32_t group_index;
    int ret = ext2_alloc_block(out_block);
    if (ret < 0) return ret;
    group_index = (*out_block - g_storage.ext2_first_data_block) /
                  g_storage.ext2_blocks_per_group;
    ret = ext2_adjust_counts(group_index, -1, 0, 0);
    if (ret < 0) {
        ext2_free_block(*out_block);
        return ret;
    }
    return 0;
}

/**
 * @brief Returns a block to the ext2 allocator and restores free-block counts.
 * @param block Physical block number to release.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_release_block(uint32_t block)
{
    uint32_t relative;
    uint32_t group_index;
    if (block < g_storage.ext2_first_data_block || block >= g_storage.ext2_blocks_count) return -22;
    relative = block - g_storage.ext2_first_data_block;
    group_index = relative / g_storage.ext2_blocks_per_group;
    ext2_free_block(block);
    return ext2_adjust_counts(group_index, 1, 0, 0);
}

/**
 * @brief Allocates an inode and updates its group and superblock counters.
 * @param directory Nonzero when the inode will represent a directory.
 * @param out_inode Receives the newly allocated inode number.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_allocate_inode(uint8_t directory, uint32_t *out_inode)
{
    uint32_t group_index;
    int ret = ext2_alloc_inode(out_inode);
    if (ret < 0) return ret;
    group_index = (*out_inode - 1u) / g_storage.ext2_inodes_per_group;
    ret = ext2_adjust_counts(group_index, 0, -1, directory ? 1 : 0);
    if (ret < 0) {
        ext2_free_inode(*out_inode);
        return ret;
    }
    return 0;
}

/**
 * @brief Releases an inode allocation after all directory references are removed.
 * @param inode Inode number to release.
 * @param directory Nonzero when the inode was a directory.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_release_inode(uint32_t inode, uint8_t directory)
{
    uint32_t group_index;
    if (!inode || inode <= 10u) return -22;
    group_index = (inode - 1u) / g_storage.ext2_inodes_per_group;
    ext2_free_inode(inode);
    return ext2_adjust_counts(group_index, 0, 1, directory ? -1 : 0);
}

/**
 * @brief Calculates the aligned directory record size for a file name.
 * @param name_len Number of bytes in the directory entry name.
 * @return Aligned ext2 directory record length.
 */
static uint16_t ext2_dir_record_size(uint32_t name_len)
{
    return (uint16_t)((8u + name_len + 3u) & ~3u);
}

/**
 * @brief Tests whether an ext2 directory entry is LeonOS ACL sidecar metadata.
 * @param entry Parsed ext2 directory entry contained in a validated block.
 * @return Nonzero when the entry must remain hidden from ordinary enumeration.
 */
static int ext2_dirent_is_acl_metadata(const struct ext2_dirent *entry)
{
    static const char acl_name[] = "LEONACL.SYS";
    uint32_t i;
    if (!entry || entry->name_len != sizeof(acl_name) - 1u) return 0;
    for (i = 0; i < sizeof(acl_name) - 1u; ++i) {
        char a = entry->name[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (a != acl_name[i]) return 0;
    }
    return 1;
}

/**
 * @brief Validates an ext2 directory entry name accepted by LeonOS paths.
 * @param name NUL-terminated single path component.
 * @return One for a valid component, otherwise zero.
 */
static int ext2_name_valid(const char *name)
{
    uint32_t i;
    if (!name || !name[0]) return 0;
    for (i = 0; name[i]; ++i) {
        if (i >= 255u || name[i] == '/') return 0;
    }
    return 1;
}

/**
 * @brief Adds one named child entry to an ext2 directory.
 * @param directory_ino Parent directory inode number.
 * @param name Nonempty file name without path separators.
 * @param child_ino Inode number stored in the new entry.
 * @param file_type ext2 directory type value for the child.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_add_dir_entry(uint32_t directory_ino, const char *name,
                              uint32_t child_ino, uint8_t file_type)
{
    struct ext2_inode directory;
    uint32_t name_len;
    uint16_t needed;
    uint32_t logical;
    int ret;
    if (!ext2_name_valid(name) || !child_ino) return -22;
    name_len = storage_strlen(name);
    needed = ext2_dir_record_size(name_len);
    ret = ext2_read_inode(directory_ino, &directory);
    if (ret < 0) return ret;
    if (ext2_node_type(&directory) != LEONOS_FS_TYPE_DIR) return -20;
    for (logical = 0; logical < (uint32_t)((ext2_inode_size(&directory) +
                                             g_storage.ext2_block_size - 1u) /
                                            g_storage.ext2_block_size); ++logical) {
        uint32_t block = 0;
        uint32_t pos;
        ret = ext2_get_block(&directory, logical, 0, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -5;
        ret = ext2_read_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        for (pos = 0; pos + 8u <= g_storage.ext2_block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)(storage_cluster_buf + pos);
            uint16_t ideal;
            if (entry->rec_len < 8u || (entry->rec_len & 3u) ||
                pos + entry->rec_len > g_storage.ext2_block_size ||
                entry->name_len > entry->rec_len - 8u) return -5;
            ideal = ext2_dir_record_size(entry->name_len);
            if (entry->inode && entry->rec_len >= ideal + needed) {
                struct ext2_dirent *new_entry;
                uint16_t old_len = entry->rec_len;
                entry->rec_len = ideal;
                new_entry = (struct ext2_dirent *)(void *)((uint8_t *)entry + ideal);
                storage_memzero(new_entry, old_len - ideal);
                new_entry->inode = child_ino;
                new_entry->rec_len = old_len - ideal;
                new_entry->name_len = (uint8_t)name_len;
                new_entry->file_type = file_type;
                storage_memcpy(new_entry->name, name, name_len);
                return ext2_write_block(block, storage_cluster_buf);
            }
            pos += entry->rec_len;
        }
    }
    logical = (uint32_t)((ext2_inode_size(&directory) + g_storage.ext2_block_size - 1u) /
                         g_storage.ext2_block_size);
    {
        uint32_t block = 0;
        ret = ext2_get_block(&directory, logical, 1, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -28;
        storage_memzero(storage_cluster_buf, g_storage.ext2_block_size);
        {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)storage_cluster_buf;
            entry->inode = child_ino;
            entry->rec_len = (uint16_t)g_storage.ext2_block_size;
            entry->name_len = (uint8_t)name_len;
            entry->file_type = file_type;
            storage_memcpy(entry->name, name, name_len);
        }
        ret = ext2_write_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        ext2_set_inode_size(&directory, (uint64_t)(logical + 1u) * g_storage.ext2_block_size);
        return ext2_write_inode(directory_ino, &directory);
    }
}

/**
 * @brief Removes one child name from an ext2 directory.
 * @param directory_ino Parent directory inode number.
 * @param name Existing child name to remove.
 * @param out_inode Optional receiver for the removed inode number.
 * @param out_type Optional receiver for the removed ext2 file type.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_remove_dir_entry(uint32_t directory_ino, const char *name,
                                 uint32_t *out_inode, uint8_t *out_type)
{
    struct ext2_inode directory;
    uint32_t logical;
    uint32_t name_len;
    int ret;
    if (!name || !name[0]) return -22;
    name_len = storage_strlen(name);
    ret = ext2_read_inode(directory_ino, &directory);
    if (ret < 0) return ret;
    if (ext2_node_type(&directory) != LEONOS_FS_TYPE_DIR) return -20;
    for (logical = 0; logical < (uint32_t)((ext2_inode_size(&directory) +
                                             g_storage.ext2_block_size - 1u) /
                                            g_storage.ext2_block_size); ++logical) {
        uint32_t block = 0;
        uint32_t pos;
        struct ext2_dirent *previous = 0;
        ret = ext2_get_block(&directory, logical, 0, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -5;
        ret = ext2_read_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        for (pos = 0; pos + 8u <= g_storage.ext2_block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)(storage_cluster_buf + pos);
            uint32_t i;
            int match = entry->inode && entry->name_len == name_len;
            if (entry->rec_len < 8u || (entry->rec_len & 3u) ||
                pos + entry->rec_len > g_storage.ext2_block_size ||
                entry->name_len > entry->rec_len - 8u) return -5;
            for (i = 0; match && i < name_len; ++i) {
                if (entry->name[i] != name[i]) match = 0;
            }
            if (match) {
                if (out_inode) *out_inode = entry->inode;
                if (out_type) *out_type = entry->file_type;
                if (previous) previous->rec_len = (uint16_t)(previous->rec_len + entry->rec_len);
                else entry->inode = 0;
                return ext2_write_block(block, storage_cluster_buf);
            }
            previous = entry;
            pos += entry->rec_len;
        }
    }
    return -2;
}

/**
 * @brief Writes a byte range to an ext2 regular file and grows it as necessary.
 * @param inode_no Inode number receiving the bytes.
 * @param inode Mutable inode state for the file.
 * @param offset Byte offset where writing begins.
 * @param buffer Source byte buffer.
 * @param len Number of bytes to write.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_write_inode_range(uint32_t inode_no, struct ext2_inode *inode,
                                  uint64_t offset, const void *buffer, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t done = 0;
    uint64_t end = offset + len;
    int ret;
    if (!inode || (!buffer && len) || end < offset || end > 0xffffffffULL) return -28;
    while (done < len) {
        uint32_t logical = (uint32_t)((offset + done) / g_storage.ext2_block_size);
        uint32_t block_offset = (uint32_t)((offset + done) % g_storage.ext2_block_size);
        uint32_t take = min_u32(g_storage.ext2_block_size - block_offset, len - done);
        uint32_t block = 0;
        ret = ext2_get_block(inode, logical, 1, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -28;
        if (block_offset || take != g_storage.ext2_block_size) {
            ret = ext2_read_block(block, storage_cluster_buf);
            if (ret < 0) return ret;
        }
        storage_memcpy(storage_cluster_buf + block_offset, src + done, take);
        ret = ext2_write_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        done += take;
    }
    if (end > ext2_inode_size(inode)) ext2_set_inode_size(inode, end);
    return ext2_write_inode(inode_no, inode);
}

/**
 * @brief Frees all direct and indirect blocks after a retained logical-block count.
 * @param inode Mutable inode whose trailing blocks are released.
 * @param keep_blocks Number of leading logical blocks to retain.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_release_trailing_blocks(struct ext2_inode *inode, uint32_t keep_blocks)
{
    uint32_t per = g_storage.ext2_block_size / sizeof(uint32_t);
    uint32_t i;
    int ret;
    for (i = keep_blocks < 12u ? keep_blocks : 12u; i < 12u; ++i) {
        if (inode->block[i]) {
            ret = ext2_release_block(inode->block[i]);
            if (ret < 0) return ret;
            inode->block[i] = 0;
            inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
        }
    }
    if (inode->block[12]) {
        uint32_t retain = keep_blocks > 12u ? keep_blocks - 12u : 0u;
        uint32_t *table = (uint32_t *)(void *)storage_fat_cache_data;
        uint8_t any = 0;
        ret = ext2_read_block(inode->block[12], table);
        if (ret < 0) return ret;
        for (i = retain; i < per; ++i) {
            if (table[i]) {
                ret = ext2_release_block(table[i]);
                if (ret < 0) return ret;
                table[i] = 0;
                inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
            }
        }
        for (i = 0; i < per; ++i) if (table[i]) { any = 1; break; }
        if (any) {
            ret = ext2_write_block(inode->block[12], table);
            if (ret < 0) return ret;
        } else {
            ret = ext2_release_block(inode->block[12]);
            if (ret < 0) return ret;
            inode->block[12] = 0;
            inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
        }
    }
    if (inode->block[13]) {
        uint32_t retain = keep_blocks > 12u + per ? keep_blocks - 12u - per : 0u;
        uint32_t *roots = (uint32_t *)(void *)storage_fat_cache_data;
        uint32_t *leaves = (uint32_t *)(void *)storage_cluster_buf;
        uint8_t any_root = 0;
        ret = ext2_read_block(inode->block[13], roots);
        if (ret < 0) return ret;
        for (i = 0; i < per; ++i) {
            uint32_t j;
            uint32_t child_keep = retain > i * per ? retain - i * per : 0u;
            uint8_t any_child = 0;
            if (!roots[i]) continue;
            ret = ext2_read_block(roots[i], leaves);
            if (ret < 0) return ret;
            for (j = child_keep; j < per; ++j) {
                if (leaves[j]) {
                    ret = ext2_release_block(leaves[j]);
                    if (ret < 0) return ret;
                    leaves[j] = 0;
                    inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
                }
            }
            for (j = 0; j < per; ++j) if (leaves[j]) { any_child = 1; break; }
            if (any_child) {
                ret = ext2_write_block(roots[i], leaves);
                if (ret < 0) return ret;
                any_root = 1;
            } else {
                ret = ext2_release_block(roots[i]);
                if (ret < 0) return ret;
                roots[i] = 0;
                inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
            }
        }
        if (any_root) {
            ret = ext2_write_block(inode->block[13], roots);
            if (ret < 0) return ret;
        } else {
            ret = ext2_release_block(inode->block[13]);
            if (ret < 0) return ret;
            inode->block[13] = 0;
            inode->blocks_512 -= g_storage.ext2_block_size / SECTOR_SIZE;
        }
    }
    return 0;
}

/**
 * @brief Replaces or creates a regular ext2 file at a normalized path.
 * @param path Absolute LeonOS path on the currently active ext2 volume.
 * @param buffer File data, or null only for a zero-length file.
 * @param len File byte length.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_write_file(const char *path, const void *buffer, uint32_t len)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent_node;
    struct storage_node existing;
    struct ext2_inode inode;
    uint32_t inode_no;
    int ret;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = ext2_lookup_path(parent_path, &parent_node);
    if (ret < 0) return ret;
    if (parent_node.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = ext2_lookup_path(path, &existing);
    if (ret == 0) {
        if (existing.type != LEONOS_FS_TYPE_FILE) return -21;
        inode_no = existing.first_cluster;
        ret = ext2_read_inode(inode_no, &inode);
        if (ret < 0) return ret;
        ret = ext2_release_trailing_blocks(&inode, 0);
        if (ret < 0) return ret;
        ext2_set_inode_size(&inode, 0);
    } else if (ret == -2) {
        ret = ext2_allocate_inode(0, &inode_no);
        if (ret < 0) return ret;
        storage_memzero(&inode, sizeof(inode));
        inode.mode = EXT2_S_IFREG | 0644u;
        inode.links_count = 1;
        ret = ext2_write_inode(inode_no, &inode);
        if (ret < 0) {
            (void)ext2_release_inode(inode_no, 0);
            return ret;
        }
        ret = ext2_add_dir_entry(parent_node.first_cluster, name, inode_no, EXT2_FT_REG_FILE);
        if (ret < 0) {
            (void)ext2_release_inode(inode_no, 0);
            return ret;
        }
    } else return ret;
    if (len) {
        ret = ext2_write_inode_range(inode_no, &inode, 0, buffer, len);
        if (ret < 0) return ret;
    } else {
        ret = ext2_write_inode(inode_no, &inode);
        if (ret < 0) return ret;
    }
    storage_cache_invalidate();
    return 0;
}

/**
 * @brief Writes a range to an existing ext2 regular file.
 * @param node Resolved existing file on the currently selected ext2 volume.
 * @param offset Starting byte offset.
 * @param buffer Source byte buffer.
 * @param len Number of bytes to write.
 * @param out_written Optional receiver for completed bytes.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_write_node(const struct storage_node *node, uint64_t offset, const void *buffer,
                           uint32_t len, uint32_t *out_written)
{
    struct ext2_inode inode;
    int ret;
    if (out_written) *out_written = 0;
    if (!node || node->type != LEONOS_FS_TYPE_FILE ||
        node->volume_id != g_storage.volume_id) return -2;
    ret = ext2_read_inode(node->first_cluster, &inode);
    if (ret < 0) return ret;
    ret = ext2_write_inode_range(node->first_cluster, &inode, offset, buffer, len);
    if (ret < 0) return ret;
    if (out_written) *out_written = len;
    storage_cache_invalidate();
    return 0;
}

/**
 * @brief Changes the visible length of an ext2 regular file.
 * @param node Resolved existing file on the currently selected ext2 volume.
 * @param length New byte length.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_truncate_file(const struct storage_node *node, uint64_t length)
{
    struct ext2_inode inode;
    uint64_t old_size;
    int ret;
    if (length > 0xffffffffULL) return -28;
    if (!node || node->type != LEONOS_FS_TYPE_FILE ||
        node->volume_id != g_storage.volume_id) return -2;
    ret = ext2_read_inode(node->first_cluster, &inode);
    if (ret < 0) return ret;
    old_size = ext2_inode_size(&inode);
    if (length < old_size) {
        uint32_t keep = (uint32_t)((length + g_storage.ext2_block_size - 1u) /
                                   g_storage.ext2_block_size);
        ret = ext2_release_trailing_blocks(&inode, keep);
        if (ret < 0) return ret;
        if (length && (length % g_storage.ext2_block_size)) {
            uint32_t block = 0;
            ret = ext2_get_block(&inode, (uint32_t)(length / g_storage.ext2_block_size), 0, &block);
            if (ret < 0) return ret;
            if (block) {
                ret = ext2_read_block(block, storage_cluster_buf);
                if (ret < 0) return ret;
                storage_memzero(storage_cluster_buf + (length % g_storage.ext2_block_size),
                                g_storage.ext2_block_size - (uint32_t)(length % g_storage.ext2_block_size));
                ret = ext2_write_block(block, storage_cluster_buf);
                if (ret < 0) return ret;
            }
        }
    }
    ext2_set_inode_size(&inode, length);
    ret = ext2_write_inode(node->first_cluster, &inode);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

/**
 * @brief Creates an empty ext2 directory with dot and dot-dot entries.
 * @param path Absolute LeonOS path for the new directory.
 * @return Zero on success, -EEXIST when already present, or a negative error.
 */
static int ext2_mkdir(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN];
    char name[LEONOS_FS_NAME_LEN];
    struct storage_node parent;
    struct storage_node existing;
    struct ext2_inode inode;
    uint32_t inode_no;
    uint32_t block;
    uint8_t linked = 0;
    int ret;
    ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = ext2_lookup_path(parent_path, &parent);
    if (ret < 0) return ret;
    if (parent.type != LEONOS_FS_TYPE_DIR) return -20;
    ret = ext2_lookup_path(path, &existing);
    if (ret == 0) return -17;
    if (ret != -2) return ret;
    ret = ext2_allocate_inode(1, &inode_no);
    if (ret < 0) return ret;
    storage_memzero(&inode, sizeof(inode));
    inode.mode = EXT2_S_IFDIR | 0755u;
    inode.links_count = 2;
    ret = ext2_get_block(&inode, 0, 1, &block);
    if (ret < 0 || !block) {
        (void)ext2_release_inode(inode_no, 1);
        return ret < 0 ? ret : -28;
    }
    ext2_set_inode_size(&inode, g_storage.ext2_block_size);
    storage_memzero(storage_cluster_buf, g_storage.ext2_block_size);
    {
        struct ext2_dirent *dot = (struct ext2_dirent *)(void *)storage_cluster_buf;
        struct ext2_dirent *dotdot = (struct ext2_dirent *)(void *)(storage_cluster_buf + 12u);
        dot->inode = inode_no; dot->rec_len = 12u; dot->name_len = 1u; dot->file_type = EXT2_FT_DIR; dot->name[0] = '.';
        dotdot->inode = parent.first_cluster; dotdot->rec_len = (uint16_t)(g_storage.ext2_block_size - 12u);
        dotdot->name_len = 2u; dotdot->file_type = EXT2_FT_DIR; dotdot->name[0] = '.'; dotdot->name[1] = '.';
    }
    ret = ext2_write_block(block, storage_cluster_buf);
    if (ret == 0) ret = ext2_write_inode(inode_no, &inode);
    if (ret == 0) {
        ret = ext2_add_dir_entry(parent.first_cluster, name, inode_no, EXT2_FT_DIR);
        if (ret == 0) linked = 1;
    }
    if (ret == 0) {
        struct ext2_inode parent_inode;
        ret = ext2_read_inode(parent.first_cluster, &parent_inode);
        if (ret == 0) {
            ++parent_inode.links_count;
            ret = ext2_write_inode(parent.first_cluster, &parent_inode);
        }
    }
    if (ret < 0 && !linked) {
        (void)ext2_release_block(block);
        (void)ext2_release_inode(inode_no, 1);
    }
    if (ret < 0) return ret;
    storage_cache_invalidate();
    return 0;
}

/**
 * @brief Tests whether an ext2 directory only contains dot and dot-dot.
 * @param inode_no Directory inode number.
 * @return One when empty, zero when nonempty, or a negative errno-style status.
 */
static int ext2_dir_is_empty(uint32_t inode_no)
{
    struct ext2_inode directory;
    uint32_t logical;
    int ret = ext2_read_inode(inode_no, &directory);
    if (ret < 0) return ret;
    for (logical = 0; logical < (uint32_t)(ext2_inode_size(&directory) / g_storage.ext2_block_size); ++logical) {
        uint32_t block = 0, pos;
        ret = ext2_get_block(&directory, logical, 0, &block);
        if (ret < 0 || !block) return ret < 0 ? ret : -5;
        ret = ext2_read_block(block, storage_cluster_buf);
        if (ret < 0) return ret;
        for (pos = 0; pos + 8u <= g_storage.ext2_block_size;) {
            struct ext2_dirent *entry = (struct ext2_dirent *)(void *)(storage_cluster_buf + pos);
            if (entry->rec_len < 8u || (entry->rec_len & 3u) || pos + entry->rec_len > g_storage.ext2_block_size) return -5;
            if (entry->inode && !((entry->name_len == 1u && entry->name[0] == '.') ||
                (entry->name_len == 2u && entry->name[0] == '.' && entry->name[1] == '.'))) return 0;
            pos += entry->rec_len;
        }
    }
    return 1;
}

/**
 * @brief Removes an ext2 file or directory inode after the parent entry is unlinked.
 * @param inode_no Target inode number.
 * @param directory Nonzero for a directory inode.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_destroy_inode(uint32_t inode_no, uint8_t directory)
{
    struct ext2_inode inode;
    int ret = ext2_read_inode(inode_no, &inode);
    if (ret < 0) return ret;
    ret = ext2_release_trailing_blocks(&inode, 0);
    if (ret < 0) return ret;
    storage_memzero(&inode, sizeof(inode));
    ret = ext2_write_inode(inode_no, &inode);
    if (ret < 0) return ret;
    return ext2_release_inode(inode_no, directory);
}

/**
 * @brief Removes a regular file at an ext2 path.
 * @param path Absolute LeonOS path for the file.
 * @return Zero on success, or a negative errno-style status.
 */
static int ext2_unlink(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN], name[LEONOS_FS_NAME_LEN];
    struct storage_node parent, node;
    uint32_t child;
    int ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = ext2_lookup_path(parent_path, &parent);
    if (ret < 0) return ret;
    ret = ext2_lookup_path(path, &node);
    if (ret < 0) return ret;
    if (node.type != LEONOS_FS_TYPE_FILE) return -21;
    ret = ext2_remove_dir_entry(parent.first_cluster, name, &child, 0);
    if (ret < 0) return ret;
    ret = ext2_destroy_inode(child, 0);
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

/**
 * @brief Removes an empty ext2 directory at an absolute path.
 * @param path Absolute LeonOS path for the directory.
 * @return Zero on success, -ENOTEMPTY when children remain, or a negative error.
 */
static int ext2_rmdir(const char *path)
{
    char parent_path[LEONOS_FS_PATH_LEN], name[LEONOS_FS_NAME_LEN];
    struct storage_node parent, node;
    uint32_t child;
    int ret = storage_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name));
    if (ret < 0) return ret;
    ret = ext2_lookup_path(parent_path, &parent);
    if (ret < 0) return ret;
    ret = ext2_lookup_path(path, &node);
    if (ret < 0) return ret;
    if (node.first_cluster == EXT2_ROOT_INO || node.type != LEONOS_FS_TYPE_DIR) return -22;
    ret = ext2_dir_is_empty(node.first_cluster);
    if (ret <= 0) return ret < 0 ? ret : -39;
    ret = ext2_remove_dir_entry(parent.first_cluster, name, &child, 0);
    if (ret < 0) return ret;
    ret = ext2_destroy_inode(child, 1);
    if (ret == 0) {
        struct ext2_inode parent_inode;
        ret = ext2_read_inode(parent.first_cluster, &parent_inode);
        if (ret == 0 && parent_inode.links_count > 0) {
            --parent_inode.links_count;
            ret = ext2_write_inode(parent.first_cluster, &parent_inode);
        }
    }
    if (ret == 0) storage_cache_invalidate();
    return ret;
}

/**
 * @brief Renames an ext2 path within the currently active filesystem.
 * @param old_path Existing absolute LeonOS path.
 * @param new_path Unused absolute LeonOS path in the same volume.
 * @return Zero on success, -EEXIST for an occupied destination, or a negative error.
 */
static int ext2_rename(const char *old_path, const char *new_path)
{
    char old_parent_path[LEONOS_FS_PATH_LEN], old_name[LEONOS_FS_NAME_LEN];
    char new_parent_path[LEONOS_FS_PATH_LEN], new_name[LEONOS_FS_NAME_LEN];
    struct storage_node old_parent, new_parent, node, existing;
    uint8_t type;
    uint32_t child;
    int ret = storage_parent_path(old_path, old_parent_path, sizeof(old_parent_path), old_name, sizeof(old_name));
    if (ret < 0) return ret;
    ret = storage_parent_path(new_path, new_parent_path, sizeof(new_parent_path), new_name, sizeof(new_name));
    if (ret < 0) return ret;
    if (!storage_text_eq_ci(old_parent_path, new_parent_path)) return -22;
    ret = ext2_lookup_path(old_parent_path, &old_parent);
    if (ret < 0) return ret;
    ret = ext2_lookup_path(new_parent_path, &new_parent);
    if (ret < 0) return ret;
    ret = ext2_lookup_path(old_path, &node);
    if (ret < 0) return ret;
    ret = ext2_lookup_path(new_path, &existing);
    if (ret == 0) return -17;
    if (ret != -2) return ret;
    type = node.type == LEONOS_FS_TYPE_DIR ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    ret = ext2_add_dir_entry(new_parent.first_cluster, new_name, node.first_cluster, type);
    if (ret < 0) return ret;
    ret = ext2_remove_dir_entry(old_parent.first_cluster, old_name, &child, 0);
    if (ret < 0) return ret;
    storage_cache_invalidate();
    return 0;
}
