#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shlex
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMAL_USER_APPS = [
    "init",
    "desktop",
    "oobe",
    "login",
    "hello",
    "uidemo",
    "cjktest",
    "taskmgr",
    "fileman",
    "terminal",
    "shell",
    "notepad",
    "calc",
    "minesweeper",
    "run",
    "osver",
    "memtest",
    "bugtest",
    "ping",
    "netctl",
    "serviced",
    "servicemgr",
    "httpget",
    "downloadmgr",
    "browser",
    "imageview",
    "oshlp",
    "settings",
    "diskmgr",
    "devmgr",
]
INSTALLER_USER_APPS = [
    "desktop",
    "installer",
]
INSTALLER_POLICY_APPS = [
    "desktop",
    "oobe",
    "settings",
]
USER_APPS = NORMAL_USER_APPS + [
    app for app in INSTALLER_USER_APPS if app not in NORMAL_USER_APPS
]

APP_ICON_APPS = USER_APPS

WINDOW_BUTTON_ICONS = [
    "window-button-minimize.bmp",
    "window-button-maximize.bmp",
    "window-button-restore.bmp",
    "window-button-close.bmp",
]

SYSTEM_FILES = [
    ("system/osmlayer.manifest", None),
    ("system/fonts/system.psf", "system/fonts/system.psf"),
    ("system/fonts/cjk16.lbf", "system/fonts/cjk16.lbf"),
    ("system/resources/mouse.bmp", "system/resources/mouse.bmp"),
    *(
        (f"system/resources/{name}", f"build/generated/window-buttons/{name}")
        for name in WINDOW_BUTTON_ICONS
    ),
]

CONFIG_DEFAULTS = {
    "CONFIG_VMDK_REQUIRE_LICENSE": "y",
    "CONFIG_INSTALLER_INSTALLED_REQUIRE_LICENSE": "y",
    "CONFIG_IMAGE_SIZE_MIB": "96",
    "CONFIG_INSTALLER_ROOT_SIZE_MIB": "64",
    "CONFIG_NINJA_REGENERATE_BEFORE_BUILD": "n",
    "CONFIG_QEMU_MEMORY_MB": "512",
    "CONFIG_QEMU_DISPLAY_WIDTH": "1920",
    "CONFIG_QEMU_DISPLAY_HEIGHT": "1080",
    "CONFIG_QEMU_ENABLE_KVM": "y",
    "CONFIG_QEMU_NET_DEVICE": '"e1000"',
    "CONFIG_QEMU_OVMF_PATH": '"/usr/share/ovmf/OVMF.fd"',
    "CONFIG_LICENSE_SERVER_URL": '"http://127.0.0.1:30301"',
    "CONFIG_LICENSE_DEBUG_LOG": "y",
}


def r(path: Path | str) -> str:
    p = Path(path)
    if p.is_absolute():
        p = p.relative_to(ROOT)
    return p.as_posix()


def collect(patterns: list[str]) -> list[Path]:
    files: list[Path] = []
    for pattern in patterns:
        files.extend(ROOT.glob(pattern))
    return sorted(p for p in files if p.is_file())


def obj_for(src: Path, prefix: str) -> Path:
    rel = src.relative_to(ROOT)
    suffix = ".o"
    return ROOT / "build" / "obj" / prefix / rel.with_suffix(rel.suffix + suffix)


def user_app_sources(app: str) -> list[Path]:
    sources = collect([
        f"userland/apps/{app}/*.c",
        f"userland/apps/{app}/*.S",
    ])
    if app == "oobe":
        browser_sources = [
            src for src in collect(["userland/apps/browser/*.c"])
            if src.name != "main.c"
        ]
        sources.extend(browser_sources)
    return sorted(sources)


def write_line(lines: list[str], line: str = "") -> None:
    lines.append(line)


def read_config(path: Path) -> dict[str, str]:
    values = dict(CONFIG_DEFAULTS)
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2 : -len(" is not set")]
            if key in CONFIG_DEFAULTS:
                values[key] = "n"
            continue
        if line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if key in CONFIG_DEFAULTS:
            values[key] = value.strip()
    return values


