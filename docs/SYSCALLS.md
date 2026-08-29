# Syscalls

LeonOS 4 exposes a small Linux-numbered x86_64 syscall ABI to Ring-3
applications. The ABI is intentionally close to Linux where it is useful, but
only the calls listed here are implemented.

## Entry Convention

Userland enters the kernel with `int $0x80`. The libc assembly helpers in
`userland/libc/src/syscall.S` translate C call arguments into the syscall ABI:

- `rax`: syscall number.
- `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`: arguments 0 through 5.
- `rax`: return value.

Return values follow the kernel convention:

- `>= 0`: success or byte/count/id result.
- `< 0`: negative errno value.
- Unknown syscall numbers return `-ENOSYS`.

The public userland numbers and wrappers are in:

- `userland/libc/include/leonos/syscall.h`
- `userland/libc/src/libc.c`

The kernel-side numbers and errno constants are in:

- `kernel/ntclks/include/ntclks/syscall.h`

## Implemented Syscall Table

| Number | Name | libc wrapper | Notes |
| ---: | --- | --- | --- |
| 0 | `read` | `read` | Reads files, directories, stdin PTY input, and directory entries. |
| 1 | `write` | `write` | Writes files, stdout/stderr console output, and PTY output. |
| 2 | `open` | `open` | Opens files, directories, and `/dev/fb0`; supports LeonOS open flags. |
| 3 | `close` | `close` | Closes task file descriptors above the stdio/device range. |
| 4 | `stat` | `stat` | Stats a path into `struct leonos_stat`. |
| 5 | `fstat` | `fstat` | Stats an open file descriptor. |
| 8 | `lseek` | `lseek` | Supports files and directory cursors. |
| 9 | `mmap` | `mmap` | Supports private anonymous mappings and private read-only file mappings. |
| 11 | `munmap` | `munmap` | Supports whole and partial unmapping of existing VMAs. |
| 16 | `ioctl` | `ioctl` | Multiplexes GUI, filesystem, system, installer, text, device, and PTY APIs. |
| 24 | `sched_yield` | `sched_yield` | Yields the current task if another task can run. |
| 35 | `nanosleep` | `sleep_ms` | libc passes milliseconds; kernel also accepts a Linux-like timespec pointer. |
| 39 | `getpid` | `getpid` | Returns the current scheduler PID. |
| 57 | `fork` | `fork` | Creates a copy-on-write child; the child receives zero. |
| 58 | `vfork` | `vfork` | Currently has the same copy-on-write behavior as `fork`. |
| 59 | `execve` | `execve` | Replaces the current process image with an ELF program. |
| 60 | `exit` | `exit` | Releases process-owned files, windows, PTYs, and exits with a code. |
| 61 | `wait4` | `wait4` | Waits for a child and writes a Linux-style shifted status. |
| 79 | `getcwd` | `getcwd` | Copies the task current directory. |
| 80 | `chdir` | `chdir` | Changes the task current directory after path lookup. |
| 82 | `rename` | `rename` | Renames exFAT, FAT32, or ext2 files/directories within one filesystem. |
| 83 | `mkdir` | `mkdir` | Creates an exFAT, FAT32, or ext2 directory. |
| 84 | `rmdir` | `rmdir` | Removes an empty exFAT, FAT32, or ext2 directory. |
| 87 | `unlink` | `unlink` | Removes an exFAT, FAT32, or ext2 file. |

## File and Directory Calls

Paths use Unix syntax such as `/system/apps/desktop/desktop.elf`. Relative
paths are resolved against the task current directory through the middlelayer
VFS resolver when available, with a kernel fallback. Inputs containing `:`
are rejected.

Open flags are defined in `include/leonos/fs.h`:

- `LEONOS_O_RDONLY`
- `LEONOS_O_WRONLY`
- `LEONOS_O_RDWR`
- `LEONOS_O_CREAT`
- `LEONOS_O_TRUNC`
- `LEONOS_O_APPEND`

Seek modes are:

- `LEONOS_SEEK_SET`
- `LEONOS_SEEK_CUR`
- `LEONOS_SEEK_END`

