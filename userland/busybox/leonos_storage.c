/* Small storage applets for the LeonOS storage ABI.
 *
 * The upstream BusyBox util-linux implementations assume Linux block ioctls,
 * /proc/partitions, and mount(2). LeonOS exposes the same useful operations
 * through the disk-management ABI, so keep this adapter deliberately small
 * and explicit instead of pretending those Linux interfaces exist.
 */
//config:config LEONOS_FDISK
//config: bool "fdisk (LeonOS GPT editor)"
//config: default y
//config: help
//config: Read and edit the LeonOS GPT partition table.
//config:
//config:config LEONOS_MKFS_FAT
//config: bool "mkfs.fat (LeonOS FAT32 formatter)"
//config: default y
//config:
//config:config LEONOS_MKFS_EXT2
//config: bool "mkfs.ext2 (LeonOS ext2 formatter)"
//config: default y
//config:
//config:config LEONOS_MKFS_EXFAT
//config: bool "mkfs.exfat (LeonOS exFAT formatter)"
//config: default y
//config:
//config:config LEONOS_MOUNT
//config: bool "mount (LeonOS filesystem mount)"
//config: default y
//config:
//config:config LEONOS_UMOUNT
//config: bool "umount (LeonOS filesystem unmount)"
//config: default y
//config:
//config:config LEONOS_GRUB_INSTALLER
//config: bool "leonos-grub-installer (copy EFI payload)"
//config: default y
//config:
//config:config LEONOS_FSCK
//config: bool "fsck.* (LeonOS filesystem checker)"
//config: default y
//config:
//config:config LEONOS_BLKID
//config: bool "blkid (LeonOS block IDs)"
//config: default y
//config:
//config:config LEONOS_LSBLK
//config: bool "lsblk (LeonOS block devices)"
//config: default y

//applet:IF_LEONOS_FDISK(APPLET_ODDNAME(fdisk, leonos_fdisk, BB_DIR_SBIN, BB_SUID_DROP, leonos_fdisk))
//applet:IF_LEONOS_MKFS_FAT(APPLET_ODDNAME(mkfs.fat, leonos_mkfs_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_mkfs_fat))
//applet:IF_LEONOS_MKFS_FAT(APPLET_ODDNAME(mkfs.fat32, leonos_mkfs_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_mkfs_fat))
//applet:IF_LEONOS_MKFS_FAT(APPLET_ODDNAME(mkfs.vfat, leonos_mkfs_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_mkfs_fat))
//applet:IF_LEONOS_MKFS_EXT2(APPLET_ODDNAME(mkfs.ext2, leonos_mkfs_ext2, BB_DIR_SBIN, BB_SUID_DROP, leonos_mkfs_ext2))
//applet:IF_LEONOS_MKFS_EXFAT(APPLET_ODDNAME(mkfs.exfat, leonos_mkfs_exfat, BB_DIR_SBIN, BB_SUID_DROP, leonos_mkfs_exfat))
//applet:IF_LEONOS_MOUNT(APPLET(mount, BB_DIR_BIN, BB_SUID_DROP))
//applet:IF_LEONOS_UMOUNT(APPLET(umount, BB_DIR_SBIN, BB_SUID_DROP))
//applet:IF_LEONOS_GRUB_INSTALLER(APPLET_ODDNAME(leonos-grub-installer, leonos_grub_installer, BB_DIR_SBIN, BB_SUID_DROP, leonos_grub_installer))
//applet:IF_LEONOS_FSCK(APPLET_ODDNAME(fsck.fat, leonos_fsck_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_fsck_fat))
//applet:IF_LEONOS_FSCK(APPLET_ODDNAME(fsck.fat32, leonos_fsck_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_fsck_fat))
//applet:IF_LEONOS_FSCK(APPLET_ODDNAME(fsck.vfat, leonos_fsck_fat, BB_DIR_SBIN, BB_SUID_DROP, leonos_fsck_fat))
//applet:IF_LEONOS_FSCK(APPLET_ODDNAME(fsck.ext2, leonos_fsck_ext2, BB_DIR_SBIN, BB_SUID_DROP, leonos_fsck_ext2))
//applet:IF_LEONOS_FSCK(APPLET_ODDNAME(fsck.exfat, leonos_fsck_exfat, BB_DIR_SBIN, BB_SUID_DROP, leonos_fsck_exfat))
//applet:IF_LEONOS_FSCK(APPLET(fsck, BB_DIR_SBIN, BB_SUID_DROP))
//applet:IF_LEONOS_BLKID(APPLET(blkid, BB_DIR_SBIN, BB_SUID_DROP))
//applet:IF_LEONOS_LSBLK(APPLET(lsblk, BB_DIR_BIN, BB_SUID_DROP))

