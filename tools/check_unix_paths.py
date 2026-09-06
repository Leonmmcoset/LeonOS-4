#!/usr/bin/env python3
"""Reject legacy disk-prefixed paths and removed drive ABI names."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_PREFIXES = (
    "third_party/",
    "build/",
    "devtools/components/tcc/upstream/",
    "devtools/components/lua/upstream/",
)
EXCLUDED_FILES = {
    "tools/check_unix_paths.py",
    # This file rejects ':' and '\\' as manifest path separators; the
    # checker's broad legacy-drive patterns are intentional there.
    "userland/libc/src/app_registry.c",
    "devtools/components/tcc/runtime/include/sqlite3.h",
    "devtools/include/sqlite3.h",
}
TEXT_SUFFIXES = {
    ".c",
    ".cfg",
    ".h",
    ".inc",
    ".json",
    ".md",
    ".mk",
    ".py",
    ".rs",
    ".s",
    ".sh",
    ".toml",
    ".txt",
    ".yml",
    ".yaml",
}
LEGACY_PATH = re.compile(r"(?:^|[^A-Za-z0-9])(?:[0-9]|[A-Za-z]):(?:/|\\(?![nrt0\"']))")
FORBIDDEN_TERMS = (
    "root" + "_drive",
    "LEONOS_DISK_" + "DRIVE_NONE",
    "resolve" + "_drive_path",
    "is" + "_drive_path",
    "numeric" + " drive",
    "drive" + " syntax",
    "numbered drives",
    "drive format",
)
LEGACY_PATH_LOGIC = (
    "path[1] == ':'",
    'path[1] == ":"',
    "name[1] == ':'",
    'name[1] == ":"',
    "base_path[1] == ':'",
    'base_path[1] == ":"',
    "filename[0] == '0'",
    'filename[0] == "0"',
    "path[0] == '0'",
    'path[0] == "0"',
    "path[0] == '\\\\'",
    'path[0] == "\\\\"',
)


def tracked_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [entry.decode("utf-8") for entry in result.stdout.split(b"\0") if entry]


def should_scan(relative: str) -> bool:
    return (relative not in EXCLUDED_FILES and
            not relative.startswith(EXCLUDED_PREFIXES) and
            Path(relative).suffix.lower() in TEXT_SUFFIXES)


def main() -> int:
    failures: list[str] = []
    for relative in tracked_files():
        if not should_scan(relative):
            continue
        path = ROOT / relative
        try:
            data = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        except OSError as exc:
            failures.append(f"{relative}: cannot read: {exc}")
            continue

        for line_number, line in enumerate(data.splitlines(), 1):
            if LEGACY_PATH.search(line):
                failures.append(f"{relative}:{line_number}: legacy disk-prefixed path")
            for pattern in LEGACY_PATH_LOGIC:
                if pattern in line:
                    failures.append(f"{relative}:{line_number}: legacy drive path logic")
            for term in FORBIDDEN_TERMS:
                if term in line:
                    failures.append(f"{relative}:{line_number}: removed ABI term {term}")

    if failures:
        print("Unix-path migration check failed:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("Unix-path migration check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
