"""Shared Linux x86-64 startup and allocator policy for LeonOS build targets."""

from pathlib import Path


def executable(prefix: Path, output: Path, objects, libraries=(), *, static=False,
               flags=(), linker="ld.lld") -> tuple[str, ...]:
    """Link with musl CRT/TLS and mimalloc, preserving the supplied libraries."""
    lib = prefix / "lib"
    command = [linker, "--gc-sections", "-z", "max-page-size=0x1000", *flags]
    if static:
        command += ["-static", "--image-base=0x4000000"]
    else:
        command += ["-pie", "--hash-style=both", "--dynamic-linker",
                    "/lib/ld-musl-x86_64.so.1", "-rpath", "/system/lib:/lib"]
    command += ["-o", str(output), str(lib / ("crt1.o" if static else "Scrt1.o")),
                str(lib / "crti.o"), *map(str, objects), "-L", str(lib)]
    command += [str(lib / "mimalloc.o")] if static else ["-l:libmimalloc.so.3"]
    command += ["--start-group", *map(str, libraries), "-lc", "--end-group",
                str(lib / "crtn.o")]
    return tuple(command)


def shared(prefix: Path, output: Path, objects, libraries=(), *, soname: str,
           flags=(), linker="ld.lld") -> tuple[str, ...]:
    """Link a DSO without executable startup objects or private ABI notes."""
    return (linker, "-shared", "--no-undefined", "--hash-style=both",
            "-z", "max-page-size=0x1000", "-soname", soname, *flags,
            "-o", str(output), *map(str, objects), "-L", str(prefix / "lib"),
            "-l:libmimalloc.so.3", *map(str, libraries), "-lc")