//usage:#define leonos_fdisk_trivial_usage "[-l] [DISK]"
//usage:#define leonos_fdisk_full_usage "\n\n"
//usage:       "Show or edit a LeonOS GPT disk. Interactive keys: p print, n new, d delete, t type, r rename, w write/exit, q quit.\n"
//usage:#define leonos_fdisk_example_usage
//usage:#define leonos_mkfs_fat_trivial_usage "[--force] BLOCKDEV"
//usage:#define leonos_mkfs_fat_full_usage "\n\nMake a FAT32 filesystem on a LeonOS partition\n"
//usage:#define leonos_mkfs_ext2_trivial_usage "[--force] BLOCKDEV"
//usage:#define leonos_mkfs_ext2_full_usage "\n\nMake an ext2 filesystem on a LeonOS partition\n"
//usage:#define leonos_mkfs_exfat_trivial_usage "[--force] BLOCKDEV"
//usage:#define leonos_mkfs_exfat_full_usage "\n\nMake an exFAT filesystem on a LeonOS partition\n"
//usage:#define mount_trivial_usage "[-t FSTYPE] BLOCKDEV DIR"
//usage:#define mount_full_usage "\n\nMount a LeonOS FAT32, exFAT, or ext2 partition\n"
//usage:#define umount_trivial_usage "DIR|BLOCKDEV"
//usage:#define umount_full_usage "\n\nUnmount a LeonOS data partition\n"
//usage:#define leonos_grub_installer_trivial_usage "ESP-MOUNTPOINT"
//usage:#define leonos_grub_installer_full_usage "\n\nInstall the LeonOS EFI/GRUB payload into an ESP mount\n"
//usage:#define leonos_fsck_fat_trivial_usage "BLOCKDEV"
//usage:#define leonos_fsck_fat_full_usage "\n\nCheck a FAT32 filesystem superblock (read-only)\n"
//usage:#define leonos_fsck_ext2_trivial_usage "BLOCKDEV"
//usage:#define leonos_fsck_ext2_full_usage "\n\nCheck an ext2 filesystem superblock (read-only)\n"
//usage:#define leonos_fsck_exfat_trivial_usage "BLOCKDEV"
//usage:#define leonos_fsck_exfat_full_usage "\n\nCheck an exFAT filesystem superblock (read-only)\n"
//usage:#define fsck_trivial_usage "[-n] BLOCKDEV"
//usage:#define fsck_full_usage "\n\nCheck a detected LeonOS filesystem superblock (read-only)\n"
//usage:#define blkid_trivial_usage "[BLOCKDEV...]"
//usage:#define blkid_full_usage "\n\nPrint LeonOS filesystem and GPT partition identifiers\n"
//usage:#define lsblk_trivial_usage
//usage:#define lsblk_full_usage "\n\nList LeonOS disks and GPT partitions\n"

