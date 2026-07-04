# Boot and Integrity

## Boot flow

LeonOS 4 boots through GRUB and the custom loader:

1. GRUB starts `boot/loader.elf` through Multiboot2.
2. The loader locates `kernel.sys` and `middlelayer.sys`.
3. The loader validates both files before loading their ELF images.
4. The kernel receives a `struct leonos_boot_handoff`, including middlelayer
   module information and the middlelayer API pointer.

Normal disk images load components from the ESP tree:

- `0:/boot/loader.elf`
- `0:/system/kernel.sys`
- `0:/system/middlelayer.sys`

Installer ISOs pass kernel, middlelayer, and installer root as GRUB modules:

- `/system/kernel.sys` with module tag `leonos-kernel`
- `/system/middlelayer.sys` with module tag `leonos-middlelayer`
- `/install/root.fat` with module tag `leonos-installer-root`

## Build-time hashes

`tools/gen_loader_integrity.py` calculates SHA-256 hashes for:

- `build/system/kernel.sys`
- `build/system/middlelayer.sys`

The generated header is:

- `include/generated/loader_integrity.h`

`tools/gen_ninja.py` wires the order so `kernel.sys` and `middlelayer.sys` are
built first, then `loader_integrity.h`, then loader C objects that include that
header. Do not hand-edit `build.ninja`; regenerate it from `tools/gen_ninja.py`.

## Runtime behavior

Before `elf_load_exec`, the loader hashes the raw component bytes it is about
to load and compares them with the generated expected hashes.

If both match, the serial log reports integrity success for each component. If
either component differs, the loader prints a warning with expected and actual
SHA-256 values and waits for a user decision:

- `Y`: continue booting anyway.
- `N`: stop at the loader warning.

This check runs for both normal EFI filesystem loading and installer GRUB
module loading.

## Installer compatibility

The installer payload is built from the same matched runtime ESP tree:

- `build/esp` contains the loader, kernel, middlelayer, resources, config, and
  userland applications for the installed system.
- `tools/make_installer_root.py` copies `build/esp` into
  `install/esp` inside `build/install/root.fat`.
- `tools/make_installer_iso.py` stages top-level installer copies of
  `boot/loader.elf`, `system/kernel.sys`, `system/middlelayer.sys`, and
  `install/root.fat`.

This keeps a freshly installed system consistent with the loader hashes instead
of producing an install that fails the next boot's integrity check.

## Trust boundary

This feature detects a mismatch between the loader's compiled-in component
hashes and the `kernel.sys`/`middlelayer.sys` bytes it is about to execute. It
is not a full Secure Boot chain, signature system, or anti-rollback mechanism.
If an attacker replaces the loader and its embedded hashes together, that is
outside the current trust model.
