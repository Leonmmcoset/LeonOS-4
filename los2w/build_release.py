"""Build a Windows los2w distribution with PyInstaller."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "los2w"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build a los2w Windows distribution.")
    parser.add_argument("--out", type=Path, default=ROOT / "dist", help="Output directory")
    parser.add_argument("--onefile", action="store_true", help="Build one executable instead of a folder")
    args = parser.parse_args(argv)
    command = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--clean",
        "--windowed",
        "--name",
        "los2w",
        "--paths",
        str(ROOT),
        "--collect-all",
        "unicorn",
        "--distpath",
        str(args.out),
        "--workpath",
        str(ROOT / "build" / "los2w-pyinstaller"),
        "--specpath",
        str(ROOT / "build" / "los2w-pyinstaller"),
    ]
    command.append("--onefile" if args.onefile else "--onedir")
    command.append(str(PACKAGE / "entry.py"))
    try:
        return subprocess.run(command, cwd=ROOT, check=False).returncode
    except ModuleNotFoundError:
        print("PyInstaller is required. Run: py -m pip install -r los2w/requirements-release.txt")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
