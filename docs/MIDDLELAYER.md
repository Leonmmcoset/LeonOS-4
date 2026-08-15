# Middlelayer

The middlelayer is loaded as `middlelayer.sys` beside `kernel.sys`. It exposes a
small ABI to the kernel through `struct leonos_middlelayer_api`.

## Current responsibilities

Middlelayer currently owns:

- Mount policy for normal and installer boots.
- Unicode services for UTF-8 layout and UTF-8/UTF-16LE conversion.
- VFS path normalization through `LEONOS_VFS_OP_RESOLVE_PATH`.
- Device catalog formatting from raw kernel device facts.
- Local account storage, salted SHA-256 password checks, session/user policy,
  and path/task/install authorization decisions.
- ACL policy through hidden `LEONACL.SYS` metadata files on FAT32 and ext2, including
  default ACL synthesis, corrupt-ACL handling, and owner/role checks.
- Service-runtime authorization for protected service tasks writing
  `0:/var` state/log files and `0:/system/config/services.cfg`.
- Boot-time self-test coverage for multi-filesystem mount policy, IPC, GUI, VFS, and
  device catalog services.

The boot log should report:

```text
[osmlayer] selftest passed=5/5 (vfs storage ipc gui device)
```

## Kernel responsibilities

The kernel still owns the low-level and privileged work:

- AHCI probing and block I/O.
- FAT32 and ext2 directory and file mutation.
- Physical page allocation, VMA tracking, page tables, and user mappings.
- Scheduler, tasks, and syscall dispatch.
- Framebuffer, PS/2, RTC, disk, and serial facts.
- Intel e1000 probing plus the v1 polling ARP/IPv4/ICMP, UDP DHCP/DNS, TCP
  active-open, and HTTP GET stack.
- Persisting task `uid`, `role`, `session_id`, `username`, and `home` after a
  successful login, plus hard syscall/ioctl denial for unauthorized operations.
- User pointer validation and copying.

Middlelayer services should not directly trust user pointers or touch hardware.
The kernel validates user input and passes bounded service structs.

## ABI files

Shared ABI:

- `include/leonos/boot_handoff.h`

Kernel bridge:

- `kernel/ntclks/osmlayer_bridge.c`

Middlelayer implementation:

- `middlelayer/osmlayer/src/lib.rs`
- `middlelayer/osmlayer/src/vfs.rs`
- `middlelayer/osmlayer/src/unicode.rs`
- `middlelayer/osmlayer/src/device.rs`
- `middlelayer/osmlayer/runtime.c`

Some service implementation lives in the C runtime because this avoids pulling
in Rust `no_std` bounds-check panic dependencies for small string/path catalog
helpers.

## Account and Policy Service

Middlelayer owns `0:/system/state/accounts.db`. The v1 serialized line format is:

```text
uid|role|flags|username|salt_hex|hash_hex
```

The database is accessed through trusted kernel services:

- `read_file`
- `write_file`
- `mkdir`

User tasks cannot open `0:/system/state/accounts.db` directly. Auth operations go through
`auth_op`, and the kernel passes bounded structs after validating user pointers.

## Filesystem ACL Service

Middlelayer owns the policy for hidden `LEONACL.SYS` files. The kernel owns the
filesystem mutations and blocks direct user-task access, while middlelayer parses and
writes the TLV metadata through trusted `read_file` and `write_file` services.

The ACL service supports:

- Get/set ACL for a path.
- Administrator take-ownership.
- Administrator repair for corrupt ACL metadata.
- Best-effort create/delete/rename notifications so directory ACL records track
  file operations.

When metadata is missing, middlelayer synthesizes defaults for system trees
including `0:/docs`, user homes, and `0:/tmp`. When metadata is corrupt,
ordinary access is denied and administrators can repair the object from File
Manager.

Middlelayer also creates and repairs:

- `0:/users`
- `0:/users/<name>`
- `0:/users/<name>/desktop`
- `0:/users/<name>/documents`
- `0:/users/<name>/downloads`
- `0:/tmp`

On new account creation, middlelayer also seeds `0:/users/<name>/desktop`
with launcher shortcuts for File Manager, Task Manager, Settings, and Browser.

First account creation is only allowed when no enabled administrator exists,
and that first account must be an administrator. Later account creation,
enable/disable, role switching, and admin password resets require an
administrator. The policy refuses to disable or demote the current
administrator and refuses to remove the last enabled administrator.

Protected system services are marked by the kernel with
`LEONOS_AUTHZ_ACTOR_SERVICE` during authorization checks. Middlelayer uses that
flag to allow `serviced.elf` to create and update
`0:/var/run/services.state`, `0:/var/run/services.cmd`,
`0:/var/log/services.log`, and `0:/system/config/services.cfg` without making those
writes available to ordinary unauthenticated tasks. `0:/var` remains readable
so user tools can show runtime service state.

## ABI v5 callbacks

`LEONOS_MIDDLELAYER_API_VERSION` is currently `5`. The required callback table
fields are:

- `init`
- `syscall`
- `selftest`
- `mount_policy`
- `unicode_op`
- `vfs_op`
- `device_catalog`
- `auth_op`

The kernel bridge rejects a middlelayer module that does not provide the full
v5 table.
