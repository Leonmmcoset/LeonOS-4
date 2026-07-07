# Filesystem

LeonOS 4 uses numbered drives and slash-separated paths:

- `0:/`
- `1:/`

Normal boots mount `0:/` from the GPT ESP FAT32 partition on the AHCI boot
disk. Installer boots mount `0:/` from the `leonos-installer-root` FAT32
ramdisk; the target ESP can be mounted as an optional target drive.

Path normalization is currently provided by the middlelayer VFS service
(`LEONOS_VFS_OP_RESOLVE_PATH`) with a kernel fallback resolver. FAT32 parsing,
block I/O, directory mutation, and user pointer validation remain in the
kernel.

## Mounts

Mount policy comes from middlelayer:

- Normal root: FAT32 boot ESP.
- Installer root: FAT32 ramdisk.
- Device tree: `0:/dev`.
- Optional target ESP during installation.

The current devfs surface is deliberately small:

- `0:/dev`
- `0:/dev/fb0`

## Users and Local Data

Multi-user v1 uses these fixed paths:

- `0:/etc/accounts.db`: middlelayer-owned account database.
- `0:/etc/oobe.done`: first-run completion marker.
- `0:/etc/fileassoc.cfg`: optional launcher file-association overrides.
- `0:/etc/services.cfg`: optional startup/service policy overrides.
- `0:/users/<name>`: user home directory.
- `0:/users/<name>/desktop`
- `0:/users/<name>/documents`
- `0:/users/<name>/downloads`
- `0:/tmp`: shared temporary write area.

The installed image does not pre-create `accounts.db`. On first boot, OOBE
creates the first administrator, creates the home layout, writes
`0:/etc/oobe.done`, and enters the administrator desktop. Later boots enter
`login.elf`. If `oobe.done` exists but middlelayer cannot find an enabled
administrator in `accounts.db`, desktop launches OOBE again.

New account creation seeds the user's desktop with `File Manager.lnk`,
`Task Manager.lnk`, `Settings.lnk`, and `Browser.lnk`, pointing to
`0:/userland/fileman.elf`, `0:/userland/taskmgr.elf`,
`0:/userland/settings.elf`, and `0:/userland/browser.elf`.

`downloadmgr.elf` saves successful downloads to `0:/users/<name>/downloads`
when a user session is active, and falls back to `0:/tmp` when no home directory
is available.

## Supported operations

Current FAT32 and syscall support includes:

- Directory listing and directory file descriptors.
- File reads and writes.
- Create and overwrite through `open` flags.
- `stat`, `fstat`, and `lseek`.
- `mkdir`.
- `unlink`.
- `rmdir`.
- `rename`.

## Permission Model

FAT32 still has no persisted owner or mode fields. LeonOS v1 permissions are
enforced at syscall and ioctl boundaries:

- All users can read system programs/resources under `0:/system`,
  `0:/userland`, and `0:/dev`.
- Normal users can write their own `0:/users/<name>` tree and `0:/tmp`.
- Administrators can access and manage other user directories.
- `0:/etc/accounts.db` is denied to user tasks, including administrators; auth
  APIs are the supported access path.
- User management and installer format/mount operations require administrator
  policy approval in normal boots.
- Installer RAM-root boots are a special controlled runtime and may copy the
  normal ESP payload to target drive paths such as `1:/`.

## Current limits

- No journaling or crash recovery.
- FAT32 long-file-name behavior is still conservative.
- Devfs only exposes the framebuffer node today.
- Permission bits and ownership are not a real security model yet.
