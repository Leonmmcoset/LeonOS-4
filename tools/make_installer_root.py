#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

from make_ext2_root import write_ext2_root

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from leonos_layout import (  # noqa: E402
    BIN,
    BOOT,
    ETC,
    ETC_LEONOS,
    ETC_SSL_CERTS,
    HOME,
    LEONOS_APPS,
    LEONOS_LIB,
    LIB,
    LICENSES,
    ROOT_SYMLINKS,
    layout_directories,
    tool_payload_paths,
    apply_root_symlinks,
    command_symlink,
    RUN_LEONOS,
    SBIN,
    USR,
    USR_BIN,
    USR_LIB,
    USR_SBIN,
    VAR_LIB_LEONOS,
    VAR_TMP,
)


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst, follow_symlinks=False)


def copy_tree(src: Path, dst: Path) -> None:
    """Copy a tree while preserving symlinks exactly as links."""
    if not src.exists() and not src.is_symlink():
        return
    if src.is_symlink():
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.unlink(missing_ok=True)
        dst.symlink_to(os.readlink(src))
        return
    if dst.is_symlink() or (dst.exists() and not dst.is_dir()):
        if dst.is_symlink() or dst.is_file():
            dst.unlink()
        else:
            shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True, dirs_exist_ok=True)


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
    elif path.is_dir():
        shutil.rmtree(path)


def remove_file(path: Path) -> None:
    path.unlink(missing_ok=True)


def share_identical_payload_files(stage: Path) -> None:
    """ext2 preserves these hard links while exposing both complete trees.

    Only regular files are deduplicated; symlinks are never replaced with
    hard links because the usr layout contract requires real links.
    """
    seen = {}
    for path in sorted(stage.rglob("*")):
        relative = path.relative_to(stage).as_posix()
        if relative.startswith("install/root/"):
            relative = relative.removeprefix("install/root/")
        if not relative.startswith(("opt/", "bin/", "sbin/", "usr/")):
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


def stage_runtime_payload(esp_tree: Path, stage: Path, policy_runtime: Path,
                          userland_dir: Path, gptinit: Path,
                          generated_icons_dir: Path,
                          policy_apps: tuple[str, ...]) -> None:
    """Build the writable ext2 live installer root."""
    # Root filesystems and libc: the musl interpreter and runtime libraries
    # are real files under /lib, matching the ELF PT_INTERP contract.
    copy_tree(esp_tree / BIN, stage / BIN)
    copy_tree(esp_tree / SBIN, stage / SBIN)
    copy_tree(esp_tree / LIB, stage / LIB)
    copy_tree(esp_tree / USR, stage / USR)
    if (esp_tree / "opt").is_dir():
        copy_tree(esp_tree / "opt", stage / "opt")
    copy_tree(esp_tree / ETC, stage / ETC)
    layout_directories(stage)
    # Installer-only programs and policy overrides.
    for app in ("imd", "windowd", "desktop", "installer"):
        copy_file(userland_dir / f"{app}.elf",
                  stage / LEONOS_APPS / app / f"{app}.elf")
    copy_file(userland_dir / "busybox.elf", stage / BIN / "busybox")
    copy_file(gptinit, stage / LEONOS_APPS / "gptinit" / "gptinit.elf")
    (stage / LEONOS_APPS / "gptinit" / "manifest.ini").write_text(
        "[app]\nid=gptinit\nname=GPT initializer\nversion=installer\n"
        "category=Installer tools\nexec=gptinit.elf\nentry=0\nterminal=1\n"
        "hidden=1\ncommands=gptinit\n",
        encoding="ascii",
    )
    copy_file(esp_tree / LEONOS_APPS / "dynlinkerror" / "dynlinkerror.elf",
              stage / LEONOS_APPS / "dynlinkerror" / "dynlinkerror.elf")
    for app in policy_apps:
        copy_file(generated_icons_dir / f"{app}.bmp",
                  stage / LEONOS_APPS / app / f"{app}.bmp")
    copy_file(policy_runtime, stage / LEONOS_LIB / "libleonos.so.2")
    for app in ("imd", "windowd", "desktop", "installer", "gptinit"):
        link, target = command_symlink(app, f"{LEONOS_APPS}/{app}/{app}.elf")
        path = stage / link
        if path.is_symlink():
            path.unlink()
        elif path.exists():
            raise ValueError(f"installer command conflicts with a regular file: {path}")
        path.symlink_to(target)
    (stage / ETC / "resolv.conf").write_text("nameserver 1.1.1.1\n",
                                              encoding="ascii")
    layout_directories(stage)
    apply_root_symlinks(stage)


def stage_installed_payloads(esp_tree: Path, destination: Path) -> None:
    """Split normal staging into ext2 root and the minimal FAT32 boot payload.

    The installed root keeps real /bin, /sbin and /lib directories; there is
    deliberately no /lib64.  The ESP gets only GRUB, the loader and the
    /leonos boot files.
    """
    root = destination / "install/root"
    esp = destination / "install/esp"
    copy_tree(esp_tree, root)
    (destination / "install/components.list").write_text(
        "".join(f"{component}\t/{path}\n" for component in ("python", "musl-gcc")
                for path in tool_payload_paths(component)), encoding="ascii")
    shutil.rmtree(root / "EFI", ignore_errors=True)
    shutil.rmtree(root / "grub", ignore_errors=True)
    remove_file(root / "loader.elf")
    shutil.rmtree(root / "leonos", ignore_errors=True)

    copy_file(esp_tree / "EFI/BOOT/BOOTX64.EFI", esp / "EFI/BOOT/BOOTX64.EFI")
    copy_tree(esp_tree / "grub", esp / "grub")
    copy_file(esp_tree / "loader.elf", esp / "loader.elf")
    copy_tree(esp_tree / "leonos", esp / "leonos")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS installer runtime ext2 root")
    parser.add_argument("--out", default="build/install/root.fat")
    parser.add_argument("--stage", default="build/install/root")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--installed-policy-dir", default="build/userland-installer-policy")
    parser.add_argument("--policy-apps", nargs="*", default=("desktop", "settings"))
    parser.add_argument("--userland-dir", default="build/userland")
    parser.add_argument("--gptinit", default="build/userland-installer/gptinit.elf")
    parser.add_argument("--policy-runtime", default="build/userland-installer-policy/libleonos.so.2")
    parser.add_argument("--generated-icons-dir", default="build/generated/app-icons")
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
    out.unlink(missing_ok=True)

    stage_runtime_payload(esp_tree, stage, policy_runtime, userland_dir,
                          gptinit, generated_icons_dir,
                          tuple(args.policy_apps))

    # Installed-system root payload: the same root namespace without the
    # live-installer-only gptinit package.
    stage_installed_payloads(esp_tree, stage)
    for app in args.policy_apps:
        if app not in {"desktop", "settings"}:
            raise ValueError(f"unsupported installer policy app: {app}")
        name = f"{app}.elf"
        copy_file(installed_policy_dir / name,
                  stage / "install/root" / LEONOS_APPS / app / name)
    copy_file(policy_runtime,
              stage / "install/root" / LEONOS_LIB / "libleonos.so.2")
    remove_file(stage / "install/root/etc/license.conf")
    remove_file(stage / "install/root/etc/install.id")
    (stage / "install/root" / VAR_LIB_LEONOS).mkdir(parents=True, exist_ok=True)
    layout_directories(stage)
    layout_directories(stage / "install/root")
    apply_root_symlinks(stage)
    apply_root_symlinks(stage / "install/root")
    share_identical_payload_files(stage)
    write_ext2_root(stage, out, args.size_mib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
