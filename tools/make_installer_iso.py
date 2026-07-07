#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GRUB_EFI_DIR = ROOT / "build/deps/grub-efi-amd64-bin/usr/lib/grub/x86_64-efi"
GRUB_MODULES = (
    "part_gpt fat iso9660 multiboot2 normal search search_fs_file configfile echo serial "
    "terminal video video_bochs video_cirrus efi_gop efi_uga all_video gfxterm"
)


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def build_installer_boot_efi() -> Path:
    out = ROOT / "build/install/installer-BOOTX64.EFI"
    out.parent.mkdir(parents=True, exist_ok=True)
    run([
        "grub-mkstandalone",
        "-O",
        "x86_64-efi",
        "-o",
        str(out),
        f"--modules={GRUB_MODULES}",
        "boot/grub/grub.cfg=boot/grub/installer_embedded.cfg",
    ])
    return out


def stage_installer_tree(stage: Path, boot_image: Path, boot_efi: Path) -> None:
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    copy_file(boot_efi, stage / "EFI/BOOT/BOOTX64.EFI")
    copy_file(ROOT / "boot/grub/installer.cfg", stage / "boot/grub/grub.cfg")
    (stage / "boot/leonos-installer-iso.marker").write_text("LeonOS installer ISO volume\n", encoding="ascii")
    copy_file(ROOT / "build/boot/loader.elf", stage / "boot/loader.elf")
    copy_file(ROOT / "build/system/kernel.sys", stage / "system/kernel.sys")
    copy_file(ROOT / "build/system/middlelayer.sys", stage / "system/middlelayer.sys")
    copy_file(ROOT / "build/install/root.fat", stage / "install/root.fat")
    copy_file(boot_image, stage / "boot/efiboot.img")


def create_boot_image(boot_image: Path, boot_efi: Path) -> None:
    boot_stage = ROOT / "build/install/efi-boot"
    if boot_stage.exists():
        shutil.rmtree(boot_stage)
    boot_stage.mkdir(parents=True)
    copy_file(boot_efi, boot_stage / "EFI/BOOT/BOOTX64.EFI")

    payload_bytes = sum(item.stat().st_size for item in boot_stage.rglob("*") if item.is_file())
    required_mib = (payload_bytes + (1024 * 1024 - 1)) // (1024 * 1024)
    size_mib = required_mib + 8
    if size_mib < 16:
        size_mib = 16

    boot_image.parent.mkdir(parents=True, exist_ok=True)
    if boot_image.exists():
        boot_image.unlink()
    run(["truncate", "-s", f"{size_mib}M", str(boot_image)])
    run(["mkfs.fat", "-F", "16", "-n", "LEONOSINST", str(boot_image)])
    for item in sorted(boot_stage.iterdir()):
        run(["mcopy", "-s", "-i", str(boot_image), str(item), "::/"])


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS UEFI installer ISO")
    parser.add_argument("--out", default="build/images/leonos4-installer.iso")
    parser.add_argument("--stage", default="build/installer-iso")
    parser.add_argument("--boot-image", default="build/install/installer-efiboot.img")
    args = parser.parse_args()

    out = ROOT / args.out
    stage = ROOT / args.stage
    boot_image = ROOT / args.boot_image

    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    boot_efi = build_installer_boot_efi()
    create_boot_image(boot_image, boot_efi)
    stage_installer_tree(stage, boot_image, boot_efi)
    run([
        "xorriso",
        "-as",
        "mkisofs",
        "-iso-level",
        "3",
        "-R",
        "-J",
        "-V",
        "LEONOS4INST",
        "-e",
        "boot/efiboot.img",
        "-no-emul-boot",
        "-o",
        str(out),
        str(stage),
    ])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
