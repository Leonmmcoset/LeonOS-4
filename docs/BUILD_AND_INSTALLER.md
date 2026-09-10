# Build and Installer

## Accounts and optional components

Fresh installations create a fixed `root` account (UID/GID 0) and a chosen
ordinary account (UID/GID 1000) in the installer. Both passwords require
1-32 UTF-8 characters without whitespace; confirmations must match. Python
and GCC/binutils can be selected independently. OOBE is no longer shipped.
See [Installer Accounts and Components](INSTALLER_ACCOUNTS.md) for the current
GUI/TTY installation and login regression commands. Older QEMU scripts below
that assume OOBE or omit account setup need that workflow updated before use.

## Installer and window regressions

After `python3 build.py run release`, run:

```sh
python3 tools/test_installer_input.py
python3 tools/test_linux_pty.py
python3 tools/test_runtime_responsiveness.py
python3 tools/test_terminal.py
python3 tools/test_oobe.py
python3 tools/test_installer_window_qemu.py installer
python3 tools/test_installer_window_qemu.py desktop
python3 tools/test_installer_window_qemu.py tty --install
python3 tools/test_installer_window_qemu.py installed --disk build/ui-regressions/tty/scratch.raw
```

The QEMU checks boot the distribution ISOs using KVM, OVMF, 4 GiB RAM and two
CPUs. Screenshots and serial logs go under `build/ui-regressions`. The TTY check
attaches only its disposable `tty/scratch.raw` hard disk; `--install` confirms
formatting and copying to that disk, with a 15-minute copy timeout for the full
toolchain/runtime payload. Without it, the test cancels at confirmation. The
`installed` case boots that disk with no ISO attached and repeats the desktop check.
The desktop check maximizes Terminal, runs Linux Vim, restores the window, and
maximizes File Manager and Task Manager. The installer check verifies its real
sidebar and footer coordinates, without compensating for a cropped surface.
These checks do not establish VMware validation.

Window resizing retains the public `leonos_gui_*` API. The shared window client
now submits a fully painted replacement segment to windowd before releasing its
old segment, and the compositor invalidates its old mapping on replacement.
Normal, installer, and SDK builds ship matching client/server code. Applications
that statically embedded an older window client need relinking to gain this fix;
the previous CREATE/PRESENT wire messages remain accepted for fixed-size frames.

TTY reads now wait for a canonical line instead of reporting EOF on an empty
live input queue. Block-device SDK helpers translate POSIX `-1`/`errno` into
their documented negative-error return values, including missing disk probes.

## Power control regressions

```sh
python3 build.py run test-power
python3 tools/test_power_qemu.py desktop-reboot
python3 tools/test_power_qemu.py desktop-shutdown
python3 tools/test_power_qemu.py installer-reboot
```

The host checks execute the actual reboot dispatcher with hardware operations
intercepted, plus desktop confirmation/error handling and authd power policy.
The QEMU checks click the real controls and require guest-originated RESET or
SHUTDOWN events. A reboot must reach a second kernel boot; shutdown must exit
QEMU. The installer case performs a full GUI install on a new 2 GiB test disk,
ejects the ISO before clicking Restart, and checks the installed ext2 system
reaches OOBE. It refuses to overwrite an existing test disk; use a fresh
`--output` directory for repeat installations. Logs, screenshots and event
records are saved under `build/power-regressions`.

The kernel accepts the Linux raw `reboot(magic1, magic2, cmd, arg)` argument
order, all four Linux magic2 values, and 32-bit argument widths. Obsolete
one-argument raw calls are rejected. The bundled unmodified musl `reboot(int)`
wrapper therefore works without an application-specific ABI workaround.

The desktop assumes the user's UID after login. Its `leonos_system_*` helpers
send power requests over a fresh authd connection, so SO_PEERCRED reflects the
current UID rather than a cached root bootstrap connection. The privileged
daemon authorizes root services or the enabled current console user, accepts
only reboot/power-off, syncs filesystems, then calls musl reboot. Other users,
logged-out sessions, disabled accounts and invalid requests are rejected.
The installer runs as root and uses musl reboot directly after sync. Errors
remain visible in the desktop, GUI installer and TTY installer.

This is a targeted power-control repair, not full reboot(2) certification:
the existing kernel UID gate and remaining capability, namespace, HALT,
CAD/RESTART2 and other command semantics remain in the ABI audit backlog.
QEMU results do not establish VMware validation.

Build 4.6.2-3490 validation: host power, OOBE, UAPI and security checks passed;
QEMU Start-menu reboot reached a second kernel boot, and shutdown produced a
guest-shutdown event and normal QEMU exit. Both ISOs and GCC payload integrity
checks passed. The full GUI installation/restart test was stopped at the user's
request before completion; its completion-page restart is not yet guest-verified.

## Source of truth