Directory reads return one `struct leonos_dir_entry` per successful `read`.
The higher-level `LEONOS_IOCTL_LIST_DIR` ioctl can also list a directory into a
caller-provided array.

## Memory Calls

`mmap(addr, len, prot, flags, fd, offset)` records a task VMA and maps pages
according to the mapping type.

Supported protection bits:

- `LEONOS_PROT_READ`
- `LEONOS_PROT_WRITE`
- `LEONOS_PROT_EXEC`

Supported mapping flags:

- `LEONOS_MAP_PRIVATE`
- `LEONOS_MAP_FIXED`
- `LEONOS_MAP_ANONYMOUS`

Anonymous mappings require:

- `LEONOS_MAP_PRIVATE | LEONOS_MAP_ANONYMOUS`
- `fd == -1`
- `offset == 0`

File mappings currently require:

- `LEONOS_MAP_PRIVATE`
- no `LEONOS_MAP_ANONYMOUS`
- page-aligned `offset`
- readable file descriptor
- `prot == LEONOS_PROT_READ`

File mappings are lazy. The page fault handler maps file-backed pages on first
read. Writable file-backed mappings are still `-ENOSYS`.

If a page fault cannot be recovered by the lazy mapping path, kernel-mode faults
and the desktop window-server fault path still go to bugcheck. Ordinary Ring-3
applications are terminated instead: the kernel releases their open file state,
destroys their GUI/PTY ownership, records exit code `0x8000000e`, schedules the
next runnable user task, and posts an `Application Page Fault` desktop window
with PID, path, user, fault address, error flags, RIP/RSP/RBP, general registers,
CS, RFLAGS, and tick details.

`munmap(addr, len)` requires a page-aligned start address inside user space and
an existing VMA that fully covers the requested range. It can trim the front or
back of a VMA, remove a whole VMA, or split a VMA in the middle.

The libc heap allocator uses anonymous private `mmap` arenas and may release
large page-aligned free blocks with `munmap`.

## Process and Scheduler Calls

`fork()` creates a copy-on-write child with inherited file descriptors. The
parent receives the child PID and the child receives zero. `execve(path, argv,
envp)` validates and copies the user argument vectors, then replaces the
current process image while preserving its PID, working directory, identity,
PTY and descriptors not marked `FD_CLOEXEC`. Use `fork()` followed by
`execve()` to start a child; GUI applications can use `leonos_spawn_argv()`.

`wait4(pid, status, options, rusage)` waits for a child process. `options` and
`rusage` are accepted for ABI shape but are not a full Linux wait
implementation.

`nanosleep` has two call shapes:

- libc `sleep_ms(ms)` calls syscall 35 with milliseconds in argument 0.
- If argument 0 points to a readable 16-byte object, the kernel treats it as
  seconds and nanoseconds and converts it to scheduler ticks.

## Ioctl Groups

`ioctl(fd, request, arg)` is the extension point for APIs that do not yet have
dedicated syscall numbers. Current request groups:

- Authentication and users: `include/leonos/auth.h`
- GUI and framebuffer: `userland/libc/include/leonos/gui.h`
- Filesystem and installer storage: `include/leonos/fs.h`
- System, performance, and RTC time: `include/leonos/system.h`
- Device inventory: `include/leonos/device.h`
- PCM audio playback: `include/leonos/audio.h`
- Minimal networking: `include/leonos/net.h`
- Text layout and Unicode services: `include/leonos/text.h`
- PTY creation, I/O, and spawn: `include/leonos/pty.h`

Important requests include:

- `LEONOS_AUTH_IOCTL_STATUS`, `LEONOS_AUTH_IOCTL_CURRENT`,
  `LEONOS_AUTH_IOCTL_LIST_USERS`, `LEONOS_AUTH_IOCTL_LOGIN`,
  `LEONOS_AUTH_IOCTL_LOGOUT`, `LEONOS_AUTH_IOCTL_CREATE_USER`,
  `LEONOS_AUTH_IOCTL_UPDATE_USER`, `LEONOS_AUTH_IOCTL_CHANGE_PASSWORD`
