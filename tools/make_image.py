#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


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

    # VMDK has a build-time language seed. Copy the staged files directly and
    # then replace only locale.conf, so image-iso retains the shared staging
    # tree without paying for a complete temporary ESP-tree copy on every run.
    for item in sorted(esp_tree.iterdir()):
        run(["mcopy", "-s", "-i", str(fat_img), str(item), "::/"])
    with tempfile.TemporaryDirectory(prefix="leonos-vmdk-esp-") as temp_dir:
        locale_path = Path(temp_dir) / "locale.conf"
        locale_path.write_text(f"lang={args.default_language}\n", encoding="utf-8")
        run(["mcopy", "-o", "-i", str(fat_img), str(locale_path),
             "::/system/config/locale.conf"])

    # `bs=512` issued roughly 380,000 writes for a 192 MiB image. This is
    # especially slow on WSL's Windows-mounted worktree. Keep the byte offset
    # exact but copy in large, sequential chunks.
    run(["dd", f"if={fat_img}", f"of={raw}", "bs=4M",
         f"seek={start_sector * 512}", "oflag=seek_bytes", "conv=notrunc",
         "status=none"])
    run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk", str(raw), str(out)])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
