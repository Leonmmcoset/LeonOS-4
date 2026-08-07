#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for item in src.rglob("*"):
        rel = item.relative_to(src)
        target = dst / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS 4 GPT/FAT32 raw disk and VMDK")
    parser.add_argument("--out", default="build/images/leonos4.vmdk")
    parser.add_argument("--raw", default="build/images/leonos4.raw")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--default-language", choices=("en", "zh"), default="en",
                        help="Language override written only to this VMDK image")
    parser.add_argument("--size-mib", type=int, default=96)
    args = parser.parse_args()

    raw = ROOT / args.raw
    out = ROOT / args.out
    esp_tree = ROOT / args.esp_tree
    if not esp_tree.is_dir():
        raise SystemExit(f"ESP staging tree does not exist: {esp_tree}")
    image_dir = out.parent
    image_dir.mkdir(parents=True, exist_ok=True)
    raw.parent.mkdir(parents=True, exist_ok=True)

    if raw.exists():
        raw.unlink()
    if out.exists():
        out.unlink()

    run(["truncate", "-s", f"{args.size_mib}M", str(raw)])
    start_sector = 2048
    total_sectors = raw.stat().st_size // 512
    end_sector = total_sectors - 2048
    run([
        "sgdisk",
        "--clear",
        f"--new=1:{start_sector}:{end_sector}",
        "--typecode=1:ef00",
        "--change-name=1:LEONOS4_ESP",
        str(raw),
    ])

    offset = start_sector * 512
    fat_img = image_dir / "esp.fat"
    if fat_img.exists():
        fat_img.unlink()
    esp_size = (end_sector - start_sector + 1) * 512
    run(["truncate", "-s", str(esp_size), str(fat_img)])
    # Keep this a standards-sized FAT32 while avoiding mkfs.fat's 512-byte
    # default.  With the old layout one 4 KiB archive write allocated and
    # updated eight FAT entries; large API payloads such as Doom's WAD could
    # then exhaust the virtual AHCI write budget.
    run(["mkfs.fat", "-F", "32", "-s", "4", "-n", "LEONOS4", str(fat_img)])

    # VMDK has a build-time language seed. Keep it in a private copy so the
    # shared ESP staging tree remains unchanged; image-iso therefore retains
    # the installer/default locale selected by its own flow.
    with tempfile.TemporaryDirectory(prefix="leonos-vmdk-esp-") as temp_dir:
        image_esp = Path(temp_dir) / "esp"
        copy_tree(esp_tree, image_esp)
        locale_path = image_esp / "system/config/locale.conf"
        locale_path.parent.mkdir(parents=True, exist_ok=True)
        locale_path.write_text(f"lang={args.default_language}\n", encoding="utf-8")
        items = sorted(image_esp.iterdir())
        for item in items:
            run(["mcopy", "-s", "-i", str(fat_img), str(item), "::/"])

    run(["dd", f"if={fat_img}", f"of={raw}", "bs=512", f"seek={start_sector}", "conv=notrunc", "status=none"])
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk", str(raw), str(out)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
