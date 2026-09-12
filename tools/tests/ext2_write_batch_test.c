#define main storage_fixture_main
#include "storage_rename_test.c"
#undef main

static void read_super(struct ext2_superblock *super)
{
    uint8_t bytes[1024];
    assert(!storage_read_sectors(test_start_lba + 2, 2, bytes));
    memcpy(super, bytes, sizeof(*super));
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    disk = fopen(argv[1], "r+b");
    assert(disk);
    g_storage.ext2_start_lba = test_start_lba;
    assert(!fseek(disk, 0, SEEK_END));
    g_storage.ext2_sector_count = ftell(disk) / 512;
    assert(ext2_mount() == 0);
    uint32_t bs = g_storage.ext2_block_size, per = bs / 4;
    uint8_t data[32768], got[32768];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(1 + i % 251);
    unsigned fail = (unsigned)strtoul(argv[2], NULL, 0);
    const uint32_t logical[] = {0, 13, 13 + per};
    for (unsigned kind = 0; kind < 3; ++kind) {
        if (!kind && fail == 6) continue; /* Direct runs commit in five writes. */
        char name[32];
        snprintf(name, sizeof(name), "/batch-%u", kind);
        assert(ext2_write_file(name, NULL, 0) == 0);
        struct storage_node node;
        assert(ext2_lookup_path(name, &node) == 0);
        uint32_t wrote;
        if (kind) {
            /* Establish the pointer block before exercising a new data run. */
            assert(!ext2_write_node(&node, (uint64_t)(logical[kind] - 1) * bs,
                                   data, bs, &wrote));
        }
        struct ext2_inode before, after;
        struct ext2_superblock counts_before, counts_after;
        assert(!ext2_read_inode(node.first_cluster, &before));
        read_super(&counts_before);
        uint64_t start = (uint64_t)logical[kind] * bs;
        test_write_commands = test_write_bytes = 0;
        test_fail_write_at = fail;
        test_partial_write = 1;
        int ret = ext2_write_node(&node, start, data, 8 * bs, &wrote);
        if (fail) {
            assert(ret == -5 && wrote == 0 && test_fail_write_at == 0);
            assert(ext2_mount() == 0);
            assert(!ext2_read_inode(node.first_cluster, &after));
            assert(!memcmp(&before, &after, sizeof(before)));
            read_super(&counts_after);
            assert(counts_before.free_blocks_count == counts_after.free_blocks_count);
            assert(!ext2_truncate_file(&node, start + 8 * bs));
            uint32_t count;
            memset(got, '#', sizeof(got));
            assert(!ext2_read_node(&node, start, got, 8 * bs, &count) && count == 8 * bs);
            for (unsigned i = 0; i < count; ++i) assert(got[i] == 0);
        } else {
            assert(!ret && wrote == 8 * bs);
            printf("batch kind=%u bytes=%u writes=%llu write_bytes=%llu\n", kind, wrote,
                   (unsigned long long)test_write_commands, (unsigned long long)test_write_bytes);
            /* Bitmap/count/pointer updates must be amortized across the run. */
            assert(test_write_commands <= 8);
            uint32_t count;
            assert(!ext2_read_node(&node, start, got, 8 * bs, &count));
            assert(count == 8 * bs && !memcmp(data, got, count));
        }
        assert(!ext2_unlink(name));
    }
    /* A reused block must not reveal the previous owner's bytes around a
     * partial write, including after truncate/extend and overwrite. */
    assert(!ext2_write_file("/secret", data, sizeof(data)));
    struct storage_node secret;
    struct ext2_inode secret_inode;
    assert(!ext2_lookup_path("/secret", &secret));
    assert(!ext2_read_inode(secret.first_cluster, &secret_inode));
    assert(!ext2_unlink("/secret"));
    g_storage.ext2_next_block = secret_inode.block[0];
    assert(!ext2_write_file("/partial", NULL, 0));
    struct storage_node node;
    uint32_t count;
    assert(!ext2_lookup_path("/partial", &node));
    assert(!ext2_write_node(&node, 31, "xyz", 3, &count) && count == 3);
    assert(!ext2_truncate_file(&node, bs));
    assert(!ext2_read_node(&node, 0, got, bs, &count) && count == bs);
    for (unsigned i = 0; i < bs; ++i)
        assert(got[i] == (i >= 31 && i < 34 ? (uint8_t)"xyz"[i - 31] : 0));
    assert(!ext2_write_node(&node, 32, "q", 1, &count));
    assert(!ext2_read_node(&node, 31, got, 3, &count) && !memcmp(got, "xqz", 3));
    assert(!ext2_write_node(&node, 9999, data, 0, &count) && !count);
    struct ext2_inode inode;
    assert(!ext2_read_inode(node.first_cluster, &inode) && ext2_inode_size(&inode) == bs);
    assert(inode.block[0] == secret_inode.block[0]);

    /* Existing mappings and holes alternate. A new run must stop before a
     * mapped block and preserve its contents on either side of an overwrite. */
    assert(!ext2_write_file("/mixed", NULL, 0));
    assert(!ext2_lookup_path("/mixed", &node));
    uint8_t expected[12 * 4096] = {0}, readback[12 * 4096];
    for (unsigned i = 1; i < 12; i += 2) {
        memset(got, 'A' + i, bs);
        assert(!ext2_write_node(&node, i * bs, got, bs, &count));
        memcpy(expected + i * bs, got, bs);
    }
    assert(!ext2_write_node(&node, bs / 2, data, 8 * bs, &count) && count == 8 * bs);
    memcpy(expected + bs / 2, data, 8 * bs);
    assert(!ext2_read_node(&node, 0, readback, 12 * bs, &count) && count == 12 * bs);
    assert(!memcmp(expected, readback, count));

    /* Stop the physical run at a block still owned by another file. */
    assert(!ext2_write_file("/gap", data, bs));
    assert(!ext2_lookup_path("/gap", &node));
    assert(!ext2_read_inode(node.first_cluster, &inode));
    uint32_t gap = inode.block[0];
    assert(!ext2_write_file("/held", data, bs));
    assert(!ext2_unlink("/gap"));
    assert(!ext2_write_file("/fragmented", NULL, 0));
    assert(!ext2_lookup_path("/fragmented", &node));
    g_storage.ext2_next_block = gap;
    assert(!ext2_write_node(&node, 0, data, 8 * bs, &count));
    assert(!ext2_read_node(&node, 0, got, 8 * bs, &count) && count == 8 * bs);
    assert(!memcmp(data, got, count));
    assert(!ext2_lookup_path("/held", &node));
    assert(!ext2_read_node(&node, 0, got, bs, &count) && count == bs && !memcmp(data, got, count));

    /* Push a run against the end of group zero; it must continue in the next
     * group, with bitmap and superblock counts agreeing after remount. */
    if (g_storage.ext2_group_count > 1) {
        assert(!ext2_write_file("/group-edge", NULL, 0));
        assert(!ext2_lookup_path("/group-edge", &node));
        g_storage.ext2_next_block = g_storage.ext2_first_data_block + g_storage.ext2_blocks_per_group - 2;
        assert(!ext2_write_node(&node, 0, data, 8 * bs, &count) && count == 8 * bs);
        assert(!ext2_read_inode(node.first_cluster, &inode));
        assert(inode.block[0] / g_storage.ext2_blocks_per_group !=
               inode.block[7] / g_storage.ext2_blocks_per_group);
        assert(ext2_mount() == 0);
        assert(!ext2_read_node(&node, 0, got, 8 * bs, &count) && !memcmp(data, got, count));
    }
    assert(!fclose(disk));
    puts("PASS ext2 bounded writes, publication, rollback and zeroed holes");
}
