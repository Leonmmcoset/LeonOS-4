# Build and Installer

## Source of truth

`tools/gen_ninja.py` generates the build graph. Treat `build.ninja` as
generated output and regenerate it when the graph changes.

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
runtime `/system/config/license.conf` server override.

## Main outputs

Common build outputs:

- `build/images/leonos4.vmdk`
- `build/images/leonos4.iso`
- `build/images/leonos4-installer.iso`
- `build/install/root.fat`
- `build/images/esp.fat`
- `build/images/root.ext2`

The common system staging tree is:

- `build/esp`

It contains the installed-system loader, kernel, middlelayer, resources,
configuration, bundled help documents under `docs/`, system application packages,
and application packages under `programs/`.
The normal application set includes `oobe.elf`, `login.elf`, and `oshlp.elf`;
the account database is intentionally not staged.

## Installer packaging

The installer has two related payload groups:

- Top-level ISO boot payload: loader, kernel, middlelayer, and installer root.
- Installed-system ext2 root payload: a copy of `build/esp` without `EFI/`,
  `boot/`, `system/kernel.sys`, or `system/middlelayer.sys`, stored under
  `install/root` inside `build/install/root.fat`.
- Installed-system FAT32 ESP payload: the UEFI/GRUB and early loader files
  stored under `install/esp` inside `build/install/root.fat`.

`tools/make_installer_root.py` creates `build/install/root.fat`, copies the
normal staging tree into `/install/root` and its boot subset into
`/install/esp`, removes stale license override files from the root payload,
and overlays
policy-sensitive binaries built with `autoconf-installer.h`.

`tools/make_installer_iso.py` creates `build/images/leonos4-installer.iso` and
stages:

- `boot/loader.elf`
- `system/kernel.sys`
- `system/middlelayer.sys`
- `install/root.fat`

This keeps installer boot and installed-system boot on the same matched
component set.

The installer runtime itself only needs `desktop.elf` and `installer.elf` under
`/system/apps`. The installed-system root payload under `/install/root/system/apps`
and `/install/root/programs` contains the normal app set, including `login.elf`
and `oobe.elf`, so a fresh
install boots into license OOBE and then first-administrator creation instead
of requiring pre-created accounts.

The policy-sensitive installed-system binaries are currently `desktop.elf`,
`oobe.elf`, and `settings.elf`. They link a libc build that uses
`autoconf-installer.h`, so disk files cannot turn off validation or redirect
the license server. To build an image without license validation, change the
corresponding source macro through Kconfig and regenerate/rebuild so the
generated binaries contain `LEONOS_LICENSE_REQUIRE 0`.

Installer update mode refreshes FAT32 ESP boot files from `/install/esp`, ext2
system files from `/install/root`, selected changed or missing `programs`
packages, and bundled docs from `/install/root/docs`. The core update replaces
`/target/system/lib` as a unit, including `ld-leonos.elf`, `libleonos.so.1`, and
versioned component libraries such as `libmagic.so.1`, `liblua.so.5`, and
`sqlite.so.3`. It also copies `/target/system/kerneldebug.sys` from the payload.
This keeps the dynamic loader, shared runtime, applications, and the built-in
kernel debugging module on one release version. Missing runtime directories or
the debug module on an older target are created during the update; the
installer payload itself must contain all of these files or the update is
rejected as incomplete.
Selected program packages are compared recursively and only shipped files that
are missing or changed are copied. This includes package metadata, licenses,
icons, headers, examples, and private runtime data; files added locally to an
installed package are left untouched.
Docs are merged: matching bundled `.hlp` files are overwritten, but extra
third-party help files already present on the target `/target/docs` are kept.
Update mode does not replace `/target/system/config` or `/target/system/state`, so local machine state such as
`license.dat`, `accounts.db`, and `oobe.done` is preserved across an
installer-driven update. The machine ID is derived from detected machine
identity at runtime instead of being stored in `/system/config/install.id`. The stable
identity source is SMBIOS System UUID when firmware provides it, otherwise the
boot GPT disk and ESP partition GUIDs. A fresh install formats and copies the
staged `system/config` tree and creates `system/state` instead, so it starts the license OOBE flow again.

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
