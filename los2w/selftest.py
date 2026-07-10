"""Lightweight los2w self-tests that do not require a LeonOS image."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

from . import constants as C
from . import structs
from .config import ConfigStore, HostConfig
from .diagnostics import write_report
from .fs import GuestFS
from .logging import LogBuffer


def run_self_tests() -> list[str]:
    lines: list[str] = []
    logger = LogBuffer(lines.append)
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "etc").mkdir()
        (root / "docs").mkdir()
        (root / "docs" / "a.txt").write_text("hello", encoding="utf-8")
        fs = GuestFS(root, language="zh", logger=logger)

        assert fs.guest_abs("docs/a.txt") == "0:/docs/a.txt"
        assert fs.guest_abs("0:/docs/../etc") == "0:/etc"
        try:
            fs.host_path("1:/bad")
            raise AssertionError("drive 1: unexpectedly accepted")
        except ValueError:
            pass

        fd = fs.open("0:/docs/a.txt", C.O_RDONLY, 0)
        assert fd >= 4
        assert fs.read(fd, 5) == b"hello"
        assert fs.close(fd) == 0

        fd = fs.open("0:/etc/locale.conf", C.O_RDONLY, 0)
        assert fd >= 4
        assert fs.read(fd, 32) == b"lang=zh\n"

        entries = fs.list_dir("0:/docs")
        assert not isinstance(entries, int)
        assert entries == [(C.FS_TYPE_FILE, "a.txt")]

        assert structs.GuiAppEvent.SIZE == 36
        assert structs.DIR_ENTRY_SIZE == 132
        assert structs.SocketIO.SIZE == 32
        assert structs.TextLayout.SIZE == 40
        assert structs.TEXT_GLYPH_SIZE == 20
        assert structs.TimeSync.SIZE == 152
        sync = structs.TimeSync(4000, C.NET_STATUS_OK, 0, 1, 1700000000, "pool.ntp.org")
        assert structs.TimeSync.unpack(sync.pack()) == sync
        assert len(structs.pack_net_config()) == 40
        event = structs.GuiAppEvent(window_id=3, type=C.APP_EVENT_KEY_DOWN, keycode=C.KEY_LEFT_SHIFT, pressed=1)
        assert structs.GuiAppEvent.unpack(event.pack()) == event

        store = ConfigStore(root / "los2w-config.json")
        store.save(HostConfig(last_elf="browser.elf", root_dir="guest-root",
                              recent_elfs=["oshlp.elf", "browser.elf"],
                              recent_roots=["old-root", "guest-root"]))
        cfg = store.load()
        assert cfg.recent_elfs == ["browser.elf", "oshlp.elf"]
        assert cfg.recent_roots == ["guest-root", "old-root"]

        report = write_report(root / "report.json", logger, reason="self-test")
        assert json.loads(report.read_text(encoding="utf-8"))["reason"] == "self-test"

    lines.append("self-test ok")
    return lines
