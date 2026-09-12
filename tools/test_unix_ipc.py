#!/usr/bin/env python3
"""Compile the real IPC consumer against host Linux headers and socket ABI."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leonos-ipc-") as tmp:
    tmp = Path(tmp)
    headers = tmp / "include" / "leonos"
    headers.mkdir(parents=True)
    (headers / "unix_ipc.h").symlink_to(root / "userland/libc/include/leonos/unix_ipc.h")
    output = tmp / "ipc"
    subprocess.run([
        "cc", "-std=c11", "-O1", "-g", "-D_GNU_SOURCE", "-DLEONOS_USE_MUSL",
        "-fsanitize=address,undefined", "-I" + str(tmp / "include"),
        "tools/tests/unix_ipc_stream_test.c", "userland/libc/src/unix_ipc.c", "-o", str(output),
    ], cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True, timeout=20)
