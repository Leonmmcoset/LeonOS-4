#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mntent.h>
#include "../../drivers/bootstrap/storage/storage_internal.h"
static void exfat_cache_invalidate(void) {}
#include "../../drivers/bootstrap/storage/storage_state.c"
static unsigned locked;
void kernel_execution_lock_irqsave(uint64_t *flags) { assert(!locked); locked = 1; *flags = 17; }
void kernel_execution_unlock_irqrestore(uint64_t flags) { assert(locked && flags == 17); locked = 0; }
static void storage_format_u32(char *out, uint32_t cap, const char *prefix, uint32_t n, int part)
{
    if (part < 0) snprintf(out, cap, "%s%u", prefix, n);
    else snprintf(out, cap, "%s%up%u", prefix, n, part + 1);
}
#include "../../drivers/bootstrap/storage/storage_mounts.c"
static int duplicate, io_error;
int storage_disk_partition_uuid(uint32_t disk, uint32_t part, char uuid[37])
{
    if (io_error) return -5;
    if (part != 3 || (disk && !duplicate)) return -2;
    strcpy(uuid, "12345678-1234-5678-90ab-112233445566");
    return 0;
}
#include "../../drivers/bootstrap/storage/storage_devlinks.c"
int main(void)
{
    g_devfs_enabled = 1;
    g_volumes[0] = (struct storage_volume){.ready=1, .filesystem=STORAGE_FILESYSTEM_EXT2,
        .mount_path="/", .ram_base=(void *)1};
    g_volumes[1] = (struct storage_volume){.ready=1, .filesystem=STORAGE_FILESYSTEM_FAT32,
        .mount_path="/boot", .data_partition_mount=1, .source_disk_id=2, .source_partition_index=0};
    g_volumes[2] = (struct storage_volume){.ready=1, .filesystem=STORAGE_FILESYSTEM_EXT2,
        .mount_path="/media/a b\\c\td\ne", .data_partition_mount=1, .source_disk_id=2, .source_partition_index=3};
    g_volumes[3] = (struct storage_volume){.ready=1, .filesystem=STORAGE_FILESYSTEM_EXT2,
        .mount_path="/media/a b\\c\td\ne/child"};
    char text[4096]={0}, chunks[4096]={0};
    uint32_t length, got;
    assert(storage_read_mounts(0, text, sizeof(text)-1, &length)==0);
    FILE *stream = tmpfile(); assert(stream);
    assert(fwrite(text, 1, length, stream)==length); rewind(stream);
    struct mntent *entry=getmntent(stream); assert(entry && !strcmp(entry->mnt_dir,"/"));
    entry=getmntent(stream); assert(entry && !strcmp(entry->mnt_fsname,"/dev/disk2p1"));
    entry=getmntent(stream); assert(entry && !strcmp(entry->mnt_dir,g_volumes[2].mount_path));
    fclose(stream);
    assert(storage_read_mountinfo(0,text,sizeof(text)-1,&length)==0); text[length]=0;
    assert(strstr(text,"1 0 0:1 / / rw - ext2 ramdisk rw\n"));
    assert(strstr(text,"2 1 0:2 / /boot rw - vfat /dev/disk2p1 rw\n"));
    assert(strstr(text,"4 3 0:4 "));
    assert(strstr(text,"0:200 / /dev rw - devfs devfs rw"));
    assert(strstr(text,"0:201 / /proc ro - proc proc ro"));
    for (uint32_t pos=0;pos<length;pos+=got) {
        assert(storage_read_mountinfo(pos,chunks+pos,7,&got)==0 && got);
    }
    assert(!memcmp(chunks,text,length));
    assert(storage_read_mountinfo(UINT64_MAX,chunks,10,&got)==0 && got==0);
    assert(storage_read_mountinfo(0,NULL,1,&got)==-22);
    const uint8_t guid[]={0x78,0x56,0x34,0x12,0x34,0x12,0x78,0x56,0x90,0xab,0x11,0x22,0x33,0x44,0x55,0x66};
    char uuid[37],target[48]; storage_partition_guid_text(guid,uuid);
    assert(!strcmp(uuid,"12345678-1234-5678-90ab-112233445566"));
    g_install_disk_count=2;
    assert(storage_devlink_target("/dev/disk/by-partuuid/12345678-1234-5678-90ab-112233445566",target)==0);
    assert(!strcmp(target,"../../disk0p4"));
    assert(storage_devlink_target("/dev/disk/by-partuuid/x",target)==-2);
    duplicate=1;
    assert(storage_devlink_target("/dev/disk/by-partuuid/12345678-1234-5678-90ab-112233445566",target)==-76);
    io_error=1;
    assert(storage_devlink_target("/dev/disk/by-partuuid/12345678-1234-5678-90ab-112233445566",target)==-5);
    puts("PASS mountinfo, mntent escaping, chunked reads, GPT identity, ambiguous identities and I/O errors");
}
