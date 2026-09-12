#!/usr/bin/env python3
"""Fetch fixed official authentication sources through the required proxy."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import tarfile
import tempfile
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "configs/auth-upstream.json"
PROXY = "http://127.0.0.1:12334"


def verify(path: Path, expected: str) -> None:
    with path.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != expected:
        raise ValueError(f"{path.name}: SHA256 {actual}, expected {expected}")


def verify_source_tree(archive: Path, destination: Path, allow_generated: bool) -> None:
    expected = set()
    with tarfile.open(archive) as source:
        for member in source:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or path.parts[0] != destination.name:
                raise ValueError(f"Unexpected archive path: {member.name}")
            relative = Path(*path.parts[1:])
            expected.add(relative)
            expected.update(relative.parents)
            actual = destination / relative
            for parent in actual.parents:
                if parent == destination.parent:
                    break
                if parent.is_symlink():
                    raise ValueError(f"Source directory replaced with a symlink: {parent}")
            try:
                mode = actual.lstat().st_mode
            except FileNotFoundError as error:
                raise ValueError(f"Missing source: {actual}") from error
            if member.isdir():
                valid = stat.S_ISDIR(mode)
            elif member.issym():
                # The standard extraction filter normalizes safe link targets.
                filtered = tarfile.data_filter(member, str(destination.parent))
                valid = stat.S_ISLNK(mode) and str(actual.readlink()) == filtered.linkname
            elif member.isfile() or member.islnk():
                valid = stat.S_ISREG(mode)
                if valid:
                    with source.extractfile(member) as packed, actual.open("rb") as unpacked:
                        valid = hashlib.file_digest(packed, "sha256").digest() == \
                                hashlib.file_digest(unpacked, "sha256").digest()
            else:
                valid = False
            if not valid:
                raise ValueError(f"Source differs from verified archive: {actual}")
    if not allow_generated:
        for path in destination.rglob("*"):
            if path.relative_to(destination) not in expected:
                raise ValueError(f"Unexpected file in upstream source tree: {path}")


def fetch(name: str, entry: dict, cache: Path, sources: Path, *, allow_generated: bool = False) -> Path:
    cache.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    archive = cache / Path(urlsplit(entry["url"]).path).name
    if not archive.exists():
        with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=cache) as tmp:
            downloaded = Path(tmp) / archive.name
            subprocess.run(["curl", "--proxy", PROXY, "--noproxy", "",
                            "--fail", "--location", "--retry", "2",
                            "--connect-timeout", "20", "--max-time", "300",
                            "--output", str(downloaded), entry["url"]], check=True)
            verify(downloaded, entry["sha256"])
            downloaded.rename(archive)
    verify(archive, entry["sha256"])
    destination = sources / entry["directory"]
    if not destination.exists():
        with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=sources) as tmp:
            with tarfile.open(archive) as source:
                source.extractall(tmp, filter="data")
            extracted = Path(tmp) / entry["directory"]
            if not (extracted / entry["license_file"]).is_file():
                raise ValueError(f"{name}: missing upstream license")
            extracted.rename(destination)
    verify_source_tree(archive, destination, allow_generated)
    if not (destination / entry["license_file"]).is_file():
        raise ValueError(f"{name}: missing upstream license")
    print(f"{name} {entry['version']} sha256={entry['sha256']} {destination}", flush=True)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=ROOT / "buildsystem/deps/auth")
    parser.add_argument("--sources", type=Path, default=ROOT / "build/auth-upstream/src")
    parser.add_argument("packages", nargs="*")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    for name in args.packages or manifest:
        if name not in manifest:
            parser.error(f"unknown package: {name}")
        fetch(name, manifest[name], args.cache.resolve(), args.sources.resolve())


if __name__ == "__main__":
    main()
