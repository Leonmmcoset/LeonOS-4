#!/usr/bin/env python3
"""Publish a classic ext2 RAM-root image with writable space and intact names."""
from pathlib import Path
import subprocess
import tempfile


def write_ext2_root(stage: Path, out: Path, minimum_mib: int = 64) -> None:
    entries = list(stage.rglob("*"))
    unique = {(p.stat().st_dev, p.stat().st_ino): p for p in entries if p.is_file()}
    allocated = sum(((p.stat().st_size + 4095) // 4096) * 4096 for p in unique.values())
    # Reserve space for directories, inode tables, bitmaps and live writes.
    required = allocated + len(entries) * 512 + sum(p.is_dir() for p in entries) * 4096 + (64 << 20)
    size_mib = max(minimum_mib, ((required + (32 << 20) - 1) // (32 << 20)) * 32)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ext2-root-", dir=out.parent) as directory:
        image = Path(directory) / "root.ext2"
        with image.open("wb") as stream:
            stream.truncate(size_mib << 20)
        subprocess.run(["mke2fs", "-q", "-t", "ext2", "-F", "-b", "4096", "-I", "128",
                        "-O", "none,filetype,sparse_super,large_file", "-m", "0",
                        "-N", str(max(8192, len(entries) * 2)),
                        "-d", str(stage), str(image)], check=True)
        subprocess.run(["e2fsck", "-f", "-n", str(image)], check=True)
        image.replace(out)
