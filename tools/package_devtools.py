#!/usr/bin/env python3
"""Create the LeonOS SDK from the WSL-built Picolibc sysroot."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path


SDK_PREFIX = "devtools"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def version_and_revision(source: Path) -> tuple[str, str]:
    version = "unknown"
    meson_build = source / "meson.build"
    if meson_build.is_file():
        match = re.search(r"^\s*version\s*:\s*['\"]([^'\"]+)",
                          meson_build.read_text(encoding="utf-8"), re.MULTILINE)
        if match:
            version = match.group(1)
    try:
        result = subprocess.run(
            ["git", "-C", str(source), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        return version, result.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return version, "unknown"


def add_file(archive: zipfile.ZipFile, archive_name: str, source: Path) -> None:
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, source.read_bytes())


def add_text(archive: zipfile.ZipFile, archive_name: str, value: str) -> None:
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, value.encode("utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--picolibc-include", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--zlib-lib", type=Path, required=True)
    parser.add_argument("--zlib-source", type=Path, required=True)
    parser.add_argument("--libpng-lib", type=Path, required=True)
    parser.add_argument("--libpng-source", type=Path, required=True)
    parser.add_argument("--libpng-config", type=Path, required=True)
    parser.add_argument("--libmagic-lib", type=Path, required=True)
    parser.add_argument("--libmagic-source", type=Path, required=True)
    parser.add_argument("--libmagic-header", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    sdk_root = args.sdk_root.resolve()
    if not (sdk_root / "Makefile").is_file():
        raise SystemExit(f"SDK source tree not found: {sdk_root}")
    zlib_headers = (args.zlib_source / "zlib.h", args.zlib_source / "zconf.h")
    libpng_headers = (args.libpng_source / "png.h", args.libpng_source / "pngconf.h",
                      args.libpng_config)
    for required in (args.leonos_lib, args.picolibc_lib, args.picolibc_include,
                     args.picolibc_source / "COPYING.picolibc", args.zlib_lib,
                     args.zlib_source / "LICENSE", args.libpng_lib,
                     args.libpng_source / "LICENSE", args.libmagic_lib,
                     args.libmagic_source / "COPYING", args.libmagic_header,
                     *zlib_headers, *libpng_headers):
        if not required.exists():
            raise SystemExit(f"required SDK input is missing: {required}")

    version, revision = version_and_revision(args.picolibc_source)
    picolibc_headers = [
        source for source in sorted(args.picolibc_include.rglob("*"))
        if source.is_file() and source.name != ".leonos-picolibc.stamp"
    ]
    picolibc_names = {
        source.relative_to(args.picolibc_include).as_posix()
        for source in picolibc_headers
    }
    output = args.out.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(delete=False, dir=output.parent,
                                     suffix=".zip") as temporary:
        temporary_path = Path(temporary.name)
    try:
        with zipfile.ZipFile(temporary_path, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            for source in sorted(sdk_root.rglob("*")):
                if not source.is_file() or "lib" in source.relative_to(sdk_root).parts:
                    continue
                relative_name = source.relative_to(sdk_root).as_posix()
                if relative_name.removeprefix("include/") in picolibc_names:
                    continue
                add_file(archive, f"{SDK_PREFIX}/{relative_name}", source)
            for source in picolibc_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.relative_to(args.picolibc_include).as_posix()}", source)
            add_file(archive, f"{SDK_PREFIX}/lib/leonos.a", args.leonos_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libc.a", args.picolibc_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libz.a", args.zlib_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libpng.a", args.libpng_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libmagic.a", args.libmagic_lib)
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/PICOLIBC-COPYING", args.picolibc_source / "COPYING.picolibc")
            for source in zlib_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.name}", source)
            for source in libpng_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.name}", source)
            add_file(archive, f"{SDK_PREFIX}/include/magic.h", args.libmagic_header)
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/ZLIB-LICENSE", args.zlib_source / "LICENSE")
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/LIBPNG-LICENSE", args.libpng_source / "LICENSE")
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/LIBMAGIC-COPYING", args.libmagic_source / "COPYING")
            add_text(
                archive,
                f"{SDK_PREFIX}/THIRD_PARTY/PICOLIBC-VERSION.txt",
                f"Picolibc version: {version}\nPicolibc revision: {revision}\n"
                "Upstream: https://github.com/picolibc/picolibc\n",
            )
            add_text(
                archive,
                f"{SDK_PREFIX}/THIRD_PARTY/ZLIB-VERSION.txt",
                "zlib version: 1.3.2\n"
                "Upstream: https://github.com/madler/zlib\n"
                "License: zlib License\n",
            )
            add_text(
                archive,
                f"{SDK_PREFIX}/THIRD_PARTY/LIBPNG-VERSION.txt",
                "libpng version: 1.6.58\n"
                "Upstream: https://github.com/pnggroup/libpng\n"
                "License: libpng License\n",
            )
            add_text(
                archive,
                f"{SDK_PREFIX}/THIRD_PARTY/LIBMAGIC-VERSION.txt",
                "file/libmagic version: 5.48\n"
                "Upstream: https://github.com/file/file\n"
                "License: BSD-2-Clause\n",
            )
        os.replace(temporary_path, output)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


if __name__ == "__main__":
    main()
