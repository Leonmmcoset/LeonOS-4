/* Exercise the real cmd adapter without executing or opening any disk. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "glibcmd.h"

/* The app registry is outside command dispatch; no app is registered here. */
int leonos_app_registry_resolve(const char *name, char *path, uint32_t capacity)
{
    (void)name; (void)path; (void)capacity;
    return -ENOENT;
}

int main(void)
{
    char path[256];
    static const char *const commands[][2] = {
        {"mount", "/bin/mount"}, {"umount", "/bin/umount"},
        {"lsblk", "/bin/lsblk"}, {"blkid", "/usr/sbin/blkid"},
        {"fsck", "/usr/sbin/fsck"}, {"mkfs.ext2", "/usr/sbin/mkfs.ext2"},
        {"mkfs.fat", "/usr/sbin/mkfs.fat"}, {"mkfs.fat32", "/usr/sbin/mkfs.fat32"},
        {"mkfs.vfat", "/usr/sbin/mkfs.vfat"}, {"mkfs.exfat", "/usr/sbin/mkfs.exfat"},
        {"fsck.ext2", "/usr/sbin/fsck.ext2"}, {"fsck.fat", "/usr/sbin/fsck.fat"},
        {"fsck.fat32", "/usr/sbin/fsck.fat32"}, {"fsck.vfat", "/usr/sbin/fsck.vfat"},
        {"fsck.exfat", "/usr/sbin/fsck.exfat"},
        {"leonos-grub-installer", "/usr/sbin/leonos-grub-installer"}
    };
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (libcmd_find_exec(commands[i][0], NULL, path, sizeof(path)) != 0 ||
            strcmp(path, commands[i][1]) != 0) {
            fprintf(stderr, "%s did not resolve to %s (got %s)\n", commands[i][0], commands[i][1], path);
            return 1;
        }
    }
    assert(libcmd_find_exec("fdisk", "/usr/sbin:/usr/bin:/sbin:/bin", path, sizeof(path)) == 0);
    if (strcmp(path, "/usr/sbin/fdisk") != 0) {
        fprintf(stderr, "fdisk resolved to %s instead of /usr/sbin/fdisk\n", path);
        return 1;
    }
    assert(libcmd_find_exec("FDISK", NULL, path, sizeof(path)) == 0);
    assert(strcmp(path, "/usr/sbin/fdisk") == 0);
    errno = 0;
    assert(libcmd_find_exec("fdisk", NULL, path, 2) == -1);
    assert(errno == ENAMETOOLONG);
    assert(libcmd_find_exec("cat", NULL, path, sizeof(path)) == 0);
    assert(strcmp(path, "/bin/busybox") == 0);
    return 0;
}
