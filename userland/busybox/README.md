# LeonOS BusyBox profile

The image builds BusyBox 1.36.1 as `/programs/busybox/busybox.elf` with a
small, static collection of file and text applets. Double-clicking it opens a
terminal and prints the applet list. Invoke a specific applet with:

```text
/programs/busybox/busybox.elf ls /
```

The profile includes BusyBox `ash` behind the `sh` applet with native
`fork`/`exec`, pipelines, redirections, background jobs, and `jobs`/`fg`/`bg`.
It supports simple command lines, shell built-ins, and the bundled applets (`ls`, `pwd`, `cat`,
`echo`, `clear`, `grep`, `head`, `tail`, `wc`, `sha256sum`, `basename`, `dirname`, `printf`, `diff`,
`less`, `ps`, and `kill`,
`mkdir`, `rmdir`, `cp`, `mv`, `rm`, `unlink`, `printenv`, `uname`, `sleep`,
`true`, `false`, `nohup`, `whoami`, and `vi`). The GUI terminal launches this
shell by default.

Storage administration applets are also included: `fdisk` can list and
interactively create/delete GPT entries and edit their type/name (`t` and `r`),
`mkfs.fat`/`mkfs.fat32`/`mkfs.ext2`/`mkfs.exfat` format an existing LeonOS
partition, and `mount`/`umount` manage runtime data mounts. `blkid` and `lsblk`
show the same disk metadata, while `fsck`, `fsck.fat`, `fsck.fat32`,
`fsck.vfat`, `fsck.ext2`, and `fsck.exfat` perform read-only superblock checks.
`leonos-grub-installer ESP` copies the staged EFI/GRUB payload to a mounted
ESP, and `sync` is available as a synchronous-write compatibility command.
They use `/dev/disk0` and `/dev/disk0pN` paths and the kernel storage ABI rather
than Linux block-device ioctls. Formatting, partition changes, and mount
operations require an administrator account; the running boot disk is
protected.

The installer ISO additionally provides `/programs/gptinit/gptinit.elf` for
blank disks. `gptinit /dev/diskN` initializes a protective MBR and empty
primary/backup GPT pair after an explicit `YES` confirmation;
`gptinit --force /dev/diskN` skips confirmation and may replace a valid GPT.
The utility is not staged into installed LeonOS systems.

Examples:

```text
fdisk -l /dev/disk0
fdisk /dev/disk0
blkid
lsblk
fsck.ext2 /dev/disk0p3
mkfs.ext2 /dev/disk0p3
mount -t ext2 /dev/disk0p3 /mnt/data
umount /mnt/data
```

Ash's fancy prompt support is enabled: `\\w` expands to the current directory
and `\\$` expands to `$` for ordinary users or `#` for root. BusyBox's line
editor calculates the visible prompt width while the Terminal consumes ANSI
color sequences without moving its cursor, so colored prompts can use the
usual `\\[...\\]` markers.

In TTY mode, `~` and `~/path` resolve to the home directory of the account that
logged in. This is resolved from the current LeonOS session, so it remains
correct even though the shell starts before the login program completes.

Interactive Ash uses BusyBox's line editor with Tab command/path completion. The terminal sends
the Tab byte to the PTY and applies only the cursor updates returned by Ash or the foreground
program, so programs that do not implement four-column Tab stops are not locally mis-rendered.

`diff` produces unified file differences.  `less` provides keyboard-controlled
pagination for text files; its input is capped at 8,192 lines to keep malformed
or exceptionally large files from exhausting the current user-space budget.
`ls` emits ANSI file-type colors by default when its output is a terminal; use
`ls --color=never` when plain output is required.

`grep` searches standard input or files with POSIX basic regular expressions.
It supports literal (`-F`), extended (`-E`), case-insensitive (`-i`), line-number
(`-n`), count (`-c`), recursive (`-r`), and before/after context (`-A`, `-B`, and
`-C`) modes.

`nohup PROG ARGS` is available in both the GUI Terminal and TTY shell. It
ignores `SIGHUP`, changes terminal stdin to `/dev/null`, and appends terminal
stdout/stderr to `nohup.out` in the current directory, falling back to
`$HOME/nohup.out` when the current directory is not writable.

`cp`, `mv`, and `rm` operate on regular files and directories through the
LeonOS filesystem ABI. Symbolic links, ownership changes, and special device
nodes remain unsupported by the filesystem and return an error.

The `file` command is provided as an external program backed by upstream
libmagic. Ash resolves it to `/programs/file/file.elf`; the matching
compiled database is installed at `/system/share/misc/magic.mgc`.
`fastfetch` is likewise resolved to `/programs/fastfetch/fastfetch.elf`.
The `sl` terminal joke is resolved to `/programs/sl/sl.elf`.

The kernel provides process inspection through the task snapshot ABI,
same-user signal termination, COW `fork`, `execve`, process groups, foreground
PTY groups, and nice-style priorities. `kill` and graphical task tools use
those interfaces. Ash uses normal pipelines and redirections (`<`, `>`, `>>`,
`2>`), and handles `Ctrl+C`/`Ctrl+Z` through the PTY foreground group. The
POSIX `SIG_DFL` and `SIG_IGN` dispositions are available; arbitrary user-space
signal handlers and shared file offsets after `fork` are not yet exposed.
Ash does not use the legacy PTY-launch adapter: its commands use the upstream
MMU `fork`/`pipe`/`dup2`/`execvp`/`waitpid` flow. The remaining
BusyBox adapter only maps bare applet names to the single
`/programs/busybox/busybox.elf` executable and maps bundled external tools
to their installed paths.

BusyBox is GPL-2.0-only; `LICENSE` and upstream version information are staged
beside the executable in the image.
