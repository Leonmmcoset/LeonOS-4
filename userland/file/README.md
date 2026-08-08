# LeonOS `file` and libmagic

LeonOS ships upstream `file` 5.48 as a static program at
`0:/programs/file/file.elf`. The shell exposes it as the external `file`
command. Its matching compiled database is loaded from
`0:/system/share/misc/magic.mgc`.

The port keeps the upstream magic database and format recognizers while using
the LeonOS/Picolibc ABI. Host-process decompression, memory mapping, and
directory-backed user magic files are disabled because LeonOS does not expose
those POSIX interfaces yet; regular files, ELF, archives, text, JSON, images,
and the other compiled database recognizers remain available.

`libmagic.a`, `include/magic.h`, and the upstream BSD-2-Clause notice are
exported by `LeonOS4-Developer-SDK.zip`.
