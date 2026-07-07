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

`include/leonos/net.h` exposes the network ioctl ABI. It covers configuration,
DHCP renew, DNS A lookups, ICMP ping, a compatibility fixed-buffer
`leonos_net_http_get` helper, and TCP client sockets.

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
`TIME_WAIT`, and `CLOSED`.

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
associations for `.txt`, `.md`, `.html`, `.htm`, and `.bmp`.

Current companion applications:

- `downloadmgr.elf`: uses the libc HTTP client and saves `http://` downloads to
  the current user's `0:/users/<name>/downloads` directory.
- `imageview.elf`: opens uncompressed 24/32-bit BMP files, supports Fit/1x/2x
  zoom, and can move to previous/next BMP siblings in the same directory.
- `servicemgr.elf`: edits `0:/etc/services.cfg`, the v1 startup/service policy
  file used by service-aware components.

The current service keys are `desktop`, `dhcp`, `network_icon`, `rtc_clock`,
and `ntp_sync`. `desktop` is fixed on. `dhcp` controls whether kernel boot
network initialization attempts DHCP before keeping the static fallback.
`network_icon` and `rtc_clock` are read by the desktop taskbar. `ntp_sync` is a
reserved switch for a future time-sync service.
