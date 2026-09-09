#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/bootstrap/storage/storage_internal.h"

static FILE *disk;
static int storage_read_sectors(uint64_t lba, uint32_t count, void *out)
{
    assert(lba && !fseek(disk, (lba - 1) * 512, SEEK_SET));
    return fread(out, 512, count, disk) == count ? 0 : -5;
}
static int storage_write_sectors(uint64_t lba, uint32_t count, const void *in)
{
    assert(lba && !fseek(disk, (lba - 1) * 512, SEEK_SET));
    return fwrite(in, 512, count, disk) == count && !fflush(disk) ? 0 : -5;
}
static void storage_memzero(void *out, size_t n) { memset(out, 0, n); }
static void storage_memcpy(void *out, const void *in, size_t n) { memcpy(out, in, n); }
static uint32_t storage_strlen(const char *text) { return strlen(text); }
static int storage_text_eq_ci(const char *a, const char *b) { return !strcasecmp(a, b); }
static void storage_copy_text(char *out, uint32_t cap, const char *text) { snprintf(out, cap, "%s", text); }
static void storage_cache_invalidate(void) {}
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

int main(int argc, char **argv)
{
    assert(argc == 2);
    disk = fopen(argv[1], "r+b");
    assert(disk);
    g_storage.ext2_start_lba = 1;
    g_storage.ext2_sector_count = 16384;
    assert(ext2_mount() == 0);
    struct storage_node source, target;
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
