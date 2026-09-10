#!/usr/bin/env python3
"""Boot the actual distribution ISO and exercise its desktop or installer UI."""
import argparse
from pathlib import Path
import re
import socket
import subprocess
import time

from PIL import Image
from run_gcc_probe_qemu import qmp_quit
from test_installer_responsiveness import InstallerProbe

ROOT = Path(__file__).resolve().parents[1]


class ScaledInstallerProbe(InstallerProbe):
    @staticmethod
    def visible_page(frame):
        # The installer may use the scaled 800x600 surface on a 1920x1080 GOP.
        x = round(frame.width * 0.30)
        runs = []
        start = None
        for y in range(round(frame.height * 0.1), round(frame.height * 0.8)):
            red, green, blue = frame.getpixel((x, y))
            highlight = max(red, green, blue) - min(red, green, blue) < 10 and 175 < red < 245
            if highlight and start is None:
                start = y
            if not highlight and start is not None:
                if y - start >= 15:
                    runs.append(start)
                start = None
        if runs:
            scale = frame.height / 600
            return round((runs[0] / scale - 98) / 34)
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", action="store_true")
    args = parser.parse_args()
    kind = "installer" if args.installer else "desktop"
    image = ROOT / "build/images" / ("leonos4-installer.iso" if args.installer else "leonos4.iso")
    out = ROOT / "build/musl-gcc" / kind
    out.mkdir(parents=True, exist_ok=True)
    serial, qmp = out / "serial.log", out / "qmp.sock"
    qmp.unlink(missing_ok=True)
    with (out / "qemu.log").open("w") as errors:
        process = subprocess.Popen([
            "qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-machine", "q35",
            "-m", "4096", "-smp", "2", "-bios", "/usr/share/edk2/x64/OVMF.4m.fd",
            "-display", "none", "-serial", f"file:{serial}",
            "-device", "VGA,xres=1280,yres=720", "-device", "qemu-xhci", "-device", "usb-tablet",
            "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
            "-cdrom", str(image), "-boot", "d", "-qmp", f"unix:{qmp},server=on,wait=off",
            "-no-reboot", "-no-shutdown"], stdout=errors, stderr=errors, cwd=ROOT)
        try:
            if args.installer:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    text = serial.read_text(errors="replace") if serial.exists() else ""
                    if "name=installer.elf" in text and qmp.exists(): break
                    if process.poll() is not None: raise RuntimeError("QEMU exited before installer")
                    time.sleep(0.2)
                time.sleep(8)
                probe = ScaledInstallerProbe(str(qmp), out)
                for expected in (1, 2, 3):
                    probe.move(probe.width - 165, probe.height - 33)
                    probe.button(True)
                    time.sleep(0.08)
                    probe.button(False)
                    time.sleep(0.8)
                    assert probe.visible_page(probe.screenshot(f"page-{expected}")) == expected
                probe.stream.close()
                probe.socket.close()
            else:
                subprocess.run(["python3", "tools/qmp_terminal_smoke.py", "--gcc", str(qmp)],
                               cwd=ROOT, check=True, timeout=240)
                process.wait(timeout=10)
                text = serial.read_text(errors="replace")
                pids = re.findall(r"exec pid=(\d+) path=/tmp/gcc-hello", text)
                assert pids, "Terminal did not execute the GCC output"
                assert any(re.search(rf"scheduler task exited pid={pid} .*code=0", text) for pid in pids), "GCC output did not exit 0"
                with Image.open(ROOT / "build/images/gcc-qmp-smoke.ppm") as frame:
                    frame.save(out / "terminal-gcc.png")
            text = serial.read_text(errors="replace")
            assert "mode=direct fs=ext2" in text, "ISO did not mount its ext2 RAM root"
            assert "KERNEL PANIC" not in text
            print(f"PASS {kind} ISO: ext2 RAM root, " + ("Language/Thanks/Style/Welcome" if args.installer else "OOBE/login/desktop/Terminal/static GCC compilation and execution"))
            print(f"Evidence: {out}")
        finally:
            if process.poll() is None: qmp_quit(qmp, process)
            qmp.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