#include "libbb.h"
#pragma push_macro("stat")
#pragma push_macro("fstat")
#undef stat
#undef fstat
#include <leonos/fs.h>
#pragma pop_macro("fstat")
#pragma pop_macro("stat")
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_disk_name(const char *path, uint32_t *disk_id, int *partition)
{
    const char *name;
    const char *p;
    char *end;
    unsigned long value;
    if (!path || !disk_id || !partition || strncmp(path, "/dev/", 5) != 0) {
        return -1;
    }
    name = path + 5;
    *partition = -1;
    if (strncmp(name, "disk", 4) == 0) {
        p = name + 4;
    } else if (strncmp(name, "sda", 3) == 0) {
        *disk_id = 0;
        p = name + 3;
        if (*p == 0) return 0;
        if (*p != 'p') {
            *partition = (int)strtoul(p, &end, 10) - 1;
            return *partition >= 0 && *end == 0 ? 0 : -1;
        }
    } else if (strncmp(name, "vda", 3) == 0) {
        *disk_id = 0;
        p = name + 3;
        if (*p == 0) return 0;
        if (*p != 'p') {
            *partition = (int)strtoul(p, &end, 10) - 1;
            return *partition >= 0 && *end == 0 ? 0 : -1;
        }
    } else if (strncmp(name, "nvme0n1", 7) == 0) {
        *disk_id = 0;
        p = name + 7;
        if (*p == 0) return 0;
        if (*p != 'p') return -1;
        ++p;
        value = strtoul(p, &end, 10);
        if (end == p || *end || value == 0 || value > LEONOS_DISK_MAX_PARTITIONS)
            return -1;
        *partition = (int)value - 1;
        return 0;
    } else {
        return -1;
    }
    value = strtoul(p, &end, 10);
    if (end == p || value >= LEONOS_INSTALL_MAX_DISKS) {
        return -1;
    }
    *disk_id = (uint32_t)value;
    if (*end) {
        if (*end != 'p') return -1;
        p = end + 1;
        value = strtoul(p, &end, 10);
        if (end == p || *end || value == 0 || value > LEONOS_DISK_MAX_PARTITIONS) {
            return -1;
        }
        *partition = (int)value - 1;
    }
    return 0;
}

static int list_partitions(uint32_t disk_id, struct leonos_disk_partition *parts,
                           uint32_t *count)
{
    uint32_t capacity = LEONOS_DISK_MAX_PARTITIONS;
    if (!parts || !count) return -1;
    return leonos_disk_list_partitions(disk_id, parts, capacity, count);
}

static const char *filesystem_name(uint32_t filesystem)
{
    switch (filesystem) {
    case LEONOS_DISK_FILESYSTEM_FAT32: return "fat32";
    case LEONOS_DISK_FILESYSTEM_EXT2: return "ext2";
    case LEONOS_DISK_FILESYSTEM_EXFAT: return "exfat";
    default: return "unknown";
    }
}

static const uint8_t leonos_basic_data_guid[16] = {
    0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
    0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
};
static const uint8_t leonos_esp_guid[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
};
static const uint8_t leonos_linux_guid[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4,
};

static int partition_guid_equal(const uint8_t *left, const uint8_t *right)
{
    return left && right && memcmp(left, right, 16) == 0;
}

static const char *partition_type_name(const struct leonos_disk_partition *part)
{
    if (!part) return "unknown";
    if (partition_guid_equal(part->type_guid, leonos_esp_guid)) return "esp";
    if (partition_guid_equal(part->type_guid, leonos_linux_guid)) return "linux";
    if (partition_guid_equal(part->type_guid, leonos_basic_data_guid)) return "basic";
    return "other";
}

static void print_partitions(uint32_t disk_id)
{
    struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
    uint32_t count = 0;
    if (list_partitions(disk_id, parts, &count) < 0) {
        bb_error_msg("cannot read GPT on disk %u", disk_id);
        return;
    }
    printf("Disk /dev/disk%u\n", disk_id);
    printf("Device        Start       Sectors     Filesystem  GPT type  Name\n");
    for (uint32_t i = 0; i < count; ++i) {
        printf("/dev/disk%up%-4u %-11llu %-11llu %-11s %-9s %s\n", disk_id,
               parts[i].index + 1u, (unsigned long long)parts[i].first_lba,
               (unsigned long long)parts[i].sector_count,
               filesystem_name(parts[i].filesystem), partition_type_name(&parts[i]), parts[i].name);
    }
}

static int print_all_partitions(void)
{
    struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
    uint32_t disk_count = 0;
    if (leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) {
        bb_error_msg("cannot read disk inventory");
        return 1;
    }
    for (uint32_t i = 0; i < disk_count; ++i) {
        print_partitions(disks[i].id);
    }
    return 0;
}