`build.py` is the maintained build entry point. Component selection comes from
`configs/components.toml` and Kconfig. Generated graph files are not edited by hand.

Important generated files include:

- `include/generated/autoconf.h`
- `include/generated/autoconf-installer.h`
- `include/generated/build_info.h`
- `include/generated/loader_integrity.h`
- `include/generated/rustcfg.args`

## Configuration

`Kconfig` currently exposes two groups:

- `Build`
- `System Configuration`

The build group controls the source macro that is compiled into binaries for
the standalone VMDK image, the source macro compiled into binaries installed
from the installer ISO, and whether Ninja should regenerate `build.ninja`
before builds. The default for both license gates is enabled; the default for
Ninja regeneration is disabled.

The system group controls the license platform URL that is compiled into
userland binaries. The default is `http://127.0.0.1:30301`.

`tools/kconfig_sync.py` normalizes `.config`, writes `autoconf.h` with the VMDK
`LEONOS_LICENSE_REQUIRE` policy, writes `autoconf-installer.h` with the
installer-installed-system policy, and writes `CONFIG_LICENSE_SERVER_URL` into
both headers. License binaries read that compiled macro directly; there is no
runtime `/etc/leonos/license.conf` server override.

## Main outputs

Common build outputs:

- `build/images/leonos4.vmdk`
- `build/images/leonos4.iso`
- `build/images/leonos4-installer.iso`
- `build/install/root.fat`
- `build/images/esp.fat`
- `build/images/root.ext2` (default; FAT/exFAT cannot represent the current real symlinks)

The common system staging tree is:

- `build/esp`

It contains the Alpine-shaped root tree (`bin/`, `sbin/`, `lib/`, `usr/`,
`etc/leonos/`, `var/lib/leonos/`, `opt/`) plus the ESP-only loader, kernel and
middlelayer under `leonos/`. Help documents live in
`usr/share/doc/leonos/`; all application packages live in
`usr/lib/leonos/apps/`.
Vim and ncurses are enabled by default. `python3 build.py run vim` builds the
unmodified static Linux musl Vim and its ncurses dependency from pinned
submodules. Both `image-vmdk` and `installer` package Vim's runtime and the
ncurses terminfo database. `musl-desktop-vim` remains a compatible target name
for the standalone GRUB live desktop ISO, now using this same normal payload.
There is no dependency on `build/musl/vim-src` or other experimental downloads.
`python3 build.py run test-terminal-packages` checks actual Linux executables
on the host; it does not certify the kernel's whole Linux ABI.
The normal application set includes `login.elf` and `oshlp.elf`. Live media
contains an empty AUS2 database; the installer provisions the target accounts.

## Installer packaging

The installer has two related payload groups:

- Top-level ISO boot payload: loader, kernel, middlelayer, and installer root.
- Installed-system root payload: a copy of `build/esp` without `EFI/`,
  `grub/`, `loader.elf`, or `leonos/`, stored under `install/root` inside
  `build/install/root.fat`.
- Installed-system FAT32 ESP payload: the UEFI/GRUB and early loader files
  stored under `install/esp` inside `build/install/root.fat`.

`tools/make_installer_root.py` creates `build/install/root.fat`, copies the
normal staging tree into `/install/root` and its boot subset into
`/install/esp`, removes stale license override files from the root payload,
and overlays
policy-sensitive binaries built with `autoconf-installer.h`.

`tools/make_installer_iso.py` creates `build/images/leonos4-installer.iso` and
stages:

- `loader.elf`
- `leonos/kernel.sys`
- `leonos/middlelayer.sys`
- `install/root.fat`

This keeps installer boot and installed-system boot on the same matched
component set.

`install/root.fat` is one Multiboot module. It is mapped directly by
the kernel and is not copied into a second RAM buffer. The installer ISO mounts
this ext2 image (the historical filename is retained) as a writable live
environment: file and directory changes are
kept in the mapped RAM image for the session and discarded on reboot; they never
modify the ISO. The guest nevertheless
needs enough physical memory for GRUB to load the complete module before the
kernel begins. In particular, a 400 MiB installer root requires at least 1 GiB
of VM RAM; a 512 MiB VM can omit the module before the loader receives control.
If GRUB places that module over the kernel or middlelayer's fixed ELF load
range, the Loader relocates it into EFI LoaderData before loading either image;
the relocated range is reserved and mounted directly by the kernel.

For VirtualBox installation tests, attach the target VDI through a SATA/AHCI,
NVMe, or legacy PIIX4 IDE controller. NVMe namespaces use synchronous polling
queues and require 512-byte logical sectors. IDE/PATA disks use synchronous
PIO with LBA48 (and LBA28 fallback); IDE/ATAPI optical media is read-only and
can provide the Installer ISO.