def config_bool(values: dict[str, str], key: str) -> bool:
    return values.get(key, CONFIG_DEFAULTS[key]) == "y"


def config_int(values: dict[str, str], key: str) -> int:
    raw = values.get(key, CONFIG_DEFAULTS[key]).strip()
    try:
        return int(raw, 10)
    except ValueError:
        return int(CONFIG_DEFAULTS[key], 10)


def config_string(values: dict[str, str], key: str) -> str:
    raw = values.get(key, CONFIG_DEFAULTS[key]).strip()
    if len(raw) >= 2 and raw[0] == '"' and raw[-1] == '"':
        raw = raw[1:-1]
    return raw.replace('\\"', '"').replace("\\\\", "\\")


def safe_qemu_device_model(value: str) -> str:
    if not value:
        return ""
    for ch in value:
        if not (ch.isalnum() or ch in "._-"):
            return ""
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LeonOS 4 build.ninja")
    parser.add_argument("--out", default="build.ninja")
    args = parser.parse_args()

    out = ROOT / args.out
    lines: list[str] = []
    config = read_config(ROOT / ".config")
    ninja_regen = config_bool(config, "CONFIG_NINJA_REGENERATE_BEFORE_BUILD")
    image_size_mib = config_int(config, "CONFIG_IMAGE_SIZE_MIB")
    installer_root_size_mib = config_int(config, "CONFIG_INSTALLER_ROOT_SIZE_MIB")
    qemu_memory_mb = config_int(config, "CONFIG_QEMU_MEMORY_MB")
    qemu_display_width = config_int(config, "CONFIG_QEMU_DISPLAY_WIDTH")
    qemu_display_height = config_int(config, "CONFIG_QEMU_DISPLAY_HEIGHT")
    qemu_kvm = "-enable-kvm " if config_bool(config, "CONFIG_QEMU_ENABLE_KVM") else ""
    qemu_net_model = safe_qemu_device_model(config_string(config, "CONFIG_QEMU_NET_DEVICE"))
    qemu_net = (
        f"-netdev user,id=net0 -device {qemu_net_model},netdev=net0 "
        if qemu_net_model else ""
    )
    qemu_ovmf_path = config_string(config, "CONFIG_QEMU_OVMF_PATH")
    qemu_bios = f"-bios {shlex.quote(qemu_ovmf_path)} " if qemu_ovmf_path else ""
    qemu_vga = f"-device VGA,xres={qemu_display_width},yres={qemu_display_height}"

    cc = os.environ.get("CC", "clang")
    rustc = os.environ.get("RUSTC", "rustc")
    ar = os.environ.get("AR", "llvm-ar")
    ld = os.environ.get("LD", "ld.lld")

    cflags_kernel = (
        "-target x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector "
        "-fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra "
        "-Ikernel/ntclks/include -Iinclude -Iinclude/generated"
    )
    asflags_kernel = "-target x86_64-unknown-none -ffreestanding -mno-red-zone -mgeneral-regs-only -Ikernel/ntclks/include -Iinclude"
    cflags_loader = (
        "-target x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector "
        "-fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Iinclude"
    )
    asflags_loader = "-target x86_64-unknown-none -ffreestanding -mno-red-zone -mgeneral-regs-only -Iinclude"
    cflags_user_base = (
        "-target x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector "
        "-fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Iuserland/libc/include -Iinclude"
    )
    cflags_user = f"{cflags_user_base} -include include/generated/autoconf.h"
    cflags_user_installer_policy = f"{cflags_user_base} -include include/generated/autoconf-installer.h"
    asflags_user = "-target x86_64-unknown-none -ffreestanding -mno-red-zone -mgeneral-regs-only -Iuserland/libc/include -Iinclude"

    write_line(lines, "ninja_required_version = 1.10")
    write_line(lines, f"cc = {cc}")
    write_line(lines, f"rustc = {rustc}")
    write_line(lines, f"ar = {ar}")
    write_line(lines, f"ld = {ld}")
    write_line(lines)
    write_line(lines, "rule kconfig_sync")
    write_line(lines, "  command = python3 tools/kconfig_sync.py")
    write_line(lines, "  description = KCONFIG sync")
    write_line(lines)
    write_line(lines, "rule ensure_config")
    write_line(lines, "  command = test -f $out || cp $in $out")
    write_line(lines, "  restat = 1")
    write_line(lines, "  description = CONFIG $out")
    write_line(lines)
    write_line(lines, "rule gen_build_ninja")
    write_line(lines, "  command = python3 tools/gen_ninja.py --out $out")
    write_line(lines, "  generator = 1")
    write_line(lines, "  description = GEN.NINJA $out")
    write_line(lines)
    write_line(lines, "rule build_info")
    write_line(lines, "  command = python3 tools/build_info.py --header $out --state build/version/build_number.txt")
    write_line(lines, "  description = BUILD.INFO $out")
    write_line(lines)
    write_line(lines, "rule loader_integrity")
    write_line(lines, "  command = python3 tools/gen_loader_integrity.py --kernel build/system/kernel.sys --middlelayer build/system/middlelayer.sys --out $out")
    write_line(lines, "  description = LOADER.INTEGRITY $out")
    write_line(lines)
    write_line(lines, "rule cc_kernel")
    write_line(lines, f"  command = $cc {cflags_kernel} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = CC.KERNEL $out")
    write_line(lines)
    write_line(lines, "rule as_kernel")
    write_line(lines, f"  command = $cc {asflags_kernel} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = AS.KERNEL $out")
    write_line(lines)
    write_line(lines, "rule cc_loader")
    write_line(lines, f"  command = $cc {cflags_loader} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = CC.LOADER $out")
    write_line(lines)
    write_line(lines, "rule as_loader")
    write_line(lines, f"  command = $cc {asflags_loader} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = AS.LOADER $out")
    write_line(lines)
    write_line(lines, "rule rust_osmlayer")
    write_line(lines, "  command = $rustc --crate-type lib --target x86_64-unknown-none -C panic=abort -C relocation-model=static -C code-model=kernel -C no-redzone=yes -C target-feature=-sse,-sse2 -C opt-level=1 --emit obj=$out $in")
    write_line(lines, "  description = RUST.OSMLAYER $out")
    write_line(lines)
    write_line(lines, "rule link_loader")
    write_line(lines, "  command = $ld -nostdlib -z max-page-size=0x1000 -T boot/loader/linker.ld -o $out $in")
    write_line(lines, "  description = LD.LOADER $out")
    write_line(lines)
    write_line(lines, "rule link_kernel")
    write_line(lines, "  command = $ld -nostdlib -z max-page-size=0x1000 -T kernel/ntclks/arch/x86_64/linker.ld -o $out $in")
    write_line(lines, "  description = LD.KERNEL $out")
    write_line(lines)
    write_line(lines, "rule link_middlelayer")
    write_line(lines, "  command = $ld -nostdlib -z max-page-size=0x1000 -T middlelayer/osmlayer/linker.ld -o $out $in")
    write_line(lines, "  description = LD.MIDDLELAYER $out")
    write_line(lines)
    write_line(lines, "rule cc_user")
    write_line(lines, f"  command = $cc {cflags_user} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = CC.USER $out")
    write_line(lines)
    write_line(lines, "rule cc_user_installer_policy")
    write_line(lines, f"  command = $cc {cflags_user_installer_policy} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = CC.USER.INSTALLER $out")
    write_line(lines)
    write_line(lines, "rule as_user")
    write_line(lines, f"  command = $cc {asflags_user} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = AS.USER $out")
    write_line(lines)
    write_line(lines, "rule ar")
    write_line(lines, "  command = mkdir -p `dirname $out` && $ar rcs $out $in")
    write_line(lines, "  description = AR $out")
    write_line(lines)
    write_line(lines, "rule link_user")
    write_line(lines, "  command = mkdir -p `dirname $out` && $ld -nostdlib -z max-page-size=0x1000 -T userland/linker.ld -o $out $in")
    write_line(lines, "  description = LD.USER $out")
    write_line(lines)
    write_line(lines, "rule copy")
    write_line(lines, "  command = mkdir -p `dirname $out` && cp $in $out")
    write_line(lines, "  description = COPY $out")
    write_line(lines)
    write_line(lines, "rule app_icons")
    write_line(lines, "  command = python3 tools/make_app_icons.py --out-dir build/generated/app-icons --apps $apps")
    write_line(lines, "  description = APP.ICONS build/generated/app-icons")
    write_line(lines)
    write_line(lines, "rule window_button_icons")
    write_line(lines, "  command = python3 tools/make_window_button_icons.py --out-dir build/generated/window-buttons")
    write_line(lines, "  description = WINDOW.ICONS build/generated/window-buttons")
    write_line(lines)
    write_line(lines, "rule mkdir")
    write_line(lines, "  command = mkdir -p $out")
    write_line(lines, "  description = MKDIR $out")
    write_line(lines)
    write_line(lines, "rule manifest")
    write_line(lines, "  command = mkdir -p `dirname $out` && printf 'name=osmlayer\\nabi=1\\nroot=0:/\\nfs=fat32\\ngui=desktop.elf\\n' > $out")
    write_line(lines, "  description = MANIFEST $out")
    write_line(lines)
    grub_efi_dir = ROOT / "build/deps/grub-efi-amd64-bin/usr/lib/grub/x86_64-efi"
    write_line(lines, "rule grub_efi")
    write_line(lines, f"  command = grub-mkstandalone -d {r(grub_efi_dir)} -O x86_64-efi -o $out --modules='part_gpt fat multiboot2 normal search search_fs_file configfile echo serial terminal video video_bochs video_cirrus efi_gop efi_uga all_video gfxterm' boot/grub/grub.cfg=boot/grub/embedded.cfg")
    write_line(lines, "  description = GRUB.EFI $out")
    write_line(lines)
    write_line(lines, "rule iso")
    iso_copy_apps = " && ".join(
        f"cp build/userland/{app}.elf build/iso/userland/{app}.elf && cp build/esp/userland/{app}.bmp build/iso/userland/{app}.bmp"
        for app in NORMAL_USER_APPS
    )
    iso_copy_system = " && ".join(
        f"mkdir -p build/iso/{Path(dst).parent.as_posix()} && cp build/esp/{dst} build/iso/{dst}"
        for dst, _ in SYSTEM_FILES
    )
    iso_copy_docs = " && ".join(
        f"cp build/esp/docs/{src.name} build/iso/docs/{src.name}"
        for src in collect(["system/docs/*.hlp"])
    )
    if not iso_copy_docs:
        iso_copy_docs = "true"
    write_line(lines, f"  command = rm -rf build/iso && mkdir -p build/iso/boot/grub build/iso/boot build/iso/system build/iso/userland build/iso/etc build/iso/docs && cp boot/grub/grub.cfg build/iso/boot/grub/grub.cfg && cp build/boot/loader.elf build/iso/boot/loader.elf && cp build/system/kernel.sys build/iso/system/kernel.sys && cp build/system/middlelayer.sys build/iso/system/middlelayer.sys && {iso_copy_system} && {iso_copy_apps} && {iso_copy_docs} && cp build/esp/etc/leonos.conf build/iso/etc/leonos.conf && grub-mkrescue -o $out build/iso")
    write_line(lines, "  description = ISO $out")
    write_line(lines)
    write_line(lines, "rule installer_root")
    write_line(lines, f"  command = rm -f build/esp/etc/license.conf build/esp/etc/install.id && rm -rf build/esp/system/resources/icons && python3 tools/make_installer_root.py --out $out --stage build/install/root --esp-tree build/esp --installed-policy-dir build/userland-installer-policy --size-mib {installer_root_size_mib}")
    write_line(lines, "  description = INSTALLER.ROOT $out")
    write_line(lines)
    write_line(lines, "rule installer_iso")
    write_line(lines, "  command = python3 tools/make_installer_iso.py --out $out --stage build/installer-iso --boot-image build/install/installer-efiboot.img")
    write_line(lines, "  description = INSTALLER.ISO $out")
    write_line(lines)
    write_line(lines, "rule image")
    write_line(lines, f"  command = rm -f build/esp/boot/ntclks.elf build/esp/etc/license.conf build/esp/etc/install.id && rm -rf build/esp/system/resources/icons && python3 tools/make_image.py --out $out --raw build/images/leonos4.raw --esp-tree build/esp --size-mib {image_size_mib}")
    write_line(lines, "  description = IMAGE $out")
    write_line(lines)
    write_line(lines, "rule run")
    write_line(lines, f"  command = qemu-system-x86_64 {qemu_kvm}-cpu host -machine q35 -m {qemu_memory_mb}M {qemu_bios}-serial stdio -display \"$${{LEONOS_QEMU_DISPLAY:-sdl}}\" {qemu_vga} {qemu_net}-drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN LeonOS 4")
    write_line(lines)
    write_line(lines, "rule run_debug")
    write_line(lines, f"  command = qemu-system-x86_64 {qemu_kvm}-cpu host -machine q35 -m {qemu_memory_mb}M {qemu_bios}-serial stdio -display none {qemu_vga} -no-reboot -no-shutdown {qemu_net}-drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN.DEBUG LeonOS 4")
    write_line(lines)
    write_line(lines, "rule run_iso")
    write_line(lines, f"  command = qemu-system-x86_64 -m {qemu_memory_mb}M -serial stdio -display none {qemu_vga} -no-reboot -no-shutdown {qemu_net}-cdrom build/images/leonos4.iso -drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN.ISO LeonOS 4")
    write_line(lines)
    write_line(lines, "rule menuconfig_rule")
    write_line(lines, "  command = test -f .config || cp configs/default.conf .config && kconfig-mconf Kconfig && python3 tools/kconfig_sync.py && python3 tools/gen_ninja.py --out build.ninja")
    write_line(lines, "  pool = console")
    write_line(lines, "  description = MENUCONFIG")
    write_line(lines)
    write_line(lines, "rule clean_rule")
    write_line(lines, "  command = rm -rf build include/generated .config")
    write_line(lines, "  description = CLEAN")
    write_line(lines)

    write_line(lines, "build .config: ensure_config configs/default.conf")
    if ninja_regen:
        write_line(lines, "build build.ninja: gen_build_ninja tools/gen_ninja.py Kconfig configs/default.conf .config")
    config_outputs = [
        ROOT / "include/generated/autoconf.h",
        ROOT / "include/generated/autoconf-installer.h",
        ROOT / "include/generated/rustcfg.args",
    ]
    write_line(lines, f"build {' '.join(r(p) for p in config_outputs)}: kconfig_sync tools/kconfig_sync.py configs/default.conf Kconfig .config")
    write_line(lines, f"build config-sync: phony {' '.join(r(p) for p in config_outputs)}")
    write_line(lines, "build build/version/always: phony")
    write_line(lines, "build include/generated/build_info.h: build_info build/version/always | tools/build_info.py")
    write_line(lines, "build build-info: phony include/generated/build_info.h")
    app_icon_outputs = [
        ROOT / "build" / "generated" / "app-icons" / f"{name}.bmp"
        for name in APP_ICON_APPS
    ]
    write_line(lines, f"build {' '.join(r(p) for p in app_icon_outputs)}: app_icons tools/make_app_icons.py")
    write_line(lines, f"  apps = {' '.join(APP_ICON_APPS)}")
    window_button_icon_outputs = [
        ROOT / "build" / "generated" / "window-buttons" / name
        for name in WINDOW_BUTTON_ICONS
    ]
    write_line(lines, f"build {' '.join(r(p) for p in window_button_icon_outputs)}: window_button_icons tools/make_window_button_icons.py")

    loader_elf = ROOT / "build" / "boot" / "loader.elf"
    kernel_sys = ROOT / "build" / "system" / "kernel.sys"
    middlelayer_sys = ROOT / "build" / "system" / "middlelayer.sys"
    loader_integrity_h = ROOT / "include" / "generated" / "loader_integrity.h"
    write_line(lines, f"build {r(loader_integrity_h)}: loader_integrity {r(kernel_sys)} {r(middlelayer_sys)} | tools/gen_loader_integrity.py")

    loader_sources = collect([
        "boot/loader/**/*.c",
        "boot/loader/**/*.S",
    ])
    loader_objects: list[Path] = []
    for src in loader_sources:
        obj = obj_for(src, "loader")
        loader_objects.append(obj)
        rule = "as_loader" if src.suffix == ".S" else "cc_loader"
        implicit = f" | {r(loader_integrity_h)}" if src.suffix == ".c" else ""
        write_line(lines, f"build {r(obj)}: {rule} {r(src)}{implicit}")
    write_line(lines, f"build {r(loader_elf)}: link_loader {' '.join(r(o) for o in loader_objects)} | boot/loader/linker.ld")
    write_line(lines, f"build loader: phony {r(loader_elf)}")

    kernel_sources = collect([
        "kernel/ntclks/**/*.c",
        "kernel/ntclks/**/*.S",
    ])
    rust_sources = collect(["middlelayer/osmlayer/src/**/*.rs"])
    version_deps = " ".join(
        r(s) for s in sorted(kernel_sources + rust_sources)
        if s != ROOT / "kernel" / "ntclks" / "version.c"
    )
    kernel_objects: list[Path] = []
    for src in kernel_sources:
        obj = obj_for(src, "kernel")
        kernel_objects.append(obj)
        if src == ROOT / "kernel" / "ntclks" / "version.c":
            rule = "cc_kernel"
            implicit = f" | include/generated/autoconf.h include/generated/build_info.h {version_deps}"
        else:
            rule = "as_kernel" if src.suffix == ".S" else "cc_kernel"
            implicit = " | include/generated/autoconf.h" if src.suffix == ".c" else ""
        write_line(lines, f"build {r(obj)}: {rule} {r(src)}{implicit}")

    rust_obj = ROOT / "build" / "obj" / "middlelayer" / "osmlayer.o"
    middlelayer_runtime = ROOT / "middlelayer" / "osmlayer" / "runtime.c"
    middlelayer_runtime_obj = obj_for(middlelayer_runtime, "middlelayer")
    write_line(lines, f"build {r(middlelayer_runtime_obj)}: cc_kernel {r(middlelayer_runtime)} | include/generated/autoconf.h")
    rust_implicit = " ".join(
        r(s) for s in rust_sources
        if s != ROOT / "middlelayer" / "osmlayer" / "src" / "lib.rs"
    )
    write_line(lines, f"build {r(rust_obj)}: rust_osmlayer {r(ROOT / 'middlelayer/osmlayer/src/lib.rs')} | include/generated/rustcfg.args {rust_implicit}")
    write_line(lines, f"build {r(middlelayer_sys)}: link_middlelayer {r(rust_obj)} {r(middlelayer_runtime_obj)} | middlelayer/osmlayer/linker.ld")
    write_line(lines, f"build middlelayer: phony {r(middlelayer_sys)}")

    all_kernel_inputs = " ".join(r(o) for o in kernel_objects)
    write_line(lines, f"build {r(kernel_sys)}: link_kernel {all_kernel_inputs} | kernel/ntclks/arch/x86_64/linker.ld")
    write_line(lines, f"build kernel: phony {r(kernel_sys)}")

    libc_sources = collect(["userland/libc/src/*.c", "userland/libc/src/*.S"])
    libc_objects: list[Path] = []
    installer_policy_libc_objects: list[Path] = []
    for src in libc_sources:
        obj = obj_for(src, "userlib")
        libc_objects.append(obj)
        if src.suffix == ".S":
            write_line(lines, f"build {r(obj)}: as_user {r(src)}")
        else:
            write_line(lines, f"build {r(obj)}: cc_user {r(src)} | include/generated/autoconf.h")

        installer_obj = obj_for(src, "userlib-installer-policy")
        installer_policy_libc_objects.append(installer_obj)
        if src.suffix == ".S":
            write_line(lines, f"build {r(installer_obj)}: as_user {r(src)}")
        else:
            write_line(lines, f"build {r(installer_obj)}: cc_user_installer_policy {r(src)} | include/generated/autoconf-installer.h")
    libc_a = ROOT / "build" / "userland" / "libc.a"
    write_line(lines, f"build {r(libc_a)}: ar {' '.join(r(o) for o in libc_objects)}")
    installer_policy_libc_a = ROOT / "build" / "userland-installer-policy" / "libc.a"
    write_line(lines, f"build {r(installer_policy_libc_a)}: ar {' '.join(r(o) for o in installer_policy_libc_objects)}")

    user_elfs: list[Path] = []
    user_elfs_by_app: dict[str, Path] = {}
    for app in USER_APPS:
        app_sources = user_app_sources(app)
        app_objects: list[Path] = []
        elf = ROOT / "build" / "userland" / f"{app}.elf"
        user_elfs.append(elf)
        user_elfs_by_app[app] = elf
        for src in app_sources:
            obj = obj_for(src, f"user-{app}")
            app_objects.append(obj)
            if src.suffix == ".S":
                write_line(lines, f"build {r(obj)}: as_user {r(src)}")
            else:
                write_line(lines, f"build {r(obj)}: cc_user {r(src)} | include/generated/autoconf.h")
        write_line(lines, f"build {r(elf)}: link_user {' '.join(r(o) for o in app_objects)} {r(libc_a)}")

    installer_policy_elfs_by_app: dict[str, Path] = {}
    for app in INSTALLER_POLICY_APPS:
        app_sources = user_app_sources(app)
        app_objects: list[Path] = []
        elf = ROOT / "build" / "userland-installer-policy" / f"{app}.elf"
        installer_policy_elfs_by_app[app] = elf
        for src in app_sources:
            obj = obj_for(src, f"user-installer-policy-{app}")
            app_objects.append(obj)
            if src.suffix == ".S":
                write_line(lines, f"build {r(obj)}: as_user {r(src)}")
            else:
                write_line(lines, f"build {r(obj)}: cc_user_installer_policy {r(src)} | include/generated/autoconf-installer.h")
        write_line(lines, f"build {r(elf)}: link_user {' '.join(r(o) for o in app_objects)} {r(installer_policy_libc_a)}")

    write_line(lines, f"build userland: phony {' '.join(r(e) for e in user_elfs)}")
    normal_user_elfs = [user_elfs_by_app[app] for app in NORMAL_USER_APPS]
    normal_user_icon_sources = [
        ROOT / "build" / "generated" / "app-icons" / f"{app}.bmp"
        for app in NORMAL_USER_APPS
    ]
    doc_sources = collect(["system/docs/*.hlp"])
    doc_outputs = [
        ROOT / "build" / "esp" / "docs" / src.name
        for src in doc_sources
    ]

    esp_outputs = [
        ROOT / "build/esp/EFI/BOOT/BOOTX64.EFI",
        ROOT / "build/esp/boot/grub/grub.cfg",
        ROOT / "build/esp/boot/loader.elf",
        ROOT / "build/esp/system/kernel.sys",
        ROOT / "build/esp/system/middlelayer.sys",
        *(ROOT / "build/esp" / dst for dst, _ in SYSTEM_FILES),
        *(ROOT / f"build/esp/userland/{app}.elf" for app in NORMAL_USER_APPS),
        *(ROOT / f"build/esp/userland/{app}.bmp" for app in NORMAL_USER_APPS),
        ROOT / "build/esp/etc/leonos.conf",
        *doc_outputs,
    ]
    # 修改点2：删除隐式依赖 modinfo.sh
    write_line(lines, f"build {r(esp_outputs[0])}: grub_efi boot/grub/embedded.cfg")
    write_line(lines, f"build {r(esp_outputs[1])}: copy boot/grub/grub.cfg")
    write_line(lines, f"build {r(esp_outputs[2])}: copy {r(loader_elf)}")
    write_line(lines, f"build {r(esp_outputs[3])}: copy {r(kernel_sys)}")
    write_line(lines, f"build {r(esp_outputs[4])}: copy {r(middlelayer_sys)}")
    write_line(lines, f"build {r(ROOT / 'build/esp/system/osmlayer.manifest')}: manifest")
    for dst, src in SYSTEM_FILES:
        if src:
            write_line(lines, f"build {r(ROOT / 'build/esp' / dst)}: copy {src}")
    system_output_count = len(SYSTEM_FILES)
    app_output_start = 5 + system_output_count
    app_output_end = app_output_start + len(normal_user_elfs)
    for output, elf in zip(esp_outputs[app_output_start:app_output_end], normal_user_elfs):
        write_line(lines, f"build {r(output)}: copy {r(elf)}")
    icon_output_end = app_output_end + len(normal_user_icon_sources)
    for output, icon in zip(esp_outputs[app_output_end:icon_output_end], normal_user_icon_sources):
        write_line(lines, f"build {r(output)}: copy {r(icon)}")
    write_line(lines, "build build/esp/etc/leonos.conf: copy .config")
    for output, src in zip(doc_outputs, doc_sources):
        write_line(lines, f"build {r(output)}: copy {r(src)}")
    write_line(lines, f"build esp: phony {' '.join(r(p) for p in esp_outputs)}")

    vmdk = ROOT / "build/images/leonos4.vmdk"
    write_line(lines, f"build {r(vmdk)}: image {' '.join(r(p) for p in esp_outputs)} | tools/make_image.py")
    iso = ROOT / "build/images/leonos4.iso"
    system_deps = " ".join(r(ROOT / "build/esp" / dst) for dst, _ in SYSTEM_FILES)
    normal_user_icon_deps = [ROOT / f"build/esp/userland/{app}.bmp" for app in NORMAL_USER_APPS]
    doc_deps = " ".join(r(p) for p in doc_outputs)
    etc_deps = "build/esp/etc/leonos.conf"
    write_line(lines, f"build {r(iso)}: iso {r(loader_elf)} {r(kernel_sys)} {r(middlelayer_sys)} {' '.join(r(e) for e in normal_user_elfs)} {' '.join(r(p) for p in normal_user_icon_deps)} {system_deps} {doc_deps} {etc_deps} boot/grub/grub.cfg")
    installer_root = ROOT / "build/install/root.fat"
    installer_elf_deps = " ".join(r(user_elfs_by_app[app]) for app in INSTALLER_USER_APPS)
    installer_icon_deps = " ".join(
        r(ROOT / "build" / "generated" / "app-icons" / f"{app}.bmp")
        for app in INSTALLER_USER_APPS
    )
    installer_policy_deps = " ".join(r(elf) for elf in installer_policy_elfs_by_app.values())
    write_line(lines, f"build {r(installer_root)}: installer_root {' '.join(r(p) for p in esp_outputs)} {installer_policy_deps} {installer_elf_deps} {installer_icon_deps} tools/make_installer_root.py")
    installer_iso = ROOT / "build/images/leonos4-installer.iso"
    # 修改点3：删除隐式依赖 modinfo.sh
    write_line(lines, f"build {r(installer_iso)}: installer_iso {r(loader_elf)} {r(kernel_sys)} {r(middlelayer_sys)} {r(installer_root)} boot/grub/installer.cfg boot/grub/installer_embedded.cfg tools/make_installer_iso.py")
    write_line(lines, f"build image-vmdk: phony {r(vmdk)}")
    write_line(lines, f"build image-iso: phony {r(iso)}")
    write_line(lines, f"build installer: phony {r(installer_iso)}")
    write_line(lines, f"build all: phony loader kernel middlelayer userland esp")
    write_line(lines, f"build run: run {r(vmdk)}")
    write_line(lines, f"build run-debug: run_debug {r(vmdk)}")
    write_line(lines, f"build run-iso: run_iso {r(iso)} {r(vmdk)}")
    write_line(lines, "build menuconfig: menuconfig_rule tools/kconfig_sync.py Kconfig configs/default.conf")
    write_line(lines, "build clean: clean_rule")
    write_line(lines, "default all")

    text = "\n".join(lines) + "\n"

    out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