static int read_line(const char *prompt, char *buffer, size_t capacity)
{
    if (prompt) fputs(prompt, stdout);
    if (!fgets(buffer, capacity, stdin)) return -1;
    buffer[strcspn(buffer, "\r\n")] = 0;
    return 0;
}

static int fdisk_new_partition(uint32_t disk_id)
{
    char line[80];
    struct leonos_disk_partition_create request = {0};
    unsigned long size;
    if (read_line("Partition size in MiB: ", line, sizeof(line)) < 0) return -1;
    size = strtoul(line, NULL, 10);
    if (!size || size > 0xffffffffUL / 2048UL) {
        bb_error_msg("invalid partition size");
        return -1;
    }
    request.disk_id = disk_id;
    request.filesystem = LEONOS_DISK_FILESYSTEM_UNKNOWN;
    request.size_mib = (uint32_t)size;
    if (read_line("Partition name (optional): ", request.name, sizeof(request.name)) < 0)
        return -1;
    if (leonos_disk_create_partition(&request) < 0) {
        bb_error_msg("partition creation failed");
        return -1;
    }
    puts("Partition created. Use mkfs.* to format it.");
    return 0;
}

static int fdisk_delete_partition(uint32_t disk_id)
{
    char line[32];
    struct leonos_disk_partition_delete request = {0};
    unsigned long number;
    if (read_line("Partition number: ", line, sizeof(line)) < 0) return -1;
    number = strtoul(line, NULL, 10);
    if (!number || number > LEONOS_DISK_MAX_PARTITIONS) {
        bb_error_msg("invalid partition number");
        return -1;
    }
    request.disk_id = disk_id;
    request.partition_index = (uint32_t)number - 1u;
    if (leonos_disk_delete_partition(&request) < 0) {
        bb_error_msg("partition deletion failed");
        return -1;
    }
    puts("Partition deleted.");
    return 0;
}

static int fdisk_edit_partition_type(uint32_t disk_id)
{
    char line[32];
    struct leonos_disk_partition_edit request = {0};
    unsigned long number;
    if (read_line("Partition number: ", line, sizeof(line)) < 0) return -1;
    number = strtoul(line, NULL, 10);
    if (!number || number > LEONOS_DISK_MAX_PARTITIONS) {
        bb_error_msg("invalid partition number");
        return -1;
    }
    if (read_line("GPT type (basic, esp, linux): ", line, sizeof(line)) < 0) return -1;
    request.disk_id = disk_id;
    request.partition_index = (uint32_t)number - 1u;
    request.edit_mask = LEONOS_DISK_PARTITION_EDIT_TYPE;
    if (strcmp(line, "basic") == 0 || strcmp(line, "basic-data") == 0 || strcmp(line, "1") == 0) {
        request.type = LEONOS_DISK_PARTITION_TYPE_BASIC_DATA;
    } else if (strcmp(line, "esp") == 0 || strcmp(line, "2") == 0) {
        request.type = LEONOS_DISK_PARTITION_TYPE_ESP;
    } else if (strcmp(line, "linux") == 0 || strcmp(line, "3") == 0) {
        request.type = LEONOS_DISK_PARTITION_TYPE_LINUX;
    } else {
        bb_error_msg("unknown GPT type");
        return -1;
    }
    if (leonos_disk_edit_partition(&request) < 0) {
        bb_error_msg("partition type update failed");
        return -1;
    }
    puts("Partition type updated.");
    return 0;
}

static int fdisk_edit_partition_name(uint32_t disk_id)
{
    char line[32];
    struct leonos_disk_partition_edit request = {0};
    unsigned long number;
    if (read_line("Partition number: ", line, sizeof(line)) < 0) return -1;
    number = strtoul(line, NULL, 10);
    if (!number || number > LEONOS_DISK_MAX_PARTITIONS) {
        bb_error_msg("invalid partition number");
        return -1;
    }
    request.disk_id = disk_id;
    request.partition_index = (uint32_t)number - 1u;
    request.edit_mask = LEONOS_DISK_PARTITION_EDIT_NAME;
    if (read_line("GPT name (empty clears): ", request.name, sizeof(request.name)) < 0) return -1;
    if (leonos_disk_edit_partition(&request) < 0) {
        bb_error_msg("partition name update failed");
        return -1;
    }
    puts("Partition name updated.");
    return 0;
}

