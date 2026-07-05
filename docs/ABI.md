# LeonOS 4 ABI

## Linux x86_64 syscall convention

- `rax`: syscall number.
- `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`: arguments 0 through 5.
- `rax`: return value.
- Negative return values are `-errno`.

Unimplemented syscalls return `-ENOSYS`.

## Syscall subset

LeonOS keeps Linux-compatible syscall numbers for the current user ABI:

- File I/O: `read`, `write`, `open`, `close`, `stat`, `fstat`, `lseek`
- Filesystem mutation: `mkdir`, `unlink`, `rmdir`, `rename`
- Process and scheduler: `execve`, `wait4`, `exit`, `getpid`, `sched_yield`,
  `nanosleep`
- Working directory: `getcwd`, `chdir`
- Memory: `mmap`, `munmap`
- Device and system extensions: `ioctl`

The libc wrappers live in `userland/libc/include/leonos/syscall.h` and
`userland/libc/src/libc.c`. `mmap` supports anonymous private mappings and
private file mappings; `munmap` supports whole or partial unmapping.

See [Syscalls](SYSCALLS.md) for the detailed syscall table, ioctl groups, and
current limitations.

## Device model

The current devfs surface exposes the framebuffer as:

- `0:/dev/fb0`

Keyboard and mouse state are owned by the kernel input path and delivered to
Ring-3 through GUI/window event ioctls rather than stable devfs file nodes.
Client applications talk to the window server through osmlayer IPC and GUI
ioctls. System applications query device inventory through
`LEONOS_IOCTL_DEVICE_LIST`.

## Authentication ABI

Multi-user state is exposed through `include/leonos/auth.h` and libc wrappers
in `userland/libc/src/libc.c`.

User-facing roles are:

- `LEONOS_AUTH_ROLE_ADMIN`
- `LEONOS_AUTH_ROLE_USER`

The account database supports up to `LEONOS_AUTH_MAX_USERS` accounts. Usernames
are lowercase letters, digits, and `_`; passwords must be non-empty. The current
v1 password verifier is salted SHA-256 and is meant for this OS stage, not as a
complete modern password-storage design.

Authentication requests use `ioctl` IDs:

- `LEONOS_AUTH_IOCTL_STATUS`
- `LEONOS_AUTH_IOCTL_CURRENT`
- `LEONOS_AUTH_IOCTL_LIST_USERS`
- `LEONOS_AUTH_IOCTL_LOGIN`
- `LEONOS_AUTH_IOCTL_LOGOUT`
- `LEONOS_AUTH_IOCTL_CREATE_USER`
- `LEONOS_AUTH_IOCTL_UPDATE_USER`
- `LEONOS_AUTH_IOCTL_CHANGE_PASSWORD`

Task snapshots now include `uid`, `role`, `session_id`, and `username`.
Children inherit identity and current directory from the parent task.

## Middlelayer ABI v5

The loader starts `kernel.sys` and `middlelayer.sys`. The middlelayer module
returns a `struct leonos_middlelayer_api` with version
`LEONOS_MIDDLELAYER_API_VERSION` set to `5`.

Current API callbacks:

- `init`: middlelayer initialization.
- `syscall`: reserved middlelayer syscall hook.
- `selftest`: boot-time service self-test.
- `mount_policy`: describes runtime mounts for normal and installer boots.
- `unicode_op`: UTF-8/UTF-16 conversion and UTF-8 text layout helpers.
- `vfs_op`: path normalization service.
- `device_catalog`: raw-device-to-user-device catalog service.
- `auth_op`: account, login, password, and authorization policy service.

The kernel service table passed to middlelayer is intentionally small:

- `log`
- `log_len`
- `read_file`
- `write_file`
- `mkdir`

Kernel code owns hardware probing, interrupts, page tables, physical memory,
scheduling, user pointer validation, storage block I/O, and FAT32 mutation.
Middlelayer owns higher-level policy or semantic services that can run on top
of those kernel facts.

The file services are trusted kernel-to-middlelayer calls. They are used by the
auth service to own `0:/etc/accounts.db` and to create or repair
`0:/users/<name>` home directories without exposing direct account-database
access to ordinary user tasks.

## VFS path service

`LEONOS_VFS_OP_RESOLVE_PATH` accepts `struct leonos_vfs_resolve_path`:

- `cwd`: current directory, for relative inputs.
- `input`: raw path from kernel or userland.
- `out` and `capacity`: normalized output buffer.
- `drive`: resolved numbered drive.
- `node_kind`: coarse directory/file/device classification.

The service resolves `cwd + input` into a normalized numbered-drive path such as
`0:/userland/desktop.elf`. Kernel storage code calls this first and keeps a C
fallback resolver for bootstrapping and compatibility.

## Device catalog service

Kernel drivers gather raw facts in `struct leonos_raw_device_info` and pass
them to middlelayer through `struct leonos_device_catalog_query`. Middlelayer
formats those facts into `struct leonos_device_info` records for userland.

Current raw device kinds:

- RTC
- PS/2 keyboard
- PS/2 mouse
- Framebuffer
- AHCI controller
- Disk
- Serial COM1
- Intel e1000 network adapter

The e1000 entry is classified as `LEONOS_DEVICE_CLASS_NETWORK`. Its detail text
reports whether the active IPv4 configuration came from DHCP or the static
fallback, and its raw values carry the MAC address, local IPv4 address, and
gateway for device-manager style tools.

## Network ABI

`include/leonos/net.h` exposes the current minimal network ioctl ABI. It covers
configuration, DHCP renew, DNS A lookups, ICMP ping, and a fixed-buffer
`leonos_net_http_get` helper that performs an active TCP connection and
`HTTP/1.0` GET. `httpget.elf` and `browser.elf` both use this helper. This is
not a socket ABI yet: applications cannot listen for TCP connections, stream
arbitrary byte ranges, or use TLS/HTTPS through the kernel network interface.
