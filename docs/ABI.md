# LeonOS 4 ABI

## Linux x86_64 syscall convention

- `rax`: syscall number.
- `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`: arguments 0 through 5.
- `rax`: return value.
- Negative return values are `-errno`.

Unimplemented syscalls return `-ENOSYS`.

## GPU Rendering ABI

`include/leonos/gpu.h` defines the versioned, process-owned offscreen rendering
API used by glxgears. `leonos_gpu_create`, `leonos_gpu_render` and
`leonos_gpu_destroy` manage bounded SVGA3D triangle batches; `leonos_gpu_info`
reports capabilities, resource counters and estimated busy time.
`leonos_gpu_diagnostics` retrieves the latest device render-failure
snapshot only for its owning process, without advancing the hardware FIFO.
Existing rendering request layouts and the CPU/memory performance structure
are unchanged. See [SVGA3D.md](SVGA3D.md)
for layout semantics, limits, synchronization, software fallback and validation.

## Dynamic Library ABI

Dynamic PIE executables use `/system/lib/ld-leonos.elf` and must contain one
`DT_NEEDED=libleonos.so.1` entry. They may additionally require ABI-v1 shared
libraries. The loader resolves an unqualified library name from the requesting
module directory and then `/system/lib`, validates each LeonOS ABI note, and
loads recursive dependencies before relocating the main executable.

The system libraries currently include `libleonos.so.1`, `libmagic.so.1`, and
`liblua.so.5`. Static `ET_EXEC` binaries remain supported for recovery tools
and SDK builds made with `STATIC=1`.

## Syscall subset

LeonOS keeps Linux-compatible syscall numbers for the current user ABI:

- File I/O: `read`, `write`, `open`, `close`, `stat`, `fstat`, `lseek`
- Filesystem mutation: `mkdir`, `unlink`, `rmdir`, `rename`
- Process and scheduler: `execve`, `wait4`, `exit`, `getpid`, `sched_yield`,
  `nanosleep`
- Working directory: `getcwd`, `chdir`
- Memory: `mmap`, `munmap`
- Device and system extensions: `ioctl`

The libc wrappers live in `userland/libc/include/leonos/syscall.h`,
`userland/libc/src/libc.c`, and the POSIX-facing files beside it. `mmap`
supports anonymous private mappings and private file mappings; `munmap`
supports whole or partial unmapping.

## Shared POSIX porting surface

`libleonos.so.1` contains the shared ANSI curses subset in
`userland/libc/src/ansi_curses.c`. Applications include either `<curses.h>` or
`<ncurses.h>` from the SDK and link only the normal runtime; Nano and `sl` use
the same implementation. It provides windows, cursor movement, buffered ANSI
output, terminal-size refresh, raw/noecho mode, and the key/input functions
used by those ports. It is a small LeonOS terminal API, not a promise of
binary compatibility with host ncurses.

The runtime also exports `usleep()`. The SDK Makefile enables
`_DEFAULT_SOURCE`, so Picolibc exposes its standard declaration without an
application-local `unistd.h` shim. `signal()` and `sigaction()` support the
default and ignore dispositions; arbitrary user callbacks remain unsupported
until the kernel has a signal-frame ABI.

`userland/libc/src/posix_process.c` is the single POSIX process and descriptor
adapter used by both dynamic applications and static ports. It implements
`fork`, `vfork`, `execve`, `wait4`, `waitpid`, `pipe`, `dup`, `dup2`, `fcntl`,
process IDs/groups, foreground PTY groups, `kill`, nice priorities, and resource
limits. The wrappers convert raw negative errno values to `-1` with `errno`.
`waitpid(..., WNOHANG)` returns `0` if the child has no state change; blocking
waits yield while the scheduler reports its temporary `EAGAIN`. `vfork` is
intentionally COW-fork equivalent until LeonOS has a parent-suspending vfork
ABI. `nice()` and `getpriority()` return the normal `-20..19` priority range.

