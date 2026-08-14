#!/usr/bin/env python3
"""LeonOS BuildSystem: the only supported build entry point."""

from __future__ import annotations

import argparse
import contextlib
import fnmatch
import hashlib
import io
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, Iterable

from buildsystem.core import (
    BuildFailure,
    BuildGraph,
    BuildPaths,
    BuildRunner,
    BuildSettings,
    GraphError,
    Target,
    TaskStore,
    edit_settings,
    load_settings,
    show_map,
)
from buildsystem.core.runner import ActionContext, CYAN, GREEN, RED, RESET
from buildsystem.core.state import utc_now
from buildsystem.components import (
    component_config_symbols,
    load_components,
    resolve_components,
    validate_component_targets,
)


ROOT = Path(__file__).resolve().parent
ROOT_POSIX_PREFIX = ROOT.as_posix().rstrip("/") + "/"
PYTHON = sys.executable

if os.name == "nt":
    raise SystemExit(
        "LeonOS 4 must be built from Linux or WSL; do not run build.py with Windows Python."
    )

DRIVER_MODULES = ["mouse", "serial", "e1000", "ac97", "es1371"]
CONFIG_CHOICE_GROUPS = (
    ("CONFIG_VMDK_DEFAULT_LANGUAGE_EN", "CONFIG_VMDK_DEFAULT_LANGUAGE_ZH"),
    ("CONFIG_VMDK_DEFAULT_THEME_METRO", "CONFIG_VMDK_DEFAULT_THEME_WIN95"),
    ("CONFIG_VMDK_WALLPAPER_FILL", "CONFIG_VMDK_WALLPAPER_STRETCH",
     "CONFIG_VMDK_WALLPAPER_CENTER"),
    ("CONFIG_BUILD_PRESET_DEBUG", "CONFIG_BUILD_PRESET_DEVELOP",
     "CONFIG_BUILD_PRESET_RELEASE"),
)
BUILD_PRESET_VALUES = {
    "CONFIG_BUILD_PRESET_DEBUG": {
        "CONFIG_BUILD_OPTIMIZATION_LEVEL": "0",
        "CONFIG_BUILD_DEBUG_SYMBOLS": "y",
        "CONFIG_BUILD_ENABLE_LTO": "n",
        "CONFIG_BUILD_STRIP_BINARIES": "n",
        "CONFIG_BUILD_DEVELOPER_DIAGNOSTICS": "y",
    },
    "CONFIG_BUILD_PRESET_DEVELOP": {
        "CONFIG_BUILD_OPTIMIZATION_LEVEL": "2",
        "CONFIG_BUILD_DEBUG_SYMBOLS": "y",
        "CONFIG_BUILD_ENABLE_LTO": "n",
        "CONFIG_BUILD_STRIP_BINARIES": "n",
        "CONFIG_BUILD_DEVELOPER_DIAGNOSTICS": "n",
    },
    "CONFIG_BUILD_PRESET_RELEASE": {
        "CONFIG_BUILD_OPTIMIZATION_LEVEL": "3",
        "CONFIG_BUILD_DEBUG_SYMBOLS": "n",
        "CONFIG_BUILD_ENABLE_LTO": "n",
        "CONFIG_BUILD_STRIP_BINARIES": "y",
        "CONFIG_BUILD_DEVELOPER_DIAGNOSTICS": "n",
    },
}
BUILD_NUMBER_EXEMPT_TARGETS = frozenset({
    "clean",
    "config-sync",
    "menuconfig",
    "test-license-server",
    "test-los2w",
    "test-qmp-terminal",
    "test-qmp-pleditor",
    "test-qmp-tcc",
    "test-qmp-fastfetch",
    "test-qmp-dynlinkerror",
    "test-qmp-cmd",
    "test-qmp-stardust",
    "test-component-config",
    "test-all",
})
WINDOW_BUTTON_ICONS = [
    "window-button-minimize.bmp",
    "window-button-maximize.bmp",
    "window-button-restore.bmp",
    "window-button-close.bmp",
]
MINESWEEPER_ASSETS = [
    "minesweeper-mine.bmp",
    "minesweeper-flag.bmp",
]
SYSTEM_FILES = [
    ("system/resources/mouse.bmp", "system/resources/mouse.bmp"),
    ("system/resources/wallpaper-metro.bmp", "system/resources/wallpaper-metro.bmp"),
    ("system/certs/cacert.pem", "system/certs/cacert.pem"),
    ("third_party/doomgeneric/FREEDOOM-COPYING.txt", "system/docs/FREEDOOM-COPYING.txt"),
]


def runtime_app_relative(app: str, extension: str, system_apps: set[str]) -> Path:
    root = "system/apps" if app in system_apps else "programs"
    return Path(root) / app / f"{app}.{extension}"

MBEDTLS_SOURCES = [
    "aes.c", "asn1parse.c", "asn1write.c", "base64.c", "bignum.c", "cipher.c",
    "cipher_wrap.c", "constant_time.c", "ctr_drbg.c", "ecdh.c", "ecdsa.c", "ecp.c",
    "ecp_curves.c", "entropy.c", "gcm.c", "md.c", "oid.c", "pem.c", "pk.c",
    "pkparse.c", "pk_wrap.c", "platform.c", "platform_util.c", "rsa.c",
    "rsa_internal.c", "sha1.c", "sha256.c", "sha512.c", "ssl_ciphersuites.c",
    "ssl_cli.c", "ssl_msg.c", "ssl_tls.c", "x509.c", "x509_crt.c",
]

ZLIB_SOURCES = [
    "adler32.c", "compress.c", "crc32.c", "deflate.c", "infback.c",
    "inffast.c", "inflate.c", "inftrees.c", "trees.c", "uncompr.c",
    "zutil.c",
]

LIBPNG_SOURCES = [
    "png.c", "pngerror.c", "pngget.c", "pngmem.c", "pngpread.c",
    "pngread.c", "pngrio.c", "pngrtran.c", "pngrutil.c", "pngset.c",
    "pngtrans.c", "pngwio.c", "pngwrite.c", "pngwtran.c", "pngwutil.c",
]

_COLLECT_CACHE: dict[tuple[str, ...], tuple[Path, ...]] = {}
_COLLECT_TREE_CACHE: dict[Path, tuple[Path, ...]] = {}
_COLLECT_RELATIVE_CACHE: dict[Path, tuple[str, ...]] = {}
_COLLECT_PATTERN_CACHE: dict[str, tuple[str, ...]] = {}
_GLOB_MARKERS = frozenset("*?[")


def root_path(value: str | Path) -> Path:
    path = Path(value)
    return ROOT / path if not path.is_absolute() else path


def relative(path: Path) -> str:
    # 如果路径是绝对路径且不在 ROOT 下，保持绝对路径
    if path.is_absolute():
        try:
            return path.relative_to(ROOT).as_posix()
        except ValueError:
            # 路径不在 ROOT 下，返回绝对路径
            return path.as_posix()
    return path.as_posix()


def _pattern_segments(pattern: str) -> tuple[str, ...]:
    cached = _COLLECT_PATTERN_CACHE.get(pattern)
    if cached is not None:
        return cached
    normalized = pattern.replace("\\", "/").strip("/")
    segments = tuple(segment for segment in normalized.split("/") if segment and segment != ".")
    _COLLECT_PATTERN_CACHE[pattern] = segments
    return segments


def _has_glob(segment: str) -> bool:
    return segment == "**" or any(marker in segment for marker in _GLOB_MARKERS)


def _pattern_base(pattern: str) -> Path:
    base: list[str] = []
    for segment in _pattern_segments(pattern):
        if _has_glob(segment):
            break
        base.append(segment)
    return ROOT.joinpath(*base) if base else ROOT


def _tree_files(base: Path) -> tuple[Path, ...]:
    cached = _COLLECT_TREE_CACHE.get(base)
    if cached is not None:
        return cached
    if base.is_file():
        files = (base,)
    elif base.is_dir():
        found: list[Path] = []
        for directory, _, filenames in os.walk(base):
            parent = Path(directory)
            found.extend(parent / filename for filename in filenames)
        files = tuple(found)
    else:
        files = ()
    _COLLECT_TREE_CACHE[base] = files
    return files


def _relative_segments(path: Path) -> tuple[str, ...]:
    cached = _COLLECT_RELATIVE_CACHE.get(path)
    if cached is not None:
        return cached
    text = path.as_posix()
    if text.startswith(ROOT_POSIX_PREFIX):
        text = text[len(ROOT_POSIX_PREFIX):]
    else:
        text = path.relative_to(ROOT).as_posix()
    segments = tuple(segment for segment in text.split("/") if segment)
    _COLLECT_RELATIVE_CACHE[path] = segments
    return segments


def _match_segments(pattern: tuple[str, ...], value: tuple[str, ...]) -> bool:
    if not pattern:
        return not value
    head = pattern[0]
    tail = pattern[1:]
    if head == "**":
        if not tail:
            return True
        return any(_match_segments(tail, value[index:]) for index in range(len(value) + 1))
    if not value or not fnmatch.fnmatchcase(value[0], head):
        return False
    return _match_segments(tail, value[1:])


def collect(*patterns: str) -> list[Path]:
    cached = _COLLECT_CACHE.get(patterns)
    if cached is not None:
        return list(cached)
    result: list[Path] = []
    for pattern in patterns:
        if not any(marker in pattern for marker in _GLOB_MARKERS):
            path = root_path(pattern)
            if path.is_file():
                result.append(path)
            continue
        segments = _pattern_segments(pattern)
        base = _pattern_base(pattern)
        result.extend(path for path in _tree_files(base) if _match_segments(segments, _relative_segments(path)))
    paths = tuple(sorted(result))
    _COLLECT_CACHE[patterns] = paths
    return list(paths)


def object_path(paths: BuildPaths, source: Path, prefix: str) -> Path:
    rel = source.relative_to(ROOT)
    return paths.objects / prefix / rel.with_suffix(rel.suffix + ".o")


def user_app_sources(app: str) -> list[Path]:
    sources = collect(f"userland/apps/{app}/*.c", f"userland/apps/{app}/*.S")
    if app == "doom":
        sources.extend(
            source for source in collect("third_party/doomgeneric/doomgeneric/*.c")
            if source.name not in {
                "doomgeneric_allegro.c", "doomgeneric_emscripten.c",
                "doomgeneric_linuxvt.c", "doomgeneric_sdl.c", "doomgeneric_soso.c",
                "doomgeneric_sosox.c", "doomgeneric_win.c", "doomgeneric_xlib.c",
                "i_allegromusic.c", "i_allegrosound.c", "i_sdlsound.c",
                "i_sdlmusic.c", "i_cdmus.c", "mus2mid.c",
            }
        )
    if app == "oobe":
        sources.extend(source for source in collect("userland/apps/browser/*.c") if source.name != "main.c")
    return sorted(set(sources))


def parse_config_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2 : -len(" is not set")]
            values[key] = "n"
        elif "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            if key.strip().startswith("CONFIG_"):
                values[key.strip()] = value.strip()
    return values


def parse_kconfig(path: Path) -> dict[str, str]:
    values = parse_config_values(ROOT / "configs/default.conf")
    configured = parse_config_values(path)
    components = load_components(ROOT / "configs/components.toml")
    validate_component_targets(components, ROOT)
    known = set(values) | component_config_symbols(components)
    unknown = sorted(key for key in configured if key not in known)
    if unknown:
        raise BuildFailure("unknown configuration symbol(s): " + ", ".join(unknown))
    values.update(configured)
    if values.get("CONFIG_BUILD_USE_ADVANCED_OVERRIDES") != "y":
        preset = next(
            (key for key in CONFIG_CHOICE_GROUPS[-1] if values.get(key) == "y"),
            "CONFIG_BUILD_PRESET_DEVELOP",
        )
        values.update(BUILD_PRESET_VALUES[preset])
    return values


