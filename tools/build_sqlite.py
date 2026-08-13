#!/usr/bin/env python3
"""Build SQLite's amalgamation and the LeonOS sqlite.so.3 ABI-v1 library."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


SQLITE_VERSION = "3.46.1"
SQLITE_COMMIT = "f3d536d37825302e31ed0eddd811c689f38f85a3"


def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def resource_headers() -> Path:
    result = subprocess.run(["clang", "-print-resource-dir"], check=True,
                            capture_output=True, text=True)
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
    parser.add_argument("--dynamic-linker-script", type=Path, required=True)
    parser.add_argument("--runtime-so", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--static-library", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("SQLite must be built from WSL/Linux, not Windows")
    source = args.source.resolve()
    port = args.port.resolve()
    work = args.work_dir.resolve()
    for required in (source / "src", source / "main.mk", source / "Makefile.linux-gcc",
                     port / "leonos_sqlite_vfs.c", args.picolibc_prefix / "include",
                     args.runtime_so, args.dynamic_crt, args.abi_note):
        if not required.exists():
            raise SystemExit(f"required SQLite build input is missing: {required}")
    revision = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                              check=True, capture_output=True, text=True).stdout.strip()
    if revision != SQLITE_COMMIT:
        raise SystemExit(
            f"unsupported SQLite revision {revision}; expected pinned commit {SQLITE_COMMIT}"
        )

    if work.exists():
        shutil.rmtree(work)
    generated = work / "generated"
    generated.mkdir(parents=True)
    # SQLite's make fragments pass TOP through unquoted shell arguments. Copy
    # the submodule to a space-free temporary path so a Windows-mounted repo
    # path cannot break generation; all generated files stay outside git.
    upstream_tmp = Path(tempfile.mkdtemp(prefix="leonos-sqlite-source-"))
    source_copy = upstream_tmp / "sqlite"
    shutil.copytree(source, source_copy)
    # The upstream generator intentionally writes its products beside the
    # temporary makefile. The vendored submodule remains untouched.
    makefile = generated / "Makefile.linux-gcc"
    makefile.write_text((source_copy / "Makefile.linux-gcc").read_text(encoding="utf-8")
                        .replace("TOP = ../sqlite", f"TOP = {source_copy.as_posix()}"),
                        encoding="utf-8")
    run(["make", "-f", str(makefile), "sqlite3.c", "sqlite3.h"], cwd=generated)
    amalgamation = generated / "sqlite3.c"
    header = generated / "sqlite3.h"
    if not amalgamation.is_file() or not header.is_file():
        raise SystemExit("SQLite amalgamation generation did not produce sqlite3.c/sqlite3.h")
    args.header.resolve().parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(header, args.header.resolve())

    objects = work / "objects"
    objects.mkdir()
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-fPIC",
        "-mno-red-zone", "-ffunction-sections", "-fdata-sections", "-Wall",
        "-Wextra", "-Wno-unused-parameter", "-nostdinc", "-isystem", str(resource_headers()),
        "-I", str(args.picolibc_prefix / "include"), "-I", str(args.leonos_libc_include),
        "-I", str(args.leonos_include), "-I", str(generated),
        "-DSQLITE_OS_OTHER=1", "-DSQLITE_THREADSAFE=0", "-DSQLITE_OMIT_LOAD_EXTENSION=1",
        "-DSQLITE_OMIT_WAL=1", "-DSQLITE_DEFAULT_MEMSTATUS=0", "-DSQLITE_MAX_MMAP_SIZE=0",
    ]
    amalgamation_obj = objects / "sqlite3.o"
    vfs_obj = objects / "leonos_sqlite_vfs.o"
    run(["clang", *flags, "-c", str(amalgamation), "-o", str(amalgamation_obj)])
    run(["clang", *flags, "-c", str(port / "leonos_sqlite_vfs.c"), "-o", str(vfs_obj)])

    args.static_library.resolve().parent.mkdir(parents=True, exist_ok=True)
    if args.static_library.exists():
        args.static_library.unlink()
    run(["llvm-ar", "rcs", str(args.static_library.resolve()), str(amalgamation_obj), str(vfs_obj)])
    args.library.resolve().parent.mkdir(parents=True, exist_ok=True)
    run(["ld.lld", "-shared", "-Bsymbolic", "--hash-style=sysv", "-soname", "sqlite.so.3",
         "-z", "max-page-size=0x1000", "-T", str(args.dynamic_linker_script.resolve()),
         "-o", str(args.library.resolve()), str(amalgamation_obj), str(vfs_obj),
         str(args.abi_note.resolve()), str(args.runtime_so.resolve())])
    args.stamp.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.stamp.resolve().write_text(json.dumps({
        "sqlite_version": SQLITE_VERSION,
        "sqlite_commit": revision,
        "vfs": "leonos",
        "features": ["SQLITE_THREADSAFE=0", "SQLITE_OMIT_WAL", "SQLITE_OMIT_LOAD_EXTENSION"],
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
