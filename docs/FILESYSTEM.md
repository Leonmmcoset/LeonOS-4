# Filesystems

LeonOS 4 uses a multi-filesystem storage layer. The normal installed system is
not a FAT32 root filesystem: it has a small FAT32 EFI System Partition (ESP)
and a separate writable ext2 root partition.

## Installed Disk Layout

| GPT partition | Type | Contents | Mounted drive |
| --- | --- | --- | --- |
| 1 | EFI System Partition / FAT32 | `EFI/`, `boot/`, `system/kernel.sys`, `system/middlelayer.sys` | `2:/` while Installer is running; boot-only in a normal session |
| 2 | Linux filesystem data / ext2 | normal system files: `system/`, `programs/`, `drivers/`, `docs/`, `users/`, `var/`, `tmp/` | `0:/` in a normal session, `1:/` while Installer is running |

UEFI GRUB and the early loader read partition 1. Once the kernel is running,
the storage layer selects partition 2 as `0:/`. A legacy one-partition FAT32
LeonOS disk remains readable and writable as `0:/`; it is a compatibility
fallback, not the layout produced by current image or installer builds.

The installer itself keeps using a FAT32 ramdisk root because it must start
before any target disk is trusted. Its payload is deliberately split:

- `0:/install/root` is copied to the ext2 target root `1:/`.
- `0:/install/esp` is copied to the FAT32 target ESP `2:/`.

Fresh installation creates both GPT partitions. Update requires both a valid
ext2 root and ESP; old FAT32-only installations should use a fresh install.

## Supported Formats

### ext2

The kernel implements the classic, unjournaled ext2 subset used by generated
images and installer-created targets:

- 1 KiB, 2 KiB, and 4 KiB blocks; generated LeonOS images use 4 KiB.
- 128-byte-or-larger classic inodes.
- Direct, singly indirect, and doubly indirect file blocks.
- File read/write/truncate, directory enumeration, create, `mkdir`, `unlink`,
  `rmdir`, and same-filesystem `rename`.
- Standard Linux filesystem-data GPT type GUID.

It intentionally rejects extents, journals, metadata checksums, 64-bit block
numbers, encryption, and unsupported incompatible feature bits. Build images
with `mke2fs -t ext2`; `tools/make_image.py` disables unsupported modern
extensions explicitly.

### FAT32

FAT32 remains supported for the ESP, installer ramdisk, legacy installed
images, removable media, and compatibility data volumes. Long file names,
directory traversal, reads, writes, creation, deletion, and renaming remain
available. FAT32 remains the only filesystem the UEFI/GRUB boot path relies
on.

### ISO 9660

Optical media is discovered through AHCI ATAPI and automatically assigned the
next free numeric drive. ISO 9660 volumes are read-only. This is independent
of whether the boot/root disk uses ext2 or FAT32.

## Runtime Data Mounts

Disk Manager can mount a supported, unprotected FAT32 or ext2 GPT data
partition for the current boot. The kernel assigns the first free numeric
drive (`1:/` through `9:/`), skipping mounted optical media and the installer's
temporary target drives. The assigned drive is shown in the partition status
and appears in File Manager's sidebar without restarting File Manager.

Runtime data mounts are deliberately non-persistent in this first version:
they are removed on reboot and no automatic mounting policy is stored on disk.
Mounting the same partition again is idempotent and returns its existing drive.
Only the FAT32 and classic ext2 subsets documented above are accepted; an
unknown or unsupported on-disk filesystem is rejected rather than mounted
according to its GPT type alone.

The installer reserves `1:/` for the target root and `2:/` for its ESP while a
target is mounted. Formatting or mounting an installer target is rejected with
busy if those drives contain an optical, data, or other runtime volume; this
prevents an installer operation from silently replacing a live drive mapping.

Unmounting is administrator-gated. The kernel refuses it while any live task
has that drive as its working directory, owns an open file on it, is executing
an image from it, or retains a file-backed mapping from it. Format and delete
are likewise refused while the partition is mounted. This avoids stale file
nodes and mappings after a volume is removed.

## Disk Manager Partitions

Disk Manager presents the GPT entries of every detected AHCI disk, including
their name, LBA range, filesystem probe, capacity, GPT role, protection state,
and assigned drive when mounted. It can create a 1 MiB-aligned FAT32 or ext2
data partition in free space, format an existing data partition as either
filesystem, delete an existing data partition's GPT entry, and mount or
unmount a supported data partition. New partition creation includes formatting
as part of the operation.

Formatting reuses the installer-grade FAT32 and ext2 formatters but applies
only to the selected partition extent. FAT32 data partitions use the Microsoft
Basic Data GPT type; ext2 data partitions use the Linux filesystem type.
Deleting a partition removes its GPT metadata only, so it is not a secure-wipe
operation.

Disk Manager never allows partition changes or runtime mounts against the
current boot disk or a target disk currently mounted by the installer. This
restriction is enforced by the kernel, not merely disabled buttons. The
initial partition-management ABI supports the common 128-entry, 128-byte GPT
table format generated by LeonOS and rejects a malformed, overlapping,
out-of-range, or CRC-invalid table before making a modification.

## VFS and Paths

LeonOS paths use numeric drive syntax, for example
`0:/system/apps/desktop/desktop.elf`. The middlelayer resolves `.` and `..`
and the storage layer selects a drive before dispatching the operation to its
filesystem implementation. Filesystem names are case-insensitive at the
LeonOS path layer for compatibility with historical FAT32 behavior; avoid
creating names that differ only by case on ext2.

The device directory is synthesized at `0:/dev`. `LEONACL.SYS` is internal
ACL sidecar metadata: it remains readable by the authorization service but is
hidden from normal FAT32 and ext2 directory enumeration.

## API Behavior

Current file syscalls support all writable ext2 and FAT32 roots:

- File reads and short, bounded writes.
- Create and overwrite through `open` flags.
- `mkdir`, `unlink`, `rmdir`, and `rename` within one mounted filesystem.
- Directory listing, `stat`, seek, and ACL service integration.

Cross-drive rename is rejected. ISO 9660 returns a read-only error for all
mutation operations. ext2 allocation updates its inode/block bitmaps and free
counts; FAT32 retains its cluster-chain allocator and LFN handling.

## Safety and Recovery

ext2 has no journal, so sudden loss of power during metadata updates can still
require offline repair. The installer writes a complete target layout before
copying payload files and treats a failed copy as an installation failure. The
normal image generator and installer formatter reserve distinct boot and root
partitions so a large runtime file cannot consume ESP space.

Use `tools/analyze_boot_log.py` when boot diagnostics show a root mount failure.
The log identifies whether ext2 was selected or the legacy FAT32 fallback was
used.
