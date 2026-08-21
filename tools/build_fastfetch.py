#!/usr/bin/env python3
"""Build the upstream Fastfetch core with the LeonOS platform adapter."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


FASTFETCH_VERSION = "2.67.0"
FASTFETCH_COMMIT = "56da8f811068289f6352db8881418aa6e0f994e8"
UPSTREAM_SOURCES = (
    "common/impl/FFPlatform.c",
    "common/impl/FFlist.c",
    "common/impl/FFstrbuf.c",
    "common/impl/duration.c",
    "common/impl/format.c",
    "common/impl/option.c",
    "common/impl/percent.c",
    "common/impl/printing.c",
    "common/impl/size.c",
    "common/impl/strutil.c",
    "common/impl/time.c",
    "3rdparty/yyjson/yyjson.c",
    "logo/builtin.c",
    "modules/break/break.c",
    "modules/colors/colors.c",
    "modules/datetime/datetime.c",
    "modules/kernel/kernel.c",
    "modules/memory/memory.c",
    "modules/os/os.c",
    "modules/processes/processes.c",
    "modules/separator/separator.c",
    "modules/title/title.c",
    "modules/uptime/uptime.c",
    "modules/version/version.c",
    "options/display.c",
)
PORT_SOURCES = (
    "main.c",
    "platform_leonos.c",
    "detection_leonos.c",
    "runtime_leonos.c",
    "logo_leonos.c",
)


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


def clang_runtime_library() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True, capture_output=True
    )
    runtime = Path(result.stdout.strip()) / "lib/linux/libclang_rt.builtins-x86_64.a"
    if not runtime.is_file():
        raise SystemExit(f"Clang x86_64 compiler runtime is missing: {runtime}")
    return runtime


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


def compile_source(flags: list[str], source: Path, output: Path) -> None:
    run(["clang", *flags, "-c", str(source), "-o", str(output)])


def generate_logo_builtin_header(source: Path, output: Path) -> None:
    """Generate the header consumed by upstream's pure ASCII-logo table.

    This is the same transformation performed by Fastfetch's CMake build.  It
    deliberately happens in the build directory so the pinned upstream tree
    remains unmodified.
    """
    logo_files = sorted((source / "src/logo/ascii").glob("[a-z_]/*.txt"))
    if not logo_files:
        raise SystemExit("Fastfetch built-in ASCII logo data is missing")

    lines = [
        "#pragma once",
        '#pragma GCC diagnostic ignored "-Wtrigraphs"',
        "",
    ]
    for logo_file in logo_files:
        content = logo_file.read_text(encoding="utf-8")
        if content.endswith("\n"):
            content = content[:-1]
        content = content.replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"')
        macro = logo_file.stem.upper()
        lines.append(f'#define FASTFETCH_DATATEXT_LOGO_{macro} "{content}"')

    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


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
        raise SystemExit("fastfetch must be built from WSL/Linux, not Windows")

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
        source / "LICENSE",
        source / "src/fastfetch.h",
        *(source / "src" / name for name in UPSTREAM_SOURCES),
        *(port / name for name in PORT_SOURCES),
        port / "include/fastfetch_config.h",
        picolibc_prefix / "include",
        leonos_libc_include,
        leonos_include,
        linker_script,
        leonos_lib,
        picolibc_lib,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required Fastfetch build input is missing: {path}")

    revision = source_revision(source)
    if revision != FASTFETCH_COMMIT:
        raise SystemExit(
            f"Fastfetch must be pinned to {FASTFETCH_COMMIT}, found {revision}"
        )

    if work_dir.exists():
        shutil.rmtree(work_dir)
    object_dir = work_dir / "objects"
    object_dir.mkdir(parents=True)
    generate_logo_builtin_header(source, work_dir / "logo_builtin.h")

    headers = clang_resource_headers()
    compiler_runtime = clang_runtime_library()
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu2x", "-ffreestanding", "-fno-stack-protector", "-fPIC",
        "-fPIE", "-mno-red-zone",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-D_POSIX_C_SOURCE=200809L",
        "-DLEONOS_USE_PICOLIBC", "-DFF_DISABLE_DLOPEN",
        "-DFF_ENABLE_WCWIDTH=0", "-DFF_HAVE_LUA=0", "-DFF_HAVE_QUICKJS=0",
        "-DFF_HAVE_CHAFA=0", "-nostdinc", "-isystem", str(headers),
        "-I" + str(work_dir),
        "-I" + str(port / "include"), "-include", "strings.h",
        "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include), "-I" + str(leonos_include),
        "-I" + str(source / "src"), "-I" + str(port),
    ]

    objects: list[Path] = []
    for name in UPSTREAM_SOURCES:
        object_file = object_dir / ("upstream_" + name.replace("/", "_")).replace(".c", ".o")
        compile_source(flags, source / "src" / name, object_file)
        objects.append(object_file)
    for name in PORT_SOURCES:
        object_file = object_dir / ("port_" + name.removesuffix(".c") + ".o")
        compile_source(flags, port / name, object_file)
        objects.append(object_file)

    output.parent.mkdir(parents=True, exist_ok=True)
    if args.dynamic and (not args.dynamic_crt or not args.abi_note):
        raise SystemExit("dynamic Fastfetch requires --dynamic-crt and --abi-note")
    dynamic = ["-pie", "--hash-style=sysv", "--dynamic-linker", "/system/lib/ld-leonos.elf",
               "-z", "relro", "-z", "now"] if args.dynamic else []
    startup = [str(args.dynamic_crt), str(args.abi_note)] if args.dynamic else []
    libraries = [str(leonos_lib)] if args.dynamic else [str(leonos_lib), str(picolibc_lib), str(compiler_runtime)]
    printf_selection = [] if args.dynamic else ["--defsym=vfprintf=__d_vfprintf"]
    run([
        "ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag, *dynamic,
        # The shared ABI exports the full Picolibc vfprintf implementation.
        # The private __d_vfprintf alias only exists in the legacy static
        # archive, where Fastfetch still selects it explicitly.
        *printf_selection,
        "-z", "max-page-size=0x1000", "-T", str(linker_script),
        "-o", str(output), *startup, *map(str, objects), "--start-group",
        *libraries, "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps(
            {
                "fastfetch_version": FASTFETCH_VERSION,
                "fastfetch_commit": revision,
                "port_sha256": hashlib.sha256(
                    b"".join((port / name).read_bytes() for name in PORT_SOURCES)
                ).hexdigest(),
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