The kernel applies default terminal signal actions to the foreground process
group. `signal()` and `sigaction()` support `SIG_DFL` and `SIG_IGN`, including
`SIGHUP` immunity for detached jobs; `raise()` uses the normal `kill()` path.
`sigprocmask` remains unavailable and `sigsuspend` yields then returns `EINTR`.
Applications must not rely on arbitrary user-space signal handlers yet.

The runtime also owns the common POSIX file-port adapters: `stat`, `fstat`,
and `lstat` are available as the explicit `leonos_posix_*` adapters in
`<leonos/posix.h>`; `access`, `fcntl`, `opendir`, `readdir`, `closedir`,
`dirfd`, and `rewinddir` are supplied through Picolibc's normal POSIX headers.
Directory entries expose LeonOS's file, directory, and device kinds.  They do
not yet provide filesystem-native inode or ownership metadata.

See [Syscalls](SYSCALLS.md) for the detailed syscall table, ioctl groups, and
current limitations.

## Time Synchronization ABI

`LEONOS_IOCTL_TIME_NTP_SYNC` accepts a `struct leonos_time_sync` from
`include/leonos/system.h`. The request may leave `server` empty to use
`pool.ntp.org`; the result returns the selected server, resolved IPv4 address,
network status, validated Unix seconds, and `valid=1` only after the kernel
updates its software wall clock. The ioctl is limited to trusted background
service tasks, so ordinary applications cannot change system time.

## Device model

The runtime exposes a synthetic devfs namespace. Common nodes are
`/dev/null`, `/dev/zero`, `/dev/full`, `/dev/random`, `/dev/urandom`,
`/dev/tty`, `/dev/console`, `/dev/fb0`, `/dev/audio0`, `/dev/serial0`,
`/dev/ttyS0`, `/dev/net0`, `/dev/ethernet0`, `/dev/disk0`, `/dev/sda`,
`/dev/vda`, `/dev/nvme0n1`, `/dev/rtc`, `/dev/kmsg`, and `/dev/input/event0`.
`/dev/stdin`, `/dev/stdout`, and `/dev/stderr` alias the current process
streams. `/dev/input` and `/dev/pts` are directories and are enumerated
through normal directory syscalls. Device nodes are synthetic and are not
stored in the filesystem image.

Framebuffer, audio, input, network, disk, PTY, and GUI libc helpers open their
corresponding `/dev` node and issue the existing ioctl ABI. Calls made by old
applications with the historical descriptor 3 are translated to the matching
node by libc, so existing binaries continue to work while using the devfs
namespace. System applications query the complete hardware inventory through
`LEONOS_IOCTL_DEVICE_LIST`.

## Driver Module ABI

Loadable Ring 0 driver modules use the public definitions in
`include/leonos/driver.h`. `LEONOS_IOCTL_DRIVER_LIST` exposes the discovered
module filename, ABI version, state, and diagnostic text to all user sessions.
`LEONOS_IOCTL_DRIVER_CONTROL` accepts load, unload, forced-unload, rescan, and
boot-enable actions, but the kernel permits it only for administrator tasks.

The kernel loads unsigned ELF64 `ET_REL` files from `/drivers` after the
root filesystem is mounted. The complete binary format, restricted kernel API,
and persistent `/system/config/drivers.conf` policy are documented in
[Drivers](DRIVERS.md).

## Kernel Debug Module ABI

`/system/kerneldebug.sys` is a built-in-only x86_64 little-endian `ET_REL`
module. It must contain a `.note.leonos.kerneldebug` ELF note owned by
`LEONKDBG`, type `0x4c4b4447`, ABI `1`, and the fixed entry-name hash. The
loader accepts only PIC-free kernel sections and the `NONE`, `64`, `32`,
`32S`, `PC32`, and `PLT32` relocations; dynamic segments, TLS, IFUNC,
undefined symbols, W+X sections, and unknown sections are rejected.

