# Third-Party Code

LeonOS keeps third-party source code in `third_party/` and records Git-backed
dependencies as submodules.

## Mbed TLS

- Path: `third_party/mbedtls`
- Upstream: `https://github.com/Mbed-TLS/mbedtls.git`
- Version: `2.28.8`
- Git submodule commit: `5a764e5555c64337ed17444410269ff21cb617b1` (`v2.28.8`)
- License: Apache-2.0 (selected from the upstream dual Apache-2.0 or
  GPL-2.0-or-later terms; see `third_party/mbedtls/LICENSE`).

LeonOS builds a TLS 1.2 client profile with certificate and hostname
verification for the shared HTTP client. The system image includes
`0:/system/certs/cacert.pem`, the curl CA Extract from
`https://curl.se/ca/cacert.pem`, to establish public Web PKI trust.

## Picolibc

- Path: `third_party/picolibc`
- Upstream: `https://github.com/picolibc/picolibc.git`
- LeonOS fork: `https://github.com/Leonmmcoset/LeonOS-4-picolibc.git`
- Version: `1.8.12`
- Pinned commit: `2fca8654025d367b3da4699a82c347840123bcd2`
- License: BSD; preserve the complete upstream attribution and license notices
  in `third_party/picolibc/COPYING.picolibc`.

LeonOS builds Picolibc as its x86_64 freestanding user-space ISO C library.
The LeonOS syscall and GUI bindings remain in the separate `leonos.a` adapter
archive. The generated Developer SDK packages both archives, Picolibc headers,
and the upstream license notice.

## zlib

- Path: `third_party/zlib`
- Upstream: `https://github.com/madler/zlib.git`
- Version: `1.3.2`
- Pinned commit: `da607da739fa6047df13e66a2af6b8bec7c2a498` (`v1.3.2`)
- License: zlib License; preserve `third_party/zlib/LICENSE`.

LeonOS builds zlib's freestanding in-memory compression/decompression core as
`libz.a`.  Gzip file-stream helpers are intentionally excluded because LeonOS
does not provide a hosted stdio file backend to the library.

## libpng

- Path: `third_party/libpng`
- Upstream: `https://github.com/pnggroup/libpng.git`
- Version: `1.6.58`
- Pinned commit: `3061454d980de7d53608f594194cfac722721d2a` (`v1.6.58`)
- License: libpng License; preserve `third_party/libpng/LICENSE`.

LeonOS builds libpng as `libpng.a` against its bundled zlib.  The public SDK
includes both upstream libraries and headers, while `leonos/png.h` provides a
bounded PNG-to-LeonOS-pixel decoder for ordinary GUI applications.

## BusyBox

- Path: `third_party/busybox`
- Upstream: `https://github.com/mirror/busybox.git`
- Version: `1.36.1`
- Pinned commit: `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4` (`1_36_1`)
- License: GPL-2.0-only; the complete upstream `LICENSE` is staged at
  `0:/programs/busybox/LICENSE` beside the executable.

LeonOS builds a static, basic-applet BusyBox profile at
`0:/programs/busybox/busybox.elf`. It includes file/text utilities such as
`ls`, `pwd`, `cat`, `echo`, `head`, `tail`, `wc`, `mkdir`, `rmdir`, `cp`, `mv`,
`rm`, `unlink`, `printenv`, `uname`, `sleep`, `true`, `false`, `vi`, and
`printf`. BusyBox ash is intentionally excluded until LeonOS has fork, pipe,
file-descriptor duplication, process-group, and signal semantics; the current
shell entry point is the no-fork Hush profile.

## GNU nano

- Path: `third_party/nano`
- Upstream: `https://git.savannah.gnu.org/git/nano.git`
- Version: `9.2`
- Pinned commit: `8e6360d1663998c62ddd0cf934923d1f18004e3e` (`v9.2`)
- License: GPL-3.0-or-later; the complete upstream `COPYING` is staged at
  `0:/programs/nano/COPYING` beside the executable.

LeonOS builds Nano at `0:/programs/nano/nano.elf` with a narrow ANSI curses
compatibility layer over the GUI terminal PTY. This initial port intentionally
uses Nano's single-buffer tiny profile: the core editor path is present, while
external spellers/formatters, rc files, syntax coloring, help pages, mouse
input and multi-buffer support remain off. Interactive editing and persistence
still require manual GUI-terminal validation on each supported VM platform.

## TinyCC

- Path: `third_party/tinycc`
- Upstream: `https://repo.or.cz/tinycc.git`
- Version: `0.9.28rc`
- Pinned commit: `2be0218be4461f9e453cbdcb98f81f41a5ae8bed`
- License: LGPL-2.1-or-later; the complete upstream `COPYING` is staged at
  `0:/programs/tcc/COPYING` beside the executable and runtime files.

