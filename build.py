#!/usr/bin/env python3
"""LeonOS BuildSystem: the only supported build entry point.

开发者导航（先读这里，再改代码）
================================
``build.py`` 只负责把配置和源码组织成 BuildGraph；真正执行编译、链接和
文件操作的是 ``buildsystem/core`` 中的 runner。新增功能时，请按下面的归属
修改，避免把业务逻辑塞进 ``main``：

* **新增或修改配置项**：先改 ``Kconfig``、``configs/default.conf`` 或
  ``Kconfig.components``；组件开关和依赖写在 ``configs/components.toml``。
  运行 ``python3 build.py run config-sync`` 生成头文件，
  本文件中的 ``parse_kconfig`` 只做读取和预设覆盖，不要在这里硬编码默认值。
* **新增内核、驱动或通用 C/汇编源文件**：把源文件放到对应目录，然后在
  ``build_graph`` 的“源文件收集/compile targets”区域加入 ``collect(...)``、
  ``add_compile(...)`` 和依赖关系；需要导出头文件时，同时增加 staging target。
* **新增用户程序**：优先在 ``configs/components.toml`` 声明 component，再把
  程序放到 ``userland/apps/<name>``。只有特殊链接参数、生成步骤或第三方源码
  才需要在 ``build_graph`` 中增加专用 target；普通 C 程序会由
  ``user_app_sources`` 自动收集。
* **接入第三方库/移植包**：在 ``build_graph`` 中为源码、port 目录、配置文件、
  stamp 和最终 ELF/库建立独立 target，并把所有输入写进 ``inputs`` 或
  ``implicit_inputs``，这样增量构建和 ``why/affected`` 才准确。不要直接在
  ``main`` 里调用 make/cmake。
* **修改启动盘、VMDK/ISO 或发布包**：查找 ``make_iso``、``make_release`` 以及
  它们上游的 staging targets；新增资源先放入 ``SYSTEM_FILES`` 或对应资源列表，
  再用 ``add_copy``/生成 target 描述来源和目标。
* **新增 QEMU 设备或运行参数**：只改 ``qemu_command``，配置来源放在 Kconfig；
  需要可测试的启动流程则在 ``qmp_test``/``qmp_test_suite`` 增加测试 target，
  不要在编译 target 中启动 QEMU。
* **新增构建测试**：在 ``build_graph`` 创建 ``test-*`` target，并把它加入
  ``BUILD_NUMBER_EXEMPT_TARGETS``（若测试不应递增 build number），再在
  ``parser`` 的 ``test.item`` choices 和 ``main`` 的分发逻辑登记命令。
* **新增 CLI 子命令**：在 ``parser`` 注册参数，在 ``main`` 中只做参数校验、
  graph 调用和结果输出；耗时工作放到独立函数。需要后台执行时复用
  ``run_client``/``TaskStore``，不要自行 fork 进程。
* **改变工具链、路径或缓存行为**：路径集中在 ``BuildPaths``；编译/链接命令
  通过 ``add_compile``、``add_link`` 生成；缓存语义由 ``BuildRunner`` 管理。
  修改后务必检查 ``inputs``、depfile 和 ``action_key``，否则可能得到陈旧产物。

代码阅读顺序建议：路径与 glob 工具 -> 配置解析 -> target 辅助函数 ->
``build_graph``（全部构建规则）-> QEMU/测试动作 -> CLI 与 ``main``。
每个 target 都应声明完整输入、输出和依赖；注释解释“为什么”，变量名和函数
名解释“是什么”。
"""

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
from tools import leonos_layout as layout
from tools import musl_link
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
# 配置选择组用于保证互斥选项只有一个生效；新增互斥配置时在这里登记，
# 同时在 Kconfig 使用 choice 定义，避免命令行覆盖后出现不一致状态。
CONFIG_CHOICE_GROUPS = (
    ("CONFIG_VMDK_DEFAULT_LANGUAGE_EN", "CONFIG_VMDK_DEFAULT_LANGUAGE_ZH"),
    ("CONFIG_VMDK_DEFAULT_THEME_METRO", "CONFIG_VMDK_DEFAULT_THEME_WIN95"),
    ("CONFIG_VMDK_WALLPAPER_FILL", "CONFIG_VMDK_WALLPAPER_STRETCH",
     "CONFIG_VMDK_WALLPAPER_CENTER"),
    ("CONFIG_STARTUP_DESKTOP", "CONFIG_STARTUP_TTY"),
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
    "defconfig",
    "menuconfig",
    "test-license-server",
    "test-los2w",
    "test-qmp-terminal",
    "test-terminal-packages",
    "test-python-package",
    "test-qmp-pleditor",
    "test-qmp-tcc",
    "gcc-probe-image",
    "gcc-probe-runner",
    "test-qmp-fastfetch",
    "test-qmp-sl",
    "test-qmp-less",
    "test-qmp-dynlinkerror",
    "test-qmp-cmd",
    "test-qmp-abittest",
    "test-qmp-stardust",
    "test-qmp-glxgears",
    "test-component-config",
    "test-linux-abi-contract",
    "test-linux-memory",
    "test-linux-process-vm",
    "test-linux-sysv-msg",
    "test-linux-sysv-sem",
    "test-linux-pty",
    "test-linux-permissions",
    "test-storage-metadata",
    "test-storage-rename",
    "test-storage-mkdir-mount",
    "test-ext2-cache",
    "test-ext2-performance",
    "test-ext2-write-batch",
    "test-storage-write-batch",
    "test-installer-copy",
    "test-uapi",
    "test-musl-abi",
    "test-musl-distribution",
    "test-linux-rootfs",
    "test-linux-inventory",
    "test-fastfetch-package",
    "test-linux-resources",
    "test-linux-threads",
    "test-linux-socket-batches",
    "test-linux-descriptors",
    "test-linux-ioctl-cloexec",
    "test-svga",
    "test-installer-input",
    "test-installer-setup",
    "test-power",
    "test-init-power",
    "test-oobe",
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
    ("logo.png", "usr/share/leonos/resources/logo.png"),
    ("system/resources/mouse.bmp", "usr/share/leonos/resources/mouse.bmp"),
    ("system/resources/wallpaper-metro.bmp", "usr/share/leonos/resources/wallpaper-metro.bmp"),
    ("system/certs/cacert.pem", "etc/ssl/certs/ca-certificates.crt"),
    ("third_party/doomgeneric/FREEDOOM-COPYING.txt", "usr/share/doc/leonos/FREEDOOM-COPYING.txt"),
    ("third_party/portablegl/LICENSE", "usr/share/doc/leonos/PORTABLEGL-LICENSE"),
]


def runtime_app_relative(app: str, extension: str, system_apps: set[str]) -> Path:
    """返回应用在来宾 rootfs 中的标准安装位置。

    所有 LeonOS 应用包现在共用 /usr/lib/leonos/apps；旧 system/apps 与
    programs 的区分由 manifest 的 system 标志承载，不再编码进路径。
    """
    del system_apps
    return Path(layout.app_exec_path(app, extension))


def find_compiler_rt_archive(cc: str) -> Path | None:
    """Locate Clang's x86_64 compiler-rt builtins archive across layouts."""
    compiler_rt = subprocess.run(
        (cc, "-target", "x86_64-unknown-none", "-rtlib=compiler-rt",
         "--print-libgcc-file-name"),
        check=False, text=True, capture_output=True,
    )
    candidates: list[Path] = []
    queried_path = compiler_rt.stdout.strip()
    if queried_path:
        queried_archive = Path(queried_path)
        candidates.append(queried_archive)
        # Some Debian/Ubuntu Clang versions return a cross-target path while
        # packaging the archive in a sibling ``lib/linux`` directory.
        candidates.append(queried_archive.parent / "linux" / queried_archive.name)

    resource_dir_result = subprocess.run(
        (cc, "-print-resource-dir"), check=False, text=True, capture_output=True,
    )
    resource_dir = resource_dir_result.stdout.strip()
    if resource_dir:
        resource_root = Path(resource_dir) / "lib"
        # Arch/CachyOS and newer Clang packages use this resource-directory
        # layout, with the x86_64 suffix in the archive name.
        candidates.append(resource_root / "linux" / "libclang_rt.builtins-x86_64.a")
        # Keep compatibility with target-specific packages using the generic
        # archive name returned by --print-libgcc-file-name.
        candidates.append(resource_root / "x86_64-unknown-none" / "libclang_rt.builtins.a")
        candidates.append(resource_root / "x86_64-unknown-none" / "linux" / "libclang_rt.builtins.a")

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


MBEDTLS_SOURCES = [
    "aes.c", "asn1parse.c", "asn1write.c", "base64.c", "bignum.c", "cipher.c",
    "cipher_wrap.c", "constant_time.c", "ctr_drbg.c", "ecdh.c", "ecdsa.c", "ecp.c",
    "ecp_curves.c", "entropy.c", "gcm.c", "md.c", "oid.c", "pem.c", "pk.c",
    "pkparse.c", "pk_wrap.c", "pkcs5.c", "platform.c", "platform_util.c", "rsa.c",
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

# ---- 路径与源码收集工具 ----
# 这些函数保证 glob、相对路径和对象目录在整个构建图中保持一致；如果改变
# 仓库目录布局，先修改这里和 object_path，再调整各 target 的模式。

def root_path(value: str | Path) -> Path:
    """把配置/命令行中的相对路径统一解释为仓库根目录下的路径。"""
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
    """收集源码文件并缓存结果；新增源码目录时优先扩展调用方的 glob。"""
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
    """为源文件生成不冲突的对象文件路径，prefix 用于隔离不同 ABI/编译模式。"""
    rel = source.relative_to(ROOT)
    return paths.objects / prefix / rel.with_suffix(rel.suffix + ".o")


def user_app_sources(app: str) -> list[Path]:
    """收集普通用户程序源码；特殊应用的额外源码在此明确列出并过滤。"""
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
    return sorted(set(sources))


def parse_config_values(path: Path) -> dict[str, str]:
    """解析 .conf 的 ``CONFIG_KEY=value`` 和 ``is not set`` 两种写法。"""
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
    """读取默认配置和当前配置，并应用 preset；不要在构建规则中重复解析配置。"""
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
    """读取整数配置并在缺失或非法时使用明确默认值。"""
    try:
        return int(values[key], 10)
    except (KeyError, ValueError):
        return default


def config_bool(values: dict[str, str], key: str) -> bool:
    """读取 Kconfig 布尔值；Kconfig 中只有字符串 ``y`` 表示启用。"""
    return values.get(key) == "y"


def config_string(values: dict[str, str], key: str) -> str:
    """读取并反转义带引号的 Kconfig 字符串。"""
    value = values.get(key, "").strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        value = value[1:-1]
    return value.replace('\\"', '"').replace("\\\\", "\\")


def resolve_qemu_ovmf_path(configured: str) -> Path | None:
    """Resolve OVMF across Debian/Ubuntu and Arch/CachyOS layouts."""
    candidates: list[Path] = []
    if configured:
        configured_path = Path(configured).expanduser()
        candidates.append(
            configured_path if configured_path.is_absolute()
            else ROOT / configured_path
        )
    candidates.extend((
        ROOT / "buildsystem/firmware/OVMF.fd",
        ROOT / "build/firmware/OVMF.fd",
        Path("/usr/share/ovmf/OVMF.fd"),
        Path("/usr/share/edk2/x64/OVMF.4m.fd"),
        Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),
        Path("/usr/share/qemu/OVMF.fd"),
    ))
    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        try:
            if candidate.is_file() and candidate.stat().st_size >= 1024 * 1024:
                return candidate
        except OSError:
            continue
    return None


def ensure_parent(context: ActionContext, output: Path, text: str) -> None:
    """创建父目录并按内容写文件，未变化时保持 mtime 以利于增量构建。"""
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


def remove_staging_path_within(path: Path, staging_root: Path) -> None:
    """Remove only descendants of staging, without following parent escapes."""
    root = staging_root.resolve()
    candidate = Path(os.path.abspath(path))
    if (candidate == root or not candidate.is_relative_to(root) or
            not candidate.parent.resolve().is_relative_to(root)):
        raise GraphError(f"refusing to remove a path outside staging: {path}")
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
    elif path.is_dir():
        shutil.rmtree(path)


def copy_tree_preserving_links(source: Path, destination: Path) -> None:
    """Copy a package tree, preserving symlinks and absolute/relative targets."""
    if source.is_symlink():
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.unlink(missing_ok=True)
        destination.symlink_to(os.readlink(source))
        return
    if source.is_dir():
        if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
            destination.unlink()
        shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination, follow_symlinks=False)


