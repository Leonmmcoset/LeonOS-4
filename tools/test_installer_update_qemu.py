#!/usr/bin/env python3
"""Update an isolated copy of an installed test disk through the actual GUI."""
import argparse
from pathlib import Path
import subprocess
import time

from test_installer_accounts_qemu import boot, next_page
from test_installer_window_qemu import wait_log

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disk", type=Path, required=True, help="Read-only input; only its copy is updated")
    parser.add_argument("--iso", type=Path, default=ROOT / "build/images/leonos4-installer.iso")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=2400)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    disk = output / "scratch.raw"
    if disk.exists():
        parser.error("output scratch.raw already exists; use a new output directory")
    subprocess.run(["cp", "--reflink=auto", "--sparse=always", args.disk.resolve(), disk], check=True)
    with boot(output / "update", disk, args.iso.resolve()) as (probe, serial, process):
        wait_log(serial, "[installer.elf] starting installer wizard", process)
        time.sleep(4)
        probe.frame("language")
        for _ in range(4):
            next_page(probe)
        y = 64 if probe.height > 640 else 44
        probe.click(350, y + 192)
        next_page(probe)
        next_page(probe)
        wait_log(serial, "[installer.elf] scan programs list ret=0", process)
        time.sleep(3)
        probe.frame("update-apps")
        next_page(probe)
        probe.click(350, y + 212)
        probe.text("UPDATE")
        probe.frame("confirmation")
        next_page(probe)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            log = serial.read_text(errors="replace")
            if "[installer.elf] update completed successfully" in log:
                probe.frame("updated")
                assert log.count("mount targets disk=") >= 2, "The second mount was not reached"
                print(f"PASS GUI update: repeated mount, payload replacement and completion; {serial}")
                return
            failures = [line for line in log.splitlines()
                        if "[installer.elf]" in line and " ret=-" in line]
            if failures:
                raise AssertionError("Installer failed: " + failures[-1])
            if process.poll() is not None:
                raise RuntimeError("QEMU exited during update")
            time.sleep(2)
        raise AssertionError(f"Update timed out; see {serial}")


if __name__ == "__main__":
    main()