The module receives the fixed `leonos_kernel_debug_api` table declared in
`kernel/ntclks/include/ntclks/kernel_debug.h`. It provides ostui output/input,
TSC timing, controlled syscall/ioctl benchmark callbacks, and explicit
continue, reboot, and shutdown operations. A valid module owns the diagnostic
session; the kernel's minimal menu is only a recovery path for a missing or
rejected module.

The one-shot marker is `/boot/system/state/kerneldebug.next`. The loader consumes
and deletes it before validating its contents, preventing repeated entry after
an interrupted or malformed debug boot. The persistent activation flag is
`/system/state/kerneldebug.enabled`.

## Appearance ABI

The runtime UI appearance is a Desktop-owned state. `Metro` is the default
(`LEONOS_UI_THEME_METRO`); `LEONOS_UI_THEME_WIN95` restores the legacy Win95
palette and bevelled controls. The global `/system/config/display.conf`
`theme=` key remains the boot/default style used before a user session is
available, including early framebuffer output and bugcheck rendering.

Per-user personalization is saved separately in
`/users/<name>/appearance.conf`. `struct leonos_appearance_state` and
`struct leonos_appearance_request` carry the active theme, independent Metro
and Win95 basic color scheme IDs, a wallpaper display mode, and a wallpaper BMP
path. Wallpaper BMP decoding is bounded to 1280 x 720 and accepts
uncompressed 24-bit or 32-bit BMP files.

The independent color scheme IDs are `Blue`, `Teal`, `Green`, `Purple`,
`Red`, `Graphite`, and `Kawaii Pink`. The `Kawaii Pink` scheme is available
for both Metro and Win95; each theme keeps its own palette and persisted
`metro.color` / `win95.color` value. Its configuration value remains `pink`
for compatibility.

`LEONOS_GUI_IOCTL_APPEARANCE_STATE` reads the current Desktop-published state.
Logged-in user tasks may submit `LEONOS_GUI_IOCTL_APPEARANCE_REQUEST`; the
window server polls and publishes the updated state through the paired
appearance ioctls, writes the current user's `appearance.conf`, reloads the
wallpaper, then sends `LEONOS_GUI_APP_EVENT_THEME_CHANGED` to active
application windows. The event carries the theme in `x`, the Metro color
scheme in `y`, and the Win95 color scheme in `dx`.

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

## Disk Management ABI

`include/leonos/fs.h` also defines the GPT disk-management ABI used by
`diskmgr.elf`. A caller first uses `leonos_install_list_disks`, then requests
the selected disk's entries with `leonos_disk_list_partitions`. Each
`struct leonos_disk_partition` carries its zero-based GPT entry index, LBA
range, decoded filesystem, GPT type GUID, display name, protection flags, and
the absolute `mount_path` when `LEONOS_DISK_PARTITION_FLAG_MOUNTED` is set.
Unmounted entries have an empty `mount_path`.

The associated ioctls are:

- `LEONOS_DISK_IOCTL_LIST_PARTITIONS`
- `LEONOS_DISK_IOCTL_FORMAT_PARTITION`
- `LEONOS_DISK_IOCTL_DELETE_PARTITION`
- `LEONOS_DISK_IOCTL_CREATE_PARTITION`
- `LEONOS_DISK_IOCTL_MOUNT_PARTITION`
- `LEONOS_DISK_IOCTL_UNMOUNT_PARTITION`
- `LEONOS_DISK_IOCTL_EDIT_PARTITION`
- `LEONOS_DISK_IOCTL_INITIALIZE_GPT`

`FORMAT_PARTITION` accepts FAT32, exFAT, and ext2 through
`struct leonos_disk_partition_format`. `CREATE_PARTITION` allocates a
1 MiB-aligned range with the requested size in MiB and formats it immediately;
FAT32 and exFAT use the GPT Microsoft Basic Data type; ext2 uses the Linux
filesystem type. `DELETE_PARTITION` removes only the GPT entry and deliberately does not
claim to securely erase the old data area.

