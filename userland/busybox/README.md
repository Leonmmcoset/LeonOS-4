# LeonOS BusyBox profile

The image builds BusyBox 1.36.1 as `0:/programs/busybox/busybox.elf` with a
small, static collection of file and text applets. Double-clicking it opens a
terminal and prints the applet list. Invoke a specific applet with:

```text
0:/programs/busybox/busybox.elf ls 0:/
```

The profile includes BusyBox `hush` behind the `sh` applet with native
`fork`/`exec`, pipelines, redirections, background jobs, and `jobs`/`fg`/`bg`.
It supports simple command lines, shell built-ins, and the bundled applets (`ls`, `pwd`, `cat`,
`echo`, `clear`, `grep`, `head`, `tail`, `wc`, `basename`, `dirname`, `printf`, `diff`,
`less`, `ps`, and `kill`,
`mkdir`, `rmdir`, `cp`, `mv`, `rm`, `unlink`, `printenv`, `uname`, `sleep`,
`true`, `false`, and `vi`). The GUI terminal launches this shell by default.

`diff` produces unified file differences.  `less` provides keyboard-controlled
pagination for text files; its input is capped at 8,192 lines to keep malformed
or exceptionally large files from exhausting the current user-space budget.
`ls` emits ANSI file-type colors by default when its output is a terminal; use
`ls --color=never` when plain output is required.

`grep` searches standard input or files with POSIX basic regular expressions.
It supports literal (`-F`), extended (`-E`), case-insensitive (`-i`), line-number
(`-n`), count (`-c`), recursive (`-r`), and before/after context (`-A`, `-B`, and
`-C`) modes.

`cp`, `mv`, and `rm` operate on regular files and directories through the
LeonOS filesystem ABI. Symbolic links, ownership changes, and special device
nodes remain unsupported by the filesystem and return an error.

The `file` command is provided as an external program backed by upstream
libmagic. Hush resolves it to `0:/programs/file/file.elf`; the matching
compiled database is installed at `0:/system/share/misc/magic.mgc`.
`fastfetch` is likewise resolved to `0:/programs/fastfetch/fastfetch.elf`.

The kernel provides process inspection through the task snapshot ABI,
same-user signal termination, COW `fork`, `execve`, process groups, foreground
PTY groups, and nice-style priorities. `kill` and graphical task tools use
those interfaces. Hush uses normal pipelines and redirections (`<`, `>`, `>>`,
`2>`), and handles `Ctrl+C`/`Ctrl+Z` through the PTY foreground group. Custom
POSIX signal handlers and shared file offsets after `fork` are not yet exposed.

BusyBox is GPL-2.0-only; `LICENSE` and upstream version information are staged
beside the executable in the image.
