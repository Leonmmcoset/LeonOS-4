# Third-Party Code

LeonOS keeps third-party source code in `third_party/` and records Git-backed
dependencies as submodules.

## Prebuilt Native Toolchain

The default `musl-gcc` component packages the unchanged x86-64 archive from
[Dyne musl 2.2.0](https://github.com/dyne/musl/releases/tag/2.2.0): GCC 15.1.0,
binutils 2.44, musl headers/static libraries, libgcc and libstdc++, plus the
upstream supplementary sysroot. SHA256:
`31420e4f978e7ccbcc597ca5e18c2dcbe640ea6985ed6325112d8a171a72ece3`.
GCC/binutils use GPL-3.0-or-later; applicable GCC runtime libraries include the
GCC Runtime Library Exception. Musl uses MIT. Bundled library notices remain
inside `/opt/dyne`; compiler license texts and file hashes are installed under
`/usr/share/licenses/musl-gcc`. See `userland/musl-gcc/README.md` for source/build
recipe references, cache configuration and unchanged-binary verification.

## Prebuilt Python Runtime

The default `python` component includes the user-provided static musl CPython
3.14.7 archive `cpython-3.14.7+20260901-x86_64-unknown-linux-musl-lto+static-full.tar.zst`.
SHA256: `e5a76e5893c39c89ed268ad71c3d6794c3be6b7236ec613449140c57686fc17f`.
The unmodified executable and full installation are in `/opt/python`.
The archive's CPython and bundled dependency license texts, `PYTHON.json`, and
package hashes are installed at `/usr/share/licenses/python`. CPython's license
is recorded in `LICENSE.cpython.txt`; other bundled libraries retain their
own notices. See `userland/python/README.md` for reproducible packaging and
the static command launcher. This replaces the temporary 3.15 runtime in
default images; it does not certify every Python module's Linux ABI usage.

## Git Submodule Inventory

The following inventory covers every Git submodule declared by the root
`.gitmodules` file, plus the nested submodule declared by StardustUI. The
commits are the revisions recorded by the LeonOS checkout.

| Path | Upstream | Pinned commit |
| --- | --- | --- |
| `devtools/components/lua/upstream` | `https://github.com/lua/lua.git` | `6e22fedb74cf0c9b6656e9fce8b7331db847c605` |
| `third_party/busybox` | `https://github.com/mirror/busybox.git` | `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4` |
| `third_party/cmd` | `https://github.com/ChenPi11/cmd.git` | `2290c38bc9da54db53aa56161a7204a27b388e21` |
| `third_party/fastfetch` | `https://github.com/fastfetch-cli/fastfetch.git` | `56da8f811068289f6352db8881418aa6e0f994e8` |
| `third_party/file` | `https://github.com/file/file.git` | `711ccc264519cdc5073ccb26651c0a9bafc3b47a` |
| `third_party/less` | `https://github.com/gwsw/less.git` | `b8bbf4297169e20d35e1cc3e015180e8a011bcf2` |
| `third_party/libpng` | `https://github.com/pnggroup/libpng.git` | `3061454d980de7d53608f594194cfac722721d2a` |
| `third_party/litehtml` | `https://github.com/litehtml/litehtml.git` | `b9e89f0b9494ff9a5f008800af35503efabddf59` |
| `third_party/lua` | `https://github.com/lua/lua.git` | `6e22fedb74cf0c9b6656e9fce8b7331db847c605` |
| `third_party/mbedtls` | `https://github.com/Mbed-TLS/mbedtls.git` | `5a764e5555c64337ed17444410269ff21cb617b1` |
| `third_party/nano` | `https://git.savannah.gnu.org/git/nano.git` | `8e6360d1663998c62ddd0cf934923d1f18004e3e` |
| `third_party/vim` | `https://github.com/vim/vim.git` | `af9a7a04f18693eee4400dd134135527f4e8cd5f` |
| `third_party/ncurses` | `https://github.com/ThomasDickey/ncurses-snapshots.git` | `0096bd402c4a9c8f39bd7ed266e1b8920327e4d8` |
| `third_party/musl` | `https://git.musl-libc.org/git/musl` | `9fa28ece75d8a2191de7c5bb53bed224c5947417` |
| `third_party/mimalloc` | `https://github.com/microsoft/mimalloc` | `34fbd7e7cd4627424490afe19b20f8066bfc537d` |
| `third_party/pl_editor` | `https://github.com/Leonmmcoset/pl_editor.git` | `22fae7a1bc2362486d8bf845f0daf6ec7060a3a1` |
| `third_party/sl` | `https://github.com/mtoyoda/sl.git` | `923e7d7ebc5c1f009755bdeb789ac25658ccce03` |
| `third_party/sqlite` | `https://github.com/sqlite/sqlite.git` | `f3d536d37825302e31ed0eddd811c689f38f85a3` |
| `third_party/stardustui` | `https://github.com/xingji-studio/StardustUI.git` | `67aae17214a0d27bb6a8b0caf10b7c1f98313086` |
| `third_party/stardustui/third_party/ab_glyph_rasterizer/upstream` | `https://github.com/alexheretic/ab-glyph.git` | `791b15214d376dec06ae1c886da4c5f92f31e2e0` |
| `third_party/tinycc` | `https://github.com/TinyCC/tinycc.git` | `2ba12e83b3599ca8f5d50c179fe5138fe956f0c9` |
| `third_party/zlib` | `https://github.com/madler/zlib.git` | `da607da739fa6047df13e66a2af6b8bec7c2a498` |

## Mbed TLS

- Path: `third_party/mbedtls`
- Upstream: `https://github.com/Mbed-TLS/mbedtls.git`
- Version: `2.28.8`
- Git submodule commit: `5a764e5555c64337ed17444410269ff21cb617b1` (`v2.28.8`)
- License: Apache-2.0 (selected from the upstream dual Apache-2.0 or
  GPL-2.0-or-later terms; see `third_party/mbedtls/LICENSE`).

LeonOS builds a TLS 1.2 client profile with certificate and hostname
verification for the shared HTTP client. The system image includes
`/etc/ssl/certs/ca-certificates.crt`, the curl CA Extract from
`https://curl.se/ca/cacert.pem`, to establish public Web PKI trust.

## musl and mimalloc

The default userland uses unmodified musl 1.2.6 for Linux x86-64 C/POSIX,
TLS, pthreads, startup and dynamic linking. mimalloc 3.5.1 provides application
allocation. License texts are in `third_party/musl/COPYRIGHT` and
`third_party/mimalloc/LICENSE` and ship in images and SDKs.
`libleonos.so.2` and `libleonos.a` provide LeonOS extensions; they contain no
replacement standard POSIX implementation. Legacy binaries must be rebuilt.
See `MUSL_MIGRATION_2026-09-08.md` for exact validation and remaining gaps.

## StardustUI

- Path: `third_party/stardustui`
- Upstream: `https://github.com/xingji-studio/StardustUI.git`
- Pinned commit: `67aae17214a0d27bb6a8b0caf10b7c1f98313086`
- License: MIT; preserve `third_party/stardustui/LICENSE`. The generated SDK
  includes the library's public headers, LeonOS C++ compatibility headers and
  the full license text.

LeonOS builds StardustUI as `libstardustui.a` over its existing pixel-buffer
window ABI and UI text renderer. The image includes the upstream Hello World,
layout and widget-showcase examples at `/usr/lib/leonos/apps/stardusthello/`,
`/usr/lib/leonos/apps/stardustlayout/` and `/usr/lib/leonos/apps/stardustshowcase/`, plus the
upstream Material 3 example themes in `/etc/stardustui/theme/`. StardustUI's
socket API is linked but currently reports that networking is unavailable, so
the network-dependent DuckChat example is intentionally not installed.

StardustUI also records one nested dependency:

- Path: `third_party/stardustui/third_party/ab_glyph_rasterizer/upstream`
- Upstream: `https://github.com/alexheretic/ab-glyph.git`
- Pinned commit: `791b15214d376dec06ae1c886da4c5f92f31e2e0`
- Crate: `ab_glyph_rasterizer` 0.1.10
- License: Apache-2.0; preserve the upstream license and attribution files.

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

## SQLite

- Path: `third_party/sqlite`
- Upstream: `https://github.com/sqlite/sqlite.git`
- Version: 3.46.1; pinned commit:
  `f3d536d37825302e31ed0eddd811c689f38f85a3`
- License: SQLite public domain dedication and blessing; preserve
  `third_party/sqlite/LICENSE.md`.

LeonOS installs the ABI-v1 shared library as `/usr/lib/sqlite.so.3` and
packages `sqlite3.h` in the SDK. The port uses a LeonOS VFS and currently
disables WAL, loadable extensions, and cross-process file locking.

## BusyBox

- Path: `third_party/busybox`
- Upstream: `https://github.com/mirror/busybox.git`
- Version: `1.36.1`
- Pinned commit: `1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4` (`1_36_1`)
- License: GPL-2.0-only; the complete upstream `LICENSE` is staged at
  `/usr/lib/leonos/apps/busybox/LICENSE` beside the executable.

LeonOS builds a static, basic-applet BusyBox profile at
`/bin/busybox`. It includes file/text utilities such as
`ls`, `pwd`, `cat`, `echo`, `head`, `tail`, `wc`, `diff`, `less`, `mkdir`,
`rmdir`, `cp`, `mv`, `rm`, `unlink`, `printenv`, `uname`, `sleep`, `true`,
`false`, `nohup`, `vi`, and `printf`. The `sh` entry point is BusyBox Ash built for
LeonOS's MMU path. It uses the kernel COW `fork`/`execve` ABI, inherited file
descriptors, process groups and PTY foreground groups for pipelines,
redirection, background jobs, and `jobs`/`fg`/`bg`. The image profile selects
Ash as its shell implementation. `nohup` is available from both the GUI
Terminal and TTY shell; it ignores `SIGHUP`, uses `/dev/null` for terminal
stdin, and appends terminal output to `nohup.out` with the usual `$HOME`
fallback.

The profile also provides LeonOS storage applets: `fdisk` (including GPT type
and name editing), `mkfs.fat`/`mkfs.fat32` (with `mkfs.vfat` as an alias),
`mkfs.ext2`, `mkfs.exfat`, read-only `fsck.*`, `blkid`, `lsblk`, `mount`,
`umount`, `sync`, and `leonos-grub-installer`. They operate on the kernel
GPT/storage ABI using `/dev/disk0` and `/dev/disk0pN`; partition mutation and
runtime mounts require administrator authorization and the active boot disk is
protected.

## Vim and ncurses

Vim 9.1.1590 and ncurses 6.6 are default components, built from the pinned,
unmodified upstream submodules by `build.py run vim` and `build.py run ncurses`.
Vim uses Linux x86-64 musl, mimalloc and the real wide-character ncurses library;
its normal terminal profile retains timers and multibyte support. No LeonOS
source patch or private syscall wrapper is applied to either upstream package.

Normal images, the live installer and its installed payload contain
`/usr/bin/vim`, `/usr/share/vim/vim91`, ncurses utilities in `/usr/bin`,
and `/usr/share/terminfo`. Vim and these utilities are static Linux
executables. The developer and musl SDKs include upstream curses headers,
`libncursesw.a`, `libtinfow.a`, panel/menu/form archives and terminfo data.
Link wide-character applications with `-lncursesw -ltinfow`.

The earlier internal ANSI curses implementation remains an implementation
detail of existing LeonOS applications; its headers are not the SDK's ncurses
interface. Licenses ship as `/usr/lib/leonos/apps/vim/LICENSE` and
`/usr/share/licenses/ncurses/COPYING`, with `THIRD_PARTY/NCURSES-COPYING` in the
developer SDK. `build.py run test-terminal-packages` runs the actual binaries
and library on Linux; guest validation is documented separately.

## GNU nano

- Path: `third_party/nano`
- Upstream: `https://git.savannah.gnu.org/git/nano.git`
- Version: `9.2`
- Pinned commit: `8e6360d1663998c62ddd0cf934923d1f18004e3e` (`v9.2`)
- License: GPL-3.0-or-later; the complete upstream `COPYING` is staged at
  `/usr/lib/leonos/apps/nano/COPYING` beside the executable.

LeonOS builds Nano at `/usr/lib/leonos/apps/nano/nano.elf` with a narrow ANSI curses
compatibility layer over the GUI terminal PTY. This initial port intentionally
uses Nano's single-buffer tiny profile: the core editor path is present, while
external spellers/formatters, rc files, syntax coloring, help pages, mouse
input and multi-buffer support remain off. Interactive editing and persistence
still require manual GUI-terminal validation on each supported VM platform.

## GNU less

- Path: `third_party/less`
- Upstream: `https://github.com/gwsw/less.git`
- Pinned commit: `b8bbf4297169e20d35e1cc3e015180e8a011bcf2`
- License: GNU GPL-3.0-or-later or the upstream Less License; preserve both
  `third_party/less/COPYING` and `third_party/less/LICENSE`.

LeonOS installs the upstream pager at `/usr/lib/leonos/apps/less/less.elf`. It uses the
shared PTY, polling and POSIX regular-expression runtime through a small ANSI
termcap adapter. Shell escapes, external editor commands, tags, user key files,
logfile output and shell pipes are disabled for the system build.

## TinyCC

- Path: `third_party/tinycc`
- Upstream: `https://github.com/TinyCC/tinycc.git`
- Version: `0.9.28rc`
- Pinned commit: `2ba12e83b3599ca8f5d50c179fe5138fe956f0c9` (`release_0_9_27-1440-g2ba12e83`)
- License: LGPL-2.1-or-later; the complete upstream `COPYING` is staged at
  `/opt/tcc/COPYING` beside the executable and runtime files.

LeonOS builds TinyCC as the static, on-device x86_64 C compiler at
`/opt/tcc/tcc.elf`. It uses the installed musl headers,
`libleonos.a`, `libc.a`, musl CRT objects, the target support archive
`libleonos-tcc-rt.a`, and TinyCC's `libtcc1.a` to produce normal static LeonOS
ELF programs. musl headers are staged unchanged; LeonOS ABI predefines are
owned by TinyCC's target definition layer. Dynamic linking, shared libraries,
PIE and in-memory `tcc -run` execution are deliberately unavailable until the
runtime loader ABI exists. The target runtime currently reports `ENOSYS` for
`times()`; `signal()` supports the `SIG_DFL` and `SIG_IGN` dispositions, while
arbitrary user callbacks remain unavailable.

## Lua

- Path: `third_party/lua`
- Upstream: `https://github.com/lua/lua.git`
- Version: `5.4.8`
- Pinned commit: `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (`v5.4.8`)
- License: MIT; the LeonOS copy of the complete upstream license is staged at
  `/opt/lua/LICENSE` beside the executable.

LeonOS builds Lua as the command-line interpreter at `/opt/lua/lua.elf`
and provides its ABI-v1 C API in `/usr/lib/liblua.so.5`. It uses Lua's
portable C89 configuration with the LeonOS runtime. Dynamic C modules and
`package.loadlib` remain unavailable. Lua scripts can be loaded from the current directory or from
`/opt/lua/lua/`.

## Lua Development Source

- Path: `devtools/components/lua/upstream`
- Upstream: `https://github.com/lua/lua.git`
- Pinned commit: `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (`v5.4.8`)
- License: MIT; preserve the upstream `COPYRIGHT` and license notices when
  redistributing this development checkout.

This separate checkout is used by the Lua development tooling and is not the
runtime submodule built into the system image. It is pinned independently in
the root `.gitmodules` file, even though it currently tracks the same Lua
release as `third_party/lua`.

## PL Editor

- Path: `third_party/pl_editor`
- Upstream modified fork: `https://github.com/Leonmmcoset/pl_editor.git`
- Pinned commit: `22fae7a1bc2362486d8bf845f0daf6ec7060a3a1`
- License: MIT; the complete upstream `LICENSE` is staged at
  `/usr/lib/leonos/apps/pleditor/LICENSE` beside the executable.

LeonOS builds PL Editor at `/usr/lib/leonos/apps/pleditor/pleditor.elf`. Its upstream
platform-independent editor core is kept as a submodule; the LeonOS platform
adapter provides raw PTY input, ANSI terminal output, terminal sizing and
multi-encoding file persistence. It is launched through Terminal and supports
syntax highlighting, search, undo/redo, line numbers, automatic bracket
completion, CRLF preservation, wrapped welcome messages and the fork's
extended syntax set.

## ChenPi11 cmd

- Path: `third_party/cmd`
- Upstream: `https://github.com/ChenPi11/cmd`
- Version: `0.1.0`
- Pinned commit: `2290c38bc9da54db53aa56161a7204a27b388e21`
- License: GPL-3.0-only; the complete upstream `LICENSE` is staged at
  `/opt/cmd/LICENSE` beside the executable.

LeonOS builds the interpreter at `/opt/cmd/cmd.elf`. From the BusyBox
Ash prompt, enter `cmd` to use it. The port keeps the upstream interpreter,
built-ins, batch files, variables and redirection, and executes enabled
BusyBox applets or supported LeonOS terminal programs through the shared COW
`fork`/`execve`/`waitpid` path. Foreground pipelines use inherited anonymous
pipes and support per-stage redirection. `cmd` also supports `command &`,
external pipelines ending in `&`, plus `jobs`, `fg`, and `bg`; those background
jobs remain limited to external commands without per-stage redirection. BusyBox
Ash is the interactive POSIX-style shell and uses the same native COW process
path for full pipeline, redirection, process-group, and terminal job-control
semantics.

## file / libmagic

- Path: `third_party/file`
- Upstream: `https://github.com/file/file.git`
- Version: `5.48`
- Pinned commit: `711ccc264519cdc5073ccb26651c0a9bafc3b47a` (`FILE5_48-17-g711ccc26`)
- License: BSD-2-Clause-style upstream license; preserve `third_party/file/COPYING`.

LeonOS builds the upstream `file` command at `/usr/lib/leonos/apps/file/file.elf` and
the ABI-v1 `libmagic.so.1` at `/usr/lib/libmagic.so.1`. The compiled magic
database is installed at `/usr/share/misc/magic.mgc`; the port keeps the
upstream recognizers while adapting file access to the Linux x86-64 musl ABI.

## Fastfetch

- Path: `third_party/fastfetch`
- Upstream: `https://github.com/fastfetch-cli/fastfetch.git`
- Version: `2.67.0`
- Pinned commit: `56da8f811068289f6352db8881418aa6e0f994e8` (`2.67.0`)
- License: MIT; the complete upstream `LICENSE` is staged at
  `/usr/lib/leonos/apps/fastfetch/LICENSE` beside the executable.

LeonOS builds upstream Fastfetch at `/usr/lib/leonos/apps/fastfetch/fastfetch.elf`.
The unmodified upstream core supplies string, format, printing, ASCII-logo
data, size, duration, percentage, display-option and module implementations.
The separate `userland/fastfetch` adapter obtains Title, OS, Kernel, Uptime,
Processes and Memory data from the LeonOS public ABI instead of Linux `/proc`
and `/sys`. The port also includes the upstream DateTime, Break, Colors and
Version modules, all 527 upstream built-in ASCII logos, logo/display options,
and restricted `--structure` selection for the modules available on LeonOS.
JSON/config files, file or image logos, dynamic refresh, dynamic libraries and
modules requiring a host POSIX or Linux interface remain disabled.

## sl

- Path: `third_party/sl`
- Upstream: `https://github.com/mtoyoda/sl.git`
- Pinned commit: `923e7d7ebc5c1f009755bdeb789ac25658ccce03`
- License: permissive upstream license; the complete upstream `LICENSE` is
  staged at `/usr/lib/leonos/apps/sl/LICENSE` beside the executable.

LeonOS builds the Steam Locomotive joke command at
`/usr/lib/leonos/apps/sl/sl.elf`. The upstream animation is kept intact and its curses
calls are implemented by the ANSI adapter in `userland/sl`.

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

## PortableGL

- Path: `third_party/portablegl`
- Upstream: `https://github.com/rswinkle/PortableGL.git`
- Pinned commit: `7cf39dc1741ea2be60ce3bd327f6e5337f60207f`
- License: MIT; the complete upstream `LICENSE` is staged at
  `/usr/share/doc/leonos/PORTABLEGL-LICENSE` and in the Developer SDK.

LeonOS builds the single-header implementation as ABI-v1
`/usr/lib/libportablegl.so.1` and also exposes `libportablegl.a` and the
`leonos/pgl.h` window wrapper in the Developer SDK. The port fixes the
framebuffer to ABGR32 and depth/stencil to D24S8 and connects presentation to
the LeonOS pixel-buffer window service. The system build uses PortableGL's
small-memory profile (50,000 output vertices per draw call) to fit the current
user address-space budget. `glxgears` is the bundled GUI smoke test; GLX/X11,
hardware acceleration and multi-threaded contexts are not part of this port.

## Rootfs network databases

`system/rootfs/etc/protocols` and `services` are unmodified Debian netbase v6.4
files, matching the SHA-512 checksums used by Alpine baselayout 3.7.2. Source:
https://salsa.debian.org/md/netbase/-/tree/v6.4 . GPL-2 license, upstream
copyright and source attribution ship in `/usr/share/licenses/netbase`.
