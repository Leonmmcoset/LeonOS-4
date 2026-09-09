#!/usr/bin/env python3
"""Package a musl SDK without importing any Picolibc headers or objects."""

import argparse
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--png-config", type=Path, required=True)
    parser.add_argument("--ncurses", type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    if stage == ROOT or stage in ROOT.parents or stage == args.prefix.resolve():
        raise SystemExit("SDK staging directory must be separate from the source/sysroot")
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True, exist_ok=True)
    shutil.copytree(args.prefix / "include", stage / "include", dirs_exist_ok=True)
    shutil.copytree(args.prefix / "lib", stage / "lib", dirs_exist_ok=True)
    shutil.copytree(args.prefix / "share/licenses", stage / "share/licenses", dirs_exist_ok=True)
    # UAPI owns wire definitions; libc owns standard C/POSIX declarations.
    for base in (ROOT / "include/uapi", ROOT / "include/leonos",
                 ROOT / "userland/libc/include/leonos"):
        target = stage / "include" if base.name == "uapi" else stage / "include/leonos"
        for source in base.rglob("*.h"):
            destination = target / source.relative_to(base)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
    for source in (ROOT / "third_party/zlib/zlib.h", ROOT / "third_party/zlib/zconf.h",
                   ROOT / "third_party/libpng/png.h", ROOT / "third_party/libpng/pngconf.h",
                   args.png_config):
        shutil.copy2(source, stage / "include" / source.name)
    if args.ncurses:
        for directory in ("include", "lib", "share"):
            shutil.copytree(args.ncurses / directory, stage / directory, dirs_exist_ok=True)
    shutil.copy2(args.runtime, stage / "lib/libleonos.so.2")
    shutil.copy2(args.archive, stage / "lib/libleonos.a")
    (stage / "bin").mkdir(exist_ok=True)
    driver = stage / "bin/leonos-musl-cc"
    shutil.copy2(ROOT / "tools/leonos_musl_cc.py", driver)
    driver.chmod(0o755)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.output, "w:gz", dereference=True) as archive:
        archive.add(stage, arcname="leonos-musl-sdk")


if __name__ == "__main__":
    main()
