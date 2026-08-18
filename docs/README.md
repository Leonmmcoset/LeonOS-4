# LeonOS 4 Docs

This directory tracks the architecture and operational notes that are useful
when changing the kernel, loader, middlelayer, installer, or user ABI.

- [ABI](ABI.md): syscall subset, device model, and middlelayer ABI.
- [Syscalls](SYSCALLS.md): syscall entry convention, syscall table,
  `mmap`/`munmap`, process calls, and ioctl groups.
- [Filesystem](FILESYSTEM.md): numbered drives, mount policy, ext2 root plus FAT32/ISO 9660 support,
  bundled `.hlp` help documents, and current limits.
- [Drivers](DRIVERS.md): bootstrap drivers, loadable `.drv` modules, ABI, and
  management policy.
- [Boot and Integrity](BOOT_AND_INTEGRITY.md): loader boot flow, SHA-256
  component checks, installer compatibility, and trust boundary.
- [Middlelayer](MIDDLELAYER.md): current middlelayer services, ABI files, and
  kernel/middlelayer responsibility split.
- [Build and Installer](BUILD_AND_INSTALLER.md): generated build graph,
  packaging outputs, installer payload layout, and WSL validation commands.
- [Browser](BROWSER.md): `browser.elf`, the upstream LiteHTML rendering
  integration, resource loading, forms, and current browser limits.
- [Third-Party Code](THIRD_PARTY.md): submodule paths, upstream licenses, and
  integration status for bundled external code.

## Tools

`tools/analyze_boot_log.py` parses loader, kernel, middlelayer, ELF, dynamic
linker, CPU exception, storage, and ACPI messages. It accepts a file or
standard input and preserves one-based evidence line numbers:

```sh
python tools/analyze_boot_log.py boot.log
python tools/analyze_boot_log.py boot.log --json tools/dist/boot-report.json
cat boot.log | python tools/analyze_boot_log.py - --strict
python tools/analyze_boot_log.py --self-test
```

`--strict` returns a non-zero status when an error or fatal finding is present,
which makes the analyzer suitable for CI/QEMU smoke-test wrappers.

`tools/check_licenses.py` checks initialized submodules, staged image and
installer payloads, SDK directories or ZIP archives, and the installer
acknowledgements policy.  It uses an explicit license manifest so disabled
components are not reported as missing:

```sh
python tools/check_licenses.py
python tools/check_licenses.py --strict
python tools/check_licenses.py --sdk LeonOS4-Developer-SDK.zip \
  --json tools/dist/license-report.json
python tools/check_licenses.py --self-test
```

Raw FAT/VMDK/ISO files are reported as skipped; pass the mounted or staged
directory (or a ZIP) to verify their contents.  The default excluded-credit
policy rejects `llama2.c`, `TinyLlama`, and `karpathy` in the installer's
acknowledgements page.  Add project-specific forbidden names with repeated
`--excluded-credit` options.
