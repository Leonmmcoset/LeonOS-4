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


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


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
            copy_file(item, target)


def remove_file(path: Path) -> None:
    if path.exists():
        path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS installer runtime FAT root")
    parser.add_argument("--out", default="build/install/root.fat")
    parser.add_argument("--stage", default="build/install/root")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--installed-policy-dir", default="build/userland-installer-policy")
    parser.add_argument("--size-mib", type=int, default=64)
    args = parser.parse_args()

    out = ROOT / args.out
    stage = ROOT / args.stage
    esp_tree = ROOT / args.esp_tree
    installed_policy_dir = ROOT / args.installed_policy_dir

    if not esp_tree.exists():
        raise FileNotFoundError(f"missing normal ESP payload: {esp_tree}")
    if not installed_policy_dir.exists():
        raise FileNotFoundError(f"missing installed policy directory: {installed_policy_dir}")

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    copy_file(ROOT / "build/userland/desktop.elf", stage / "userland/desktop.elf")
    copy_file(ROOT / "build/userland/installer.elf", stage / "userland/installer.elf")
    copy_file(ROOT / "build/generated/app-icons/desktop.bmp", stage / "userland/desktop.bmp")
    copy_file(ROOT / "build/generated/app-icons/installer.bmp", stage / "userland/installer.bmp")
    copy_file(esp_tree / "etc/leonos.conf", stage / "etc/leonos.conf")
    copy_file(ROOT / "build/esp/system/osmlayer.manifest", stage / "system/osmlayer.manifest")
    copy_file(ROOT / "system/fonts/system.psf", stage / "system/fonts/system.psf")
    copy_file(ROOT / "system/fonts/cjk16.lbf", stage / "system/fonts/cjk16.lbf")
    copy_file(ROOT / "system/fonts/metro-latin.lbf", stage / "system/fonts/metro-latin.lbf")
    copy_tree(esp_tree / "system/resources", stage / "system/resources")
    copy_tree(esp_tree / "drivers", stage / "drivers")
    copy_tree(esp_tree, stage / "install/esp")
    remove_file(stage / "install/esp/etc/license.conf")
    remove_file(stage / "install/esp/etc/install.id")
    for name in ("desktop.elf", "oobe.elf", "settings.elf"):
        copy_file(installed_policy_dir / name, stage / "install/esp/userland" / name)

    payload_bytes = sum(item.stat().st_size for item in stage.rglob("*") if item.is_file())
    required_mib = (payload_bytes + (1024 * 1024 - 1)) // (1024 * 1024)
    required_mib += 8
    size_mib = args.size_mib if args.size_mib > required_mib else required_mib

    run(["truncate", "-s", f"{size_mib}M", str(out)])
    run(["mkfs.fat", "-F", "32", "-n", "LEONOSINST", str(out)])
    for item in sorted(stage.iterdir()):
        run(["mcopy", "-s", "-i", str(out), str(item), "::/"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
