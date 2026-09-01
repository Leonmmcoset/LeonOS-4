#!/usr/bin/env python3
"""Build the PortableGL ABI-v1 shared and static libraries for LeonOS."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


PORTABLEGL_COMMIT = "7cf39dc1741ea2be60ce3bd327f6e5337f60207f"


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def resource_headers() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True,
        capture_output=True,
    )
    path = Path(result.stdout.strip()) / "include"
    if not path.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {path}")
    return path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--generated-include", type=Path, required=True)
    parser.add_argument("--autoconf", type=Path, required=True)
    parser.add_argument("--dynamic-linker-script", type=Path, required=True)
    parser.add_argument("--runtime-so", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--static-library", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    source = args.source.resolve()
    port = args.port.resolve()
    picolibc_prefix = args.picolibc_prefix.resolve()
    leonos_libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    generated_include = args.generated_include.resolve()
    autoconf = args.autoconf.resolve()
    dynamic_linker_script = args.dynamic_linker_script.resolve()
    runtime_so = args.runtime_so.resolve()
    abi_note = args.abi_note.resolve()
    work_dir = args.work_dir.resolve()
    source_file = port / "leonos_pgl.c"
    required = (
        source / "portablegl.h", source / "LICENSE", source_file,
        picolibc_prefix / "include", leonos_libc_include, leonos_include,
        generated_include, autoconf, dynamic_linker_script, runtime_so, abi_note,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required PortableGL build input is missing: {path}")

    if work_dir.exists():
        shutil.rmtree(work_dir)
    object_dir = work_dir / "objects"
    object_dir.mkdir(parents=True)
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=c99", "-ffreestanding", "-fno-stack-protector", "-fPIC",
        "-mno-red-zone", "-mno-avx", "-mno-avx2", "-ffunction-sections",
        "-fdata-sections", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-DLEONOS_USE_PICOLIBC", "-D_POSIX_C_SOURCE=200809L", "-nostdinc",
        "-isystem", str(resource_headers()), "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include), "-I" + str(leonos_include),
        "-I" + str(generated_include), "-I" + str(source), "-I" + str(port),
        "-include", str(autoconf),
    ]
    object_file = object_dir / "leonos_pgl.o"
    run(["clang", *flags, "-c", str(source_file), "-o", str(object_file)])

    library = args.library.resolve()
    library.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-shared", "-Bsymbolic", "--hash-style=sysv", "--entry=0",
        "-soname", "libportablegl.so.1", "-z", "max-page-size=0x1000",
        "-T", str(dynamic_linker_script), "-o", str(library),
        str(object_file), str(abi_note), str(runtime_so), *args.linker_flag,
    ])

    static_library = args.static_library.resolve()
    static_library.parent.mkdir(parents=True, exist_ok=True)
    if static_library.exists():
        static_library.unlink()
    run(["llvm-ar", "rcs", str(static_library), str(object_file)])

    stamp = args.stamp.resolve()
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({
        "portablegl_commit": PORTABLEGL_COMMIT,
        "framebuffer": "PGL_ABGR32",
        "depth_stencil": "PGL_D24S8",
        "max_vertices": 50000,
        "abi": "LeonOS ABI-v1",
        "threading": "single current context per process",
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
