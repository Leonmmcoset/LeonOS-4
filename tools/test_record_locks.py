#!/usr/bin/env python3
"""Exercise production record locks with sanitizer and injected allocation failure."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leonos-record-locks-") as directory:
    binary = str(Path(directory) / "locks")
    subprocess.run(["cc", "-std=c11", "-O2", "-g", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
                    "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
                    "tools/tests/record_locks_test.c", "-o", binary], cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=30)
