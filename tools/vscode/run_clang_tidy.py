#!/usr/bin/env python3
"""Run clang-tidy for a selected LeonOS source region from WSL."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path

from generate_compile_commands import REGION_PATTERNS, all_sources, generate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--region", choices=[*REGION_PATTERNS, "all"], default="all")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument(
        "--checks",
        default="bugprone-*,performance-*,portability-*,clang-analyzer-core-*",
        help="clang-tidy checks expression",
    )
    parser.add_argument("--fix", action="store_true", help="apply clang-tidy fixes")
    parser.add_argument("--warnings-as-errors", action="store_true")
    args = parser.parse_args()
    if shutil.which("clang-tidy") is None:
        raise SystemExit("clang-tidy is not installed in the active Linux/WSL environment")
    root = args.root.absolute()
    database = root / "build/vscode/compile_commands.json"
    generate(root, args.region, database)
    files = [path.relative_to(root).as_posix() for path in all_sources(root, args.region)
             if path.suffix in {".c", ".cpp", ".cc", ".cxx"}]
    if not files:
        return 0
    command = [
        "clang-tidy", "-p", str(database),
        f"--checks={args.checks}",
        "--header-filter=(boot|drivers|include|kernel|middlelayer|userland|devtools)/.*",
    ]
    if args.fix:
        command.append("--fix")
    if args.warnings_as_errors:
        command.append("--warnings-as-errors=*")
    command.extend(files)
    return subprocess.run(command, cwd=root).returncode


if __name__ == "__main__":
    raise SystemExit(main())
