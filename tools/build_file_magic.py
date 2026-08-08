#!/usr/bin/env python3
"""Build the upstream libmagic database with the matching file revision."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    if not (source / "configure.ac").is_file() or not (source / "magic").is_dir():
        raise SystemExit(f"libmagic source tree is incomplete: {source}")

    work_parent = Path(tempfile.mkdtemp(prefix="leonos-file-magic-"))
    work = work_parent / "file"
    try:
        # Keep the submodule pristine.  The generated configure files and host
        # objects live only in this disposable copy.
        shutil.copytree(
            source, work,
            ignore=shutil.ignore_patterns(".git", "build", "*.o", "*.lo"),
        )
        run(["autoreconf", "-fi"], work)
        run([
            "./configure",
            "--disable-shared",
            "--disable-zlib",
            "--disable-bzlib",
            "--disable-xzlib",
            "--disable-zstdlib",
            "--disable-lzlib",
            "--disable-lrziplib",
            "--disable-lz4lib",
            "--disable-libseccomp",
            "--disable-landlock",
        ], work)
        jobs = max(1, min(4, os.cpu_count() or 1))
        run(["make", "-j", str(jobs), "all"], work)
        generated = work / "magic/magic.mgc"
        if not generated.is_file():
            raise SystemExit(f"libmagic did not generate {generated}")
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(generated, output)
        stamp = args.stamp.resolve()
        stamp.parent.mkdir(parents=True, exist_ok=True)
        revision = subprocess.run(
            ["git", "-C", str(source), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        stamp.write_text(
            f"file 5.48\nrevision: {revision}\nsize: {output.stat().st_size}\n",
            encoding="ascii",
        )
    finally:
        shutil.rmtree(work_parent, ignore_errors=True)


if __name__ == "__main__":
    main()