For QEMU, the normal `python3 build.py run run` command keeps the AHCI topology. Set
`LEONOS_QEMU_IDE=1` when running `python3 build.py run run-iso` or a QMP smoke test to use
a PIIX3 IDE controller with the disk on primary master and the ISO on the
secondary channel.
Set `LEONOS_QEMU_NVME=1` to attach the same VMDK through a QEMU NVMe controller;
this is intentionally opt-in so normal QEMU runs retain the AHCI regression path.

The installer ISO GRUB menu provides the default graphical installer, an
`Install LeonOS 4 (TTY)` entry, and an `Install LeonOS 4 (Advanced mode, TTY
shell)` entry. The regular TTY entry starts `installer.elf` directly on the
console PTY. Advanced mode starts a BusyBox shell in the installer root instead,
with the LeonOS `fdisk`, `mkfs.fat`/`mkfs.fat32`, `mkfs.ext2`, `mkfs.exfat`,
`fsck.*`, `blkid`, `lsblk`, `mount`, `umount`, `sync`, and
`leonos-grub-installer` tools available for manual preparation. The
installer-only `/usr/lib/leonos/apps/gptinit/gptinit.elf` utility initializes an empty
GPT; `fdisk` can edit GPT partition type and name; `fsck.*` performs read-only
superblock validation. It does not start the installer application or perform
automatic partitioning.

The installer runtime includes `desktop.elf`, `installer.elf`, and
`/bin/busybox`. The installed-system root payload under
`/install/root/usr/lib/leonos/apps` contains the normal app set, including
`login.elf`. A fresh install boots directly into login using accounts created
on the installer's Accounts page. Copying the root payload manually does not
provision these accounts or the installed-system marker.

The policy-sensitive installed-system binaries are currently `desktop.elf`
and `settings.elf`. They link a libc build that uses
`autoconf-installer.h`, so disk files cannot turn off validation or redirect
the license server. To build an image without license validation, change the
corresponding source macro through Kconfig and regenerate/rebuild so the
generated binaries contain `LEONOS_LICENSE_REQUIRE 0`.

Installer update mode refreshes FAT32 ESP boot files from `/install/esp`, ext2
system files from `/install/root`, selected changed or missing application
packages under `/usr/lib/leonos/apps`, and bundled docs from
`/install/root/usr/share/doc/leonos`. The core update refreshes the real
`/bin`, `/sbin`, `/lib`, `/usr/bin`, `/usr/sbin`, `/usr/lib`, `/etc/leonos`,
`/opt` and `/var/lib/leonos` trees without deleting unrelated target files.
Third-party shared libraries such as `libmagic.so.1`, `liblua.so.5`, and
`sqlite.so.3` live in `/usr/lib`; private `libleonos.so.2` lives in
`/usr/lib/leonos`. It also copies `/usr/lib/leonos/kerneldebug.sys` from the
payload.
This keeps the dynamic loader, shared runtime, applications, and the built-in
kernel debugging module on one release version. Missing runtime directories or
the debug module on an older target are created during the update; the
installer payload itself must contain all of these files or the update is
rejected as incomplete.
Selected program packages are compared recursively and only shipped files that
are missing or changed are copied. This includes package metadata, licenses,
icons, headers, examples, and private runtime data; files added locally to an
installed package are left untouched.
Docs are merged: matching bundled `.hlp` files are overwritten under
`/target/usr/share/doc/leonos`, but extra third-party help files already
present there are kept. Update mode does not replace `/target/etc/leonos` or
`/target/var/lib/leonos`, so local machine state such as
`license.dat`, `users.db`, and the installed marker is preserved across an
installer-driven update. The machine ID is derived from detected machine
identity at runtime instead of being stored in `/etc/install.id`. The stable
identity source is SMBIOS System UUID when firmware provides it, otherwise the
boot GPT disk and ESP partition GUIDs. A fresh install copies the staged
`etc/leonos` and `var/lib/leonos` trees, then writes the chosen accounts before
publishing the ESP boot payload. Update supports only an existing non-usr-merge ext2 layout; old
pre-FHS installations need a fresh install. There is no migration/archive
stage. See `docs/ROOTFS_LAYOUT_AND_MIGRATION.md`.

All ext2 image producers require `fakeroot` and `e2fsprogs`. File ownership is
normalized to root:root inside fakeroot before mke2fs imports the payload;
this does not change host workspace ownership.

## WSL validation commands

For this checkout, validate Linux-targeted build and QEMU behavior through WSL.
Native Windows build-tool output is not the authoritative proof path.

Build the normal disk image and installer ISO:

```sh
wsl -e bash -lc 'cd "/mnt/d/Projects/C/LeonOS 4" && python3 build.py run image-vmdk && python3 build.py run installer'
```

For docs-only changes, a full image rebuild is normally unnecessary. At minimum
check whitespace and patch hygiene:

```sh
wsl -e bash -lc 'cd "/mnt/d/Projects/C/LeonOS 4" && git diff --check -- docs'
```