`MOUNT_PARTITION` accepts a writable `struct leonos_disk_partition_mount`. An
empty `mount_path` selects the normalized `/mnt/disk<N>p<M>` path; callers may
instead provide an absolute runtime target such as `/mnt/data`. The mount is
not persistent across reboot. `UNMOUNT_PARTITION` takes a
`struct leonos_disk_partition_unmount` and returns busy when a live task still
uses the mounted volume through a CWD, descriptor, image, or file mapping.

`EDIT_PARTITION` accepts `struct leonos_disk_partition_edit` and updates the
standard GPT type (`basic-data`, `ESP`, or `Linux filesystem`) and/or its
printable UTF-16 name. Both primary and backup GPT copies are rewritten with
fresh CRCs; protected or mounted partitions are rejected.

`INITIALIZE_GPT` accepts `struct leonos_disk_gpt_initialize` and writes a
protective MBR, an empty standard 128-entry GPT, and matching primary and
backup headers. It is destructive and is exposed only through the installer
ISO's `gptinit` utility. A valid existing GPT is rejected unless the utility is
run with `--force`; the operation never creates or formats partitions.

Listing is available to disk-management clients, while create, format, delete,
mount, and unmount are checked through the administrator install authorization
path in the kernel. The running boot disk and an installer target that is
currently mounted are exported as protected and their partitions are rejected
by the kernel even if a client constructs an ioctl request directly. The
initial ABI accepts the standard 128-entry, 128-byte GPT table emitted by
LeonOS; malformed, out of range, overlapping, or CRC-invalid tables are never
mutated.

## Middlelayer ABI v6

The loader starts `kernel.sys` and `middlelayer.sys`. The middlelayer module
returns a `struct leonos_middlelayer_api` with version
`LEONOS_MIDDLELAYER_API_VERSION` set to `6`.

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
scheduling, user pointer validation, storage block I/O, and exFAT/FAT32/ext2 mutation.
Middlelayer owns higher-level policy or semantic services that can run on top
of those kernel facts.

The file services are trusted kernel-to-middlelayer calls. They are used by the
auth service to own `/system/state/accounts.db` and to create or repair
`/users/<name>` home directories without exposing direct account-database
access to ordinary user tasks.

## VFS path service

`LEONOS_VFS_OP_RESOLVE_PATH` accepts `struct leonos_vfs_resolve_path`:

- `cwd`: current directory, for relative inputs.
- `input`: raw path from kernel or userland.
- `out` and `capacity`: normalized output buffer.
- `node_kind`: coarse directory/file/device classification.

The service resolves `cwd + input` into a normalized Unix path such as
`/system/apps/desktop/desktop.elf`. It rejects any input containing `:`.
Kernel storage code calls this first and keeps a C fallback resolver for
bootstrapping.

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
- IDE/PATA controller
- NVMe controller
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
tasks. The license OOBE has a narrow pre-login exception: `/system/apps/oobe/oobe.elf`
may renew DHCP only while `/system/state/oobe.done` is absent. Ordinary users can still
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
It follows bounded redirects, reports final URL/status/content type, copies
response headers, decodes chunked transfer bodies, and returns truncation flags
for callers with small buffers. `httpget.elf`, `browser.elf`, and
`downloadmgr.elf` use this library for `http://` and `https://` traffic;
lower-level tools such as `ping.elf` and `netctl.elf` continue to use
ICMP/DHCP/DNS/socket status APIs directly. HTTPS uses a TLS 1.2 Mbed TLS client
profile, a bundled CA store, hostname validation, and a valid system clock. TCP
server/listener sockets, UDP sockets, cookies, cache, and full TCP window
management are still out of scope for this ABI version.

## Audio ABI

