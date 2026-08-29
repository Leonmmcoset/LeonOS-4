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


def stage_installed_payloads(esp_tree: Path, destination: Path) -> None:
    """Split normal staging into exFAT root and the minimal FAT32 boot payload."""
    root = destination / "install/root"
    esp = destination / "install/esp"
    copy_tree(esp_tree, root)
    shutil.rmtree(root / "EFI", ignore_errors=True)
    shutil.rmtree(root / "grub", ignore_errors=True)
    remove_file(root / "loader.elf")
    remove_file(root / "system/kernel.sys")
    remove_file(root / "system/middlelayer.sys")

    copy_file(esp_tree / "EFI/BOOT/BOOTX64.EFI", esp / "EFI/BOOT/BOOTX64.EFI")
    copy_tree(esp_tree / "grub", esp / "grub")
    copy_file(esp_tree / "loader.elf", esp / "loader.elf")
    copy_file(esp_tree / "system/kernel.sys", esp / "system/kernel.sys")
    copy_file(esp_tree / "system/middlelayer.sys", esp / "system/middlelayer.sys")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS installer runtime FAT root")
    parser.add_argument("--out", default="build/install/root.fat")
    parser.add_argument("--stage", default="build/install/root")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--installed-policy-dir", default="build/userland-installer-policy")
    parser.add_argument("--policy-apps", nargs="*", default=("desktop", "oobe", "settings"))
    parser.add_argument("--userland-dir", default="build/userland")
    parser.add_argument("--policy-runtime", default="build/userland-installer-policy/libleonos.so.1")
    parser.add_argument("--generated-icons-dir", default="build/generated/app-icons")
    # Accepted only so a build.py process started before the payload split can
    # finish. New build graphs no longer pass this option.
    parser.add_argument("--manifest", help=argparse.SUPPRESS)
    parser.add_argument("--size-mib", type=int, default=64)
    args = parser.parse_args()

    out = ROOT / args.out
    stage = ROOT / args.stage
    esp_tree = ROOT / args.esp_tree
    installed_policy_dir = ROOT / args.installed_policy_dir
    userland_dir = ROOT / args.userland_dir
    generated_icons_dir = ROOT / args.generated_icons_dir
    policy_runtime = ROOT / args.policy_runtime

    if not esp_tree.exists():
        raise FileNotFoundError(f"missing normal ESP payload: {esp_tree}")
    if not installed_policy_dir.exists():
        raise FileNotFoundError(f"missing installed policy directory: {installed_policy_dir}")
    if (not userland_dir.exists() or not generated_icons_dir.exists() or
            not policy_runtime.is_file()):
        raise FileNotFoundError("missing installer build inputs")

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    copy_file(userland_dir / "desktop.elf", stage / "system/apps/desktop/desktop.elf")
    copy_file(userland_dir / "installer.elf", stage / "system/apps/installer/installer.elf")
    copy_file(esp_tree / "system/apps/dynlinkerror/dynlinkerror.elf",
              stage / "system/apps/dynlinkerror/dynlinkerror.elf")
    copy_file(generated_icons_dir / "desktop.bmp", stage / "system/apps/desktop/desktop.bmp")
    copy_file(generated_icons_dir / "installer.bmp", stage / "system/apps/installer/installer.bmp")
    copy_tree(esp_tree / "system/config", stage / "system/config")
    (stage / "system/state").mkdir(parents=True, exist_ok=True)
    # The installer itself runs from this FAT32 ramdisk. The staged installed
    # The root payload retains the normal exFAT manifest copied from esp_tree;
    # the installer can also update an existing ext2 target.
    (stage / "system/osmlayer.manifest").write_text(
        "name=osmlayer\nabi=2\nroot=/\nfs=fat32\ngui=desktop.elf\n",
        encoding="ascii",
    )
    copy_file(esp_tree / "system/fonts/leonos-metro.ttf", stage / "system/fonts/leonos-metro.ttf")
    copy_file(esp_tree / "system/fonts/leonos-win95.ttf", stage / "system/fonts/leonos-win95.ttf")
    copy_file(esp_tree / "system/fonts/times-new-roman.ttf", stage / "system/fonts/times-new-roman.ttf")
    copy_file(esp_tree / "system/fonts/simsun.ttc", stage / "system/fonts/simsun.ttc")
    copy_tree(esp_tree / "system/certs", stage / "system/certs")
    copy_tree(esp_tree / "system/resources", stage / "system/resources")
    copy_tree(esp_tree / "drivers", stage / "drivers")
    copy_file(esp_tree / "system/lib/ld-leonos.elf", stage / "system/lib/ld-leonos.elf")
    copy_file(policy_runtime, stage / "system/lib/libleonos.so.1")
    stage_installed_payloads(esp_tree, stage)
    copy_file(policy_runtime, stage / "install/root/system/lib/libleonos.so.1")
    remove_file(stage / "install/root/etc/license.conf")
    remove_file(stage / "install/root/etc/install.id")
    for app in args.policy_apps:
        if app not in {"desktop", "oobe", "settings"}:
            raise ValueError(f"unsupported installer policy app: {app}")
        name = f"{app}.elf"
        copy_file(installed_policy_dir / name,
                  stage / "install/root/system/apps" / app / name)

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
