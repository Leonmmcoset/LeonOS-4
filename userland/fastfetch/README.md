# Fastfetch for LeonOS

The image ships Fastfetch 2.68.1 as a prebuilt native x86-64 static musl
executable, with the LeonOS logo included. Its Linux detection code uses
`uname`, `/etc/os-release`, `/proc`, and `/sys`; it does not link the LeonOS
adapter. The former submodule and adapter build have been removed.

The packager downloads the binary from
`https://github.com/VasilyZa/fastfetch/releases/download/2.68.1/fastfetch`
through `http://127.0.0.1:12334`. It pins SHA-256
`25107efd56d0286059487bab17d964a6ec72275263de2ab09095a46637b06be1`
and caches it in
`buildsystem/deps/fastfetch/fastfetch-2.68.1-leonos-x86_64-linux-musl`.
Subsequent builds use the validated cache without downloading again.
Downloads are verified before atomic publication; partial downloads and hash
mismatches are errors. The build never falls back to an older implementation.
`LEONOS_FASTFETCH_BINARY` can supply an offline copy with the same pinned hash.
Updating the release requires explicitly updating the URL and pinned hash.

```sh
python3 build.py run fastfetch
python3 build.py run installer
python3 build.py run test-fastfetch-package
python3 tools/test_linux_inventory.py --guest
```

`/usr/bin/fastfetch` resolves to the packaged executable at
`/usr/lib/leonos/apps/fastfetch/fastfetch.elf`. Both normal and installer image
staging use this same payload. `/etc/fastfetch/config.jsonc` selects the built-in
`LeonOS` logo by default. Users can override the logo with
`fastfetch --logo LeonOS`, another upstream logo, or their own configuration.
The component's MIT license is installed in `/usr/share/licenses/fastfetch`.
Its `package.json` records the release URL, version, and binary hash.

The Kernel row reports NTCLKS metadata: `uname().sysname` is the kernel's
`ntclks` name, `release` is the generated `4.6.2-<build>` kernel version, and
`version` is its build time. The corresponding procfs files expose the same
values. LeonOS distribution identity remains in `/etc/os-release` for the OS
row. No application-side output substitution is used.

The guest inventory test checks the binary actually staged at
`build/esp/usr/bin/fastfetch`, including Kernel, hardware and resource JSON,
and execution through Desktop Terminal. Passing this subset does not certify
every upstream module or VMware behavior.

Validation on 2026-09-11 (kernel build 3523):

- A fresh cache downloaded and verified the pinned GitHub release through the
  proxy. All four packaging tests and the component configuration checks passed.
- The installer build completed with zero errors. Its installed payload and
  normal image staging both contain the pinned binary, configuration, license,
  and release manifest.
- Host ASan/UBSan inventory checks and QEMU/KVM inventory checks passed. Default
  output includes the LeonOS logo, OS and Memory rows, and
  `Kernel: ntclks 4.6.2-3523`. Console and Desktop Terminal JSON agree with
  `uname` and procfs; the guest reported `[inventory] DONE failures=0`.
  QEMU configured two CPUs, but the kernel enabled only one; this is not an SMP
  validation. Evidence is in `build/linux-inventory/`.
- Fastfetch license packaging checks passed for both staging trees. The full
  repository license check still reports three unrelated failures: Lua's SDK
  upstream license lookup and the two PortableGL SDK library notices.
- VMware was not tested.
