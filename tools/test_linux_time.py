#!/usr/bin/env python3
"""Exercise actual native nanosleep state without a libc adapter."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leonos-time-") as tmp:
    output = Path(tmp) / "time"
    subprocess.run(["cc", "-std=c11", "-g", "-O1", "-fsanitize=address,undefined",
                    "-fno-pie", "-no-pie", "-Iinclude", "-Iinclude/uapi",
                    "-Ikernel/ntclks/include", "tools/tests/nanosleep_state_test.c",
                    "-o", output], cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True, timeout=20)
