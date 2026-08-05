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
hello.elf
```

It automatically uses the headers and static runtime in
`0:/programs/tcc/`.  Generated executables use LeonOS's existing `_start`,
`libleonos.a`, Picolibc, and TinyCC compiler runtime.

## Deliberately unavailable

- Dynamic libraries, PIE and shared-library output.
- `tcc -run` / in-memory JIT execution.
- Host/Linux headers and libraries.

Paths accepted by the compiler use LeonOS drive syntax, such as
`0:/programs/demo/main.c`.  For multiple `C_INCLUDE_PATH`, `CPATH` or
`LIBRARY_PATH` entries, use a semicolon (`;`), because `:` is part of the
drive prefix.
