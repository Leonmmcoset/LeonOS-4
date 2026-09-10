#!/usr/bin/env python3
"""Pack the normal desktop payload in a bootable ext2 ramdisk."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from make_image import make_root_tree
from make_ext2_root import write_ext2_root


def make_live_tree(tree: Path, stage: Path) -> None:
    """Use exactly the same applications and data as the installed system."""
    make_root_tree(tree, stage, "en")
    for directory in ("bin", "tmp", "root", "proc", "dev", "run/leonos"):
        (stage / directory).mkdir(parents=True, exist_ok=True)
    (stage / "tmp").chmod(0o1777)
    (stage / "system/osmlayer.manifest").write_text(
        "name=osmlayer\nabi=2\nroot=/\nfs=ext2\ngui=desktop.elf\n",
        encoding="ascii",
    )


def write_fat_root(stage: Path, out: Path) -> None:
    """Size for allocated clusters plus writable headroom and publish atomically."""
    allocated = sum(((p.stat().st_size + 511) // 512) * 512
                    for p in stage.rglob("*") if p.is_file())
    size_mib = max(64, ((allocated + (32 << 20) - 1) // (32 << 20) + 1) * 32)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".live-root-", dir=out.parent) as temp:
        image = Path(temp) / "root.fat"
        with image.open("wb") as stream:
            stream.truncate(size_mib << 20)
        subprocess.run(["mkfs.fat", "-F", "32", "-s", "1", "-n", "LEONOSLIVE", str(image)], check=True)
        for item in sorted(stage.iterdir()):
            subprocess.run(["mcopy", "-s", "-i", str(image), str(item), "::/"], check=True)
        image.replace(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="leonos-live-tree-", dir=args.out.parent) as directory:
        stage = Path(directory)
        make_live_tree(args.tree, stage)
        write_ext2_root(stage, args.out)


if __name__ == "__main__":
    main()
