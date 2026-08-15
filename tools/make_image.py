#!/usr/bin/env python3
"""Create a LeonOS GPT disk with a FAT32 ESP and an ext2 runtime root."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SECTOR_SIZE = 512
ESP_FIRST_SECTOR = 2048


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def make_boot_tree(staging: Path, destination: Path) -> None:
    """Stage only files GRUB and the LeonOS loader must read before ext2 mounts."""
    copy_file(staging / "EFI/BOOT/BOOTX64.EFI", destination / "EFI/BOOT/BOOTX64.EFI")
    shutil.copytree(staging / "boot", destination / "boot", dirs_exist_ok=True)
    copy_file(staging / "system/kernel.sys", destination / "system/kernel.sys")
    copy_file(staging / "system/middlelayer.sys", destination / "system/middlelayer.sys")


def make_root_tree(staging: Path, destination: Path, language: str) -> None:
    """Stage the normal writable root without duplicating ESP-only boot files."""
    shutil.copytree(staging, destination, dirs_exist_ok=True)
    shutil.rmtree(destination / "EFI", ignore_errors=True)
    shutil.rmtree(destination / "boot", ignore_errors=True)
    for name in ("kernel.sys", "middlelayer.sys"):
        (destination / "system" / name).unlink(missing_ok=True)
    locale = destination / "system/config/locale.conf"
    locale.parent.mkdir(parents=True, exist_ok=True)
    locale.write_text(f"lang={language}\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS 4 GPT FAT32-ESP/ext2-root VMDK")
    parser.add_argument("--out", default="build/images/leonos4.vmdk")
    parser.add_argument("--raw", default="build/images/leonos4.raw")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--root-image", default="build/images/root.ext2")
    parser.add_argument("--esp-image", default="build/images/esp.fat")
    parser.add_argument("--default-language", choices=("en", "zh"), default="en",
                        help="Language seed written into this VMDK root filesystem")
    parser.add_argument("--size-mib", type=int, default=512)
    parser.add_argument("--esp-size-mib", type=int, default=128)
    args = parser.parse_args()

    raw = ROOT / args.raw
    out = ROOT / args.out
    esp_tree = ROOT / args.esp_tree
    root_image = ROOT / args.root_image
    esp_image = ROOT / args.esp_image
    if not esp_tree.is_dir():
        raise SystemExit(f"ESP staging tree does not exist: {esp_tree}")
    if args.esp_size_mib < 128 or args.size_mib <= args.esp_size_mib + 128:
        raise SystemExit("VMDK needs a 128 MiB+ FAT32 ESP and at least 128 MiB ext2 root space")

    for path in (raw, out, root_image, esp_image):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.unlink(missing_ok=True)

    run(["truncate", "-s", f"{args.size_mib}M", str(raw)])
    total_sectors = raw.stat().st_size // SECTOR_SIZE
    esp_sectors = args.esp_size_mib * 1024 * 1024 // SECTOR_SIZE
    esp_last = ESP_FIRST_SECTOR + esp_sectors - 1
    root_first = (esp_last + 1 + 2047) & ~2047
    root_last = total_sectors - 2048
    if root_last <= root_first or root_last - root_first + 1 < 262144:
        raise SystemExit("VMDK root partition is smaller than the 128 MiB ext2 minimum")
    run([
        "sgdisk", "--clear",
        f"--new=1:{ESP_FIRST_SECTOR}:{esp_last}", "--typecode=1:ef00",
        "--change-name=1:LEONOS4_ESP",
        f"--new=2:{root_first}:{root_last}", "--typecode=2:8300",
        "--change-name=2:LEONOS4_ROOT",
        str(raw),
    ])

    with tempfile.TemporaryDirectory(prefix="leonos-vmdk-") as temp_dir:
        temp = Path(temp_dir)
        boot_tree = temp / "esp"
        root_tree = temp / "root"
        make_boot_tree(esp_tree, boot_tree)
        make_root_tree(esp_tree, root_tree, args.default_language)

        run(["truncate", "-s", str(esp_sectors * SECTOR_SIZE), str(esp_image)])
        run(["mkfs.fat", "-F", "32", "-s", "2", "-n", "LEONOS4ESP", str(esp_image)])
        for item in sorted(boot_tree.iterdir()):
            run(["mcopy", "-s", "-i", str(esp_image), str(item), "::/"])

        root_bytes = (root_last - root_first + 1) * SECTOR_SIZE
        run(["truncate", "-s", str(root_bytes), str(root_image)])
        # The kernel supports the stable classic ext2 subset only. Explicitly
        # disable modern ext4 extensions rather than relying on host defaults.
        run([
            "mke2fs", "-q", "-t", "ext2", "-F", "-b", "4096", "-I", "128",
            "-O", "^has_journal,^resize_inode,^dir_index,^metadata_csum,^64bit",
            "-d", str(root_tree), str(root_image),
        ])

    run(["dd", f"if={esp_image}", f"of={raw}", "bs=4M",
         f"seek={ESP_FIRST_SECTOR * SECTOR_SIZE}", "oflag=seek_bytes",
         "conv=notrunc", "status=none"])
    run(["dd", f"if={root_image}", f"of={raw}", "bs=4M",
         f"seek={root_first * SECTOR_SIZE}", "oflag=seek_bytes",
         "conv=notrunc", "status=none"])
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk", str(raw), str(out)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
