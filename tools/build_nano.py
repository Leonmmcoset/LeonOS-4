#!/usr/bin/env python3
"""Build a static GNU nano with LeonOS's ANSI curses compatibility layer."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
NANO_SOURCES = (
    "browser.c", "chars.c", "color.c", "cut.c", "files.c", "global.c",
    "help.c", "history.c", "move.c", "nano.c", "prompt.c", "rcfile.c",
    "search.c", "text.c", "utils.c", "winio.c",
)


def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


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


def compile_source(compiler: str, flags: list[str], source: Path, output: Path) -> None:
    run([compiler, *flags, "-c", str(source), "-o", str(output)])


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
    parser.add_argument("--dynamic-crt", type=Path)
    parser.add_argument("--abi-note", type=Path)
    parser.add_argument("--dynamic", action="store_true")
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("nano must be built from WSL/Linux, not Windows")

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
        source / "src/nano.c", source / "COPYING", port / "leonos_curses.c",
        port / "leonos_port.c", port / "include/config.h", port / "include/ncurses.h",
        port / "include/revision.h", picolibc_prefix / "include", leonos_libc_include,
        leonos_include, linker_script, leonos_lib, picolibc_lib,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required nano build input is missing: {path}")

    # This is a build-owned directory, never an upstream source directory.
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    object_dir = work_dir / "objects"
    object_dir.mkdir()

    headers = clang_resource_headers()
    common_flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]), "-std=gnu11", "-ffreestanding",
        "-fno-stack-protector", "-fPIC", "-fPIE", "-mno-red-zone",
        "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections", "-Wall",
        "-Wextra", "-Wno-unused-parameter", "-D_POSIX_C_SOURCE=200809L",
        "-D_DEFAULT_SOURCE", "-DHAVE_CONFIG_H", "-DLEONOS_USE_PICOLIBC",
        "-nostdinc", "-isystem", str(headers), "-I" + str(port / "include"),
        "-I" + str(picolibc_prefix / "include"), "-I" + str(leonos_libc_include),
        "-I" + str(leonos_include), "-I" + str(source / "src"),
    ]
    port_flags = common_flags[:]
    nano_flags = common_flags + [
        "-Dstat=leonos_posix_stat", "-Dfstat=leonos_posix_fstat",
        "-Dlstat=leonos_posix_lstat",
    ]

    objects: list[Path] = []
    for name in NANO_SOURCES:
        source_file = source / "src" / name
        object_file = object_dir / (name.removesuffix(".c") + ".o")
        compile_source("clang", nano_flags, source_file, object_file)
        objects.append(object_file)
    for name in ("leonos_curses.c", "leonos_port.c"):
        source_file = port / name
        object_file = object_dir / (name.removesuffix(".c") + ".o")
        compile_source("clang", port_flags, source_file, object_file)
        objects.append(object_file)

    output.parent.mkdir(parents=True, exist_ok=True)
    if args.dynamic and (not args.dynamic_crt or not args.abi_note):
        raise SystemExit("dynamic Nano requires --dynamic-crt and --abi-note")
    dynamic = ["-pie", "--hash-style=sysv", "--dynamic-linker", "0:/system/lib/ld-leonos.elf",
               "-z", "relro", "-z", "now"] if args.dynamic else []
    startup = [str(args.dynamic_crt), str(args.abi_note)] if args.dynamic else []
    libraries = [str(leonos_lib)] if args.dynamic else [str(leonos_lib), str(picolibc_lib)]
    run(["ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag, *dynamic,
         "-z", "max-page-size=0x1000", "-T", str(linker_script), "-o", str(output),
         *startup, *map(str, objects), "--start-group", *libraries, "--end-group"])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps(
            {
                "nano_commit": source_revision(source),
                "nano_version": "9.2",
                "port_sha256": hashlib.sha256(
                    (port / "leonos_curses.c").read_bytes()
                    + (port / "leonos_port.c").read_bytes()
                    + (port / "include/config.h").read_bytes()
                ).hexdigest(),
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
