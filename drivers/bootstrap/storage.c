/*
 * LeonOS storage facade.
 *
 * The implementation is split by responsibility under storage/.  These
 * modules are included into one translation unit deliberately: the storage
 * cache, active-volume selector, and transport scratch buffers remain private
 * to the storage subsystem while each feature has a focused source file.
 */
#include "storage/storage_internal.h"
#include "storage/storage_state.c"
#include "storage/storage_ide.c"
#include "storage/storage_ahci.c"
#include "storage/storage_nvme.c"
#include "storage/storage_block.c"
#include "storage/storage_iso.c"
#include "storage/storage_fat32.c"
#include "storage/storage_ext2.c"
#include "storage/storage_exfat.c"
#include "storage/storage_mount.c"
#include "storage/storage_vfs.c"
#include "storage/storage_installer.c"
#include "storage/storage_disk.c"
#include "storage/storage_identity.c"
