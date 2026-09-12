/* Bounded synchronous allocation of a run within one existing pointer leaf.
 * Like ext2_get_blocks in Linux, stop at pointer and allocation-group boundaries.
 * Data is initialized before its mapping is published; no dirty cache survives
 * this operation. Missing indirect branches use the single-block path. */
static int ext2_write_new_run(uint32_t inode_no, struct ext2_inode *inode,
                              uint64_t offset, const uint8_t *src, uint32_t len,
                              uint32_t *out_written)
{
    uint32_t bs = g_storage.ext2_block_size, per = bs / sizeof(uint32_t);
    uint32_t logical = (uint32_t)(offset / bs), within = (uint32_t)(offset % bs);
    uint32_t index = logical, leaf = 0, slots_count = 12;
    uint32_t *slots = inode->block;
    uint32_t *table = (uint32_t *)(void *)storage_fat_cache_data;
    uint32_t count, group_index = 0, bit = 0, first = 0;
    struct ext2_group_desc group;
    struct ext2_inode old_inode = *inode;
    uint8_t old_bitmap[4096], old_leaf[4096], old_super[1024];
    uint64_t super_lba = g_storage.ext2_start_lba + 2u;
    int ret, stage = 0;
    *out_written = 0;
    storage_fat_cache.valid = 0;
    if (logical >= 12u) {
        index = logical - 12u;
        slots_count = per;
        leaf = inode->block[12];
        if (index >= per) {
            index -= per;
            if (index >= per * per) return -28;
            if (!inode->block[13]) return 0;
            ret = ext2_read_block(inode->block[13], table);
            if (ret < 0) return ret;
            leaf = table[index / per];
            index %= per;
        }
        if (!leaf) return 0;
        ret = ext2_read_block(leaf, table);
        if (ret < 0) return ret;
        slots = table;
        storage_memcpy(old_leaf, table, bs);
    }
    if (slots[index]) return 0;
    count = min_u32(len, sizeof(storage_cluster_buf) - within);
    count = (within + count + bs - 1u) / bs;
    count = min_u32(count, slots_count - index);
    for (uint32_t i = 1; i < count; ++i) {
        if (slots[index + i]) { count = i; break; }
    }

    uint32_t hint = g_storage.ext2_next_block;
    if (hint < g_storage.ext2_first_data_block || hint >= g_storage.ext2_blocks_count)
        hint = g_storage.ext2_first_data_block;
    uint32_t first_group = (hint - g_storage.ext2_first_data_block) / g_storage.ext2_blocks_per_group;
    for (uint32_t n = 0; n < g_storage.ext2_group_count; ++n) {
        group_index = (first_group + n) % g_storage.ext2_group_count;
        uint32_t start = g_storage.ext2_first_data_block + group_index * g_storage.ext2_blocks_per_group;
        uint32_t blocks = min_u32(g_storage.ext2_blocks_per_group, g_storage.ext2_blocks_count - start);
        ret = ext2_group_desc(group_index, &group);
        if (ret < 0) return ret;
        if (!group.free_blocks_count) continue;
        ret = ext2_read_block(group.block_bitmap, storage_scratch);
        if (ret < 0) return ret;
        bit = ext2_free_bitmap_bit(storage_scratch, blocks, n ? 0 : hint - start);
        if (bit == blocks) continue;
        count = min_u32(count, min_u32(blocks - bit, group.free_blocks_count));
        for (uint32_t i = 1; i < count; ++i) {
            if (storage_scratch[(bit + i) / 8u] & (1u << ((bit + i) & 7u))) {
                count = i;
                break;
            }
        }
        storage_memcpy(old_bitmap, storage_scratch, bs);
        first = start + bit;
        break;
    }
    if (!first) return -28;
    ret = ext2_cache_read(super_lba, 2, storage_scratch) ? 0 :
        storage_read_sectors(super_lba, 2, storage_scratch);
    if (ret < 0) return ret;
    struct ext2_superblock *super = (struct ext2_superblock *)(void *)storage_scratch;
    if (super->magic != EXT2_SUPER_MAGIC || super->free_blocks_count < count) return -5;
    storage_memcpy(old_super, storage_scratch, sizeof(old_super));

    storage_memcpy(storage_scratch, old_bitmap, bs);
    for (uint32_t i = 0; i < count; ++i)
        storage_scratch[(bit + i) / 8u] |= (uint8_t)(1u << ((bit + i) & 7u));
    ret = ext2_write_block(group.block_bitmap, storage_scratch);
    if (ret < 0) goto rollback;

    uint32_t take = min_u32(len, count * bs - within);
    storage_memzero(storage_cluster_buf, count * bs);
    storage_memcpy(storage_cluster_buf + within, src, take);
    ret = storage_write_sectors(g_storage.ext2_start_lba + (uint64_t)first * (bs / SECTOR_SIZE),
                                count * (bs / SECTOR_SIZE), storage_cluster_buf);
    if (ret < 0) goto rollback;
    for (uint32_t i = 0; i < count; ++i)
        ext2_cache_store(g_storage.ext2_start_lba + (uint64_t)(first + i) * (bs / SECTOR_SIZE),
                          bs / SECTOR_SIZE, storage_cluster_buf + i * bs);

    struct ext2_group_desc updated = group;
    updated.free_blocks_count -= count;
    stage = 1;
    ret = ext2_write_group_desc(group_index, &updated);
    if (ret < 0) goto rollback;
    storage_memcpy(storage_scratch, old_super, sizeof(old_super));
    super->free_blocks_count -= count;
    stage = 2;
    ret = storage_write_sectors(super_lba, 2, storage_scratch);
    if (ret < 0) goto rollback;
    ext2_cache_store(super_lba, 2, storage_scratch);
    if (leaf) {
        storage_memcpy(table, old_leaf, bs);
        slots = table;
    }
    for (uint32_t i = 0; i < count; ++i) slots[index + i] = first + i;
    if (leaf) {
        stage = 3;
        ret = ext2_write_block(leaf, table);
        if (ret < 0) goto rollback;
    }
    inode->blocks_512 += count * (bs / SECTOR_SIZE);
    if (offset + take > ext2_inode_size(inode)) ext2_set_inode_size(inode, offset + take);
    stage = 4;
    ret = ext2_write_inode(inode_no, inode);
    if (ret < 0) goto rollback;
    g_storage.ext2_next_block = first + count;
    *out_written = take;
    return 1;

rollback:
    *inode = old_inode;
    /* A failed device command may have written a prefix. Undo references
     * before freeing blocks. If rollback itself fails, retain the allocation
     * rather than allow a possibly referenced block to be reused. */
    int undo = 0;
    if (stage >= 4) undo = ext2_write_inode(inode_no, &old_inode);
    if (!undo && leaf && stage >= 3) {
        storage_memcpy(table, old_leaf, bs);
        undo = ext2_write_block(leaf, table);
    }
    if (!undo && stage >= 1) undo = ext2_write_group_desc(group_index, &group);
    if (!undo && stage >= 2) {
        storage_memcpy(storage_scratch, old_super, sizeof(old_super));
        undo = storage_write_sectors(super_lba, 2, storage_scratch);
    }
    if (!undo) {
        storage_memcpy(storage_scratch, old_bitmap, bs);
        undo = ext2_write_block(group.block_bitmap, storage_scratch);
    }
    ext2_cache_reset(g_active_volume);
    if (undo < 0)
        console_printf("[ntclks] ext2 write rollback failed inode=%u ret=%d; filesystem needs checking\n",
                       inode_no, undo);
    return ret;
}
