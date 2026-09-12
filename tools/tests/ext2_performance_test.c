#define main ext2_existing_regression_main
#include "storage_rename_test.c"
#undef main

static void reset_counters(void)
{
    test_read_commands = test_write_commands = test_read_bytes = test_write_bytes = 0;
}

static void report(const char *phase, uint64_t bytes)
{
    printf("{\"phase\":\"%s\",\"payload_bytes\":%llu,\"reads\":%llu,\"writes\":%llu,"
           "\"read_bytes\":%llu,\"write_bytes\":%llu}\n", phase,
           (unsigned long long)bytes, (unsigned long long)test_read_commands,
           (unsigned long long)test_write_commands, (unsigned long long)test_read_bytes,
           (unsigned long long)test_write_bytes);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    disk = fopen(argv[1], "r+b");
    assert(disk);
    g_storage.ext2_start_lba = test_start_lba;
    assert(!fseek(disk, 0, SEEK_END));
    g_storage.ext2_sector_count = ftell(disk) / 512;
    assert(ext2_mount() == 0);
    assert(ext2_write_file("/large", NULL, 0) == 0);
    struct storage_node node;
    assert(ext2_lookup_path("/large", &node) == 0);
    uint8_t data[32768], readback[32768];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 37 + i / 257);
    reset_counters();
    for (uint32_t offset = 0; offset < 8 * 1024 * 1024; offset += sizeof(data)) {
        uint32_t wrote;
        assert(ext2_write_node(&node, offset, data, sizeof(data), &wrote) == 0);
        assert(wrote == sizeof(data));
    }
    report("sequential-create", 8 * 1024 * 1024);
    reset_counters();
    for (uint32_t offset = 0; offset < 8 * 1024 * 1024; offset += sizeof(readback)) {
        uint32_t got;
        assert(ext2_read_node(&node, offset, readback, sizeof(readback), &got) == 0);
        assert(got == sizeof(readback) && !memcmp(data, readback, got));
    }
    report("sequential-read", 8 * 1024 * 1024);
    reset_counters();
    assert(ext2_mkdir("/small") == 0);
    for (unsigned i = 0; i < 256; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/small/file-%03u", i);
        assert(ext2_write_file(path, data, 192) == 0);
    }
    report("small-files", 256 * 192);
    assert(!fclose(disk));
}
