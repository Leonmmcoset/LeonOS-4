#!/usr/bin/env python3
"""Build upstream less with the LeonOS PTY and ANSI terminal adapter."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


LESS_COMMIT = "b8bbf4297169e20d35e1cc3e015180e8a011bcf2"
LESS_SOURCES = (
    "main.c", "screen.c", "brac.c", "ch.c", "charset.c", "cmdbuf.c",
    "command.c", "cvt.c", "decode.c", "edit.c", "evar.c", "filename.c",
    "forwback.c", "help.c", "ifile.c", "input.c", "jump.c", "line.c",
    "linenum.c", "lmsg.c", "lsystem.c", "mark.c", "optfunc.c", "option.c",
    "opttbl.c", "os.c", "output.c", "pattern.c", "position.c", "prompt.c",
    "search.c", "signal.c", "tags.c", "ttyin.c", "version.c", "xbuf.c",
)


def run(command: list[str], *, stdin=None, stdout=None) -> None:
    subprocess.run(command, check=True, stdin=stdin, stdout=stdout)


def clang_headers() -> Path:
    result = subprocess.run(
        ["clang", "-print-resource-dir"], check=True, text=True, capture_output=True
    )
    path = Path(result.stdout.strip()) / "include"
    if not path.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {path}")
    return path


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    )
    return result.stdout.strip()


def generate_funcs(source: Path, output: Path) -> None:
    declarations: list[str] = []
    for name in LESS_SOURCES:
        if name == "help.c":
            continue
        for line in (source / name).read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if stripped.startswith("public ") and ";" not in stripped:
                declarations.append(stripped + ";")
    output.write_text("\n".join(declarations) + "\n", encoding="utf-8")


def generate_messages(source: Path, output: Path) -> None:
    entries: list[str] = []
    for input_name in ("lessmsg", "lessmsg_int"):
        for line in (source / input_name).read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            name, separator, message = stripped.partition(" ")
            if separator:
                entries.append(f'M({name},"{message.lstrip()}")')
    output.write_text("\n".join(entries) + "\n", encoding="utf-8")


def generate_help(source: Path, output: Path) -> None:
    with (source / "less.hlp").open("rb") as input_file, output.open("wb") as output_file:
        run(["python3", str(source / "mkhelp.py")], stdin=input_file, stdout=output_file)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("less must be built from WSL/Linux, not Windows")
    source = args.source.resolve()
    port = args.port.resolve()
    picolibc = args.picolibc_prefix.resolve()
    libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    work = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    required = [source / "main.c", source / "less.hlp", source / "lessmsg",
                source / "COPYING", source / "LICENSE", port / "leonos_termcap.c",
                port / "include/defines.h", port / "include/termcap.h",
                picolibc / "include", libc_include, leonos_include,
                args.linker_script.resolve(), args.leonos_lib.resolve(),
                args.dynamic_crt.resolve(), args.abi_note.resolve()]
    for path in required:
        if not path.exists():
            raise SystemExit(f"required less build input is missing: {path}")
    revision = source_revision(source)
    if revision != LESS_COMMIT:
        raise SystemExit(f"less must be pinned to {LESS_COMMIT}, found {revision}")

    if work.exists():
        shutil.rmtree(work)
    generated = work / "generated"
    objects = work / "objects"
    generated.mkdir(parents=True)
    objects.mkdir()
    generate_funcs(source, generated / "funcs.h")
    generate_messages(source, generated / "lessmsg.inc")
    generate_help(source, generated / "help.c")

    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-fPIC", "-fPIE",
        "-mno-red-zone", "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
        "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-DLEONOS_USE_PICOLIBC",
        "-Dstat=leonos_posix_stat", "-Dfstat=leonos_posix_fstat", "-Dlstat=leonos_posix_lstat",
        "-nostdinc", "-isystem", str(clang_headers()), "-I" + str(port / "include"),
        "-I" + str(generated), "-I" + str(picolibc / "include"),
        "-I" + str(libc_include), "-I" + str(leonos_include), "-I" + str(source),
    ]
    object_paths: list[Path] = []
    for name in LESS_SOURCES:
        source_file = generated / name if name == "help.c" else source / name
        object_file = objects / (name.removesuffix(".c") + ".o")
        run(["clang", *flags, "-c", str(source_file), "-o", str(object_file)])
        object_paths.append(object_file)
    termcap_object = objects / "leonos_termcap.o"
    run(["clang", *flags, "-c", str(port / "leonos_termcap.c"), "-o", str(termcap_object)])
    object_paths.append(termcap_object)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag,
        "-pie", "--hash-style=sysv", "--dynamic-linker", "0:/system/lib/ld-leonos.elf",
        "-z", "relro", "-z", "now", "-z", "max-page-size=0x1000",
        "-T", str(args.linker_script.resolve()), "-o", str(output),
        str(args.dynamic_crt.resolve()), str(args.abi_note.resolve()),
        *map(str, object_paths), "--start-group", str(args.leonos_lib.resolve()), "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({
        "less_commit": revision,
        "port_sha256": hashlib.sha256((port / "leonos_termcap.c").read_bytes() +
                                       (port / "include/defines.h").read_bytes()).hexdigest(),
        "features": ["ansi-termcap", "posix-regex", "pty-poll"],
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
