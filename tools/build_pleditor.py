#!/usr/bin/env python3
"""Build PL Editor from a pristine upstream worktree for LeonOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EDITOR_SOURCES = ("main.c", "pleditor.c", "syntax.c")


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


def patch_renderer(source: Path) -> None:
    """Apply the LeonOS-only bounds fix without changing the submodule."""
    text = source.read_text(encoding="utf-8")
    include_before = "#include <stdlib.h>\n"
    include_after = "#include <stdlib.h>\n#include <stdint.h>\n"
    allocation_before = """    /* Buffer to build screen update in (large enough for entire screen) */
    char *buffer = malloc(state->screen_rows * state->screen_cols * 5);
    int len = 0;
"""
    allocation_after = """    /*
     * Highlighting can change colour before every displayed byte.  Reserve a
     * conservative per-row upper bound for ANSI SGR sequences, line numbers
     * and clear-to-EOL commands before doing any screen write.
     */
    size_t rows = (size_t)state->screen_rows;
    size_t cols = (size_t)state->screen_cols;
    size_t bytes_per_row;
    size_t buffer_capacity;
    if (rows == 0 || cols == 0 || cols > (SIZE_MAX - 256U) / 8U) {
        return;
    }
    bytes_per_row = cols * 8U + 256U;
    if (rows > (SIZE_MAX - 256U) / bytes_per_row) {
        return;
    }
    buffer_capacity = rows * bytes_per_row + 256U;
    char *buffer = malloc(buffer_capacity);
    if (!buffer) {
        return;
    }
    int len = 0;
"""
    if include_before not in text or allocation_before not in text:
        raise SystemExit("unsupported PL Editor source revision: renderer patch did not apply")
    text = text.replace(include_before, include_after, 1)
    source.write_text(text.replace(allocation_before, allocation_after, 1), encoding="utf-8")


def compile_source(flags: list[str], source: Path, output: Path) -> None:
    run(["clang", *flags, "-c", str(source), "-o", str(output)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--generated-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("pleditor must be built from WSL/Linux, not Windows")

    source = args.source.resolve()
    port = args.port.resolve()
    picolibc_prefix = args.picolibc_prefix.resolve()
    leonos_libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    generated_include = args.generated_include.resolve()
    linker_script = args.linker_script.resolve()
    leonos_lib = args.leonos_lib.resolve()
    picolibc_lib = args.picolibc_lib.resolve()
    work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    required = (
        source / "src/main.c", source / "src/pleditor.c", source / "src/syntax.c",
        source / "src/pleditor.h", source / "src/platform.h", source / "LICENSE",
        port / "platform_leonos.c", picolibc_prefix / "include", leonos_libc_include,
        leonos_include, generated_include, linker_script, leonos_lib, picolibc_lib,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required PL Editor build input is missing: {path}")
    if ROOT not in work_dir.parents:
        raise SystemExit(f"PL Editor work directory must be inside the project: {work_dir}")

    # The patched copy is build-owned; the pinned upstream submodule stays clean.
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_source = work_dir / "src"
    shutil.copytree(source / "src", work_source)
    patch_renderer(work_source / "pleditor.c")
    object_dir = work_dir / "objects"
    object_dir.mkdir()

    headers = clang_resource_headers()
    flags = [
        "-target", "x86_64-unknown-none", "-O2", "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
        "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections", "-Wall",
        "-Wextra", "-DLEONOS_USE_PICOLIBC", "-D_POSIX_C_SOURCE=200809L",
        "-include", str(generated_include / "autoconf.h"), "-nostdinc", "-isystem",
        str(headers), "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include), "-I" + str(leonos_include),
        "-I" + str(generated_include), "-I" + str(work_source),
    ]

    objects: list[Path] = []
    for name in EDITOR_SOURCES:
        object_file = object_dir / name.removesuffix(".c").replace("/", "_")
        object_file = object_file.with_suffix(".o")
        compile_source(flags, work_source / name, object_file)
        objects.append(object_file)
    port_object = object_dir / "platform_leonos.o"
    compile_source(flags, port / "platform_leonos.c", port_object)
    objects.append(port_object)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", "-z", "max-page-size=0x1000",
        "-T", str(linker_script), "-o", str(output), *map(str, objects),
        "--start-group", str(leonos_lib), str(picolibc_lib), "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps(
            {
                "pleditor_commit": source_revision(source),
                "renderer_patch": "bounded-ansi-screen-buffer-v1",
                "port_sha256": hashlib.sha256((port / "platform_leonos.c").read_bytes()).hexdigest(),
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
