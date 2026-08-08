#!/usr/bin/env python3
"""Build the upstream file(1)/libmagic sources for LeonOS."""

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
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--magic-header", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    port = args.port.resolve()
    work = Path(tempfile.mkdtemp(prefix="leonos-file-build-"))
    # libmagic uses PATHSEP to split its database search path.  LeonOS paths
    # contain a drive separator (``0:/``), so the upstream POSIX ':' value
    # would incorrectly turn the default database into ``0.mgc`` and
    # ``/system/share/...``.  Patch only this throw-away build copy; keep the
    # vendored source unchanged.
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
    compiler_runtime = Path(resource_dir) / "lib/linux/libclang_rt.builtins-x86_64.a"
    if not compiler_runtime.is_file():
        raise SystemExit(f"Clang x86_64 compiler runtime is missing: {compiler_runtime}")

    cflags = [
        "-target", "x86_64-unknown-none", "-O2", "-std=gnu11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone",
        "-ffunction-sections", "-fdata-sections",
        "-nostdinc", "-isystem", str(args.generated_include),
        "-isystem", str(Path(resource_dir) / "include"),
        "-isystem", str(args.picolibc_prefix / "include"),
        "-I", str(port / "include"), "-I", str(args.leonos_include),
        "-I", str(include_dir),
        "-I", str(patched_source),
        "-DHAVE_CONFIG_H", "-Dstat(...)=leonos_posix_stat(__VA_ARGS__)",
        "-Dfstat(...)=leonos_posix_fstat(__VA_ARGS__)",
        "-DLEONOS_FILE_PATHSEP_SEMICOLON",
        "-DMAGIC=\"0:/system/share/misc/magic.mgc\"",
    ]

    def compile_source(path: Path, name: str) -> Path:
        output = object_dir / (name + ".o")
        run(["clang", *cflags, "-c", str(path), "-o", str(output)])
        return output

    library_objects = [compile_source(patched_source / name, Path(name).stem)
                       for name in LIBMAGIC_SOURCES]
    library_objects.append(compile_source(port / "leonos_shim.c", "leonos_shim"))
    args.library.resolve().parent.mkdir(parents=True, exist_ok=True)
    run(["llvm-ar", "rcs", str(args.library.resolve()), *map(str, library_objects)])

    app_objects = [
        compile_source(patched_source / "file.c", "file-main"),
        compile_source(patched_source / "getopt_long.c", "getopt-long"),
    ]
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", "-z", "max-page-size=0x1000",
        "-u", "_start", "-T", str(args.linker_script.resolve()),
        "-o", str(args.output.resolve()), *map(str, app_objects), "--start-group",
        str(args.library.resolve()), str(args.leonos_lib.resolve()),
        str(args.picolibc_lib.resolve()), str(compiler_runtime), "--end-group",
    ])
    args.stamp.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.stamp.resolve().write_text("libmagic 5.48\n", encoding="ascii")


if __name__ == "__main__":
    main()