- `LEONOS_GUI_IOCTL_FB_INFO`, `LEONOS_GUI_IOCTL_FB_FILL`,
  `LEONOS_GUI_IOCTL_FB_RECT`, `LEONOS_GUI_IOCTL_FB_TEXT`,
  `LEONOS_GUI_IOCTL_FB_PIXEL`, `LEONOS_GUI_IOCTL_FB_BLIT`,
  `LEONOS_GUI_IOCTL_FB_CAPS`, `LEONOS_GUI_IOCTL_FB_SET_MODE`. `FB_SET_MODE` is restricted to the Desktop
  window server. `leonos_fb_capabilities` reports whether `LEONOS_FB_CAP_MODE_SET` is
  available, the backend, maximum dimensions, and usable video-memory bytes.
- `LEONOS_GUI_IOCTL_CREATE_WINDOW`, `LEONOS_GUI_IOCTL_PRESENT_WINDOW`,
  `LEONOS_GUI_IOCTL_FETCH_WINDOW`, `LEONOS_GUI_IOCTL_WINDOW_EVENT`,
  `LEONOS_GUI_IOCTL_WAIT_WINDOW_EVENT`
- `LEONOS_GUI_IOCTL_APPEARANCE_STATE`, `LEONOS_GUI_IOCTL_APPEARANCE_REQUEST`,
  `LEONOS_GUI_IOCTL_POLL_APPEARANCE_REQUEST`,
  `LEONOS_GUI_IOCTL_PUBLISH_APPEARANCE_STATE`. Appearance requests carry the
  theme, independent Metro and Win95 basic color scheme IDs, wallpaper display
  mode, and wallpaper path. They are accepted from logged-in user tasks; the
  Desktop window server is the sole state publisher, saves the current user's
  personalization file, and broadcasts `LEONOS_GUI_APP_EVENT_THEME_CHANGED` to
  clients.
- `LEONOS_GUI_IOCTL_TASKS`, `LEONOS_GUI_IOCTL_TASK_KILL`
- `LEONOS_IOCTL_LIST_DIR`
- `LEONOS_IOCTL_SYSTEM_INFO`, `LEONOS_IOCTL_PERF_INFO`,
  `LEONOS_IOCTL_TIME_INFO`, `LEONOS_IOCTL_MACHINE_IDENTITY`
- `LEONOS_IOCTL_DEVICE_LIST`
- `LEONOS_IOCTL_AUDIO_CONFIGURE`, `LEONOS_IOCTL_AUDIO_WRITE`,
  `LEONOS_IOCTL_AUDIO_GET_STATE`
- `LEONOS_IOCTL_NET_CONFIG`, `LEONOS_IOCTL_NET_DHCP`,
  `LEONOS_IOCTL_NET_DNS`, `LEONOS_IOCTL_NET_PING`,
  `LEONOS_IOCTL_NET_HTTP_GET`
- `LEONOS_IOCTL_NET_SOCKET_OPEN`, `LEONOS_IOCTL_NET_SOCKET_CONNECT`,
  `LEONOS_IOCTL_NET_SOCKET_SEND`, `LEONOS_IOCTL_NET_SOCKET_RECV`,
  `LEONOS_IOCTL_NET_SOCKET_CLOSE`, `LEONOS_IOCTL_NET_CONNECTIONS`
- `LEONOS_INSTALL_IOCTL_LIST_DISKS`, `LEONOS_INSTALL_IOCTL_FORMAT_TARGET`
  (with `LEONOS_INSTALL_IOCTL_FORMAT_ESP` retained as an ABI alias),
  `LEONOS_INSTALL_IOCTL_MOUNT_TARGET`
- `LEONOS_DISK_IOCTL_LIST_PARTITIONS`, `LEONOS_DISK_IOCTL_FORMAT_PARTITION`,
  `LEONOS_DISK_IOCTL_DELETE_PARTITION`, `LEONOS_DISK_IOCTL_CREATE_PARTITION`,
  `LEONOS_DISK_IOCTL_MOUNT_PARTITION`,
  `LEONOS_DISK_IOCTL_UNMOUNT_PARTITION`. These use the fixed-size records in
  `leonos/fs.h`; listing reads a GPT table, while create/format/delete/mount/
  unmount require administrator install authorization and reject current boot
  or mounted installer-target disks. A data mount returns a stable
  `/mnt/disk<N>p<M>` path; unmount is rejected with busy while a live process
  holds a CWD, descriptor, executable image, or file mapping on that volume.
