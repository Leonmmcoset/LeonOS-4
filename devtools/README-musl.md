# LeonOS musl SDK

Use `make APP=examples/helloworld` with host clang/lld and Python 3.
The SDK driver links musl 1.2.6 and mimalloc 3.5.1. STATIC=1 selects the static
musl CRT and allocator; dynamic executables use /lib/ld-musl-x86_64.so.1 and
libleonos.so.2. Rebuild all binaries created with the retired libc ABI.
Linux ABI coverage and outstanding kernel work are documented in
../docs/LINUX_ABI_PROGRESS_2026-09-08.md in the source distribution.
