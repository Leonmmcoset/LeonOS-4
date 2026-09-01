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
COMPONENT_ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")


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
    written_names = getattr(archive, "_leonos_written_names", None)
    if written_names is None:
        written_names = set()
        setattr(archive, "_leonos_written_names", written_names)
    if archive_name in written_names:
        raise SystemExit(f"duplicate SDK archive member: {archive_name}")
    written_names.add(archive_name)
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, source.read_bytes())


def add_text(archive: zipfile.ZipFile, archive_name: str, value: str) -> None:
    written_names = getattr(archive, "_leonos_written_names", None)
    if written_names is None:
        written_names = set()
        setattr(archive, "_leonos_written_names", written_names)
    if archive_name in written_names:
        raise SystemExit(f"duplicate SDK archive member: {archive_name}")
    written_names.add(archive_name)
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, value.encode("utf-8"))


def component_archive_name(component: str, destination: str, source_name: str = "") -> str:
    if not COMPONENT_ID_RE.fullmatch(component):
        raise SystemExit(f"invalid SDK component id: {component}")
    path = Path(destination)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise SystemExit(f"invalid SDK component destination: {destination}")
    return f"{SDK_PREFIX}/components/{component}/{path.as_posix()}" + (
        f"/{source_name}" if source_name else ""
    )