LeonOS builds TinyCC as the static, on-device x86_64 C compiler at
`0:/programs/tcc/tcc.elf`. It uses the installed Picolibc headers,
`libleonos.a`, `libpicolibc.a`, LeonOS `crt0.o`, the target support archive
`libleonos-tcc-rt.a`, and TinyCC's `libtcc1.a` to produce normal static LeonOS
ELF programs. Picolibc headers are staged unchanged; LeonOS ABI predefines are
owned by TinyCC's target definition layer. Dynamic linking, shared libraries,
PIE and in-memory `tcc -run` execution are deliberately unavailable until the
runtime loader ABI exists. The target runtime currently reports `ENOSYS` for
`times()` and `SIG_ERR` for `signal()` because those LeonOS ABIs do not yet
exist.

## Lua

- Path: `third_party/lua`
- Upstream: `https://github.com/lua/lua.git`
- Version: `5.4.8`
- Pinned commit: `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (`v5.4.8`)
- License: MIT; the LeonOS copy of the complete upstream license is staged at
  `0:/programs/lua/LICENSE` beside the executable.

LeonOS builds Lua as the static command-line interpreter at
`0:/programs/lua/lua.elf`. It uses Lua's portable C89 configuration with
Picolibc and the LeonOS adapter archive. Dynamic C modules and `package.loadlib`
are intentionally unavailable until the system has a dynamic-loader ABI. Lua
scripts can be loaded from the current directory or from
`0:/programs/lua/lua/`.

## PL Editor

- Path: `third_party/pl_editor`
- Upstream: `https://github.com/plos-clan/pl_editor.git`
- Pinned commit: `15435b8aab125537421d57c6220e3b8a0c827787`
- License: MIT; the complete upstream `LICENSE` is staged at
  `0:/programs/pleditor/LICENSE` beside the executable.

LeonOS builds PL Editor at `0:/programs/pleditor/pleditor.elf`. Its upstream
platform-independent editor core is kept as a submodule; the LeonOS platform
adapter provides raw PTY input, ANSI terminal output, terminal sizing and
file persistence. It is launched through Terminal and supports syntax
highlighting, search, undo/redo and line numbers.

## minimp3

- Path: `third_party/minimp3/minimp3.h`
- Upstream: `https://github.com/lieff/minimp3`
- Pinned commit: `ea99364f61c14656440e8d77e9c233ccf3124633`
- License: CC0-1.0; upstream declares the source public domain dedication and
  warranty disclaimer in the vendored header.

## GNU GRUB

- Delivered component: UEFI and Multiboot2 boot support in normal and installer
  media.
- Upstream: `https://www.gnu.org/software/grub/`
- License: GNU General Public License version 3 or later (GPL-3.0-or-later).

## litehtml

- Path: `third_party/litehtml`
- Upstream: `https://github.com/litehtml/litehtml.git`
- Recorded commit: `b9e89f0b9494ff9a5f008800af35503efabddf59`
- License: New BSD License / BSD-3-Clause, see `third_party/litehtml/LICENSE`

`browser.elf` does not yet link upstream litehtml directly because LeonOS
userland is still freestanding C without a C++ runtime or STL. The browser now
uses `userland/apps/browser/litehtml_core.c` as the staged C document layout
core. That keeps the browser shell, network loading, history, and GUI wiring
ready for a later full litehtml container.

## Gumbo HTML Parser

- Path: `third_party/litehtml/src/gumbo`
- Upstream: `https://github.com/google/gumbo-parser`
- License: Apache License 2.0, see `third_party/litehtml/src/gumbo/LICENSE`.

Gumbo source is present through the litehtml submodule. It is not built or
executed by the current LeonOS browser path.

## Noto Sans Mono

- Derived resource: `system/fonts/metro-latin.lbf`, generated by
  `tools/make_metro_latin_font.py` from Noto Sans Mono Regular at 13 px.
- Upstream: `https://fonts.google.com/noto/specimen/Noto+Sans+Mono`
- License: SIL Open Font License 1.1.

## Microsoft Fonts

- Resources: `system/fonts/Deng.ttf`, `system/fonts/times.ttf`, and
  `system/fonts/simsun.ttc`.
- Purpose: deterministic local and CI font packaging; the browser uses Times
  New Roman for Latin text and SimSun as its Chinese fallback.

## CJK Font Generation Chain

- Derived resource: `system/fonts/cjk16.lbf`, generated by
  `tools/make_cjk16_font.py`.
- Primary font: Droid Sans Fallback, Apache License 2.0.
- Fallback font: Noto Sans CJK, SIL Open Font License 1.1.

The font generator uses this configured chain when regenerating the CJK glyph
resource; the runtime ships only the generated 16x16 bitmap resource.

## Metro Desktop Wallpaper

- Derived resource: `system/resources/wallpaper-metro.bmp`.
- Source: NASA Image and Video Library asset PIA18033,
  `https://images.nasa.gov/details-PIA18033`.
- Credit and usage: NASA; see `system/resources/wallpaper-metro.source.txt`
  and NASA media usage guidelines.
