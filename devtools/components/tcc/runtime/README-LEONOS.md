# TinyCC for LeonOS 4

`tcc` is the on-device C compiler for x86_64 LeonOS userland.

## Supported first-stage workflow

```text
tcc -c hello.c -o hello.o
tcc hello.c -o hello.elf
/path/to/hello.elf
```

The installed `examples/hello.c` provides a small end-to-end check:

```text
cd /programs/tcc
tcc examples/hello.c -o hello.elf
/programs/tcc/hello.elf
```

It automatically uses the headers and static runtime in
`/programs/tcc/`.  Generated executables use LeonOS's existing `_start`,
`libleonos.a`, the unmodified Picolibc headers/library, and the separate
LeonOS target runtime archive `libleonos-tcc-rt.a` plus TinyCC's compiler
runtime `libtcc1.a`.

The Picolibc headers are copied as supplied by Picolibc.  LeonOS-specific
predefined ABI macros are provided by TinyCC's target layer (`__leonos__`,
LP64 widths, and related compiler definitions), rather than by rewriting
Picolibc private headers during the build.

## Deliberately unavailable

- Dynamic libraries, PIE and shared-library output.
- `tcc -run` / in-memory JIT execution.
- Host/Linux headers and libraries.
- `times()` in generated programs; LeonOS does not yet expose process CPU-time
  accounting, so the target-runtime stub returns `ENOSYS`. `signal()` is
  supplied by `libleonos`, but user-installed handlers remain unsupported and
  return `SIG_ERR` with `errno = ENOSYS`.

Paths accepted by the compiler use Unix syntax, such as
`/programs/demo/main.c`. For multiple `C_INCLUDE_PATH`, `CPATH` or
`LIBRARY_PATH` entries, use a semicolon (`;`); `:` is rejected in LeonOS
paths.
