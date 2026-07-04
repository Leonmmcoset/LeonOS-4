# Build and Installer

## Source of truth

`tools/gen_ninja.py` generates the build graph. Treat `build.ninja` as
generated output and regenerate it when the graph changes.

Important generated files include:

- `include/generated/autoconf.h`
- `include/generated/build_info.h`
- `include/generated/loader_integrity.h`
- `build/rustcfg.args`

## Main outputs

Common build outputs:

- `build/images/leonos4.vmdk`
- `build/images/leonos4.iso`
- `build/images/leonos4-installer.iso`
- `build/install/root.fat`

The runtime ESP staging tree is:

- `build/esp`

It contains the installed-system loader, kernel, middlelayer, resources,
configuration, and userland applications. The normal application set includes
`oobe.elf` and `login.elf`; the account database is intentionally not staged.

## Installer packaging

The installer has two related payloads:

- Top-level ISO boot payload: loader, kernel, middlelayer, and installer root.
- Installed-system payload: a copy of `build/esp` stored under `install/esp`
  inside `build/install/root.fat`.

`tools/make_installer_root.py` creates `build/install/root.fat` and copies the
normal ESP tree into the installer runtime at `0:/install/esp`.

`tools/make_installer_iso.py` creates `build/images/leonos4-installer.iso` and
stages:

- `boot/loader.elf`
- `system/kernel.sys`
- `system/middlelayer.sys`
- `install/root.fat`

This keeps installer boot and installed-system boot on the same matched
component set.

The installer runtime itself only needs `desktop.elf` and `installer.elf` under
`0:/userland`. The installed-system payload under `0:/install/esp/userland`
contains the normal app set, including `login.elf` and `oobe.elf`, so a fresh
install boots into OOBE instead of requiring pre-created accounts.

## WSL validation commands

For this checkout, validate Linux-targeted build and QEMU behavior through WSL.
Native Windows build-tool output is not the authoritative proof path.

Regenerate the graph:

```sh
wsl -e bash -lc 'cd "/mnt/d/Projects/C/LeonOS 4" && python3 tools/gen_ninja.py --out build.ninja'
```

Build the normal disk image and installer ISO:

```sh
wsl -e bash -lc 'cd "/mnt/d/Projects/C/LeonOS 4" && ninja -f build.ninja build/images/leonos4.vmdk build/images/leonos4-installer.iso'
```

For docs-only changes, a full image rebuild is normally unnecessary. At minimum
check whitespace and patch hygiene:

```sh
wsl -e bash -lc 'cd "/mnt/d/Projects/C/LeonOS 4" && git diff --check -- docs'
```
