#!/usr/bin/env python3
"""LeonOS BuildSystem: the only supported build entry point."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import platform
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


ROOT = Path(__file__).resolve().parent
PYTHON = sys.executable

SYSTEM_APPS = [
    "init", "desktop", "oobe", "login", "serviced", "installer", "shell", "fileman",
    "taskmgr", "settings", "terminal", "run", "osver", "netctl", "servicemgr",
    "diskmgr", "devmgr", "drvmgr", "sysconfdialog", "apiapp",
]
PROGRAM_APPS = [
    "hello", "uidemo", "cjktest", "notepad", "calc", "minesweeper", "memtest",
    "bugtest", "ping", "httpget", "downloadmgr", "browser", "imageview", "wavplay",
    "oshlp", "helloworld", "doomlauncher", "doom",
]
NORMAL_USER_APPS = [app for app in SYSTEM_APPS if app != "installer"] + PROGRAM_APPS
INSTALLER_USER_APPS = ["desktop", "installer"]
INSTALLER_POLICY_APPS = ["desktop", "oobe", "settings"]
USER_APPS = NORMAL_USER_APPS + [app for app in INSTALLER_USER_APPS if app not in NORMAL_USER_APPS]
DRIVER_MODULES = ["mouse", "serial", "e1000", "ac97"]
BUILD_NUMBER_EXEMPT_TARGETS = frozenset({
    "clean",
    "config-sync",
    "menuconfig",
    "test-license-server",
    "test-los2w",
})
WINDOW_BUTTON_ICONS = [
    "window-button-minimize.bmp",
    "window-button-maximize.bmp",
    "window-button-restore.bmp",
    "window-button-close.bmp",
]
SYSTEM_FILES = [
    ("system/resources/mouse.bmp", "system/resources/mouse.bmp"),
    ("system/resources/wallpaper-metro.bmp", "system/resources/wallpaper-metro.bmp"),
    ("system/certs/cacert.pem", "system/certs/cacert.pem"),
    ("third_party/doomgeneric/FREEDOOM-COPYING.txt", "system/docs/FREEDOOM-COPYING.txt"),
]


def runtime_app_relative(app: str, extension: str) -> Path:
    root = "system/apps" if app in SYSTEM_APPS else "programs"
    return Path(root) / app / f"{app}.{extension}"

KCONFIG_DEFAULTS = {
    "CONFIG_VMDK_REQUIRE_LICENSE": "y",
    "CONFIG_INSTALLER_INSTALLED_REQUIRE_LICENSE": "y",
    "CONFIG_IMAGE_SIZE_MIB": "96",
    "CONFIG_INSTALLER_ROOT_SIZE_MIB": "128",
    "CONFIG_QEMU_MEMORY_MB": "512",
    "CONFIG_QEMU_DISPLAY_WIDTH": "1920",
    "CONFIG_QEMU_DISPLAY_HEIGHT": "1080",
    "CONFIG_QEMU_ENABLE_KVM": "y",
    "CONFIG_QEMU_NET_DEVICE": '"e1000"',
    "CONFIG_QEMU_OVMF_PATH": '"/usr/share/ovmf/OVMF.fd"',
    "CONFIG_LICENSE_SERVER_URL": '"http://127.0.0.1:30301"',
    "CONFIG_LICENSE_DEBUG_LOG": "y",
}

MBEDTLS_SOURCES = [
    "aes.c", "asn1parse.c", "asn1write.c", "base64.c", "bignum.c", "cipher.c",
    "cipher_wrap.c", "constant_time.c", "ctr_drbg.c", "ecdh.c", "ecdsa.c", "ecp.c",
    "ecp_curves.c", "entropy.c", "gcm.c", "md.c", "oid.c", "pem.c", "pk.c",
    "pkparse.c", "pk_wrap.c", "platform.c", "platform_util.c", "rsa.c",
    "rsa_internal.c", "sha1.c", "sha256.c", "sha512.c", "ssl_ciphersuites.c",
    "ssl_cli.c", "ssl_msg.c", "ssl_tls.c", "x509.c", "x509_crt.c",
]

_COLLECT_CACHE: dict[tuple[str, ...], tuple[Path, ...]] = {}


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


def collect(*patterns: str) -> list[Path]:
    cached = _COLLECT_CACHE.get(patterns)
    if cached is not None:
        return list(cached)
    result: list[Path] = []
    for pattern in patterns:
        result.extend(ROOT.glob(pattern))
    paths = tuple(sorted(path for path in result if path.is_file()))
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


def parse_kconfig(path: Path) -> dict[str, str]:
    values = dict(KCONFIG_DEFAULTS)
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2 : -len(" is not set")]
            if key in values:
                values[key] = "n"
        elif "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            if key.strip() in values:
                values[key.strip()] = value.strip()
    return values


def config_int(values: dict[str, str], key: str) -> int:
    try:
        return int(values[key], 10)
    except (KeyError, ValueError):
        return int(KCONFIG_DEFAULTS[key], 10)


def config_bool(values: dict[str, str], key: str) -> bool:
    return values.get(key, KCONFIG_DEFAULTS[key]) == "y"


def config_string(values: dict[str, str], key: str) -> str:
    value = values.get(key, KCONFIG_DEFAULTS[key]).strip()
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
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

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
        command.append("-enable-kvm")
    command += ["-cpu", "host", "-machine", "q35", "-m", f"{memory}M"]
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


def build_graph(paths: BuildPaths) -> BuildGraph:
    graph = BuildGraph(ROOT)
    values = parse_kconfig(paths.kconfig)
    cc = os.environ.get("CC", "clang")
    rustc = os.environ.get("RUSTC", "rustc")
    ar = os.environ.get("AR", "llvm-ar")
    ld = os.environ.get("LD", "ld.lld")
    generated = paths.generated_include
    autoconf = generated / "autoconf.h"
    installer_autoconf = generated / "autoconf-installer.h"
    rustcfg = generated / "rustcfg.args"
    build_info = ROOT / "include/generated/build_info.h"
    loader_integrity = generated / "loader_integrity.h"
    grub_efi_dir = paths.deps / "grub-efi-amd64-bin/usr/lib/grub/x86_64-efi"
    system_grub_efi_dir = Path("/usr/lib/grub/x86_64-efi")
    using_system_grub = False
    if not (grub_efi_dir / "modinfo.sh").exists() and (system_grub_efi_dir / "modinfo.sh").exists():
        grub_efi_dir = system_grub_efi_dir
        using_system_grub = True

    def sync_config(context: ActionContext) -> None:
        context.run(
            (
                PYTHON, "tools/kconfig_sync.py", "--config", relative(paths.kconfig),
                "--defaults", "configs/default.conf", "--out-dir", relative(generated),
            ),
            announce=True,
        )

    graph.add(
        Target(
            name="config-sync",
            outputs=(paths.kconfig, autoconf, installer_autoconf, rustcfg),
            inputs=(ROOT / "configs/default.conf", ROOT / "Kconfig", ROOT / "tools/kconfig_sync.py"),
            kind="generate",
            action=sync_config,
            action_key="kconfig-sync-v2",
            always=True,
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

    cflags_kernel = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-mcmodel=kernel", "-Wall", "-Wextra", "-Ikernel/ntclks/include",
        "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    asflags_kernel = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Ikernel/ntclks/include", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_loader = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-Wall", "-Wextra", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    asflags_loader = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_user_base = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Iuserland/libc/include",
        "-Iinclude", f"-I{relative(paths.out / 'include')}", "-Ithird_party/mbedtls/include",
        '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
    ]
    cflags_user = cflags_user_base + ["-include", relative(autoconf)]
    cflags_doom = cflags_user + ["-Ithird_party/doomgeneric/doomgeneric"]
    cflags_installer = cflags_user_base + ["-include", relative(installer_autoconf)]
    asflags_user = [
        cc, "-target", "x86_64-unknown-none", "-O2", "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Iuserland/libc/include", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]

    loader_sources = collect("boot/loader/**/*.c", "boot/loader/**/*.S")
    kernel_sources = collect("kernel/ntclks/**/*.c", "kernel/ntclks/**/*.S", "drivers/bootstrap/**/*.c", "drivers/bootstrap/**/*.S")
    rust_sources = collect("middlelayer/osmlayer/src/**/*.rs")
    kernel_objects: list[Path] = []
    for source in kernel_sources:
        implicit: list[Path] = [autoconf]
        if source == ROOT / "kernel/ntclks/version.c":
            implicit += [build_info] + [candidate for candidate in kernel_sources + rust_sources if candidate != source]
        flags = asflags_kernel if source.suffix == ".S" else cflags_kernel + (["-include", relative(autoconf)] if source.suffix == ".c" else [])
        kernel_objects.append(add_compile(graph, paths, f"compile:kernel:{relative(source)}", source, "kernel", flags, implicit, kind="assemble" if source.suffix == ".S" else "compile"))

    kernel_sys = paths.out / "system/kernel.sys"
    graph.add(
        Target(
            name="kernel-image",
            outputs=(kernel_sys,),
            inputs=tuple(kernel_objects),
            implicit_inputs=(ROOT / "kernel/ntclks/arch/x86_64/linker.ld",),
            kind="link",
            command=(ld, "-nostdlib", "-z", "max-page-size=0x1000", "-T", "kernel/ntclks/arch/x86_64/linker.ld", "-o", relative(kernel_sys), *map(relative, kernel_objects)),
        )
    )
    graph.add(Target(name="kernel", depends_on=("kernel-image",), group=True, kind="aggregate"))

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
        loader_objects.append(add_compile(graph, paths, f"compile:loader:{relative(source)}", source, "loader", flags, (loader_integrity,), kind="assemble" if source.suffix == ".S" else "compile"))
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

    libc_sources = collect("userland/libc/src/*.c", "userland/libc/src/*.S")
    libc_sources += [ROOT / "third_party/mbedtls/library" / source for source in MBEDTLS_SOURCES]
    libc_objects: list[Path] = []
    installer_libc_objects: list[Path] = []
    for source in sorted(libc_sources):
        is_asm = source.suffix == ".S"
        libc_objects.append(add_compile(graph, paths, f"compile:libc:{relative(source)}", source, "userlib", asflags_user if is_asm else cflags_user, (autoconf,) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        installer_libc_objects.append(add_compile(graph, paths, f"compile:installer-libc:{relative(source)}", source, "userlib-installer-policy", asflags_user if is_asm else cflags_installer, (installer_autoconf,) if not is_asm else (), kind="assemble" if is_asm else "compile"))
    libc_a = paths.out / "userland/libc.a"
    installer_libc_a = paths.out / "userland-installer-policy/libc.a"
    graph.add(Target(name="archive:libc", outputs=(libc_a,), inputs=tuple(libc_objects), kind="link", command=(ar, "rcs", relative(libc_a), *map(relative, libc_objects))))
    graph.add(Target(name="archive:installer-libc", outputs=(installer_libc_a,), inputs=tuple(installer_libc_objects), kind="link", command=(ar, "rcs", relative(installer_libc_a), *map(relative, installer_libc_objects))))

    app_elfs: dict[str, Path] = {}
    user_targets: list[str] = ["archive:libc"]
    for app in USER_APPS:
        objects: list[Path] = []
        for source in user_app_sources(app):
            is_asm = source.suffix == ".S"
            objects.append(add_compile(graph, paths, f"compile:app:{app}:{relative(source)}", source, f"user-{app}", asflags_user if is_asm else (cflags_doom if app == "doom" else cflags_user), (autoconf,) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland/{app}.elf"
        graph.add(Target(name=f"app:{app}", outputs=(output,), inputs=tuple([*objects, libc_a]), implicit_inputs=(ROOT / "userland/linker.ld",), kind="link", command=(ld, "-nostdlib", "--gc-sections", "-z", "max-page-size=0x1000", "-T", "userland/linker.ld", "-o", relative(output), *map(relative, objects), relative(libc_a))))
        app_elfs[app] = output
        user_targets.append(f"app:{app}")

    installer_policy_elfs: dict[str, Path] = {}
    user_targets.append("archive:installer-libc")
    for app in INSTALLER_POLICY_APPS:
        objects = []
        for source in user_app_sources(app):
            is_asm = source.suffix == ".S"
            objects.append(add_compile(graph, paths, f"compile:installer-app:{app}:{relative(source)}", source, f"user-installer-policy-{app}", asflags_user if is_asm else cflags_installer, (installer_autoconf,) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland-installer-policy/{app}.elf"
        name = f"installer-policy:{app}"
        graph.add(Target(name=name, outputs=(output,), inputs=tuple([*objects, installer_libc_a]), implicit_inputs=(ROOT / "userland/linker.ld",), kind="link", command=(ld, "-nostdlib", "--gc-sections", "-z", "max-page-size=0x1000", "-T", "userland/linker.ld", "-o", relative(output), *map(relative, objects), relative(installer_libc_a))))
        installer_policy_elfs[app] = output
        user_targets.append(name)

    app_icons = tuple(paths.out / f"generated/app-icons/{app}.bmp" for app in USER_APPS)
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
    graph.add(Target(name="app-icons", outputs=app_icons, inputs=(ROOT / "tools/make_app_icons.py",), kind="generate", command=(PYTHON, "tools/make_app_icons.py", "--out-dir", relative(paths.out / "generated/app-icons"), "--apps", *USER_APPS)))
    button_icons = tuple(paths.out / f"generated/window-buttons/{name}" for name in WINDOW_BUTTON_ICONS)
    graph.add(Target(name="window-button-icons", outputs=button_icons, inputs=(ROOT / "tools/make_window_button_icons.py",), kind="generate", command=(PYTHON, "tools/make_window_button_icons.py", "--out-dir", relative(paths.out / "generated/window-buttons"))))
    user_targets += ["app-icons", "window-button-icons", "ui-font", "browser-font"]
    graph.add(Target(name="userland", depends_on=tuple(user_targets), group=True, kind="aggregate"))

    grub_efi = paths.staging / "EFI/BOOT/BOOTX64.EFI"
    # 如果是系统 GRUB 路径，使用绝对路径；否则使用相对路径
    grub_dir_arg = str(grub_efi_dir) if using_system_grub else relative(grub_efi_dir)
    graph.add(Target(name="grub-efi", outputs=(grub_efi,), inputs=(ROOT / "boot/grub/embedded.cfg", grub_efi_dir / "modinfo.sh"), kind="generate", command=("grub-mkstandalone", "-d", grub_dir_arg, "-O", "x86_64-efi", "-o", relative(grub_efi), "--modules=part_gpt fat multiboot2 normal search search_fs_file configfile echo serial terminal video video_bochs video_cirrus efi_gop efi_uga all_video gfxterm", "boot/grub/grub.cfg=boot/grub/embedded.cfg")))
    esp_names = ["grub-efi"]
    esp_outputs: list[Path] = [grub_efi]
    for source, destination_rel in [(ROOT / "boot/grub/grub.cfg", "boot/grub/grub.cfg"), (loader_elf, "boot/loader.elf"), (kernel_sys, "system/kernel.sys"), (middle_sys, "system/middlelayer.sys")]:
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    manifest = paths.staging / "system/osmlayer.manifest"
    graph.add(Target(name="esp:manifest", outputs=(manifest,), kind="generate", action=text_action(manifest, "name=osmlayer\nabi=1\nroot=0:/\nfs=fat32\ngui=desktop.elf\n"), action_key="manifest-v1"))
    esp_names.append("esp:manifest")
    esp_outputs.append(manifest)
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
            shutil.rmtree(paths.staging / legacy_dir, ignore_errors=True)
        for legacy_name in ("system.psf", "cjk16.lbf", "metro-latin.lbf", "leonos.lbf", "leonos-ui.ttf"):
            (ui_metro_font_destination.parent / legacy_name).unlink(missing_ok=True)
        ui_metro_font_destination.parent.mkdir(parents=True, exist_ok=True)
        (paths.staging / "system/state").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ui_metro_font, ui_metro_font_destination)
        shutil.copyfile(ui_win95_font, ui_win95_font_destination)
        shutil.copyfile(browser_font, browser_font_destination)
        shutil.copyfile(browser_cjk_font_source, browser_cjk_font_destination)

    target = graph.add(Target(name="esp:system-font", outputs=(ui_metro_font_destination, ui_win95_font_destination,
                                                                 browser_font_destination, browser_cjk_font_destination),
                              inputs=(ui_metro_font, ui_win95_font, browser_font, browser_cjk_font_source), kind="generate", action=sync_ui_font,
                              action_key="sync-ui-font-v7"))
    esp_names.append(target.name)
    esp_outputs.extend((ui_metro_font_destination, ui_win95_font_destination,
                        browser_font_destination, browser_cjk_font_destination))
    for source, icon in zip(button_icons, WINDOW_BUTTON_ICONS):
        destination = paths.staging / "system/resources" / icon
        target = add_copy(graph, f"esp:window-icon:{icon}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for driver, source in zip(DRIVER_MODULES, driver_outputs):
        destination = paths.staging / f"drivers/{driver}.drv"
        target = add_copy(graph, f"esp:driver:{driver}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for app in NORMAL_USER_APPS:
        destination = paths.staging / runtime_app_relative(app, "elf")
        target = add_copy(graph, f"esp:app:{app}", app_elfs[app], destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    doom_wad = ROOT / "third_party/doomgeneric/freedoom1.wad"
    if doom_wad.exists():
        doom_wad_destination = paths.staging / "programs/doom/freedoom1.wad"
        target = add_copy(graph, "esp:doom:freedoom1-wad", doom_wad, doom_wad_destination)
        esp_names.append(target.name)
        esp_outputs.append(doom_wad_destination)
    for app in NORMAL_USER_APPS:
        source = paths.out / f"generated/app-icons/{app}.bmp"
        destination = paths.staging / runtime_app_relative(app, "bmp")
        target = add_copy(graph, f"esp:icon:{app}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    helloworld_api = paths.out / "api/helloworld.api"
    api_destination = paths.staging / "api/helloworld.api"
    graph.add(Target(
        name="esp:api:helloworld",
        outputs=(helloworld_api,),
        inputs=(app_elfs["helloworld"], ROOT / "tools/build_api.py"),
        kind="generate",
        command=(PYTHON, "tools/build_api.py", relative(app_elfs["helloworld"]), relative(helloworld_api)),
    ))
    target = add_copy(graph, "esp:api:helloworld-copy", helloworld_api, api_destination)
    esp_names.append(target.name)
    esp_outputs.append(api_destination)
    config_destination = paths.staging / "system/config/leonos.conf"
    target = add_copy(graph, "esp:config", paths.kconfig, config_destination)
    esp_names.append(target.name)
    esp_outputs.append(config_destination)
    for source in collect("system/config/*"):
        destination = paths.staging / "system/config" / source.name
        target = add_copy(graph, f"esp:config:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for source in collect("system/docs/*.hlp"):
        destination = paths.staging / "docs" / source.name
        target = add_copy(graph, f"esp:doc:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    graph.add(Target(name="esp", depends_on=tuple(esp_names), group=True, kind="aggregate"))

    vmdk = paths.images / "leonos4.vmdk"
    raw = paths.images / "leonos4.raw"
    esp_fat = paths.images / "esp.fat"
    graph.add(Target(name="image-vmdk", outputs=(vmdk, raw, esp_fat), inputs=tuple([*esp_outputs, ROOT / "tools/make_image.py"]), kind="generate", command=(PYTHON, "tools/make_image.py", "--out", relative(vmdk), "--raw", relative(raw), "--esp-tree", relative(paths.staging), "--size-mib", str(config_int(values, "CONFIG_IMAGE_SIZE_MIB")))))

    iso = paths.images / "leonos4.iso"
    iso_stage = paths.out / "iso"

    def make_iso(context: ActionContext) -> None:
        if iso_stage.exists():
            shutil.rmtree(iso_stage)
        shutil.copytree(paths.staging, iso_stage)
        context.run(("grub-mkrescue", "-o", relative(iso), relative(iso_stage)), announce=True)

    graph.add(Target(name="image-iso", outputs=(iso,), inputs=tuple([*esp_outputs, ROOT / "boot/grub/grub.cfg"]), kind="generate", action=make_iso, action_key="iso-stage-v1"))

    installer_root = paths.out / "install/root.fat"
    installer_stage = paths.out / "install/root"
    graph.add(Target(name="installer-root", outputs=(installer_root,), inputs=tuple([*esp_outputs, app_elfs["desktop"], app_elfs["installer"], *(installer_policy_elfs.values()), ROOT / "tools/make_installer_root.py"]), kind="generate", command=(PYTHON, "tools/make_installer_root.py", "--out", relative(installer_root), "--stage", relative(installer_stage), "--esp-tree", relative(paths.staging), "--installed-policy-dir", relative(paths.out / "userland-installer-policy"), "--userland-dir", relative(paths.out / "userland"), "--generated-icons-dir", relative(paths.out / "generated/app-icons"), "--manifest", relative(manifest), "--size-mib", str(config_int(values, "CONFIG_INSTALLER_ROOT_SIZE_MIB")))))
    installer_iso = paths.images / "leonos4-installer.iso"
    installer_boot_image = paths.out / "install/installer-efiboot.img"
    graph.add(Target(name="installer-image", outputs=(installer_iso, installer_boot_image), inputs=(loader_elf, kernel_sys, middle_sys, installer_root, grub_efi_dir / "modinfo.sh", ROOT / "boot/grub/installer.cfg", ROOT / "boot/grub/installer_embedded.cfg", ROOT / "tools/make_installer_iso.py"), kind="generate", command=(PYTHON, "tools/make_installer_iso.py", "--out", relative(installer_iso), "--stage", relative(paths.out / "installer-iso"), "--boot-image", relative(installer_boot_image), "--loader", relative(loader_elf), "--kernel", relative(kernel_sys), "--middlelayer", relative(middle_sys), "--installer-root", relative(installer_root), "--work-dir", relative(paths.out / "install"), "--grub-efi-dir", grub_dir_arg)))

    graph.add(Target(name="all", depends_on=("config-sync", "build-info", "loader", "kernel", "drivers", "middlelayer", "userland", "esp"), group=True, kind="aggregate"))
    graph.add(Target(name="run", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values)))
    graph.add(Target(name="run-debug", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values, debug=True)))
    graph.add(Target(name="run-iso", inputs=(vmdk, iso), depends_on=("image-vmdk", "image-iso"), kind="command", command=qemu_command(paths, values, debug=True, iso=True)))
    graph.add(Target(name="installer", depends_on=("all", "installer-root", "installer-image"), group=True, kind="aggregate"))

    def menuconfig(context: ActionContext) -> None:
        paths.config.mkdir(parents=True, exist_ok=True)
        if not paths.kconfig.exists():
            shutil.copy2(ROOT / "configs/default.conf", paths.kconfig)
        context.run(
            ("kconfig-mconf", "Kconfig"),
            environment={"KCONFIG_CONFIG": str(paths.kconfig)},
            announce=True,
            interactive=True,
        )
        context.run((PYTHON, "tools/kconfig_sync.py", "--config", relative(paths.kconfig), "--defaults", "configs/default.conf", "--out-dir", relative(generated)), announce=True)

    graph.add(Target(name="menuconfig", inputs=(ROOT / "Kconfig", ROOT / "configs/default.conf", ROOT / "tools/kconfig_sync.py"), kind="command", action=menuconfig, action_key="menuconfig-v3", always=True))

    def clean(context: ActionContext) -> None:
        for directory in (paths.out, paths.legacy_out, paths.target_state, paths.tmp):
            if directory.exists():
                shutil.rmtree(directory)
        context.runner.store.clear_target_states()
        paths.ensure()

    graph.add(Target(name="clean", kind="command", action=clean, action_key="clean-v1", always=True))

    graph.add(Target(name="test-license-server", inputs=(ROOT / "tools/test_license_server.py", ROOT / "tools/license_server.py"), kind="command", command=(PYTHON, "tools/test_license_server.py")))
    graph.add(Target(name="test-los2w", inputs=tuple(collect("los2w/*.py")), kind="command", command=(PYTHON, "-c", "from los2w.selftest import run_self_tests; print('\\n'.join(run_self_tests()))")))

    def qmp_test(context: ActionContext) -> None:
        socket = Path(tempfile.gettempdir()) / f"leonos4-qmp-{context.runner.task_id}.sock"
        socket.unlink(missing_ok=True)
        command = list(qemu_command(paths, values, debug=True))
        command += ["-qmp", f"unix:{socket},server=on,wait=off"]
        context.runner.logger.command(context.worker_id, command)
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        try:
            context.run((PYTHON, "tools/qmp_terminal_smoke.py", str(socket)), announce=True)
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

    graph.add(Target(name="test-qmp-terminal", inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"), depends_on=("image-vmdk",), kind="command", action=qmp_test, action_key="qmp-terminal-v2"))
    graph.add(Target(name="test-all", depends_on=("test-license-server", "test-los2w", "test-qmp-terminal"), group=True, kind="aggregate"))
    return graph


def require_linux() -> None:
    if platform.system() != "Linux":
        raise BuildFailure("LeonOS BuildSystem only supports Linux or WSL; run python3 build.py inside WSL")


def require_tools(names: Iterable[str]) -> None:
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise BuildFailure("missing required tools: " + ", ".join(missing))


def require_grub_efi_modules(paths: BuildPaths, task: str) -> None:
    if task not in {"esp", "all", "image-vmdk", "image-iso", "installer", "run", "run-debug", "run-iso"}:
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
    userland = (*compiler, "llvm-ar")
    esp = (*userland, "rustc", "grub-mkstandalone")
    vmdk = (*esp, "truncate", "sgdisk", "mkfs.fat", "mcopy", "dd", "qemu-img")
    iso = (*esp, "grub-mkrescue", "xorriso")
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
    if task in {"run", "run-debug"}:
        return (*vmdk, "qemu-system-x86_64")
    if task == "run-iso":
        return (*vmdk, "grub-mkrescue", "xorriso", "qemu-system-x86_64")
    if task == "menuconfig":
        return ("kconfig-mconf",)
    if task in {"test-qmp-terminal", "test-all"}:
        return (*vmdk, "qemu-system-x86_64")
    return ()


def create_runner(paths: BuildPaths, graph: BuildGraph, task_id: str) -> BuildRunner:
    return BuildRunner(graph, paths, load_settings(paths), task_id)


def build_roots(graph: BuildGraph, target: Target) -> tuple[Target, ...]:
    if target.name in BUILD_NUMBER_EXEMPT_TARGETS or target.name == "build-info":
        return (target,)
    return (graph.targets["build-info"], target)


def display_help() -> str:
    return """LeonOS BuildSystem\n\nCommands:\n  build.py help\n  build.py run <task>\n  build.py info <file-or-task>\n  build.py why <file-or-task>\n  build.py affected <file>\n  build.py profile <task>\n  build.py cache <stats|prune>\n  build.py settings\n  build.py map\n  build.py gen <file>\n  build.py test <license-server|los2w|qmp-terminal|all>\n  build.py client <run|gen|test|profile> ...\n  build.py status <task-id>\n  build.py log <task-id>\n\nTasks:\n  all, config-sync, build-info, loader, kernel, drivers, middlelayer, userland, esp, image-vmdk, image-iso, installer, run, run-debug, run-iso, menuconfig, clean\n"""


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
) -> dict[str, object]:
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id)
    roots = build_roots(graph, target)
    if json_output:
        with contextlib.redirect_stdout(io.StringIO()):
            runner.run(roots, label)
    else:
        runner.run(roots, label)
    report = runner.profile_data()
    report["graph_build_seconds"] = round(graph_seconds, 3)
    return report


def run_foreground(paths: BuildPaths, graph: BuildGraph, task_id: str, target: Target, label: str) -> int:
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id)
    runner.run(build_roots(graph, target), label)
    return 0


def run_client(
    paths: BuildPaths,
    store: TaskStore,
    task_id: str,
    command: list[str],
    *,
    json_output: bool,
) -> int:
    if not command or command[0] not in {"run", "gen", "test", "profile"}:
        raise BuildFailure("client accepts run, gen, test, or profile followed by its arguments")
    store.update(task_id, status="queued", queued_at=utc_now(), task=" ".join(command))
    log = store.log_path(task_id)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"Queued background task {task_id}: {' '.join(command)}\n")
    with open(os.devnull, "w", encoding="utf-8") as sink:
        worker_command = [*command]
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
        print(f"build: task \"{' '.join(command)}\" start. ID:{task_id}")
    return 0


def parser() -> argparse.ArgumentParser:
    argument_parser = argparse.ArgumentParser(add_help=False, prog="build.py")
    argument_parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    argument_parser.add_argument("--task-id", help=argparse.SUPPRESS)
    argument_parser.add_argument("--json", dest="json_output", action="store_true", help="emit machine-readable JSON")
    commands = argument_parser.add_subparsers(dest="command")
    commands.add_parser("help")
    run = commands.add_parser("run")
    run.add_argument("task")
    info = commands.add_parser("info")
    info.add_argument("subject")
    why = commands.add_parser("why")
    why.add_argument("subject")
    affected = commands.add_parser("affected")
    affected.add_argument("file")
    profile = commands.add_parser("profile")
    profile.add_argument("task")
    cache = commands.add_parser("cache")
    cache.add_argument("action", choices=("stats", "prune"))
    commands.add_parser("settings")
    commands.add_parser("map")
    generate = commands.add_parser("gen")
    generate.add_argument("file")
    test = commands.add_parser("test")
    test.add_argument("item", choices=("license-server", "los2w", "qmp-terminal", "all"))
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
    values = [value for value in values if value != "--json"]
    if json_output:
        values.insert(0, "--json")
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
            return run_client(paths, store, task_id, arguments.args, json_output=arguments.json_output)
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
        graph_started = time.perf_counter()
        graph = build_graph(paths)
        graph_seconds = time.perf_counter() - graph_started
        if arguments.command == "run":
            return run_foreground(paths, graph, task_id, graph.resolve_target(arguments.task), f"run {arguments.task}")
        if arguments.command == "gen":
            return run_foreground(paths, graph, task_id, graph.resolve_target(arguments.file), f"gen {arguments.file}")
        if arguments.command == "test":
            target = graph.resolve_target(f"test-{arguments.item}")
            return run_foreground(paths, graph, task_id, target, f"test {arguments.item}")
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
