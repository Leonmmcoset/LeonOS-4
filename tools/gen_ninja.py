#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


def write_line(lines: list[str], line: str = "") -> None:
    lines.append(line)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LeonOS 4 build.ninja")
    parser.add_argument("--out", default="build.ninja")
    args = parser.parse_args()

    out = ROOT / args.out
    lines: list[str] = []

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
    cflags_user = (
        "-target x86_64-unknown-none -std=c11 -ffreestanding -fno-stack-protector "
        "-fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Iuserland/libc/include -Iinclude"
    )
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
    write_line(lines, "rule rust_osmlayer")
    write_line(lines, "  command = $rustc --crate-type staticlib --target x86_64-unknown-none -C panic=abort -C relocation-model=static -C code-model=kernel -C no-redzone=yes -C target-feature=-sse,-sse2 -C opt-level=1 --emit link=$out $in")
    write_line(lines, "  description = RUST.OSMLAYER $out")
    write_line(lines)
    write_line(lines, "rule link_kernel")
    write_line(lines, "  command = $ld -nostdlib -z max-page-size=0x1000 -T kernel/ntclks/arch/x86_64/linker.ld -o $out $in")
    write_line(lines, "  description = LD.KERNEL $out")
    write_line(lines)
    write_line(lines, "rule cc_user")
    write_line(lines, f"  command = $cc {cflags_user} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = CC.USER $out")
    write_line(lines)
    write_line(lines, "rule as_user")
    write_line(lines, f"  command = $cc {asflags_user} -MMD -MF $out.d -c $in -o $out")
    write_line(lines, "  depfile = $out.d")
    write_line(lines, "  deps = gcc")
    write_line(lines, "  description = AS.USER $out")
    write_line(lines)
    write_line(lines, "rule ar")
    write_line(lines, "  command = $ar rcs $out $in")
    write_line(lines, "  description = AR $out")
    write_line(lines)
    write_line(lines, "rule link_user")
    write_line(lines, "  command = $ld -nostdlib -z max-page-size=0x1000 -T userland/linker.ld -o $out $in")
    write_line(lines, "  description = LD.USER $out")
    write_line(lines)
    write_line(lines, "rule copy")
    write_line(lines, "  command = mkdir -p `dirname $out` && cp $in $out")
    write_line(lines, "  description = COPY $out")
    write_line(lines)
    write_line(lines, "rule mkdir")
    write_line(lines, "  command = mkdir -p $out")
    write_line(lines, "  description = MKDIR $out")
    write_line(lines)
    write_line(lines, "rule manifest")
    write_line(lines, "  command = mkdir -p `dirname $out` && printf 'name=osmlayer\\nabi=1\\nroot=0:/\\nfs=fat32\\ngui=desktop.elf\\n' > $out")
    write_line(lines, "  description = MANIFEST $out")
    write_line(lines)
    write_line(lines, "rule grub_efi")
    write_line(lines, "  command = mkdir -p `dirname $out` && grub-mkstandalone -d build/deps/grub-efi-amd64-bin/usr/lib/grub/x86_64-efi -O x86_64-efi -o $out --modules='part_gpt fat multiboot2 normal search search_fs_file configfile echo serial terminal' boot/grub/grub.cfg=boot/grub/grub.cfg")
    write_line(lines, "  description = GRUB.EFI $out")
    write_line(lines)
    write_line(lines, "rule iso")
    write_line(lines, "  command = rm -rf build/iso && mkdir -p build/iso/boot/grub build/iso/system build/iso/userland build/iso/etc && cp boot/grub/grub.cfg build/iso/boot/grub/grub.cfg && cp build/x86_64/ntclks.elf build/iso/boot/ntclks.elf && cp build/esp/system/osmlayer.manifest build/iso/system/osmlayer.manifest && cp build/userland/init.elf build/iso/userland/init.elf && cp build/userland/desktop.elf build/iso/userland/desktop.elf && cp build/userland/hello.elf build/iso/userland/hello.elf && cp build/userland/uidemo.elf build/iso/userland/uidemo.elf && cp build/userland/taskmgr.elf build/iso/userland/taskmgr.elf && cp build/userland/fileman.elf build/iso/userland/fileman.elf && cp build/userland/terminal.elf build/iso/userland/terminal.elf && cp build/userland/shell.elf build/iso/userland/shell.elf && cp build/userland/notepad.elf build/iso/userland/notepad.elf && cp build/userland/calc.elf build/iso/userland/calc.elf && cp build/userland/run.elf build/iso/userland/run.elf && cp configs/default.conf build/iso/etc/leonos.conf && grub-mkrescue -o $out build/iso")
    write_line(lines, "  description = ISO $out")
    write_line(lines)
    write_line(lines, "rule image")
    write_line(lines, "  command = python3 tools/make_image.py --out $out --raw build/images/leonos4.raw --esp-tree build/esp")
    write_line(lines, "  description = IMAGE $out")
    write_line(lines)
    write_line(lines, "rule run")
    write_line(lines, "  command = qemu-system-x86_64 -enable-kvm -cpu host -machine q35 -m 512M -bios /usr/share/ovmf/OVMF.fd -serial stdio -display gtk,grab-on-hover=on,show-cursor=off -drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN LeonOS 4")
    write_line(lines)
    write_line(lines, "rule run_debug")
    write_line(lines, "  command = qemu-system-x86_64 -enable-kvm -cpu host -machine q35 -m 512M -bios /usr/share/ovmf/OVMF.fd -serial stdio -display none -no-reboot -no-shutdown -drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN.DEBUG LeonOS 4")
    write_line(lines)
    write_line(lines, "rule run_iso")
    write_line(lines, "  command = qemu-system-x86_64 -m 512M -serial stdio -display none -no-reboot -no-shutdown -cdrom build/images/leonos4.iso -drive file=build/images/leonos4.vmdk,if=none,id=sata0,format=vmdk -device ich9-ahci,id=ahci -device ide-hd,drive=sata0,bus=ahci.0")
    write_line(lines, "  description = RUN.ISO LeonOS 4")
    write_line(lines)
    write_line(lines, "rule menuconfig_rule")
    write_line(lines, "  command = test -f .config || cp configs/default.conf .config && kconfig-mconf Kconfig && python3 tools/kconfig_sync.py")
    write_line(lines, "  description = MENUCONFIG")
    write_line(lines)
    write_line(lines, "rule clean_rule")
    write_line(lines, "  command = rm -rf build include/generated .config")
    write_line(lines, "  description = CLEAN")
    write_line(lines)

    write_line(lines, "build include/generated/autoconf.h include/generated/rustcfg.args: kconfig_sync tools/kconfig_sync.py configs/default.conf | .config")
    write_line(lines, "build config-sync: phony include/generated/autoconf.h include/generated/rustcfg.args")

    kernel_sources = collect([
        "kernel/ntclks/**/*.c",
        "kernel/ntclks/**/*.S",
    ])
    kernel_objects: list[Path] = []
    for src in kernel_sources:
        obj = obj_for(src, "kernel")
        kernel_objects.append(obj)
        rule = "as_kernel" if src.suffix == ".S" else "cc_kernel"
        implicit = " | include/generated/autoconf.h" if src.suffix == ".c" else ""
        write_line(lines, f"build {r(obj)}: {rule} {r(src)}{implicit}")

    rust_sources = collect(["middlelayer/osmlayer/src/**/*.rs"])
    rust_lib = ROOT / "build" / "x86_64" / "libosmlayer.a"
    rust_implicit = " ".join(r(s) for s in rust_sources[1:])
    write_line(lines, f"build {r(rust_lib)}: rust_osmlayer {r(ROOT / 'middlelayer/osmlayer/src/lib.rs')} | include/generated/rustcfg.args {rust_implicit}")

    kernel_elf = ROOT / "build" / "x86_64" / "ntclks.elf"
    all_kernel_inputs = " ".join(r(o) for o in kernel_objects + [rust_lib])
    write_line(lines, f"build {r(kernel_elf)}: link_kernel {all_kernel_inputs}")
    write_line(lines, f"build kernel: phony {r(kernel_elf)}")

    libc_sources = collect(["userland/libc/src/*.c", "userland/libc/src/*.S"])
    libc_objects: list[Path] = []
    for src in libc_sources:
        obj = obj_for(src, "userlib")
        libc_objects.append(obj)
        rule = "as_user" if src.suffix == ".S" else "cc_user"
        write_line(lines, f"build {r(obj)}: {rule} {r(src)}")
    libc_a = ROOT / "build" / "userland" / "libc.a"
    write_line(lines, f"build {r(libc_a)}: ar {' '.join(r(o) for o in libc_objects)}")

    user_elfs: list[Path] = []
    for app in ["init", "desktop", "hello", "uidemo", "taskmgr", "fileman", "terminal", "shell", "notepad", "calc", "run"]:
        src = ROOT / "userland" / "apps" / app / "main.c"
        obj = obj_for(src, f"user-{app}")
        elf = ROOT / "build" / "userland" / f"{app}.elf"
        user_elfs.append(elf)
        write_line(lines, f"build {r(obj)}: cc_user {r(src)}")
        write_line(lines, f"build {r(elf)}: link_user {r(obj)} {r(libc_a)}")

    write_line(lines, f"build userland: phony {' '.join(r(e) for e in user_elfs)}")

    esp_outputs = [
        ROOT / "build/esp/EFI/BOOT/BOOTX64.EFI",
        ROOT / "build/esp/boot/grub/grub.cfg",
        ROOT / "build/esp/boot/ntclks.elf",
        ROOT / "build/esp/system/osmlayer.manifest",
        ROOT / "build/esp/userland/init.elf",
        ROOT / "build/esp/userland/desktop.elf",
        ROOT / "build/esp/userland/hello.elf",
        ROOT / "build/esp/userland/uidemo.elf",
        ROOT / "build/esp/userland/taskmgr.elf",
        ROOT / "build/esp/userland/fileman.elf",
        ROOT / "build/esp/userland/terminal.elf",
        ROOT / "build/esp/userland/shell.elf",
        ROOT / "build/esp/userland/notepad.elf",
        ROOT / "build/esp/userland/calc.elf",
        ROOT / "build/esp/userland/run.elf",
        ROOT / "build/esp/etc/leonos.conf",
    ]
    write_line(lines, f"build {r(esp_outputs[0])}: grub_efi boot/grub/grub.cfg | build/deps/grub-efi-amd64-bin/usr/lib/grub/x86_64-efi/modinfo.sh")
    write_line(lines, f"build {r(esp_outputs[1])}: copy boot/grub/grub.cfg")
    write_line(lines, f"build {r(esp_outputs[2])}: copy {r(kernel_elf)}")
    write_line(lines, f"build {r(esp_outputs[3])}: manifest")
    write_line(lines, f"build {r(esp_outputs[4])}: copy {r(user_elfs[0])}")
    write_line(lines, f"build {r(esp_outputs[5])}: copy {r(user_elfs[1])}")
    write_line(lines, f"build {r(esp_outputs[6])}: copy {r(user_elfs[2])}")
    write_line(lines, f"build {r(esp_outputs[7])}: copy {r(user_elfs[3])}")
    write_line(lines, f"build {r(esp_outputs[8])}: copy {r(user_elfs[4])}")
    write_line(lines, f"build {r(esp_outputs[9])}: copy {r(user_elfs[5])}")
    write_line(lines, f"build {r(esp_outputs[10])}: copy {r(user_elfs[6])}")
    write_line(lines, f"build {r(esp_outputs[11])}: copy {r(user_elfs[7])}")
    write_line(lines, f"build {r(esp_outputs[12])}: copy {r(user_elfs[8])}")
    write_line(lines, f"build {r(esp_outputs[13])}: copy {r(user_elfs[9])}")
    write_line(lines, f"build {r(esp_outputs[14])}: copy {r(user_elfs[10])}")
    write_line(lines, "build build/esp/etc/leonos.conf: copy configs/default.conf")
    write_line(lines, f"build esp: phony {' '.join(r(p) for p in esp_outputs)}")

    vmdk = ROOT / "build/images/leonos4.vmdk"
    write_line(lines, f"build {r(vmdk)}: image {' '.join(r(p) for p in esp_outputs)} | tools/make_image.py")
    iso = ROOT / "build/images/leonos4.iso"
    write_line(lines, f"build {r(iso)}: iso {r(kernel_elf)} {' '.join(r(e) for e in user_elfs)} build/esp/system/osmlayer.manifest boot/grub/grub.cfg configs/default.conf")
    write_line(lines, f"build image-vmdk: phony {r(vmdk)}")
    write_line(lines, f"build image-iso: phony {r(iso)}")
    write_line(lines, f"build all: phony kernel userland esp")
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
