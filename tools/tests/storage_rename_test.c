#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/bootstrap/storage/storage_internal.h"

static FILE *disk;
static uint64_t test_start_lba = 1;
static int storage_read_sectors(uint64_t lba, uint32_t count, void *out)
{
    assert(lba >= test_start_lba && !fseek(disk, (lba - test_start_lba) * 512, SEEK_SET));
    return fread(out, 512, count, disk) == count ? 0 : -5;
}
static int storage_write_sectors(uint64_t lba, uint32_t count, const void *in)
{
    /* The real storage_block.c write invalidates the shared sector cache. */
    storage_read_cache.valid = 0;
    assert(lba >= test_start_lba && !fseek(disk, (lba - test_start_lba) * 512, SEEK_SET));
    return fwrite(in, 512, count, disk) == count && !fflush(disk) ? 0 : -5;
}
static void storage_memzero(void *out, size_t n) { memset(out, 0, n); }
static void storage_memcpy(void *out, const void *in, size_t n) { memcpy(out, in, n); }
static uint32_t storage_strlen(const char *text) { return strlen(text); }
static int storage_text_eq_ci(const char *a, const char *b) { return !strcasecmp(a, b); }
static int storage_text_eq(const char *a, const char *b) { return !strcmp(a, b); }
static void storage_copy_text(char *out, uint32_t cap, const char *text) { snprintf(out, cap, "%s", text); }
static void storage_cache_invalidate(void) { storage_read_cache.valid = 0; }
static void storage_begin_mutation(void) {}
static int storage_dir_index_lookup(uint32_t directory, const char *name, struct storage_node *node)
{ (void)directory; (void)name; (void)node; return 0; }
static void storage_dir_index_store(uint32_t directory, const char *name, const struct storage_node *node)
{ (void)directory; (void)name; (void)node; }
static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static int storage_parent_path(const char *path, char *parent, uint32_t parent_cap, char *name, uint32_t name_cap)
{
    const char *slash = strrchr(path, '/');
    if (!slash || !slash[1]) return -22;
    snprintf(name, name_cap, "%s", slash + 1);
    if (slash == path) snprintf(parent, parent_cap, "/");
    else snprintf(parent, parent_cap, "%.*s", (int)(slash - path), path);
    return 0;
}
void console_printf(const char *format, ...) { (void)format; }

#include "../../drivers/bootstrap/storage/storage_ext2.c"

