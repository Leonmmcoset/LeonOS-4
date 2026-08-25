# Boot and Integrity

## Boot flow

LeonOS 4 boots through GRUB and the custom loader:

1. GRUB starts `boot/loader.elf` through Multiboot2.
2. The loader locates `kernel.sys` and `middlelayer.sys`.
3. The loader validates both files before loading their ELF images.
4. The kernel receives a `struct leonos_boot_handoff`, including middlelayer
   module information and the middlelayer API pointer.

During early boot, normal disk images load components from the FAT32 ESP:

- `/boot/loader.elf`
- `/system/kernel.sys`
- `/system/middlelayer.sys`

After the kernel starts, its storage layer mounts the separate ext2 partition
as the normal `/` runtime root. The ESP stays separate so a full root cannot
consume UEFI boot space.

Installer ISOs pass kernel, middlelayer, and installer root as GRUB modules:

- `/system/kernel.sys` with module tag `leonos-kernel`
- `/system/middlelayer.sys` with module tag `leonos-middlelayer`
- `/install/root.fat` with module tag `leonos-installer-root`

The installer root remains resident for the installer session. It is accessed
through a shared supervisor-only high direct map so user page tables cannot
replace its low physical placement. The VM must provide enough RAM for GRUB to
load the whole module; a 400 MiB root is supported with 1 GiB or more of guest
memory.

GRUB chooses module placement, while `kernel.sys` and `middlelayer.sys` have
fixed physical `PT_LOAD` destinations. Before loading either executable, the
Loader checks those destinations against the installer-root module. When they
overlap, it allocates replacement EFI LoaderData pages below 4 GiB, copies the
module, and records the new range in the boot handoff. That remains inside the
kernel's 16 GiB direct map. The kernel imports the range before physical-memory
initialization, so the original Multiboot range can be reclaimed without
corrupting the FAT filesystem.

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

## GRUB framebuffer boot log

After GRUB supplies the Multiboot2 framebuffer tag, the loader creates an
on-screen boot log using the built-in 8x16 PSF font. All subsequent loader
serial output is mirrored to this panel, including component discovery,
integrity results, load failures, and the kernel handoff. The loader records
the panel geometry and cursor in `struct leonos_boot_handoff`, so the kernel
bootstrap console appends its startup log to the same GRUB framebuffer panel
instead of opening a separate top-corner framebuffer console.

The panel uses Metro blue by default and switches to the persisted Win95 or
Metro theme after the loader reads `/system/config/display.conf`. It requires a 32-bit
linear framebuffer; serial logging remains available when GOP/framebuffer
output is unavailable.

## Installer compatibility

The current installer writes target disks through AHCI. In VirtualBox, attach
the destination VDI to a SATA controller with the controller type set to
`AHCI`; do not use the default PIIX4 IDE controller. The ISO may boot from its
virtual optical device independently, but IDE target disks are not installable
until an IDE driver is added.

The installer payload is built from the same matched runtime staging tree:

- `build/esp` contains the loader, kernel, middlelayer, resources, config, and
  userland applications for the installed system.
- `tools/make_installer_root.py` splits `build/esp` into `install/esp` (the
  FAT32 ESP boot subset) and `install/root` (the ext2 runtime root) inside
  `build/install/root.fat`.
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
