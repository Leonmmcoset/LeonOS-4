#!/usr/bin/env python3
"""Measure real ext2 I/O amplification and check the resulting filesystem."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--write-baseline", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ext2-performance-", dir=ROOT / "build") as directory:
        work = Path(directory)
        image, executable = work / "disk.ext2", work / "performance"
        subprocess.run(["mke2fs", "-q", "-t", "ext2", "-b", "4096", "-I", "128",
                        "-O", "none,filetype", "-F", image, "32768"], check=True)
        subprocess.run(["cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                        "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
                        "tools/tests/ext2_performance_test.c", "-o", executable], cwd=ROOT, check=True)
        output = subprocess.check_output([executable, image], cwd=ROOT, text=True, timeout=180)
        result = [json.loads(line) for line in output.splitlines()]
        print(output, end="")
        subprocess.run(["e2fsck", "-f", "-n", image], check=True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        if args.baseline:
            before = {row["phase"]: row for row in json.loads(args.baseline.read_text())}
            for row in result:
                previous = before[row["phase"]]
                assert row["payload_bytes"] == previous["payload_bytes"]
                assert row["reads"] < previous["reads"] / 2, (previous, row)
                assert row["writes"] <= previous["writes"], (previous, row)
        if args.write_baseline:
            before = {row["phase"]: row for row in json.loads(args.write_baseline.read_text())}
            for row in result:
                previous = before[row["phase"]]
                assert row["payload_bytes"] == previous["payload_bytes"]
                assert row["reads"] <= previous["reads"], (previous, row)
                assert row["writes"] <= previous["writes"], (previous, row)
                if row["phase"] == "sequential-create":
                    assert row["writes"] < previous["writes"] / 2, (previous, row)
                    assert row["write_bytes"] < previous["write_bytes"] / 2, (previous, row)
        print("PASS ext2 data round-trip and e2fsck consistency")


if __name__ == "__main__":
    main()