def add_component_tree(
    archive: zipfile.ZipFile, component: str, destination: str, source: Path
) -> None:
    if not source.is_dir():
        raise SystemExit(f"SDK component tree is missing: {source}")
    for item in sorted(source.rglob("*")):
        if not item.is_file():
            continue
        relative = item.relative_to(source)
        # Component sources can be checked-out repositories.  Their VCS
        # metadata is neither part of the SDK nor useful to SDK consumers.
        if ".git" in relative.parts:
            continue
        add_file(
            archive,
            component_archive_name(component, destination, relative.as_posix()),
            item,
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--runtime-so", type=Path, required=True)
    parser.add_argument("--runtime-loader", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--picolibc-lib", type=Path, required=True)
    parser.add_argument("--picolibc-include", type=Path, required=True)
    parser.add_argument("--picolibc-source", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path)
    parser.add_argument("--zlib-lib", type=Path, required=True)
    parser.add_argument("--zlib-source", type=Path, required=True)
    parser.add_argument("--libpng-lib", type=Path, required=True)
    parser.add_argument("--libpng-source", type=Path, required=True)
    parser.add_argument("--libpng-config", type=Path, required=True)
    parser.add_argument("--libmagic-lib", type=Path)
    parser.add_argument("--libmagic-so", type=Path)
    parser.add_argument("--libmagic-source", type=Path)
    parser.add_argument("--libmagic-header", type=Path)
    parser.add_argument("--liblua-lib", type=Path)
    parser.add_argument("--liblua-so", type=Path)
    parser.add_argument("--liblua-source", type=Path)
    parser.add_argument("--sqlite-lib", type=Path)
    parser.add_argument("--sqlite-so", type=Path)
    parser.add_argument("--sqlite-source", type=Path)
    parser.add_argument("--sqlite-header", type=Path)
    parser.add_argument("--sqlite-stamp", type=Path)
    parser.add_argument("--portablegl-lib", type=Path)
    parser.add_argument("--portablegl-so", type=Path)
    parser.add_argument("--portablegl-source", type=Path)
    parser.add_argument("--portablegl-header", type=Path)
    parser.add_argument("--portablegl-stamp", type=Path)
    parser.add_argument("--stardustui-lib", type=Path)
    parser.add_argument("--stardustui-source", type=Path)
    parser.add_argument("--component-metadata", type=Path)
    parser.add_argument("--component-file", nargs=3, action="append", default=[],
                        metavar=("ID", "DESTINATION", "SOURCE"))
    parser.add_argument("--component-tree", nargs=3, action="append", default=[],
                        metavar=("ID", "DESTINATION", "SOURCE"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    sdk_root = args.sdk_root.resolve()
    if not (sdk_root / "Makefile").is_file():
        raise SystemExit(f"SDK source tree not found: {sdk_root}")
    zlib_headers = (args.zlib_source / "zlib.h", args.zlib_source / "zconf.h")
    libpng_headers = (args.libpng_source / "png.h", args.libpng_source / "pngconf.h",
                      args.libpng_config)
    include_libmagic = any((
        args.libmagic_lib is not None,
        args.libmagic_so is not None,
        args.libmagic_source is not None,
        args.libmagic_header is not None,
    ))
    if include_libmagic and not all((
        args.libmagic_lib is not None,
        args.libmagic_so is not None,
        args.libmagic_source is not None,
        args.libmagic_header is not None,
    )):
        raise SystemExit("libmagic SDK inputs must be provided together")
    for required in (args.leonos_lib, args.runtime_so, args.runtime_loader,
                     args.dynamic_crt, args.abi_note,
                     args.picolibc_lib, args.picolibc_include,
                     args.picolibc_source / "COPYING.picolibc", args.zlib_lib,
                     args.zlib_source / "LICENSE", args.libpng_lib,
                     args.libpng_source / "LICENSE",
                     *zlib_headers, *libpng_headers):
        if not required.exists():
            raise SystemExit(f"required SDK input is missing: {required}")
    if include_libmagic:
        assert args.libmagic_lib is not None
        assert args.libmagic_source is not None
        assert args.libmagic_header is not None
        for required in (
            args.libmagic_lib, args.libmagic_so, args.libmagic_source / "COPYING", args.libmagic_header,
        ):
            if not required.exists():
                raise SystemExit(f"required libmagic SDK input is missing: {required}")
    include_lua = any((args.liblua_lib is not None, args.liblua_so is not None,
                       args.liblua_source is not None))
    if include_lua and not all((args.liblua_lib is not None, args.liblua_so is not None,
                                args.liblua_source is not None)):
        raise SystemExit("liblua SDK inputs must be provided together")
    if include_lua:
        assert args.liblua_lib is not None
        assert args.liblua_so is not None
        assert args.liblua_source is not None
        for required in (
            args.liblua_lib, args.liblua_so, args.liblua_source / "README.md",
            *(args.liblua_source / name for name in ("lua.h", "lauxlib.h", "lualib.h", "luaconf.h")),
        ):
            if not required.exists():
                raise SystemExit(f"required liblua SDK input is missing: {required}")
    include_stardustui = args.stardustui_lib is not None or args.stardustui_source is not None
    include_sqlite = any((args.sqlite_lib is not None, args.sqlite_so is not None,
                          args.sqlite_source is not None, args.sqlite_header is not None,
                          args.sqlite_stamp is not None))
    if include_sqlite and not all((args.sqlite_lib is not None, args.sqlite_so is not None,
                                   args.sqlite_source is not None, args.sqlite_header is not None,
                                   args.sqlite_stamp is not None)):
        raise SystemExit("SQLite SDK inputs must be provided together")
    if include_sqlite:
        assert args.sqlite_lib is not None and args.sqlite_so is not None
        assert args.sqlite_source is not None and args.sqlite_header is not None
        assert args.sqlite_stamp is not None
        for required in (args.sqlite_lib, args.sqlite_so, args.sqlite_source / "LICENSE.md",
                         args.sqlite_header, args.sqlite_stamp):
            if not required.exists():
                raise SystemExit(f"required SQLite SDK input is missing: {required}")
    include_portablegl = any((args.portablegl_lib is not None, args.portablegl_so is not None,
                              args.portablegl_source is not None, args.portablegl_header is not None,
                              args.portablegl_stamp is not None))
    if include_portablegl and not all((args.portablegl_lib is not None, args.portablegl_so is not None,
                                       args.portablegl_source is not None, args.portablegl_header is not None,
                                       args.portablegl_stamp is not None)):
        raise SystemExit("PortableGL SDK inputs must be provided together")
    if include_portablegl:
        assert args.portablegl_lib is not None and args.portablegl_so is not None
        assert args.portablegl_source is not None and args.portablegl_header is not None
        assert args.portablegl_stamp is not None
        for required in (args.portablegl_lib, args.portablegl_so,
                         args.portablegl_source / "portablegl.h",
                         args.portablegl_source / "LICENSE", args.portablegl_header,
                         args.portablegl_stamp):
            if not required.exists():
                raise SystemExit(f"required PortableGL SDK input is missing: {required}")
    if include_stardustui:
        if args.stardustui_lib is None or args.stardustui_source is None:
            raise SystemExit("StardustUI SDK inputs must be provided together")
        for required in (
            args.stardustui_lib, args.stardustui_source / "LICENSE",
            args.stardustui_source / "includes",
            args.stardustui_source / "platforms/platform.hpp",
            args.stardustui_source / "settings.hpp",
        ):
            if not required.exists():
                raise SystemExit(f"required StardustUI SDK input is missing: {required}")
    if args.component_metadata is not None and not args.component_metadata.exists():
        raise SystemExit(f"component metadata is missing: {args.component_metadata}")
    for component, destination, source in args.component_file:
        source_path = Path(source)
        if not source_path.is_file():
            raise SystemExit(f"SDK component file is missing: {source_path}")
        component_archive_name(component, destination)
    for component, destination, source in args.component_tree:
        source_path = Path(source)
        if not source_path.is_dir():
            raise SystemExit(f"SDK component tree is missing: {source_path}")
        component_archive_name(component, destination)

    version, revision = version_and_revision(args.picolibc_source)
    picolibc_headers = [
        source for source in sorted(args.picolibc_include.rglob("*"))
        if source.is_file() and source.name != ".leonos-picolibc.stamp"
    ]
    shared_posix_headers = ()
    if args.leonos_libc_include is not None:
        shared_posix_headers = tuple(
            args.leonos_libc_include / name
            for name in (Path("curses.h"), Path("ncurses.h"), Path("leonos/posix.h"))
        )
        for required in shared_posix_headers:
            if not required.is_file():
                raise SystemExit(f"required shared POSIX header is missing: {required}")
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
                if not source.is_file():
                    continue
                relative_name = source.relative_to(sdk_root).as_posix()
                relative_parts = source.relative_to(sdk_root).parts
                # Libraries, selected component payloads, third-party notices,
                # and generated SDK metadata are added below from their
                # authoritative source.  Keeping this broad SDK-tree copy out
                # of those destinations prevents stale inputs and duplicate
                # ZIP members when a component is rebuilt.
                if (
                    "lib" in relative_parts
                    or relative_parts[0] in {"components", "THIRD_PARTY"}
                    or relative_name in {"dynamic-app.ld", "interpreter.ld", "share/leonos/components.json"}
                    or relative_name in {
                        "include/lua5.4/lua.h", "include/lua5.4/lauxlib.h",
                        "include/lua5.4/lualib.h", "include/lua5.4/luaconf.h",
                        "include/sqlite3.h", "include/magic.h", "include/zlib.h",
                        "include/zconf.h", "include/png.h", "include/pngconf.h",
                        "include/pnglibconf.h",
                        "include/curses.h", "include/ncurses.h",
                        "include/leonos/posix.h",
                        "include/portablegl.h", "include/leonos/pgl.h",
                    }
                    or relative_name.startswith("include/stardustui/")
                ):
                    continue
                if relative_name.removeprefix("include/") in picolibc_names:
                    continue
                add_file(archive, f"{SDK_PREFIX}/{relative_name}", source)
            for source in picolibc_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.relative_to(args.picolibc_include).as_posix()}", source)
            for source in shared_posix_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.relative_to(args.leonos_libc_include).as_posix()}", source)
            add_file(archive, f"{SDK_PREFIX}/lib/leonos.a", args.leonos_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libleonos.so.1", args.runtime_so)
            add_file(archive, f"{SDK_PREFIX}/lib/ld-leonos.elf", args.runtime_loader)
            add_file(archive, f"{SDK_PREFIX}/lib/crt0-dynamic.o", args.dynamic_crt)
            add_file(archive, f"{SDK_PREFIX}/lib/leonos-abi-note.o", args.abi_note)
            for name in ("dynamic-app.ld", "interpreter.ld"):
                source = sdk_root.parent / "userland" / name
                if source.is_file():
                    add_file(archive, f"{SDK_PREFIX}/{name}", source)
            add_file(archive, f"{SDK_PREFIX}/lib/libc.a", args.picolibc_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libz.a", args.zlib_lib)
            add_file(archive, f"{SDK_PREFIX}/lib/libpng.a", args.libpng_lib)
            if include_libmagic:
                assert args.libmagic_lib is not None
                assert args.libmagic_so is not None
                add_file(archive, f"{SDK_PREFIX}/lib/libmagic.a", args.libmagic_lib)
                add_file(archive, f"{SDK_PREFIX}/lib/libmagic.so.1", args.libmagic_so)
            if include_lua:
                assert args.liblua_lib is not None
                assert args.liblua_so is not None
                assert args.liblua_source is not None
                add_file(archive, f"{SDK_PREFIX}/lib/liblua.a", args.liblua_lib)
                add_file(archive, f"{SDK_PREFIX}/lib/liblua.so.5", args.liblua_so)
                for name in ("lua.h", "lauxlib.h", "lualib.h", "luaconf.h"):
                    add_file(archive, f"{SDK_PREFIX}/include/lua5.4/{name}", args.liblua_source / name)
            if include_sqlite:
                assert args.sqlite_lib is not None and args.sqlite_so is not None
                assert args.sqlite_source is not None and args.sqlite_header is not None
                assert args.sqlite_stamp is not None
                add_file(archive, f"{SDK_PREFIX}/lib/sqlite.a", args.sqlite_lib)
                add_file(archive, f"{SDK_PREFIX}/lib/sqlite.so.3", args.sqlite_so)
                add_file(archive, f"{SDK_PREFIX}/include/sqlite3.h", args.sqlite_header)
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/SQLITE-LICENSE", args.sqlite_source / "LICENSE.md")
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/SQLITE-VERSION.txt", args.sqlite_stamp)
            if include_portablegl:
                assert args.portablegl_lib is not None and args.portablegl_so is not None
                assert args.portablegl_source is not None and args.portablegl_header is not None
                assert args.portablegl_stamp is not None
                add_file(archive, f"{SDK_PREFIX}/lib/libportablegl.a", args.portablegl_lib)
                add_file(archive, f"{SDK_PREFIX}/lib/libportablegl.so.1", args.portablegl_so)
                add_file(archive, f"{SDK_PREFIX}/include/portablegl.h",
                         args.portablegl_source / "portablegl.h")
                add_file(archive, f"{SDK_PREFIX}/include/leonos/pgl.h", args.portablegl_header)
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/PORTABLEGL-LICENSE",
                         args.portablegl_source / "LICENSE")
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/PORTABLEGL-VERSION.txt",
                         args.portablegl_stamp)
            if include_stardustui:
                assert args.stardustui_lib is not None
                assert args.stardustui_source is not None
                add_file(archive, f"{SDK_PREFIX}/lib/libstardustui.a", args.stardustui_lib)
                for source in sorted(args.stardustui_source.rglob("*.hpp")):
                    relative_name = source.relative_to(args.stardustui_source).as_posix()
                    if relative_name.startswith("includes/") or relative_name in {
                        "platforms/platform.hpp", "settings.hpp"
                    }:
                        add_file(archive, f"{SDK_PREFIX}/include/stardustui/{relative_name}", source)
                for source in sorted((Path("userland/stardustui/include")).glob("*")
                                     if Path("userland/stardustui/include").is_dir() else []):
                    if source.is_file():
                        add_file(archive, f"{SDK_PREFIX}/include/stardustui/leonos/{source.name}", source)
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/PICOLIBC-COPYING", args.picolibc_source / "COPYING.picolibc")
            for source in zlib_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.name}", source)
            for source in libpng_headers:
                add_file(archive, f"{SDK_PREFIX}/include/{source.name}", source)
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/ZLIB-LICENSE", args.zlib_source / "LICENSE")
            add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/LIBPNG-LICENSE", args.libpng_source / "LICENSE")
            if include_libmagic:
                assert args.libmagic_header is not None
                assert args.libmagic_source is not None
                add_file(archive, f"{SDK_PREFIX}/include/magic.h", args.libmagic_header)
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/LIBMAGIC-COPYING", args.libmagic_source / "COPYING")
            if include_lua:
                assert args.liblua_source is not None
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/LUA-LICENSE", args.liblua_source / "README.md")
            if include_stardustui:
                assert args.stardustui_source is not None
                add_file(archive, f"{SDK_PREFIX}/THIRD_PARTY/STARDUSTUI-LICENSE", args.stardustui_source / "LICENSE")
            if args.component_metadata is not None:
                add_file(archive, f"{SDK_PREFIX}/share/leonos/components.json", args.component_metadata)
            for component, destination, source in args.component_file:
                source_path = Path(source)
                add_file(archive, component_archive_name(component, destination), source_path)
            for component, destination, source in args.component_tree:
                add_component_tree(archive, component, destination, Path(source))
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
            if include_libmagic:
                add_text(
                    archive,
                    f"{SDK_PREFIX}/THIRD_PARTY/LIBMAGIC-VERSION.txt",
                    "file/libmagic version: 5.48\n"
                    "Upstream: https://github.com/file/file\n"
                    "License: BSD-2-Clause\n",
                )
            if include_lua:
                add_text(
                    archive,
                    f"{SDK_PREFIX}/THIRD_PARTY/LUA-VERSION.txt",
                    "Lua version: 5.4.8\n"
                    "Upstream: https://www.lua.org/\n"
                    "License: MIT\n",
                )
            if include_stardustui:
                add_text(
                    archive,
                    f"{SDK_PREFIX}/THIRD_PARTY/STARDUSTUI-VERSION.txt",
                    "StardustUI revision: 67aae17214a0d27bb6a8b0caf10b7c1f98313086\n"
                    "Upstream: https://github.com/xingji-studio/StardustUI\n"
                    "License: MIT\n",
                )
        os.replace(temporary_path, output)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


if __name__ == "__main__":
    main()