def config_int(values: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(values[key], 10)
    except (KeyError, ValueError):
        return default


def config_bool(values: dict[str, str], key: str) -> bool:
    return values.get(key) == "y"


def config_string(values: dict[str, str], key: str) -> str:
    value = values.get(key, "").strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        value = value[1:-1]
    return value.replace('\\"', '"').replace("\\\\", "\\")


def ensure_parent(context: ActionContext, output: Path, text: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    previous = output.read_text(encoding="utf-8") if output.exists() else None
    if previous != text:
        output.write_text(text, encoding="utf-8", newline="\n")


def copy_action(source: Path, destination: Path) -> Callable[[ActionContext], None]:
    def action(context: ActionContext) -> None:
        context.copy(source, destination)

    return action


def text_action(destination: Path, text: str) -> Callable[[ActionContext], None]:
    def action(context: ActionContext) -> None:
        ensure_parent(context, destination, text)

    return action


def add_copy(graph: BuildGraph, name: str, source: Path, destination: Path) -> Target:
    return graph.add(
        Target(
            name=name,
            outputs=(destination,),
            inputs=(source,),
            kind="generate",
            source=source,
            action=copy_action(source, destination),
            action_key="copy-v2",
        )
    )


def add_compile(
    graph: BuildGraph,
    paths: BuildPaths,
    name: str,
    source: Path,
    prefix: str,
    flags: list[str],
    implicit: Iterable[Path] = (),
    *,
    kind: str = "compile",
) -> Path:
    output = object_path(paths, source, prefix)
    depfile = output.with_suffix(output.suffix + ".d")
    command = tuple(flags + ["-MMD", "-MF", relative(depfile), "-c", relative(source), "-o", relative(output)])
    graph.add(
        Target(
            name=name,
            outputs=(output,),
            inputs=(source,),
            implicit_inputs=tuple(implicit),
            kind=kind,
            source=source,
            command=command,
            depfile=depfile,
        )
    )
    return output


def add_link(
    graph: BuildGraph,
    name: str,
    output: Path,
    inputs: Iterable[Path],
    command: list[str],
    implicit: Iterable[Path] = (),
) -> Target:
    return graph.add(
        Target(
            name=name,
            outputs=(output,),
            inputs=tuple(inputs),
            implicit_inputs=tuple(implicit),
            kind="link",
            command=tuple(command + ["-o", relative(output)]),
        )
    )


def qemu_command(paths: BuildPaths, values: dict[str, str], *, debug: bool = False, iso: bool = False) -> tuple[str, ...]:
    memory = config_int(values, "CONFIG_QEMU_MEMORY_MB")
    width = config_int(values, "CONFIG_QEMU_DISPLAY_WIDTH")
    height = config_int(values, "CONFIG_QEMU_DISPLAY_HEIGHT")
    command = ["qemu-system-x86_64"]
    if config_bool(values, "CONFIG_QEMU_ENABLE_KVM"):
        command += ["-enable-kvm", "-cpu", "host"]
    else:
        # QEMU's host CPU model is valid only with KVM/HVF.  TCG uses max so
        # the smoke tests remain runnable on hosts without hardware access.
        command += ["-cpu", "max"]
    command += ["-machine", "q35", "-m", f"{memory}M"]
    ovmf = config_string(values, "CONFIG_QEMU_OVMF_PATH")
    if ovmf:
        command += ["-bios", ovmf]
    command += ["-serial", "stdio", "-display", "none" if debug or iso else os.environ.get("LEONOS_QEMU_DISPLAY", "sdl")]
    command += ["-device", f"VGA,xres={width},yres={height}"]
    if debug or iso:
        command += ["-no-reboot", "-no-shutdown"]
    net_model = config_string(values, "CONFIG_QEMU_NET_DEVICE")
    if net_model and all(character.isalnum() or character in "._-" for character in net_model):
        command += ["-netdev", "user,id=net0", "-device", f"{net_model},netdev=net0"]
    command += ["-audiodev", "sdl,id=snd0", "-device", "AC97,audiodev=snd0"]
    if iso:
        command += ["-cdrom", relative(paths.images / "leonos4.iso")]
    command += [
        "-drive", f"file={relative(paths.images / 'leonos4.vmdk')},if=none,id=sata0,format=vmdk",
        "-device", "ich9-ahci,id=ahci", "-device", "ide-hd,drive=sata0,bus=ahci.0",
    ]
    return tuple(command)


def build_graph(paths: BuildPaths, config_path: Path | None = None) -> BuildGraph:
    graph = BuildGraph(ROOT)
    config_path = config_path or paths.kconfig
    values = parse_kconfig(config_path)
    components = load_components(ROOT / "configs/components.toml")
    component_selection = resolve_components(components, values)
    components_by_id = {component.id: component for component in components}
    build_user_apps = [
        component.id
        for component in components
        if component.kind in {"system-app", "program-app", "package-app"}
        and bool(component_selection[component.id]["build"])
    ]
    staged_user_apps = [
        component.id
        for component in components
        if component.kind in {"system-app", "program-app"}
        and bool(component_selection[component.id]["image"])
    ]
    system_apps = {
        component.id for component in components if component.kind == "system-app"
    }
    stardustui_apps = {
        component.id for component in components if "stardustui" in component.depends
    }
    def component_enabled(component_id: str, option: str = "build") -> bool:
        record = component_selection.get(component_id)
        return bool(record and record.get(option))

    def component_api_destination(component_id: str) -> Path:
        stage_path = components_by_id[component_id].api_stage_path
        if not stage_path:
            raise GraphError(f"component {component_id} has no API staging path")
        return paths.staging / stage_path

    def component_api_enabled(component_id: str) -> bool:
        return component_enabled(component_id, "api")

    installer_policy_apps = tuple(
        app for app in ("desktop", "oobe", "settings") if component_enabled(app, "image")
    )
    cc = os.environ.get("CC", "clang")
    cxx = os.environ.get("CXX", "clang++")
    rustc = os.environ.get("RUSTC", "rustc")
    ar = os.environ.get("AR", "llvm-ar")
    ld = os.environ.get("LD", "ld.lld")
    objcopy = os.environ.get("OBJCOPY", "llvm-objcopy")
    compiler_rt = subprocess.run(
        (cc, "-target", "x86_64-unknown-none", "-rtlib=compiler-rt",
         "--print-libgcc-file-name"),
        check=False, text=True, capture_output=True,
    )
    compiler_rt_archive = Path(compiler_rt.stdout.strip())
    # Debian/Ubuntu package compiler-rt under ``lib/linux`` while Clang's
    # generic target query returns the adjacent cross-target location.
    if not compiler_rt_archive.is_file():
        linux_candidate = compiler_rt_archive.parent / "linux" / compiler_rt_archive.name
        if linux_candidate.is_file():
            compiler_rt_archive = linux_candidate
    if compiler_rt.returncode or not compiler_rt_archive.is_file():
        raise GraphError("Clang compiler-rt builtins archive is required for the dynamic runtime")
    generated = paths.generated_include
    autoconf = generated / "autoconf.h"
    installer_autoconf = generated / "autoconf-installer.h"
    rustcfg = generated / "rustcfg.args"
    build_info = ROOT / "include/generated/build_info.h"
    loader_integrity = generated / "loader_integrity.h"
    gbk_table_header = generated / "leonos_gbk_table.h"
    picolibc_source = ROOT / "third_party/picolibc"
    picolibc_cross_file = ROOT / "userland/picolibc/leonos-x86_64.ini"
    picolibc_static_build_dir = paths.out / "picolibc-static"
    picolibc_static_prefix = picolibc_static_build_dir / "sysroot"
    picolibc_static_archive = picolibc_static_prefix / "lib/libc.a"
    picolibc_build_dir = paths.out / "picolibc"
    picolibc_prefix = picolibc_build_dir / "sysroot"
    picolibc_archive = picolibc_prefix / "lib/libc.a"
    runtime_so = paths.out / "system/lib/libleonos.so.1"
    runtime_loader = paths.out / "system/lib/ld-leonos.elf"
    libmagic_so = paths.out / "system/lib/libmagic.so.1"
    liblua_so = paths.out / "system/lib/liblua.so.5"
    sqlite_source = ROOT / "third_party/sqlite"
    sqlite_port = ROOT / "userland/sqlite"
    sqlite_so = paths.out / "system/lib/sqlite.so.3"
    sqlite_archive = paths.out / "userland/sqlite.a"
    sqlite_header = paths.out / "generated/sqlite/sqlite3.h"
    sqlite_stamp = paths.out / "userland/sqlite.stamp"
    sqlite_work_dir = paths.out / "sqlite-work"
    dynlinkerror_elf = paths.out / "userland/dynlinkerror.elf"
    installer_runtime_so = paths.out / "userland-installer-policy/libleonos.so.1"
    picolibc_header_stamp = picolibc_prefix / "include/.leonos-picolibc.stamp"
    zlib_source = ROOT / "third_party/zlib"
    libpng_source = ROOT / "third_party/libpng"
    libpng_config_source = libpng_source / "scripts/pnglibconf.h.prebuilt"
    libpng_generated_dir = paths.out / "generated/libpng"
    libpng_config = libpng_generated_dir / "pnglibconf.h"
    zlib_archive = paths.out / "userland/libz.a"
    libpng_archive = paths.out / "userland/libpng.a"
    file_source = ROOT / "third_party/file"
    file_port = ROOT / "userland/file"
    file_elf = paths.out / "userland/file.elf"
    libmagic_archive = paths.out / "userland/libmagic.a"
    file_stamp = paths.out / "userland/file.stamp"
    file_magic_header = paths.out / "generated/file/magic.h"
    magic_database = paths.out / "userland/magic.mgc"
    magic_database_stamp = paths.out / "userland/magic.stamp"
    busybox_source = ROOT / "third_party/busybox"
    busybox_config = ROOT / "userland/busybox/leonos.config"
    busybox_shim = ROOT / "userland/busybox/leonos_shim.c"
    busybox_headers = collect("userland/busybox/include/**/*.h")
    busybox_source_stamp = paths.out / "busybox/source-revision.txt"
    busybox_elf = paths.out / "userland/busybox.elf"
    busybox_stamp = paths.out / "userland/busybox.stamp"
    nano_source = ROOT / "third_party/nano"
    nano_port = ROOT / "userland/nano"
    nano_elf = paths.out / "userland/nano.elf"
    nano_stamp = paths.out / "userland/nano.stamp"
    nano_work_dir = paths.out / "nano-work"
    fastfetch_source = ROOT / "third_party/fastfetch"
    fastfetch_port = ROOT / "userland/fastfetch"
    fastfetch_elf = paths.out / "userland/fastfetch.elf"
    fastfetch_stamp = paths.out / "userland/fastfetch.stamp"
    fastfetch_work_dir = paths.out / "fastfetch-work"
    tcc_source = ROOT / "third_party/tinycc"
    tcc_port = ROOT / "userland/tcc"
    tcc_app_manifest = ROOT / "userland/apps/tcc/tcc.app.ini"
    tcc_elf = paths.out / "userland/tcc.elf"
    tcc_runtime_dir = paths.out / "tcc-runtime"
    tcc_stamp = paths.out / "userland/tcc.stamp"
    lua_source = ROOT / "third_party/lua"
    lua_port = ROOT / "userland/lua"
    lua_app_manifest = ROOT / "userland/apps/lua/lua.app.ini"
    lua_elf = paths.out / "userland/lua.elf"
    liblua_archive = paths.out / "userland/liblua.a"
    lua_stamp = paths.out / "userland/lua.stamp"
    lua_work_dir = paths.out / "lua-work"
    cmd_source = ROOT / "third_party/cmd"
    cmd_port = ROOT / "userland/cmd"
    cmd_elf = paths.out / "userland/cmd.elf"
    cmd_stamp = paths.out / "userland/cmd.stamp"
    cmd_work_dir = paths.out / "cmd-work"
    pleditor_source = ROOT / "third_party/pl_editor"
    pleditor_port = ROOT / "userland/apps/pleditor"
    pleditor_elf = paths.out / "userland/pleditor.elf"
    pleditor_stamp = paths.out / "userland/pleditor.stamp"
    pleditor_work_dir = paths.out / "pleditor-work"
    developer_sdk = ROOT / "LeonOS4-Developer-SDK.zip"
    grub_efi_dir = paths.deps / "grub-efi-amd64-bin/usr/lib/grub/x86_64-efi"
    system_grub_efi_dir = Path("/usr/lib/grub/x86_64-efi")
    using_system_grub = False
    if not (grub_efi_dir / "modinfo.sh").exists() and (system_grub_efi_dir / "modinfo.sh").exists():
        grub_efi_dir = system_grub_efi_dir
        using_system_grub = True

    def sync_config(context: ActionContext) -> None:
        context.run(
            (
                PYTHON, "tools/generate_component_kconfig.py",
            ),
            announce=True,
        )
        context.run(
            (
                PYTHON, "tools/kconfig_sync.py", "--config", relative(config_path),
                "--defaults", "configs/default.conf", "--out-dir", relative(generated),
                "--selection-out", relative(paths.out / "generated/component-selection.json"),
            ),
            announce=True,
        )

    graph.add(
        Target(
            name="config-sync",
            outputs=(config_path, autoconf, installer_autoconf, rustcfg,
                     paths.out / "generated/component-selection.json"),
            inputs=(ROOT / "configs/default.conf", ROOT / "configs/components.toml",
                    ROOT / "Kconfig", ROOT / "Kconfig.components",
                    ROOT / "tools/generate_component_kconfig.py",
                    ROOT / "tools/kconfig_sync.py", ROOT / "buildsystem/components.py"),
            kind="generate",
            action=sync_config,
            action_key="kconfig-sync-v3",
            always=True,
        )
    )
    def generate_libpng_config(context: ActionContext) -> None:
        config_text = libpng_config_source.read_text(encoding="utf-8")
        marker = "/* end of options */"
        if marker not in config_text:
            raise GraphError("unsupported libpng revision: pnglibconf.h format changed")
        # Normal LeonOS user processes deliberately avoid x87/SSE state.  Keep
        # libpng's fixed-point path instead of introducing soft-float runtime
        # helpers to every PNG consumer.
        config_text = config_text.replace(
            marker,
            "#undef PNG_FLOATING_ARITHMETIC_SUPPORTED\n"
            "#undef PNG_FLOATING_POINT_SUPPORTED\n"
            "#undef PNG_READ_FLOAT_SUPPORTED\n"
            "#undef PNG_INCH_CONVERSIONS_SUPPORTED\n"
            + marker,
            1,
        )
        ensure_parent(context, libpng_config, config_text)

    graph.add(Target(
        name="generate:libpng-config",
        outputs=(libpng_config,),
        inputs=(libpng_config_source,),
        kind="generate",
        action=generate_libpng_config,
        action_key="generate-libpng-config-v3",
    ))
    if not (picolibc_source / "meson.build").is_file():
        raise GraphError("third_party/picolibc is missing; initialize the Picolibc source tree")
    if not (busybox_source / "Makefile").is_file():
        raise GraphError("third_party/busybox is missing; initialize the BusyBox source tree")
    if not (nano_source / "src/nano.c").is_file():
        raise GraphError("third_party/nano is missing; initialize the GNU nano source tree")
    if not (fastfetch_source / "src/fastfetch.h").is_file() or not (fastfetch_source / "LICENSE").is_file():
        raise GraphError("third_party/fastfetch is missing; initialize the Fastfetch source tree")
    if not (fastfetch_port / "main.c").is_file() or not (fastfetch_port / "include/fastfetch_config.h").is_file():
        raise GraphError("the LeonOS Fastfetch port metadata is missing")
    if not (tcc_source / "tcc.c").is_file():
        raise GraphError("third_party/tinycc is missing; initialize the TinyCC source tree")
    if not (lua_source / "lua.c").is_file() or not (lua_source / "lua.h").is_file():
        raise GraphError("third_party/lua is missing; initialize the Lua source tree")
    if not (cmd_source / "cmain.c").is_file() or not (cmd_source / "LICENSE").is_file():
        raise GraphError("third_party/cmd is missing; initialize the ChenPi11/cmd source tree")
    if not (lua_port / "LICENSE").is_file() or not lua_app_manifest.is_file():
        raise GraphError("the LeonOS Lua port metadata is missing")
    if not (pleditor_source / "src/pleditor.c").is_file():
        raise GraphError("third_party/pl_editor is missing; initialize the PL Editor source tree")
    if not (zlib_source / "zlib.h").is_file():
        raise GraphError("third_party/zlib is missing; initialize the zlib source tree")
    if not (libpng_source / "png.h").is_file() or not libpng_config_source.is_file():
        raise GraphError("third_party/libpng is missing; initialize the libpng source tree")
    if not (file_source / "configure.ac").is_file() or not (file_source / "magic").is_dir():
        raise GraphError("third_party/file is missing; initialize the libmagic source tree")
    if not (file_port / "config.h").is_file() or not (file_port / "leonos_shim.c").is_file():
        raise GraphError("the LeonOS file/libmagic port metadata is missing")
    if not (sqlite_source / "main.mk").is_file() or not (sqlite_source / "Makefile.linux-gcc").is_file():
        raise GraphError("third_party/sqlite is missing; initialize the SQLite source tree")
    if not (sqlite_port / "leonos_sqlite_vfs.c").is_file():
        raise GraphError("the LeonOS SQLite VFS port metadata is missing")
    optimization_level = config_int(values, "CONFIG_BUILD_OPTIMIZATION_LEVEL", 2)
    optimization_flag = f"-O{max(0, min(3, optimization_level))}"
    build_compile_flags: list[str] = [optimization_flag]
    if config_bool(values, "CONFIG_BUILD_DEBUG_SYMBOLS"):
        build_compile_flags.append("-g")
    if config_bool(values, "CONFIG_BUILD_ENABLE_LTO"):
        build_compile_flags.append("-flto=thin")
    build_link_flags: list[str] = []
    if config_bool(values, "CONFIG_BUILD_ENABLE_LTO"):
        build_link_flags.append(f"--lto-O{max(0, min(3, optimization_level))}")
    if config_bool(values, "CONFIG_BUILD_STRIP_BINARIES"):
        build_link_flags.append("--strip-all")
    compile_option_args = tuple(f"--compile-flag={flag}" for flag in build_compile_flags)
    linker_option_args = tuple(f"--linker-flag={flag}" for flag in build_link_flags)
    picolibc_inputs = collect(
        "third_party/picolibc/**/*.c",
        "third_party/picolibc/**/*.h",
        "third_party/picolibc/**/*.S",
        "third_party/picolibc/**/meson.build",
        "third_party/picolibc/meson_options.txt",
    )
    graph.add(
        Target(
            name="picolibc",
            outputs=(picolibc_archive, picolibc_header_stamp),
            inputs=tuple([ROOT / "tools/build_picolibc.py", picolibc_cross_file,
                          *picolibc_inputs]),
            kind="compile",
            command=(
                PYTHON, "tools/build_picolibc.py",
                "--source", "third_party/picolibc",
                "--cross-file", "userland/picolibc/leonos-x86_64.ini",
                "--build-dir", relative(picolibc_build_dir),
                "--prefix", relative(picolibc_prefix),
                "--archive", relative(picolibc_archive),
                "--stamp", relative(picolibc_header_stamp),
                *compile_option_args,
                "--compile-flag=-fPIC",
                *linker_option_args,
            ),
        )
    )
    graph.add(
        Target(
            name="picolibc-static",
            outputs=(picolibc_static_archive,),
            inputs=tuple([ROOT / "tools/build_picolibc.py", picolibc_cross_file, *picolibc_inputs]),
            kind="compile",
            command=(PYTHON, "tools/build_picolibc.py", "--source", "third_party/picolibc",
                     "--cross-file", "userland/picolibc/leonos-x86_64.ini",
                     "--build-dir", relative(picolibc_static_build_dir),
                     "--prefix", relative(picolibc_static_prefix),
                     "--archive", relative(picolibc_static_archive),
                     "--stamp", relative(picolibc_static_prefix / "include/.leonos-picolibc.stamp"),
                     *compile_option_args, *linker_option_args),
        )
    )
    graph.add(
        Target(
            name="build-info",
            outputs=(build_info, paths.state / "build_number.txt"),
            inputs=(ROOT / "tools/build_info.py",),
            kind="generate",
            command=(
                PYTHON, "tools/build_info.py", "--header", relative(build_info), "--state",
                relative(paths.state / "build_number.txt"),
            ),
            always=True,
        )
    )

    default_theme = "win95" if config_bool(values, "CONFIG_VMDK_DEFAULT_THEME_WIN95") else "metro"
    wallpaper_mode = (
        "stretch" if config_bool(values, "CONFIG_VMDK_WALLPAPER_STRETCH")
        else "center" if config_bool(values, "CONFIG_VMDK_WALLPAPER_CENTER")
        else "fill"
    )
    generated_display_config = paths.out / "generated/config/display.conf"
    graph.add(Target(
        name="generate:display-config",
        outputs=(generated_display_config,),
        inputs=(config_path,),
        kind="generate",
        action=text_action(
            generated_display_config,
            f"theme={default_theme}\nwallpaper.mode={wallpaper_mode}\n",
        ),
        action_key="generate-display-config-v1",
    ))
    desktop_entry_policy = paths.out / "generated/config/desktop-entries.conf"
    hidden_desktop_entries: list[str] = []
    for component in components:
        if component.kind not in {"system-app", "program-app"}:
            continue
        if component_enabled(component.id, "image") and not component_enabled(component.id, "entry"):
            root = "system/apps" if component.kind == "system-app" else "programs"
            hidden_desktop_entries.append(f"hide=0:/{root}/{component.id}")
    graph.add(Target(
        name="generate:desktop-entry-policy",
        outputs=(desktop_entry_policy,),
        inputs=(config_path, ROOT / "configs/components.toml"),
        kind="generate",
        action=text_action(
            desktop_entry_policy,
            "# Generated from the dynamic component selection.\n"
            + "\n".join(hidden_desktop_entries) + ("\n" if hidden_desktop_entries else ""),
        ),
        action_key="generate-desktop-entry-policy-v1",
    ))

    cflags_kernel = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-mcmodel=kernel", "-Wall", "-Wextra", "-Ikernel/ntclks/include",
        "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    asflags_kernel = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Ikernel/ntclks/include", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_loader = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-Wall", "-Wextra", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    asflags_loader = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_user_base = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fPIC", "-fPIE", "-mno-red-zone", "-mgeneral-regs-only",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-DLEONOS_USE_PICOLIBC",
        "-D_POSIX_C_SOURCE=200809L",
        f"-I{relative(picolibc_prefix / 'include')}", "-Iuserland/libc/include",
        "-Iinclude", f"-I{relative(paths.out / 'include')}", "-Ithird_party/mbedtls/include",
        "-Ithird_party/zlib", "-Ithird_party/libpng", f"-I{relative(libpng_generated_dir)}",
        '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
    ]
    cflags_user = cflags_user_base + ["-include", relative(autoconf)]
    cflags_doom = cflags_user + ["-DLEONOS_DOOM", "-Ithird_party/doomgeneric/doomgeneric"]
    cflags_mp3play = [flag for flag in cflags_user if flag != "-mgeneral-regs-only"] + [
        "-mno-avx", "-mno-avx2",
        "-Ithird_party/minimp3",
    ]
    cflags_installer = cflags_user_base + ["-include", relative(installer_autoconf)]
    cflags_user_libc_base = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fPIC", "-mno-red-zone", "-mgeneral-regs-only",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-DLEONOS_USE_PICOLIBC",
        f"-I{relative(picolibc_prefix / 'include')}",
        "-Iuserland/libc/include", "-Iinclude", f"-I{relative(paths.out / 'include')}",
        "-Ithird_party/mbedtls/include", "-Ithird_party/zlib", "-Ithird_party/libpng",
        f"-I{relative(libpng_generated_dir)}", '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
    ]
    cflags_user_libc = cflags_user_libc_base + ["-include", relative(autoconf)]
    cflags_installer_libc = cflags_user_libc_base + ["-include", relative(installer_autoconf)]
    asflags_user = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Iuserland/libc/include", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_runtime = [flag for flag in cflags_user_libc if flag not in {"-include", relative(autoconf)}]
    cflags_runtime += ["-include", relative(autoconf), "-fPIC"]
    asflags_runtime = [*asflags_user, "-fPIC"]
    dynamic_link_flags = [
        "-pie", "--hash-style=sysv", "--dynamic-linker", "0:/system/lib/ld-leonos.elf",
        "-z", "relro", "-z", "now", "-z", "max-page-size=0x1000",
    ]

    boot_logo = paths.out / "include/generated/boot_logo.h"
    graph.add(
        Target(
            name="boot-logo",
            outputs=(boot_logo,),
            inputs=(ROOT / "logo.png", ROOT / "tools/generate_boot_logo.py"),
            kind="generate",
            command=(PYTHON, "tools/generate_boot_logo.py", "--input", "logo.png",
                     "--out", relative(boot_logo)),
        )
    )

    loader_sources = collect("boot/loader/**/*.c", "boot/loader/**/*.S")
    kernel_sources = collect("kernel/ntclks/**/*.c", "kernel/ntclks/**/*.S", "drivers/bootstrap/**/*.c", "drivers/bootstrap/**/*.S")
    rust_sources = collect("middlelayer/osmlayer/src/**/*.rs")
    kernel_objects: list[Path] = []
    for source in kernel_sources:
        implicit: list[Path] = [autoconf]
        if source == ROOT / "drivers/bootstrap/boot_splash.c":
            implicit.append(boot_logo)
        if source == ROOT / "kernel/ntclks/version.c":
            implicit += [build_info] + [candidate for candidate in kernel_sources + rust_sources if candidate != source]
        flags = asflags_kernel if source.suffix == ".S" else cflags_kernel + (["-include", relative(autoconf)] if source.suffix == ".c" else [])
        kernel_objects.append(add_compile(graph, paths, f"compile:kernel:{relative(source)}", source, "kernel", flags, implicit, kind="assemble" if source.suffix == ".S" else "compile"))

    kernel_unstripped = paths.out / "system/kernel.unstripped"
    kernel_debug = paths.out / "system/kernel.debug"
    kernel_sys = paths.out / "system/kernel.sys"
    graph.add(
        Target(
            name="kernel-link",
            outputs=(kernel_unstripped,),
            inputs=tuple(kernel_objects),
            implicit_inputs=(ROOT / "kernel/ntclks/arch/x86_64/linker.ld",),
            kind="link",
            command=(ld, "-nostdlib", "-z", "max-page-size=0x1000", "-T", "kernel/ntclks/arch/x86_64/linker.ld", "-o", relative(kernel_unstripped), *map(relative, kernel_objects)),
        )
    )
    graph.add(
        Target(
            name="kernel-debug",
            outputs=(kernel_debug,),
            inputs=(kernel_unstripped,),
            depends_on=("kernel-link",),
            kind="generate",
            command=(objcopy, "--only-keep-debug", relative(kernel_unstripped), relative(kernel_debug)),
        )
    )
    graph.add(
        Target(
            name="kernel-image",
            outputs=(kernel_sys,),
            inputs=(kernel_unstripped,),
            depends_on=("kernel-link",),
            kind="generate",
            command=(objcopy, "--strip-debug", relative(kernel_unstripped), relative(kernel_sys)),
        )
    )
    graph.add(Target(name="kernel", depends_on=("kernel-image", "kernel-debug"), group=True, kind="aggregate"))

    rust_obj = paths.objects / "middlelayer/osmlayer.o"
    middle_runtime = ROOT / "middlelayer/osmlayer/runtime.c"
    middle_runtime_obj = add_compile(
        graph, paths, "compile:middlelayer:runtime", middle_runtime, "middlelayer",
        cflags_kernel + ["-include", relative(autoconf)], (autoconf,),
    )
    graph.add(
        Target(
            name="compile:middlelayer:rust",
            outputs=(rust_obj,),
            inputs=(ROOT / "middlelayer/osmlayer/src/lib.rs",),
            implicit_inputs=tuple([rustcfg, *[path for path in rust_sources if path.name != "lib.rs"]]),
            kind="compile",
            source=ROOT / "middlelayer/osmlayer/src/lib.rs",
            command=(
                rustc, "--crate-type", "lib", "--target", "x86_64-unknown-none", "-C", "panic=abort",
                "-C", "relocation-model=static", "-C", "code-model=kernel", "-C", "no-redzone=yes",
                "-C", "target-feature=-sse,-sse2", "-C", "opt-level=2", "--emit", f"obj={relative(rust_obj)}",
                "middlelayer/osmlayer/src/lib.rs",
            ),
        )
    )
    middle_sys = paths.out / "system/middlelayer.sys"
    graph.add(
        Target(
            name="middlelayer-image",
            outputs=(middle_sys,),
            inputs=(rust_obj, middle_runtime_obj),
            implicit_inputs=(ROOT / "middlelayer/osmlayer/linker.ld",),
            kind="link",
            command=(ld, "-nostdlib", "-z", "max-page-size=0x1000", "-T", "middlelayer/osmlayer/linker.ld", "-o", relative(middle_sys), relative(rust_obj), relative(middle_runtime_obj)),
        )
    )
    graph.add(Target(name="middlelayer", depends_on=("middlelayer-image",), group=True, kind="aggregate"))

    graph.add(
        Target(
            name="loader-integrity",
            outputs=(loader_integrity,),
            inputs=(kernel_sys, middle_sys, ROOT / "tools/gen_loader_integrity.py"),
            kind="generate",
            command=(PYTHON, "tools/gen_loader_integrity.py", "--kernel", relative(kernel_sys), "--middlelayer", relative(middle_sys), "--out", relative(loader_integrity)),
        )
    )
    loader_objects: list[Path] = []
    for source in loader_sources:
        flags = asflags_loader if source.suffix == ".S" else cflags_loader
        implicit = (loader_integrity, boot_logo) if source == ROOT / "boot/loader/main.c" else (loader_integrity,)
        loader_objects.append(add_compile(graph, paths, f"compile:loader:{relative(source)}", source, "loader", flags, implicit, kind="assemble" if source.suffix == ".S" else "compile"))
    loader_elf = paths.out / "boot/loader.elf"
    graph.add(
        Target(
            name="loader-image",
            outputs=(loader_elf,),
            inputs=tuple(loader_objects),
            implicit_inputs=(ROOT / "boot/loader/linker.ld",),
            kind="link",
            command=(ld, "-nostdlib", "-z", "max-page-size=0x1000", "-T", "boot/loader/linker.ld", "-o", relative(loader_elf), *map(relative, loader_objects)),
        )
    )
    graph.add(Target(name="loader", depends_on=("loader-image",), group=True, kind="aggregate"))

    driver_outputs: list[Path] = []
    driver_groups: list[str] = []
    for driver in DRIVER_MODULES:
        sources = collect(f"drivers/{driver}/**/*.c", f"drivers/{driver}/**/*.S")
        objects = [
            add_compile(
                graph, paths, f"compile:driver:{driver}:{relative(source)}", source, f"driver-{driver}",
                asflags_kernel if source.suffix == ".S" else cflags_kernel + ["-include", relative(autoconf)],
                (autoconf,) if source.suffix == ".c" else (), kind="assemble" if source.suffix == ".S" else "compile",
            )
            for source in sources
        ]
        output = paths.out / "drivers" / f"{driver}.drv"
        name = f"driver:{driver}"
        graph.add(Target(name=name, outputs=(output,), inputs=tuple(objects), kind="link", command=(ld, "-r", "-o", relative(output), *map(relative, objects))))
        driver_outputs.append(output)
        driver_groups.append(name)
    graph.add(Target(name="drivers", depends_on=tuple(driver_groups), group=True, kind="aggregate"))

    graph.add(Target(
        name="gbk-table",
        outputs=(gbk_table_header,),
        inputs=(ROOT / "tools/generate_gbk_table.py",
                ROOT / "third_party/litehtml/src/encodings.cpp"),
        kind="generate",
        command=(PYTHON, "tools/generate_gbk_table.py", "--source",
                 "third_party/litehtml/src/encodings.cpp", "--output",
                 relative(gbk_table_header)),
    ))
    dynamic_crt_obj = add_compile(
        graph, paths, "compile:runtime:crt0-dynamic", ROOT / "userland/runtime/crt0_dynamic.S",
        "runtime-crt", asflags_runtime, (), kind="assemble")
    dynamic_note_obj = add_compile(
        graph, paths, "compile:runtime:abi-note", ROOT / "userland/runtime/abi_note.S",
        "runtime-crt", asflags_runtime, (), kind="assemble")

    libc_sources = collect("userland/libc/src/*.c", "userland/libc/src/*.S")
    libc_sources += [ROOT / "third_party/mbedtls/library" / source for source in MBEDTLS_SOURCES]
    libc_objects: list[Path] = []
    installer_libc_objects: list[Path] = []
    for source in sorted(libc_sources):
        is_asm = source.suffix == ".S"
        implicit = (autoconf, picolibc_header_stamp, libpng_config) if not is_asm else ()
        installer_implicit = (installer_autoconf, picolibc_header_stamp, libpng_config) if not is_asm else ()
        if source == ROOT / "userland/libc/src/text_encoding.c":
            implicit += (gbk_table_header,)
            installer_implicit += (gbk_table_header,)
        libc_objects.append(add_compile(graph, paths, f"compile:libc:{relative(source)}", source, "userlib", asflags_user if is_asm else cflags_user_libc, implicit, kind="assemble" if is_asm else "compile"))
        installer_libc_objects.append(add_compile(graph, paths, f"compile:installer-libc:{relative(source)}", source, "userlib-installer-policy", asflags_user if is_asm else cflags_installer_libc, installer_implicit, kind="assemble" if is_asm else "compile"))
    libc_a = paths.out / "userland/libc.a"
    installer_libc_a = paths.out / "userland-installer-policy/libc.a"
    graph.add(Target(name="archive:libc", outputs=(libc_a,), inputs=tuple(libc_objects), kind="link", command=(ar, "rcs", relative(libc_a), *map(relative, libc_objects))))
    graph.add(Target(name="archive:installer-libc", outputs=(installer_libc_a,), inputs=tuple(installer_libc_objects), kind="link", command=(ar, "rcs", relative(installer_libc_a), *map(relative, installer_libc_objects))))

    runtime_sources = [source for source in sorted(libc_sources)
                       if source.name != "crt0.S"] + [ROOT / "userland/runtime/ld_leonos.c",
                                                       ROOT / "userland/runtime/abi_note.S"]
    runtime_objects: list[Path] = []
    for source in runtime_sources:
        is_asm = source.suffix == ".S"
        runtime_objects.append(add_compile(
            graph, paths, f"compile:runtime:{relative(source)}", source, "runtime",
            asflags_runtime if is_asm else cflags_runtime,
            (autoconf, picolibc_header_stamp, libpng_config) if not is_asm else (),
            kind="assemble" if is_asm else "compile"))
    graph.add(Target(
        name="runtime",
        outputs=(runtime_so,),
        inputs=tuple([*runtime_objects, picolibc_archive, zlib_archive, libpng_archive,
                      compiler_rt_archive,
                      ROOT / "userland/dynamic-linker.ld"]),
        depends_on=("picolibc", "archive:zlib", "archive:libpng"),
        kind="link",
        command=(ld, "-shared", "-Bsymbolic", "--allow-multiple-definition", "--hash-style=sysv", "-soname", "libleonos.so.1",
                 "-z", "max-page-size=0x1000", "-T", "userland/dynamic-linker.ld",
                 "-o", relative(runtime_so), *map(relative, runtime_objects),
                 # These are referenced by the dynamic CRT, not by the runtime
                 # objects themselves.  Keep their Picolibc archive members in
                 # the shared ABI so every dynamic executable starts uniformly.
                 "-u", "environ", "-u", "__libc_init_array", "-u", "exit",
                 # ABI v1 promises the public Picolibc, zlib and libpng
                 # surfaces.  Archive extraction based only on runtime-local
                 # references would silently drop valid application symbols.
                 "--whole-archive", relative(picolibc_archive), relative(zlib_archive),
                 relative(libpng_archive), "--no-whole-archive", relative(compiler_rt_archive)),
    ))
    installer_runtime_sources = [source for source in sorted(libc_sources)
                                 if source.name != "crt0.S"] + [ROOT / "userland/runtime/ld_leonos.c",
                                                                 ROOT / "userland/runtime/abi_note.S"]
    installer_runtime_objects: list[Path] = []
    for source in installer_runtime_sources:
        is_asm = source.suffix == ".S"
        installer_runtime_objects.append(add_compile(
            graph, paths, f"compile:installer-runtime:{relative(source)}", source, "installer-runtime",
            asflags_runtime if is_asm else cflags_installer_libc + ["-fPIC"],
            (installer_autoconf, picolibc_header_stamp, libpng_config) if not is_asm else (),
            kind="assemble" if is_asm else "compile"))
    graph.add(Target(
        name="installer-runtime",
        outputs=(installer_runtime_so,),
        inputs=tuple([*installer_runtime_objects, picolibc_archive, zlib_archive, libpng_archive,
                      compiler_rt_archive,
                      ROOT / "userland/dynamic-linker.ld"]),
        depends_on=("picolibc", "archive:zlib", "archive:libpng"),
        kind="link",
        command=(ld, "-shared", "-Bsymbolic", "--allow-multiple-definition", "--hash-style=sysv", "-soname", "libleonos.so.1",
                 "-z", "max-page-size=0x1000", "-T", "userland/dynamic-linker.ld",
                 "-o", relative(installer_runtime_so), *map(relative, installer_runtime_objects),
                 "-u", "environ", "-u", "__libc_init_array", "-u", "exit",
                 "--whole-archive", relative(picolibc_archive), relative(zlib_archive),
                 relative(libpng_archive), "--no-whole-archive", relative(compiler_rt_archive)),
    ))
    loader_sources = [ROOT / "userland/runtime/ld_start.S", ROOT / "userland/runtime/ld_leonos.c",
                      ROOT / "userland/runtime/abi_note.S"]
    loader_objects = [add_compile(
        graph, paths, f"compile:ld-leonos:{relative(source)}", source, "ld-leonos",
        asflags_runtime if source.suffix == ".S" else cflags_runtime,
        (autoconf, picolibc_header_stamp) if source.suffix != ".S" else (),
        kind="assemble" if source.suffix == ".S" else "compile") for source in loader_sources]
    graph.add(Target(
        name="runtime-loader",
        outputs=(runtime_loader,),
        inputs=tuple([*loader_objects, libc_a, picolibc_archive,
                      ROOT / "userland/interpreter.ld"]),
        depends_on=("archive:libc", "picolibc"),
        kind="link",
        command=(ld, "-pie", "-nostdlib", "--gc-sections", "-Bsymbolic", "--hash-style=sysv",
                 "-u", "stdin", "-u", "stdout", "-u", "stderr",
                 "-z", "max-page-size=0x1000",
                 "-T", "userland/interpreter.ld", "-o", relative(runtime_loader),
                 *map(relative, loader_objects), "--start-group", relative(libc_a),
                 relative(picolibc_archive), "--end-group"),
    ))

    # This recovery window must remain usable when libleonos.so.1 is missing.
    # Keep it as a conventional ET_EXEC image with no PT_INTERP or DT_NEEDED.
    cflags_dynlinkerror = [flag for flag in cflags_user if flag not in {"-fPIC", "-fPIE"}]
    cflags_dynlinkerror.extend(("-fno-pic", "-fno-pie"))
    dynlinkerror_source = ROOT / "userland/apps/dynlinkerror/main.c"
    dynlinkerror_object = add_compile(
        graph, paths, "compile:dynlinkerror", dynlinkerror_source, "user-dynlinkerror",
        cflags_dynlinkerror, (autoconf, picolibc_header_stamp),
    )
    graph.add(Target(
        name="dynlinkerror",
        outputs=(dynlinkerror_elf,),
        inputs=(dynlinkerror_object, libc_a, picolibc_archive,
                compiler_rt_archive, ROOT / "userland/linker.ld"),
        depends_on=("archive:libc", "picolibc"),
        kind="link",
        command=(ld, "-nostdlib", "--gc-sections", *build_link_flags,
                 "-z", "max-page-size=0x1000", "-T", "userland/linker.ld",
                 "-o", relative(dynlinkerror_elf), relative(dynlinkerror_object),
                 "--start-group", relative(libc_a), relative(picolibc_archive),
                 relative(compiler_rt_archive), "--end-group"),
    ))

    zlib_objects = [
        add_compile(graph, paths, f"compile:zlib:{source}", zlib_source / source, "zlib",
                    cflags_user_libc + ["-DZ_SOLO", "-include", "stddef.h"],
                    (autoconf, picolibc_header_stamp))
        for source in ZLIB_SOURCES
    ]
    graph.add(Target(name="archive:zlib", outputs=(zlib_archive,), inputs=tuple(zlib_objects),
                     kind="link", command=(ar, "rcs", relative(zlib_archive),
                                             *map(relative, zlib_objects))))
    libpng_cflags = cflags_user_libc + ["-DLEONOS_LIBPNG_FIXED_POINT=3"]
    libpng_objects = [
        add_compile(graph, paths, f"compile:libpng:{source}", libpng_source / source, "libpng",
                    libpng_cflags, (autoconf, picolibc_header_stamp, libpng_config))
        for source in LIBPNG_SOURCES
    ]
    graph.add(Target(name="archive:libpng", outputs=(libpng_archive,),
                     inputs=tuple([*libpng_objects, libpng_config]), kind="link",
                     command=(ar, "rcs", relative(libpng_archive),
                              *map(relative, libpng_objects))))

    file_magic_inputs = tuple([
        ROOT / "tools/build_file_magic.py", file_source / "configure.ac",
        file_source / "acinclude.m4", file_source / "Makefile.am",
        file_source / "src/magic.h.in", *collect("third_party/file/m4/*.m4"),
        *collect("third_party/file/src/*.c"), *collect("third_party/file/src/*.h"),
        *collect("third_party/file/magic/**/*"),
    ])
    graph.add(Target(
        name="file-magic",
        outputs=(magic_database, magic_database_stamp),
        inputs=file_magic_inputs,
        kind="generate",
        command=(
            PYTHON, "tools/build_file_magic.py", "--source", "third_party/file",
            "--output", relative(magic_database), "--stamp", relative(magic_database_stamp),
        ),
    ))

    file_inputs = tuple([
        ROOT / "tools/build_file.py", file_port / "config.h",
        file_port / "leonos_shim.c", file_port / "README.md",
        *collect("userland/file/include/**/*.h"),
        *collect("third_party/file/src/*.c"), *collect("third_party/file/src/*.h"),
        file_source / "COPYING",
    ])
    graph.add(Target(
        name="file",
        outputs=(file_elf, libmagic_so, libmagic_archive, file_magic_header, file_stamp),
        inputs=tuple([*file_inputs, ROOT / "userland/dynamic-app.ld", runtime_so,
                      dynamic_crt_obj, dynamic_note_obj, picolibc_archive]),
        depends_on=("picolibc", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_file.py", "--source", "third_party/file",
            "--port", "userland/file", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--generated-include", relative(paths.generated_include), "--linker-script",
            "userland/linker.ld", "--dynamic-linker-script", "userland/dynamic-app.ld",
            "--leonos-lib", relative(libc_a), "--runtime-so", relative(runtime_so),
            "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            "--picolibc-lib", relative(picolibc_archive), "--output", relative(file_elf),
            "--library", relative(libmagic_so), "--static-library", relative(libmagic_archive),
            "--magic-header", relative(file_magic_header),
            "--stamp", relative(file_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    sqlite_inputs = tuple([
        ROOT / "tools/build_sqlite.py", sqlite_source / "VERSION", sqlite_source / "main.mk",
        sqlite_source / "Makefile.linux-gcc", sqlite_source / "tool/mksqlite3c.tcl",
        sqlite_port / "leonos_sqlite_vfs.c", sqlite_port / "README.md",
        ROOT / "userland/dynamic-app.ld", runtime_so, dynamic_crt_obj, dynamic_note_obj,
    ])
    graph.add(Target(
        name="sqlite",
        outputs=(sqlite_so, sqlite_archive, sqlite_header, sqlite_stamp),
        inputs=sqlite_inputs,
        depends_on=("picolibc", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_sqlite.py", "--source", "third_party/sqlite",
            "--port", "userland/sqlite", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--dynamic-linker-script", "userland/dynamic-app.ld", "--runtime-so", relative(runtime_so),
            "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            "--library", relative(sqlite_so), "--static-library", relative(sqlite_archive),
            "--header", relative(sqlite_header), "--work-dir", relative(sqlite_work_dir),
            "--stamp", relative(sqlite_stamp), *compile_option_args, *linker_option_args,
        ),
    ))

    def busybox_source_revision_action(context: ActionContext) -> None:
        context.detail(f"reading source revision: git -C {relative(busybox_source)} rev-parse HEAD")
        result = subprocess.run(
            ("git", "-C", str(busybox_source), "rev-parse", "HEAD"),
            check=True, text=True, capture_output=True,
        )
        ensure_parent(context, busybox_source_stamp, result.stdout.strip() + "\n")

    graph.add(Target(
        name="busybox-source-revision",
        outputs=(busybox_source_stamp,),
        inputs=(busybox_source / "Makefile", busybox_source / "Config.in"),
        kind="generate",
        action=busybox_source_revision_action,
        action_key="busybox-source-revision-v1",
        always=True,
    ))
    graph.add(Target(
        name="busybox",
        outputs=(busybox_elf, busybox_stamp),
        inputs=tuple([
            busybox_source_stamp, ROOT / "tools/build_busybox.py", busybox_config,
            busybox_shim, *busybox_headers, ROOT / "userland/linker.ld", libc_a,
            picolibc_archive,
        ]),
        depends_on=("busybox-source-revision", "picolibc", "archive:libc"),
        kind="compile",
        command=(
            PYTHON, "tools/build_busybox.py", "--source", "third_party/busybox",
            "--config", "userland/busybox/leonos.config", "--picolibc-prefix",
            relative(picolibc_prefix), "--leonos-libc-include", "userland/libc/include",
            "--leonos-include", "include", "--linker-script", "userland/linker.ld",
            "--leonos-lib", relative(libc_a), "--picolibc-lib", relative(picolibc_archive),
            "--output", relative(busybox_elf), "--stamp", relative(busybox_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    nano_inputs = collect(
        "third_party/nano/src/**/*.c", "third_party/nano/src/**/*.h",
        "third_party/nano/COPYING", "userland/nano/**/*.c", "userland/nano/**/*.h",
        "tools/build_nano.py",
    )
    graph.add(Target(
        name="nano",
        outputs=(nano_elf, nano_stamp),
        inputs=tuple([*nano_inputs, ROOT / "userland/dynamic-app.ld", runtime_so, dynamic_crt_obj, dynamic_note_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_nano.py", "--source", "third_party/nano",
            "--port", "userland/nano", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--linker-script", "userland/dynamic-app.ld", "--leonos-lib", relative(runtime_so),
            "--picolibc-lib", relative(picolibc_archive), "--work-dir", relative(nano_work_dir),
            "--output", relative(nano_elf), "--stamp", relative(nano_stamp),
            "--dynamic", "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    fastfetch_inputs = collect(
        "third_party/fastfetch/src/common/**/*.c", "third_party/fastfetch/src/common/**/*.h",
        "third_party/fastfetch/src/detection/**/*.c", "third_party/fastfetch/src/detection/**/*.h",
        "third_party/fastfetch/src/logo/**/*.c", "third_party/fastfetch/src/logo/**/*.h",
        "third_party/fastfetch/src/logo/**/*.inc", "third_party/fastfetch/src/logo/**/*.txt",
        "third_party/fastfetch/src/modules/**/*.c", "third_party/fastfetch/src/modules/**/*.h",
        "third_party/fastfetch/src/fastfetch.h", "third_party/fastfetch/LICENSE",
        "userland/fastfetch/**/*.c", "userland/fastfetch/**/*.h", "userland/fastfetch/**/*.md",
        "tools/build_fastfetch.py",
    )
    graph.add(Target(
        name="fastfetch",
        outputs=(fastfetch_elf, fastfetch_stamp),
        inputs=tuple([*fastfetch_inputs, ROOT / "userland/dynamic-app.ld", runtime_so, dynamic_crt_obj, dynamic_note_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_fastfetch.py", "--source", "third_party/fastfetch",
            "--port", "userland/fastfetch", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--linker-script", "userland/dynamic-app.ld", "--leonos-lib", relative(runtime_so),
            "--picolibc-lib", relative(picolibc_archive), "--work-dir", relative(fastfetch_work_dir),
            "--output", relative(fastfetch_elf), "--stamp", relative(fastfetch_stamp),
            "--dynamic", "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    tcc_inputs = collect(
        "third_party/tinycc/**/*.c", "third_party/tinycc/**/*.h",
        "third_party/tinycc/**/*.S", "third_party/tinycc/**/*.def",
        "third_party/tinycc/VERSION", "third_party/tinycc/COPYING",
        "userland/tcc/**/*.c", "userland/tcc/**/*.h", "userland/tcc/**/*.md",
        "tools/build_tcc.py",
    )
    graph.add(Target(
        name="tcc",
        outputs=(tcc_elf, tcc_stamp),
        inputs=tuple([*tcc_inputs, ROOT / "userland/linker.ld", libc_a, picolibc_archive,
                      zlib_archive, libpng_archive]),
        depends_on=("picolibc", "archive:libc", "archive:zlib", "archive:libpng"),
        kind="compile",
        command=(
            PYTHON, "tools/build_tcc.py", "--source", "third_party/tinycc",
            "--port", "userland/tcc", "--sdk-include", "devtools/include",
            "--picolibc-prefix", relative(picolibc_prefix), "--leonos-lib", relative(libc_a),
            "--picolibc-lib", relative(picolibc_archive), "--linker-script", "userland/linker.ld",
            "--zlib-lib", relative(zlib_archive), "--libpng-lib", relative(libpng_archive),
            "--zlib-source", "third_party/zlib", "--libpng-source", "third_party/libpng",
            "--libpng-config", relative(libpng_config),
            "--work-dir", relative(paths.out / "tcc-work"), "--runtime-dir", relative(tcc_runtime_dir),
            "--output", relative(tcc_elf), "--stamp", relative(tcc_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    lua_inputs = collect(
        "third_party/lua/*.c", "third_party/lua/*.h", "third_party/lua/README.md",
        "userland/lua/**/*.c", "userland/lua/**/*.h", "userland/lua/**/*.md",
        "userland/lua/LICENSE", "userland/apps/lua/lua.app.ini",
        "tools/build_lua.py",
    )
    graph.add(Target(
        name="lua",
        outputs=(lua_elf, liblua_so, liblua_archive, lua_stamp),
        inputs=tuple([*lua_inputs, ROOT / "userland/dynamic-app.ld", runtime_so,
                      dynamic_crt_obj, dynamic_note_obj, picolibc_archive]),
        depends_on=("picolibc", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_lua.py", "--source", "third_party/lua",
            "--port", "userland/lua", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--linker-script", "userland/linker.ld", "--dynamic-linker-script", "userland/dynamic-app.ld",
            "--leonos-lib", relative(libc_a), "--runtime-so", relative(runtime_so),
            "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            "--picolibc-lib", relative(picolibc_archive), "--work-dir", relative(lua_work_dir),
            "--output", relative(lua_elf), "--library", relative(liblua_so),
            "--static-library", relative(liblua_archive), "--stamp", relative(lua_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    cmd_inputs = collect(
        "third_party/cmd/*.c", "third_party/cmd/*.h", "third_party/cmd/LICENSE",
        "userland/cmd/**/*.c", "userland/cmd/**/*.h", "userland/cmd/**/*.md",
        "tools/build_cmd.py",
    )
    graph.add(Target(
        name="cmd",
        outputs=(cmd_elf, cmd_stamp),
        inputs=tuple([*cmd_inputs, ROOT / "userland/linker.ld", libc_a, picolibc_archive]),
        depends_on=("picolibc", "archive:libc"),
        kind="compile",
        command=(
            PYTHON, "tools/build_cmd.py", "--source", "third_party/cmd",
            "--port", "userland/cmd", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--linker-script", "userland/linker.ld", "--leonos-lib", relative(libc_a),
            "--picolibc-lib", relative(picolibc_archive), "--work-dir", relative(cmd_work_dir),
            "--output", relative(cmd_elf), "--stamp", relative(cmd_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    pleditor_inputs = collect(
        "third_party/pl_editor/src/**/*.c", "third_party/pl_editor/src/**/*.h",
        "third_party/pl_editor/LICENSE", "userland/apps/pleditor/**/*.c",
        "userland/apps/pleditor/**/*.h", "tools/build_pleditor.py",
    )
    graph.add(Target(
        name="app:pleditor",
        outputs=(pleditor_elf, pleditor_stamp),
        inputs=tuple([*pleditor_inputs, ROOT / "userland/dynamic-app.ld", runtime_so, dynamic_crt_obj, dynamic_note_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON, "tools/build_pleditor.py", "--source", "third_party/pl_editor",
            "--port", "userland/apps/pleditor", "--picolibc-prefix", relative(picolibc_prefix),
            "--leonos-libc-include", "userland/libc/include", "--leonos-include", "include",
            "--generated-include", relative(paths.generated_include),
            "--linker-script", "userland/dynamic-app.ld", "--leonos-lib", relative(runtime_so),
            "--picolibc-lib", relative(picolibc_archive), "--work-dir", relative(pleditor_work_dir),
            "--output", relative(pleditor_elf), "--stamp", relative(pleditor_stamp),
            "--dynamic", "--dynamic-crt", relative(dynamic_crt_obj), "--abi-note", relative(dynamic_note_obj),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    # StardustUI is a freestanding C++ library.  Keep its component and theme
    # implementation in one archive and use the upstream examples as the
    # first LeonOS applications.  The LeonOS platform adapter deliberately
    # uses the existing pixel-buffer GUI ABI, so SDL/XAPI backends are not
    # pulled into the user image.
    stardustui_sources = [
        ROOT / "third_party/stardustui/src/file.cpp",
        ROOT / "third_party/stardustui/src/network.cpp",
        ROOT / "third_party/stardustui/src/sytel.cpp",
        ROOT / "third_party/stardustui/src/theme.cpp",
        ROOT / "third_party/stardustui/src/window.cpp",
        ROOT / "third_party/stardustui/src/text/font.cpp",
        ROOT / "userland/stardustui/src/platform_leonos.cpp",
        *collect("third_party/stardustui/src/components/*.cpp"),
    ]
    stardustui_headers = tuple([
        ROOT / "third_party/stardustui/settings.hpp",
        ROOT / "third_party/stardustui/platforms/platform.hpp",
        *collect("third_party/stardustui/includes/**/*.hpp"),
        *collect("third_party/stardustui/includes/*"),
        *collect("userland/stardustui/include/*"),
        ROOT / "userland/stardustui/src/platform_leonos.cpp",
    ])
    cxxflags_stardustui = [
        cxx, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c++17", "-ffreestanding",
        "-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit", "-fno-threadsafe-statics",
        "-fno-stack-protector", "-fPIC", "-mno-red-zone", "-mgeneral-regs-only",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-DLEONOS_USE_PICOLIBC", "-DSTARDUSTUI_LINUX", "-D_POSIX_C_SOURCE=200809L",
        "-nostdinc++", f"-I{relative(picolibc_prefix / 'include')}",
        "-Iuserland/stardustui/include", "-Ithird_party/stardustui/includes",
        "-Ithird_party/stardustui", "-Iuserland/libc/include", "-Iinclude",
        f"-I{relative(paths.out / 'include')}", "-Ithird_party/mbedtls/include",
        "-Ithird_party/zlib", "-Ithird_party/libpng", f"-I{relative(libpng_generated_dir)}",
        '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"', "-include", relative(autoconf),
    ]
    stardustui_objects = [
        add_compile(graph, paths, f"compile:stardustui:{relative(source)}", source,
                    "stardustui", cxxflags_stardustui,
                    (autoconf, picolibc_header_stamp, *stardustui_headers))
        for source in sorted(stardustui_sources)
    ]
    stardustui_archive = paths.out / "userland/libstardustui.a"
    graph.add(Target(
        name="archive:stardustui",
        outputs=(stardustui_archive,),
        inputs=tuple(stardustui_objects),
        depends_on=("picolibc",),
        kind="link",
        command=(ar, "rcs", relative(stardustui_archive), *map(relative, stardustui_objects)),
    ))
    stardustui_examples = {
        "stardusthello": ROOT / "third_party/stardustui/examples/helloworld/helloworld.cpp",
        "stardustlayout": ROOT / "third_party/stardustui/examples/layout/layout.cpp",
        "stardustshowcase": ROOT / "third_party/stardustui/examples/showcase/showcase.cpp",
    }
    stardustui_elfs: dict[str, Path] = {}
    for app, source in stardustui_examples.items():
        wrapper_source = ROOT / "userland/apps" / app / "main.c"
        wrapper_compile_name = f"compile:app:{app}:{relative(wrapper_source)}"
        wrapper_obj = add_compile(graph, paths, wrapper_compile_name, wrapper_source,
                                  f"user-{app}", cflags_user,
                                  (autoconf, picolibc_header_stamp, libpng_config))
        example_inputs = tuple([source, wrapper_source, *stardustui_headers, ROOT / "userland/linker.ld",
                                stardustui_archive, libc_a, picolibc_archive])
        compile_name = f"compile:app:{app}:{relative(source)}"
        obj = add_compile(graph, paths, compile_name, source,
                          f"user-{app}", cxxflags_stardustui,
                          (autoconf, picolibc_header_stamp, *stardustui_headers))
        output = paths.out / f"userland/{app}.elf"
        graph.add(Target(
            name=f"app:{app}",
            outputs=(output,),
        inputs=tuple([obj, wrapper_obj, *example_inputs, runtime_so, dynamic_crt_obj, dynamic_note_obj,
                      ROOT / "userland/dynamic-app.ld"]),
            depends_on=(compile_name, wrapper_compile_name, "archive:stardustui", "runtime", "runtime-loader"),
            implicit_inputs=(ROOT / "userland/dynamic-app.ld",),
            kind="link",
            command=(ld, "-nostdlib", "--gc-sections", *build_link_flags, *dynamic_link_flags,
                     "-T", "userland/dynamic-app.ld", "-o", relative(output),
                     relative(dynamic_crt_obj), relative(dynamic_note_obj), relative(wrapper_obj), relative(obj),
                     "--start-group", relative(stardustui_archive), relative(runtime_so), "--end-group"),
        ))
        stardustui_elfs[app] = output

    app_elfs: dict[str, Path] = {}
    user_targets: list[str] = ["picolibc", "archive:libc", "archive:zlib", "archive:libpng",
                               "runtime", "runtime-loader", "dynlinkerror"]
    if component_enabled("file"):
        user_targets.extend(("file-magic", "file"))
    if component_enabled("sqlite"):
        user_targets.append("sqlite")
    if component_enabled("busybox"):
        user_targets.append("busybox")
    if component_enabled("nano"):
        user_targets.append("nano")
    if component_enabled("fastfetch"):
        user_targets.append("fastfetch")
    if component_enabled("tcc"):
        user_targets.append("tcc")
    if component_enabled("lua"):
        user_targets.append("lua")
    if component_enabled("cmd"):
        user_targets.append("cmd")
    if component_enabled("pleditor"):
        user_targets.append("app:pleditor")
    if component_enabled("stardustui"):
        user_targets.append("archive:stardustui")
    for app, output in stardustui_elfs.items():
        if app in build_user_apps:
            app_elfs[app] = output
            user_targets.append(f"app:{app}")
    for app in build_user_apps:
        if app == "nano":
            app_elfs[app] = nano_elf
            continue
        if app == "fastfetch":
            app_elfs[app] = fastfetch_elf
            continue
        if app == "pleditor":
            app_elfs[app] = pleditor_elf
            continue
        if app in stardustui_apps:
            continue
        objects: list[Path] = []
        for source in user_app_sources(app):
            is_asm = source.suffix == ".S"
            cflags_app = cflags_doom if app == "doom" else (cflags_mp3play if app == "mp3play" else cflags_user)
            objects.append(add_compile(graph, paths, f"compile:app:{app}:{relative(source)}", source, f"user-{app}", asflags_user if is_asm else cflags_app, (autoconf, picolibc_header_stamp, libpng_config) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland/{app}.elf"
        app_archives = [runtime_so]
        graph.add(Target(name=f"app:{app}", outputs=(output,),
                         inputs=tuple([*objects, *app_archives, dynamic_crt_obj, dynamic_note_obj,
                                       ROOT / "userland/dynamic-app.ld"]),
                         implicit_inputs=(ROOT / "userland/dynamic-app.ld",), kind="link",
                         command=(ld, "-nostdlib", "--gc-sections", *build_link_flags, *dynamic_link_flags,
                                  "-T", "userland/dynamic-app.ld", "-o", relative(output),
                                  relative(dynamic_crt_obj), relative(dynamic_note_obj),
                                  *map(relative, objects), relative(runtime_so))))
        app_elfs[app] = output
        user_targets.append(f"app:{app}")

    installer_policy_elfs: dict[str, Path] = {}
    user_targets.append("archive:installer-libc")
    for app in installer_policy_apps:
        objects = []
        for source in user_app_sources(app):
            is_asm = source.suffix == ".S"
            objects.append(add_compile(graph, paths, f"compile:installer-app:{app}:{relative(source)}", source, f"user-installer-policy-{app}", asflags_user if is_asm else cflags_installer, (installer_autoconf, picolibc_header_stamp) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland-installer-policy/{app}.elf"
        name = f"installer-policy:{app}"
        graph.add(Target(name=name, outputs=(output,), inputs=tuple([*objects, installer_runtime_so, dynamic_crt_obj, dynamic_note_obj]), implicit_inputs=(ROOT / "userland/dynamic-app.ld",), kind="link", command=(ld, "-nostdlib", "--gc-sections", *build_link_flags, *dynamic_link_flags, "-T", "userland/dynamic-app.ld", "-o", relative(output), relative(dynamic_crt_obj), relative(dynamic_note_obj), *map(relative, objects), relative(installer_runtime_so))))
        installer_policy_elfs[app] = output
        user_targets.append(name)

    app_icons = tuple(paths.out / f"generated/app-icons/{app}.bmp" for app in build_user_apps)
    minesweeper_assets = tuple(paths.out / f"generated/minesweeper-assets/{name}"
                               for name in MINESWEEPER_ASSETS)
    stardustui_theme_files = tuple(
        ROOT / "third_party/stardustui/docs/zh-cn/example" / name
        for name in (
            "md3-light.theme.json", "md3-dark.theme.json",
            "green_light.theme.json", "green_dark.theme.json",
        )
    )
    ui_metro_font = paths.out / "generated/fonts/leonos-metro.ttf"
    ui_win95_font = paths.out / "generated/fonts/leonos-win95.ttf"
    browser_font = paths.out / "generated/fonts/times-new-roman.ttf"
    browser_font_source = ROOT / "system/fonts/times.ttf"
    browser_cjk_font_source = ROOT / "system/fonts/simsun.ttc"
    graph.add(Target(name="ui-font", outputs=(ui_metro_font, ui_win95_font),
                     inputs=(ROOT / "tools/prepare_ui_font.py", ROOT / "system/fonts/Deng.ttf",
                             ROOT / "system/fonts/system.psf"),
                     kind="generate", command=(PYTHON, "tools/prepare_ui_font.py", "--metro-out", relative(ui_metro_font),
                                                   "--win95-out", relative(ui_win95_font))))
    graph.add(Target(name="browser-font", outputs=(browser_font,),
                     inputs=(ROOT / "tools/prepare_browser_font.py", browser_font_source), kind="generate",
                     command=(PYTHON, "tools/prepare_browser_font.py", "--font",
                              relative(browser_font_source), "--out",
                              relative(browser_font))))
    graph.add(Target(name="app-icons", outputs=app_icons, inputs=(ROOT / "tools/make_app_icons.py",),
                     kind="generate", command=(PYTHON, "tools/make_app_icons.py",
                                                "--out-dir", relative(paths.out / "generated/app-icons"),
                                                "--apps", *build_user_apps)))
    graph.add(Target(name="minesweeper-assets", outputs=minesweeper_assets,
                     inputs=(ROOT / "tools/make_minesweeper_assets.py",), kind="generate",
                     command=(PYTHON, "tools/make_minesweeper_assets.py", "--out-dir",
                              relative(paths.out / "generated/minesweeper-assets"))))
    button_icons = tuple(paths.out / f"generated/window-buttons/{name}" for name in WINDOW_BUTTON_ICONS)
    graph.add(Target(name="window-button-icons", outputs=button_icons, inputs=(ROOT / "tools/make_window_button_icons.py",), kind="generate", command=(PYTHON, "tools/make_window_button_icons.py", "--out-dir", relative(paths.out / "generated/window-buttons"))))
    user_targets += ["app-icons", "window-button-icons", "minesweeper-assets", "ui-font", "browser-font"]
    graph.add(Target(name="userland", depends_on=tuple(user_targets), group=True, kind="aggregate"))

    component_metadata = paths.out / "generated/component-selection.json"
    sdk_inputs_list: list[Path] = [
        ROOT / "tools/package_devtools.py", ROOT / "third_party/picolibc/COPYING.picolibc",
        ROOT / "third_party/zlib/LICENSE", ROOT / "third_party/libpng/LICENSE",
        libpng_config,
        # The packager copies these build outputs verbatim. Keep them as
        # explicit inputs so a rebuilt runtime cannot leave a stale SDK ZIP.
        libc_a, runtime_so, runtime_loader, picolibc_archive, picolibc_header_stamp, zlib_archive, libpng_archive,
        dynamic_crt_obj, dynamic_note_obj, ROOT / "userland/dynamic-app.ld",
        ROOT / "userland/interpreter.ld", *collect("devtools/**/*"),
    ]
    sdk_depends = ["picolibc", "archive:libc", "archive:zlib", "archive:libpng"]
    sdk_command: list[str] = [
        PYTHON, "tools/package_devtools.py",
        "--sdk-root", "devtools",
        "--leonos-lib", relative(libc_a),
        "--runtime-so", relative(runtime_so),
        "--runtime-loader", relative(runtime_loader),
        "--dynamic-crt", relative(dynamic_crt_obj),
        "--abi-note", relative(dynamic_note_obj),
        "--picolibc-lib", relative(picolibc_archive),
        "--picolibc-include", relative(picolibc_prefix / "include"),
        "--picolibc-source", "third_party/picolibc",
        "--zlib-lib", relative(zlib_archive), "--zlib-source", "third_party/zlib",
        "--libpng-lib", relative(libpng_archive), "--libpng-source", "third_party/libpng",
        "--libpng-config", relative(libpng_config),
    ]
    if component_enabled("file", "sdk"):
        sdk_inputs_list.extend((file_source / "COPYING", libmagic_so, libmagic_archive, file_magic_header))
        sdk_depends.append("file")
        sdk_command.extend((
            "--libmagic-lib", relative(libmagic_archive), "--libmagic-so", relative(libmagic_so),
            "--libmagic-source", "third_party/file",
            "--libmagic-header", relative(file_magic_header),
        ))
    if component_enabled("sqlite", "sdk"):
        sdk_inputs_list.extend((sqlite_source / "LICENSE.md", sqlite_so, sqlite_archive,
                                sqlite_header, sqlite_stamp, sqlite_port / "README.md"))
        sdk_depends.append("sqlite")
        sdk_command.extend((
            "--sqlite-lib", relative(sqlite_archive), "--sqlite-so", relative(sqlite_so),
            "--sqlite-source", "third_party/sqlite", "--sqlite-header", relative(sqlite_header),
            "--sqlite-stamp", relative(sqlite_stamp),
        ))
    if component_enabled("stardustui", "sdk"):
        sdk_inputs_list.extend((
            ROOT / "third_party/stardustui/LICENSE", stardustui_archive,
            *collect("third_party/stardustui/includes/**/*.hpp"),
            *collect("third_party/stardustui/includes/*"),
            ROOT / "third_party/stardustui/platforms/platform.hpp",
            ROOT / "third_party/stardustui/settings.hpp",
            *collect("userland/stardustui/include/*"),
        ))
        sdk_depends.append("archive:stardustui")
        sdk_command.extend((
            "--stardustui-lib", relative(stardustui_archive),
            "--stardustui-source", "third_party/stardustui",
        ))
    if component_enabled("tcc", "sdk"):
        sdk_inputs_list.extend((tcc_elf, tcc_stamp, *collect("userland/tcc/**/*")))
        sdk_depends.append("tcc")
        sdk_command.extend((
            "--component-file", "tcc", "bin/tcc.elf", relative(tcc_elf),
            "--component-tree", "tcc", "runtime", relative(tcc_runtime_dir),
            "--component-tree", "tcc", "port", "userland/tcc",
        ))
    if component_enabled("lua", "sdk"):
        sdk_inputs_list.extend((
            lua_elf, liblua_so, liblua_archive, lua_stamp, *collect("third_party/lua/*"),
            *collect("userland/lua/**/*"), lua_app_manifest,
        ))
        sdk_depends.append("lua")
        sdk_command.extend((
            "--component-file", "lua", "bin/lua.elf", relative(lua_elf),
            "--liblua-lib", relative(liblua_archive), "--liblua-so", relative(liblua_so),
            "--liblua-source", "third_party/lua",
            "--component-tree", "lua", "upstream", "third_party/lua",
            "--component-tree", "lua", "port", "userland/lua",
            "--component-file", "lua", "lua.app.ini", "userland/apps/lua/lua.app.ini",
        ))
    for app, source in stardustui_examples.items():
        if not component_enabled(app, "sdk"):
            continue
        sdk_inputs_list.extend((stardustui_elfs[app], source, ROOT / "userland/apps" / app / "main.c"))
        sdk_depends.append(f"app:{app}")
        sdk_command.extend((
            "--component-file", app, f"bin/{app}.elf", relative(stardustui_elfs[app]),
            "--component-file", app, "example.cpp", relative(source),
            "--component-file", app, "leonos-main.c", f"userland/apps/{app}/main.c",
        ))
    if config_bool(values, "CONFIG_SDK_INCLUDE_COMPONENT_METADATA"):
        sdk_inputs_list.append(component_metadata)
        sdk_depends.append("config-sync")
        sdk_command.extend(("--component-metadata", relative(component_metadata)))
    sdk_command.extend(("--out", relative(developer_sdk)))
    graph.add(Target(
        name="sdk",
        outputs=(developer_sdk,),
        inputs=tuple(sdk_inputs_list),
        depends_on=tuple(sdk_depends),
        kind="generate",
        command=tuple(sdk_command),
    ))

    grub_font = paths.out / "generated/grub/leonos-unicode.pf2"
    graph.add(Target(
        name="grub-font",
        outputs=(grub_font,),
        inputs=(ROOT / "system/fonts/Deng.ttf",),
        kind="generate",
        command=(
            "grub-mkfont", "-s", "16", "-n", "LeonOS Unicode",
            "-o", relative(grub_font), "system/fonts/Deng.ttf",
        ),
    ))
    grub_efi = paths.staging / "EFI/BOOT/BOOTX64.EFI"
    # 如果是系统 GRUB 路径，使用绝对路径；否则使用相对路径
    grub_dir_arg = str(grub_efi_dir) if using_system_grub else relative(grub_efi_dir)
    graph.add(Target(name="grub-efi", outputs=(grub_efi,), inputs=(ROOT / "boot/grub/embedded.cfg", grub_efi_dir / "modinfo.sh"), kind="generate", command=("grub-mkstandalone", "-d", grub_dir_arg, "-O", "x86_64-efi", "-o", relative(grub_efi), "--modules=part_gpt fat multiboot2 normal search search_fs_file configfile echo serial terminal video video_bochs video_cirrus efi_gop efi_uga all_video font gfxterm", "boot/grub/grub.cfg=boot/grub/embedded.cfg")))
    component_prune_stamp = paths.out / "generated/component-staging-prune.json"

    def prune_component_staging(context: ActionContext) -> None:
        for component in components:
            selected = component_enabled(component.id, "image")
            if component.kind in {"system-app", "program-app"}:
                root = "system/apps" if component.kind == "system-app" else "programs"
                target_dir = paths.staging / root / component.id
            elif component.kind == "tool":
                target_dir = paths.staging / "programs" / component.id
            else:
                continue
            if not selected and target_dir.exists():
                context.detail(f"remove disabled component staging: {relative(target_dir)}")
                shutil.rmtree(target_dir)
        for component, library_name in (("file", "libmagic.so.1"), ("lua", "liblua.so.5"),
                                        ("sqlite", "sqlite.so.3")):
            library = paths.staging / "system/lib" / library_name
            if not component_enabled(component, "image") and library.exists():
                context.detail(f"remove disabled shared library staging: {relative(library)}")
                library.unlink()
        for app in staged_user_apps:
            if component_enabled(app, "entry"):
                continue
            root = "system/apps" if app in system_apps else "programs"
            target_dir = paths.staging / root / app
            for filename in (f"{app}.bmp", f"{app}.app.ini"):
                stale = target_dir / filename
                if stale.exists():
                    context.detail(f"remove disabled desktop entry asset: {relative(stale)}")
                    stale.unlink()
        for component in components:
            if not component.api_stage_path:
                continue
            destination = paths.staging / component.api_stage_path
            if not component_api_enabled(component.id) and destination.exists():
                context.detail(f"remove disabled API package: {relative(destination)}")
                destination.unlink()
        if not any(component_enabled(app, "image") for app in stardustui_apps):
            for source in stardustui_theme_files:
                destination = paths.staging / "etc/stardustui/theme" / source.name
                if destination.exists():
                    context.detail(f"remove disabled component staging: {relative(destination)}")
                    destination.unlink()
        ensure_parent(
            context,
            component_prune_stamp,
            json.dumps(component_selection, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        )

    graph.add(Target(
        name="staging-prune",
        outputs=(component_prune_stamp,),
        inputs=(config_path, ROOT / "configs/components.toml"),
        depends_on=("config-sync",),
        kind="generate",
        action=prune_component_staging,
        action_key="staging-prune-v1",
    ))
    esp_names = ["staging-prune", "grub-efi"]
    esp_outputs: list[Path] = [grub_efi]
    grub_font_destination = paths.staging / "boot/grub/fonts/leonos-unicode.pf2"
    target = add_copy(graph, "esp:grub-font", grub_font, grub_font_destination)
    esp_names.append(target.name)
    esp_outputs.append(grub_font_destination)
    for source, destination_rel in [(ROOT / "boot/grub/grub.cfg", "boot/grub/grub.cfg"), (loader_elf, "boot/loader.elf"), (kernel_sys, "system/kernel.sys"), (middle_sys, "system/middlelayer.sys")]:
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    manifest = paths.staging / "system/osmlayer.manifest"
    graph.add(Target(name="esp:manifest", outputs=(manifest,), kind="generate", action=text_action(manifest, "name=osmlayer\nabi=1\nroot=0:/\nfs=fat32\ngui=desktop.elf\n"), action_key="manifest-v1"))
    esp_names.append("esp:manifest")
    esp_outputs.append(manifest)
    for source, destination_rel in ((runtime_loader, "system/lib/ld-leonos.elf"),
                                    (runtime_so, "system/lib/libleonos.so.1")):
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for component, source, filename in (
        ("file", libmagic_so, "libmagic.so.1"),
        ("lua", liblua_so, "liblua.so.5"),
        ("sqlite", sqlite_so, "sqlite.so.3"),
    ):
        if not component_enabled(component, "image"):
            continue
        destination = paths.staging / "system/lib" / filename
        target = add_copy(graph, f"esp:system/lib/{filename}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    dynlinkerror_destination = paths.staging / "system/apps/dynlinkerror/dynlinkerror.elf"
    target = add_copy(graph, "esp:dynlinkerror", dynlinkerror_elf, dynlinkerror_destination)
    esp_names.append(target.name)
    esp_outputs.append(dynlinkerror_destination)
    for source_rel, destination_rel in SYSTEM_FILES:
        source = ROOT / source_rel
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    ui_metro_font_destination = paths.staging / "system/fonts/leonos-metro.ttf"
    ui_win95_font_destination = paths.staging / "system/fonts/leonos-win95.ttf"
    browser_font_destination = paths.staging / "system/fonts/times-new-roman.ttf"
    browser_cjk_font_destination = paths.staging / "system/fonts/simsun.ttc"

    def sync_ui_font(context: ActionContext) -> None:
        for legacy_dir in ("etc", "userland"):
            context.detail(f"remove obsolete staging directory: {relative(paths.staging / legacy_dir)}")
            shutil.rmtree(paths.staging / legacy_dir, ignore_errors=True)
        for legacy_name in ("system.psf", "cjk16.lbf", "metro-latin.lbf", "leonos.lbf", "leonos-ui.ttf"):
            context.detail(f"remove obsolete font: {relative(ui_metro_font_destination.parent / legacy_name)}")
            (ui_metro_font_destination.parent / legacy_name).unlink(missing_ok=True)
        ui_metro_font_destination.parent.mkdir(parents=True, exist_ok=True)
        (paths.staging / "system/state").mkdir(parents=True, exist_ok=True)
        context.copy(ui_metro_font, ui_metro_font_destination)
        context.copy(ui_win95_font, ui_win95_font_destination)
        context.copy(browser_font, browser_font_destination)
        context.copy(browser_cjk_font_source, browser_cjk_font_destination)

    target = graph.add(Target(name="esp:system-font", outputs=(ui_metro_font_destination, ui_win95_font_destination,
                                                                 browser_font_destination, browser_cjk_font_destination),
                              inputs=(ui_metro_font, ui_win95_font, browser_font, browser_cjk_font_source), kind="generate", action=sync_ui_font,
                              action_key="sync-ui-font-v7"))
    esp_names.append(target.name)
    esp_outputs.extend((ui_metro_font_destination, ui_win95_font_destination,
                        browser_font_destination, browser_cjk_font_destination))
    if any(component_enabled(app, "image") for app in stardustui_apps):
        for source in stardustui_theme_files:
            destination = paths.staging / "etc/stardustui/theme" / source.name
            target = add_copy(graph, f"esp:stardustui-theme:{source.stem}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    for source, icon in zip(button_icons, WINDOW_BUTTON_ICONS):
        destination = paths.staging / "system/resources" / icon
        target = add_copy(graph, f"esp:window-icon:{icon}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for source, name in zip(minesweeper_assets, MINESWEEPER_ASSETS):
        destination = paths.staging / "system/resources" / name
        target = add_copy(graph, f"esp:minesweeper-asset:{name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for driver, source in zip(DRIVER_MODULES, driver_outputs):
        destination = paths.staging / f"drivers/{driver}.drv"
        target = add_copy(graph, f"esp:driver:{driver}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for app in staged_user_apps:
        destination = paths.staging / runtime_app_relative(app, "elf", system_apps)
        target = add_copy(graph, f"esp:app:{app}", app_elfs[app], destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for app in staged_user_apps:
        if not component_enabled(app, "entry"):
            continue
        source = paths.out / f"generated/app-icons/{app}.bmp"
        destination = paths.staging / runtime_app_relative(app, "bmp", system_apps)
        target = add_copy(graph, f"esp:icon:{app}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for app in staged_user_apps:
        if not component_enabled(app, "entry"):
            continue
        source = ROOT / "userland/apps" / app / f"{app}.app.ini"
        if source.exists():
            destination = paths.staging / runtime_app_relative(app, "app.ini", system_apps)
            target = add_copy(graph, f"esp:manifest:{app}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("busybox", "image"):
        busybox_destination = paths.staging / "programs/busybox/busybox.elf"
        target = add_copy(graph, "esp:busybox", busybox_elf, busybox_destination)
        esp_names.append(target.name)
        esp_outputs.append(busybox_destination)
        for source in (
            ROOT / "third_party/busybox/LICENSE",
            ROOT / "userland/busybox/busybox.app.ini",
        ):
            destination = paths.staging / "programs/busybox" / source.name
            target = add_copy(graph, f"esp:busybox:{source.name}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("file", "image"):
        file_destination = paths.staging / "programs/file/file.elf"
        target = add_copy(graph, "esp:file", file_elf, file_destination)
        esp_names.append(target.name)
        esp_outputs.append(file_destination)
        file_license_destination = paths.staging / "programs/file/COPYING"
        target = add_copy(graph, "esp:file:COPYING", file_source / "COPYING",
                          file_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(file_license_destination)
        magic_destination = paths.staging / "system/share/misc/magic.mgc"
        target = add_copy(graph, "esp:file:magic.mgc", magic_database, magic_destination)
        esp_names.append(target.name)
        esp_outputs.append(magic_destination)
    if component_enabled("tcc", "image"):
        tcc_destination = paths.staging / "programs/tcc"

        def sync_tcc_runtime(context: ActionContext) -> None:
            if tcc_destination.exists():
                context.detail(f"replace staged TCC runtime: {relative(tcc_destination)}")
                shutil.rmtree(tcc_destination)
            context.detail(
                f"copy runtime tree: {relative(tcc_runtime_dir)} -> {relative(tcc_destination)}"
            )
            shutil.copytree(tcc_runtime_dir, tcc_destination)
            context.copy(tcc_elf, tcc_destination / "tcc.elf")
            context.copy(tcc_app_manifest, tcc_destination / "tcc.app.ini")

        target = graph.add(Target(
            name="esp:tcc",
            outputs=(tcc_destination / "tcc.elf", tcc_destination / "lib/libtcc1.a",
                     tcc_destination / "lib/libleonos-tcc-rt.a",
                     tcc_destination / "tcc.app.ini"),
            inputs=(tcc_elf, tcc_stamp, tcc_app_manifest),
            depends_on=("tcc",),
            kind="generate",
            action=sync_tcc_runtime,
            action_key="sync-tcc-runtime-v2",
        ))
        esp_names.append(target.name)
        esp_outputs.extend((tcc_destination / "tcc.elf", tcc_destination / "lib/libtcc1.a",
                            tcc_destination / "lib/libleonos-tcc-rt.a",
                            tcc_destination / "tcc.app.ini"))
    if component_enabled("lua", "image"):
        lua_destination = paths.staging / "programs/lua"
        for source in (lua_elf, lua_port / "LICENSE", lua_app_manifest):
            destination = lua_destination / ("lua.elf" if source == lua_elf else source.name)
            target = add_copy(graph, f"esp:lua:{destination.name}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("cmd", "image"):
        cmd_destination = paths.staging / "programs/cmd"
        for source in (cmd_elf, cmd_source / "LICENSE", cmd_port / "README.md"):
            destination = cmd_destination / ("cmd.elf" if source == cmd_elf else source.name)
            target = add_copy(graph, f"esp:cmd:{destination.name}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("nano", "image"):
        nano_license_destination = paths.staging / "programs/nano/COPYING"
        target = add_copy(graph, "esp:nano:COPYING", ROOT / "third_party/nano/COPYING",
                          nano_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(nano_license_destination)
    if component_enabled("fastfetch", "image"):
        fastfetch_license_destination = paths.staging / "programs/fastfetch/LICENSE"
        target = add_copy(graph, "esp:fastfetch:LICENSE", fastfetch_source / "LICENSE",
                          fastfetch_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(fastfetch_license_destination)
    if component_enabled("pleditor", "image"):
        pleditor_license_destination = paths.staging / "programs/pleditor/LICENSE"
        target = add_copy(graph, "esp:pleditor:LICENSE", ROOT / "third_party/pl_editor/LICENSE",
                          pleditor_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(pleditor_license_destination)
    test_mp3 = paths.staging / "test/test.mp3"
    target = add_copy(graph, "esp:test:test.mp3", ROOT / "test/test.mp3", test_mp3)
    esp_names.append(target.name)
    esp_outputs.append(test_mp3)
    if component_api_enabled("helloworld"):
        helloworld_api = paths.out / "api/helloworld.api"
        api_destination = component_api_destination("helloworld")
        graph.add(Target(
            name="esp:api:helloworld",
            outputs=(helloworld_api,),
            inputs=(app_elfs["helloworld"], ROOT / "tools/build_api.py"),
            kind="generate",
            command=(PYTHON, "tools/build_api.py", relative(app_elfs["helloworld"]),
                     relative(helloworld_api)),
        ))
        target = add_copy(graph, "esp:api:helloworld-copy", helloworld_api, api_destination)
        esp_names.append(target.name)
        esp_outputs.append(api_destination)
    if component_api_enabled("doom"):
        doom_wad = ROOT / "third_party/doomgeneric/freedoom1.wad"
        doom_icon = paths.out / "generated/app-icons/doom.bmp"
        doom_api = paths.out / "api/doom.api"
        doom_api_destination = component_api_destination("doom")
        graph.add(Target(
            name="esp:api:doom",
            outputs=(doom_api,),
            inputs=(app_elfs["doomlauncher"], app_elfs["doom"], doom_wad,
                    doom_icon, ROOT / "tools/build_api.py"),
            kind="generate",
            command=(
                PYTHON, "tools/build_api.py",
                "--name", "DOOM",
                "--version", "1.0.0-freedoom",
                "--main-exe", "doomlauncher.elf",
                "--default-path", "0:/programs/doom",
                "--requires-admin",
                "--desktop-shortcut",
                "--icon", "doom.bmp",
                "--file", relative(app_elfs["doomlauncher"]), "doomlauncher.elf",
                "--file", relative(app_elfs["doom"]), "doom.elf",
                "--file", relative(doom_wad), "freedoom1.wad",
                "--file", relative(doom_icon), "doom.bmp",
                "--output", relative(doom_api),
            ),
        ))
        target = add_copy(graph, "esp:api:doom-copy", doom_api, doom_api_destination)
        esp_names.append(target.name)
        esp_outputs.append(doom_api_destination)
    if component_api_enabled("oschinpt"):
        oschinpt_api = paths.out / "api/oschinpt.api"
        oschinpt_api_destination = component_api_destination("oschinpt")
        oschinpt_dict = ROOT / "third_party/rime-pinyin-simp/pinyin_simp.dict.yaml"
        oschinpt_index = paths.out / "api/pinyin_simp.idx"
        oschinpt_license = ROOT / "third_party/rime-pinyin-simp/LICENSE"
        oschinpt_attribution = ROOT / "third_party/rime-pinyin-simp/ATTRIBUTION.txt"
        oschinpt_settings = ROOT / "userland/apps/oschinpt/settings.ini"
        graph.add(Target(
            name="esp:api:oschinpt-index",
            outputs=(oschinpt_index,),
            inputs=(oschinpt_dict, ROOT / "tools/make_oschinpt_index.py"),
            kind="generate",
            command=(
                PYTHON, "tools/make_oschinpt_index.py",
                "--input", relative(oschinpt_dict),
                "--output", relative(oschinpt_index),
            ),
        ))
        graph.add(Target(
            name="esp:api:oschinpt",
            outputs=(oschinpt_api,),
            inputs=(app_elfs["oschinpt"], oschinpt_dict, oschinpt_index,
                    oschinpt_license, oschinpt_attribution, oschinpt_settings,
                    ROOT / "tools/build_api.py"),
            kind="generate",
            command=(
                PYTHON, "tools/build_api.py",
                "--name", "LeonOS 4 Chinese Input",
                "--version", "1.0.0",
                "--main-exe", "oschinpt.elf",
                "--default-path", "0:/programs/oschinpt",
                "--requires-admin",
                "--input-method-id", "oschinpt",
                "--input-method-abbreviation", "OSC",
                "--input-method-startup", "login",
                "--input-method-settings", "settings.ini",
                "--launch-after-install",
                "--file", relative(app_elfs["oschinpt"]), "oschinpt.elf",
                "--file", relative(oschinpt_dict), "pinyin_simp.dict.yaml",
                "--file", relative(oschinpt_index), "oscp.idx",
                "--file", relative(oschinpt_license), "LICENSE",
                "--file", relative(oschinpt_attribution), "ATTRIBUTION.txt",
                "--file", relative(oschinpt_settings), "settings.ini",
                "--output", relative(oschinpt_api),
            ),
        ))
        target = add_copy(graph, "esp:api:oschinpt-copy", oschinpt_api,
                          oschinpt_api_destination)
        esp_names.append(target.name)
        esp_outputs.append(oschinpt_api_destination)
    config_destination = paths.staging / "system/config/leonos.conf"
    target = add_copy(graph, "esp:config", config_path, config_destination)
    esp_names.append(target.name)
    esp_outputs.append(config_destination)
    for source in collect("system/config/*"):
        if source.name == "display.conf":
            continue
        destination = paths.staging / "system/config" / source.name
        target = add_copy(graph, f"esp:config:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    display_destination = paths.staging / "system/config/display.conf"
    target = add_copy(graph, "esp:config:display.conf", generated_display_config, display_destination)
    esp_names.append(target.name)
    esp_outputs.append(display_destination)
    entry_policy_destination = paths.staging / "system/config/desktop-entries.conf"
    target = add_copy(graph, "esp:config:desktop-entries.conf", desktop_entry_policy,
                      entry_policy_destination)
    esp_names.append(target.name)
    esp_outputs.append(entry_policy_destination)
    for source in collect("system/docs/*.hlp"):
        destination = paths.staging / "docs" / source.name
        target = add_copy(graph, f"esp:doc:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    graph.add(Target(name="esp", depends_on=tuple(esp_names), group=True, kind="aggregate"))

    vmdk = paths.images / "leonos4.vmdk"
    raw = paths.images / "leonos4.raw"
    esp_fat = paths.images / "esp.fat"
    vmdk_language = "zh" if config_bool(values, "CONFIG_VMDK_DEFAULT_LANGUAGE_ZH") else "en"
    graph.add(Target(name="image-vmdk", outputs=(vmdk, raw, esp_fat),
                     inputs=tuple([*esp_outputs, config_path, ROOT / "tools/make_image.py"]),
                     depends_on=("esp",), kind="generate", command=(PYTHON, "tools/make_image.py", "--out",
                     relative(vmdk), "--raw", relative(raw), "--esp-tree",
                     relative(paths.staging), "--default-language", vmdk_language,
                     "--size-mib", str(config_int(values, "CONFIG_IMAGE_SIZE_MIB")))))

    iso = paths.images / "leonos4.iso"
    iso_stage = paths.out / "iso"

    def make_iso(context: ActionContext) -> None:
        if iso_stage.exists():
            context.detail(f"replace ISO staging tree: {relative(iso_stage)}")
            shutil.rmtree(iso_stage)
        context.detail(f"copy ESP staging tree: {relative(paths.staging)} -> {relative(iso_stage)}")
        shutil.copytree(paths.staging, iso_stage)
        context.run(("grub-mkrescue", "-o", relative(iso), relative(iso_stage)), announce=True)

    graph.add(Target(name="image-iso", outputs=(iso,), inputs=tuple([*esp_outputs, ROOT / "boot/grub/grub.cfg"]), depends_on=("esp",), kind="generate", action=make_iso, action_key="iso-stage-v1"))

    installer_root = paths.out / "install/root.fat"
    installer_stage = paths.out / "install/root"
    graph.add(Target(name="installer-root", outputs=(installer_root,), inputs=tuple([*esp_outputs, app_elfs["desktop"], app_elfs["installer"], installer_runtime_so, *(installer_policy_elfs.values()), ROOT / "tools/make_installer_root.py"]), depends_on=("esp", "installer-runtime"), kind="generate", command=(PYTHON, "tools/make_installer_root.py", "--out", relative(installer_root), "--stage", relative(installer_stage), "--esp-tree", relative(paths.staging), "--installed-policy-dir", relative(paths.out / "userland-installer-policy"), "--policy-runtime", relative(installer_runtime_so), "--policy-apps", *installer_policy_apps, "--userland-dir", relative(paths.out / "userland"), "--generated-icons-dir", relative(paths.out / "generated/app-icons"), "--manifest", relative(manifest), "--size-mib", str(config_int(values, "CONFIG_INSTALLER_ROOT_SIZE_MIB")))))
    installer_iso = paths.images / "leonos4-installer.iso"
    installer_boot_image = paths.out / "install/installer-efiboot.img"
    graph.add(Target(name="installer-image", outputs=(installer_iso, installer_boot_image), inputs=(loader_elf, kernel_sys, middle_sys, installer_root, grub_font, grub_efi_dir / "modinfo.sh", ROOT / "boot/grub/installer.cfg", ROOT / "boot/grub/installer_embedded.cfg", ROOT / "tools/make_installer_iso.py"), kind="generate", command=(PYTHON, "tools/make_installer_iso.py", "--out", relative(installer_iso), "--stage", relative(paths.out / "installer-iso"), "--boot-image", relative(installer_boot_image), "--loader", relative(loader_elf), "--kernel", relative(kernel_sys), "--middlelayer", relative(middle_sys), "--installer-root", relative(installer_root), "--grub-font", relative(grub_font), "--work-dir", relative(paths.out / "install"), "--grub-efi-dir", grub_dir_arg)))

    graph.add(Target(name="all", depends_on=("config-sync", "build-info", "loader", "kernel", "drivers", "middlelayer", "userland", "sdk", "esp"), group=True, kind="aggregate"))
    graph.add(Target(name="run", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values)))
    graph.add(Target(name="run-debug", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values, debug=True)))
    graph.add(Target(name="run-iso", inputs=(vmdk, iso), depends_on=("image-vmdk", "image-iso"), kind="command", command=qemu_command(paths, values, debug=True, iso=True)))
    graph.add(Target(name="installer", depends_on=("all", "installer-root", "installer-image"), group=True, kind="aggregate"))

    release_dir = paths.out / "release"
    release_stamp = release_dir / ".release-stamp"
    release_checksums = release_dir / "SHA256SUMS.txt"
    release_notices = release_dir / "THIRD_PARTY_NOTICES.md"
    release_artifacts = (
        paths.images / "leonos4.vmdk",
        paths.images / "leonos4.iso",
        paths.images / "leonos4-installer.iso",
        developer_sdk,
    )
    # The stamp makes a metadata-only release cacheable. The two public files
    # are only declared while their respective switches are enabled.
    release_outputs: list[Path] = [release_stamp]
    if config_bool(values, "CONFIG_RELEASE_WRITE_CHECKSUMS"):
        release_outputs.append(release_checksums)
    if config_bool(values, "CONFIG_RELEASE_INCLUDE_THIRD_PARTY_NOTICES"):
        release_outputs.append(release_notices)

    def make_release(context: ActionContext) -> None:
        release_dir.mkdir(parents=True, exist_ok=True)
        if config_bool(values, "CONFIG_RELEASE_WRITE_CHECKSUMS"):
            lines: list[str] = []
            for artifact in release_artifacts:
                if not artifact.exists():
                    raise BuildFailure(f"release artifact is missing: {relative(artifact)}")
                with artifact.open("rb") as handle:
                    digest = hashlib.sha256()
                    for block in iter(lambda: handle.read(1024 * 1024), b""):
                        digest.update(block)
                lines.append(f"{digest.hexdigest()}  {artifact.name}")
            ensure_parent(context, release_checksums, "\n".join(lines) + "\n")
        elif release_checksums.exists():
            context.detail(f"remove disabled release artifact: {relative(release_checksums)}")
            release_checksums.unlink()
        if config_bool(values, "CONFIG_RELEASE_INCLUDE_THIRD_PARTY_NOTICES"):
            source = ROOT / "docs/THIRD_PARTY.md"
            ensure_parent(context, release_notices, source.read_text(encoding="utf-8"))
        elif release_notices.exists():
            context.detail(f"remove disabled release artifact: {relative(release_notices)}")
            release_notices.unlink()
        release_stamp.touch()

    graph.add(Target(
        name="release",
        outputs=tuple(release_outputs),
        inputs=(ROOT / "docs/THIRD_PARTY.md", config_path, *release_artifacts),
        depends_on=("image-vmdk", "image-iso", "installer", "sdk"),
        kind="generate",
        action=make_release,
        action_key="release-artifacts-v1",
    ))

    def menuconfig(context: ActionContext) -> None:
        context.run((PYTHON, "tools/generate_component_kconfig.py"), announce=True)
        config_path.parent.mkdir(parents=True, exist_ok=True)
        if not config_path.exists():
            shutil.copy2(ROOT / "configs/default.conf", config_path)
        context.run(
            ("kconfig-mconf", "Kconfig"),
            environment={"KCONFIG_CONFIG": str(config_path)},
            announce=True,
            interactive=True,
        )
        context.run((
            PYTHON, "tools/kconfig_sync.py", "--config", relative(config_path),
            "--defaults", "configs/default.conf", "--out-dir", relative(generated),
            "--selection-out", relative(paths.out / "generated/component-selection.json"),
        ), announce=True)

    graph.add(Target(
        name="menuconfig",
        inputs=(ROOT / "Kconfig", ROOT / "Kconfig.components",
                ROOT / "configs/default.conf", ROOT / "configs/components.toml",
                ROOT / "tools/generate_component_kconfig.py",
                ROOT / "tools/kconfig_sync.py", ROOT / "buildsystem/components.py"),
        kind="command", action=menuconfig, action_key="menuconfig-v4", always=True,
    ))

    def clean(context: ActionContext) -> None:
        for directory in (paths.out, paths.legacy_out, paths.target_state, paths.tmp):
            if directory.exists():
                shutil.rmtree(directory)
        context.runner.store.clear_target_states()
        paths.ensure()

    graph.add(Target(name="clean", kind="command", action=clean, action_key="clean-v1", always=True))

    graph.add(Target(name="test-license-server", inputs=(ROOT / "tools/test_license_server.py", ROOT / "tools/license_server.py"), kind="command", command=(PYTHON, "tools/test_license_server.py")))
    graph.add(Target(name="test-los2w", inputs=tuple(collect("los2w/*.py")), kind="command", command=(PYTHON, "-c", "from los2w.selftest import run_self_tests; print('\\n'.join(run_self_tests()))")))
    graph.add(Target(
        name="test-component-config",
        inputs=(ROOT / "tools/test_component_config.py",
                ROOT / "tools/generate_component_kconfig.py",
                ROOT / "buildsystem/components.py",
                ROOT / "configs/components.toml"),
        kind="command",
        command=(PYTHON, "tools/test_component_config.py"),
    ))

    def qmp_test(context: ActionContext, editor: str = "nano", tcc_smoke: bool = False,
                 fastfetch_smoke: bool = False,
                 dynlinkerror_smoke: bool = False,
                 cmd_pipeline_smoke: bool = False,
                 desktop_app: str | None = None) -> None:
        if cmd_pipeline_smoke and not component_enabled("cmd", "image"):
            raise BuildFailure(
                "QMP cmd pipeline test requires CONFIG_LEON_COMPONENT_TOOL_CMD_IMAGE=y"
            )
        socket = Path(tempfile.gettempdir()) / f"leonos4-qmp-{context.runner.task_id}.sock"
        test_name = desktop_app if desktop_app else ("dynlinkerror" if dynlinkerror_smoke else
                                                     ("cmd" if cmd_pipeline_smoke else
                                                     ("fastfetch" if fastfetch_smoke else
                                                      ("tcc" if tcc_smoke else editor))))
        serial_log = paths.out / f"qmp-{test_name}-serial.log"
        socket.unlink(missing_ok=True)
        serial_log.parent.mkdir(parents=True, exist_ok=True)
        command = list(qemu_command(paths, values, debug=True))
        command += ["-qmp", f"unix:{socket},server=on,wait=off"]
        context.runner.logger.command(context.worker_id, command)
        with serial_log.open("wb") as serial_output:
            process = subprocess.Popen(command, cwd=ROOT, stdout=serial_output,
                                       stderr=subprocess.STDOUT)
            try:
                smoke_command = [PYTHON, "tools/qmp_terminal_smoke.py"]
                if tcc_smoke:
                    smoke_command.append("--tcc")
                elif fastfetch_smoke:
                    smoke_command.append("--fastfetch")
                elif dynlinkerror_smoke:
                    smoke_command.append("--dynlinkerror")
                elif cmd_pipeline_smoke:
                    smoke_command.append("--cmd-pipeline")
                elif desktop_app:
                    smoke_command += ["--desktop-app", desktop_app]
                else:
                    smoke_command += ["--editor", editor]
                smoke_command.append(str(socket))
                context.run(tuple(smoke_command), announce=True)
                process.wait(timeout=15)
                if process.returncode not in (0, None):
                    raise BuildFailure(f"QEMU QMP test exited with {process.returncode}")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                socket.unlink(missing_ok=True)
        serial_text = serial_log.read_text(encoding="utf-8", errors="replace")
        # Once the graphical desktop owns the console, the framebuffer remains
        # the most reliable user-visible assertion. Keep serial checks as well
        # when a specific process launch is expected.
        if cmd_pipeline_smoke:
            screenshot = paths.images / "cmd-pipeline-qmp-smoke.ppm"
            if not screenshot.is_file() or screenshot.stat().st_size == 0:
                raise BuildFailure("QMP cmd pipeline test did not produce a terminal screenshot")
        if desktop_app:
            expected_spawns = (f"spawn path=0:/programs/{desktop_app}/{desktop_app}.elf",)
            expected_exits = (f"name={desktop_app}.elf",)
        elif tcc_smoke:
            expected_spawns = (
                "spawn path=0:/programs/tcc/tcc.elf",
                "spawn path=0:/programs/tcc/hello.elf",
            )
            expected_exits = ("name=tcc.elf", "name=hello.elf")
        elif fastfetch_smoke:
            expected_spawns = ("spawn path=0:/programs/fastfetch/fastfetch.elf",)
            expected_exits = ("name=fastfetch.elf",)
        elif dynlinkerror_smoke:
            expected_spawns = (
                "spawn path=0:/programs/nano/nano.elf",
                "spawn path=0:/system/apps/dynlinkerror/dynlinkerror.elf",
            )
            expected_exits = ("name=nano.elf",)
        elif cmd_pipeline_smoke:
            expected_spawns = (
                "spawn path=0:/programs/cmd/cmd.elf",
                "spawn path=0:/programs/busybox/busybox.elf",
            )
            expected_exits = ("name=busybox.elf",)
        else:
            expected_spawns = (f"spawn path=0:/programs/{editor}/{editor}.elf",)
            expected_exits = (f"name={editor}.elf",)
        for expected_spawn in expected_spawns:
            # Desktop launchers still use the kernel's controlled spawn API,
            # while Hush now performs a real COW fork followed by execve.
            # Accept either diagnostic form, but always require the exact
            # executable path so a different child cannot satisfy the check.
            executable_path = expected_spawn.removeprefix("spawn path=")
            fork_exec_seen = "exec pid=" in serial_text and f"path={executable_path}" in serial_text
            if expected_spawn not in serial_text and not fork_exec_seen:
                raise BuildFailure(f"QMP test did not start {test_name}: missing {expected_spawn}")
        for expected_exit in expected_exits:
            if expected_exit not in serial_text:
                raise BuildFailure(f"QMP test did not observe {test_name} exit: missing {expected_exit}")
        if cmd_pipeline_smoke:
            cmd_pids = re.findall(
                r"\[ntclks\] exec pid=(\d+) path=0:/programs/cmd/cmd\.elf",
                serial_text,
            )
            if not cmd_pids:
                raise BuildFailure("QMP cmd pipeline test did not identify the cmd process")
            cmd_pid = cmd_pids[-1]
            stage_pids = re.findall(
                rf"\[ntclks\] spawn path=0:/programs/busybox/busybox\.elf pid=(\d+) "
                rf"parent={re.escape(cmd_pid)} fds=",
                serial_text,
            )
            if len(stage_pids) < 2:
                raise BuildFailure(
                    "QMP cmd pipeline test did not start both BusyBox pipeline stages"
                )
            missing_stage_exits = [
                pid for pid in stage_pids
                if f"scheduler task exited pid={pid} name=busybox.elf code=0" not in serial_text
            ]
            if missing_stage_exits:
                raise BuildFailure(
                    "QMP cmd pipeline test did not observe successful exit for BusyBox stage(s): "
                    + ", ".join(missing_stage_exits)
                )
    graph.add(Target(name="test-qmp-terminal", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=qmp_test, action_key="qmp-terminal-v3"))
    graph.add(Target(name="test-qmp-pleditor", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, "pleditor"), action_key="qmp-pleditor-v1"))
    graph.add(Target(name="test-qmp-tcc", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, tcc_smoke=True), action_key="qmp-tcc-v1"))
    graph.add(Target(name="test-qmp-fastfetch", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, fastfetch_smoke=True), action_key="qmp-fastfetch-v1"))
    graph.add(Target(name="test-qmp-dynlinkerror", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, dynlinkerror_smoke=True), action_key="qmp-dynlinkerror-v1"))
    graph.add(Target(name="test-qmp-cmd", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, cmd_pipeline_smoke=True), action_key="qmp-cmd-v2"))
    graph.add(Target(name="test-qmp-stardust", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, desktop_app="stardusthello"), action_key="qmp-stardust-v1"))
    selected_tests: list[str] = []
    if config_bool(values, "CONFIG_TEST_LICENSE_SERVER"):
        selected_tests.append("test-license-server")
    if config_bool(values, "CONFIG_TEST_LOS2W"):
        selected_tests.append("test-los2w")
    if config_bool(values, "CONFIG_TEST_QMP_TERMINAL"):
        selected_tests.append("test-qmp-terminal")
    if config_bool(values, "CONFIG_TEST_QMP_TCC") and component_enabled("tcc", "image"):
        selected_tests.append("test-qmp-tcc")
    if component_enabled("cmd", "image"):
        selected_tests.append("test-qmp-cmd")
    if config_bool(values, "CONFIG_TEST_QMP_STARDUST") and component_enabled("stardusthello", "image"):
        selected_tests.append("test-qmp-stardust")
    if config_bool(values, "CONFIG_TEST_COMPONENT_CONFIG"):
        selected_tests.append("test-component-config")
    graph.add(Target(name="test-all", depends_on=tuple(selected_tests), group=True, kind="aggregate"))
    return graph


def require_linux() -> None:
    if platform.system() != "Linux":
        raise BuildFailure("LeonOS BuildSystem only supports Linux or WSL; run python3 build.py inside WSL")


def require_tools(names: Iterable[str]) -> None:
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise BuildFailure("missing required tools: " + ", ".join(missing))


def require_grub_efi_modules(paths: BuildPaths, task: str) -> None:
    if task not in {"esp", "all", "image-vmdk", "image-iso", "installer", "release", "run", "run-debug", "run-iso"}:
        return
    candidates = (
        paths.deps / "grub-efi-amd64-bin/usr/lib/grub/x86_64-efi/modinfo.sh",
        Path("/usr/lib/grub/x86_64-efi/modinfo.sh"),
    )
    if not any(candidate.exists() for candidate in candidates):
        raise BuildFailure(
            "missing GRUB EFI modules: install grub-efi-amd64-bin or place its x86_64-efi directory under "
            f"{paths.deps.relative_to(paths.root).as_posix()}/grub-efi-amd64-bin/usr/lib/grub"
        )


def task_tools(task: str) -> tuple[str, ...]:
    compiler = ("clang", "ld.lld")
    host_userland = ("autoreconf", "autoconf", "automake", "libtoolize", "make",
                     "gcc", "gawk")
    userland = (*compiler, "llvm-ar", *host_userland)
    esp = (*userland, "rustc", "grub-mkfont", "grub-mkstandalone")
    vmdk = (*esp, "truncate", "sgdisk", "mkfs.fat", "mcopy", "dd", "qemu-img")
    iso = (*esp, "grub-mkrescue", "xorriso")
    if task in {"file", "file-magic", "sqlite"}:
        return userland
    if task in {"kernel", "loader", "drivers"}:
        return compiler
    if task == "middlelayer":
        return (*compiler, "rustc")
    if task == "userland":
        return userland
    if task in {"esp", "all"}:
        return esp
    if task == "image-vmdk":
        return vmdk
    if task == "image-iso":
        return iso
    if task == "installer":
        return (*vmdk, "grub-mkrescue", "xorriso")
    if task == "release":
        # A release bundles all bootable artifacts and the SDK. Keep this
        # check aligned with the graph dependencies before long builds start.
        return (*vmdk, "grub-mkrescue", "xorriso")
    if task in {"run", "run-debug"}:
        return (*vmdk, "qemu-system-x86_64")
    if task == "run-iso":
        return (*vmdk, "grub-mkrescue", "xorriso", "qemu-system-x86_64")
    if task == "menuconfig":
        return ("kconfig-mconf",)
    if task in {"test-qmp-terminal", "test-qmp-pleditor", "test-qmp-tcc", "test-qmp-fastfetch",
                "test-qmp-dynlinkerror", "test-qmp-cmd", "test-qmp-stardust", "test-all"}:
        return (*vmdk, "qemu-system-x86_64")
    return ()


def create_runner(
    paths: BuildPaths,
    graph: BuildGraph,
    task_id: str,
    *,
    verbose: bool = False,
) -> BuildRunner:
    return BuildRunner(graph, paths, load_settings(paths), task_id, verbose=verbose)


def build_roots(graph: BuildGraph, target: Target) -> tuple[Target, ...]:
    if target.name in BUILD_NUMBER_EXEMPT_TARGETS or target.name == "build-info":
        return (target,)
    return (graph.targets["build-info"], target)


def display_help() -> str:
    return """LeonOS BuildSystem\n\nCommands:\n  build.py help\n  build.py tui\n  build.py [-v|--verbose] run <task> [--profile NAME] [--set CONFIG_KEY=VALUE]\n  build.py profile <task> [--profile NAME] [--set CONFIG_KEY=VALUE]\n  build.py config <list|save|load|reset|import|export> [name] [path]\n  build.py info <file-or-task>\n  build.py why <file-or-task>\n  build.py affected <file>\n  build.py cache <stats|prune>\n  build.py settings\n  build.py map\n  build.py gen <file>\n  build.py test <license-server|los2w|component-config|qmp-terminal|qmp-pleditor|qmp-tcc|qmp-fastfetch|qmp-dynlinkerror|qmp-cmd|qmp-stardust|all>\n  build.py client <run|gen|test|profile> ...\n  build.py status <task-id>\n  build.py log <task-id>\n\nOptions:\n  -v, --verbose  Print target graph, cache decisions, commands, process diagnostics, and actions.\n  --profile       Build from configs/profiles/<name>.conf without modifying the active config.\n\nTasks:\n  all, config-sync, build-info, loader, kernel, drivers, middlelayer, userland, sdk, esp, image-vmdk, image-iso, installer, release, run, run-debug, run-iso, menuconfig, clean\n"""


PROFILE_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


def profile_directory() -> Path:
    return ROOT / "configs/profiles"


def profile_path(name: str) -> Path:
    if not PROFILE_NAME_RE.fullmatch(name):
        raise BuildFailure(f"invalid profile name: {name}")
    return profile_directory() / f"{name}.conf"


def write_config_values(path: Path, values: dict[str, str]) -> None:
    lines = [
        f"# {key} is not set" if value == "n" else f"{key}={value}"
        for key, value in sorted(values.items())
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def apply_config_overrides(source: Path, destination: Path, overrides: list[str]) -> None:
    values = parse_config_values(source)
    components = load_components(ROOT / "configs/components.toml")
    known = set(parse_config_values(ROOT / "configs/default.conf")) | component_config_symbols(components)
    for override in overrides:
        if "=" not in override:
            raise BuildFailure(f"configuration override must be KEY=VALUE: {override}")
        key, value = (part.strip() for part in override.split("=", 1))
        if not key.startswith("CONFIG_") or not key.isidentifier():
            raise BuildFailure(f"invalid configuration symbol: {key}")
        if key not in known:
            raise BuildFailure(f"unknown configuration symbol: {key}")
        if value not in {"y", "n"} and not value.startswith('"') and not re.fullmatch(r"-?[0-9]+", value):
            raise BuildFailure(f"invalid configuration value for {key}: {value}")
        if value == "y":
            for choice in CONFIG_CHOICE_GROUPS:
                if key in choice:
                    for peer in choice:
                        if peer != key:
                            values[peer] = "n"
                    break
        values[key] = value
    write_config_values(destination, values)


def resolve_build_config(
    paths: BuildPaths,
    task_id: str,
    profile: str | None,
    overrides: list[str],
    *,
    interactive: bool = False,
) -> Path:
    if profile:
        source = profile_path(profile)
        if not source.exists():
            raise BuildFailure(f"profile does not exist: {profile}")
        if interactive and not overrides:
            return source
    else:
        source = paths.kconfig if paths.kconfig.exists() else ROOT / "configs/default.conf"
        if interactive and not overrides:
            return source
    if not overrides and not profile:
        return source
    paths.tmp.mkdir(parents=True, exist_ok=True)
    temporary = paths.tmp / f"config-{task_id}.conf"
    shutil.copy2(source, temporary)
    if overrides:
        apply_config_overrides(temporary, temporary, overrides)
    return temporary


def sync_config_file(paths: BuildPaths, config: Path) -> None:
    try:
        subprocess.run((PYTHON, "tools/generate_component_kconfig.py"), cwd=ROOT, check=True)
        subprocess.run(
            (
                PYTHON, "tools/kconfig_sync.py", "--config", relative(config),
                "--defaults", "configs/default.conf", "--out-dir",
                relative(paths.generated_include), "--selection-out",
                relative(paths.out / "generated/component-selection.json"),
            ),
            cwd=ROOT,
            check=True,
        )
    except subprocess.CalledProcessError as exc:
        raise BuildFailure(f"configuration synchronization failed with exit {exc.returncode}") from exc


def normalize_config_copy(paths: BuildPaths, source: Path, stem: str) -> Path:
    """Validate and normalize a config without first replacing a saved profile."""
    paths.tmp.mkdir(parents=True, exist_ok=True)
    temporary = paths.tmp / f"{stem}.conf"
    shutil.copy2(source, temporary)
    sync_config_file(paths, temporary)
    return temporary


def handle_config_command(paths: BuildPaths, arguments: argparse.Namespace) -> int:
    action = arguments.action
    if action == "list":
        profile_directory().mkdir(parents=True, exist_ok=True)
        for profile in sorted(profile_directory().glob("*.conf")):
            print(profile.stem)
        return 0
    if action == "reset":
        paths.kconfig.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "configs/default.conf", paths.kconfig)
        sync_config_file(paths, paths.kconfig)
        print("active configuration reset to defaults")
        return 0
    if not arguments.name:
        raise BuildFailure(f"config {action} requires a profile name")
    saved_profile = profile_path(arguments.name)
    if action == "save":
        source = paths.kconfig if paths.kconfig.exists() else ROOT / "configs/default.conf"
        saved_profile.parent.mkdir(parents=True, exist_ok=True)
        normalized = normalize_config_copy(paths, source, f"profile-save-{arguments.name}")
        shutil.copy2(normalized, saved_profile)
        print(f"saved profile {arguments.name}")
        return 0
    if action == "load":
        if not saved_profile.exists():
            raise BuildFailure(f"profile does not exist: {arguments.name}")
        paths.kconfig.parent.mkdir(parents=True, exist_ok=True)
        normalized = normalize_config_copy(paths, saved_profile, f"profile-load-{arguments.name}")
        shutil.copy2(normalized, paths.kconfig)
        print(f"loaded profile {arguments.name}")
        return 0
    if action not in {"import", "export"} or not arguments.path:
        raise BuildFailure(f"config {action} requires a profile name and file path")
    external = root_path(arguments.path)
    if action == "import":
        if not external.exists():
            raise BuildFailure(f"configuration file does not exist: {external}")
        saved_profile.parent.mkdir(parents=True, exist_ok=True)
        normalized = normalize_config_copy(paths, external, f"profile-import-{arguments.name}")
        shutil.copy2(normalized, saved_profile)
        print(f"imported profile {arguments.name}")
    else:
        if not saved_profile.exists():
            raise BuildFailure(f"profile does not exist: {arguments.name}")
        external.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(saved_profile, external)
        print(f"exported profile {arguments.name}")
    return 0


def complete_simple(store: TaskStore, task_id: str, command: str, text: str, success: bool = True) -> None:
    log = store.log_path(task_id)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(text.rstrip() + "\n", encoding="utf-8", newline="\n")
    store.update(task_id, status="done" if success else "failed", started_at=utc_now(), finished_at=utc_now(), task=command, error="" if success else text)


def human_label(value: object) -> str:
    return str(value).replace("_", " ").replace("-", " ").capitalize()


def human_scalar(value: object) -> str:
    if value is None:
        return "none"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def append_human_lines(lines: list[str], value: object, indent: str = "") -> None:
    if isinstance(value, dict):
        if not value:
            lines.append(f"{indent}(none)")
            return
        for key, item in value.items():
            label = human_label(key)
            if isinstance(item, (dict, list, tuple)):
                lines.append(f"{indent}{label}:")
                append_human_lines(lines, item, indent + "  ")
            else:
                lines.append(f"{indent}{label}: {human_scalar(item)}")
        return
    if isinstance(value, (list, tuple)):
        if not value:
            lines.append(f"{indent}(none)")
            return
        for item in value:
            if isinstance(item, (dict, list, tuple)):
                lines.append(f"{indent}-")
                append_human_lines(lines, item, indent + "  ")
            else:
                lines.append(f"{indent}- {human_scalar(item)}")
        return
    lines.append(f"{indent}{human_scalar(value)}")


def format_human(title: str, value: object) -> str:
    lines = [title]
    append_human_lines(lines, value)
    return "\n".join(lines)


def emit_data(value: object, *, json_output: bool, title: str) -> str:
    text = (
        json.dumps(value, ensure_ascii=False, indent=2)
        if json_output
        else format_human(title, value)
    )
    print(text)
    return text


def tree_stats(path: Path) -> dict[str, int]:
    files = 0
    size = 0
    if path.exists():
        for candidate in path.rglob("*"):
            if candidate.is_file():
                files += 1
                size += candidate.stat().st_size
    return {"files": files, "bytes": size}


def cache_report(paths: BuildPaths, store: TaskStore) -> dict[str, object]:
    states = store.target_states()
    return {
        "tracked_targets": len(states),
        "target_state": tree_stats(paths.state),
        "objects": tree_stats(paths.objects),
        "depfiles": {
            "files": sum(1 for path in paths.objects.rglob("*.d")) if paths.objects.exists() else 0,
            "bytes": sum(path.stat().st_size for path in paths.objects.rglob("*.d")) if paths.objects.exists() else 0,
        },
        "temporary": tree_stats(paths.tmp),
        "dependency_cache": tree_stats(paths.deps),
    }


def prune_cache(paths: BuildPaths, store: TaskStore) -> dict[str, object]:
    result: dict[str, object] = store.prune_target_states()
    removed_temporary = 0
    if paths.tmp.exists():
        for child in paths.tmp.iterdir():
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink(missing_ok=True)
            removed_temporary += 1
    result["removed_temporary_entries"] = removed_temporary
    result["cache"] = cache_report(paths, store)
    return result


def explain_target(paths: BuildPaths, graph: BuildGraph, subject: str, task_id: str) -> dict[str, object]:
    target = graph.resolve_target(subject)
    runner = create_runner(paths, graph, task_id)
    try:
        if not target.group:
            return runner.explain(target)
        checks = [
            runner.explain(candidate)
            for candidate in graph.closure((target,))
            if not candidate.group
        ]
        dirty = [check for check in checks if bool(check["will_rebuild"])]
        return {
            "target": target.name,
            "kind": target.kind,
            "will_rebuild": bool(dirty),
            "checked_targets": len(checks),
            "dirty_targets": dirty,
        }
    finally:
        runner.close()


def affected_targets(paths: BuildPaths, graph: BuildGraph, subject: str) -> dict[str, object]:
    path = graph.path(subject)
    roots = {target.name: target for target in graph.related_targets(path)}
    store = TaskStore(paths)
    for name, state in store.target_states().items():
        target = graph.targets.get(name)
        if target is None:
            continue
        for raw in state.get("depfile_dependencies", []):
            if isinstance(raw, str) and graph.path(raw) == path:
                roots[target.name] = target
                break
    if not roots:
        raise GraphError(f"no graph or depfile target references {graph.relative(path)}")
    affected = graph.dependents(roots.values())
    return {
        "file": graph.relative(path),
        "direct_targets": sorted(roots),
        "affected_count": len(affected),
        "affected_targets": [
            {
                "name": target.name,
                "kind": target.kind,
                "outputs": [graph.relative(output) for output in target.outputs],
            }
            for target in affected
        ],
    }


def profile_target(
    paths: BuildPaths,
    graph: BuildGraph,
    target: Target,
    task_id: str,
    label: str,
    graph_seconds: float,
    *,
    json_output: bool,
    verbose: bool,
) -> dict[str, object]:
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id, verbose=verbose)
    roots = build_roots(graph, target)
    if json_output:
        with contextlib.redirect_stdout(io.StringIO()):
            runner.run(roots, label)
    else:
        runner.run(roots, label)
    report = runner.profile_data()
    report["graph_build_seconds"] = round(graph_seconds, 3)
    return report


def run_foreground(
    paths: BuildPaths,
    graph: BuildGraph,
    task_id: str,
    target: Target,
    label: str,
    *,
    verbose: bool,
) -> int:
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id, verbose=verbose)
    runner.run(build_roots(graph, target), label)
    return 0


def run_client(
    paths: BuildPaths,
    store: TaskStore,
    task_id: str,
    command: list[str],
    *,
    json_output: bool,
    verbose: bool,
) -> int:
    if not command or command[0] not in {"run", "gen", "test", "profile"}:
        raise BuildFailure("client accepts run, gen, test, or profile followed by its arguments")
    store.update(task_id, status="queued", queued_at=utc_now(), task=" ".join(command))
    log = store.log_path(task_id)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8", newline="\n") as handle:
        detail = " (verbose)" if verbose else ""
        handle.write(f"Queued background task {task_id}{detail}: {' '.join(command)}\n")
    with open(os.devnull, "w", encoding="utf-8") as sink:
        worker_command = [*command]
        if verbose:
            worker_command.append("--verbose")
        if json_output:
            worker_command.append("--json")
        subprocess.Popen(
            [PYTHON, "build.py", "--worker", "--task-id", task_id, *worker_command],
            cwd=ROOT,
            stdin=subprocess.DEVNULL,
            stdout=sink,
            stderr=sink,
            start_new_session=True,
        )
    if json_output:
        emit_data(
            {"task_id": task_id, "status": "queued", "command": command},
            json_output=True,
            title="Background task",
        )
    else:
        suffix = " (verbose)" if verbose else ""
        print(f"build: task \"{' '.join(command)}\" start. ID:{task_id}{suffix}")
    return 0


def parser() -> argparse.ArgumentParser:
    argument_parser = argparse.ArgumentParser(add_help=False, prog="build.py")
    argument_parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    argument_parser.add_argument("--task-id", help=argparse.SUPPRESS)
    argument_parser.add_argument("--json", dest="json_output", action="store_true", help="emit machine-readable JSON")
    argument_parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="show full build graph, cache, command, process, and action diagnostics",
    )
    commands = argument_parser.add_subparsers(dest="command")

    def add_config_options(command: argparse.ArgumentParser) -> None:
        command.add_argument(
            "--profile", dest="config_profile",
            help="use configs/profiles/<name>.conf without changing the active configuration",
        )
        command.add_argument(
            "--set", dest="config_overrides", metavar="CONFIG_KEY=VALUE",
            action="append", default=[],
            help="apply a non-persistent configuration override",
        )

    commands.add_parser("help")
    commands.add_parser("tui")
    run = commands.add_parser("run")
    run.add_argument("task")
    add_config_options(run)
    info = commands.add_parser("info")
    info.add_argument("subject")
    add_config_options(info)
    why = commands.add_parser("why")
    why.add_argument("subject")
    add_config_options(why)
    affected = commands.add_parser("affected")
    affected.add_argument("file")
    profile = commands.add_parser("profile")
    profile.add_argument("task")
    add_config_options(profile)
    cache = commands.add_parser("cache")
    cache.add_argument("action", choices=("stats", "prune"))
    commands.add_parser("settings")
    commands.add_parser("map")
    generate = commands.add_parser("gen")
    generate.add_argument("file")
    add_config_options(generate)
    test = commands.add_parser("test")
    test.add_argument("item", choices=("license-server", "los2w", "component-config",
                                       "qmp-terminal", "qmp-pleditor", "qmp-tcc", "qmp-fastfetch",
                                       "qmp-dynlinkerror", "qmp-cmd", "qmp-stardust", "all"))
    add_config_options(test)
    config = commands.add_parser("config")
    config.add_argument("action", choices=("list", "save", "load", "reset", "import", "export"))
    config.add_argument("name", nargs="?")
    config.add_argument("path", nargs="?")
    client = commands.add_parser("client")
    client.add_argument("args", nargs=argparse.REMAINDER)
    status = commands.add_parser("status")
    status.add_argument("task_id")
    log = commands.add_parser("log")
    log.add_argument("task_id")
    return argument_parser


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    values = list(argv if argv is not None else sys.argv[1:])
    json_output = "--json" in values
    verbose = any(value in {"-v", "--verbose"} for value in values)
    values = [value for value in values if value not in {"--json", "-v", "--verbose"}]
    if json_output:
        values.insert(0, "--json")
    if verbose:
        values.insert(0, "--verbose")
    return parser().parse_args(values)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    if arguments.command is None:
        arguments.command = "help"
    paths = BuildPaths(ROOT)
    paths.ensure()
    store = TaskStore(paths)
    task_id = arguments.task_id if arguments.worker and arguments.task_id else store.new_id(list(argv or sys.argv[1:]))
    if arguments.worker and not arguments.task_id:
        raise BuildFailure("background worker requires a task ID")
    try:
        if arguments.command == "help":
            text = display_help()
            if arguments.json_output:
                text = emit_data({"help": text.rstrip()}, json_output=True, title="Help")
            else:
                print(text, end="")
            complete_simple(store, task_id, "help", text)
            return 0
        if arguments.command == "client":
            return run_client(
                paths,
                store,
                task_id,
                arguments.args,
                json_output=arguments.json_output,
                verbose=arguments.verbose,
            )
        if arguments.command == "cache":
            data = cache_report(paths, store) if arguments.action == "stats" else prune_cache(paths, store)
            text = emit_data(data, json_output=arguments.json_output, title=f"Cache {arguments.action}")
            complete_simple(store, task_id, f"cache {arguments.action}", text)
            return 0
        if arguments.command == "status":
            record = store.read(arguments.task_id)
            text = emit_data(record, json_output=arguments.json_output, title="Task status")
            complete_simple(store, task_id, f"status {arguments.task_id}", text)
            return 0
        if arguments.command == "log":
            record = store.read(arguments.task_id)
            log_path = root_path(record["log"])
            if not log_path.exists():
                raise BuildFailure(f"log is not available for task {arguments.task_id}")
            complete_simple(store, task_id, f"log {arguments.task_id}", f"Opening {log_path}")
            return subprocess.run(["vim", "-R", str(log_path)], check=False).returncode
        if arguments.command == "settings":
            require_linux()
            edit_settings(paths)
            complete_simple(store, task_id, "settings", "settings closed")
            return 0
        if arguments.command == "config":
            result = handle_config_command(paths, arguments)
            complete_simple(store, task_id, f"config {arguments.action}", "configuration updated")
            return result
        if arguments.command == "tui":
            require_linux()
            from buildsystem.core.tui import run_tui

            result = run_tui(paths, lambda: build_graph(paths))
            complete_simple(store, task_id, "tui", "tui closed", success=result == 0)
            return result
        config_path = resolve_build_config(
            paths,
            task_id,
            getattr(arguments, "config_profile", None),
            getattr(arguments, "config_overrides", []),
            interactive=(
                arguments.command == "run"
                and getattr(arguments, "task", None) == "menuconfig"
            ),
        )
        graph_started = time.perf_counter()
        graph = build_graph(paths, config_path)
        graph_seconds = time.perf_counter() - graph_started
        effective_verbose = arguments.verbose or config_bool(
            parse_kconfig(config_path), "CONFIG_BUILD_VERBOSE_LOG"
        )
        if arguments.command == "run":
            return run_foreground(
                paths,
                graph,
                task_id,
                graph.resolve_target(arguments.task),
                f"run {arguments.task}",
                verbose=effective_verbose,
            )
        if arguments.command == "gen":
            return run_foreground(
                paths,
                graph,
                task_id,
                graph.resolve_target(arguments.file),
                f"gen {arguments.file}",
                verbose=effective_verbose,
            )
        if arguments.command == "test":
            target = graph.resolve_target(f"test-{arguments.item}")
            return run_foreground(
                paths,
                graph,
                task_id,
                target,
                f"test {arguments.item}",
                verbose=effective_verbose,
            )
        if arguments.command == "profile":
            target = graph.resolve_target(arguments.task)
            data = profile_target(
                paths,
                graph,
                target,
                task_id,
                f"profile {arguments.task}",
                graph_seconds,
                json_output=arguments.json_output,
                verbose=effective_verbose,
            )
            store.update(task_id, profile=data)
            emit_data(data, json_output=arguments.json_output, title="Build profile")
            return 0
        if arguments.command == "why":
            data = explain_target(paths, graph, arguments.subject, task_id)
            text = emit_data(data, json_output=arguments.json_output, title="Rebuild explanation")
            complete_simple(store, task_id, f"why {arguments.subject}", text)
            return 0
        if arguments.command == "affected":
            data = affected_targets(paths, graph, arguments.file)
            text = emit_data(data, json_output=arguments.json_output, title="Affected targets")
            complete_simple(store, task_id, f"affected {arguments.file}", text)
            return 0
        if arguments.command == "info":
            target = graph.resolve_target(arguments.subject)
            data = {
                "name": target.name,
                "kind": target.kind,
                "outputs": [graph.relative(path) for path in target.outputs],
                "inputs": [graph.relative(path) for path in target.all_inputs()],
                "depends_on": list(target.depends_on),
            }
            text = emit_data(data, json_output=arguments.json_output, title="Target information")
            complete_simple(store, task_id, f"info {arguments.subject}", text)
            return 0
        if arguments.command == "map":
            require_linux()
            show_map(graph)
            complete_simple(store, task_id, "map", "dependency map closed")
            return 0
        raise BuildFailure(f"unsupported command: {arguments.command}")
    except (BuildFailure, GraphError, FileNotFoundError, ValueError) as exc:
        message = f"build: {exc}"
        print(f"{RED}{message}{RESET}", file=sys.stderr)
        try:
            record = store.read(task_id)
            if record.get("status") not in {"done", "failed"}:
                complete_simple(store, task_id, str(arguments.command), message, success=False)
        except (FileNotFoundError, ValueError):
            pass
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
