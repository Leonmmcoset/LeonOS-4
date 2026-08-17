# TinyCC for LeonOS 4

`tcc` is the on-device C compiler for x86_64 LeonOS userland.

## Supported first-stage workflow

```text
tcc -c hello.c -o hello.o
tcc hello.c -o hello.elf
0:/path/to/hello.elf
```

The installed `examples/hello.c` provides a small end-to-end check:

```text
cd 0:/programs/tcc
tcc examples/hello.c -o hello.elf
0:/programs/tcc/hello.elf
```

It automatically uses the headers and static runtime in
`0:/programs/tcc/`.  Generated executables use LeonOS's existing `_start`,
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
- `times()` and `signal()` in generated programs; LeonOS does not yet expose
  process CPU-time or POSIX signal ABIs, so these target-runtime stubs return
  `ENOSYS`/`SIG_ERR`.

Paths accepted by the compiler use LeonOS drive syntax, such as
`0:/programs/demo/main.c`.  For multiple `C_INCLUDE_PATH`, `CPATH` or
`LIBRARY_PATH` entries, use a semicolon (`;`), because `:` is part of the
drive prefix.
