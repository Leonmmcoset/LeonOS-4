# Official BusyBox and Storage Tools

## Status and Scope

The production build now packages unmodified upstream BusyBox, util-linux,
e2fsprogs, dosfstools and exfatprogs. Former private storage commands are no
longer dispatched as BusyBox applets. The project-specific boot-payload copier
is a separate script, explicitly not GNU `grub-install`.

Both production root targets have built successfully:

- `build/live/root.ext2`: normal Live root.
- `build/install/root.fat`: ext2 despite its historical filename; contains the
  installer runtime root and the installed-system payload at `/install/root`.

Actual embedded tool/library bytes and compatibility links are checked with
`debugfs`, not inferred from staging success. This is **not full guest functional
acceptance**. QEMU testing exposes outstanding filesystem I/O, sysfs inventory,
and mounting limitations described below. No successful full installation,
physical-disk formatting, or VMware run is claimed. Ordinary and installer ISOs
were rebuilt by `run images-iso` after the upstream-tool and init changes;
VMDK was not rebuilt.

The earlier kernel compilation failure was resolved by replacing an undeclared
`memcpy` with `__builtin_memcpy`, matching existing local code.
The kernel and root-image builds now pass that compilation gate.
Broader Linux ABI work remains paused. The existing storage change stops logging
pending asynchronous DMA (`-EAGAIN`) as a device error and retains real errors;
its host regression passes. This does not establish VMware transport stability.

## Sources and Linkage

### BusyBox

