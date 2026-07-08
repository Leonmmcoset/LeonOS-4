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
Protected service tasks are marked with `LEONOS_AUTHZ_ACTOR_SERVICE` when the
kernel asks middlelayer authorization policy. That lets the policy distinguish
system service runtime writes from ordinary user writes without granting broad
access to unauthenticated tasks.

File authorization operations include `LEONOS_AUTHZ_READ`,
`LEONOS_AUTHZ_WRITE`, `LEONOS_AUTHZ_EXEC`, `LEONOS_AUTHZ_DELETE`, and
`LEONOS_AUTHZ_MANAGE`. Middlelayer handles them through the same `auth_op`
policy callback.

## Filesystem ACL ABI

Filesystem ACL types are defined in `include/leonos/fs.h`. Userland calls:

- `leonos_fs_acl_get`
- `leonos_fs_acl_set`
- `leonos_fs_acl_take_ownership`
- `leonos_fs_acl_repair`

The libc wrappers use these ioctls:

- `LEONOS_FS_IOCTL_ACL_GET`
- `LEONOS_FS_IOCTL_ACL_SET`
- `LEONOS_FS_IOCTL_ACL_TAKE_OWNERSHIP`
- `LEONOS_FS_IOCTL_ACL_REPAIR`

`struct leonos_fs_acl` contains the owner uid and up to
`LEONOS_FS_ACL_MAX_ACE` ACE rows. Supported principals are Owner, System,
Administrators, Users, and Everyone. Supported permission bits are Read/List,
Write/Create, Execute/Traverse, Delete, and Manage Permissions. ACL rows only
grant allowed permissions; an unchecked permission bit means no grant.

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

## Machine Identity ABI

`LEONOS_IOCTL_MACHINE_IDENTITY` returns `struct leonos_machine_identity` from
`include/leonos/system.h`. The kernel fills stable platform identity from SMBIOS
System UUID when available and augments it with boot GPT disk and ESP partition
GUIDs after storage is mounted. License code uses SMBIOS UUID as the primary
machine binding and falls back to the boot GPT GUID pair when firmware does not
provide a valid UUID. Network adapter MAC addresses are deliberately excluded
from the license machine ID so hot-adding or removing e1000 hardware does not
invalidate an existing activation.

## Network ABI

`include/leonos/net.h` exposes the network ioctl ABI. It covers configuration,
DHCP renew, DNS A lookups, ICMP ping, a compatibility fixed-buffer
`leonos_net_http_get` helper, and TCP client sockets.

Runtime DHCP renew mutates the global IPv4 configuration, so
`LEONOS_IOCTL_NET_DHCP` is restricted to administrators and trusted service
tasks. The license OOBE has a narrow pre-login exception: `0:/userland/oobe.elf`
may renew DHCP only while `0:/etc/oobe.done` is absent. Ordinary users can still
read network configuration and use DNS, HTTP, ping, and TCP client socket APIs.

Socket requests use:

- `LEONOS_IOCTL_NET_SOCKET_OPEN`
- `LEONOS_IOCTL_NET_SOCKET_CONNECT`
- `LEONOS_IOCTL_NET_SOCKET_SEND`
- `LEONOS_IOCTL_NET_SOCKET_RECV`
- `LEONOS_IOCTL_NET_SOCKET_CLOSE`
- `LEONOS_IOCTL_NET_CONNECTIONS`

libc wraps those as `leonos_socket_tcp`, `leonos_socket_connect`,
`leonos_socket_send`, `leonos_socket_recv`, `leonos_socket_close`, and
`leonos_net_connections`. A socket is an integer task-owned TCP client handle.
`connect` accepts a host name or IPv4 literal, resolves DNS A records when
needed, and returns the selected remote IP and local port. `send` and `recv` are
synchronous byte-stream operations with per-call timeouts and status fields.
Connection states exported to userland are `SYN_SENT`, `ESTABLISHED`,
`TIME_WAIT`, and `CLOSED`. `leonos_net_connections` is filtered by identity:
administrators and trusted service tasks see all sockets, while normal users
see only sockets owned by their uid.

`include/leonos/http.h` adds a libc HTTP client on top of those sockets:
`leonos_http_get`, `leonos_http_request`, and `leonos_http_resolve_url`.
It sends plain `HTTP/1.1` requests with `Connection: close`, follows bounded
redirects, reports final URL/status/content type, copies response headers,
decodes chunked transfer bodies, and returns truncation flags for callers with
small buffers. `httpget.elf` and `browser.elf` use this library for `http://`
traffic; lower-level tools such as `ping.elf` and `netctl.elf` continue to use
ICMP/DHCP/DNS/socket status APIs directly. TCP server/listener sockets, UDP
sockets, TLS/HTTPS, cookies, cache, and full TCP window management are still out
of scope for this ABI version.

## Application Services

The launcher library in `leonos/launch.h` owns user-facing file launch policy.
It supports `.lnk` shortcuts, built-in program aliases, and persistent extension
associations stored in `0:/etc/fileassoc.cfg`. Settings can edit the common
associations for `.txt`, `.md`, `.html`, `.htm`, `.bmp`, and `.hlp`.
The default `.hlp` handler is `0:/userland/oshlp.elf`; it accepts
`oshlp.elf <file.hlp> [doc.id]` and opens a Markdown page inside a LeonOS help
container.

Current companion applications:

- `downloadmgr.elf`: uses the libc HTTP client and saves `http://` downloads to
  the current user's `0:/users/<name>/downloads` directory.
- `imageview.elf`: opens uncompressed 24/32-bit BMP files, supports Fit/1x/2x
  zoom, and can move to previous/next BMP siblings in the same directory.
- `oshlp.elf`: opens LeonOS `.hlp` help containers from `0:/docs` or any path
  passed by another app. The help viewer uses the current system language as its
  default but language changes inside the window are local to that process.
- `serviced.elf`: protected background service runtime. Desktop starts it once
  after the window server is ready. It writes `0:/var/run/services.state`,
  consumes `0:/var/run/services.cmd`, logs to `0:/var/log/services.log`, and
  keeps retrying DHCP while the static fallback is active.
- `servicemgr.elf`: edits `0:/etc/services.cfg`, reads the runtime state file,
  and queues administrator start/stop/restart commands through
  `0:/var/run/services.cmd`.

The current service keys are `desktop`, `dhcp`, `network_icon`, `rtc_clock`,
and `ntp_sync`. `desktop` is fixed on. `dhcp` controls whether kernel boot
network initialization attempts DHCP before keeping the static fallback and is
also supervised by `serviced.elf` after the desktop starts. `network_icon` and
`rtc_clock` are read by the desktop taskbar. `ntp_sync` is wired into service
state reporting but remains failed/reserved until a kernel set-time ABI exists.
