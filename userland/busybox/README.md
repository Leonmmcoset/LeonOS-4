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
`echo`, `clear`, `head`, `tail`, `wc`, `basename`, `dirname`, `printf`,
`mkdir`, `rmdir`, `true`, and `false`). The GUI terminal launches this shell
by default.

LeonOS does not yet provide POSIX `fork`/`vfork`, pipe, descriptor duplication,
process groups, or signal semantics. Therefore pipelines, redirections,
background jobs, command substitution, and arbitrary external ELF programs
are not supported by this BusyBox profile. They must not be advertised as
working shell functionality until those kernel interfaces are implemented.

BusyBox is GPL-2.0-only; `LICENSE` and upstream version information are staged
beside the executable in the image.
