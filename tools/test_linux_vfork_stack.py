#!/usr/bin/env python3
"""Host Linux reference regression for raw clone(CLONE_VFORK), vfork and RLIMIT_STACK."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="leonos-vfork-stack-") as directory:
    output = Path(directory) / "vfork_stack_abi"
    subprocess.run([
        "cc", "-D_GNU_SOURCE", "-std=c11", "-O2", "-g", "-Wall", "-Wextra",
        "-Werror", "-pthread",
        str(ROOT / "tools/tests/vfork_stack_abi_test.c"), "-o", str(output),
    ], cwd=ROOT, check=True)
    # argv[0] must be an absolute executable path for the vfork+exec case.
    subprocess.run([str(output)], cwd=ROOT, check=True, timeout=60)
