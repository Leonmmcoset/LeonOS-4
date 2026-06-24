#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = ROOT / "configs" / "default.conf"
CONFIG = ROOT / ".config"


def parse(path: Path) -> list[tuple[str, str]]:
    items: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        items.append((key, value))
    return items


def main() -> int:
    if not CONFIG.exists():
        CONFIG.write_text(DEFAULTS.read_text(encoding="utf-8"), encoding="utf-8")

    items = parse(CONFIG)
    print("LeonOS 4 menuconfig")
    print("Press Enter to keep the current value, y/n to toggle booleans.")

    updated: list[tuple[str, str]] = []
    for key, value in items:
        if value in {"y", "n"}:
            answer = input(f"{key} [{value}]: ").strip().lower()
            if answer in {"y", "n"}:
                value = answer
        else:
            answer = input(f"{key} [{value}]: ").strip()
            if answer:
                value = answer
        updated.append((key, value))

    CONFIG.write_text("\n".join(f"{k}={v}" for k, v in updated) + "\n", encoding="utf-8")
    print("Wrote .config")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
