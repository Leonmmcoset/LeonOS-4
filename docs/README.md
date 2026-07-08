# LeonOS 4 Docs

This directory tracks the architecture and operational notes that are useful
when changing the kernel, loader, middlelayer, installer, or user ABI.

- [ABI](ABI.md): syscall subset, device model, and middlelayer ABI.
- [Syscalls](SYSCALLS.md): syscall entry convention, syscall table,
  `mmap`/`munmap`, process calls, and ioctl groups.
- [Filesystem](FILESYSTEM.md): numbered drives, mount policy, FAT32 support,
  bundled `.hlp` help documents, and current limits.
- [Boot and Integrity](BOOT_AND_INTEGRITY.md): loader boot flow, SHA-256
  component checks, installer compatibility, and trust boundary.
- [Middlelayer](MIDDLELAYER.md): current middlelayer services, ABI files, and
  kernel/middlelayer responsibility split.
- [Build and Installer](BUILD_AND_INSTALLER.md): generated build graph,
  packaging outputs, installer payload layout, and WSL validation commands.
- [Browser](BROWSER.md): `browser.elf`, current HTML renderer limits, and the
  staged path toward a full litehtml port.
- [Third-Party Code](THIRD_PARTY.md): submodule paths, upstream licenses, and
  integration status for bundled external code.
