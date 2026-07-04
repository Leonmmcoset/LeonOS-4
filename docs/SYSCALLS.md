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
| 2 | `open` | `open` | Opens files, directories, and `0:/dev/fb0`; supports LeonOS open flags. |
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
| 59 | `execve` | `execve` | Spawns an ELF user program and returns the child PID. |
| 60 | `exit` | `exit` | Releases process-owned files, windows, PTYs, and exits with a code. |
| 61 | `wait4` | `wait4` | Waits for a child and writes a Linux-style shifted status. |
| 79 | `getcwd` | `getcwd` | Copies the task current directory. |
| 80 | `chdir` | `chdir` | Changes the task current directory after path lookup. |
| 82 | `rename` | `rename` | Renames FAT32 files or directories. |
| 83 | `mkdir` | `mkdir` | Creates a FAT32 directory. |
| 84 | `rmdir` | `rmdir` | Removes an empty FAT32 directory. |
| 87 | `unlink` | `unlink` | Removes a FAT32 file. |

## File and Directory Calls

Paths use LeonOS numbered-drive syntax such as `0:/userland/desktop.elf`.
Relative paths are resolved against the task current directory through the
middlelayer VFS resolver when available, with a kernel fallback.

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

`munmap(addr, len)` requires a page-aligned start address inside user space and
an existing VMA that fully covers the requested range. It can trim the front or
back of a VMA, remove a whole VMA, or split a VMA in the middle.

The libc heap allocator uses anonymous private `mmap` arenas and may release
large page-aligned free blocks with `munmap`.

## Process and Scheduler Calls

`execve(path, argv, envp)` validates and copies the user argument vectors,
spawns the requested ELF, and returns the child PID rather than replacing the
current process image.

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
- Text layout and Unicode services: `include/leonos/text.h`
- PTY creation, I/O, and spawn: `include/leonos/pty.h`

Important requests include:

- `LEONOS_AUTH_IOCTL_STATUS`, `LEONOS_AUTH_IOCTL_CURRENT`,
  `LEONOS_AUTH_IOCTL_LIST_USERS`, `LEONOS_AUTH_IOCTL_LOGIN`,
  `LEONOS_AUTH_IOCTL_LOGOUT`, `LEONOS_AUTH_IOCTL_CREATE_USER`,
  `LEONOS_AUTH_IOCTL_UPDATE_USER`, `LEONOS_AUTH_IOCTL_CHANGE_PASSWORD`
- `LEONOS_GUI_IOCTL_FB_INFO`, `LEONOS_GUI_IOCTL_FB_FILL`,
  `LEONOS_GUI_IOCTL_FB_RECT`, `LEONOS_GUI_IOCTL_FB_TEXT`,
  `LEONOS_GUI_IOCTL_FB_PIXEL`, `LEONOS_GUI_IOCTL_FB_BLIT`
- `LEONOS_GUI_IOCTL_CREATE_WINDOW`, `LEONOS_GUI_IOCTL_PRESENT_WINDOW`,
  `LEONOS_GUI_IOCTL_FETCH_WINDOW`, `LEONOS_GUI_IOCTL_WINDOW_EVENT`
- `LEONOS_GUI_IOCTL_TASKS`, `LEONOS_GUI_IOCTL_TASK_KILL`
- `LEONOS_IOCTL_LIST_DIR`
- `LEONOS_IOCTL_SYSTEM_INFO`, `LEONOS_IOCTL_PERF_INFO`,
  `LEONOS_IOCTL_TIME_INFO`
- `LEONOS_IOCTL_DEVICE_LIST`
- `LEONOS_INSTALL_IOCTL_LIST_DISKS`, `LEONOS_INSTALL_IOCTL_FORMAT_ESP`,
  `LEONOS_INSTALL_IOCTL_MOUNT_TARGET`
- `LEONOS_TEXT_IOCTL_LAYOUT_UTF8`
- `LEONOS_PTY_IOCTL_CREATE`, `LEONOS_PTY_IOCTL_SELF`,
  `LEONOS_PTY_IOCTL_READ_OUTPUT`, `LEONOS_PTY_IOCTL_WRITE_INPUT`,
  `LEONOS_PTY_IOCTL_SPAWN`

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
installer-storage operations. Normal users can read system programs/resources,
write their own home and `0:/tmp`, and see or kill only their own tasks.
Administrators can manage users and other user directories. Shutdown and reboot
remain available to any logged-in user.

## Current Limitations

- There is no `fork`, `clone`, `pipe`, `socket`, `poll`, or signal ABI.
- `execve` spawns a child process instead of replacing the caller.
- File-backed `mmap` is private and read-only.
- Open permissions are simple capability checks, not a full Unix permission
  model.
- FAT32 does not store real owner/mode metadata; v1 permissions are enforced at
  syscall/ioctl boundaries through the kernel and middlelayer policy.
- `ioctl` is intentionally broad and should be split into dedicated syscalls or
  narrower devices as the ABI stabilizes.
