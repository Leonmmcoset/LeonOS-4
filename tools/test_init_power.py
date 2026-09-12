#!/usr/bin/env python3
"""Exercise init's real signal handling; reboot and sync never reach the host."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="init-power-") as directory:
    binary = Path(directory) / "init-power"
    subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
                    "-Wl,--gc-sections", "-fsanitize=address,undefined",
                    "-Iinclude", "-Iinclude/uapi", "-idirafter", "userland/libc/include",
                    "tools/tests/init_power_test.c", "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