int leonos_fdisk_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_fdisk_main(int argc, char **argv)
{
    uint32_t disk_id;
    int partition;
    char line[32];
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        return print_all_partitions();
    }
    if (argc < 2 || argc > 3) bb_show_usage();
    if (parse_disk_name(argv[argc - 1], &disk_id, &partition) < 0 || partition >= 0) {
        bb_error_msg("expected a whole disk such as /dev/disk0");
        return 1;
    }
    if (argc == 3) {
        if (strcmp(argv[1], "-l") != 0) bb_show_usage();
        print_partitions(disk_id);
        return 0;
    }
    for (;;) {
        if (read_line("Command (p print, n new, d delete, t type, r rename, w write, q quit): ",
                      line, sizeof(line)) < 0) return 0;
        if (line[0] == 'p') print_partitions(disk_id);
        else if (line[0] == 'n') (void)fdisk_new_partition(disk_id);
        else if (line[0] == 'd') (void)fdisk_delete_partition(disk_id);
        else if (line[0] == 't') (void)fdisk_edit_partition_type(disk_id);
        else if (line[0] == 'r') (void)fdisk_edit_partition_name(disk_id);
        else if (line[0] == 'w' || line[0] == 'q') return 0;
        else puts("Unknown command.");
    }
}

static int format_partition(int argc, char **argv, uint32_t filesystem)
{
    uint32_t disk_id;
    int partition;
    struct leonos_disk_partition_format request;
    const char *path;
    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--force") == 0) {
        /* The kernel still refuses to format a mounted/protected partition;
         * accept the conventional spelling without weakening that guard. */
        path = argv[2];
    } else {
        bb_show_usage();
        return 1;
    }
    if (parse_disk_name(path, &disk_id, &partition) < 0 || partition < 0) {
        bb_error_msg("expected a partition such as /dev/disk0p2");
        return 1;
    }
    request = (struct leonos_disk_partition_format){
        .disk_id = disk_id,
        .partition_index = (uint32_t)partition,
        .filesystem = filesystem,
    };
    if (leonos_disk_format_partition(&request) < 0) {
        bb_error_msg("format failed for %s", path);
        return 1;
    }
    printf("Formatted %s.\n", path);
    return 0;
}

int leonos_mkfs_fat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_mkfs_fat_main(int argc, char **argv)
{
    return format_partition(argc, argv, LEONOS_DISK_FILESYSTEM_FAT32);
}

int leonos_mkfs_ext2_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_mkfs_ext2_main(int argc, char **argv)
{
    return format_partition(argc, argv, LEONOS_DISK_FILESYSTEM_EXT2);
}

int leonos_mkfs_exfat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_mkfs_exfat_main(int argc, char **argv)
{
    return format_partition(argc, argv, LEONOS_DISK_FILESYSTEM_EXFAT);
}

static int find_mount(const char *target, uint32_t *disk_id, uint32_t *partition)
{
    struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
    uint32_t disk_count = 0;
    if (leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) return -1;
    for (uint32_t d = 0; d < disk_count; ++d) {
        struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
        uint32_t count = 0;
        if (list_partitions(disks[d].id, parts, &count) < 0) continue;
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(parts[i].mount_path, target) == 0) {
                *disk_id = disks[d].id;
                *partition = parts[i].index;
                return 0;
            }
        }
    }
    return -1;
}

static int leonos_fstype_matches(const char *fstype, uint32_t filesystem)
{
    if (!fstype || strcmp(fstype, "auto") == 0) return 1;
    if (filesystem == LEONOS_DISK_FILESYSTEM_FAT32) {
        return strcmp(fstype, "fat") == 0 || strcmp(fstype, "vfat") == 0 ||
               strcmp(fstype, "fat32") == 0;
    }
    return strcmp(fstype, filesystem_name(filesystem)) == 0;
}

