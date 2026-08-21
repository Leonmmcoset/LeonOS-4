# LeonOS `file` and libmagic

LeonOS ships upstream `file` 5.48 at `/programs/file/file.elf` and its
ABI-v1 shared `libmagic.so.1` at `/system/lib/libmagic.so.1`. The shell
exposes it as the external `file` command. Its matching compiled database is loaded from
`/system/share/misc/magic.mgc`.

The port keeps the upstream magic database and format recognizers while using
the LeonOS/Picolibc ABI. Host-process decompression, memory mapping, and
directory-backed user magic files are disabled because LeonOS does not expose
those POSIX interfaces yet; regular files, ELF, archives, text, JSON, images,
and the other compiled database recognizers remain available.

`libmagic.so.1`, `libmagic.a`, `include/magic.h`, and the upstream BSD-2-Clause
notice are exported by `LeonOS4-Developer-SDK.zip`. Applications that use the
shared library must link it after `libleonos.so.1`; static applications can
continue using `libmagic.a` with `STATIC=1`.
