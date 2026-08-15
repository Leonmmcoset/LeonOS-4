#!/usr/bin/env python3
"""Build the upstream sl animation with LeonOS's ANSI terminal adapter."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


SL_COMMIT = "923e7d7ebc5c1f009755bdeb789ac25658ccce03"


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def clang_resource_headers() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True, capture_output=True
    )
    headers = Path(result.stdout.strip()) / "include"
    if not headers.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {headers}")
    return headers


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    )
    return result.stdout.strip()


def compile_source(flags: list[str], source: Path, output: Path) -> None:
    run(["clang", *flags, "-c", str(source), "-o", str(output)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("sl must be built from WSL/Linux, not Windows")

    source = args.source.resolve()
    port = args.port.resolve()
    picolibc_prefix = args.picolibc_prefix.resolve()
    leonos_libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    linker_script = args.linker_script.resolve()
    leonos_lib = args.leonos_lib.resolve()
    picolibc_lib = args.picolibc_lib.resolve()
    work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    required = (
        source / "sl.c", source / "sl.h", source / "LICENSE",
        port / "leonos_curses.c", port / "leonos_signal.c", port / "include/curses.h",
        port / "include/unistd.h",
        picolibc_prefix / "include", leonos_libc_include, leonos_include,
        linker_script, leonos_lib, picolibc_lib, args.dynamic_crt, args.abi_note,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required sl build input is missing: {path}")

    revision = source_revision(source)
    if revision != SL_COMMIT:
        raise SystemExit(f"sl must be pinned to {SL_COMMIT}, found {revision}")

    if work_dir.exists():
        shutil.rmtree(work_dir)
    object_dir = work_dir / "objects"
    object_dir.mkdir(parents=True)

    headers = clang_resource_headers()
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-fPIC", "-fPIE",
        "-mno-red-zone", "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-D_POSIX_C_SOURCE=200809L",
        "-DLEONOS_USE_PICOLIBC", "-nostdinc", "-isystem", str(headers),
        "-I" + str(port / "include"), "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include), "-I" + str(leonos_include),
        "-I" + str(source), "-I" + str(port),
    ]
    objects: list[Path] = []
    for name, path in (
        ("sl", source / "sl.c"),
        ("leonos_curses", port / "leonos_curses.c"),
        ("leonos_signal", port / "leonos_signal.c"),
    ):
        object_file = object_dir / f"{name}.o"
        compile_source(flags, path, object_file)
        objects.append(object_file)

    output.parent.mkdir(parents=True, exist_ok=True)
    dynamic = [
        "-pie", "--hash-style=sysv", "--dynamic-linker", "0:/system/lib/ld-leonos.elf",
        "-z", "relro", "-z", "now",
    ]
    run([
        "ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag, *dynamic,
        "-z", "max-page-size=0x1000", "-T", str(linker_script), "-o", str(output),
        str(args.dynamic_crt), str(args.abi_note), *map(str, objects), "--start-group",
        str(leonos_lib), "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps({
            "sl_commit": revision,
            "port_sha256": hashlib.sha256(
                b"".join((port / name).read_bytes()
                         for name in ("leonos_curses.c", "leonos_signal.c", "include/curses.h",
                                      "include/unistd.h"))
            ).hexdigest(),
        }, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
