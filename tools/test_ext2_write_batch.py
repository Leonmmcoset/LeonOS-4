#!/usr/bin/env python3
"""Check real ext2 batched allocation, partial I/O failures and disk consistency."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ext2-write-batch-", dir=ROOT / "build") as directory:
    work = Path(directory)
    executable = work / "batch"
    subprocess.run(["cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                    "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
                    "-Wl,--gc-sections", "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
                    "tools/tests/ext2_write_batch_test.c", "-o", executable], cwd=ROOT, check=True)
    for bs in (1024, 2048, 4096):
        for failure in range(7):
            image = work / f"disk-{bs}-{failure}.ext2"
            subprocess.run(["mke2fs", "-q", "-t", "ext2", "-b", str(bs), "-I", "128",
                            "-g", "1024", "-O", "none,filetype", "-F", image, "16384"], check=True)
            subprocess.run([executable, image, str(failure)], check=True, timeout=30)
            subprocess.run(["e2fsck", "-f", "-n", image], check=True)
