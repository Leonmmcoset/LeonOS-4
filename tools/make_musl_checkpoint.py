#!/usr/bin/env python3
"""Stage a QEMU boot checkpoint with musl live-installer processes.

The default installer already contains musl. This adds diagnostic programs
without changing the live processes or installation payload.
"""

import argparse
from pathlib import Path
import shutil
import subprocess


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--musl", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--abi-probes", action="store_true")
    parser.add_argument("--ltp", type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    (stage / "install").mkdir(exist_ok=True)
    root = stage / "install/root.fat"
    base = args.base if args.base.is_file() else args.base / "install/root.fat"
    shutil.copy2(base, root)
    with root.open("rb") as stream:
        stream.seek(1080)
        ext2 = stream.read(2) == b"\x53\xef"
    if args.abi_probes or args.ltp:
        if ext2:
            for directory in ("/usr", "/usr/lib", "/usr/lib/leonos",
                              "/usr/lib/leonos/tests"):
                run("debugfs", "-w", "-R", f"mkdir {directory}", root)
        else:
            for directory in ("::/usr", "::/usr/lib", "::/usr/lib/leonos",
                              "::/usr/lib/leonos/tests"):
                run("mmd", "-i", root, directory)

    def add_probe(source):
        if ext2:
            run("debugfs", "-w", "-R",
                f"write {source.resolve()} /usr/lib/leonos/tests/{source.name}", root)
        else:
            run("mcopy", "-o", "-i", root, source,
                f"::/usr/lib/leonos/tests/{source.name}")

    if args.abi_probes:
        for kind in ("dynamic", "static"):
            add_probe(args.musl / f"tests/musl-abi-{kind}.elf")
    if args.ltp:
        for source in sorted(args.ltp.glob("*.elf")):
            add_probe(source)
    print(f"Musl checkpoint root: {root}")


if __name__ == "__main__":
    main()
