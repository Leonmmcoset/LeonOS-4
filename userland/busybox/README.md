# LeonOS BusyBox profile

The image builds BusyBox 1.36.1 as `0:/programs/busybox/busybox.elf` with a
small, static collection of file and text applets. Double-clicking it opens a
terminal and prints the applet list. Invoke a specific applet with:

```text
0:/programs/busybox/busybox.elf ls 0:/
```

The profile includes BusyBox `hush` behind the `sh` applet in standalone
no-fork mode. It supports simple command lines, shell built-ins, and the
bundled applets (`ls`, `pwd`, `cat`,
`echo`, `clear`, `head`, `tail`, `wc`, `basename`, `dirname`, `printf`, `diff`,
`less`,
`mkdir`, `rmdir`, `cp`, `mv`, `rm`, `unlink`, `printenv`, `uname`, `sleep`,
`true`, `false`, and `vi`). The GUI terminal launches this shell by default.

`diff` produces unified file differences.  `less` provides keyboard-controlled
pagination for text files; its input is capped at 8,192 lines to keep malformed
or exceptionally large files from exhausting the current user-space budget.
`ls` emits ANSI file-type colors by default when its output is a terminal; use
`ls --color=never` when plain output is required.

`cp`, `mv`, and `rm` operate on regular files and directories through the
LeonOS filesystem ABI. Symbolic links, ownership changes, and special device
nodes remain unsupported by the filesystem and return an error.

The `file` command is provided as an external program backed by upstream
libmagic. Hush resolves it to `0:/programs/file/file.elf`; the matching
compiled database is installed at `0:/system/share/misc/magic.mgc`.

LeonOS does not yet provide POSIX `fork`/`vfork`, pipe, descriptor duplication,
process groups, or signal semantics. Therefore pipelines, redirections,
background jobs, command substitution, and arbitrary external ELF programs
are not supported by this BusyBox profile. They must not be advertised as
working shell functionality until those kernel interfaces are implemented.

BusyBox is GPL-2.0-only; `LICENSE` and upstream version information are staged
beside the executable in the image.