- Upstream [BusyBox](https://busybox.net/), tag `1_36_1`, commit
  `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4`.
- The production recipe passes `--official-source`, exports committed bytes using
  `git archive`, and verifies the export before and after `make`. Dirty checkout
  files do not enter the official build; injected or changed cache files fail
  verification. No upstream source file is patched.
- The selected revision is the checkout's HEAD, recorded in the stamp; changing
  HEAD changes the input. This is not an independent signature-authentication
  mechanism. Build time is pinned to that commit via `SOURCE_DATE_EPOCH` and UTC,
  so separate root builds no longer differ only in BusyBox's timestamp banner.
- Private `CONFIG_LEONOS_*` and `CONFIG_EXTRA_LDLIBS` are filtered. Official mode
  neither injects private SDK headers nor links `libleonos.a`. The legacy adapted
  builder branch and old private source files remain in the repository, but the
  standard graph does not use them.
- BusyBox is static against the existing project musl runtime and its mimalloc
  startup object. **Unmodified application sources do not imply an unmodified
  C runtime.** This task does not replace musl or the allocator.
- `build/userland/busybox.stamp` records source, config, build flags, applets and
  ELF SHA256. Its conservative `guest_verified: false` is not promoted by a
  limited shell-pipeline test.

### util-linux

- Official [util-linux 2.41.6 archive](https://www.kernel.org/pub/linux/utils/util-linux/v2.41/util-linux-2.41.6.tar.xz).
- SHA256: `e596083744e746be7d2823b62b43f4418dd7bf56303b4dc09e6fe8112fe3d7ed`.
- `--enable-fdisks=check` builds fdisk and sfdisk; cfdisk is skipped without a
  terminal library. The profile also enables mount/umount, blkid, lsblk, fsck,
  libmount, libblkid, libfdisk, libsmartcols, libuuid, su and runuser.
- Builds are out-of-tree. Source verification was not relaxed for generated
  files. An earlier exploratory in-source `config.log` was moved to
  `build/auth-upstream/diagnostic-source-config.log`, outside the verified tree.
- 2.41.6's `hook_idmap.c` uses `RESOLVE_NO_SYMLINKS` without its defining header.
  `CPPFLAGS=-include linux/openat2.h` supplies the official target Linux UAPI
  definitions to all translation units without patching source or disabling
  idmapped mounts. The actual constant comes from Linux headers, not a private
  replacement header.
- The compiler places target musl libraries before libtool's install-time
  `/usr/lib` search. Tests inspect the installed ELFs to reject host glibc and
  private-library dependencies, not just the pre-install executables.
- Executables use `/lib/ld-musl-x86_64.so.1`; shared libraries live in `/usr/lib`.
  Build metadata: `build/auth-upstream/util-linux-build.json`.

### Filesystem Tools

Pinned official archives, SHA256 values and checksum provenance are in
`configs/storage-upstream.json`:

| Package | Version | Commands |
| --- | --- | --- |
| [e2fsprogs](https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.3/) | 1.47.3 | mkfs.ext2, fsck.ext2 |
| [dosfstools](https://github.com/dosfstools/dosfstools/releases/tag/v4.2) | 4.2 | mkfs.fat, fsck.fat |
| [exfatprogs](https://github.com/exfatprogs/exfatprogs/releases/tag/1.4.3) | 1.4.3 | mkfs.exfat, fsck.exfat |

The builder verifies archives and extracted source trees, configures out-of-tree,
and checks source integrity again after installation. No source patches apply.
The six core tools are static target ELFs, using target util-linux UUID/blkid
archives where needed. exfatprogs uses libtool `-all-static` and an explicit
`libblkid.a` path to avoid installed `.la` host paths. Package licenses are staged
under `/usr/share/licenses`; build commands and hashes are recorded under
`build/storage-upstream/`, including `root/.storage-package.json`.

## Command and Rootfs Ownership

| Commands | Canonical location | Owner |
| --- | --- | --- |
| fdisk, sfdisk, blkid, fsck, runuser | `/usr/sbin` | util-linux via `esp:auth` |
| mount, umount, lsblk | `/bin` | util-linux via `esp:auth` |
| mkfs.ext2, fsck.ext2, mkfs.fat, fsck.fat, mkfs.exfat, fsck.exfat | `/usr/sbin` | official filesystem packages via `esp:storage` |
| leonos-grub-installer | `/usr/sbin` | project shell script via `esp:storage` |
| sync, shell and selected standard applets | `/bin/busybox` | official BusyBox |

`tools/storage_tools.py` declares package outputs and old `/sbin` aliases.
`esp:layout-links` runs after producers and replaces stale BusyBox storage links.
`mkfs.vfat`/`mkfs.fat32` resolve to `mkfs.fat`; `fsck.vfat`/`fsck.fat32` resolve to
`fsck.fat`. These are path aliases, not translations of the private CLI. Use
upstream options: in particular **FAT32 requires `-F 32`**, including when using
the `mkfs.fat32` alias; dosfstools otherwise selects FAT width by volume size.
The former private `--force` syntax is not emulated.

ChenPi cmd resolves storage commands directly to those external paths.
`/bin`, `/sbin`, `/lib`, `/usr/bin`, `/usr/sbin` remain real separate directories;
this is not a usr-merge. Normal, installer-runtime and installed-payload roots
consume the same staging tree. Repeated storage staging replaces owned files
before copying, preserving e2fsprogs' read-only archive modes without failing on
the second build.

Before copying util-linux, staging checks every required command and library
against the package root. A missing library or a link escaping that root fails
before copying; an old staging library cannot satisfy the check.

Upstream BusyBox power commands normally signal PID 1. LeonOS init blocks and
synchronously receives SIGUSR1 (halt), SIGUSR2 (poweroff) and SIGTERM (reboot),
calls sync and the real reboot interface, and stays alive if that call fails.
It also waits for SIGCHLD and reaps exited children. This preserves unmodified
BusyBox command behavior without converting commands to forced reboot wrappers.
This is not a service shutdown supervisor: orderly termination of every service
before power transition remains unimplemented.

The boot copier validates its input files and destination directory, then copies
`EFI/BOOT/BOOTX64.EFI`, `loader.elf`, `leonos/kernel.sys`, `leonos/middlelayer.sys`
and the complete `grub` directory. Default source is `/install/esp`; `--source DIR`
supports an explicit prebuilt payload. It does not generate GRUB, format/mount an
ESP, install boot sectors, or update NVRAM. The supplied destination must already
be the intended mounted ESP. Tests cover spaces, exact bytes, absent destination,
missing/partial source and nonzero failure reporting.

The graphical/TTY installer still uses its existing native shared block/format/
mount implementation. This task does not redesign or validate that workflow.

## Verification

```sh
python3 build.py run images-iso
python3 build.py run installer-root
python3 build.py run desktop-live-root
python3 tools/test_upstream_tools.py -v
python3 tools/test_auth_source_integrity.py -v
python3 tools/test_storage_payload.py -v
python3 tools/test_upstream_tools_runtime.py -v
python3 tools/test_storage_upstream_runtime.py -v
python3 tools/test_upstream_tools_images.py -v
LEONOS_STORAGE_TEST_ROOT=build/esp LEONOS_UPSTREAM_TEST_ROOT=build/esp \
  python3 tools/test_storage_upstream_runtime.py -v
python3 tools/test_storage_upstream_guest.py --root build/install/root.fat
python3 tools/test_storage_upstream_guest.py --root build/live/root.ext2
python3 tools/test_init_power.py
python3 tools/test_file_runtime.py
python3 tools/test_storage_upstream_guest.py --root build/live/root.ext2 --power reboot
python3 tools/test_storage_upstream_guest.py --root build/live/root.ext2 --power poweroff
```

Host reference tests execute the actual target ELFs with explicit target library
search paths. They create GPT and ext2/FAT32/exFAT only on disposable regular
files; checkers must reject unformatted files. Host mount tests are version and
read-only listing tests. None of these tests writes a host block device.

Image tests compare 20 embedded commands/libraries at each of the three root
locations and verify the declared compatibility links. The final combined build
log is `build/upstream-tools-final-build.log` (0 errors). Artifacts:
`build/images/leonos4.iso` and `build/images/leonos4-installer.iso`.
The root payload extracted from each ISO matches the corresponding tested root
image byte-for-byte by SHA256. ISO SHA256 values for this build:

| ISO | SHA256 |
| --- | --- |
| `leonos4.iso` | `739327e18e646cec56ae325bc871426cab9fe88c7ac3383263ffc16f3a97a8c8` |
| `leonos4-installer.iso` | `ddf28be0f16d38233817065be038c5de7137c2195dd80e98ddb35c6e70956b9d` |

Guest probes copy a production root, add only a test executable at the existing
inventory autospawn slot, then build a diagnostic ISO. They do not patch the
kernel or replace production tools. QEMU has no host block devices attached.
The runner records base-image SHA256, invocation, complete serial output and
failures under `build/storage-upstream-guest/{install,live}/` and returns nonzero
for failures; passing individual commands does not set package-wide acceptance.

The dedicated power probes pass with the rebuilt Live root:
`--power reboot` receives `SHUTDOWN reason=guest-reset`, and `--power poweroff`
receives `SHUTDOWN reason=guest-shutdown`. Both serial logs record the expected
PID 1 `reboot(2)` command. Evidence is under the `live-reboot` and `live-poweroff`
subdirectories. The host init harness separately covers halt/reboot/poweroff
signal mappings, child reaping and failed reboot without invoking host power.

The inventory autospawn slot can execute before init finishes runtime startup.
Power probes wait for the power signals to be blocked in `/proc/1/stat` before
executing BusyBox. An earlier immediate request terminated PID 1 with SIGTERM:
the kernel's missing early PID 1 default-signal protection remains a limitation.
These passing post-initialization tests do not certify that early-boot case.

### Observed Guest Limitations

The final installer and live root probes each complete with 20 passing checks
and seven failed checks, not a hang. BusyBox pipelines, `file` ELF/text detection
without magic warnings and missing-input errors, boot-payload copy/rejection,
fdisk version and GPT write/read, mount version/listing, fsck dry-run dispatch,
FAT32 formatting/identification and exFAT formatting/checking/identification pass.
The pass count includes four disposable-file creation checks. Host `file` tests
also pass with an injected 32768-byte read limit; that injection is host-only.

- `lsblk`: `/sys/dev/block` is missing. Host inventory success does not establish
  LeonOS sysfs compatibility.
- `mkfs.ext2`: short writes while clearing blocks/inode tables. Subsequent
  `fsck.ext2` and `blkid` cannot validate the failed filesystem creation; these
  are not independent evidence that their ext2 parsers are broken.
- `fsck.fat`: received 32768 bytes instead of the requested 516096 bytes. The
  formatter exit code and blkid result do not prove the FAT volume is healthy.
- `mount -t tmpfs tmpfs ...`: reports that special device `tmpfs` does not exist.
  The following umount fails because no mount was created. No actual mount/
  unmount workflow has been accepted.

These failures remain explicit, without changed expectations or private tool
fallbacks. See the per-root `result.json` and `serial.log` for the final
rerun results, including the project boot-copier checks.
