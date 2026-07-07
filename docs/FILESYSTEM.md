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
- `0:/var/run/services.state`: service runtime state written by
  `serviced.elf`.
- `0:/var/run/services.cmd`: one-shot service control command file written by
  administrator tools.
- `0:/var/log/services.log`: service runtime log.
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

LeonOS v1 stores Windows-style ACL metadata beside FAT32 directory entries.
Each directory may contain a hidden/system `LEONACL.SYS` file. The kernel hides
that file from normal directory enumeration and denies direct user-task access;
middlelayer reads and writes it through trusted kernel file services.

`LEONACL.SYS` is a LeonOS TLV binary file:

- Header: `LACL` magic, version `1`, record count, checksum.
- Record TLV: directory-entry name, owner uid, flags, and up to
  `LEONOS_FS_ACL_MAX_ACE` ACEs.
- ACE principals: Owner, System, Administrators, Users, Everyone.
- ACE permissions: Read/List, Write/Create, Execute/Traverse, Delete, and
  Manage Permissions.

ACL rows only grant allowed permissions; an unchecked permission bit means no
grant. Explicit parent ACL records dynamically affect children; synthetic
built-in defaults are used for missing metadata but are not written until an
object is changed. A child may add explicit ACL entries but v1 does not
implement a "disable inherited permissions" switch.

Default policy:

- `0:/boot`, `0:/system`, `0:/userland`, and `0:/etc`: System and
  Administrators get full control; Users get read/execute.
- `0:/users/<name>` and descendants: Owner, System, and Administrators get full
  control by default. Other normal users are not granted access.
- `0:/tmp`: Users, System, and Administrators get read/write/execute/delete.
- `0:/etc/accounts.db` and `LEONACL.SYS` are denied to user tasks; supported
  access goes through auth and ACL APIs.
- Installer RAM-root boots bypass normal policy so installation/update code can
  copy the ESP payload to target drive paths such as `1:/`.

If an ACL file is missing, middlelayer synthesizes the default ACL. If an ACL
file is corrupt, normal users are denied and administrators can repair it from
File Manager properties.

## Current limits

- No journaling or crash recovery.
- FAT32 long-file-name behavior is still conservative.
- Devfs only exposes the framebuffer node today.
- ACL metadata is LeonOS-specific and is not compatible with external FAT32
  permission tools.
