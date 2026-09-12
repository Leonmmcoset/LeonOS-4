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
tcc /opt/tcc/examples/hello.c -o /tmp/hello.elf
/tmp/hello.elf
```

It automatically uses the headers and static runtime in
`/opt/tcc/`.  Generated executables use musl's `crt1.o`, `crti.o`, `crtn.o`, mimalloc,
`libleonos.a`, the unmodified musl headers/library, and the separate
LeonOS target runtime archive `libleonos-tcc-rt.a` plus TinyCC's compiler
runtime `libtcc1.a`.

The musl headers are copied as supplied by musl.  LeonOS-specific
predefined ABI macros are provided by TinyCC's target layer (`__leonos__`,
LP64 widths, and related compiler definitions), rather than by rewriting
musl private headers during the build.

## Deliberately unavailable

- Dynamic libraries, PIE and shared-library output.
- `tcc -run` / in-memory JIT execution.
- Host/Linux headers and libraries.
- Complete Linux syscall coverage: standard functions use musl and the real
  kernel interfaces. Consult the Linux ABI ledger for outstanding behavior;
  there is no separate target-runtime POSIX emulation.

Paths accepted by the compiler use Unix syntax, such as
`/opt/demo/main.c`. For multiple `C_INCLUDE_PATH`, `CPATH` or
`LIBRARY_PATH` entries, use a colon (`:`), matching the target compiler configuration.
