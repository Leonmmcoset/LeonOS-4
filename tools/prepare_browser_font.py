#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


DEFAULT_FONT = Path("system/fonts/times.ttf")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Copy the external Times New Roman font used by browser.elf"
    )
    parser.add_argument(
        "--font", type=Path,
    )
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def resolve_font(requested: Path | None) -> Path:
    if requested is not None:
        return requested
    return DEFAULT_FONT


def main() -> int:
    args = parse_args()
    font = resolve_font(args.font)
    if not font.is_file():
        raise FileNotFoundError(
            f"browser font not found: {font}; "
            "restore system/fonts/times.ttf or pass --font PATH"
        )
    with font.open("rb") as source:
        if source.read(4) != b"\x00\x01\x00\x00":
            raise ValueError(f"browser font is not a TrueType glyf font: {font}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(font, args.out)
    print(f"browser font: {font} -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
