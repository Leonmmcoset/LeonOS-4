"""Check a running installer's redraw latency through an isolated QEMU QMP socket.

Start the installer at its language page with no writable disk attached. This
test visits only Language, Thanks, Style, and Welcome; it never starts a copy.
Requires Pillow. Screenshots and measured response times are saved in --output.
"""

import argparse
import json
from pathlib import Path
import socket
import time

from PIL import Image


class InstallerProbe:
    def __init__(self, socket_path, output):
        self.output = output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        self.socket.connect(socket_path)
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.stream.readline()
        self.command("qmp_capabilities")
        frame = self.screenshot("initial")
        self.width, self.height = frame.size
        assert self.visible_page(frame) == 0, "Start at the installer language page"

    def command(self, name, arguments=None):
        request = {"execute": name}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode() + b"\n")
        while True:
            reply = json.loads(self.stream.readline())
            if "error" in reply:
                raise RuntimeError(reply)
            if "return" in reply:
                return reply["return"]

    def move(self, x, y):
        self.command("input-send-event", {"events": [
            {"type": "abs", "data": {
                "axis": "x", "value": round(x * 32767 / (self.width - 1))}},
            {"type": "abs", "data": {
                "axis": "y", "value": round(y * 32767 / (self.height - 1))}},
        ]})

    def button(self, down):
        self.command("input-send-event", {"events": [
            {"type": "btn", "data": {"button": "left", "down": down}},
        ]})

    def screenshot(self, name):
        path = self.output / "frame.ppm"
        self.command("screendump", {"filename": str(path)})
        with Image.open(path) as source:
            frame = source.convert("RGB")
        frame.save(self.output / f"{name}.png")
        return frame

    @staticmethod
    def visible_page(frame):
        # Sample the highlight away from both text and the moving pointer.
        for page in range(10):
            red, green, blue = frame.getpixel((190, 100 + page * 34))
            if min(red, green, blue) > 180:
                return page
        return None

    def run(self, max_latency):
        results = []
        observed = set()
        for expected in (1, 2, 3):
            self.move(self.width - 165, self.height - 33)
            time.sleep(0.05)
            started = time.monotonic()
            self.button(True)
            release_at = started + 0.06
            released = False
            first_visible = None
            next_frame = started
            index = 0
            # Keep motion arriving more often than the old 100 ms idle wait.
            # The first click must be painted even while that motion continues.
            while time.monotonic() - started < 1.5:
                now = time.monotonic()
                if not released and now >= release_at:
                    self.button(False)
                    released = True
                if released:
                    self.move(500 + index % 100, 450 + index % 50)
                else:
                    self.move(self.width - 165 + index % 3, self.height - 33)
                if now >= next_frame:
                    frame = self.screenshot(f"step-{expected}-{index:03d}")
                    visible = self.visible_page(frame)
                    observed.add(visible)
                    if visible == expected and first_visible is None:
                        first_visible = time.monotonic() - started
                    next_frame = time.monotonic() + 0.08
                index += 1
                time.sleep(0.015)
            if not released:
                self.button(False)
            results.append({"page": expected, "latency_ms":
                            round(first_visible * 1000, 1) if first_visible is not None else None})
        time.sleep(0.2)
        final_page = self.visible_page(self.screenshot("settled"))
        report = {"clicks": results, "observed_pages": sorted(
            value for value in observed if value is not None), "final_page": final_page}
        (self.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2), flush=True)
        assert final_page == 3, "Click order/count changed"
        assert all(item["latency_ms"] is not None and
                   item["latency_ms"] <= max_latency * 1000 for item in results), (
            "Installer deferred click feedback while mouse motion continued")

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qmp_socket")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-latency", type=float, default=0.5)
    args = parser.parse_args()
    probe = InstallerProbe(args.qmp_socket, args.output)
    try:
        probe.run(args.max_latency)
    finally:
        probe.close()


if __name__ == "__main__":
    main()
