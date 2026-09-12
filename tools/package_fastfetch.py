#!/usr/bin/env python3
"""Download, verify and stage the static musl Fastfetch release for LeonOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "2.68.1"
BINARY_SHA256 = "25107efd56d0286059487bab17d964a6ec72275263de2ab09095a46637b06be1"
BINARY_NAME = "fastfetch-2.68.1-leonos-x86_64-linux-musl"
BINARY_URL = "https://github.com/VasilyZa/fastfetch/releases/download/2.68.1/fastfetch"
CACHE = ROOT / "buildsystem/deps/fastfetch" / BINARY_NAME


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_binary(source: Path) -> None:
    if not source.is_file():
        raise SystemExit(f"Fastfetch binary is missing: {source}")
    if digest(source) != BINARY_SHA256:
        raise ValueError(f"Fastfetch SHA256 mismatch: {source}")
    with source.open("rb") as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01" or
            struct.unpack_from("<HH", header, 16) not in ((2, 62), (3, 62))):
        raise ValueError(f"Fastfetch binary is not native x86-64 ELF: {source}")
    readelf = subprocess.check_output(("readelf", "-l", "-d", str(source)), text=True)
    if "INTERP" in readelf or "(NEEDED)" in readelf:
        raise SystemExit("Fastfetch binary contains a dynamic interpreter or dependency")


def copy_binary(source: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".fastfetch-", dir=output.parent) as directory:
        temporary = Path(directory) / "fastfetch"
        shutil.copyfile(source, temporary)
        verify_binary(temporary)
        temporary.chmod(0o755)
        temporary.replace(output)


def obtain_binary(cache: Path, source: Path | None) -> None:
    if source is not None and source.resolve() != cache.resolve():
        copy_binary(source, cache)
    elif source is None and not cache.exists():
        cache.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".fastfetch-download-", dir=cache.parent) as directory:
            candidate = Path(directory) / "fastfetch"
            subprocess.run([
                "curl", "--proxy", "http://127.0.0.1:12334", "--fail", "--location",
                "--proto", "=https", "--proto-redir", "=https",
                "--retry", "3", "--connect-timeout", "30", "--max-time", "180",
                "--output", str(candidate), BINARY_URL,
            ], check=True)
            verify_binary(candidate)
            candidate.chmod(0o755)
            candidate.replace(cache)
    verify_binary(cache)


def package(source: Path, output: Path, stamp: Path) -> None:
    source = source.resolve()
    verify_binary(source)
    copy_binary(source, output)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({
        "url": BINARY_URL,
        "version": VERSION,
        "sha256": digest(source),
        "linkage": "static",
        "logo": "LeonOS",
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--cache", type=Path, default=CACHE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    args = parser.parse_args()
    obtain_binary(args.cache, args.source)
    package(args.cache, args.output, args.stamp)