`include/leonos/audio.h` exposes a bounded, non-blocking PCM submission
interface backed by autoloaded audio driver modules. `LEONOS_IOCTL_AUDIO_CONFIGURE`
selects the stream format, `LEONOS_IOCTL_AUDIO_WRITE` submits at most 64 KiB per
call and may return a short write when the device cannot accept more data, and
`LEONOS_IOCTL_AUDIO_GET_STATE` returns device and stream state. The initial
`ac97.drv` supports QEMU's Intel ICH AC'97 controller, while `es1371.drv`
supports VMware's Ensoniq AudioPCI ES1371 controller. Both accept 16-bit,
stereo PCM at 8000–48000 Hz. The AC'97 backend keeps a persistent DMA ring and
reports short writes with `LEONOS_AUDIO_STATUS_WOULD_BLOCK`; callers should
retain and retry the unwritten tail. `doom.elf` and the built-in `wavplay.elf`
test tone use native 48000 Hz output to avoid emulator-side resampling.
`wavplay.elf` also opens matching PCM WAV files and is the default `.wav`
handler.

## Application Services

## PortableGL Rendering ABI

`/system/lib/libportablegl.so.1` provides the PortableGL 0.101 API with the
LeonOS ABI-v1 window wrapper declared by `leonos/pgl.h`. The wrapper manages a
GUI window, an ABGR32 color buffer and a D24S8 depth/stencil buffer, and submits
frames through `leonos_gui_present_window`. Contexts are single-process and
single-current; callers must handle resize events through
`leonos_pgl_process_event` before drawing the next frame. The SDK includes the
matching `portablegl.h`, `leonos/pgl.h`, shared library and static archive. The
system build limits one draw call to 50,000 output vertices, leaving the
renderer usable within the current user address-space budget.

The launcher library in `leonos/launch.h` owns user-facing file launch policy.
It supports `.lnk` shortcuts, built-in program aliases, and persistent extension
associations stored in `/system/config/fileassoc.cfg`. Settings can edit the common
associations for `.txt`, `.md`, `.html`, `.htm`, `.bmp`, `.wav`, and `.hlp`.
The default `.hlp` handler is `/programs/oshlp/oshlp.elf`; it accepts
`oshlp.elf <file.hlp> [doc.id]` and opens a Markdown page inside a LeonOS help
container.

Current companion applications:

- `downloadmgr.elf`: uses the libc HTTP client and saves HTTP/HTTPS downloads to
  the current user's `/users/<name>/downloads` directory.
- `imageview.elf`: opens uncompressed 24/32-bit BMP/DIB files and PNG files,
  supports Fit/1x/2x zoom, and can move to previous/next supported images in the
  same directory.
- `wavplay.elf`: plays 16-bit stereo PCM WAV files through the active audio
  driver, or a built-in test melody when started without a file.
- `oshlp.elf`: opens LeonOS `.hlp` help containers from `/docs` or any path
  passed by another app. The help viewer uses the current system language as its
  default but language changes inside the window are local to that process.
- `serviced.elf`: protected background service runtime. Desktop starts it once
  after the window server is ready. It writes `/var/run/services.state`,
  consumes `/var/run/services.cmd`, logs to `/var/log/services.log`, and
  keeps retrying DHCP while the static fallback is active.
- `servicemgr.elf`: edits `/system/config/services.cfg`, reads the runtime state file,
  and queues administrator start/stop/restart commands through
  `/var/run/services.cmd`.

The current service keys are `desktop`, `dhcp`, `network_icon`, `rtc_clock`,
and `ntp_sync`. `desktop` is fixed on. `dhcp` controls whether kernel boot
network initialization attempts DHCP before keeping the static fallback and is
also supervised by `serviced.elf` after the desktop starts. `network_icon` and
`rtc_clock` are read by the desktop taskbar. `ntp_sync` asks the protected
`serviced.elf` task to resolve `pool.ntp.org`, send an NTP UDP request, and set
the kernel software wall clock after validating the server reply. It retries
failed synchronization after five minutes and refreshes a successful sync every
six hours. This updates the runtime clock only; it does not write the RTC/CMOS.
