#!/usr/bin/env python3
"""Run the default Python package through the real desktop Terminal."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import time

from run_gcc_probe_qemu import qmp_quit
from test_installer_window_qemu import Probe, installer, wait_log

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", action="store_true", help="smoke the installer UI instead of the desktop")
    args = parser.parse_args()
    kind = "installer" if args.installer else "desktop"
    iso = ROOT / "build/images" / ("leonos4-installer.iso" if args.installer else "leonos4.iso")
    out = ROOT / "build/python" / kind
    out.mkdir(parents=True, exist_ok=True)
    serial = out / "serial.log"
    with tempfile.TemporaryDirectory(prefix="leonos-python-qmp-") as directory, (out / "qemu.log").open("w") as errors:
        qmp = Path(directory) / "qmp.sock"
        process = subprocess.Popen([
            "qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-machine", "q35",
            "-m", "4096", "-smp", "2", "-bios", "/usr/share/edk2/x64/OVMF.4m.fd",
            "-display", "none", "-serial", f"file:{serial}",
            "-device", "VGA,xres=1280,yres=720", "-device", "qemu-xhci", "-device", "usb-tablet",
            "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
            "-cdrom", str(iso), "-boot", "d", "-qmp", f"unix:{qmp},server=on,wait=off",
            "-no-reboot", "-no-shutdown"], cwd=ROOT, stdout=errors, stderr=errors)
        probe = None
        try:
            wait_log(serial, "name=installer.elf" if args.installer else "[oobe.elf] starting first-run", process)
            time.sleep(10)
            probe = Probe(qmp, out)
            if args.installer:
                installer(probe)
                print("PASS Python installer ISO: boot and first four GUI pages")
                return
            probe.text("pythontest")
            probe.key("ret")
            time.sleep(8)
            probe.text("pythontest")
            probe.key("ret")
            time.sleep(5)
            probe.key("meta_l")
            time.sleep(1.5)
            probe.text("terminal")
            probe.key("ret")
            wait_log(serial, "terminal: PTY ready", process)
            time.sleep(4)
            for command in ("python3 --version", "python3 /usr/share/examples/python/hello.py"):
                before = len(serial.read_text(errors="replace"))
                probe.text(command)
                probe.key("ret")
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    text = serial.read_text(errors="replace")[before:]
                    launched = re.search(r"exec pid=(\d+) path=/opt/python/bin/python3\.14 pty=([1-9]\d*)", text)
                    if launched:
                        exited = re.search(rf"scheduler task exited pid={launched[1]} name=python3\.14 code=(\d+)", text)
                        if exited:
                            assert exited[1] == "0", (command, exited[1])
                            break
                    if process.poll() is not None:
                        raise RuntimeError("QEMU exited unexpectedly")
                    time.sleep(0.2)
                else:
                    probe.frame("python-failure")
                    raise AssertionError(f"Python command did not complete: {command}")
                print(f"PASS Terminal command: {command} (pid={launched[1]}, exit=0)")
            time.sleep(2)
            probe.frame("terminal-python")
            assert "KERNEL PANIC" not in serial.read_text(errors="replace")
        finally:
            if probe is not None:
                probe.stream.close()
                probe.socket.close()
            if process.poll() is None:
                qmp_quit(qmp, process)


if __name__ == "__main__":
    main()
