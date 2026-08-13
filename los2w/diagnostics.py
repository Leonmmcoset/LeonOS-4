"""Local crash and compatibility reports for los2w."""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def _emulator_data(emulator) -> dict[str, Any]:
    if emulator is None:
        return {}
    dispatcher = getattr(emulator, "dispatcher", None)
    snapshot = getattr(dispatcher, "compatibility_snapshot", lambda: {})()
    registers = getattr(emulator, "register_snapshot", lambda: {})()
    loaded = getattr(emulator, "loaded", None)
    processes = getattr(getattr(emulator, "process_manager", None), "processes", {})
    return {
        "elf": str(getattr(emulator, "elf_path", "")),
        "root": str(getattr(emulator, "root_dir", "")),
        "entry": f"0x{getattr(loaded, 'entry', 0):x}",
        "image": f"0x{getattr(loaded, 'low', 0):x}-0x{getattr(loaded, 'high', 0):x}",
        "exit_code": getattr(emulator, "exit_code", None),
        "fault": getattr(emulator, "fault_message", None),
        "registers": registers,
        "compatibility": snapshot,
        "pid": getattr(emulator, "pid", None),
        "ppid": getattr(emulator, "ppid", None),
        "processes": {
            str(pid): {"elf": str(getattr(proc, "elf_path", "")),
                       "ppid": getattr(proc, "ppid", None),
                       "exit_code": getattr(proc, "exit_code", None)}
            for pid, proc in processes.items()
        },
    }


def report_data(logger, emulator=None, reason: str | None = None) -> dict[str, Any]:
    return {
        "format": "los2w-diagnostic-v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "reason": reason or "manual export",
        "emulator": _emulator_data(emulator),
        "log": list(getattr(logger, "lines", [])),
    }


def write_report(path: str | Path, logger, emulator=None, reason: str | None = None) -> Path:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report_data(logger, emulator, reason), ensure_ascii=False, indent=2), encoding="utf-8")
    return target


def default_report_path(directory: str | Path, prefix: str = "los2w-report") -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path(directory) / f"{prefix}-{stamp}.json"
