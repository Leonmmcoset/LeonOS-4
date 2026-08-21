#!/usr/bin/env python3
"""Build the upstream file(1) and ABI-v1 libmagic shared library for LeonOS."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


LIBMAGIC_SOURCES = (
    "buffer.c", "magic.c", "apprentice.c", "softmagic.c", "ascmagic.c",
    "encoding.c", "is_csv.c", "is_json.c", "is_simh.c",
    "is_tar.c", "readelf.c", "print.c", "fsmagic.c", "funcs.c",
    "apptype.c", "der.c", "cdf.c", "cdf_time.c", "readcdf.c", "swap.c",
    "asprintf.c", "vasprintf.c", "dprintf.c", "getline.c", "strcasestr.c",
    "strlcat.c", "strlcpy.c", "ctime_r.c", "asctime_r.c", "gmtime_r.c",
    "localtime_r.c", "fmtcheck.c",
)


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


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
    parser.add_argument("--dynamic-linker-script", type=Path, required=True)
    parser.add_argument("--runtime-so", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--static-library", type=Path, required=True)
    parser.add_argument("--magic-header", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    source = args.source.resolve()
    port = args.port.resolve()
    work = Path(tempfile.mkdtemp(prefix="leonos-file-build-"))
    # libmagic uses PATHSEP to split its database search path. LeonOS keeps a
    # semicolon separator so its Unix paths remain compatible with the target
    # runtime. Patch only this throw-away build copy; keep the vendored source
    # unchanged.
    patched_source = work / "src"
    shutil.copytree(source / "src", patched_source)
    file_header = patched_source / "file.h"
    header_text = file_header.read_text(encoding="utf-8")
    header_text = header_text.replace(
        "#if defined(__EMX__) || defined (WIN32)",
        "#if defined(__EMX__) || defined (WIN32) || defined(LEONOS_FILE_PATHSEP_SEMICOLON)",
        1,
    )
    file_header.write_text(header_text, encoding="utf-8")
    include_dir = work / "include"
    object_dir = work / "objects"
    include_dir.mkdir()
    object_dir.mkdir()
    magic_header = (source / "src/magic.h.in").read_text(encoding="utf-8")
    (include_dir / "magic.h").write_text(magic_header.replace("X.YY", "548"), encoding="utf-8")
    args.magic_header.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.magic_header.resolve().write_text(magic_header.replace("X.YY", "548"), encoding="utf-8")
    shutil.copyfile(port / "config.h", include_dir / "config.h")
    # ``-nostdinc`` keeps host libc headers out of the freestanding build, but
    # Clang's builtin ``stdarg.h``/``stddef.h`` are still required by the
    # Picolibc-compatible LeonOS headers.  Add the resource include directory
    # explicitly instead of relying on the host driver's implicit search path.
    resource_dir = subprocess.run(
        ["clang", "-print-resource-dir"], check=True,
        capture_output=True, text=True,
    ).stdout.strip()
    cflags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]), "-std=gnu11", "-ffreestanding",
        "-fno-stack-protector", "-fPIC", "-mno-red-zone",
        "-ffunction-sections", "-fdata-sections",
        "-nostdinc", "-isystem", str(args.generated_include),
        "-isystem", str(Path(resource_dir) / "include"),
        "-isystem", str(args.picolibc_prefix / "include"),
        "-I", str(port / "include"),
        "-I", str(args.leonos_include),
        "-I", str(include_dir),
        "-I", str(patched_source),
        # Prefer Picolibc's full POSIX headers for upstream libmagic.  The
        # LeonOS headers remain available for our additive compatibility API
        # without shadowing standard headers such as <stdlib.h>.
        "-idirafter", str(args.leonos_libc_include),
        "-DHAVE_CONFIG_H", "-Dstat(...)=leonos_posix_stat(__VA_ARGS__)",
        "-Dfstat(...)=leonos_posix_fstat(__VA_ARGS__)",
        "-DLEONOS_FILE_PATHSEP_SEMICOLON",
        "-DMAGIC=\"/system/share/misc/magic.mgc\"",
    ]

    def compile_source(path: Path, name: str) -> Path:
        output = object_dir / (name + ".o")
        run(["clang", *cflags, "-c", str(path), "-o", str(output)])
        return output

    library_objects = [compile_source(patched_source / name, Path(name).stem)
                       for name in LIBMAGIC_SOURCES]
    library_objects.append(compile_source(port / "leonos_shim.c", "leonos_shim"))
    static_library = args.static_library.resolve()
    static_library.parent.mkdir(parents=True, exist_ok=True)
    # llvm-ar replaces members named on the command line but retains every
    # other old member.  Always recreate the archive so a locally built
    # libmagic cannot retain sources from an earlier port revision and diverge
    # from CI's clean archive.
    if static_library.exists():
        static_library.unlink()
    run(["llvm-ar", "rcs", str(static_library), *map(str, library_objects)])

    library = args.library.resolve()
    library.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-shared", "-Bsymbolic", "--hash-style=sysv", "-soname", "libmagic.so.1",
        "-z", "max-page-size=0x1000", "-T", str(args.dynamic_linker_script.resolve()),
        "-o", str(library), *map(str, library_objects), str(args.abi_note.resolve()),
        str(args.runtime_so.resolve()),
    ])

    app_objects = [
        compile_source(patched_source / "file.c", "file-main"),
        compile_source(patched_source / "getopt_long.c", "getopt-long"),
    ]
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", "-pie", "--hash-style=sysv",
        "--dynamic-linker", "/system/lib/ld-leonos.elf", "-z", "relro", "-z", "now",
        "-z", "max-page-size=0x1000", *args.linker_flag,
        "-T", str(args.dynamic_linker_script.resolve()),
        "-o", str(args.output.resolve()), str(args.dynamic_crt.resolve()),
        str(args.abi_note.resolve()), *map(str, app_objects), str(args.runtime_so.resolve()),
        str(args.library.resolve()),
    ])
    args.stamp.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.stamp.resolve().write_text("libmagic 5.48\n", encoding="ascii")


if __name__ == "__main__":
    main()
