# Filesystem

LeonOS 4 uses numbered drives and slash-separated paths:

- `0:/`
- `1:/`
- `2:/` through `9:/` when additional media is present

Normal boots mount `0:/` from the GPT ESP FAT32 partition on the AHCI boot
disk. Installer boots mount `0:/` from the `leonos-installer-root` FAT32
ramdisk; the target ESP can be mounted as an optional target drive.

During normal boot the bootstrap storage driver scans every present AHCI port.
ATAPI optical drives with a valid ISO 9660 primary volume descriptor are
mounted read-only in scan order, starting at `1:`. Additional optical media
receive the next free digit drive. The file manager lists each detected optical
drive, and the middlelayer grants ordinary users read/execute access to these
removable volumes. Writes, directory creation, deletion, rename, and ACL
changes return a read-only filesystem error.

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

Optical ISO 9660 mounts are discovered by the kernel's AHCI bootstrap scan and
are not part of the static middlelayer mount-policy entries.

The current devfs surface is deliberately small:

- `0:/dev`
- `0:/dev/fb0`

## Users and Local Data

Multi-user v1 uses these fixed paths:

- `0:/system/state/accounts.db`: middlelayer-owned account database.
- `0:/system/state/license.dat`: local activation record with mode, email hash,
  machine ID, key hash, and local HMAC; plaintext keys are not stored.
- `0:/system/state/oobe.done`: first-run completion marker.
- `0:/system/state/startup.db`: kernel-managed per-user startup application records.
- `0:/system/state/startup-denials.db`: kernel-managed remembered startup permission denials.
- `0:/system/config/leonos.conf`: build and kernel configuration.
- `0:/system/config/display.conf`: default Metro appearance and display policy.
- `0:/system/config/drivers.conf`: loadable-driver policy; all drivers are enabled by default.
- `0:/system/config/fileassoc.cfg`: user-defined launcher file-association overrides.
- `0:/system/config/locale.conf`: default UI language.
- `0:/system/config/services.cfg`: default startup and service policy.
- `0:/drivers`: unsigned Ring 0 `.drv` modules loaded after root mount.
- `0:/docs`: bundled and third-party `.hlp` help containers shown by the
  desktop Documents menu and opened by `oshlp.elf`.
- `0:/var/run/services.state`: service runtime state written by
  `serviced.elf`.
- `0:/var/run/services.cmd`: one-shot service control command file written by
  administrator tools.
- `0:/var/log/services.log`: service runtime log.
- `0:/users/<name>`: user home directory.
- `0:/users/<name>/appearance.conf`: per-user theme, independent Metro/Win95
  color choices, wallpaper path, and wallpaper display mode.
- `0:/users/<name>/desktop`
- `0:/users/<name>/documents`
- `0:/users/<name>/downloads`
- `0:/tmp`: shared temporary write area.

The installed image does not pre-create `accounts.db` or `license.dat`. On
first boot, OOBE requires license activation first when the build's
`LEONOS_LICENSE_REQUIRE` policy is enabled. Online activation posts email, key,
and the machine ID to the license server compiled into the license binaries
through `CONFIG_LICENSE_SERVER_URL`. The machine ID comes from stable platform
identity: SMBIOS System UUID first, then boot GPT disk and ESP partition GUIDs
when SMBIOS UUID is unavailable. Network adapter MAC addresses are not part of
the license machine ID, so adding or removing an e1000 adapter does not change
activation state. Offline activation validates a
50-character key against the local RTC date and only checks the offline key's
validity window at activation time. After the license is valid, or after the
source/build configuration compiles a no-license policy into the binaries, OOBE
creates the first administrator, creates the home layout, writes
`0:/system/state/oobe.done`, and enters the administrator desktop. Later boots enter
`login.elf`. If `oobe.done` exists but desktop cannot find an enabled
administrator, or cannot find a valid license when the compiled policy requires
one, desktop launches OOBE again.

New account creation seeds the user's desktop with `File Manager.lnk`,
`Task Manager.lnk`, `Settings.lnk`, and `Browser.lnk`, pointing to
`0:/system/apps/fileman/fileman.elf`, `0:/system/apps/taskmgr/taskmgr.elf`,
`0:/system/apps/settings/settings.elf`, and `0:/programs/browser/browser.elf`.

`downloadmgr.elf` saves successful downloads to `0:/users/<name>/downloads`
when a user session is active, and falls back to `0:/tmp` when no home directory
is available.

## Help Documents

LeonOS help files use the `.hlp` extension and are plain-text containers with
file metadata plus one or more Markdown document pages. The default system help
file is `0:/docs/leonos.hlp`; `oshlp.elf <file.hlp> [doc.id]` opens a file and
optionally jumps directly to one page.

Each document page can provide `title.en`, `title.zh`, `path.en`, `path.zh`,
`author`, and `version` metadata. `path.*` uses `/` separators to build the
left-side tree in `oshlp.elf`. The desktop Start menu scans only top-level
`0:/docs/*.hlp` files and displays the file-level title when it can be read.

## Supported operations

Current FAT32 and ISO 9660 syscall support includes:

- Directory listing and directory file descriptors.
- File reads; FAT32 also supports writes.
- Create and overwrite through `open` flags on FAT32.
- `stat`, `fstat`, and `lseek` on both filesystem types.
- `mkdir`, `unlink`, `rmdir`, and `rename` on FAT32.

ISO 9660 names are matched case-insensitively and the common `;1` version
suffix is hidden from callers. Rock Ridge/Joliet extensions, multi-extent files,
and media insertion/removal notifications are not currently implemented.

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

- `0:/boot`, `0:/docs`, `0:/system`, and `0:/programs`: System and
  Administrators get full control; Users get read/execute.
- `0:/users/<name>` and descendants: Owner, System, and Administrators get full
  control by default. Other normal users are not granted access.
- `0:/tmp`: Users, System, and Administrators get read/write/execute/delete.
- `0:/system/state/accounts.db` and `LEONACL.SYS` are denied to user tasks; supported
  access goes through auth and ACL APIs. License OOBE writes
  `0:/system/state/license.dat` before normal user login exists.
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