static int mounted_filesystem(uint32_t disk_id, uint32_t partition, uint32_t *filesystem)
{
    struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
    uint32_t count = 0;
    if (!filesystem || list_partitions(disk_id, parts, &count) < 0) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (parts[i].index == partition) {
            *filesystem = parts[i].filesystem;
            return 0;
        }
    }
    return -1;
}

static void print_mounts(void)
{
    struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
    uint32_t disk_count = 0;
    if (leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) {
        bb_error_msg("cannot read disk inventory");
        return;
    }
    for (uint32_t d = 0; d < disk_count; ++d) {
        struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
        uint32_t count = 0;
        if (list_partitions(disks[d].id, parts, &count) < 0) continue;
        for (uint32_t i = 0; i < count; ++i) {
            if ((parts[i].flags & LEONOS_DISK_PARTITION_FLAG_MOUNTED) != 0) {
                printf("/dev/disk%up%u on %s type %s\n", disks[d].id,
                       parts[i].index + 1u, parts[i].mount_path,
                       filesystem_name(parts[i].filesystem));
            }
        }
    }
}

int mount_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mount_main(int argc, char **argv)
{
    uint32_t disk_id;
    int partition;
    char mounted[LEONOS_FS_PATH_LEN];
    const char *source;
    const char *target;
    const char *fstype = NULL;
    if (argc == 1) {
        print_mounts();
        return 0;
    }
    if (argc == 3) {
        source = argv[1];
        target = argv[2];
    } else if (argc == 5 && strcmp(argv[1], "-t") == 0) {
        fstype = argv[2];
        source = argv[3];
        target = argv[4];
    } else {
        bb_show_usage();
        return 1;
    }
    /* The kernel detects the on-disk format. Keep -t as a compatibility
     * check and reject names outside the filesystems supported by LeonOS. */
    if (fstype && strcmp(fstype, "auto") != 0 &&
        strcmp(fstype, "fat") != 0 && strcmp(fstype, "vfat") != 0 &&
        strcmp(fstype, "fat32") != 0 && strcmp(fstype, "ext2") != 0 &&
        strcmp(fstype, "exfat") != 0) {
        bb_error_msg("unsupported filesystem type: %s", fstype);
        return 1;
    }
    if (parse_disk_name(source, &disk_id, &partition) < 0 || partition < 0 ||
        target[0] != '/') {
        bb_error_msg("usage: mount [-t FSTYPE] /dev/disk0pN /mountpoint");
        return 1;
    }
    if (fstype) {
        uint32_t filesystem;
        if (mounted_filesystem(disk_id, (uint32_t)partition, &filesystem) < 0 ||
            !leonos_fstype_matches(fstype, filesystem)) {
            bb_error_msg("%s is not a %s filesystem", source, fstype);
            return 1;
        }
    }
    if (leonos_disk_mount_partition_at(disk_id, (uint32_t)partition, target,
                                       mounted, sizeof(mounted)) < 0) {
        bb_error_msg("mount failed for %s", source);
        return 1;
    }
    printf("%s mounted on %s\n", source, mounted);
    return 0;
}

int umount_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int umount_main(int argc, char **argv)
{
    uint32_t disk_id;
    int partition;
    uint32_t mounted_partition;
    if (argc != 2) bb_show_usage();
    if (parse_disk_name(argv[1], &disk_id, &partition) == 0 && partition >= 0) {
        if (leonos_disk_unmount_partition(disk_id, (uint32_t)partition) < 0) {
            bb_error_msg("umount failed for %s", argv[1]);
            return 1;
        }
        return 0;
    }
    if (find_mount(argv[1], &disk_id, &mounted_partition) < 0 ||
        leonos_disk_unmount_partition(disk_id, mounted_partition) < 0) {
        bb_error_msg("mount point is not a mounted LeonOS partition: %s", argv[1]);
        return 1;
    }
    return 0;
}