static void test_symlinks(void)
{
    struct storage_node node, alias;
    struct ext2_inode inode;
    char target[4097], result[4097];
    const unsigned lengths[] = {59, 60, 61, 255, g_storage.ext2_block_size - 1};
    assert(ext2_symlink("", "/empty-link") == -2);
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        unsigned length = lengths[i];
        memset(target, 't', length);
        target[length] = 0;
        assert(ext2_symlink(target, "/link") == 0);
        assert(ext2_symlink("other", "/link") == -17);
        assert(ext2_lookup_path("/link", &node) == 0 && node.type == LEONOS_FS_TYPE_SYMLINK);
        assert(ext2_read_inode(node.first_cluster, &inode) == 0);
        assert(inode.mode == 0120777 && inode.size_lo == length);
        assert(inode.blocks_512 == (length < 60 ? 0 : g_storage.ext2_block_size / 512));
        uint32_t got;
        memset(result, '#', sizeof(result));
        assert(ext2_symlink_read(&node, result, sizeof(result), &got) == 0 && got == length);
        assert(!memcmp(result, target, length) && result[length] == '#');
        assert(ext2_symlink_read(&node, result, 3, &got) == 0 && got == 3);
        assert(ext2_link("/link", "/link-hard") == 0);
        assert(ext2_lookup_path("/link-hard", &alias) == 0 && alias.first_cluster == node.first_cluster);
        assert(ext2_symlink("replace", "/replacement") == 0);
        assert(ext2_rename("/replacement", "/link") == 0);
        assert(ext2_symlink_read(&alias, result, sizeof(result), &got) == 0 && got == length);
        assert(ext2_rename("/link-hard", "/link-renamed") == 0);
        assert(ext2_unlink("/link") == 0 && ext2_unlink("/link-renamed") == 0);
    }
    memset(target, 't', g_storage.ext2_block_size);
    target[g_storage.ext2_block_size] = 0;
    assert(ext2_symlink(target, "/too-long") == -36);
    assert(ext2_symlink("dangling-target", "/symlink-persisted") == 0);
    assert(ext2_symlink("upper", "/CaseLink") == 0);
    assert(ext2_lookup_path("/caselink", &node) == -2);
    assert(ext2_symlink("lower", "/caselink") == 0);
    assert(ext2_unlink("/CaseLink") == 0);
    assert(ext2_lookup_path("/caselink", &node) == 0);
    uint32_t got;
    assert(ext2_symlink_read(&node, result, sizeof(result), &got) == 0 && got == 5);
    assert(!memcmp(result, "lower", 5));
    assert(ext2_unlink("/caselink") == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    if (argc == 3) {
        assert(!strcmp(argv[2], "--ram-root"));
        test_start_lba = 0;
        g_storage.kind = STORAGE_VOLUME_RAM;
    }
    disk = fopen(argv[1], "r+b");
    assert(disk);
    g_storage.ext2_start_lba = test_start_lba;
    assert(!fseek(disk, 0, SEEK_END));
    g_storage.ext2_sector_count = ftell(disk) / 512;
    assert(ext2_mount() == 0);
    test_symlinks();
    struct storage_node source, target;
    assert(ext2_lookup_path("/source", &source) == 0);
    assert(ext2_link("/source", "/source-hard") == 0);
    assert(ext2_lookup_path("/source-hard", &target) == 0 &&
           target.first_cluster == source.first_cluster);
    struct ext2_inode linked_inode;
    assert(ext2_read_inode(source.first_cluster, &linked_inode) == 0 &&
           linked_inode.links_count == 2);
    assert(ext2_unlink("/source") == 0);
    assert(ext2_lookup_path("/source-hard", &target) == 0);
    assert(ext2_read_inode(target.first_cluster, &linked_inode) == 0 &&
           linked_inode.links_count == 1);
    assert(ext2_unlink("/source-hard") == 0 && ext2_lookup_path("/source-hard", &target) == -2);
    assert(ext2_write_file("/source", "new", 3) == 0);
    assert(ext2_lookup_path("/source", &source) == 0);
    assert(ext2_rename("/source", "/target") == 0);
    assert(ext2_lookup_path("/source", &target) == -2);
    assert(ext2_lookup_path("/target", &target) == 0 && target.first_cluster == source.first_cluster);
    assert(ext2_rename("/target", "/target") == 0);
    assert(ext2_rename("/missing", "/missing") == -2);
    assert(ext2_rename("/target", "/left") == -21);
    assert(ext2_rename("/left", "/target") == -20);
    assert(ext2_rename("/left", "/right") == -39);
    assert(ext2_unlink("/right/child") == 0);
    assert(ext2_rename("/left", "/right") == 0);
    assert(ext2_lookup_path("/left", &source) == -2);
    assert(ext2_lookup_path("/right", &target) == 0 && target.type == LEONOS_FS_TYPE_DIR);
    assert(ext2_write_file("/socket", "", 0) == 0);
    assert(ext2_lookup_path("/socket", &source) == 0);
    assert(ext2_mark_socket("/socket", &source) == 0);
    assert(ext2_lookup_path("/socket", &source) == 0 && source.type == LEONOS_FS_TYPE_SOCKET);
    assert(ext2_write_file("/replaced", "old", 3) == 0);
    assert(ext2_rename("/socket", "/replaced") == 0);
    assert(ext2_lookup_path("/replaced", &target) == 0 && target.type == LEONOS_FS_TYPE_SOCKET);
    assert(ext2_rename("/replaced", "/socket-persisted") == 0);
    assert(ext2_lookup_path("/socket-persisted", &target) == 0 && target.type == LEONOS_FS_TYPE_SOCKET);
    assert(ext2_write_file("/deleted-socket", "", 0) == 0);
    assert(ext2_lookup_path("/deleted-socket", &source) == 0);
    assert(ext2_mark_socket("/deleted-socket", &source) == 0 && ext2_unlink("/deleted-socket") == 0);
    assert(fclose(disk) == 0);
    puts("ext2 rename: replacement, inode identity, type errors, empty-directory rules PASS");
    return 0;
}