- `LEONOS_TEXT_IOCTL_LAYOUT_UTF8`
- `LEONOS_PTY_IOCTL_CREATE`, `LEONOS_PTY_IOCTL_SELF`,
  `LEONOS_PTY_IOCTL_READ_OUTPUT`, `LEONOS_PTY_IOCTL_WRITE_INPUT`,
  `LEONOS_PTY_IOCTL_SPAWN`

## Networking

The network ABI is defined in `include/leonos/net.h`. Legacy configuration and
diagnostic helpers are wrapped by libc as:

- `leonos_net_config`
- `leonos_net_dhcp_renew`
- `leonos_net_dns_resolve`
- `leonos_net_http_get`
- `leonos_net_ping`

The TCP client socket ABI is wrapped as:

- `leonos_socket_tcp`
- `leonos_socket_connect`
- `leonos_socket_send`
- `leonos_socket_recv`
- `leonos_socket_close`
- `leonos_net_connections`

IPv4 values are host-order packed addresses. For example, `10.0.2.2` is
`0x0a000202`.

The kernel currently supports a polling Intel e1000 MMIO driver, ARP, IPv4,
ICMP Echo, a small DHCP client, UDP transmit/receive for DHCP/DNS, DNS A record
lookups, a small ARP cache, active-open TCP client sockets, and a compatibility
`HTTP/1.0` GET helper over TCP. Boot starts with the QEMU user-network fallback so
early networking is usable, then automatically tries DHCP three times unless
`/system/config/services.cfg` contains `dhcp=0`. If DHCP succeeds, the active config
switches to the lease; if it fails or is disabled, the fallback remains active:

- guest IPv4: `10.0.2.15/24`
- gateway: `10.0.2.2`
- DNS: `10.0.2.3`

`netctl.elf` can still issue `leonos_net_dhcp_renew` after the desktop is
running to manually renew or recover a lease when the caller is an
administrator. Non-admin users may read network status and use DNS/HTTP/socket
APIs, but DHCP renew changes the global IPv4 configuration and returns
`EPERM` unless the caller is an administrator or trusted service task. The only
pre-login exception is `/system/apps/oobe/oobe.elf` while `/system/state/oobe.done` is
absent, so the license screen can expose a narrow `Renew DHCP` recovery button.
`netctl.elf` also queries `leonos_net_connections` and displays TCP client
sockets in `SYN_SENT`, `ESTABLISHED`, `TIME_WAIT`, or `CLOSED`. Administrators
and trusted service tasks see the full socket table; normal users see only
connections owned by their uid.

`serviced.elf` now runs as a protected service task started by the desktop. It
uses the same `leonos_net_config` and `leonos_net_dhcp_renew` wrappers to keep
retrying DHCP in the background when `/system/config/services.cfg` has `dhcp=1` and the
kernel is still using the static fallback. It publishes status to
`/var/run/services.state` for `servicemgr.elf`.

`leonos_socket_tcp` returns an integer socket handle owned by the current task.
`leonos_socket_connect` accepts a host name or IPv4 literal, resolves DNS A
records when needed, tries each returned address, and records the selected
remote IP and local port. `leonos_socket_send` and `leonos_socket_recv` are
synchronous TCP byte-stream calls with per-call timeouts and explicit network
status fields. `leonos_socket_close` moves established connections through
`TIME_WAIT` before the kernel garbage-collects the socket record.

`include/leonos/http.h` provides the higher-level userland HTTP client:
`leonos_http_get`, `leonos_http_request`, and `leonos_http_resolve_url`.
The client uses the socket wrappers, follows bounded redirects, decodes chunked
transfer responses, exposes response headers, content type, body length, final
URL, redirect count, and truncation flags. It sends plain `HTTP/1.1` for
`http://`, and uses Mbed TLS 2.28.8 for TLS 1.2, CA-chain, hostname, and clock
validation of `https://`. `httpget.elf` and `browser.elf` use this library for
both schemes. The older `leonos_net_http_get` ioctl remains as a compatibility
helper for small diagnostic callers.

