#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
from pathlib import Path
from make_ext2_root import write_ext2_root


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


def share_identical_payload_files(stage: Path) -> None:
    """ext2 preserves these hard links while exposing both complete trees."""
    seen = {}
    for path in sorted(stage.rglob("*")):
        relative = path.relative_to(stage).as_posix()
        if relative.startswith("install/root/"):
            relative = relative.removeprefix("install/root/")
        if not relative.startswith(("opt/", "bin/", "usr/", "share/")):
            continue
        if not path.is_file() or path.is_symlink():
            continue
        status = path.stat()
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").digest()
        key = (status.st_size, status.st_mode, status.st_uid, status.st_gid, digest)
        previous = seen.get(key)
        if previous is None:
            seen[key] = path
        else:
            path.unlink()
            os.link(previous, path)


def stage_installed_payloads(esp_tree: Path, destination: Path) -> None:
    """Split normal staging into ext2 root and the minimal FAT32 boot payload."""
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
    parser = argparse.ArgumentParser(description="Create LeonOS installer runtime ext2 root")
    parser.add_argument("--out", default="build/install/root.fat")
    parser.add_argument("--stage", default="build/install/root")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--installed-policy-dir", default="build/userland-installer-policy")
    parser.add_argument("--policy-apps", nargs="*", default=("desktop", "oobe", "settings"))
    parser.add_argument("--userland-dir", default="build/userland")
    parser.add_argument("--gptinit", default="build/userland-installer/gptinit.elf")
    parser.add_argument("--policy-runtime", default="build/userland-installer-policy/libleonos.so.2")
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
    gptinit = ROOT / args.gptinit
    generated_icons_dir = ROOT / args.generated_icons_dir
    policy_runtime = ROOT / args.policy_runtime

    if not esp_tree.exists():
        raise FileNotFoundError(f"missing normal ESP payload: {esp_tree}")
    if not installed_policy_dir.exists():
        raise FileNotFoundError(f"missing installed policy directory: {installed_policy_dir}")
    if (not userland_dir.exists() or not generated_icons_dir.exists() or
            not policy_runtime.is_file() or not gptinit.is_file()):
        raise FileNotFoundError("missing installer build inputs")

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    copy_file(userland_dir / "authd.elf", stage / "system/apps/authd/authd.elf")
    copy_file(userland_dir / "imd.elf", stage / "system/apps/imd/imd.elf")
    copy_file(userland_dir / "windowd.elf", stage / "system/apps/windowd/windowd.elf")
    copy_file(userland_dir / "desktop.elf", stage / "system/apps/desktop/desktop.elf")
    copy_file(userland_dir / "installer.elf", stage / "system/apps/installer/installer.elf")
    # Advanced installer mode enters the installer root directly through the
    # BusyBox shell, so keep the command environment available on the ISO.
    copy_file(userland_dir / "busybox.elf", stage / "programs/busybox/busybox.elf")
    # Ship the selected terminal packages in both the live installer and its payload.
    copy_tree(esp_tree / "bin", stage / "bin")
    copy_tree(esp_tree / "usr", stage / "usr")
    if (esp_tree / "opt").is_dir():
        copy_tree(esp_tree / "opt", stage / "opt")
    if (esp_tree / "programs/vim").is_dir():
        copy_tree(esp_tree / "programs/vim", stage / "programs/vim")
    # gptinit is installer-only and must never enter the installed root tree.
    copy_file(gptinit, stage / "programs/gptinit/gptinit.elf")
    (stage / "programs/gptinit/manifest.ini").write_text(
        "[app]\nid=gptinit\nname=GPT initializer\nversion=installer\n"
        "category=Installer tools\nexec=gptinit.elf\nentry=0\nterminal=1\n"
        "hidden=1\ncommands=gptinit\n",
        encoding="ascii",
    )
    copy_file(esp_tree / "system/apps/dynlinkerror/dynlinkerror.elf",
              stage / "system/apps/dynlinkerror/dynlinkerror.elf")
    copy_file(generated_icons_dir / "desktop.bmp", stage / "system/apps/desktop/desktop.bmp")
    copy_file(generated_icons_dir / "installer.bmp", stage / "system/apps/installer/installer.bmp")
    copy_tree(esp_tree / "system/config", stage / "system/config")
    (stage / "system/state").mkdir(parents=True, exist_ok=True)
    # Keep the conventional live-environment mount point available before the
    # advanced shell starts.  Without this, the documented `mkdir /mnt/esp`
    # and `mkdir /mnt/root` commands fail because POSIX mkdir does not create
    # missing parents unless -p is supplied.  Keep /root available as the
    # default HOME for the installer advanced shell as well.
    # Installer mount targets must exist before mount(2) is called: the
    # Linux mount ABI requires the target directory to already be present.
    for directory in ("mnt", "tmp", "media", "root", "target"):
        (stage / directory).mkdir(parents=True, exist_ok=True)
    (stage / "tmp").chmod(0o1777)
    # Both the live environment and installed payload retain case-sensitive
    # Linux toolchain headers. The EFI boot payload remains FAT32.
    (stage / "system/osmlayer.manifest").write_text(
        "name=osmlayer\nabi=2\nroot=/\nfs=ext2\ngui=desktop.elf\n",
        encoding="ascii",
    )
    copy_file(esp_tree / "system/fonts/leonos-metro.ttf", stage / "system/fonts/leonos-metro.ttf")
    copy_file(esp_tree / "system/fonts/leonos-win95.ttf", stage / "system/fonts/leonos-win95.ttf")
    copy_file(esp_tree / "system/fonts/times-new-roman.ttf", stage / "system/fonts/times-new-roman.ttf")
    copy_file(esp_tree / "system/fonts/simsun.ttc", stage / "system/fonts/simsun.ttc")
    copy_tree(esp_tree / "system/certs", stage / "system/certs")
    copy_tree(esp_tree / "system/resources", stage / "system/resources")
    copy_tree(esp_tree / "drivers", stage / "drivers")
    copy_tree(esp_tree / "lib", stage / "lib")
    copy_tree(esp_tree / "share/licenses", stage / "share/licenses")
    if (esp_tree / "share/examples").is_dir():
        copy_tree(esp_tree / "share/examples", stage / "share/examples")
    copy_file(policy_runtime, stage / "system/lib/libleonos.so.2")
    # Unix IPC service sockets live in /run/leonos and procfs is fixed at
    # /proc; FAT/exFAT have no permission bits, so service-side SO_PEERCRED
    # checks are the access-control boundary.
    (stage / "run/leonos").mkdir(parents=True, exist_ok=True)
    (stage / "proc").mkdir(parents=True, exist_ok=True)
    (stage / "etc").mkdir(parents=True, exist_ok=True)
    (stage / "etc/resolv.conf").write_text("nameserver 1.1.1.1\n", encoding="ascii")
    (stage / "etc/machine-id").write_text("00000000000000000000000000000000\n", encoding="ascii")
    (stage / "system/config/users.db").write_bytes(b"")
    stage_installed_payloads(esp_tree, stage)
    (stage / "install/root/run/leonos").mkdir(parents=True, exist_ok=True)
    (stage / "install/root/proc").mkdir(parents=True, exist_ok=True)
    (stage / "install/root/etc").mkdir(parents=True, exist_ok=True)
    (stage / "install/root/etc/resolv.conf").write_text("nameserver 1.1.1.1\n", encoding="ascii")
    (stage / "install/root/etc/machine-id").write_text("00000000000000000000000000000000\n", encoding="ascii")
    (stage / "install/root/system/config/users.db").write_bytes(b"")
    copy_file(policy_runtime, stage / "install/root/system/lib/libleonos.so.2")
    remove_file(stage / "install/root/etc/license.conf")
    remove_file(stage / "install/root/etc/install.id")
    for app in args.policy_apps:
        if app not in {"desktop", "oobe", "settings"}:
            raise ValueError(f"unsupported installer policy app: {app}")
        name = f"{app}.elf"
        copy_file(installed_policy_dir / name,
                  stage / "install/root/system/apps" / app / name)

    share_identical_payload_files(stage)
    write_ext2_root(stage, out, args.size_mib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