static int storage_fsck_device(const char *path, uint32_t expected, int allow_unknown)
{
    struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
    uint32_t disk_id;
    uint32_t count = 0;
    int partition;
    if (parse_disk_name(path, &disk_id, &partition) < 0 || partition < 0 ||
        list_partitions(disk_id, parts, &count) < 0) {
        bb_error_msg("cannot read partition %s", path);
        return 1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (parts[i].index != (uint32_t)partition) continue;
        if (!allow_unknown && parts[i].filesystem != expected) {
            bb_error_msg("%s is %s, not %s", path, filesystem_name(parts[i].filesystem),
                         filesystem_name(expected));
            return 1;
        }
        if (parts[i].filesystem == LEONOS_DISK_FILESYSTEM_UNKNOWN) {
            bb_error_msg("%s: filesystem signature is invalid", path);
            return 1;
        }
        printf("%s: clean, %s filesystem\n", path, filesystem_name(parts[i].filesystem));
        return 0;
    }
    bb_error_msg("partition does not exist: %s", path);
    return 1;
}

int leonos_fsck_fat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_fsck_fat_main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "-n") == 0) return storage_fsck_device(argv[2], LEONOS_DISK_FILESYSTEM_FAT32, 0);
    if (argc != 2) bb_show_usage();
    return storage_fsck_device(argv[1], LEONOS_DISK_FILESYSTEM_FAT32, 0);
}

int leonos_fsck_ext2_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_fsck_ext2_main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "-n") == 0) return storage_fsck_device(argv[2], LEONOS_DISK_FILESYSTEM_EXT2, 0);
    if (argc != 2) bb_show_usage();
    return storage_fsck_device(argv[1], LEONOS_DISK_FILESYSTEM_EXT2, 0);
}

int leonos_fsck_exfat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_fsck_exfat_main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "-n") == 0) return storage_fsck_device(argv[2], LEONOS_DISK_FILESYSTEM_EXFAT, 0);
    if (argc != 2) bb_show_usage();
    return storage_fsck_device(argv[1], LEONOS_DISK_FILESYSTEM_EXFAT, 0);
}

int fsck_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fsck_main(int argc, char **argv)
{
    const char *path;
    if (argc == 3 && strcmp(argv[1], "-n") == 0) path = argv[2];
    else {
        if (argc != 2) bb_show_usage();
        path = argv[1];
    }
    return storage_fsck_device(path, LEONOS_DISK_FILESYSTEM_UNKNOWN, 1);
}

static int print_blkid_partition(const char *path)
{
    uint32_t disk_id;
    int partition;
    struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
    uint32_t count = 0;
    if (parse_disk_name(path, &disk_id, &partition) < 0 || partition < 0 ||
        list_partitions(disk_id, parts, &count) < 0) return 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (parts[i].index != (uint32_t)partition) continue;
        printf("%s: TYPE=\"%s\" PARTLABEL=\"%s\"\n", path,
               filesystem_name(parts[i].filesystem), parts[i].name);
        return 0;
    }
    bb_error_msg("%s: partition not found", path);
    return 1;
}

int blkid_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int blkid_main(int argc, char **argv)
{
    struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
    uint32_t disk_count = 0;
    int status = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (print_blkid_partition(argv[i]) != 0) status = 1;
        }
        return status;
    }
    if (leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) {
        bb_error_msg("cannot read disk inventory");
        return 1;
    }
    for (uint32_t d = 0; d < disk_count; ++d) {
        struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
        uint32_t count = 0;
        if (list_partitions(disks[d].id, parts, &count) < 0) continue;
        for (uint32_t i = 0; i < count; ++i) {
            char path[48];
            snprintf(path, sizeof(path), "/dev/disk%up%u", disks[d].id, parts[i].index + 1u);
            (void)print_blkid_partition(path);
        }
    }
    return 0;
}