def add_copy(graph: BuildGraph, name: str, source: Path, destination: Path) -> Target:
    return graph.add(
        Target(
            name=name,
            outputs=(destination,),
            inputs=(source,),
            kind="generate",
            source=source,
            action=copy_action(source, destination),
            action_key="copy-v3-preserve-mode",
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
    """向图中加入一次编译/汇编，并生成 depfile 让头文件变化能触发重编译。"""
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
    """向图中加入链接目标；所有参与链接的文件都必须放入 inputs。"""
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
    """根据配置拼装 QEMU 命令；设备拓扑和启动介质的修改集中在此函数。"""
    memory = config_int(values, "CONFIG_QEMU_MEMORY_MB")
    cpus = max(1, min(64, config_int(values, "CONFIG_QEMU_SMP_CPUS", 1)))
    width = config_int(values, "CONFIG_QEMU_DISPLAY_WIDTH")
    height = config_int(values, "CONFIG_QEMU_DISPLAY_HEIGHT")
    command = ["qemu-system-x86_64"]
    if config_bool(values, "CONFIG_QEMU_ENABLE_KVM"):
        command += ["-enable-kvm", "-cpu", "host"]
    else:
        # QEMU's host CPU model is valid only with KVM/HVF.  TCG uses max so
        # the smoke tests remain runnable on hosts without hardware access.
        command += ["-cpu", "max"]
    # The default remains the production AHCI topology used by existing
    # images. LEONOS_QEMU_IDE and LEONOS_QEMU_NVME select focused transport
    # smoke topologies without changing the shipped image layout.
    ide_mode = os.environ.get("LEONOS_QEMU_IDE", "").lower() in {"1", "y", "yes", "true"}
    nvme_mode = os.environ.get("LEONOS_QEMU_NVME", "").lower() in {"1", "y", "yes", "true"}
    if nvme_mode:
        ide_mode = False
    command += ["-machine", "pc" if ide_mode else "q35", "-m", f"{memory}M", "-smp", str(cpus)]
    configured_ovmf = config_string(values, "CONFIG_QEMU_OVMF_PATH")
    ovmf = resolve_qemu_ovmf_path(configured_ovmf)
    if ovmf is not None:
        command += ["-bios", str(ovmf)]
    # Keep the host pointer captured while it is over the guest window.  The
    # LeonOS input stack consumes QEMU's default PS/2 mouse, so a USB tablet
    # would not fix input and would instead add an unsupported device path.
    # Explicit GTK also avoids frontend-dependent grab behaviour when the
    # command is launched through `build.py run run`.
    command += ["-display", "gtk,grab-on-hover=on,show-cursor=on"]
    command += ["-serial", "stdio"]
    command += ["-device", f"VGA,xres={width},yres={height}"]
    if debug or iso:
        command += ["-no-reboot", "-no-shutdown"]
    net_model = config_string(values, "CONFIG_QEMU_NET_DEVICE")
    if net_model and all(character.isalnum() or character in "._-" for character in net_model):
        command += ["-netdev", "user,id=net0", "-device", f"{net_model},netdev=net0"]
    command += ["-audiodev", "sdl,id=snd0", "-device", "AC97,audiodev=snd0"]
    if iso and not ide_mode:
        command += ["-cdrom", relative(paths.images / "leonos4.iso")]
    if ide_mode:
        command += [
            "-device", "piix3-ide,id=ide",
            "-drive", f"file={relative(paths.images / 'leonos4.vmdk')},if=none,id=ide_disk,format=vmdk",
            "-device", f"ide-hd,drive=ide_disk,bus=ide.0,unit=0,bootindex={'2' if iso else '1'}",
        ]
        if iso:
            command += [
                "-drive", f"file={relative(paths.images / 'leonos4.iso')},if=none,id=ide_cd,format=raw,readonly=on",
                "-device", "ide-cd,drive=ide_cd,bus=ide.1,unit=0,bootindex=1",
            ]
    elif nvme_mode:
        command += [
            "-drive", f"file={relative(paths.images / 'leonos4.vmdk')},if=none,id=nvme0,format=vmdk",
            "-device", f"nvme,drive=nvme0,serial=leonosnvme,bootindex={'2' if iso else '1'}",
        ]
    else:
        command += [
            "-drive", f"file={relative(paths.images / 'leonos4.vmdk')},if=none,id=sata0,format=vmdk",
            "-device", "ich9-ahci,id=ahci", "-device", "ide-hd,drive=sata0,bus=ahci.0",
        ]
    return tuple(command)


def build_graph(paths: BuildPaths, config_path: Path | None = None) -> BuildGraph:
    """声明完整构建图。

    本函数按“生成配置 -> 工具链/库 -> 内核和驱动 -> 用户态 -> ESP/镜像 ->
    发布与测试”的顺序建立 target。新增产物应放在对应区域，并连接到上游
    target；不要只在列表末尾追加一个没有 inputs/depends_on 的孤立节点。
    """
    graph = BuildGraph(ROOT)

    def remove_staging_path(path: Path) -> None:
        remove_staging_path_within(path, paths.staging)
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

    # glxgears has a runtime dependency on PortableGL. Keep the shared object
    # in images whenever the test application is staged, even if a legacy
    # profile disabled the library's standalone IMAGE toggle.
    portablegl_image = (component_enabled("portablegl", "image") or
                        component_enabled("glxgears", "image"))

    def component_api_destination(component_id: str) -> Path:
        stage_path = components_by_id[component_id].api_stage_path
        if not stage_path:
            raise GraphError(f"component {component_id} has no API staging path")
        return paths.staging / stage_path

    def component_api_enabled(component_id: str) -> bool:
        return component_enabled(component_id, "api")

    installer_policy_apps = tuple(
        app for app in ("desktop", "settings") if component_enabled(app, "image")
    )
    gptinit_source = ROOT / "userland/apps/gptinit/main.c"
    cc = os.environ.get("CC", "clang")
    cxx = os.environ.get("CXX", "clang++")
    rustc = os.environ.get("RUSTC", "rustc")
    ar = os.environ.get("AR", "llvm-ar")
    ld = os.environ.get("LD", "ld.lld")
    objcopy = os.environ.get("OBJCOPY", "llvm-objcopy")
    clang_headers = Path(subprocess.check_output((cc, "-print-resource-dir"), text=True).strip()) / "include"
    compiler_rt_archive = find_compiler_rt_archive(cc)
    if compiler_rt_archive is None:
        raise GraphError("Clang compiler-rt builtins archive is required for the dynamic runtime")
    generated = paths.generated_include
    autoconf = generated / "autoconf.h"
    installer_autoconf = generated / "autoconf-installer.h"
    rustcfg = generated / "rustcfg.args"
    build_info = ROOT / "include/generated/build_info.h"
    loader_integrity = generated / "loader_integrity.h"
    gbk_table_header = generated / "leonos_gbk_table.h"
    musl_prefix = paths.out / "musl/sysroot"
    graph.add(Target(
        name="musl",
        outputs=tuple(musl_prefix / name for name in (
            ".leonos-musl.json", "lib/libc.so", "lib/libc.a", "lib/libmimalloc.so.3",
            "share/licenses/musl/COPYRIGHT", "share/licenses/mimalloc/LICENSE",
            "lib/mimalloc.o", "lib/crt1.o", "lib/Scrt1.o", "lib/crti.o", "lib/crtn.o")),
        inputs=tuple([ROOT / "tools/build_musl.py", ROOT / "tools/fetch_auth_upstream.py",
            ROOT / "patches/musl/0001-enforce-password-file-lock.patch", *collect(
            "third_party/musl/**/*.c", "third_party/musl/**/*.h", "third_party/musl/**/*.s",
            "third_party/musl/**/*.in", "third_party/musl/configure", "third_party/musl/Makefile",
            "third_party/mimalloc/src/**/*.c", "third_party/mimalloc/include/**/*.h")]),
        kind="compile",
        command=(PYTHON, "tools/build_musl.py", "--build-dir", relative(paths.out / "musl"),
                 "--prefix", relative(musl_prefix)),
    ))
    graph.add(Target(
        name="test-musl-abi", depends_on=("musl",), kind="test",
        command=(PYTHON, "tools/test_musl_abi.py", "--prefix", relative(musl_prefix)),
    ))
    from tools.storage_tools import (FORMATTER_COMMANDS, UTIL_LINUX_COMMANDS,
                                     UTIL_LINUX_LIBRARIES, COMPAT_LINKS, stage_filesystems)
    auth_root = paths.out / "auth-upstream/root"
    auth_libraries = (auth_root / "lib/libpam.so.0", auth_root / "lib/libcrypt.so.2")
    auth_headers = (auth_root / "usr/include/security/pam_appl.h", auth_root / "usr/include/crypt.h")
    graph.add(Target(
        name="auth-upstream", depends_on=("musl",), kind="command",
        outputs=(*auth_libraries, *auth_headers, auth_root / "usr/bin/sudo",
                 auth_root / "usr/bin/passwd", auth_root / "bin/su",
                 *(auth_root / name for name in (*UTIL_LINUX_COMMANDS, *UTIL_LINUX_LIBRARIES)),
                 auth_root / "usr/lib/libuuid.a", auth_root / "usr/lib/libblkid.a",
                 auth_root / "lib/security/pam_leonos_password.so"),
        inputs=(ROOT / "configs/auth-upstream.json", ROOT / "tools/fetch_auth_upstream.py",
                musl_prefix / ".leonos-musl.json",
                ROOT / "patches/linux-pam/0001-reject-unavailable-salt-entropy.patch",
                ROOT / "patches/linux-pam/0002-use-sized-libcrypt-entry-point.patch",
                ROOT / "patches/linux-pam/0003-limits-handle-unimplemented-resources.patch",
                ROOT / "userland/pam/pam_leonos_password.c",
                ROOT / "userland/libc/src/auth_password.c",
                ROOT / "tools/build_auth_upstream.py"),
        command=(PYTHON, "tools/build_auth_upstream.py", "linux-pam", "sudo", "util-linux", "shadow",
                 "--work", relative(paths.out / "auth-upstream"), "--musl", relative(musl_prefix)),
    ))
    storage_root = paths.out / "storage-upstream/root"
    storage_package = storage_root / ".storage-package.json"
    graph.add(Target(
        name="storage-upstream", depends_on=("musl", "auth-upstream"), kind="command",
        outputs=(storage_package, *(storage_root / name for name in FORMATTER_COMMANDS)),
        inputs=(ROOT / "configs/storage-upstream.json", ROOT / "tools/build_storage_upstream.py",
                ROOT / "tools/fetch_auth_upstream.py", ROOT / "tools/storage_tools.py",
                ROOT / "tools/build_auth_upstream.py", musl_prefix / ".leonos-musl.json",
                auth_root / "usr/lib/libuuid.a", auth_root / "usr/lib/libblkid.a"),
        command=(PYTHON, "tools/build_storage_upstream.py", "--work", relative(paths.out / "storage-upstream"),
                 "--musl", relative(musl_prefix), "--util-root", relative(auth_root)),
    ))
    musl_archive = musl_prefix / "lib/libc.a"
    musl_stamp = musl_prefix / ".leonos-musl.json"
    from tools.package_musl_gcc import ARCHIVE_NAME
    gcc_archive = ROOT / "buildsystem/deps/musl-gcc" / ARCHIVE_NAME
    gcc_source = Path(os.environ["LEONOS_GCC_ARCHIVE"]).resolve() if os.environ.get("LEONOS_GCC_ARCHIVE") else None
    gcc_package = paths.out / "musl-gcc/root"
    gcc_stamp = gcc_package / ".leonos-package.json"
    graph.add(Target(name="musl-gcc", outputs=(gcc_stamp, gcc_archive,
                         gcc_package / "opt/dyne",
                         gcc_package / "usr/share/licenses/musl-gcc",
                         gcc_package / "usr/share/examples/musl-gcc"),
                     inputs=(ROOT / "tools/package_musl_gcc.py", ROOT / "userland/musl-gcc/launcher.c",
                             ROOT / "userland/musl-gcc/README.md", ROOT / "userland/musl-gcc/hello.c",
                             ROOT / "userland/musl-gcc/COPYING3", ROOT / "userland/musl-gcc/COPYING.RUNTIME",
                             ROOT / "third_party/musl/COPYRIGHT", *((gcc_source,) if gcc_source else ())),
                     kind="generate", command=(PYTHON, "tools/package_musl_gcc.py",
                         "--archive", str(gcc_archive), "--out", relative(gcc_package),
                         *(("--source", str(gcc_source)) if gcc_source else ()))))
    graph.add(Target(name="test-musl-gcc-package", depends_on=("musl-gcc",), kind="test",
                     command=(PYTHON, "tools/test_musl_gcc_package.py", "--root", relative(gcc_package))))
    from tools.package_python import ARCHIVE_NAME as python_archive_name, PAYLOAD_PATHS as python_payload_paths
    python_archive = ROOT / "buildsystem/deps/python" / python_archive_name
    python_source = Path(os.environ["LEONOS_PYTHON_ARCHIVE"]).resolve() if os.environ.get("LEONOS_PYTHON_ARCHIVE") else None
    python_package = paths.out / "python/root"
    python_stamp = python_package / ".leonos-package.json"
    graph.add(Target(name="python", outputs=(python_stamp, python_archive,
                         *(python_package / name for name in python_payload_paths)),
                     inputs=(ROOT / "tools/package_python.py", ROOT / "tools/musl_link.py", musl_stamp,
                             ROOT / "userland/python/launcher.c", ROOT / "userland/python/README.md",
                             ROOT / "userland/python/hello.py", *((python_source,) if python_source else ())),
                     depends_on=("musl",), kind="generate",
                     command=(PYTHON, "tools/package_python.py", "--archive", str(python_archive),
                              "--out", relative(python_package), "--musl", relative(musl_prefix),
                              *(("--source", str(python_source)) if python_source else ()))))
    graph.add(Target(name="test-python-package", depends_on=("python",), kind="test",
                     command=(PYTHON, "tools/test_python_package.py", "--root", relative(python_package))))
    ncurses_prefix = paths.out / "ncurses/install/usr"
    vim_prefix = paths.out / "vim/install/usr"
    ncurses_stamp = ncurses_prefix / ".leonos-package.json"
    vim_stamp = vim_prefix / ".leonos-package.json"
    vim_elf = vim_prefix / "bin/vim"
    for package, prefix, outputs in (
        ("ncurses", ncurses_prefix, (ncurses_stamp, ncurses_prefix / "lib/libncursesw.a",
                                    ncurses_prefix / "lib/libtinfow.a", ncurses_prefix / "include/curses.h",
                                    ncurses_prefix / "share/terminfo")),
        ("vim", vim_prefix, (vim_stamp, vim_elf, vim_prefix / "share/vim/vim91")),
    ):
        graph.add(Target(name=package, outputs=outputs,
                         inputs=(musl_stamp, ROOT / "tools/build_terminal_packages.py",
                                 *collect(f"third_party/{package}/**/*"),
                                 *((ncurses_stamp,) if package == "vim" else ())),
                         depends_on=("musl", "ncurses") if package == "vim" else ("musl",),
                         kind="compile", command=(
                             PYTHON, "tools/build_terminal_packages.py", package,
                             "--work", relative(paths.out / package / "work"),
                             "--prefix", relative(prefix), "--musl", relative(musl_prefix),
                             *(("--ncurses", relative(ncurses_prefix)) if package == "vim" else ()))))
    graph.add(Target(name="test-terminal-packages", depends_on=("vim",), kind="test",
                     command=(PYTHON, "tools/test_terminal_packages.py",
                              "--musl", relative(musl_prefix), "--ncurses", relative(ncurses_prefix),
                              "--vim", relative(vim_prefix))))
    runtime_so = paths.out / "system/lib/libleonos.so.2"
    runtime_loader = musl_prefix / "lib/libc.so"
    musl_scrt_obj = musl_prefix / "lib/Scrt1.o"
    musl_crti_obj = musl_prefix / "lib/crti.o"
    libmagic_so = paths.out / "system/lib/libmagic.so.1"
    liblua_so = paths.out / "system/lib/liblua.so.5"
    portablegl_source = ROOT / "third_party/portablegl"
    portablegl_port = ROOT / "userland/portablegl"
    portablegl_so = paths.out / "system/lib/libportablegl.so.1"
    portablegl_archive = paths.out / "userland/libportablegl.a"
    portablegl_stamp = paths.out / "userland/portablegl.stamp"
    portablegl_work_dir = paths.out / "portablegl-work"
    glxgears_source = paths.out / "generated/glxgears/gears-upstream.c"
    sqlite_source = ROOT / "third_party/sqlite"
    sqlite_port = ROOT / "userland/sqlite"
    sqlite_so = paths.out / "system/lib/sqlite.so.3"
    sqlite_archive = paths.out / "userland/sqlite.a"
    sqlite_header = paths.out / "generated/sqlite/sqlite3.h"
    sqlite_stamp = paths.out / "userland/sqlite.stamp"
    sqlite_work_dir = paths.out / "sqlite-work"
    dynlinkerror_elf = paths.out / "userland/dynlinkerror.elf"
    installer_runtime_so = paths.out / "userland-installer-policy/libleonos.so.2"
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
    busybox_source_stamp = paths.out / "busybox/source-revision.txt"
    busybox_elf = paths.out / "userland/busybox.elf"
    busybox_stamp = paths.out / "userland/busybox.stamp"
    busybox_links = paths.out / "userland/busybox.links"
    nano_source = ROOT / "third_party/nano"
    nano_port = ROOT / "userland/nano"
    nano_elf = paths.out / "userland/nano.elf"
    nano_stamp = paths.out / "userland/nano.stamp"
    nano_work_dir = paths.out / "nano-work"
    fastfetch_license = ROOT / "userland/fastfetch/LICENSE"
    fastfetch_elf = paths.out / "userland/fastfetch.elf"
    fastfetch_stamp = paths.out / "userland/fastfetch.stamp"
    from tools.package_fastfetch import CACHE as fastfetch_cache
    fastfetch_prebuilt = (Path(os.environ["LEONOS_FASTFETCH_BINARY"]).resolve()
        if os.environ.get("LEONOS_FASTFETCH_BINARY") else
        None)
    sl_source = ROOT / "third_party/sl"
    sl_port = ROOT / "userland/sl"
    sl_elf = paths.out / "userland/sl.elf"
    sl_stamp = paths.out / "userland/sl.stamp"
    sl_work_dir = paths.out / "sl-work"
    less_source = ROOT / "third_party/less"
    less_port = ROOT / "userland/less"
    less_elf = paths.out / "userland/less.elf"
    less_stamp = paths.out / "userland/less.stamp"
    less_work_dir = paths.out / "less-work"
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
    grub_efi_dir = paths.build_modules
    system_grub_efi_dir = Path("/usr/lib/grub/x86_64-efi")
    using_system_grub = False
    if not (grub_efi_dir / "modinfo.sh").exists() and (system_grub_efi_dir / "modinfo.sh").exists():
        grub_efi_dir = system_grub_efi_dir
        using_system_grub = True

    # ---- 配置同步 ----
    # 先从 components.toml 生成组件 Kconfig，再将用户配置转换成 C/Rust 可消费的
    # 生成文件。新增配置输出格式时修改 tools/kconfig_sync.py，并把脚本加入 inputs。
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
    if not (busybox_source / "Makefile").is_file():
        raise GraphError("third_party/busybox is missing; initialize the BusyBox source tree")
    if not (nano_source / "src/nano.c").is_file():
        raise GraphError("third_party/nano is missing; initialize the GNU nano source tree")
    if not fastfetch_license.is_file():
        raise GraphError("userland/fastfetch/LICENSE is missing")
    if not (sl_source / "sl.c").is_file() or not (sl_source / "sl.h").is_file() or not (sl_source / "LICENSE").is_file():
        raise GraphError("third_party/sl is missing; initialize the sl source tree")
    if not (ROOT / "userland/libc/include/curses.h").is_file():
        raise GraphError("the LeonOS sl port metadata is missing")
    if (not (less_source / "main.c").is_file() or
            not (less_source / "COPYING").is_file() or
            not (less_source / "LICENSE").is_file()):
        raise GraphError("third_party/less is missing; initialize the GNU less source tree")
    if (not (less_port / "leonos_termcap.c").is_file() or
            not (less_port / "include/defines.h").is_file()):
        raise GraphError("the LeonOS less port metadata is missing")
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
    if (not (portablegl_source / "portablegl.h").is_file() or
            not (portablegl_source / "LICENSE").is_file() or
            not (portablegl_port / "leonos_pgl.c").is_file() or
            not (ROOT / "userland/libc/include/leonos/pgl.h").is_file()):
        raise GraphError("the PortableGL source or LeonOS port metadata is missing")
    # ---- 全局编译策略 ----
    # 这些 flag 影响内核、运行库和应用；组件若需例外，应在自己的 target
    # 中追加参数，避免改变所有产物的 ABI。
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
            hidden_desktop_entries.append(
                f"hide={layout.app_package_dir_abs(component.id)}/{component.id}.elf"
            )
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
        "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    asflags_kernel = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Ikernel/ntclks/include", "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_loader = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mgeneral-regs-only",
        "-Wall", "-Wextra", "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}",
        "-include", relative(autoconf),
    ]
    asflags_loader = [
        cc, "-target", "x86_64-unknown-none", *build_compile_flags, "-ffreestanding", "-mno-red-zone",
        "-mgeneral-regs-only", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_user_base = [
        cc, "-target", "x86_64-linux-musl", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-nostdinc", "-isystem", str(clang_headers),
        "-fno-stack-protector", "-fPIC", "-fPIE",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-DLEONOS_USE_MUSL", "-D_GNU_SOURCE", "-mno-avx", "-mno-avx2",
        "-D_POSIX_C_SOURCE=200809L",
        f"-I{relative(musl_prefix / 'include')}", "-Iuserland/libc/include",
        "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}", "-Ithird_party/mbedtls/include",
        "-Ithird_party/zlib", "-Ithird_party/libpng", f"-I{relative(libpng_generated_dir)}",
        '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
    ]
    cflags_user = cflags_user_base + ["-include", relative(autoconf)]
    cflags_doom = cflags_user + ["-DLEONOS_DOOM", "-DFEATURE_SOUND", "-Ithird_party/doomgeneric/doomgeneric"]
    cflags_mp3play = [flag for flag in cflags_user if flag != "-mgeneral-regs-only"] + [
        "-mno-avx", "-mno-avx2",
        "-Ithird_party/minimp3",
    ]
    # PortableGL uses the x86-64 SSE floating-point ABI for its shader and
    # matrix callbacks. Keep it isolated from the normal GPR-only app ABI.
    cflags_glxgears = [flag for flag in cflags_user if flag != "-mgeneral-regs-only"] + [
        "-mno-avx", "-mno-avx2", "-Ithird_party/portablegl",
        "-Iuserland/apps/glxgears", f"-I{relative(glxgears_source.parent)}",
    ]
    cflags_installer = cflags_user_base + ["-include", relative(installer_autoconf)]
    cflags_user_libc_base = [
        cc, "-target", "x86_64-linux-musl", *build_compile_flags, "-std=c11", "-ffreestanding",
        "-fno-stack-protector", "-fPIC",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-DLEONOS_USE_MUSL", "-D_GNU_SOURCE", "-mno-avx", "-mno-avx2",
        f"-I{relative(musl_prefix / 'include')}",
        "-Iuserland/libc/include", "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}",
        "-Ithird_party/mbedtls/include", "-Ithird_party/zlib", "-Ithird_party/libpng",
        f"-I{relative(libpng_generated_dir)}", '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"',
    ]
    # TCC's target linker consumes a static, non-PIC SDK.  The normal libc
    # objects are PIC inputs for the shared runtime and contain GOT relaxation
    # relocations that TinyCC intentionally does not accept in static links.
    cflags_user_libc_static_base = [
        "-fno-pic" if flag == "-fPIC" else
        f"-I{relative(musl_prefix / 'include')}" if flag == f"-I{relative(musl_prefix / 'include')}" else
        flag
        for flag in cflags_user_libc_base
    ]
    cflags_user_libc = cflags_user_libc_base + ["-include", relative(autoconf)]
    cflags_user_libc_static = cflags_user_libc_static_base + ["-include", relative(autoconf)]
    cflags_installer_libc = cflags_user_libc_base + ["-include", relative(installer_autoconf)]
    asflags_user = [
        cc, "-target", "x86_64-linux-musl", *build_compile_flags, "-ffreestanding",
         "-Iuserland/libc/include", "-Iinclude/uapi", "-Iinclude", f"-I{relative(paths.out / 'include')}",
    ]
    cflags_runtime = [flag for flag in cflags_user_libc if flag not in {"-include", relative(autoconf)}]
    cflags_runtime += ["-include", relative(autoconf), "-fPIC"]
    asflags_runtime = [*asflags_user, "-fPIC"]
    dynamic_link_flags = [
        "-pie", "--hash-style=sysv", "--dynamic-linker", "/lib/ld-musl-x86_64.so.1",
        "-z", "relro", "-z", "now", "-z", "max-page-size=0x1000",
    ]

    # ---- 启动链：boot logo、loader、kernel、middlelayer ----
    # 启动链 target 按依赖顺序连接。新增启动阶段时要同时声明输入、输出，
    # 并把最终产物接入 all/esp，否则命令行看似成功但镜像不会包含它。
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
    kernel_sources = collect("kernel/ntclks/**/*.c", "kernel/ntclks/**/*.S", "kernel/ostui/**/*.c", "drivers/bootstrap/**/*.c", "drivers/bootstrap/**/*.S")
    # storage.c is a facade for the private storage modules.  The modules are
    # included into that one translation unit and must not also be compiled as
    # independent kernel objects (many intentionally begin/end at function
    # boundaries and share private state).
    storage_facade = ROOT / "drivers/bootstrap/storage.c"
    storage_modules = collect("drivers/bootstrap/storage/*.c")
    kernel_sources = [source for source in kernel_sources
                      if source not in storage_modules]
    rust_sources = collect("middlelayer/osmlayer/src/**/*.rs")
    kernel_objects: list[Path] = []
    for source in kernel_sources:
        implicit: list[Path] = [autoconf]
        if source == storage_facade:
            implicit += [ROOT / "drivers/bootstrap/storage/storage_internal.h",
                         *storage_modules]
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
            command=(
                ld,
                "-nostdlib",
                "-z",
                "max-page-size=0x1000",
                "-T",
                "kernel/ntclks/arch/x86_64/linker.ld",
                "-o",
                relative(kernel_unstripped),
                *map(relative, kernel_objects),
            ),
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

    kerneldebug_source = ROOT / "kernel/kerneldebug/kerneldebug.c"
    kerneldebug_sys = paths.out / "system/kerneldebug.sys"
    kerneldebug_obj = add_compile(
        graph, paths, "compile:kerneldebug:module", kerneldebug_source,
        "kerneldebug", cflags_kernel + ["-fno-pic", "-fno-pie"], (autoconf,))
    graph.add(Target(
        name="kerneldebug-module",
        outputs=(kerneldebug_sys,),
        inputs=(kerneldebug_obj,),
        depends_on=("compile:kerneldebug:module",),
        kind="generate",
        command=(objcopy, "--remove-section", ".llvm_addrsig", "--remove-section", ".comment",
                 "--remove-section", ".note.GNU-stack", "--rename-section",
                 ".note.leonos.kerneldebug=.note.leonos.kerneldebug,alloc,load,readonly,data,contents",
                 relative(kerneldebug_obj), relative(kerneldebug_sys)),
        action_key="kerneldebug-module-v2",
    ))

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
            command=(
                ld,
                "-nostdlib",
                "-z",
                "max-page-size=0x1000",
                "-T",
                "middlelayer/osmlayer/linker.ld",
                "-o",
                relative(middle_sys),
                relative(rust_obj),
                relative(middle_runtime_obj),
            ),
        )
    )
    graph.add(Target(name="middlelayer", depends_on=("middlelayer-image",), group=True, kind="aggregate"))

    graph.add(
        Target(
            name="loader-integrity",
            outputs=(loader_integrity,),
            inputs=(kernel_sys, middle_sys, ROOT / "tools/gen_loader_integrity.py"),
            kind="generate",
            command=(
                PYTHON,
                "tools/gen_loader_integrity.py",
                "--kernel",
                relative(kernel_sys),
                "--middlelayer",
                relative(middle_sys),
                "--out",
                relative(loader_integrity),
            ),
        )
    )
    loader_objects: list[Path] = []
    for source in loader_sources:
        flags = asflags_loader if source.suffix == ".S" else cflags_loader
        if source == ROOT / "boot/loader/main.c":
            implicit = (autoconf, loader_integrity, boot_logo)
        else:
            implicit = (loader_integrity,)
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
    # musl owns POSIX and startup; libleonos contains only OS extensions.
    libc_sources = collect("userland/libc/src/*.c", "userland/libc/src/*.S")
    libc_sources += collect("userland/auth/*.c")
    libc_sources += [ROOT / "third_party/mbedtls/library" / source for source in MBEDTLS_SOURCES]
    libc_a = paths.out / "musl/lib/libleonos.a"
    static_libc_a = libc_a
    installer_libc_a = paths.out / "musl/lib/libleonos-installer.a"

    zlib_objects = [
        add_compile(graph, paths, f"compile:zlib:{source}", zlib_source / source, "zlib",
                    cflags_user_libc + ["-DZ_SOLO", "-include", "stddef.h"],
                    (autoconf, musl_stamp))
        for source in ZLIB_SOURCES
    ]
    graph.add(Target(name="archive:zlib", outputs=(zlib_archive,), inputs=tuple(zlib_objects),
                     kind="link", command=(ar, "rcs", relative(zlib_archive),
                                             *map(relative, zlib_objects))))
    libpng_cflags = cflags_user_libc + ["-DLEONOS_LIBPNG_FIXED_POINT=3"]
    libpng_objects = [
        add_compile(graph, paths, f"compile:libpng:{source}", libpng_source / source, "libpng",
                    libpng_cflags, (autoconf, musl_stamp, libpng_config))
        for source in LIBPNG_SOURCES
    ]
    graph.add(Target(name="archive:libpng", outputs=(libpng_archive,),
                     inputs=tuple([*libpng_objects, libpng_config]), kind="link",
                     command=(ar, "rcs", relative(libpng_archive),
                              *map(relative, libpng_objects))))

    # Build the new runtime independently until all image consumers migrate.
    # musl alone owns the standard POSIX symbols; this DSO exports extensions.
    musl_lib = musl_prefix / "lib/libc.so"
    mimalloc_lib = musl_prefix / "lib/libmimalloc.so.3"
    musl_stamp = musl_prefix / ".leonos-musl.json"
    musl_runtime_so = runtime_so
    musl_extension_archive = libc_a
    musl_cflags = [cflags_user_libc[0], "-I", relative(auth_root / "usr/include"),
                  *cflags_user_libc[1:]]
    musl_sources = list(libc_sources)
    musl_sources += [zlib_source / source for source in ZLIB_SOURCES]
    musl_sources += [libpng_source / source for source in LIBPNG_SOURCES]
    musl_objects = []
    for source in sorted(musl_sources):
        flags = list(musl_cflags)
        if source.parent == zlib_source:
            flags += ["-DZ_SOLO", "-include", "stddef.h"]
        if source.parent == libpng_source:
            flags += ["-DLEONOS_LIBPNG_FIXED_POINT=3"]
        musl_objects.append(add_compile(
            graph, paths, f"compile:musl-runtime:{relative(source)}", source, "musl-runtime",
            ([cc, "--target=x86_64-linux-musl", "-fPIC", "-Iinclude/uapi"]
             if source.suffix == ".S" else flags),
            (autoconf, musl_stamp, libpng_config, gbk_table_header, *auth_headers),
            kind="assemble" if source.suffix == ".S" else "compile"))
    graph.add(Target(
        name="musl-runtime", outputs=(musl_runtime_so,),
        inputs=tuple([*musl_objects, mimalloc_lib, musl_lib, compiler_rt_archive, *auth_libraries]),
        kind="link", command=(ld, "-shared", "--no-undefined", "--hash-style=both",
            "-soname", "libleonos.so.2", "-o", relative(musl_runtime_so),
            *map(relative, musl_objects), "-L", relative(musl_prefix / "lib"),
            "-l:libmimalloc.so.3", *map(relative, auth_libraries), "-lc",
            relative(compiler_rt_archive)),
    ))
    graph.add(Target(
        name="musl-extension-archive", outputs=(musl_extension_archive,),
        inputs=(*musl_objects, ROOT / "tools/rebuild_archive.py"), kind="link",
        command=(PYTHON, "tools/rebuild_archive.py", "--ar", ar,
                 "--output", relative(musl_extension_archive), *map(relative, musl_objects)),
    ))
    installer_objects = []
    for source in sorted(musl_sources):
        flags = [relative(installer_autoconf) if flag == relative(autoconf) else flag
                 for flag in musl_cflags]
        if source.parent == zlib_source:
            flags += ["-DZ_SOLO", "-include", "stddef.h"]
        if source.parent == libpng_source:
            flags += ["-DLEONOS_LIBPNG_FIXED_POINT=3"]
        installer_objects.append(add_compile(
            graph, paths, f"compile:installer-runtime:{relative(source)}", source,
            "musl-installer-runtime", flags,
            (installer_autoconf, musl_stamp, libpng_config, gbk_table_header, *auth_headers),
            kind="assemble" if source.suffix == ".S" else "compile"))
    graph.add(Target(name="installer-runtime", outputs=(installer_runtime_so,),
                     inputs=tuple([*installer_objects, musl_lib, mimalloc_lib, compiler_rt_archive, *auth_libraries]),
                     kind="link", command=musl_link.shared(musl_prefix, installer_runtime_so,
                         installer_objects, (compiler_rt_archive, *auth_libraries), soname="libleonos.so.2")))
    graph.add(Target(name="archive:installer-libc", outputs=(installer_libc_a,),
                     inputs=(*installer_objects, ROOT / "tools/rebuild_archive.py"), kind="link",
                     command=(PYTHON, "tools/rebuild_archive.py", "--ar", ar,
                              "--output", str(installer_libc_a), *map(str, installer_objects))))
    for name, dependency in (("runtime", "musl-runtime"), ("runtime-loader", "musl"),
                             ("archive:libc", "musl-extension-archive"),
                             ("archive:libc-static", "musl-extension-archive")):
        graph.add(Target(name=name, depends_on=(dependency,), kind="aggregate", group=True))
    dynlinkerror_object = add_compile(graph, paths, "compile:dynlinkerror",
        ROOT / "userland/apps/dynlinkerror/main.c", "musl-dynlinkerror", cflags_user,
        (autoconf, musl_stamp, libpng_config))
    graph.add(Target(name="dynlinkerror", outputs=(dynlinkerror_elf,),
        inputs=(dynlinkerror_object, libc_a, musl_archive, musl_stamp, compiler_rt_archive),
        kind="link", command=musl_link.executable(musl_prefix, dynlinkerror_elf,
            (dynlinkerror_object,), (libc_a, compiler_rt_archive), static=True)))

    graph.add(Target(
        name="musl-sdk", outputs=(paths.out / "musl/leonos-musl-sdk.tar.gz",),
        inputs=tuple([musl_runtime_so, musl_extension_archive, musl_stamp, libpng_config,
                      ROOT / "tools/package_musl_sdk.py", ROOT / "tools/leonos_musl_cc.py",
                      *collect("include/uapi/**/*.h", "include/leonos/*.h",
                               "userland/libc/include/leonos/*.h")]),
        depends_on=("ncurses",) if component_enabled("ncurses", "sdk") else (),
        implicit_inputs=(ncurses_stamp,) if component_enabled("ncurses", "sdk") else (),
        kind="generate", command=(PYTHON, "tools/package_musl_sdk.py",
            "--prefix", relative(musl_prefix), "--runtime", relative(musl_runtime_so),
            "--archive", relative(musl_extension_archive), "--png-config", relative(libpng_config),
            "--stage", relative(paths.out / "musl/sdk"),
            "--output", relative(paths.out / "musl/leonos-musl-sdk.tar.gz"),
            *(("--ncurses", relative(ncurses_prefix)) if component_enabled("ncurses", "sdk") else ())),
    ))
    for mode in ("dynamic", "static"):
        probe = paths.out / f"musl/tests/musl-abi-{mode}.elf"
        graph.add(Target(
            name=f"musl-probe:{mode}", outputs=(probe,), depends_on=("musl-sdk",),
            inputs=(ROOT / "tools/tests/musl_guest_test.c",
                    ROOT / "tools/tests/futex2_abi_test.c",
                    ROOT / "tools/tests/clone3_abi_test.c",
                    ROOT / "tools/tests/symlink_abi_test.c",
                    ROOT / "tools/tests/signal_queue_abi_test.c",
                    ROOT / "tools/tests/socket_batch_abi_test.c",
                    ROOT / "tools/tests/signalfd_abi_test.c",
                    ROOT / "tools/tests/process_vm_abi_test.c",
                    ROOT / "tools/tests/sysv_msg_abi_test.c",
                    ROOT / "tools/tests/sysv_sem_abi_test.c",
                    ROOT / "tools/tests/prctl_abi_test.c",
                    ROOT / "tools/tests/utsname_abi_test.c",
                    ROOT / "tools/tests/membarrier_abi_test.c",
                    ROOT / "tools/tests/openat2_abi_test.c",
                    paths.out / "musl/leonos-musl-sdk.tar.gz"),
            kind="link", command=(PYTHON, relative(paths.out / "musl/sdk/bin/leonos-musl-cc"),
                "-O2", f'-DPROBE_KIND="{mode}"', *(('-static',) if mode == "static" else ()),
                "tools/tests/musl_guest_test.c", "-o", relative(probe)),
        ))
    graph.add(Target(name="musl-probes", depends_on=("musl-probe:dynamic", "musl-probe:static"),
                     group=True, kind="aggregate"))
    gcc_probe = paths.out / "gcc-probe/gcc-probe.elf"
    graph.add(Target(name="gcc-probe-runner", outputs=(gcc_probe,), depends_on=("musl-sdk",),
                     inputs=(ROOT / "tools/tests/gcc_guest_probe.c", ROOT / "tools/tests/vfork_linux_edges.c",
                             ROOT / "userland/apps/installer/installer_directory.h",
                             paths.out / "musl/leonos-musl-sdk.tar.gz"),
                     kind="link", command=(PYTHON, relative(paths.out / "musl/sdk/bin/leonos-musl-cc"),
                         "-O2", "-static", "tools/tests/gcc_guest_probe.c", "-o", relative(gcc_probe))))
    if component_enabled("musl-gcc", "image"):
        graph.add(Target(name="gcc-probe-image", outputs=(paths.out / "gcc-probe/gcc-probe.vmdk",),
                         depends_on=("gcc-probe-runner", "esp"),
                         inputs=(gcc_stamp, gcc_probe, kernel_sys, loader_elf, middle_sys,
                                 ROOT / "tools/prepare_gcc_probe.py"),
                         kind="generate", command=(PYTHON, "tools/prepare_gcc_probe.py",
                             "--runner", str(gcc_probe),
                             "--out", relative(paths.out / "gcc-probe"))))
    ltp_programs = ("getcwd01", "fcntl01", "fstat02", "mprotect01", "chmod01", "fchmod01", "chown01", "ltp-runner",
                    "pthread_create_1-1", "pthread_join_1-1", "pthread_mutex_lock_1-1", "pthread_cond_wait_1-1",
                    "pthread_cancel_1-1", "pthread_key_create_1-1", "pthread_barrier_wait_1-1",
                    "pthread_rwlock_rdlock_1-1", "pthread_once_1-1", "pthread_mutex_timedlock_1-1",
                    "pthread_cond_timedwait_1-1", "pthread_mutex_trylock_1-1", "sem_timedwait_1-1", "pthread_spin_lock_1-1")
    graph.add(Target(name="musl-ltp", depends_on=("musl-sdk",), kind="generate",
                     outputs=tuple(paths.out / f"musl/ltp/{name}.elf" for name in ltp_programs),
                     inputs=(ROOT / "tools/build_musl_ltp.py", ROOT / "tools/tests/ltp_guest_runner.c",
                             paths.out / "musl/leonos-musl-sdk.tar.gz"),
                     command=(PYTHON, "tools/build_musl_ltp.py", "--cache", relative(paths.out),
                              "--sdk", relative(paths.out / "musl/sdk"), "--out", relative(paths.out / "musl/ltp"))))

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
        inputs=tuple([*file_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj, musl_archive]),
        depends_on=("musl", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_file.py",
            "--source",
            "third_party/file",
            "--port",
            "userland/file",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--generated-include",
            relative(paths.generated_include),
            "--leonos-lib",
            relative(libc_a),
            "--runtime-so",
            relative(runtime_so),
            "--musl-lib",
            relative(musl_archive),
            "--output",
            relative(file_elf),
            "--library",
            relative(libmagic_so),
            "--static-library",
            relative(libmagic_archive),
            "--magic-header",
            relative(file_magic_header),
            "--stamp",
            relative(file_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    sqlite_inputs = tuple([
        ROOT / "tools/build_sqlite.py",
        sqlite_source / "VERSION",
        sqlite_source / "main.mk",
        sqlite_source / "Makefile.linux-gcc",
        sqlite_source / "tool/mksqlite3c.tcl",
        sqlite_port / "leonos_sqlite_vfs.c",
        sqlite_port / "README.md",
        ROOT / "tools/musl_link.py",
        runtime_so,
        musl_scrt_obj,
        musl_crti_obj,
    ])
    graph.add(Target(
        name="sqlite",
        outputs=(sqlite_so, sqlite_archive, sqlite_header, sqlite_stamp),
        inputs=sqlite_inputs,
        depends_on=("musl", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_sqlite.py",
            "--source",
            "third_party/sqlite",
            "--port",
            "userland/sqlite",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--runtime-so",
            relative(runtime_so),
            "--library",
            relative(sqlite_so),
            "--static-library",
            relative(sqlite_archive),
            "--header",
            relative(sqlite_header),
            "--work-dir",
            relative(sqlite_work_dir),
            "--stamp",
            relative(sqlite_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    portablegl_inputs = tuple([
        ROOT / "tools/build_portablegl.py", portablegl_source / "portablegl.h",
        portablegl_source / "LICENSE", portablegl_port / "leonos_pgl.c",
        ROOT / "userland/libc/include/leonos/pgl.h",
    ])
    graph.add(Target(
        name="portablegl",
        outputs=(portablegl_so, portablegl_archive, portablegl_stamp),
        inputs=tuple([*portablegl_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_crti_obj, autoconf]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_portablegl.py",
            "--source",
            "third_party/portablegl",
            "--port",
            "userland/portablegl",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--generated-include",
            relative(paths.generated_include),
            "--autoconf",
            relative(autoconf),
            "--runtime-so",
            relative(runtime_so),
            "--library",
            relative(portablegl_so),
            "--static-library",
            relative(portablegl_archive),
            "--work-dir",
            relative(portablegl_work_dir),
            "--stamp",
            relative(portablegl_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    def generate_glxgears_source(context: ActionContext) -> None:
        # The upstream example intentionally defines PORTABLEGL_IMPLEMENTATION
        # because it is normally a standalone SDL program. LeonOS supplies the
        # implementation through libportablegl.so.1, so remove only that one
        # line in a generated copy and leave the vendored source untouched.
        upstream = portablegl_source / "examples/classic/gears.c"
        text = upstream.read_text(encoding="utf-8")
        marker = "#define PORTABLEGL_IMPLEMENTATION"
        if text.count(marker) != 1:
            raise GraphError("unexpected PortableGL gears implementation marker")
        ensure_parent(
            context,
            glxgears_source,
            text.replace(marker, "/* LeonOS links PortableGL dynamically. */", 1),
        )

    graph.add(Target(
        name="generate:glxgears-source",
        outputs=(glxgears_source,),
        inputs=(portablegl_source / "examples/classic/gears.c",),
        kind="generate",
        action=generate_glxgears_source,
        action_key="portablegl-gears-adapter-v1",
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
        outputs=(busybox_elf, busybox_stamp, busybox_links),
        inputs=tuple([
            busybox_source_stamp,
            ROOT / "tools/build_busybox.py",
            busybox_config,
            ROOT / "tools/fetch_auth_upstream.py",
            musl_stamp,
            musl_archive,
        ]),
        depends_on=("busybox-source-revision", "musl"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_busybox.py",
            "--source",
            "third_party/busybox",
            "--config",
            "userland/busybox/leonos.config",
            "--musl-prefix",
            relative(musl_prefix),
            "--musl-lib",
            relative(musl_archive),
            "--output",
            relative(busybox_elf),
            "--stamp",
            relative(busybox_stamp),
            "--links",
            relative(busybox_links),
            *compile_option_args,
            *linker_option_args,
            "--official-source",
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
        inputs=tuple([*nano_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_nano.py",
            "--source",
            "third_party/nano",
            "--port",
            "userland/nano",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--leonos-lib",
            relative(runtime_so),
            "--musl-lib",
            relative(musl_archive),
            "--work-dir",
            relative(nano_work_dir),
            "--output",
            relative(nano_elf),
            "--stamp",
            relative(nano_stamp),
            "--dynamic",
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    graph.add(Target(
        name="fastfetch",
        outputs=(fastfetch_elf, fastfetch_stamp, fastfetch_cache),
        inputs=(ROOT / "tools/package_fastfetch.py", *((fastfetch_prebuilt,) if fastfetch_prebuilt else ())),
        kind="generate",
        command=(
            PYTHON,
            "tools/package_fastfetch.py",
            "--cache", str(fastfetch_cache),
            *(("--source", str(fastfetch_prebuilt)) if fastfetch_prebuilt else ()),
            "--output",
            relative(fastfetch_elf),
            "--stamp",
            relative(fastfetch_stamp),
        ),
    ))
    graph.add(Target(name="test-fastfetch-package", depends_on=("fastfetch",), kind="test",
                     command=(PYTHON, "tools/test_fastfetch_package.py")))

    sl_inputs = collect(
        "third_party/sl/sl.c", "third_party/sl/sl.h", "third_party/sl/LICENSE",
        "userland/sl/**/*.c", "userland/sl/**/*.h", "userland/sl/**/*.md",
        "tools/build_sl.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="sl",
        outputs=(sl_elf, sl_stamp),
        inputs=tuple([*sl_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_sl.py",
            "--source",
            "third_party/sl",
            "--port",
            "userland/sl",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--leonos-lib",
            relative(runtime_so),
            "--musl-lib",
            relative(musl_archive),
            "--work-dir",
            relative(sl_work_dir),
            "--output",
            relative(sl_elf),
            "--stamp",
            relative(sl_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    less_inputs = collect(
        "third_party/less/*.c", "third_party/less/*.h", "third_party/less/*.uni",
        "third_party/less/less.hlp", "third_party/less/lessmsg", "third_party/less/lessmsg_int",
        "third_party/less/mkhelp.py", "third_party/less/COPYING", "third_party/less/LICENSE",
        "userland/less/**/*.c", "userland/less/**/*.h", "userland/less/**/*.md",
        "tools/build_less.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="less",
        outputs=(less_elf, less_stamp),
        inputs=tuple([*less_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_less.py",
            "--source",
            "third_party/less",
            "--port",
            "userland/less",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--leonos-lib",
            relative(runtime_so),
            "--work-dir",
            relative(less_work_dir),
            "--output",
            relative(less_elf),
            "--stamp",
            relative(less_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    tcc_inputs = collect(
        "third_party/tinycc/**/*.c", "third_party/tinycc/**/*.h",
        "third_party/tinycc/**/*.S", "third_party/tinycc/**/*.def",
        "third_party/tinycc/VERSION", "third_party/tinycc/COPYING",
        "userland/tcc/**/*.c", "userland/tcc/**/*.h", "userland/tcc/**/*.md",
        "include/uapi/**/*.h",
        "tools/build_tcc.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="tcc",
        outputs=(tcc_elf, tcc_stamp),
        inputs=tuple([*tcc_inputs, ROOT / "tools/musl_link.py", static_libc_a, musl_archive, zlib_archive, libpng_archive]),
        depends_on=("musl", "archive:libc-static", "archive:zlib", "archive:libpng"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_tcc.py",
            "--source",
            "third_party/tinycc",
            "--port",
            "userland/tcc",
            "--sdk-include",
            "devtools/include",
            "--uapi-include",
            "include/uapi",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-lib",
            relative(static_libc_a),
            "--musl-lib",
            relative(musl_archive),
            "--zlib-lib",
            relative(zlib_archive),
            "--libpng-lib",
            relative(libpng_archive),
            "--zlib-source",
            "third_party/zlib",
            "--libpng-source",
            "third_party/libpng",
            "--libpng-config",
            relative(libpng_config),
            "--work-dir",
            relative(paths.out / "tcc-work"),
            "--runtime-dir",
            relative(tcc_runtime_dir),
            "--output",
            relative(tcc_elf),
            "--stamp",
            relative(tcc_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    lua_inputs = collect(
        "third_party/lua/*.c", "third_party/lua/*.h", "third_party/lua/README.md",
        "userland/lua/**/*.c", "userland/lua/**/*.h", "userland/lua/**/*.md",
        "userland/lua/LICENSE", "userland/apps/lua/lua.app.ini",
        "tools/build_lua.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="lua",
        outputs=(lua_elf, liblua_so, liblua_archive, lua_stamp),
        inputs=tuple([*lua_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj, musl_archive]),
        depends_on=("musl", "runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_lua.py",
            "--source",
            "third_party/lua",
            "--port",
            "userland/lua",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--leonos-lib",
            relative(libc_a),
            "--runtime-so",
            relative(runtime_so),
            "--musl-lib",
            relative(musl_archive),
            "--work-dir",
            relative(lua_work_dir),
            "--output",
            relative(lua_elf),
            "--library",
            relative(liblua_so),
            "--static-library",
            relative(liblua_archive),
            "--stamp",
            relative(lua_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    cmd_inputs = collect(
        "third_party/cmd/*.c", "third_party/cmd/*.h", "third_party/cmd/LICENSE",
        "userland/cmd/**/*.c", "userland/cmd/**/*.h", "userland/cmd/**/*.md",
        "tools/build_cmd.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="cmd",
        outputs=(cmd_elf, cmd_stamp),
        inputs=tuple([*cmd_inputs, ROOT / "tools/musl_link.py", libc_a, musl_archive]),
        depends_on=("musl", "archive:libc"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_cmd.py",
            "--source",
            "third_party/cmd",
            "--port",
            "userland/cmd",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--leonos-lib",
            relative(libc_a),
            "--musl-lib",
            relative(musl_archive),
            "--work-dir",
            relative(cmd_work_dir),
            "--output",
            relative(cmd_elf),
            "--stamp",
            relative(cmd_stamp),
            *compile_option_args,
            *linker_option_args,
        ),
    ))

    pleditor_inputs = collect(
        "third_party/pl_editor/src/**/*.c", "third_party/pl_editor/src/**/*.h",
        "third_party/pl_editor/LICENSE", "userland/apps/pleditor/**/*.c",
        "userland/apps/pleditor/**/*.h", "tools/build_pleditor.py", "tools/musl_link.py",
    )
    graph.add(Target(
        name="app:pleditor",
        outputs=(pleditor_elf, pleditor_stamp),
        inputs=tuple([*pleditor_inputs, ROOT / "tools/musl_link.py", runtime_so, musl_scrt_obj, musl_crti_obj]),
        depends_on=("runtime", "runtime-loader"),
        kind="compile",
        command=(
            PYTHON,
            "tools/build_pleditor.py",
            "--source",
            "third_party/pl_editor",
            "--port",
            "userland/apps/pleditor",
            "--musl-prefix",
            relative(musl_prefix),
            "--leonos-libc-include",
            "userland/libc/include",
            "--leonos-include",
            "include",
            "--generated-include",
            relative(paths.generated_include),
            "--leonos-lib",
            relative(runtime_so),
            "--musl-lib",
            relative(musl_archive),
            "--work-dir",
            relative(pleditor_work_dir),
            "--output",
            relative(pleditor_elf),
            "--stamp",
            relative(pleditor_stamp),
            "--dynamic",
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
        cxx, "-target", "x86_64-linux-musl", *build_compile_flags, "-std=c++17", "-ffreestanding",
        "-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit", "-fno-threadsafe-statics",
        "-fno-stack-protector", "-fPIC",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-DLEONOS_USE_MUSL", "-D_GNU_SOURCE", "-DSTARDUSTUI_LINUX", "-D_POSIX_C_SOURCE=200809L",
        "-nostdinc", "-nostdinc++", "-isystem", str(clang_headers), f"-I{relative(musl_prefix / 'include')}",
        "-Iuserland/stardustui/include", "-Ithird_party/stardustui/includes",
        "-Ithird_party/stardustui", "-Iuserland/libc/include", "-Iinclude/uapi", "-Iinclude",
        f"-I{relative(paths.out / 'include')}", "-Ithird_party/mbedtls/include",
        "-Ithird_party/zlib", "-Ithird_party/libpng", f"-I{relative(libpng_generated_dir)}",
        '-DMBEDTLS_CONFIG_FILE="leonos_mbedtls_config.h"', "-include", relative(autoconf),
    ]
    stardustui_objects = [
        add_compile(graph, paths, f"compile:stardustui:{relative(source)}", source,
                    "stardustui", cxxflags_stardustui,
                    (autoconf, musl_stamp, *stardustui_headers))
        for source in sorted(stardustui_sources)
    ]
    stardustui_archive = paths.out / "userland/libstardustui.a"
    graph.add(Target(
        name="archive:stardustui",
        outputs=(stardustui_archive,),
        inputs=tuple(stardustui_objects),
        depends_on=("musl",),
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
                                  (autoconf, musl_stamp, libpng_config))
        example_inputs = tuple([source, wrapper_source, *stardustui_headers, ROOT / "tools/musl_link.py", stardustui_archive, libc_a, musl_archive])
        compile_name = f"compile:app:{app}:{relative(source)}"
        obj = add_compile(graph, paths, compile_name, source,
                          f"user-{app}", cxxflags_stardustui,
                          (autoconf, musl_stamp, *stardustui_headers))
        output = paths.out / f"userland/{app}.elf"
        graph.add(Target(
            name=f"app:{app}",
            outputs=(output,),
        inputs=tuple([obj, wrapper_obj, *example_inputs, runtime_so, musl_scrt_obj, musl_crti_obj, ROOT / "tools/musl_link.py"]),
            depends_on=(compile_name, wrapper_compile_name, "archive:stardustui", "runtime", "runtime-loader"),
            implicit_inputs=(ROOT / "tools/musl_link.py",),
            kind="link",
            command=musl_link.executable(musl_prefix, output, (wrapper_obj, obj), (stardustui_archive, runtime_so), flags=build_link_flags),
        ))
        stardustui_elfs[app] = output

    app_elfs: dict[str, Path] = {}
    user_targets: list[str] = ["musl", "archive:libc", "archive:zlib", "archive:libpng",
                               "runtime", "runtime-loader", "dynlinkerror"]
    if component_enabled("file"):
        user_targets.extend(("file-magic", "file"))
    if component_enabled("sqlite"):
        user_targets.append("sqlite")
    if component_enabled("portablegl"):
        user_targets.append("portablegl")
    if component_enabled("busybox"):
        user_targets.append("busybox")
    if component_enabled("nano"):
        user_targets.append("nano")
    for package in ("ncurses", "vim", "musl-gcc", "python"):
        if component_enabled(package):
            user_targets.append(package)
    if component_enabled("fastfetch"):
        user_targets.append("fastfetch")
    if component_enabled("sl"):
        user_targets.append("sl")
    if component_enabled("less"):
        user_targets.append("less")
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
            cflags_app = (
                cflags_doom if app == "doom" else
                cflags_mp3play if app == "mp3play" else
                cflags_glxgears if app == "glxgears" else cflags_user
            )
            implicit = (autoconf, musl_stamp, libpng_config)
            if app == "glxgears":
                implicit += (glxgears_source,)
            objects.append(add_compile(graph, paths, f"compile:app:{app}:{relative(source)}", source, f"user-{app}", asflags_user if is_asm else cflags_app, implicit if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland/{app}.elf"
        app_archives = [runtime_so]
        if app == "glxgears":
            app_archives.append(portablegl_so)
        graph.add(Target(name=f"app:{app}", outputs=(output,),
                         inputs=tuple([*objects, *app_archives, musl_scrt_obj, musl_crti_obj, ROOT / "tools/musl_link.py"]),
                         implicit_inputs=(ROOT / "tools/musl_link.py",), kind="link",
                         command=musl_link.executable(musl_prefix, output, objects, app_archives, flags=build_link_flags)))
        app_elfs[app] = output
        user_targets.append(f"app:{app}")

    musl_app_targets = []
    for app, output in app_elfs.items():
        target = add_copy(graph, f"musl-app:{app}", output,
                          paths.out / f"musl/userland/{app}.elf")
        musl_app_targets.append(target.name)
    graph.add(Target(name="musl-userland", depends_on=tuple(musl_app_targets),
                     group=True, kind="aggregate"))

    # ---- 用户程序和 installer policy 程序 ----
    # 普通应用走 user_app_sources；只有自定义构建命令或特殊链接的程序才在此单独建 target。
    installer_policy_elfs: dict[str, Path] = {}
    user_targets.append("archive:installer-libc")
    for app in installer_policy_apps:
        objects = []
        for source in user_app_sources(app):
            is_asm = source.suffix == ".S"
            objects.append(add_compile(graph, paths, f"compile:installer-app:{app}:{relative(source)}", source, f"user-installer-policy-{app}", asflags_user if is_asm else cflags_installer, (
                installer_autoconf,
                musl_stamp,
            ) if not is_asm else (), kind="assemble" if is_asm else "compile"))
        output = paths.out / f"userland-installer-policy/{app}.elf"
        name = f"installer-policy:{app}"
        graph.add(Target(name=name, outputs=(output,), inputs=tuple([
            *objects,
            installer_runtime_so,
            musl_scrt_obj,
            musl_crti_obj,
        ]), implicit_inputs=(ROOT / "tools/musl_link.py",), kind="link", command=musl_link.executable(musl_prefix, output, objects, (installer_runtime_so,), flags=build_link_flags)))
        installer_policy_elfs[app] = output
        user_targets.append(name)

    # gptinit is an installer-only utility. It is built with the installer
    # runtime and copied only into the ISO live root.
    gptinit_obj = add_compile(
        graph, paths, "compile:installer-tool:gptinit", gptinit_source,
        "user-installer-tool-gptinit", cflags_installer,
        (installer_autoconf, musl_stamp),
    )
    gptinit_elf = paths.out / "userland-installer/gptinit.elf"
    graph.add(Target(
        name="installer-tool:gptinit",
        outputs=(gptinit_elf,),
        inputs=(gptinit_obj, installer_runtime_so, musl_scrt_obj, musl_crti_obj, ROOT / "tools/musl_link.py",),
        depends_on=("installer-runtime", "runtime-loader"),
        implicit_inputs=(ROOT / "tools/musl_link.py",),
        kind="link",
        command=musl_link.executable(musl_prefix, gptinit_elf, (gptinit_obj,), (installer_runtime_so,), flags=build_link_flags),
    ))

    # ---- 资源生成与 ESP staging ----
    # staging 是源码产物进入镜像前的边界。新增资源先复制/生成到 staging，
    # 由 image-vmdk/image-iso 统一打包，禁止在动作中直接修改最终镜像。
    app_icons = tuple(paths.out / f"generated/app-icons/{app}.bmp" for app in build_user_apps)
    # Every runnable image component gets one runtime manifest.  Third-party
    # API packages write the same format at install time, so the registry does
    # not need a second compiled-in application table.
    registry_apps = list(staged_user_apps)
    registry_tool_outputs = {
        "vim": vim_elf,
        "busybox": busybox_elf,
        "file": file_elf,
        "tcc": tcc_elf,
        "lua": lua_elf,
        "cmd": cmd_elf,
        "less": less_elf,
        "sl": sl_elf,
    }
    for app in registry_tool_outputs:
        if component_enabled(app, "image") and app not in registry_apps:
            registry_apps.append(app)
    registry_manifest_dir = paths.out / "generated/app-manifests"
    registry_manifest_outputs = tuple(
        registry_manifest_dir / (runtime_app_relative(app, "elf", system_apps).parent / "manifest.ini")
        for app in registry_apps
    )
    registry_manifest_inputs: list[Path] = [
        ROOT / "tools/generate_app_manifests.py", ROOT / "configs/components.toml",
    ]
    registry_manifest_inputs.extend(
        app_elfs[app] for app in staged_user_apps if app in app_elfs
    )
    registry_manifest_inputs.extend(
        output for app, output in registry_tool_outputs.items()
        if app in registry_apps
    )
    registry_manifest_inputs.extend(
        source for app in registry_apps
        for source in (
            ROOT / "userland/apps" / app / f"{app}.app.ini",
            ROOT / "userland" / app / f"{app}.app.ini",
        ) if source.is_file()
    )
    graph.add(Target(
        name="app-manifests",
        outputs=registry_manifest_outputs,
        inputs=tuple(registry_manifest_inputs),
        depends_on=("userland",),
        kind="generate",
        command=(PYTHON, "tools/generate_app_manifests.py", "--out-dir",
                 relative(registry_manifest_dir), "--apps", *registry_apps,
                 "--system-apps", *sorted(system_apps)),
        action_key="app-manifests-v1",
    ))
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
    graph.add(Target(name="window-button-icons", outputs=button_icons, inputs=(ROOT / "tools/make_window_button_icons.py",), kind="generate", command=(
        PYTHON,
        "tools/make_window_button_icons.py",
        "--out-dir",
        relative(paths.out / "generated/window-buttons"),
    )))
    user_targets += ["app-icons", "window-button-icons", "minesweeper-assets", "ui-font", "browser-font"]
    graph.add(Target(name="userland", depends_on=tuple(user_targets), group=True, kind="aggregate"))

    component_metadata = paths.out / "generated/component-selection.json"
    sdk_inputs_list: list[Path] = [
        ROOT / "tools/package_devtools.py",
        paths.out / "auth-upstream/linux-pam-build.json",
        paths.out / "auth-upstream/libxcrypt-build.json",
        ROOT / "third_party/musl/COPYRIGHT",
        ROOT / "third_party/zlib/LICENSE",
        ROOT / "third_party/libpng/LICENSE",
        ROOT / "userland/libc/include/curses.h",
        ROOT / "userland/libc/include/ncurses.h",
        musl_prefix / "include/pty.h",
        musl_prefix / "include/arpa/inet.h",
        musl_prefix / "include/netinet/in.h",
        musl_prefix / "include/sys/socket.h",
        musl_prefix / "include/sys/un.h",
        ROOT / "userland/libc/include/leonos/app.h",
        *collect("include/uapi/**/*.h"),
        *collect("include/leonos/**/*.h", "userland/libc/include/leonos/**/*.h"),
        libpng_config,
        libc_a,
        runtime_so,
        runtime_loader,
        musl_archive,
        musl_stamp,
        zlib_archive,
        libpng_archive,
        musl_scrt_obj,
        musl_crti_obj,
        ROOT / "tools/musl_link.py",
        ROOT / "tools/musl_link.py",
        *collect("devtools/**/*"),
    ]
    sdk_depends = ["musl", "auth-upstream", "archive:libc", "archive:zlib", "archive:libpng"]
    sdk_command: list[str] = [
        PYTHON,
        "tools/package_devtools.py",
        "--pam-root",
        relative(paths.out / "auth-upstream/root"),
        "--pam-build-metadata",
        relative(paths.out / "auth-upstream/linux-pam-build.json"),
        "--crypt-build-metadata",
        relative(paths.out / "auth-upstream/libxcrypt-build.json"),
        "--sdk-root",
        "devtools",
        "--leonos-lib",
        relative(libc_a),
        "--runtime-so",
        relative(runtime_so),
        "--runtime-loader",
        relative(runtime_loader),
        "--musl-lib",
        relative(musl_archive),
        "--musl-include",
        relative(musl_prefix / "include"),
        "--musl-source",
        "third_party/musl",
        "--leonos-libc-include",
        "userland/libc/include",
        "--leonos-include",
        "include/leonos",
        "--uapi-include",
        "include/uapi",
        "--zlib-lib",
        relative(zlib_archive),
        "--zlib-source",
        "third_party/zlib",
        "--libpng-lib",
        relative(libpng_archive),
        "--libpng-source",
        "third_party/libpng",
        "--libpng-config",
        relative(libpng_config),
    ]
    if component_enabled("ncurses", "sdk"):
        sdk_inputs_list.append(ncurses_stamp)
        sdk_depends.append("ncurses")
        sdk_command.extend(("--ncurses", relative(ncurses_prefix)))
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
    if component_enabled("portablegl", "sdk"):
        sdk_inputs_list.extend((portablegl_source / "LICENSE", portablegl_source / "portablegl.h",
                                portablegl_so, portablegl_archive, portablegl_stamp,
                                ROOT / "userland/libc/include/leonos/pgl.h"))
        sdk_depends.append("portablegl")
        sdk_command.extend((
            "--portablegl-lib", relative(portablegl_archive),
            "--portablegl-so", relative(portablegl_so),
            "--portablegl-source", "third_party/portablegl",
            "--portablegl-header", "userland/libc/include/leonos/pgl.h",
            "--portablegl-stamp", relative(portablegl_stamp),
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

    grub_bdf = paths.out / "generated/grub/leonos-pixel.bdf"
    graph.add(Target(
        name="grub-bdf",
        outputs=(grub_bdf,),
        inputs=(ROOT / "tools/make_grub_font.py", ROOT / "system/fonts/system.psf"),
        kind="generate",
        command=(PYTHON, "tools/make_grub_font.py", "--out", relative(grub_bdf)),
    ))
    grub_font = paths.out / "generated/grub/leonos-unicode.pf2"
    graph.add(Target(
        name="grub-font",
        outputs=(grub_font,),
        inputs=(grub_bdf,),
        depends_on=("grub-bdf",),
        kind="generate",
        command=(
            "grub-mkfont", "-n", "LeonOS Pixel", "-o", relative(grub_font), relative(grub_bdf),
        ),
    ))
    grub_efi = paths.staging / "EFI/BOOT/BOOTX64.EFI"
    # 如果是系统 GRUB 路径，使用绝对路径；否则使用相对路径
    grub_dir_arg = str(grub_efi_dir) if using_system_grub else relative(grub_efi_dir)
    graph.add(Target(name="grub-efi", outputs=(grub_efi,), inputs=(
        ROOT / "boot/grub/embedded.cfg",
        grub_efi_dir / "modinfo.sh",
    ), kind="generate", command=(
        "grub-mkstandalone",
        "-d",
        grub_dir_arg,
        "-O",
        "x86_64-efi",
        "-o",
        relative(grub_efi),
        "--modules=part_gpt fat multiboot2 normal search search_fs_file configfile echo serial terminal video video_bochs video_cirrus efi_gop efi_uga all_video font gfxterm gfxmenu",
        "boot/grub/grub.cfg=boot/grub/embedded.cfg",
    )))
    component_prune_stamp = paths.out / "generated/component-staging-prune.json"

    def prune_component_staging(context: ActionContext) -> None:
        # Remove build-owned outputs from obsolete image layouts.
        for obsolete in ("system/lib/ld-leonos.elf", "system/lib/libleonos.so.1",
                         "system/kernel.sys", "system/middlelayer.sys",
                         "usr/lib/leonos/libleonos.so.1", "etc/machine-id",
                         "usr/lib/leonos/apps/oobe", "usr/bin/oobe"):
            stale = paths.staging / obsolete
            if stale.exists() or stale.is_symlink():
                context.detail(f"remove obsolete staging path: {relative(stale)}")
                remove_staging_path(stale)
        # Remove pre-FHS staging names left by incremental builds.  These are
        # only build-owned outputs; the source repository directories of the
        # same name are untouched.
        for legacy_dir in ("boot", "system", "programs", "drivers", "docs",
                           "share", "lib64", "usr/lib64", "opt/busybox"):
            stale = paths.staging / legacy_dir
            if stale.exists() or stale.is_symlink():
                context.detail(f"remove obsolete staging directory: {relative(stale)}")
                remove_staging_path(stale)
        for recreated in ("bin", "sbin", "lib"):
            stale = paths.staging / recreated
            if stale.exists() or stale.is_symlink():
                context.detail(f"reset staging directory: {relative(stale)}")
                remove_staging_path(stale)
        for component in components:
            selected = component_enabled(component.id, "image")
            if component.kind in {"system-app", "program-app", "package-app"}:
                target_dir = paths.staging / layout.LEONOS_APPS / component.id
                if not selected:
                    if target_dir.exists():
                        context.detail(f"remove disabled component staging: {relative(target_dir)}")
                        remove_staging_path(target_dir)
                    command = paths.staging / layout.USR_BIN / component.id
                    expected = layout.relative_symlink_target(
                        f"{layout.USR_BIN}/{component.id}", str(layout.app_exec_path(component.id)))
                    if command.is_symlink() and os.readlink(command) == expected:
                        remove_staging_path(command)
                    for name in layout.tool_payload_paths(component.id):
                        stale = paths.staging / name
                        if stale.exists() or stale.is_symlink():
                            context.detail(f"remove disabled component staging: {relative(stale)}")
                            remove_staging_path(stale)
                continue
            if component.kind == "tool":
                if selected:
                    continue
                for name in layout.tool_payload_paths(component.id):
                    stale = paths.staging / name
                    if stale.exists() or stale.is_symlink():
                        context.detail(f"remove disabled component staging: {relative(stale)}")
                        remove_staging_path(stale)
        for component, library_name in (("file", "libmagic.so.1"), ("lua", "liblua.so.5"),
                                        ("sqlite", "sqlite.so.3"),
                                        ("portablegl", "libportablegl.so.1")):
            library = paths.staging / layout.USR_LIB / library_name
            keep = portablegl_image if component == "portablegl" else component_enabled(component, "image")
            if not keep and (library.exists() or library.is_symlink()):
                context.detail(f"remove disabled shared library staging: {relative(library)}")
                remove_staging_path(library)
        # The private runtime keeps its own library root.
        for name in ("libleonos.so.2", "libleonos.so.1", "kerneldebug.sys",
                     "osmlayer.manifest"):
            stale = paths.staging / layout.LEONOS_LIB / name
            if stale.exists() or stale.is_symlink():
                context.detail(f"remove obsolete private payload: {relative(stale)}")
                remove_staging_path(stale)
        for app in staged_user_apps:
            if component_enabled(app, "entry"):
                continue
            target_dir = paths.staging / layout.LEONOS_APPS / app
            for filename in (f"{app}.bmp", f"{app}.app.ini", "manifest.ini"):
                stale = target_dir / filename
                if stale.exists() or stale.is_symlink():
                    context.detail(f"remove disabled desktop entry asset: {relative(stale)}")
                    remove_staging_path(stale)
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
        inputs=(config_path, ROOT / "configs/components.toml", ROOT / "tools/leonos_layout.py"),
        depends_on=("config-sync",),
        kind="generate",
        action=prune_component_staging,
        action_key="staging-prune-musl-v9",
    ))
    esp_names = ["staging-prune", "grub-efi"]
    esp_outputs: list[Path] = [grub_efi]
    rootfs_seed = ROOT / "system/rootfs"
    rootfs_stamp = paths.out / "generated/rootfs-layout.json"
    rootfs_seed_files = tuple(sorted(p for p in rootfs_seed.rglob("*") if p.is_file()))
    rootfs_outputs = (rootfs_stamp, *(paths.staging / p.relative_to(rootfs_seed)
                                     for p in rootfs_seed_files))

    def stage_rootfs(context: ActionContext) -> None:
        layout.layout_directories(paths.staging)
        for source in rootfs_seed_files:
            destination = paths.staging / source.relative_to(rootfs_seed)
            if destination.is_symlink():
                raise GraphError(f"rootfs seed destination is a symlink: {destination}")
            destination.unlink(missing_ok=True)
            context.copy(source, destination)
            destination.chmod(0o600 if source.name in {"shadow", "gshadow"} else
                              0o440 if source.name == "sudoers" else 0o644)
        for directory in ("desktop", "documents", "downloads"):
            skeleton = paths.staging / "etc/skel" / directory
            skeleton.mkdir(parents=True, exist_ok=True)
            skeleton.chmod(0o700)
        # Retire only the known empty seed. Never overwrite populated legacy data.
        database = paths.staging / layout.VAR_LIB_LEONOS / "users.db"
        if database.is_symlink():
            raise GraphError(f"rootfs account seed is a symlink: {database}")
        if database.exists():
            if database.read_bytes() != bytes.fromhex("3253554100000000"):
                raise GraphError(f"populated legacy account database requires recovery: {database}")
            database.unlink()
        ensure_parent(context, rootfs_stamp,
                      json.dumps(layout.ROOT_DIRECTORIES, sort_keys=True) + "\n")

    graph.add(Target(name="esp:rootfs", outputs=rootfs_outputs,
                     inputs=(*rootfs_seed_files, layout.ROOTFS_CONTRACT,
                             ROOT / "tools/leonos_layout.py"),
                     depends_on=("staging-prune",), kind="generate",
                     action=stage_rootfs, action_key="rootfs-pam-shadow-v3"))
    esp_names.append("esp:rootfs")
    esp_outputs.extend(rootfs_outputs)
    auth_payload_stamp = paths.out / "generated/auth-payload.json"
    auth_payload_fdisk = paths.staging / "usr/sbin/fdisk"
    auth_payload_tools = tuple(paths.staging / name for name in
                               (*UTIL_LINUX_COMMANDS, *UTIL_LINUX_LIBRARIES))

    def stage_auth_payload(context: ActionContext) -> None:
        source_fdisk = auth_root / "usr/sbin/fdisk"
        if source_fdisk.is_symlink() or not source_fdisk.is_file():
            raise GraphError(f"upstream util-linux fdisk is missing: {source_fdisk}")
        for name in (*UTIL_LINUX_COMMANDS, *UTIL_LINUX_LIBRARIES):
            source = auth_root / name
            if not source.is_file() or not source.resolve().is_relative_to(auth_root.resolve()):
                raise GraphError(f"upstream util-linux payload is missing or escapes its root: {source}")
        for source in auth_root.rglob("*"):
            if source.relative_to(auth_root).parts[0] not in {"bin", "sbin", "lib", "usr"}:
                continue
            destination = paths.staging / source.relative_to(auth_root)
            if destination.is_symlink() or (source.is_symlink() and destination.exists()):
                remove_staging_path(destination)
        for directory in ("bin", "sbin", "lib", "usr"):
            shutil.copytree(auth_root / directory, paths.staging / directory,
                            symlinks=True, dirs_exist_ok=True)
        # Reviewable product policy overrides upstream example configurations.
        for source in (auth_root / "etc").rglob("*"):
            if source.is_file() and not (rootfs_seed / source.relative_to(auth_root)).exists():
                destination = paths.staging / source.relative_to(auth_root)
                if destination.is_symlink():
                    raise GraphError(f"authentication configuration symlink: {destination}")
                destination.unlink(missing_ok=True)
                context.copy(source, destination)
        for directory, mode in (("etc/sudoers.d", 0o750), ("var/lib/sudo", 0o700),
                                ("run/sudo", 0o711), ("run/sudo/ts", 0o700)):
            destination = paths.staging / directory
            if destination.is_symlink():
                raise GraphError(f"authentication directory symlink: {destination}")
            destination.mkdir(parents=True, exist_ok=True)
            destination.chmod(mode)
        for obsolete in ("usr/bin/authd", "usr/lib/leonos/apps/authd"):
            remove_staging_path(paths.staging / obsolete)
        for name in ("usr/bin/sudo", "usr/bin/passwd", "bin/su", "sbin/unix_chkpwd"):
            (paths.staging / name).chmod(0o4755)
        if not auth_payload_fdisk.is_file():
            raise GraphError(f"upstream util-linux fdisk is missing: {auth_payload_fdisk}")
        ensure_parent(context, auth_payload_stamp, json.dumps({
            "authority": "passwd/shadow/group/gshadow", "execution": "upstream sudo/su",
            "manifest": json.loads((ROOT / "configs/auth-upstream.json").read_text()),
        }, indent=2) + "\n")

    graph.add(Target(name="esp:auth", outputs=(auth_payload_stamp, *auth_payload_tools),
                     inputs=(ROOT / "configs/auth-upstream.json", *rootfs_seed_files,
                             auth_root / "usr/sbin/fdisk"),
                     depends_on=("auth-upstream", "esp:rootfs"), kind="generate", always=True,
                     action=stage_auth_payload, action_key="upstream-auth-production-v1"))
    esp_names.append("esp:auth")
    esp_outputs.extend((auth_payload_stamp, *auth_payload_tools))
    storage_payload_stamp = paths.out / "generated/storage-payload.json"
    storage_payload_files = tuple(paths.staging / name for name in FORMATTER_COMMANDS)
    boot_copy_script = paths.staging / "usr/sbin/leonos-grub-installer"

    def stage_storage_payload(context: ActionContext) -> None:
        stage_filesystems(storage_root, paths.staging)
        source = ROOT / "userland/storage/leonos-grub-installer"
        context.copy(source, boot_copy_script)
        boot_copy_script.chmod(0o755)
        ensure_parent(context, storage_payload_stamp, storage_package.read_text())

    graph.add(Target(name="esp:storage", outputs=(storage_payload_stamp, boot_copy_script, *storage_payload_files),
                     inputs=(storage_package, ROOT / "tools/storage_tools.py",
                             ROOT / "userland/storage/leonos-grub-installer"),
                     depends_on=("storage-upstream", "esp:rootfs"), kind="generate",
                     action=stage_storage_payload, action_key="official-storage-v1", always=True))
    esp_names.append("esp:storage")
    esp_outputs.extend((storage_payload_stamp, boot_copy_script, *storage_payload_files))
    terminal_payload_stamp = paths.out / "generated/terminal-payload.json"

    def stage_terminal_packages(context: ActionContext) -> None:
        for package, prefix, directories in (
            ("ncurses", ncurses_prefix, ("bin", "share/terminfo", "share/licenses/ncurses")),
            ("vim", vim_prefix, ("share/vim",)),
        ):
            if not component_enabled(package, "image"):
                continue
            for directory in directories:
                destination = paths.staging / layout.USR / directory
                if directory == "bin":
                    # /usr/bin is shared with other independent staging jobs.
                    for source in sorted((prefix / directory).iterdir()):
                        entry = destination / source.name
                        remove_staging_path(entry)
                        copy_tree_preserving_links(source, entry)
                else:
                    remove_staging_path(destination)
                    copy_tree_preserving_links(prefix / directory, destination)
        if component_enabled("vim", "image"):
            destination = paths.staging / layout.USR_BIN / "vim"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(vim_elf, destination)
            license_destination = paths.staging / layout.LICENSES / "vim" / "LICENSE"
            license_destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "third_party/vim/LICENSE", license_destination)
        ensure_parent(context, terminal_payload_stamp,
                      json.dumps({name: component_enabled(name, "image")
                                  for name in ("vim", "ncurses", "busybox")}) + "\n")
    gcc_payload_stamp = paths.out / "generated/musl-gcc-payload.json"

    def stage_gcc_package(context: ActionContext) -> None:
        enabled = component_enabled("musl-gcc", "image")
        for name in (layout.OPT_DYNE, f"{layout.LICENSES}/musl-gcc",
                     f"{layout.EXAMPLES}/musl-gcc"):
            destination = paths.staging / name
            remove_staging_path(destination)
            if enabled:
                copy_tree_preserving_links(gcc_package / name, destination)
        ensure_parent(context, gcc_payload_stamp, json.dumps({"musl-gcc": enabled}) + "\n")

    gcc_payload_outputs = (gcc_payload_stamp,)
    if component_enabled("musl-gcc", "image"):
        gcc_payload_outputs += (paths.staging / layout.OPT_DYNE,
                                paths.staging / layout.LICENSES / "musl-gcc",
                                paths.staging / layout.EXAMPLES / "musl-gcc")
    graph.add(Target(name="esp:musl-gcc", outputs=gcc_payload_outputs,
                     inputs=(config_path, ROOT / "configs/components.toml",
                             *((gcc_stamp,) if component_enabled("musl-gcc", "image") else ())),
                     depends_on=("esp:terminal-packages",), kind="generate",
                     action=stage_gcc_package, action_key="musl-gcc-payload-v2"))
    esp_names.append("esp:musl-gcc")
    esp_outputs.extend(gcc_payload_outputs)
    python_payload_stamp = paths.out / "generated/python-payload.json"

    def stage_python_package(context: ActionContext) -> None:
        enabled = component_enabled("python", "image")
        for name in python_payload_paths:
            destination = paths.staging / name
            remove_staging_path(destination)
            if enabled:
                copy_tree_preserving_links(python_package / name, destination)
        ensure_parent(context, python_payload_stamp, json.dumps({"python": enabled}) + "\n")

    python_payload_outputs = (python_payload_stamp,)
    if component_enabled("python", "image"):
        python_payload_outputs += tuple(paths.staging / name for name in python_payload_paths)
    graph.add(Target(name="esp:python", outputs=python_payload_outputs,
                     inputs=(config_path, ROOT / "configs/components.toml",
                             *((python_stamp,) if component_enabled("python", "image") else ())),
                     depends_on=("esp:musl-gcc",), kind="generate",
                     action=stage_python_package, action_key="python-payload-v2"))
    esp_names.append("esp:python")
    esp_outputs.extend(python_payload_outputs)
    grub_font_destination = paths.staging / "grub/fonts/leonos-unicode.pf2"
    target = add_copy(graph, "esp:grub-font", grub_font, grub_font_destination)
    esp_names.append(target.name)
    esp_outputs.append(grub_font_destination)
    grub_theme = ROOT / "boot/grub/theme/theme.txt"
    grub_theme_destination = paths.staging / "grub/theme/theme.txt"
    target = add_copy(graph, "esp:grub-theme", grub_theme, grub_theme_destination)
    esp_names.append(target.name)
    esp_outputs.append(grub_theme_destination)
    for (
        source,
        destination_rel,
    ) in [
        (ROOT / "boot/grub/grub.cfg", "grub/grub.cfg"),
        (loader_elf, "loader.elf"),
        (kernel_sys, "leonos/kernel.sys"),
        (middle_sys, "leonos/middlelayer.sys"),
    ]:
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    kerneldebug_destination = paths.staging / layout.LEONOS_LIB / "kerneldebug.sys"
    target = add_copy(graph, "esp:kerneldebug", kerneldebug_sys, kerneldebug_destination)
    esp_names.append(target.name)
    esp_names.append("kerneldebug-module")
    esp_outputs.append(kerneldebug_destination)
    manifest = paths.staging / layout.LEONOS_LIB / "osmlayer.manifest"
    graph.add(Target(name="esp:manifest", outputs=(manifest,), kind="generate", action=text_action(manifest, "name=osmlayer\nabi=2\nroot=/\nfs=ext2\ngui=desktop.elf\n"), action_key="manifest-v6-ext2"))
    esp_names.append("esp:manifest")
    esp_outputs.append(manifest)
    for source, destination_rel in ((runtime_loader, f"{layout.LIB}/ld-musl-x86_64.so.1"),
                                    (musl_lib, f"{layout.LIB}/libc.so"),
                                    (mimalloc_lib, f"{layout.LIB}/libmimalloc.so.3"),
                                    (runtime_so, f"{layout.LEONOS_LIB}/libleonos.so.2")):
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for name in ("musl", "mimalloc"):
        source = musl_prefix / f"share/licenses/{name}" / ("COPYRIGHT" if name == "musl" else "LICENSE")
        destination = paths.staging / layout.LICENSES / name / "LICENSE"
        target = add_copy(graph, f"esp:license:{name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    musl_search_path = paths.staging / "etc/ld-musl-x86_64.path"
    graph.add(Target(name="esp:musl-search-path", outputs=(musl_search_path,),
                     kind="generate", action=text_action(musl_search_path,
                         "/lib:/usr/local/lib:/usr/lib:/usr/lib/leonos\n"),
                     action_key="musl-search-path-fhs-v1"))
    esp_names.append("esp:musl-search-path")
    esp_outputs.append(musl_search_path)
    for component, source, filename in (
        ("file", libmagic_so, "libmagic.so.1"),
        ("lua", liblua_so, "liblua.so.5"),
        ("sqlite", sqlite_so, "sqlite.so.3"),
        ("portablegl", portablegl_so, "libportablegl.so.1"),
    ):
        enabled = portablegl_image if component == "portablegl" else component_enabled(component, "image")
        if not enabled:
            continue
        destination = paths.staging / layout.USR_LIB / filename
        target = add_copy(graph, f"esp:lib:{filename}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    dynlinkerror_destination = paths.staging / runtime_app_relative("dynlinkerror", "elf", system_apps)
    target = add_copy(graph, "esp:dynlinkerror", dynlinkerror_elf, dynlinkerror_destination)
    esp_names.append(target.name)
    esp_outputs.append(dynlinkerror_destination)
    for source_rel, destination_rel in SYSTEM_FILES:
        source = ROOT / source_rel
        destination = paths.staging / destination_rel
        target = add_copy(graph, f"esp:{destination_rel}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    ui_metro_font_destination = paths.staging / layout.LEONOS_FONTS / "leonos-metro.ttf"
    ui_win95_font_destination = paths.staging / layout.LEONOS_FONTS / "leonos-win95.ttf"
    browser_font_destination = paths.staging / layout.LEONOS_FONTS / "times-new-roman.ttf"
    browser_cjk_font_destination = paths.staging / layout.LEONOS_FONTS / "simsun.ttc"

    def sync_ui_font(context: ActionContext) -> None:
        for legacy_name in ("system.psf", "cjk16.lbf", "metro-latin.lbf", "leonos.lbf", "leonos-ui.ttf"):
            context.detail(f"remove obsolete font: {relative(ui_metro_font_destination.parent / legacy_name)}")
            (ui_metro_font_destination.parent / legacy_name).unlink(missing_ok=True)
        ui_metro_font_destination.parent.mkdir(parents=True, exist_ok=True)
        context.copy(ui_metro_font, ui_metro_font_destination)
        context.copy(ui_win95_font, ui_win95_font_destination)
        context.copy(browser_font, browser_font_destination)
        context.copy(browser_cjk_font_source, browser_cjk_font_destination)
        context.copy(ROOT / "system/fonts/system.psf", psf_font_destination)

    psf_font_destination = paths.staging / layout.LEONOS_FONTS / "system.psf"
    target = graph.add(Target(name="esp:system-font", outputs=(ui_metro_font_destination, ui_win95_font_destination,
                                                                 browser_font_destination, browser_cjk_font_destination,
                                                                 psf_font_destination),
                              inputs=(ui_metro_font, ui_win95_font, browser_font, browser_cjk_font_source,
                                      ROOT / "system/fonts/system.psf"), kind="generate", action=sync_ui_font,
                              action_key="sync-ui-font-v10"))
    esp_names.append(target.name)
    esp_outputs.extend((ui_metro_font_destination, ui_win95_font_destination,
                        browser_font_destination, browser_cjk_font_destination,
                        psf_font_destination))
    if any(component_enabled(app, "image") for app in stardustui_apps):
        for source in stardustui_theme_files:
            destination = paths.staging / "etc/stardustui/theme" / source.name
            target = add_copy(graph, f"esp:stardustui-theme:{source.stem}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    for source, icon in zip(button_icons, WINDOW_BUTTON_ICONS):
        destination = paths.staging / layout.LEONOS_RESOURCES / icon
        target = add_copy(graph, f"esp:window-icon:{icon}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for source, name in zip(minesweeper_assets, MINESWEEPER_ASSETS):
        destination = paths.staging / layout.LEONOS_RESOURCES / name
        target = add_copy(graph, f"esp:minesweeper-asset:{name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    for driver, source in zip(DRIVER_MODULES, driver_outputs):
        destination = paths.staging / layout.LEONOS_DRIVERS / f"{driver}.drv"
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
    for app in registry_apps:
        source = registry_manifest_dir / (runtime_app_relative(app, "elf", system_apps).parent / "manifest.ini")
        destination = paths.staging / source.relative_to(registry_manifest_dir)
        target = add_copy(graph, f"esp:manifest:{app}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    if component_enabled("leonmmcoset", "image"):
        source = ROOT / "userland/apps/leonmmcoset/leonmmcoset.png"
        destination = paths.staging / runtime_app_relative("leonmmcoset", "png", system_apps)
        target = add_copy(graph, "esp:asset:leonmmcoset", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    if component_enabled("xiaobai", "image"):
        source = ROOT / "userland/apps/xiaobai/xiaobai.png"
        destination = paths.staging / runtime_app_relative("xiaobai", "png", system_apps)
        target = add_copy(graph, "esp:asset:xiaobai", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    terminal_inputs = tuple(stamp for name, stamp in
                            (("vim", vim_stamp), ("ncurses", ncurses_stamp), ("busybox", busybox_elf))
                            if component_enabled(name, "image"))
    terminal_outputs = [terminal_payload_stamp]
    if component_enabled("vim", "image"):
        terminal_outputs.extend(paths.staging / name for name in
                                (f"{layout.USR_BIN}/vim", f"{layout.LICENSES}/vim/LICENSE",
                                 f"{layout.USR_SHARE}/vim/vim91/defaults.vim"))
    if component_enabled("ncurses", "image"):
        terminal_outputs.extend(paths.staging / name for name in
                                (layout.TERMINFO, f"{layout.USR_BIN}/infocmp",
                                 f"{layout.USR_BIN}/tput",
                                 f"{layout.LICENSES}/ncurses/COPYING"))
    graph.add(Target(name="esp:terminal-packages", outputs=tuple(terminal_outputs),
                     inputs=(config_path, ROOT / "configs/components.toml", *terminal_inputs),
                     depends_on=("staging-prune",), kind="generate",
                     action=stage_terminal_packages,
                     action_key="terminal-packages-v4-alpine"))
    esp_names.append("esp:terminal-packages")
    esp_outputs.extend(terminal_outputs)
    if component_enabled("busybox", "image"):
        busybox_destination = paths.staging / layout.BIN / "busybox"
        target = add_copy(graph, "esp:busybox", busybox_elf, busybox_destination)
        esp_names.append(target.name)
        esp_outputs.append(busybox_destination)
        busybox_license = paths.staging / layout.LICENSES / "busybox" / "LICENSE"
        target = add_copy(graph, "esp:busybox:LICENSE", ROOT / "third_party/busybox/LICENSE",
            busybox_license)
        esp_names.append(target.name)
        esp_outputs.append(busybox_license)
    if component_enabled("file", "image"):
        file_destination = paths.staging / layout.USR_BIN / "file"
        target = add_copy(graph, "esp:file", file_elf, file_destination)
        esp_names.append(target.name)
        esp_outputs.append(file_destination)
        file_license_destination = paths.staging / layout.LICENSES / "file" / "COPYING"
        target = add_copy(graph, "esp:file:COPYING", file_source / "COPYING",
            file_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(file_license_destination)
        magic_destination = paths.staging / layout.MISC / "magic.mgc"
        target = add_copy(graph, "esp:file:magic.mgc", magic_database, magic_destination)
        esp_names.append(target.name)
        esp_outputs.append(magic_destination)
    if component_enabled("tcc", "image"):
        tcc_destination = paths.staging / layout.OPT_TCC

        def sync_tcc_runtime(context: ActionContext) -> None:
            remove_staging_path(tcc_destination)
            context.detail(
                f"copy runtime tree: {relative(tcc_runtime_dir)} -> {relative(tcc_destination)}"
            )
            copy_tree_preserving_links(tcc_runtime_dir, tcc_destination)
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
            action_key="sync-tcc-runtime-v3",
        ))
        # Replacing the runtime directory must precede its separately owned entries.
        for staged_target in graph.targets.values():
            if staged_target is not target and any(
                output.is_relative_to(tcc_destination) for output in staged_target.outputs):
                staged_target.depends_on += (target.name,)
        esp_names.append(target.name)
        esp_outputs.extend((tcc_destination / "tcc.elf", tcc_destination / "lib/libtcc1.a",
            tcc_destination / "lib/libleonos-tcc-rt.a",
            tcc_destination / "tcc.app.ini"))
        tcc_license = paths.staging / layout.LICENSES / "tcc" / "COPYING"
        target = add_copy(graph, "esp:tcc:COPYING", ROOT / "third_party/tinycc/COPYING",
            tcc_license)
        esp_names.append(target.name)
        esp_outputs.append(tcc_license)
    if component_enabled("lua", "image"):
        lua_destination = paths.staging / layout.OPT_LUA / "lua.elf"
        target = add_copy(graph, "esp:lua:lua.elf", lua_elf, lua_destination)
        esp_names.append(target.name)
        esp_outputs.append(lua_destination)
        lua_license_destination = paths.staging / layout.LICENSES / "lua" / "LICENSE"
        target = add_copy(graph, "esp:lua:LICENSE", lua_port / "LICENSE",
            lua_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(lua_license_destination)
    if component_enabled("cmd", "image"):
        cmd_destination = paths.staging / layout.OPT_CMD / "cmd.elf"
        target = add_copy(graph, "esp:cmd:cmd.elf", cmd_elf, cmd_destination)
        esp_names.append(target.name)
        esp_outputs.append(cmd_destination)
        for source in (cmd_source / "LICENSE", cmd_port / "README.md"):
            destination = (paths.staging / layout.LICENSES / "cmd" / source.name
                if source == cmd_source / "LICENSE"
                else paths.staging / layout.OPT_CMD / source.name)
            target = add_copy(graph, f"esp:cmd:{destination.name}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("nano", "image"):
        nano_license_destination = paths.staging / layout.LICENSES / "nano" / "COPYING"
        target = add_copy(graph, "esp:nano:COPYING", ROOT / "third_party/nano/COPYING",
            nano_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(nano_license_destination)
    if component_enabled("fastfetch", "image"):
        fastfetch_license_destination = paths.staging / layout.LICENSES / "fastfetch" / "LICENSE"
        target = add_copy(graph, "esp:fastfetch:LICENSE", fastfetch_license,
            fastfetch_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(fastfetch_license_destination)
        fastfetch_config_destination = paths.staging / "etc/fastfetch/config.jsonc"
        target = add_copy(graph, "esp:fastfetch:config", ROOT / "userland/fastfetch/config.jsonc",
            fastfetch_config_destination)
        esp_names.append(target.name)
        esp_outputs.append(fastfetch_config_destination)
        fastfetch_manifest_destination = paths.staging / layout.LICENSES / "fastfetch" / "package.json"
        target = add_copy(graph, "esp:fastfetch:package", fastfetch_stamp, fastfetch_manifest_destination)
        esp_names.append(target.name)
        esp_outputs.append(fastfetch_manifest_destination)
    if component_enabled("sl", "image"):
        sl_destination = paths.staging / layout.USR_BIN / "sl"
        target = add_copy(graph, "esp:sl", sl_elf, sl_destination)
        esp_names.append(target.name)
        esp_outputs.append(sl_destination)
        sl_license_destination = paths.staging / layout.LICENSES / "sl" / "LICENSE"
        target = add_copy(graph, "esp:sl:LICENSE", sl_source / "LICENSE",
            sl_license_destination)
        esp_names.append(target.name)
        esp_outputs.append(sl_license_destination)
    if component_enabled("less", "image"):
        less_destination = paths.staging / layout.USR_BIN / "less"
        target = add_copy(graph, "esp:less", less_elf, less_destination)
        esp_names.append(target.name)
        esp_outputs.append(less_destination)
        for source in (less_source / "COPYING", less_source / "LICENSE", less_port / "README.md"):
            destination = (paths.staging / layout.LICENSES / "less" / source.name
                if source != less_port / "README.md"
                else paths.staging / layout.USR_SHARE / "doc/leonos/less-README.md")
            target = add_copy(graph, f"esp:less:{destination.name}", source, destination)
            esp_names.append(target.name)
            esp_outputs.append(destination)
    if component_enabled("pleditor", "image"):
        pleditor_license_destination = paths.staging / layout.LICENSES / "pleditor" / "LICENSE"
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
            command=(PYTHON, "tools/build_api.py",
                     "--name", "Hello World", "--id", "helloworld",
                     "--version", "1.0.0", "--category", "Developer applications",
                     "--main-exe", "helloworld.elf",
                     "--default-path", layout.app_package_dir_abs("helloworld"),
                     "--requires-admin", "--desktop-shortcut",
                     "--file", relative(app_elfs["helloworld"]), "helloworld.elf",
                     "--output", relative(helloworld_api)),
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
                "--id", "doom",
                "--version", "1.0.0-freedoom",
                "--category", "Games",
                "--main-exe", "doomlauncher.elf",
                "--default-path", layout.app_package_dir_abs("doom"),
                "--commands", "doom,doomlauncher",
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
                "--id", "oschinpt",
                "--version", "1.0.0",
                "--category", "Input methods",
                "--main-exe", "oschinpt.elf",
                "--default-path", layout.app_package_dir_abs("oschinpt"),
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
    config_destination = paths.staging / layout.ETC_LEONOS / "leonos.conf"
    target = add_copy(graph, "esp:config", config_path, config_destination)
    esp_names.append(target.name)
    esp_outputs.append(config_destination)
    for source in collect("system/config/*"):
        if source.name == "display.conf":
            continue
        destination = paths.staging / layout.ETC_LEONOS / source.name
        target = add_copy(graph, f"esp:config:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    display_destination = paths.staging / layout.ETC_LEONOS / "display.conf"
    target = add_copy(graph, "esp:config:display.conf", generated_display_config, display_destination)
    esp_names.append(target.name)
    esp_outputs.append(display_destination)
    boot_display_destination = paths.staging / layout.ESP_DISPLAY_CONF.lstrip("/")
    target = add_copy(graph, "esp:boot-display.conf", generated_display_config, boot_display_destination)
    esp_names.append(target.name)
    esp_outputs.append(boot_display_destination)
    entry_policy_destination = paths.staging / layout.ETC_LEONOS / "desktop-entries.conf"
    target = add_copy(graph, "esp:config:desktop-entries.conf", desktop_entry_policy,
                      entry_policy_destination)
    esp_names.append(target.name)
    esp_outputs.append(entry_policy_destination)
    for source in collect("system/docs/*.hlp"):
        destination = paths.staging / layout.LEONOS_DOC / source.name
        target = add_copy(graph, f"esp:doc:{source.name}", source, destination)
        esp_names.append(target.name)
        esp_outputs.append(destination)
    # ---- standard root links and command entries ----
    layout_stamp = paths.out / "generated/layout-links.json"
    layout_link_map: dict[str, str] = {}
    for link, target in layout.root_symlink_entries():
        layout_link_map[link] = target
    for link, target in layout.builtin_command_links(
        lambda package: component_enabled(package, "image")):
        layout_link_map[link] = target
    for command in ("sudo", "sudoedit", "passwd", "su"):
        layout_link_map.pop(f"usr/bin/{command}", None)
    layout_link_map["usr/bin/su"] = "../../bin/su"
    # Keep old system-tool paths usable without a second binary or BusyBox
    # dispatch. util-linux --sbindir now installs both under /usr/sbin.
    layout_link_map.update(COMPAT_LINKS)
    for app in staged_user_apps:
        if app in {"sudo", "su"}:
            continue
        link = f"{layout.USR_BIN}/{app}"
        layout_link_map[link] = layout.relative_symlink_target(
            link, str(layout.app_exec_path(app))
        )
    def stage_layout_links(context: ActionContext) -> None:
        layout.layout_directories(paths.staging)
        links = dict(layout_link_map)
        if component_enabled("busybox", "image"):
            # Generated by upstream mkll from the actual compiled config.
            for line in busybox_links.read_text(encoding="ascii").splitlines():
                link = line.removeprefix("/")
                entry = Path(link)
                if (not line.startswith("/") or entry.parent.as_posix() not in
                        {"bin", "sbin", "usr/bin", "usr/sbin"} or entry.name in {".", "..", "busybox"}):
                    raise GraphError(f"invalid BusyBox applet path: {line!r}")
                if link in links:
                    continue
                destination = paths.staging / link
                target = layout.relative_symlink_target(link, "bin/busybox")
                # A separately packaged command (e.g. ncurses clear) wins.
                if destination.exists() and not destination.is_symlink():
                    continue
                if destination.is_symlink() and os.readlink(destination) != target:
                    continue
                links[link] = target
        if layout_stamp.is_file():
            previous = json.loads(layout_stamp.read_text(encoding="utf-8"))
            for link, target in previous.items():
                destination = paths.staging / link
                if link not in links and destination.is_symlink() and os.readlink(destination) == target:
                    remove_staging_path(destination)
        for link, target in sorted(links.items()):
            destination = paths.staging / link
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.is_symlink() and os.readlink(destination) == target:
                continue
            if destination.exists() or destination.is_symlink():
                context.detail(f"replace staging path with symlink: {relative(destination)}")
                remove_staging_path(destination)
            else:
                context.detail(f"create staging symlink: {relative(destination)}")
            os.symlink(target, destination)
        ensure_parent(
            context,
            layout_stamp,
            json.dumps(links, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        )

    layout_outputs = (layout_stamp, *(paths.staging / link for link in layout_link_map))
    graph.add(Target(name="esp:layout-links", outputs=layout_outputs,
                     inputs=(config_path, ROOT / "configs/components.toml", layout.ROOTFS_CONTRACT,
                             *((busybox_links,) if component_enabled("busybox", "image") else ()),
                             ROOT / "tools/leonos_layout.py"),
                     depends_on=tuple(esp_names), kind="generate",
                     action=stage_layout_links, action_key="layout-links-v5-storage", always=True))
    esp_names.append("esp:layout-links")
    esp_outputs.extend(layout_outputs)
    # staging-prune removes obsolete pre-FHS trees before any staging producer
    # runs.  Producers created by add_copy() do not declare that dependency
    # individually, so add it centrally here rather than duplicating it at
    # dozens of call sites.
    for staged_target in graph.targets.values():
        if staged_target.name in {"staging-prune", "esp", "esp:layout-links", "esp:rootfs"} or staged_target.group:
            continue
        if any(output.is_relative_to(paths.staging) for output in staged_target.outputs):
            if "esp:rootfs" not in staged_target.depends_on:
                staged_target.depends_on += ("esp:rootfs",)
    graph.add(Target(name="esp", depends_on=tuple(esp_names), group=True, kind="aggregate"))

    # ---- 磁盘镜像、ISO、安装器和运行目标 ----
    # image-* 只消费 staging；分区大小、文件系统或 GRUB 参数的变更应同步更新
    # 对应 tools 脚本的 inputs，确保增量缓存能感知脚本变化。
    vmdk = paths.images / "leonos4.vmdk"
    raw = paths.images / "leonos4.raw"
    esp_fat = paths.images / "esp.fat"
    root_ext2 = paths.images / "root.ext2"
    test_account_inputs = (ROOT / "tools/image_test_accounts.py",
                           *sorted((ROOT / "system/test-accounts").glob("*")))
    vmdk_language = "zh" if config_bool(values, "CONFIG_VMDK_DEFAULT_LANGUAGE_ZH") else "en"
    graph.add(Target(name="image-vmdk", outputs=(vmdk, raw, esp_fat, root_ext2),
                     inputs=tuple([*esp_outputs, config_path, ROOT / "tools/make_image.py", ROOT / "tools/make_ext2_root.py",
                                    layout.ROOTFS_CONTRACT, ROOT / "tools/leonos_layout.py", *test_account_inputs]),
                     depends_on=("esp",), kind="generate", command=(PYTHON, "tools/make_image.py", "--out",
                     relative(vmdk), "--raw", relative(raw), "--esp-tree",
                     relative(paths.staging), "--esp-image", relative(esp_fat),
                     "--root-image", relative(root_ext2), "--root-fs", "ext2", "--default-language", vmdk_language,
                     "--size-mib", str(max(config_int(values, "CONFIG_IMAGE_SIZE_MIB"),
                                           2048 if component_enabled("python", "image") else 0,
                                           1024 if component_enabled("musl-gcc", "image") else 0)))))

    iso = paths.images / "leonos4.iso"
    iso_stage = paths.out / "iso"
    desktop_root = paths.out / "live/root.ext2"
    graph.add(Target(name="desktop-live-root", outputs=(desktop_root,),
                     inputs=(*esp_outputs, ROOT / "tools/make_live_root.py",
                             ROOT / "tools/make_installer_root.py",
                             ROOT / "tools/make_ext2_root.py", layout.ROOTFS_CONTRACT, ROOT / "tools/leonos_layout.py", ROOT / "tools/make_image.py", *test_account_inputs),
                     depends_on=("esp",), kind="generate", command=(
                         PYTHON, "tools/make_live_root.py", "--tree", relative(paths.staging),
                         "--out", relative(desktop_root))))
    graph.add(Target(name="image-iso", outputs=(iso,), inputs=(desktop_root, loader_elf,
                     kernel_sys, middle_sys, grub_font, ROOT / "boot/grub/live.cfg",
                     ROOT / "boot/grub/installer_embedded.cfg", ROOT / "tools/make_installer_iso.py"),
                     kind="generate", command=(PYTHON, "tools/make_installer_iso.py",
                         "--out", relative(iso), "--stage", relative(iso_stage),
                         "--boot-image", relative(paths.out / "live/efiboot.img"),
                         "--loader", relative(loader_elf), "--kernel", relative(kernel_sys),
                         "--middlelayer", relative(middle_sys), "--installer-root", relative(desktop_root),
                         "--grub-font", relative(grub_font), "--work-dir", relative(paths.out / "live/work"),
                         "--grub-efi-dir", grub_dir_arg, "--grub-config", "boot/grub/live.cfg", "--bios")))

    installer_root = paths.out / "install/root.fat"
    installer_stage = paths.out / "install/root"
    graph.add(Target(name="installer-root", outputs=(installer_root,), inputs=tuple([
        *esp_outputs,
        app_elfs["desktop"],
        app_elfs["installer"],
        busybox_elf,
        gptinit_elf,
        installer_runtime_so,
        *(installer_policy_elfs.values()),
        ROOT / "tools/make_installer_root.py",
        ROOT / "tools/image_test_accounts.py",
        ROOT / "tools/make_ext2_root.py", layout.ROOTFS_CONTRACT, ROOT / "tools/leonos_layout.py",
    ]), depends_on=(
        "esp",
        "installer-runtime",
        "busybox",
        "installer-tool:gptinit",
    ), kind="generate", command=(
        PYTHON,
        "tools/make_installer_root.py",
        "--out",
        relative(installer_root),
        "--stage",
        relative(installer_stage),
        "--esp-tree",
        relative(paths.staging),
        "--installed-policy-dir",
        relative(paths.out / "userland-installer-policy"),
        "--policy-runtime",
        relative(installer_runtime_so),
        "--policy-apps",
        *installer_policy_apps,
        "--userland-dir",
        relative(paths.out / "userland"),
        "--gptinit",
        relative(gptinit_elf),
        "--generated-icons-dir",
        relative(paths.out / "generated/app-icons"),
        "--size-mib",
        str(config_int(values, "CONFIG_INSTALLER_ROOT_SIZE_MIB")),
    )))
    installer_iso = paths.images / "leonos4-installer.iso"
    installer_boot_image = paths.out / "install/installer-efiboot.img"
    graph.add(Target(name="installer-image", outputs=(
        installer_iso,
        installer_boot_image,
    ), inputs=(
        loader_elf,
        kernel_sys,
        middle_sys,
        installer_root,
        grub_font,
        grub_theme,
        grub_efi_dir / "modinfo.sh",
        ROOT / "boot/grub/installer.cfg",
        ROOT / "boot/grub/installer_embedded.cfg",
        ROOT / "tools/make_installer_iso.py",
    ), kind="generate", command=(
        PYTHON,
        "tools/make_installer_iso.py",
        "--out",
        relative(installer_iso),
        "--stage",
        relative(paths.out / "installer-iso"),
        "--boot-image",
        relative(installer_boot_image),
        "--loader",
        relative(loader_elf),
        "--kernel",
        relative(kernel_sys),
        "--middlelayer",
        relative(middle_sys),
        "--installer-root",
        relative(installer_root),
        "--grub-font",
        relative(grub_font),
        "--work-dir",
        relative(paths.out / "install"),
        "--grub-efi-dir",
        grub_dir_arg,
    )))

    # Explicit checkpoints keep the validated live musl processes separate
    # from the still-legacy installation payload and normal distribution.
    for suffix, diagnostic, extra_args, extra_deps in (
        ("installer", ROOT / "boot/grub/installer.cfg", (), ()),
        ("probes", ROOT / "tools/tests/musl-installer-grub.cfg", ("--abi-probes",), ("musl-probes",)),
        ("ltp", ROOT / "tools/tests/ltp-installer-grub.cfg",
         ("--ltp", relative(paths.out / "musl/ltp")), ("musl-ltp",)),
    ):
        stage = paths.out / f"musl/checkpoint-{suffix}"
        checkpoint_root = stage / "install/root.fat"
        root_target = f"musl-checkpoint-root:{suffix}"
        live_apps = tuple(paths.out / f"musl/userland/{app}.elf"
                          for app in ("imd", "windowd", "desktop", "installer"))
        diagnostic_inputs = (
            tuple(paths.out / f"musl/tests/musl-abi-{mode}.elf" for mode in ("static", "dynamic"))
            if suffix == "probes" else
            tuple(paths.out / f"musl/ltp/{name}.elf" for name in ltp_programs)
            if suffix == "ltp" else ()
        )
        graph.add(Target(name=root_target, outputs=(checkpoint_root,),
                         inputs=(installer_root, musl_runtime_so, musl_lib, mimalloc_lib,
                                 *live_apps, *diagnostic_inputs, ROOT / "tools/make_musl_checkpoint.py"),
                         depends_on=extra_deps, kind="generate",
                         command=(PYTHON, "tools/make_musl_checkpoint.py", "--base", relative(installer_root),
                                  "--musl", relative(paths.out / "musl"), "--stage", relative(stage), *extra_args)))
        graph.add(Target(name=f"musl-installer-{suffix}" if suffix != "installer" else "musl-installer",
                         outputs=(paths.images / f"leonos4-musl-{suffix}.iso",),
                         inputs=(checkpoint_root, loader_elf, kernel_sys, middle_sys, diagnostic,
                                 grub_font, ROOT / "tools/make_installer_iso.py"), kind="generate",
                         command=(PYTHON, "tools/make_installer_iso.py", "--out", relative(paths.images / f"leonos4-musl-{suffix}.iso"),
                                  "--stage", relative(stage / "iso"), "--boot-image", relative(stage / "efiboot.img"),
                                  "--loader", relative(loader_elf), "--kernel", relative(kernel_sys),
                                  "--middlelayer", relative(middle_sys), "--installer-root", relative(checkpoint_root),
                                  "--grub-font", relative(grub_font), "--work-dir", relative(stage / "work"),
                                  "--grub-efi-dir", grub_dir_arg, "--grub-config", relative(diagnostic))))

    live_stage = paths.out / "musl/live-desktop-vim"
    live_root = live_stage / "root.fat"
    live_config = ROOT / "boot/grub/live.cfg"
    graph.add(Target(name="test-live-iso", kind="test", command=(
        PYTHON, "tools/test_live_iso.py")))
    graph.add(Target(name="musl-desktop-vim-root", outputs=(live_root,),
                     inputs=(*esp_outputs,
                             ROOT / "tools/make_live_root.py", ROOT / "tools/make_installer_root.py",
                             ROOT / "tools/make_ext2_root.py", layout.ROOTFS_CONTRACT, ROOT / "tools/leonos_layout.py", ROOT / "tools/make_image.py"),
                     depends_on=("esp",), kind="generate", command=(
                         PYTHON, "tools/make_live_root.py", "--tree", relative(paths.staging),
                         "--out", relative(live_root))))
    graph.add(Target(name="musl-desktop-vim", kind="generate",
                     outputs=(paths.images / "leonos4-musl-desktop-vim-fixed.iso",),
                     inputs=(live_root, loader_elf, kernel_sys, middle_sys, grub_font,
                             live_config, ROOT / "boot/grub/installer_embedded.cfg",
                             ROOT / "tools/make_installer_iso.py"), command=(
                         PYTHON, "tools/make_installer_iso.py", "--out",
                         relative(paths.images / "leonos4-musl-desktop-vim-fixed.iso"),
                         "--stage", relative(live_stage / "iso"),
                         "--boot-image", relative(live_stage / "efiboot.img"),
                         "--loader", relative(loader_elf), "--kernel", relative(kernel_sys),
                         "--middlelayer", relative(middle_sys), "--installer-root", relative(live_root),
                         "--grub-font", relative(grub_font), "--work-dir", relative(live_stage / "work"),
                         "--grub-efi-dir", grub_dir_arg, "--grub-config", relative(live_config))))

    graph.add(Target(name="all", depends_on=(
        "config-sync",
        "build-info",
        "loader",
        "kernel",
        "kerneldebug-module",
        "drivers",
        "middlelayer",
        "userland",
        "sdk",
        "esp",
    ), group=True, kind="aggregate"))
    graph.add(Target(name="run", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values)))
    graph.add(Target(name="run-debug", inputs=(vmdk,), depends_on=("image-vmdk",), kind="command", command=qemu_command(paths, values, debug=True)))
    graph.add(Target(name="run-iso", inputs=(
        vmdk,
        iso,
    ), depends_on=(
        "image-vmdk",
        "image-iso",
    ), kind="command", command=qemu_command(paths, values, debug=True, iso=True)))
    graph.add(Target(name="installer", depends_on=("all", "installer-root", "installer-image"), group=True, kind="aggregate"))
    graph.add(Target(name="images-iso", depends_on=("image-iso", "installer-image"),
                     group=True, kind="aggregate"))

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

    # ---- 主机单元测试和 QEMU 冒烟测试 ----
    graph.add(Target(name="test-oobe", kind="command", always=True,
                     command=(PYTHON, "tools/test_oobe.py")))
    # The elevation gates are the ones a regression would turn into a privilege
    # escalation, so they run as a first-class host target.
    graph.add(Target(name="test-sudo-policy",
                     inputs=(ROOT / "tools/test_sudo_policy.py",
                             ROOT / "tools/tests/sudo_policy_test.c",
                             ROOT / "tools/tests/authd_sudo_test.c",
                             ROOT / "tools/tests/sudo_security_test.c",
                             ROOT / "tools/tests/sudo_spawn_test.c",
                             ROOT / "tools/tests/sudo_client_test.c",
                             ROOT / "tools/tests/sudod_paths_test.c",
                             ROOT / "tools/tests/sudo_result_test.c",
                             ROOT / "tools/tests/sudo_password_test.c",
                             ROOT / "tools/tests/legacy_authd/sudo_policy.h",
                             ROOT / "tools/tests/legacy_authd/authd_sudo.c",
                             ROOT / "tools/tests/legacy_authd/authd_sudo.h"),
                     kind="command", always=True,
                     command=(PYTHON, "tools/test_sudo_policy.py")))
    graph.add(Target(name="test-installer-setup", kind="command", always=True,
                     command=(PYTHON, "tools/test_installer_setup.py")))
    graph.add(Target(name="test-installer-input",
                     inputs=(ROOT / "tools/test_installer_input.py",
                             ROOT / "tools/tests/blockdev_errno_test.c",
                             ROOT / "tools/tests/window_resize_test.c",
                             ROOT / "tools/tests/windowd_announce_test.c",
                             ROOT / "tools/tests/unix_ipc_frame_test.c",
                             ROOT / "tools/tests/wind_reply_test.c",
                             ROOT / "tools/tests/mouse_init_test.c",
                             ROOT / "drivers/mouse/mouse.c",
                             ROOT / "userland/libc/src/unix_ipc.c",
                             ROOT / "userland/apps/windowd/main.c",
                             ROOT / "userland/libc/src/blockdev.c",
                             ROOT / "userland/libc/include/leonos/windowd.h",
                             ROOT / "userland/libc/src/wind.c"),
                     kind="command", command=(PYTHON, "tools/test_installer_input.py")))
    graph.add(Target(name="test-svga",
                     inputs=tuple(collect("drivers/bootstrap/svga/**/*.[ch]")) +
                            tuple(collect("tools/tests/*.[ch]", "userland/apps/glxgears/**/*.[ch]",
                                          "userland/apps/taskmgr/gpu_sample.h")) +
                            (ROOT / "tools/test_svga.py", ROOT / "tools/test_gpu.py",
                             ROOT / "kernel/ntclks/gpu.c", ROOT / "kernel/ntclks/user/usercopy.c",
                             ROOT / "include/leonos/gpu.h", ROOT / "devtools/include/leonos/gpu.h"),
                     kind="command", command=(PYTHON, "tools/test_svga.py")))
    # 主机测试直接运行 Python；QEMU 测试统一经过 qmp_test，负责启动、超时、
    # 串口采集和失败诊断，并且必须依赖 image-vmdk。
    graph.add(Target(name="test-license-server", inputs=(
        ROOT / "tools/test_license_server.py",
        ROOT / "tools/license_server.py",
    ), kind="command", command=(
        PYTHON,
        "tools/test_license_server.py",
    )))
    graph.add(Target(name="test-los2w", inputs=tuple(collect("los2w/*.py")), kind="command", command=(
        PYTHON,
        "-c",
        "from los2w.selftest import run_self_tests; print('\\n'.join(run_self_tests()))",
    )))
    graph.add(Target(name="test-unix-paths", inputs=(ROOT / "tools/check_unix_paths.py",), kind="command", command=(PYTHON, "tools/check_unix_paths.py")))
    graph.add(Target(
        name="test-linux-abi-contract",
        inputs=(ROOT / "tools/test_linux_abi_contract.py",
                ROOT / "include/uapi/linux/syscall.h",
                ROOT / "include/uapi/linux/fcntl.h",
                ROOT / "userland/libc/src/syscall.S",
                ROOT / "kernel/ntclks/syscall.c"),
        kind="command",
        command=(PYTHON, "tools/test_linux_abi_contract.py"),
    ))
    graph.add(Target(name="test-linux-memory", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_memory.py")))
    graph.add(Target(name="test-linux-process-vm", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_process_vm.py")))
    graph.add(Target(name="test-linux-sysv-msg", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_sysv_msg.py")))
    graph.add(Target(name="test-linux-sysv-sem", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_sysv_sem.py")))
    graph.add(Target(name="test-linux-pty", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_pty.py")))
    graph.add(Target(name="test-power", kind="command", always=True,
                     command=(PYTHON, "tools/test_power.py")))
    graph.add(Target(name="test-init-power", kind="command", always=True,
                     command=(PYTHON, "tools/test_init_power.py")))
    graph.add(Target(name="test-linux-permissions", kind="command", always=True,
                     command=(PYTHON, "tools/test_linux_permissions.py")))
    graph.add(Target(name="test-storage-metadata", kind="command", always=True,
                     command=(PYTHON, "tools/test_storage_metadata.py")))
    graph.add(Target(name="test-storage-rename", kind="command", always=True,
                     command=(PYTHON, "tools/test_storage_rename.py")))
    graph.add(Target(name="test-storage-mkdir-mount", kind="command", always=True,
                     command=(PYTHON, "tools/test_storage_mkdir_mount.py")))
    graph.add(Target(name="test-ext2-cache", kind="command", always=True,
                     command=(PYTHON, "tools/test_ext2_cache.py")))
    graph.add(Target(name="test-ext2-performance", kind="command", always=True,
                     command=(PYTHON, "tools/test_ext2_performance.py", "--output",
                              relative(paths.out / "ext2-performance.json"))))
    graph.add(Target(name="test-ext2-write-batch", kind="command", always=True,
                     command=(PYTHON, "tools/test_ext2_write_batch.py")))
    graph.add(Target(name="test-storage-write-batch", kind="command", always=True,
                     command=(PYTHON, "tools/test_storage_write_batch.py")))
    graph.add(Target(name="test-installer-copy", kind="command", always=True,
                     command=(PYTHON, "tools/test_installer_copy.py")))
    for suffix, script in (("linux-rootfs", "test_linux_rootfs.py"),
                           ("linux-inventory", "test_linux_inventory.py"),
                           ("linux-resources", "test_linux_resources.py"),
                           ("linux-socket-batches", "test_linux_socket_batches.py"),
                           ("linux-threads", "test_linux_threads.py"),
                           ("linux-descriptors", "test_linux_descriptors.py"),
                           ("linux-ioctl-cloexec", "test_linux_ioctl_cloexec.py"),
                           ("linux-vfork-stack", "test_linux_vfork_stack.py")):
        graph.add(Target(name="test-" + suffix, kind="command", always=True,
                         command=(PYTHON, "tools/" + script)))
    graph.add(Target(name="test-musl-distribution", kind="command", always=True,
                     depends_on=("installer-root",),
                     command=(PYTHON, "tools/test_musl_distribution.py",
                              relative(paths.staging), relative(installer_stage))))
    graph.add(Target(name="test-uapi", kind="command", always=True,
                     command=(PYTHON, "tools/test_uapi.py")))
    abi_report = paths.out / "generated/abi-migration-report.txt"
    graph.add(Target(
        name="test-abi-migration",
        outputs=(abi_report,),
        inputs=(ROOT / "tools/check_abi_migration.py", ROOT / "docs/ABI_MIGRATION.md"),
        kind="generate",
        command=(PYTHON, "tools/check_abi_migration.py", "--report", relative(abi_report)),
    ))
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
                 sl_smoke: bool = False,
                 less_smoke: bool = False,
                 dynlinkerror_smoke: bool = False,
                 cmd_pipeline_smoke: bool = False,
                 abittest_smoke: bool = False,
                 desktop_app: str | None = None) -> None:
        if cmd_pipeline_smoke and not component_enabled("cmd", "image"):
            raise BuildFailure(
                "QMP cmd pipeline test requires CONFIG_LEON_COMPONENT_TOOL_CMD_IMAGE=y"
            )
        if desktop_app and not component_enabled(desktop_app, "image"):
            raise BuildFailure(f"QMP {desktop_app} test requires that component in the selected image profile")
        test_name = desktop_app if desktop_app else (
            "abittest" if abittest_smoke else
            "dynlinkerror" if dynlinkerror_smoke else
            "cmd" if cmd_pipeline_smoke else
            "less" if less_smoke else
            "sl" if sl_smoke else
            "fastfetch" if fastfetch_smoke else
            "tcc" if tcc_smoke else editor
        )
        # Keep the control endpoint and writes isolated from the produced VMDK.
        # The test name also makes per-test serial logs and QMP diagnostics
        # unambiguous when individual tests are started independently.
        socket = Path(tempfile.gettempdir()) / f"leonos4-qmp-{context.runner.task_id}-{test_name}.sock"
        overlay = Path(tempfile.gettempdir()) / f"leonos4-qmp-{context.runner.task_id}-{test_name}.qcow2"
        serial_log = paths.out / f"qmp-{test_name}-serial.log"
        socket.unlink(missing_ok=True)
        overlay.unlink(missing_ok=True)
        serial_log.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            "qemu-img", "create", "-q", "-f", "qcow2",
            "-b", str(vmdk), "-F", "vmdk", str(overlay),
        ], cwd=ROOT, check=True)
        command = list(qemu_command(paths, values, debug=True))
        if test_name in ("vim", "abittest"):
            for index, argument in enumerate(command[:-1]):
                if argument == "-m":
                    command[index + 1] = "2048M"
                    break
        drive_file = f"file={relative(vmdk)}"
        for index, argument in enumerate(command):
            if drive_file in argument:
                command[index] = (argument.replace(drive_file, f"file={overlay}")
                                           .replace("format=vmdk", "format=qcow2"))
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
                elif sl_smoke:
                    smoke_command.append("--sl")
                elif less_smoke:
                    smoke_command.append("--less")
                elif dynlinkerror_smoke:
                    smoke_command.append("--dynlinkerror")
                elif cmd_pipeline_smoke:
                    smoke_command.append("--cmd-pipeline")
                elif abittest_smoke:
                    smoke_command.append("--abittest")
                elif desktop_app:
                    smoke_command += ["--desktop-app", desktop_app]
                else:
                    smoke_command += ["--editor", editor]
                if less_smoke:
                    smoke_command += ["--serial-log", str(serial_log)]
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
                overlay.unlink(missing_ok=True)
        serial_text = serial_log.read_text(encoding="utf-8", errors="replace")
        # Once the graphical desktop owns the console, the framebuffer remains
        # the most reliable user-visible assertion. Keep serial checks as well
        # when a specific process launch is expected.
        if cmd_pipeline_smoke or less_smoke:
            screenshot_name = "less-qmp-smoke.ppm" if less_smoke else "cmd-pipeline-qmp-smoke.ppm"
            screenshot = paths.images / screenshot_name
            if not screenshot.is_file() or screenshot.stat().st_size == 0:
                raise BuildFailure(f"QMP {test_name} test did not produce a terminal screenshot")
        if desktop_app:
            expected_spawns = (f"spawn path=/usr/lib/leonos/apps/{desktop_app}/{desktop_app}.elf",)
            expected_exits = (f"name={desktop_app}.elf",)
        elif abittest_smoke:
            expected_spawns = (
                "spawn path=/bin/busybox",
                "spawn path=/usr/lib/leonos/apps/abittest/abittest.elf",
            )
            expected_exits = ("name=busybox.elf", "name=abittest.elf")
        elif tcc_smoke:
            expected_spawns = (
                "spawn path=/opt/tcc/tcc.elf",
                "spawn path=/tmp/leonos-tcc-smoke",
            )
            expected_exits = ("name=tcc.elf", "name=leonos-tcc-smoke")
        elif fastfetch_smoke:
            expected_spawns = ("spawn path=/usr/lib/leonos/apps/fastfetch/fastfetch.elf",)
            expected_exits = ("name=fastfetch.elf",)
        elif sl_smoke:
            expected_spawns = ("spawn path=/usr/bin/sl",)
            expected_exits = ("name=sl.elf",)
        elif less_smoke:
            expected_spawns = ("spawn path=/usr/bin/less",)
            expected_exits = ("name=less.elf",)
        elif dynlinkerror_smoke:
            expected_spawns = (
                "spawn path=/usr/lib/leonos/apps/nano/nano.elf",
                "spawn path=/usr/lib/leonos/apps/dynlinkerror/dynlinkerror.elf",
            )
            expected_exits = ("name=nano.elf",)
        elif cmd_pipeline_smoke:
            expected_spawns = (
                "spawn path=/opt/cmd/cmd.elf",
                "spawn path=/bin/busybox",
            )
            expected_exits = ("name=busybox.elf",)
        elif editor == "vi":
            expected_spawns = ("spawn path=/bin/busybox",)
            expected_exits = ("name=busybox.elf",)
        else:
            expected_spawns = (f"spawn path=/usr/lib/leonos/apps/{editor}/{editor}.elf",)
            expected_exits = (f"name={editor}.elf",)
        for expected_spawn in expected_spawns:
            # Desktop launchers still use the kernel's direct launcher API,
            # while Ash now performs a real COW fork followed by execve.
            # Accept either diagnostic form, but always require the exact
            # executable path so a different child cannot satisfy the check.
            executable_path = expected_spawn.removeprefix("spawn path=")
            fork_exec_seen = "exec pid=" in serial_text and f"path={executable_path}" in serial_text
            if expected_spawn not in serial_text and not fork_exec_seen:
                raise BuildFailure(f"QMP test did not start {test_name}: missing {expected_spawn}")
        if desktop_app == "glxgears":
            app_name = f"{desktop_app}.elf"
            app_pids = re.findall(
                rf"\[ntclks\] task pid=(\d+) .*name={re.escape(app_name)} ",
                serial_text,
            )
            app_pids += re.findall(
                rf"\[ntclks\] exec pid=(\d+) path=/usr/lib/leonos/apps/{desktop_app}/{re.escape(app_name)}(?:\s|$)",
                serial_text,
            )
            if not app_pids or not any(
                re.search(rf"scheduler task exited pid={pid} name={re.escape(app_name)} code=0(?:\s|$)", serial_text)
                for pid in app_pids
            ):
                raise BuildFailure(f"QMP test did not observe {test_name} exit")
        else:
            for expected_exit in expected_exits:
                if expected_exit not in serial_text:
                    raise BuildFailure(f"QMP test did not observe {test_name} exit: missing {expected_exit}")
        if tcc_smoke and not re.search(
                r"scheduler task exited pid=\d+ name=leonos-tcc-smoke code=0(?:\s|$)", serial_text):
            raise BuildFailure("QMP TCC-generated musl executable did not exit successfully")
        if abittest_smoke:
            if "[abittest] signal PASS" not in serial_text or \
               "[abittest] pty PASS" not in serial_text or \
               "[abittest] evdev PASS" not in serial_text or \
               "[abittest] ALL PASS" not in serial_text:
                raise BuildFailure("QMP abittest did not report all Linux ABI v1 runtime passes")
        if cmd_pipeline_smoke:
            cmd_pids = re.findall(
                r"\[ntclks\] exec pid=(\d+) path=/opt/cmd/cmd\.elf",
                serial_text,
            )
            if not cmd_pids:
                raise BuildFailure("QMP cmd pipeline test did not identify the cmd process")
            cmd_pid = cmd_pids[-1]
            fork_edges = re.findall(
                r"\[ntclks\] fork parent=(\d+) child=(\d+) ",
                serial_text,
            )
            descendants = {cmd_pid}
            changed = True
            while changed:
                changed = False
                for parent_pid, child_pid in fork_edges:
                    if parent_pid in descendants and child_pid not in descendants:
                        descendants.add(child_pid)
                        changed = True
            stage_pids = [
                pid for pid in descendants
                if pid != cmd_pid
                if f"[ntclks] exec pid={pid} path=/bin/busybox" in serial_text
            ]
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
    graph.add(Target(name="test-qmp-terminal", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command",
        action=lambda context: qmp_test(context, "vim"), action_key="qmp-terminal-vim-v1"))
    graph.add(Target(name="test-qmp-pleditor", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, "pleditor"), action_key="qmp-pleditor-v1"))
    graph.add(Target(name="test-qmp-vi", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, "vi"), action_key="qmp-vi-v1"))
    graph.add(Target(name="test-qmp-vim", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, "vim"), action_key="qmp-vim-v1"))
    graph.add(Target(name="test-qmp-tcc", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, tcc_smoke=True), action_key="qmp-tcc-v1"))
    graph.add(Target(name="test-qmp-fastfetch", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, fastfetch_smoke=True), action_key="qmp-fastfetch-v1"))
    graph.add(Target(name="test-qmp-sl", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, sl_smoke=True), action_key="qmp-sl-v1"))
    graph.add(Target(name="test-qmp-less", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, less_smoke=True), action_key="qmp-less-v1"))
    graph.add(Target(name="test-qmp-dynlinkerror", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, dynlinkerror_smoke=True), action_key="qmp-dynlinkerror-v1"))
    graph.add(Target(name="test-qmp-abittest", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, abittest_smoke=True), action_key="qmp-abittest-v1"))
    graph.add(Target(name="test-qmp-cmd", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, cmd_pipeline_smoke=True), action_key="qmp-cmd-v3"))
    graph.add(Target(name="test-qmp-stardust", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, desktop_app="stardusthello"), action_key="qmp-stardust-v1"))
    graph.add(Target(name="test-qmp-glxgears", inputs=(
        vmdk,
        ROOT / "tools/qmp_terminal_smoke.py",
    ), depends_on=("image-vmdk",), kind="command", action=lambda context: qmp_test(context, desktop_app="glxgears"), action_key="qmp-glxgears-v1"))
    qmp_suite_specs: list[dict[str, object]] = []
    if config_bool(values, "CONFIG_TEST_QMP_TERMINAL"):
        qmp_suite_specs.append({})
    if config_bool(values, "CONFIG_TEST_QMP_TCC") and component_enabled("tcc", "image"):
        qmp_suite_specs.append({"tcc_smoke": True})
    if component_enabled("cmd", "image"):
        qmp_suite_specs.append({"cmd_pipeline_smoke": True})
    if component_enabled("less", "image"):
        qmp_suite_specs.append({"less_smoke": True})
    if config_bool(values, "CONFIG_TEST_QMP_STARDUST") and component_enabled("stardusthello", "image"):
        qmp_suite_specs.append({"desktop_app": "stardusthello"})

    def qmp_test_suite(context: ActionContext) -> None:
        # A full TinyCC run exceeds a minute on a cold guest.  Running QMP
        # guests serially keeps timing deterministic and avoids CPU contention
        # that can make shell input arrive while the compiler owns the PTY.
        for spec in qmp_suite_specs:
            qmp_test(context, **spec)

    graph.add(Target(
        name="test-qmp-suite",
        inputs=(vmdk, ROOT / "tools/qmp_terminal_smoke.py"),
        depends_on=("image-vmdk",),
        kind="command",
        action=qmp_test_suite,
        action_key="qmp-suite-v1",
    ))
    selected_tests: list[str] = []
    if config_bool(values, "CONFIG_TEST_LICENSE_SERVER"):
        selected_tests.append("test-license-server")
    if config_bool(values, "CONFIG_TEST_LOS2W"):
        selected_tests.append("test-los2w")
    if qmp_suite_specs:
        selected_tests.append("test-qmp-suite")
    if config_bool(values, "CONFIG_TEST_COMPONENT_CONFIG"):
        selected_tests.append("test-component-config")
    graph.add(Target(name="test-all", depends_on=tuple(selected_tests), group=True, kind="aggregate"))
    return graph


def require_linux() -> None:
    if platform.system() != "Linux":
        raise BuildFailure("LeonOS BuildSystem only supports Linux or WSL; run python3 build.py inside WSL")


def require_tools(names: Iterable[str]) -> None:
    """在构建前检查当前 target 所需的宿主工具，错误信息比 subprocess 更易读。"""
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
    """按 target 类型返回最小宿主依赖；新增外部工具时在这里登记。"""
    compiler = ("clang", "ld.lld")
    host_userland = ("autoreconf", "autoconf", "automake", "libtoolize", "make",
                     "gcc", "gawk")
    userland = (*compiler, "llvm-ar", *host_userland)
    esp = (*userland, "rustc", "grub-mkfont", "grub-mkstandalone")
    vmdk = (*esp, "truncate", "mkfs.fat", "mcopy", "mke2fs", "dd", "qemu-img")
    iso = (*esp, "grub-mkrescue", "xorriso")
    if task in {"file", "file-magic", "sqlite", "portablegl", "app:glxgears"}:
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
    if task in {"test-qmp-terminal", "test-qmp-pleditor", "test-qmp-tcc", "test-qmp-fastfetch", "test-qmp-sl", "test-qmp-less",
                "test-qmp-dynlinkerror", "test-qmp-cmd", "test-qmp-abittest", "test-qmp-stardust", "test-qmp-glxgears",
                "test-linux-abi-contract", "test-all"}:
        return (*vmdk, "qemu-system-x86_64")
    return ()


def create_runner(
    paths: BuildPaths,
    graph: BuildGraph,
    task_id: str,
    *,
    verbose: bool = False,
    theme: str = "default",
) -> BuildRunner:
    """创建执行器；日志、任务状态和缓存均通过该执行器维护。"""
    return BuildRunner(graph, paths, load_settings(paths), task_id, verbose=verbose, theme=theme)


def build_roots(graph: BuildGraph, target: Target) -> tuple[Target, ...]:
    if target.name in BUILD_NUMBER_EXEMPT_TARGETS or target.name == "build-info":
        return (target,)
    return (graph.targets["build-info"], target)


def display_help() -> str:
    return """LeonOS BuildSystem

Commands:
  build.py help
  build.py tui
  build.py [-v|--verbose] run <task> [--profile NAME] [--set CONFIG_KEY=VALUE]
  build.py profile <task> [--profile NAME] [--set CONFIG_KEY=VALUE]
  build.py config <list|save|load|reset|import|export> [name] [path]
  build.py info <file-or-task>
  build.py why <file-or-task>
  build.py affected <file>
  build.py cache <stats|prune>
  build.py settings
  build.py map
  build.py gen <file>
  build.py test <license-server|los2w|component-config|svga|installer-input|oobe|qmp-terminal|qmp-pleditor|qmp-tcc|qmp-fastfetch|qmp-sl|qmp-less|qmp-dynlinkerror|qmp-cmd|qmp-stardust|qmp-glxgears|all>
  build.py client <run|gen|test|profile> ...
  build.py status <task-id>
  build.py log <task-id>

Options:
  -v, --verbose  Print target graph, cache decisions, commands, process diagnostics, and actions.
  --profile       Build from configs/profiles/<name>.conf without modifying the active config.

Tasks:
  all, config-sync, build-info, loader, kernel, drivers, middlelayer, userland, sdk, esp, image-vmdk, image-iso, installer, release, run, run-debug, run-iso, menuconfig, defconfig, clean
"""


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
    """为一次构建确定有效配置，支持 profile 和不落盘的 --set 覆盖。"""
    if profile:
        source = profile_path(profile)
        if not source.exists():
            raise BuildFailure(f"profile does not exist: {profile}")
        if interactive and not overrides:
            return source
    else:
        if not paths.kconfig.exists():
            paths.kconfig.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "configs/default.conf", paths.kconfig)
        source = paths.kconfig
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


def apply_defconfig(paths: BuildPaths) -> None:
    """Install the checked-in default template as the developer config."""
    paths.kconfig.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "configs/default.conf", paths.kconfig)
    sync_config_file(paths, paths.kconfig)


def handle_config_command(paths: BuildPaths, arguments: argparse.Namespace) -> int:
    action = arguments.action
    if action == "list":
        profile_directory().mkdir(parents=True, exist_ok=True)
        for profile in sorted(profile_directory().glob("*.conf")):
            print(profile.stem)
        return 0
    if action == "reset":
        apply_defconfig(paths)
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
    """收集缓存统计；新增缓存目录时在这里暴露，便于诊断空间占用。"""
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
    theme: str = "default",
) -> dict[str, object]:
    """运行 target 并返回耗时/命中率报告；JSON 模式会隐藏 runner 的过程输出。"""
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id, verbose=verbose, theme=theme)
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
    theme: str = "default",
) -> int:
    """在前台执行单个 target，并统一执行 Linux、工具和 GRUB 前置检查。"""
    require_linux()
    require_tools(task_tools(target.name))
    require_grub_efi_modules(paths, target.name)
    runner = create_runner(paths, graph, task_id, verbose=verbose, theme=theme)
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
    theme: str = "default",
) -> int:
    """登记并启动后台 worker；后台输出只写 TaskStore 日志，不污染客户端终端。"""
    if not command or command[0] not in {"run", "gen", "test", "profile"}:
        raise BuildFailure("client accepts run, gen, test, or profile followed by its arguments")
    store.update(task_id, status="queued", queued_at=utc_now(), task=" ".join(command))
    log = store.log_path(task_id)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8", newline="\n") as handle:
        details: list[str] = []
        if verbose:
            details.append("verbose")
        if theme != "default":
            details.append(f"theme={theme}")
        detail = f" ({', '.join(details)})" if details else ""
        handle.write(f"Queued background task {task_id}{detail}: {' '.join(command)}\n")
    with open(os.devnull, "w", encoding="utf-8") as sink:
        worker_command = [*command]
        if verbose:
            worker_command.append("--verbose")
        if json_output:
            worker_command.append("--json")
        if theme != "default":
            worker_command.append(f"--theme={theme}")
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
        details: list[str] = []
        if verbose:
            details.append("verbose")
        if theme != "default":
            details.append(f"theme={theme}")
        suffix = f" ({', '.join(details)})" if details else ""
        print(f"build: task \"{' '.join(command)}\" start. ID:{task_id}{suffix}")
    return 0


def parser() -> argparse.ArgumentParser:
    """定义 CLI 表面；新增命令须同时在 main 中实现分发和任务记录。"""
    argument_parser = argparse.ArgumentParser(add_help=False, prog="build.py")
    argument_parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    argument_parser.add_argument("--task-id", help=argparse.SUPPRESS)
    argument_parser.add_argument("--json", dest="json_output", action="store_true", help="emit machine-readable JSON")
    argument_parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="show full build graph, cache, command, process, and action diagnostics",
    )
    argument_parser.add_argument(
        "--theme", choices=("default", "linux", "meson", "cargo"), default="default",
        help="render build logs using the selected build-system style",
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
    test.add_argument("item", choices=("license-server", "los2w", "component-config", "svga", "installer-input", "installer-setup", "oobe", "sudo-policy",
                                       "qmp-terminal", "qmp-pleditor", "qmp-tcc", "qmp-fastfetch", "qmp-sl", "qmp-less",
                                       "qmp-dynlinkerror", "qmp-cmd", "qmp-abittest", "qmp-stardust", "qmp-glxgears",
                                       "linux-abi-contract", "linux-memory", "linux-pty", "linux-permissions",
                                       "linux-ioctl-cloexec", "python-package",
                                       "storage-metadata", "storage-rename", "storage-mkdir-mount", "musl-abi", "uapi", "all"))
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
    """预处理全局选项并交给 argparse；兼容选项出现在子命令前后两种写法。"""
    values = list(argv if argv is not None else sys.argv[1:])
    json_output = "--json" in values
    verbose = any(value in {"-v", "--verbose"} for value in values)
    theme = "default"
    filtered: list[str] = []
    index = 0
    while index < len(values):
        value = values[index]
        if value.startswith("--theme="):
            theme = value.split("=", 1)[1]
        elif value == "--theme":
            if index + 1 < len(values):
                theme = values[index + 1]
                index += 1
            else:
                filtered.append(value)
        elif value not in {"--json", "-v", "--verbose"}:
            filtered.append(value)
        index += 1
    values = filtered
    if json_output:
        values.insert(0, "--json")
    if verbose:
        values.insert(0, "--verbose")
    if theme != "default":
        values.insert(0, f"--theme={theme}")
    return parser().parse_args(values)


def main(argv: list[str] | None = None) -> int:
    """CLI 调度入口。

    轻量查询命令在构图前返回；run/gen/test/profile 等命令先解析有效配置，
    再构建图并交给 runner。新命令不要绕过此流程，以保持日志和后台任务一致。
    """
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
                theme=arguments.theme,
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
        if arguments.command == "run" and arguments.task == "defconfig":
            if arguments.config_profile or arguments.config_overrides:
                raise BuildFailure("run defconfig does not accept --profile or --set")
            apply_defconfig(paths)
            text = "developer configuration updated from configs/default.conf"
            print(text)
            complete_simple(store, task_id, "run defconfig", text)
            return 0
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
        if arguments.command == "test" and arguments.item == "svga":
            # This driver test has no userland or submodule dependencies.
            graph = BuildGraph(ROOT)
            graph.add(Target(name="test-svga", kind="command",
                             inputs=(ROOT / "tools/test_svga.py",),
                             command=(PYTHON, "tools/test_svga.py")))
        else:
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
                theme=arguments.theme,
            )
        if arguments.command == "gen":
            return run_foreground(
                paths,
                graph,
                task_id,
                graph.resolve_target(arguments.file),
                f"gen {arguments.file}",
                verbose=effective_verbose,
                theme=arguments.theme,
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
                theme=arguments.theme,
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
                theme=arguments.theme,
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
        message = f"{'' if arguments.theme != 'default' else 'build: '}{exc}"
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
