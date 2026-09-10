#!/usr/bin/env python3
"""Test the real reboot syscall without executing host power operations."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="leonos-power-") as directory:
    output = str(Path(directory) / "reboot")
    # Specialize the included dispatcher for SYS_reboot, discarding unrelated
    # kernel handlers. Only the final hardware operations are intercepted.
    subprocess.run([
        "cc", "-std=c11", "-O2", "-flto", "-fwhole-program",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
        "tools/tests/reboot_syscall_test.c", "-o", output,
    ], cwd=ROOT, check=True)
    subprocess.run([output], cwd=ROOT, check=True, timeout=10)
    controls = str(Path(directory) / "controls")
    subprocess.run([
        "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-Iinclude", "-Iinclude/uapi", "-idirafter", "userland/libc/include",
        "tools/tests/power_controls_test.c", "-o", controls,
    ], cwd=ROOT, check=True)
    subprocess.run([controls], cwd=ROOT, check=True, timeout=10)
    daemon = str(Path(directory) / "authd-power")
    subprocess.run([
        "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-idirafter", "userland/libc/include", "-idirafter", "include", "-Iinclude/uapi",
        "tools/tests/authd_power_test.c", "-o", daemon,
    ], cwd=ROOT, check=True)
    subprocess.run([daemon], cwd=ROOT, check=True, timeout=10)
    client = str(Path(directory) / "authd-power-client")
    subprocess.run([
        "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-idirafter", "userland/libc/include", "-idirafter", "include", "-Iinclude/uapi",
        "tools/tests/authd_power_client_test.c", "-o", client,
    ], cwd=ROOT, check=True)
    subprocess.run([client], cwd=ROOT, check=True, timeout=10)