int lsblk_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsblk_main(int argc, char **argv)
{
    struct leonos_install_disk disks[LEONOS_INSTALL_MAX_DISKS];
    uint32_t disk_count = 0;
    (void)argv;
    if (argc != 1) bb_show_usage();
    if (leonos_install_list_disks(disks, LEONOS_INSTALL_MAX_DISKS, &disk_count) < 0) {
        bb_error_msg("cannot read disk inventory");
        return 1;
    }
    puts("NAME        SIZE        TYPE  FSTYPE  LABEL              MOUNTPOINT");
    for (uint32_t d = 0; d < disk_count; ++d) {
        struct leonos_disk_partition parts[LEONOS_DISK_MAX_PARTITIONS];
        uint32_t count = 0;
        unsigned long long bytes = (unsigned long long)disks[d].sector_count * disks[d].sector_size;
        printf("disk%u       %llu      disk\n", disks[d].id, bytes);
        if (list_partitions(disks[d].id, parts, &count) < 0) continue;
        for (uint32_t i = 0; i < count; ++i) {
            printf("disk%up%-4u %-11llu part  %-7s %-18s %s\n", disks[d].id,
                   parts[i].index + 1u,
                   (unsigned long long)parts[i].sector_count * disks[d].sector_size,
                   filesystem_name(parts[i].filesystem), parts[i].name,
                   parts[i].mount_path);
        }
    }
    return 0;
}

static int grub_copy_file(const char *source, const char *destination)
{
    FILE *in = fopen(source, "rb");
    FILE *out;
    unsigned char buffer[8192];
    size_t got;
    if (!in) return -1;
    out = fopen(destination, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((got = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    return fclose(out);
}

static int grub_copy_tree(const char *source, const char *destination)
{
    DIR *dir = opendir(source);
    struct dirent *entry;
    if (!dir) return -1;
    (void)mkdir(destination, 0);
    while ((entry = readdir(dir)) != NULL) {
        char src[LEONOS_FS_PATH_LEN];
        char dst[LEONOS_FS_PATH_LEN];
        int length = snprintf(src, sizeof(src), "%s/%s", source, entry->d_name);
        int target_length = snprintf(dst, sizeof(dst), "%s/%s", destination, entry->d_name);
        if (length < 0 || target_length < 0 || (size_t)length >= sizeof(src) ||
            (size_t)target_length >= sizeof(dst)) {
            closedir(dir);
            return -1;
        }
        if (entry->d_type == DT_DIR) {
            if (grub_copy_tree(src, dst) < 0) {
                closedir(dir);
                return -1;
            }
        } else if (grub_copy_file(src, dst) < 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

int leonos_grub_installer_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int leonos_grub_installer_main(int argc, char **argv)
{
    const char *esp = argc == 2 ? argv[1] : NULL;
    static const char *const files[] = {
        "EFI/BOOT/BOOTX64.EFI", "loader.elf", "system/kernel.sys", "system/middlelayer.sys", NULL
    };
    if (!esp) bb_show_usage();
    {
        char path[LEONOS_FS_PATH_LEN];
        int length = snprintf(path, sizeof(path), "%s/EFI", esp);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            bb_error_msg("cannot prepare ESP directories");
            return 1;
        }
        (void)mkdir(path, 0);
        length = snprintf(path, sizeof(path), "%s/EFI/BOOT", esp);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            bb_error_msg("cannot prepare ESP directories");
            return 1;
        }
        (void)mkdir(path, 0);
        length = snprintf(path, sizeof(path), "%s/system", esp);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            bb_error_msg("cannot prepare ESP directories");
            return 1;
        }
        (void)mkdir(path, 0);
    }
    for (size_t i = 0; files[i]; ++i) {
        char source[LEONOS_FS_PATH_LEN];
        char destination[LEONOS_FS_PATH_LEN];
        int source_length = snprintf(source, sizeof(source), "/install/esp/%s", files[i]);
        int destination_length = snprintf(destination, sizeof(destination), "%s/%s", esp, files[i]);
        if (source_length < 0 || destination_length < 0 ||
            (size_t)source_length >= sizeof(source) || (size_t)destination_length >= sizeof(destination) ||
            grub_copy_file(source, destination) < 0) {
            bb_error_msg("cannot install %s", files[i]);
            return 1;
        }
    }
    {
        char source[LEONOS_FS_PATH_LEN];
        char destination[LEONOS_FS_PATH_LEN];
        if (snprintf(source, sizeof(source), "/install/esp/grub") < 0 ||
            snprintf(destination, sizeof(destination), "%s/grub", esp) < 0 ||
            grub_copy_tree(source, destination) < 0) {
            bb_error_msg("cannot install GRUB files");
            return 1;
        }
    }
    printf("LeonOS GRUB payload installed to %s.\n", esp);
    return 0;
}
