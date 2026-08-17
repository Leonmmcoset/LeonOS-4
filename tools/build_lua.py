#!/usr/bin/env python3
"""Build the Lua shared library and interpreter for the LeonOS x86_64 ABI.

The upstream source tree is not patched. Lua is compiled in its portable C89
configuration: this deliberately leaves out POSIX-only behavior, Readline, and
runtime loading of C modules. Lua is
the exception to LeonOS's usual ``-mgeneral-regs-only`` userland compile mode:
its ``double`` values must use the x86-64 SSE ABI used by Picolibc and the
compiler runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


LUA_VERSION = "5.4.8"
LUA_CORE_SOURCES = (
    "lapi.c", "lauxlib.c", "lbaselib.c", "lcode.c", "lcorolib.c", "lctype.c",
    "ldblib.c", "ldebug.c", "ldo.c", "ldump.c", "lfunc.c", "lgc.c", "linit.c",
    "liolib.c", "llex.c", "lmathlib.c", "lmem.c", "loadlib.c", "lobject.c",
    "lopcodes.c", "loslib.c", "lparser.c", "lstate.c", "lstring.c", "lstrlib.c",
    "ltable.c", "ltablib.c", "ltm.c", "lundump.c", "lutf8lib.c", "lvm.c",
    "lzio.c",
)
LUA_APP_SOURCES = ("lua.c",)


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
    parser.add_argument("--dynamic-linker-script", type=Path, required=True)
    parser.add_argument("--runtime-so", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--static-library", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    if os.name == "nt":
        raise SystemExit("Lua must be built from WSL/Linux, not Windows")

    source = args.source.resolve()
    port = args.port.resolve()
    picolibc_prefix = args.picolibc_prefix.resolve()
    leonos_libc_include = args.leonos_libc_include.resolve()
    leonos_include = args.leonos_include.resolve()
    linker_script = args.linker_script.resolve()
    leonos_lib = args.leonos_lib.resolve()
    picolibc_lib = args.picolibc_lib.resolve()
    dynamic_linker_script = args.dynamic_linker_script.resolve()
    runtime_so = args.runtime_so.resolve()
    dynamic_crt = args.dynamic_crt.resolve()
    abi_note = args.abi_note.resolve()
    library = args.library.resolve()
    static_library = args.static_library.resolve()
    work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    required = (
        source / "lua.c", source / "lua.h", source / "luaconf.h", source / "lualib.h",
        port / "LICENSE", port / "leonos_lua_time.c", picolibc_prefix / "include", leonos_libc_include,
        leonos_include, linker_script, leonos_lib, picolibc_lib,
        dynamic_linker_script, runtime_so, dynamic_crt, abi_note,
    )
    for path in required:
        if not path.exists():
            raise SystemExit(f"required Lua build input is missing: {path}")

    # This directory belongs to the build. Never write generated files upstream.
    if work_dir.exists():
        shutil.rmtree(work_dir)
    object_dir = work_dir / "objects"
    object_dir.mkdir(parents=True)

    headers = clang_resource_headers()
    # Lua uses lua_Number (double) throughout the VM and standard libraries.
    # Keep the normal x86-64 SSE floating-point ABI here.  The generic LeonOS
    # userland flags normally include -mgeneral-regs-only, but that flag moves
    # floating-point arguments/results into GPRs while Picolibc/libclang
    # floating-point helpers use XMM registers.  Mixing the two ABIs makes
    # lua_version()/luaL_checkversion_() compare unrelated bit patterns and
    # breaks every other arithmetic or math-library call as well.  The kernel
    # saves/restores each task's x87/SSE state with fxsave/fxrstor, so SSE is a
    # supported user-process ABI and is intentional for this target.
    common_flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]), "-std=c99", "-ffreestanding",
        "-fno-stack-protector", "-fPIC", "-mno-red-zone",
        "-ffunction-sections", "-fdata-sections", "-Wall",
        "-Wextra", "-Wno-unused-parameter", "-DLUA_USE_C89",
        '-DLUA_PATH_DEFAULT="0:/programs/lua/lua/?.lua;0:/programs/lua/lua/?/init.lua;./?.lua;./?/init.lua"',
        '-DLUA_CPATH_DEFAULT=""',
        "-nostdinc", "-isystem", str(headers),
        "-I" + str(picolibc_prefix / "include"),
        "-I" + str(leonos_libc_include),
        "-I" + str(leonos_include),
        "-I" + str(source),
    ]

    library_objects: list[Path] = []
    for name in LUA_CORE_SOURCES:
        source_file = source / name
        object_file = object_dir / (name.removesuffix(".c") + ".o")
        compile_source(common_flags, source_file, object_file)
        library_objects.append(object_file)
    time_object = object_dir / "leonos_lua_time.o"
    compile_source(common_flags, port / "leonos_lua_time.c", time_object)
    library_objects.append(time_object)

    library.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-shared", "-Bsymbolic", "--hash-style=sysv", "-soname", "liblua.so.5",
        "-z", "max-page-size=0x1000", "-T", str(dynamic_linker_script),
        "-o", str(library), *map(str, library_objects), str(abi_note), str(runtime_so),
    ])
    static_library.parent.mkdir(parents=True, exist_ok=True)
    if static_library.exists():
        static_library.unlink()
    run(["llvm-ar", "rcs", str(static_library), *map(str, library_objects)])

    objects: list[Path] = []
    for name in LUA_APP_SOURCES:
        source_file = source / name
        object_file = object_dir / (name.removesuffix(".c") + "-app.o")
        compile_source([*common_flags, "-fPIE"], source_file, object_file)
        objects.append(object_file)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", "-pie", "--hash-style=sysv",
        "--dynamic-linker", "0:/system/lib/ld-leonos.elf", "-z", "relro", "-z", "now",
        "-z", "max-page-size=0x1000", *args.linker_flag, "-T", str(dynamic_linker_script),
        "-o", str(output), str(dynamic_crt), str(abi_note), *map(str, objects),
        str(runtime_so), str(library),
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps(
            {
                "lua_commit": source_revision(source),
                "lua_version": LUA_VERSION,
                "configuration": (
                    "LUA_USE_C89, liblua.so.5 plus dynamic interpreter, no dynamic C modules, "
                    "x86-64 SSE floating-point ABI"
                ),
                "port_sha256": hashlib.sha256((port / "LICENSE").read_bytes()).hexdigest(),
            },
            indent=2,
        ) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
