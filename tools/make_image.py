#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
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
    parser.add_argument("--size-mib", type=int, default=96)
    args = parser.parse_args()

    raw = ROOT / args.raw
    out = ROOT / args.out
    esp_tree = ROOT / args.esp_tree
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
    run(["mkfs.fat", "-F", "32", "-n", "LEONOS4", str(fat_img)])

    for item in sorted(esp_tree.iterdir()):
        run(["mcopy", "-s", "-i", str(fat_img), str(item), "::/"])

    run(["dd", f"if={fat_img}", f"of={raw}", "bs=512", f"seek={start_sector}", "conv=notrunc", "status=none"])
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk", str(raw), str(out)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
