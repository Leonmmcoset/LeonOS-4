# LeonOS user/kernel boundary

`linux/` owns the Linux v6.12 **native x86-64** wire ABI. `leonos/` owns
LeonOS-specific extensions. Neither i386 nor x32 is included.

- Add syscall numbers only to the pinned upstream table and regenerate
  `linux/syscall.h` with `tools/generate_linux_syscalls.py`. Kernel dispatch
  aliases and libc raw-call helpers include this file; they do not maintain
  independent number tables. A number declaration does not implement a call.
- Keep structures, flag values, ioctl commands and type widths here. Use
  `LINUX_` prefixes when a definition must coexist with libc's public API.
  Public libc structures may differ: TCGETS transfers 36 bytes although musl's
  public termios is larger; musl performs the kernel conversion.
- Kernel-only objects, scheduler state, pointers and helper declarations stay
  under `kernel/ntclks/include`. Public LeonOS convenience functions stay in
  `include/leonos` or `userland/libc/include/leonos` and import these definitions.
- musl sources and headers remain upstream. Verify their actual installed
  headers against UAPI with `python3 build.py test musl-abi`. Never patch a
  musl constant to match a divergent kernel. Removed legacy adapters historically
  translate its API; its installed fcntl definitions use the shared wire flags.
- SDK packagers export this directory without flattening `linux/` and
  `leonos/`. Current source headers override legacy `devtools/include` templates.
  Build the SDK archive; the checked-in template is not a current sysroot.
- Run `python3 build.py test uapi` after changing wire declarations. Compile
  checks only certify declarations. Syscall behavior, errors, blocking, signal
  delivery and object lifetime require separate guest regression evidence.

ABI 1 binaries must be rebuilt after the Linux boundary corrections. The musl
runtime uses `/lib/ld-musl-x86_64.so.1` and `libleonos.so.2`; its FILE, errno, TLS
and allocation objects cannot be mixed with the Picolibc runtime. See
`docs/MUSL_MIGRATION_2026-09-08.md` for current migration and guest-test status.

Permission wire records use independent 32-bit UID/GID values and Linux mode
bits. `linux/stat.h`, `linux/statfs.h`, `linux/fcntl.h`, `linux/errno.h` and
`leonos/permissions.h` are shared declarations, not duplicate libc policy.
On FAT/exFAT, version-1 ACL records remain readable and gain explicit POSIX
metadata on their next write. ext2 uses its native inode fields. Legacy calls
that passed zero to O_CREAT/mkdir must be rebuilt with intentional modes;
zero now means no permissions and is never silently converted to a permissive
default. Authentication uses standard passwd/shadow/group/gshadow files and
upstream Linux-PAM. Populated private AUS2/accounts.db stores are rejected
before update; they are never silently converted or reset. The retired authd
protocol is not a public ABI. The administrator is UID 0, named root.
Kernel DAC always uses the actual UID/GID, not a desktop role. See
`docs/POSIX_PERMISSIONS_2026-09-08.md` for usage and remaining limits.

`linux/signal.h` owns native sigaction flags, the 64-bit mask, x86-64 signal
context/frame and altstack records. musl performs its normal Linux ABI conversion;
old private-frame binaries are not compatible and must be rebuilt. `time.h`
owns native 64-bit timespec/timeval, and `uio.h` supplies the iovec shared by
readv/writev and socket messages. None of these declarations certifies behavior.
Native musl probes are also checked against a Linux 6.12.0 reference guest;
the newer host kernel is not treated as authoritative when versions differ.