`downloadmgr.elf` also uses the HTTP client. It is currently fixed-buffer and
reports oversized responses through the truncation flag instead of streaming
large files incrementally.

The network ioctls return `0` when the request structure was processed. Per
operation results are reported in the structure `status` field. Timeouts are
bounded by the kernel even if a larger value is requested; socket, HTTP, DNS,
and DHCP paths currently cap requested waits at 10000 ms.

## Authentication and Authorization

The authentication ABI is defined in `include/leonos/auth.h`. libc exposes:

- `leonos_auth_status`
- `leonos_auth_current`
- `leonos_auth_list_users`
- `leonos_auth_login`
- `leonos_auth_logout`
- `leonos_auth_create_user`
- `leonos_auth_update_user`
- `leonos_auth_change_password`

Successful login updates the current desktop session identity in the scheduler:
`uid`, `role`, `session_id`, `username`, and `home` are attached to the desktop
task and inherited by child applications. Logout clears the session identity and
kills ordinary user tasks in the session, then desktop returns to `login.elf`.

The kernel asks middlelayer policy before file, task-kill, user-management, and
installer-storage operations. File authorization uses the `LEONACL.SYS` ACL
sidecar model on exFAT, FAT32, and ext2 roots. The mapping is:

- `stat`, directory reads, and file reads: Read/List.
- `open` create/truncate, `write`, and `mkdir`: Write/Create.
- `execve` and path traversal: Execute/Traverse.
- `unlink`, `rmdir`, and the source side of `rename`: Delete.
- ACL set operations: Manage Permissions.

Normal users can access their own home through Owner permissions and shared
temporary files through the `/tmp` default ACL. Bundled help files under
`/docs` are treated as a system tree: normal users receive read/execute access
by default, while administrators retain full control. Administrators can manage
users and can take ownership or repair corrupt ACL metadata. Shutdown and reboot
remain available to any logged-in user.

## Current Limitations

- `fork`, `vfork`, `pipe`, process groups and default signal actions are
  available; user-installed signal handlers and `clone` are not yet supported.
- Networking has TCP client sockets and a TLS 1.2 HTTPS client path, but no TCP
  listener/server mode, UDP socket API, or full retransmission/window-management
  surface yet.
- `execve` replaces the caller; use `fork` followed by `execve` to launch a
  child process.
- `libleonos.so.1` owns the common process, descriptor, pipe, process-group,
  PTY foreground-group, priority, resource-limit, and wait wrappers. Their
  standard declarations come from the SDK's Picolibc headers. `waitpid` with
  `WNOHANG` returns `0` when no child state is available; blocking waits yield
  across the kernel's temporary `EAGAIN` response. `vfork` currently has the
  same COW semantics as `fork`.
- `nice` and `getpriority` return standard priorities in the `-20..19` range.
  Raw syscall users receive `priority + 20` and must subtract 20 after checking
  for a negative errno; the shared runtime performs that decoding.
- Terminal Ctrl+C/Ctrl+Z actions are delivered to the foreground process group,
  but `signal`, `sigaction`, `sigprocmask`, and user-installed signal handlers
  are not available yet (`signal()` returns `SIG_ERR` with `ENOSYS`).
- The shared runtime contains the common ANSI curses subset used by Nano and
  `sl`; applications should include the SDK's `<curses.h>` or `<ncurses.h>`
  instead of carrying a private terminal shim.
- `libleonos.so.1` also supplies the common POSIX adapters for file status,
  directory iteration and `fcntl`; third-party ports should use the SDK's
  normal Picolibc headers rather than copying those wrappers into each port.
- File-backed `mmap` is private and read-only.
- Open permissions are ACL checks, not a full Unix permission model.
- FAT32 and the supported ext2 subset do not expose LeonOS ACLs as native
  ownership/mode metadata; LeonOS stores ACL metadata in hidden `LEONACL.SYS`
  sidecar files and enforces it at syscall/ioctl boundaries.
- `ioctl` is intentionally broad and should be split into dedicated syscalls or
  narrower devices as the ABI stabilizes.
