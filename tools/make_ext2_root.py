#!/usr/bin/env python3
"""Publish a classic ext2 RAM-root image with writable space and intact names."""
import argparse
import os
from pathlib import Path
import stat
import sys
import subprocess
import tempfile
from image_test_accounts import apply_test_home_ownership


def populate_ext2(stage: Path, image: Path, inode_count: int) -> None:
    """Populate an existing image with root-owned payload, without host chown.

    fakeroot intercepts both lchown and mke2fs's stat calls in the child, so
    regular files, hard links, symlinks and directories all become root:root.
    This is shared by installed-disk, Live and installer image creation.
    """
    subprocess.run(["fakeroot", "--", sys.executable, str(Path(__file__).resolve()),
                    "--populate", str(stage.resolve()), str(image.resolve()),
                    "--inodes", str(inode_count)], check=True)


def _populate_in_fakeroot(stage: Path, image: Path, inode_count: int) -> None:
    if not os.environ.get("FAKEROOTKEY"):
        raise RuntimeError("image ownership normalization requires fakeroot")
    # Never follow payload links out of the tree, including absolute guest links.
    os.chown(stage, 0, 0, follow_symlinks=False)
    for directory, dirs, files in os.walk(stage, followlinks=False):
        for name in dirs + files:
            os.chown(Path(directory) / name, 0, 0, follow_symlinks=False)
    apply_test_home_ownership(stage)
    subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", "4096", "-I", "128",
                    "-O", "none,filetype,sparse_super,large_file", "-m", "0",
                    "-E", "root_owner=0:0", "-N", str(inode_count),
                    "-d", str(stage), str(image)], check=True)


def write_ext2_root(stage: Path, out: Path, minimum_mib: int = 64) -> None:
    entries = [p.lstat() for p in stage.rglob("*")]
    unique = {(s.st_dev, s.st_ino): s for s in entries if stat.S_ISREG(s.st_mode)}
    allocated = sum(((s.st_size + 4095) // 4096) * 4096 for s in unique.values())
    # Reserve space for directories, inode tables, bitmaps and live writes.
    required = allocated + len(entries) * 512 + sum(stat.S_ISDIR(s.st_mode) for s in entries) * 4096 + (64 << 20)
    size_mib = max(minimum_mib, ((required + (32 << 20) - 1) // (32 << 20)) * 32)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ext2-root-", dir=out.parent) as directory:
        image = Path(directory) / "root.ext2"
        with image.open("wb") as stream:
            stream.truncate(size_mib << 20)
        populate_ext2(stage, image, max(8192, len(entries) * 2))
        subprocess.run(["e2fsck", "-f", "-n", str(image)], check=True)
        image.replace(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--populate", nargs=2, type=Path, required=True,
                        metavar=("STAGE", "IMAGE"))
    parser.add_argument("--inodes", type=int, required=True)
    arguments = parser.parse_args()
    _populate_in_fakeroot(*arguments.populate, arguments.inodes)
