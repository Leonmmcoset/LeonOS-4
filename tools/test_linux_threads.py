#!/usr/bin/env python3
"""Run kernel futex queue contracts independently of a libc wrapper."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leonos-threads-") as tmp:
    for test in ("futex_queue", "process_signal_queue", "signal_info_queue", "signalfd",
                 "signal_address_space", "resource_limits", "membarrier_cpu"):
        output = str(Path(tmp) / test)
        subprocess.run([
            "cc", "-std=c11", "-pthread", "-g", "-O1", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-fno-pie", "-no-pie",
            "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
            *(["-D_GNU_SOURCE"] if test == "signal_address_space" else []),
            f"tools/tests/{test}_test.c", "kernel/ntclks/syscall_sysv_sem.c", "-o", output,
        ], cwd=root, check=True)
        subprocess.run([output], cwd=root, check=True, timeout=20)
    output = str(Path(tmp) / "signal_queue_abi")
    subprocess.run(["cc", "-std=c11", "-pthread", "-O2", "-Wall", "-Wextra",
                    "tools/tests/signal_queue_abi_test.c", "-o", output], cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True, timeout=20)
    output = str(Path(tmp) / "signalfd_abi")
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra",
                    "tools/tests/signalfd_abi_test.c", "-o", output], cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True, timeout=20)
