# Native Static Musl Toolchain

The default `musl-gcc` component ships the unchanged x86-64 binary distribution
from [Dyne musl 2.2.0](https://github.com/dyne/musl/releases/tag/2.2.0).
The archive SHA256 is
`31420e4f978e7ccbcc597ca5e18c2dcbe640ea6985ed6325112d8a171a72ece3`.
It contains GCC 15.1.0 (C/C++), binutils, musl headers and static libraries,
libgcc, libstdc++, and the additional sysroot shipped by Dyne. Its host
executables require x86-64-v2. Original contents live under `/opt/dyne`.

`/bin` contains static ELF launchers for `gcc`, `cc`, `musl-gcc`, `g++`, `c++`,
`musl-g++`, `cpp`, `as`, `ld`, `ar`, `ranlib`, `nm`, `objcopy`, `objdump`,
`readelf`, `strip`, the remaining supplied tools, and their target-prefixed
names. They locate the installed compiler and sysroot; no compiler source or
binary patches are applied. Compiler flags are forwarded unchanged, so request
static output explicitly, for example `musl-gcc -static hello.c -o hello`.
This standalone Linux toolchain does not replace the LeonOS GUI SDK and does
not link mimalloc into generated programs automatically.

Build: `python3 build.py run musl-gcc`. The package is included by default in
`image-iso`, `image-vmdk`, and `installer`. Disable its BUILD/IMAGE component
switches in menuconfig to omit it. The build downloads the fixed release via
`http://127.0.0.1:12334` into `buildsystem/deps/musl-gcc/`. For an offline build,
set `LEONOS_GCC_ARCHIVE` to the original tar.xz; the same checksum is required.
Subsequent builds reuse the cache. `run clean` need not redownload the input.

The distribution's existing notices and documentation are preserved. Upstream
source and build recipes are available at https://github.com/dyne/musl/tree/2.2.0
(including its pinned GCC/binutils/musl build inputs). GCC and binutils are GPL
software; GCC runtime libraries carry the GCC Runtime Library Exception where
specified upstream. Musl uses the MIT license. Additional bundled libraries
retain their own licenses. `/usr/share/licenses/musl-gcc/package.json` records every
upstream file's SHA256 for comparison with the original archive.

The case-sensitive Linux headers require ext2 in the live/installer root as
well as on the installed system. The boot module keeps the legacy filename
`/install/root.fat` for boot configuration compatibility, but new media contain
ext2 and the kernel recognizes the filesystem magic. EFI boot remains FAT.
