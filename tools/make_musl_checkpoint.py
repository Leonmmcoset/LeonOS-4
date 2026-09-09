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
    if args.abi_probes or args.ltp:
        run("mmd", "-i", root, "::/system/tests")
    if args.abi_probes:
        for kind in ("dynamic", "static"):
            run("mcopy", "-o", "-i", root, args.musl / f"tests/musl-abi-{kind}.elf",
                f"::/system/tests/musl-abi-{kind}.elf")
    if args.ltp:
        for source in sorted(args.ltp.glob("*.elf")):
            run("mcopy", "-o", "-i", root, source, f"::/system/tests/{source.name}")
    print(f"Musl checkpoint root: {root}")


if __name__ == "__main__":
    main()
